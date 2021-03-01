//
// Created by Michael Davis on 07/04/2020.
//

/*
 * Copyright (c) 2013-2015, University of Lugano
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the copyright holders nor the names of it
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "client_benchmarker.h"
#include "client_value.h"
#include <paxos.h>
#include <errno.h>
#include <ev_epoch_paxos.h>
#include <stdlib.h>
#include "unistd.h"
#include <time.h>
#include <string.h>
#include <signal.h>
#include <event2/event.h>
#include <netinet/tcp.h>
#include <assert.h>

struct epoch_client
{
    bool synced;
    uint32_t id;
    uint32_t value_size;
    uint32_t max_outstanding;
    struct client_benchmarker* benchmarker;
    char* send_buffer;
    struct event_base* base;
    struct bufferevent* bev;
    struct event* stats_ev;
    struct timeval stats_interval;
    struct event* sig;
    struct ev_epoch_learner* learner;
};

static void
handle_sigint(int sig, short ev, void* arg)
{
    struct epoch_client* client= arg;
    client_benchmarker_free(&client->benchmarker);
    printf("Caught signal %d\n", sig);
    event_base_loopexit(client->base, NULL);
}


static void
client_submit_value(struct epoch_client* c)
{
 //   paxos_log_debug("Trying to submit new client value");
    client_value_generate((struct client_value **) &c->send_buffer, c->value_size, c->id);
    struct client_value* v = (struct client_value*) c->send_buffer;
    uint32_t size = sizeof(struct client_value) + v->size;
    while (!client_benchmarker_register_value(c->benchmarker, v))
        v->uid = rand();
    epoch_paxos_submit_client_value(c->bev, c->send_buffer, size);
    paxos_log_debug("Submitted new client value");
}

static void
client_submit_sync(struct epoch_client* c) {
    struct client_value* v = (struct client_value*) c->send_buffer;
    v->client_id = c->id;
    v->uid = 0;
    strncpy(v->value, "SYNC", 5);
    uint32_t size = sizeof(*v) + 5;
    epoch_paxos_submit_client_value(c->bev, c->send_buffer, size);
}


static void
on_deliver(char* value, size_t size, void* arg)
{
    struct epoch_client* c = arg;
    struct client_value* v = (struct client_value*)value;
    paxos_log_debug("Checking whether delivered value is outstanding");

    if (strncmp (value, "NOP.", size) != 0) {
        if (!c->synced && v->client_id == c->id && v->uid == 0 && strncmp(v->value, "SYNC", 4) == 0) {
            for (int i = 0; i < c->max_outstanding; ++i)
                client_submit_value(c);
            c->synced = true;
            return;
        } else if (!c->synced) {
            client_submit_sync(c);
            return;
        }


        if (client_benchmarker_is_outstanding(c->benchmarker, v)) {
            paxos_log_debug("Client Value delivered.");
            bool found = client_benchmarker_close_value_and_update_stats(c->benchmarker, v);
            // assert(found);
            client_submit_value(c);

        } else {
            if (v->client_id == c->id) {
                paxos_log_debug("Delivered value out of date.");
            } else {
                paxos_log_debug("Delivered value of another client.");
            }
        }
    }

}

static void
on_stats(evutil_socket_t fd, short event, void *arg)
{
    struct epoch_client* c = arg;
    client_benchmarker_print_and_reset_stats(c->benchmarker);
    event_add(c->stats_ev, &c->stats_interval);
}

static void
on_connect(struct bufferevent* bev, short events, void* arg)
{
    struct epoch_client* c = arg;
    if (events & BEV_EVENT_CONNECTED) {
        printf("Connected to proposer\n");
        c->synced = false;
        client_submit_sync(c);
//        for (int i = 0; i < c->max_outstanding; ++i)
           // client_submit_value(c);
    } else {
        printf("%s\n", evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));
    }
}

static struct bufferevent*
connect_to_proposer(struct epoch_client* c, const char* config, int proposer_id)
{
    struct bufferevent* bev;
    struct evpaxos_config* conf = evpaxos_config_read(config);
    if (conf == NULL) {
        printf("Failed to read config file %s\n", config);
        return NULL;
    }
    struct sockaddr_in addr = evpaxos_proposer_address(conf, proposer_id);
    bev = bufferevent_socket_new(c->base, -1, BEV_OPT_CLOSE_ON_FREE);
    bufferevent_setcb(bev, NULL, NULL, on_connect, c);
    bufferevent_enable(bev, EV_READ|EV_WRITE);
    bufferevent_socket_connect(bev, (struct sockaddr*)&addr, sizeof(addr));
    int flag = 1;
    setsockopt(bufferevent_getfd(bev), IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int));
    return bev;
}

static struct epoch_client *
make_epoch_client(const char *config, int proposer_id, uint32_t outstanding, uint32_t value_size,
                  const char *latency_record_output_path)
{
    struct epoch_client* c;
    c = malloc(sizeof(struct epoch_client));
    c->base = event_base_new();

    struct evpaxos_config* ev_config = evpaxos_config_read(config);

    c->id = rand();
    //paxos_config.learner_catch_up = 0;
    c->value_size = value_size;
    c->max_outstanding = outstanding;
    c->send_buffer = malloc(sizeof(struct client_value) + sizeof(char) * value_size);


    struct timeval settle_in_time = (struct timeval) {.tv_sec = paxos_config.settle_in_time, .tv_usec = 0};
    uint32_t number_of_latencies_to_record = paxos_config.number_of_latencies_to_record;
    c->benchmarker = client_benchmarker_new(c->id, number_of_latencies_to_record, settle_in_time,
                                            latency_record_output_path);

    c->stats_interval = (struct timeval){1, 0};
    c->stats_ev = evtimer_new(c->base, on_stats, c);
    event_add(c->stats_ev, &c->stats_interval);

    c->learner = ev_epoch_learner_init(config, on_deliver, c, c->base, proposer_id);
    c->bev = connect_to_proposer(c, config, proposer_id);
    c->sig = evsignal_new(c->base, SIGINT, handle_sigint, c);
    evsignal_add(c->sig, NULL);

    free(ev_config);


    if (c->bev == NULL)
        exit(1);
    return c;
}

static void
client_free(struct epoch_client* c)
{
    free(c->send_buffer);
    bufferevent_free(c->bev);
    event_free(c->stats_ev);
    event_free(c->sig);
    event_base_free(c->base);
    if (c->learner)
        ev_epoch_learner_free(&c->learner);
    free(c);
}

static void
start_epoch_client(const char *config, int proposer_id, uint32_t outstanding, uint32_t value_size,
                   const char *latency_record_output_path)
{
    struct epoch_client* client;
    client = make_epoch_client(config, proposer_id, outstanding, value_size, latency_record_output_path);
    signal(SIGPIPE, SIG_IGN);
    event_base_dispatch(client->base);
    client_free(client);
}

static void
usage(const char* name)
{
    printf("Usage: %s [path/to/paxos.conf] [-h] [-o] [-v] [-p]\n", name);
    printf("  %-30s%s\n", "-h, --help", "Output this message and exit");
    printf("  %-30s%s\n", "-o, --max_outstanding #", "Number of max_outstanding client values");
    printf("  %-30s%s\n", "-v, --value-size #", "Size of client value (in bytes)");
    printf("  %-30s%s\n", "-p, --proposer-id #", "d of the proposer to connect to");
    exit(1);
}

int
main(int argc, char const *argv[])
{
    int i = 1;
    int proposer_id = 0;
    uint32_t outstanding = 1;
    uint32_t value_size = 64;
    struct timeval seed;
    const char* config = "../paxos.conf";
    const char* latency_record_path = "./latencies_recorded.txt";

    if (argc > 1 && argv[1][0] != '-') {
        config = argv[1];
        i++;
    }

    while (i != argc) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
            usage(argv[0]);
        else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--max_outstanding") == 0)
            outstanding = atoi(argv[++i]);
        else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--value-size") == 0)
            value_size = atoi(argv[++i]);
        else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--proposer-id") == 0)
            proposer_id = atoi(argv[++i]);
        else if (strcmp(argv[i], "-lop") == 0 || strcmp(argv[i], "--latency-output-path") == 0)
            latency_record_path = argv[++i];
        else
            usage(argv[0]);
        i++;
    }

    gettimeofday(&seed, NULL);
    srand(seed.tv_usec ^ getpid());
    start_epoch_client(config, proposer_id, outstanding, value_size, latency_record_path);

    return 0;
}

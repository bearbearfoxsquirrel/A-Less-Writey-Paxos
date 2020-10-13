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


#include <paxos.h>
#include "ev_epoch_paxos.h"
#include <errno.h>
#include <stdlib.h>
#include "unistd.h"
#include <time.h>
#include <string.h>
#include <signal.h>
#include <event2/event.h>
#include <netinet/tcp.h>
#include <latency_recorder.h>

#define MAX_VALUE_SIZE 8192


struct client_value
{
    int client_id;
    struct timeval t;
    size_t size;
    char value[0];
    int uid;
};

struct stats
{
    long min_latency;
    long max_latency;
    long avg_latency;
    int delivered_count;
    size_t delivered_bytes;
};

struct epoch_client
{
    // todo add in break in time - don't record for this long
    int id;
    int value_size;
    int max_outstanding;
    int current_outstanding;
    char* send_buffer;
    struct stats stats;
    struct event_base* base;
    struct bufferevent* bev;
    struct event* stats_ev;
    struct timeval stats_interval;
    struct event* sig;
    struct ev_epoch_learner* learner;

    struct latency_recorder* latency_recorder;

    int* outstanding_client_value_ids;
};

static void
handle_sigint(int sig, short ev, void* arg)
{
//    struct event_base* base = arg;
    struct epoch_client* client= arg;
    latency_recorder_free(&client->latency_recorder);
    printf("Caught signal %d\n", sig);
    event_base_loopexit(client->base, NULL);
}

static void
random_string(char *s, const int len)
{
    int i;
    static const char alphanum[] =
            "0123456789abcdefghijklmnopqrstuvwxyz";
    for (i = 0; i < len-1; ++i)
        s[i] = alphanum[rand() % (sizeof(alphanum) - 1)];
    s[len-1] = 0;
}

static void
client_submit_value(struct epoch_client* c)
{
    struct client_value* v = (struct client_value*)c->send_buffer;
    v->client_id = c->id;
    gettimeofday(&v->t, NULL);
    v->size = c->value_size;
    random_string(v->value, v->size);
    v->uid = rand();
    c->outstanding_client_value_ids[c->current_outstanding++] = v->uid;
    size_t size = sizeof(struct client_value) + v->size;
    epoch_paxos_submit_client_value(c->bev, c->send_buffer, size);
    paxos_log_debug("Submitted new client value");


}

// Returns t2 - t1 in microseconds.
static long
timeval_diff(struct timeval* t1, struct timeval* t2)
{
    long us;
    us = (t2->tv_sec - t1->tv_sec) * 1e6;
    if (us < 0) return 0;
    us += (t2->tv_usec - t1->tv_usec);
    return us;
}

static void
update_stats(struct latency_recorder* recorder, struct stats* stats, struct client_value* delivered, size_t size)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    long lat = timeval_diff(&delivered->t, &tv);
    stats->delivered_count++;
    stats->delivered_bytes += size;
    stats->avg_latency = stats->avg_latency +
                         ((lat - stats->avg_latency) / stats->delivered_count);
    if (stats->min_latency == 0 || lat < stats->min_latency)
        stats->min_latency = lat;
    if (lat > stats->max_latency)
        stats->max_latency = lat;

    latency_recorder_record(recorder, lat);
}

//todo refactor so there is a struct uid made of a value number and a client id
static bool is_value_uid_awaiting(const int* outstanding_uids, int number_of_awaiting_values, int value_uid) {
    for (int i = 0; i < number_of_awaiting_values; i++) {
        if (outstanding_uids[i] == value_uid) {
            return true;
        }
    }
    return false;
}
static void
on_deliver(char* value, size_t size, void* arg)
{
    struct epoch_client* c = arg;
    struct client_value* v = (struct client_value*)value;
    if (v->client_id == c->id && is_value_uid_awaiting(c->outstanding_client_value_ids, c->max_outstanding, v->uid)) {
        paxos_log_debug("Client Value delivered.");
        update_stats(c->latency_recorder, &c->stats, v, size);
        c->current_outstanding--;
        client_submit_value(c);

    }

}

static void
on_stats(evutil_socket_t fd, short event, void *arg)
{
    struct epoch_client* c = arg;
    double mbps = (double)(c->stats.delivered_bytes * 8) / (1024*1024);
    printf("%d value/sec, %.2f Mbps, latency min %ld us max %ld us avg %ld us\n",
           c->stats.delivered_count, mbps, c->stats.min_latency,
           c->stats.max_latency, c->stats.avg_latency);
    memset(&c->stats, 0, sizeof(struct stats));
    event_add(c->stats_ev, &c->stats_interval);
}

static void
on_connect(struct bufferevent* bev, short events, void* arg)
{
    int i;
    struct epoch_client* c = arg;
    if (events & BEV_EVENT_CONNECTED) {
        printf("Connected to proposer\n");
        for (i = 0; i < c->max_outstanding; ++i)
            client_submit_value(c);
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
make_epoch_client(const char *config, int proposer_id, int outstanding, int value_size, const char *latency_record_output_path)
{
    struct epoch_client* c;
    c = malloc(sizeof(struct epoch_client));
    c->base = event_base_new();

    //c->outstanding_values = malloc(max_outstanding * sizeof(int));
    struct evpaxos_config* ev_config = evpaxos_config_read(config);


    memset(&c->stats, 0, sizeof(struct stats));
    c->bev = connect_to_proposer(c, config, proposer_id);
    if (c->bev == NULL)
        exit(1);

    c->id = rand();
    c->value_size = value_size;
    c->max_outstanding = outstanding;
    c->send_buffer = malloc(sizeof(struct client_value) + value_size);
    c->outstanding_client_value_ids = malloc(sizeof(int) * c->max_outstanding);
    c->current_outstanding = 0;

    struct timeval settle_in_time = (struct timeval) {.tv_sec = paxos_config.settle_in_time, .tv_usec = 0};
    uint32_t number_of_latencies_to_record = paxos_config.number_of_latencies_to_record;
    c->latency_recorder = latency_recorder_new(latency_record_output_path, settle_in_time, number_of_latencies_to_record);

    c->stats_interval = (struct timeval){1, 0};
    c->stats_ev = evtimer_new(c->base, on_stats, c);
    event_add(c->stats_ev, &c->stats_interval);

    paxos_config.learner_catch_up = 0;
    c->learner = ev_epoch_learner_init(config, on_deliver, c, c->base, c->bev);

    c->sig = evsignal_new(c->base, SIGINT, handle_sigint, c);
    evsignal_add(c->sig, NULL);

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
start_epoch_client(const char *config, int proposer_id, int outstanding, int value_size, const char *latency_record_output_path)
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
    int outstanding = 1;
    int value_size = 64;
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
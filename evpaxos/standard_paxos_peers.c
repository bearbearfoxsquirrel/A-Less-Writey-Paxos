/*
 * Copyright (c) 2013-2014, University of Lugano
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


#include "standard_paxos_peers.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>
#include <netinet/tcp.h>
#include <standard_paxos_message.h>


struct standard_paxos_peer
{
	int id;
	int status;
	struct bufferevent* bev;
	struct event* reconnect_ev;
	struct sockaddr_in addr;
	struct standard_paxos_peers* peers;
};

struct subscription
{
	paxos_message_type type;
	peer_cb callback;
	void* arg;
};

struct standard_paxos_peers
{
	int peers_count, clients_count;
	struct standard_paxos_peer** peers;   /* peers we connected to */
	struct standard_paxos_peer** clients; /* peers we accepted connections from */
	struct evconnlistener* listener;
	struct event_base* base;
	struct evpaxos_config* config;
	int subs_count;
	struct subscription subs[32];
	struct bufferevent_rate_limit_group* rate_limit_group;
};

static struct timeval reconnect_timeout = {2,0};
static struct standard_paxos_peer* make_peer(struct standard_paxos_peers* p, int id, struct sockaddr_in* in);
static void free_peer(struct standard_paxos_peer* p);
static void free_all_peers(struct standard_paxos_peer** p, int count);
static void connect_peer(struct standard_paxos_peer* p);
static void peers_connect(struct standard_paxos_peers* p, int id, struct sockaddr_in* addr);
static void on_read(struct bufferevent* bev, void* arg);
static void on_peer_event(struct bufferevent* bev, short ev, void *arg);
static void on_client_event(struct bufferevent* bev, short events, void *arg);
static void on_connection_timeout(int fd, short ev, void* arg);
static void on_listener_error(struct evconnlistener* l, void* arg);
static void on_accept(struct evconnlistener *l, evutil_socket_t fd,
	struct sockaddr* addr, int socklen, void *arg);
static void socket_set_nodelay(int fd);

struct standard_paxos_peers *
peers_new(struct event_base *base, struct evpaxos_config *config)
{
	struct standard_paxos_peers* p = malloc(sizeof(struct standard_paxos_peers));
	p->peers_count = 0;
	p->clients_count = 0;
	p->subs_count = 0;
	p->peers = NULL;
	p->clients = NULL;
	p->listener = NULL;
	p->base = base;
	p->config = config;
//	int avg_size = (sizeof(struct paxos_accepted) * messages_batched_average) + value_size + 50;
//	int max_size = (sizeof(struct paxos_accepted) * max_messages_batched) + value_size + 100;
//	struct timeval tick = (struct timeval){.tv_sec = 1, .tv_usec = 0};
//	struct ev_token_bucket_cfg* config_ev = ev_token_bucket_cfg_new(avg_size,  max_size, avg_size, max_size, &tick);
//	p->rate_limit_group = bufferevent_rate_limit_group_new(base, config_ev);
	return p;
}

void
peers_free(struct standard_paxos_peers* p)
{
	free_all_peers(p->peers, p->peers_count);
	free_all_peers(p->clients, p->clients_count);
	if (p->listener != NULL)
		evconnlistener_free(p->listener);
	free(p);
}

int
peers_count(struct standard_paxos_peers* p)
{
	return p->peers_count;
}

static void
peers_connect(struct standard_paxos_peers* p, int id, struct sockaddr_in* addr)
{
	p->peers = realloc(p->peers, sizeof(struct standard_paxos_peer*) * (p->peers_count + 1));
	p->peers[p->peers_count] = make_peer(p, id, addr);

	struct standard_paxos_peer* peer = p->peers[p->peers_count];
	bufferevent_setcb(peer->bev, on_read, NULL, on_peer_event, peer);
    peer->reconnect_ev = evtimer_new(p->base, on_connection_timeout, peer);
	connect_peer(peer);

	p->peers_count++;
}

void
peers_connect_to_acceptors(struct standard_paxos_peers* p, int source_id)
{
	int i;
	for (i = 0; i < evpaxos_acceptor_count(p->config); i++) {
		struct sockaddr_in addr = evpaxos_acceptor_address(p->config, i);
		peers_connect(p, i, &addr);
	}


	// What was I doing here?
    for (unsigned int i = 0; i < p->peers_count; i++){
        //if (i == source_id){
        struct standard_paxos_peer* tmp = p->peers[i];
        if (tmp->id == source_id) {
            p->peers[i] = p->peers[0];
            p->peers[0] = tmp;//p->peers[i];
            //  }
        }
    }

    paxos_log_debug("Order of acceptors connected to:");
    for (unsigned int i = 0; i < p->peers_count; i++){
        //if (i == source_id){
        struct standard_paxos_peer* tmp = p->peers[i];
        paxos_log_debug("acceptor %i", tmp->id);
    }


}

void
peers_connect_to_proposers(struct standard_paxos_peers* p){
    for (int i = 0; i < evpaxos_proposer_count(p->config); i++) {
        struct sockaddr_in address = evpaxos_proposer_address(p->config, i);
        peers_connect(p, i, &address);
    }
}

void peers_foreach_proposer(struct standard_paxos_peers* p, peer_iter_cb cb, void* arg){
    for (int i = 0; i < p->peers_count; i++)
        cb(p->peers[i], arg);
}
void
peers_foreach_acceptor(struct standard_paxos_peers* p, peer_iter_cb cb, void* arg)
{
	int i;
	for (i = 0; i < p->peers_count; ++i)
		cb(p->peers[i], arg);
}


static void shuffle(struct standard_paxos_peer**array, size_t n)
{
    if (n > 1)
    {
        size_t i;
        for (i = 0; i < n - 1; i++)
        {
            size_t j = i + rand() / (RAND_MAX / (n - i) + 1);
            struct standard_paxos_peer* t = array[j];
            array[j] = array[i];
            array[i] = t;
        }
    }
}


void
peers_for_n_acceptor(struct standard_paxos_peers* p, peer_iter_cb cb, void* arg, int n)
{
	if (n>p->peers_count)
		n=p->peers_count;
	int i;

     p->peers++;
    shuffle(p->peers, p->peers_count - 1);

	p->peers--;
	for (i = 0; i < n; ++i) {
        paxos_log_debug("sending to %i", p->peers[i]->id);
        cb(p->peers[i], arg);
    }

}

void
peers_foreach_client(struct standard_paxos_peers* p, peer_iter_cb cb, void* arg)
{
	int i;
	for (i = 0; i < p->clients_count; ++i)
		cb(p->clients[i], arg);
}

struct standard_paxos_peer*
peers_get_acceptor(struct standard_paxos_peers* p, int id)
{
	int i;
	for (i = 0; p->peers_count; ++i)
		if (p->peers[i]->id == id)
			return p->peers[i];
	return NULL;
}

struct bufferevent*
peer_get_buffer(struct standard_paxos_peer* p)
{
	return p->bev;
}

int
peer_get_id(struct standard_paxos_peer* p)
{
	return p->id;
}

int peer_connected(struct standard_paxos_peer* p)
{
	return p->status == BEV_EVENT_CONNECTED;
}

int
peers_listen(struct standard_paxos_peers* p, int port)
{
	struct sockaddr_in addr;
	unsigned flags = LEV_OPT_CLOSE_ON_EXEC
		| LEV_OPT_CLOSE_ON_FREE
		| LEV_OPT_REUSEABLE;

	/* listen on the given port at address 0.0.0.0 */
	memset(&addr, 0, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(0);
	addr.sin_port = htons(port);

	p->listener = evconnlistener_new_bind(p->base, on_accept, p, flags, -1,
		(struct sockaddr*)&addr, sizeof(addr));
	if (p->listener == NULL) {
		paxos_log_error("Failed to bind on port %d", port);
		return 0;
	}
	evconnlistener_set_error_cb(p->listener, on_listener_error);
	paxos_log_info("Listening on port %d", port);
	return 1;
}

void
peers_subscribe(struct standard_paxos_peers* p, paxos_message_type type, peer_cb cb, void* arg)
{
	struct subscription* sub = &p->subs[p->subs_count];
	sub->type = type;
	sub->callback = cb;
	sub->arg = arg;
	p->subs_count++;
}

struct event_base*
peers_get_event_base(struct standard_paxos_peers* p)
{
	return p->base;
}

static void
dispatch_message(struct standard_paxos_peer* p, standard_paxos_message* msg)
{
	int i;
	for (i = 0; i < p->peers->subs_count; ++i) {
		struct subscription* sub = &p->peers->subs[i];
		if (sub->type == msg->type) {
            sub->callback(p, msg, sub->arg);
        }
	}
}

static void
on_read(struct bufferevent* bev, void* arg)
{
	struct standard_paxos_message msg;
	struct standard_paxos_peer* p = (struct standard_paxos_peer*)arg;
	struct evbuffer* in = bufferevent_get_input(bev);
	while (recv_paxos_message(in, &msg)) {
	    // add multi threading here?
		dispatch_message(p, &msg);
        paxos_message_destroy_contents(&msg);
	}
}

static void
on_peer_event( struct bufferevent* bev, short ev, void *arg)
{
    struct standard_paxos_peer* p = (struct standard_paxos_peer*)arg;

    if (ev & BEV_EVENT_CONNECTED) {
        paxos_log_info("Connected to %s:%d",
                       inet_ntoa(p->addr.sin_addr), ntohs(p->addr.sin_port));
        p->status = ev;
    } else if (ev & BEV_EVENT_ERROR || ev & BEV_EVENT_EOF) {
        struct event_base* base;
        int err = EVUTIL_SOCKET_ERROR();
        paxos_log_error("%s (%s:%d)", evutil_socket_error_to_string(err),
                        inet_ntoa(p->addr.sin_addr), ntohs(p->addr.sin_port));
        base = bufferevent_get_base(p->bev);
        bufferevent_free(p->bev);
        p->bev = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);
        bufferevent_setcb(p->bev, on_read, NULL, on_peer_event, p);
        event_add(p->reconnect_ev, &reconnect_timeout);
        p->status = ev;
    } else {
        paxos_log_error("Event %d not handled", ev);
    }
}

static void
on_client_event( struct bufferevent* bev, short ev, void *arg)
{
	struct standard_paxos_peer* p = (struct standard_paxos_peer*)arg;
	if (ev & BEV_EVENT_EOF || ev & BEV_EVENT_ERROR) {
		int i;
		struct standard_paxos_peer** clients = p->peers->clients;
		for (i = p->id; i < p->peers->clients_count-1; ++i) {
			clients[i] = clients[i+1];
			clients[i]->id = i;
		}
		p->peers->clients_count--;
		p->peers->clients = realloc(p->peers->clients,
                                    sizeof(struct standard_paxos_peer*) * (p->peers->clients_count));
		free_peer(p);
	} else {
		paxos_log_error("Event %d not handled", ev);
	}
}

static void
on_connection_timeout( int fd,  short ev, void* arg)
{
	connect_peer((struct standard_paxos_peer*)arg);
}

static void
on_listener_error(struct evconnlistener* l,  void* arg)
{
	int err = EVUTIL_SOCKET_ERROR();
	struct event_base *base = evconnlistener_get_base(l);
	paxos_log_error("Listener error %d: %s. Shutting down event loop.", err,
		evutil_socket_error_to_string(err));
	event_base_loopexit(base, NULL);
}

static void
on_accept( struct evconnlistener *l, evutil_socket_t fd,
	struct sockaddr* addr,  int socklen, void *arg)
{

	struct standard_paxos_peer* peer;
	struct standard_paxos_peers* peers = arg;

	peers->clients = realloc(peers->clients,
                             sizeof(struct standard_paxos_peer*) * (peers->clients_count + 1));
	peers->clients[peers->clients_count] =
		make_peer(peers, peers->clients_count, (struct sockaddr_in*)addr);

	peer = peers->clients[peers->clients_count];
	bufferevent_setfd(peer->bev, fd);
	bufferevent_setcb(peer->bev, on_read, NULL, on_client_event, peer);
	bufferevent_enable(peer->bev, EV_READ|EV_WRITE);
	socket_set_nodelay(fd);

	paxos_log_info("Accepted connection from %s:%d",
		inet_ntoa(((struct sockaddr_in*)addr)->sin_addr),
		ntohs(((struct sockaddr_in*)addr)->sin_port));

	peers->clients_count++;
}

static void
connect_peer(struct standard_paxos_peer* p)
{
    bufferevent_enable(p->bev, EV_READ|EV_WRITE);
    bufferevent_socket_connect(p->bev,
                               (struct sockaddr*)&p->addr, sizeof(p->addr));
    socket_set_nodelay(bufferevent_getfd(p->bev));
    paxos_log_info("Connect to %s:%d",
                   inet_ntoa(p->addr.sin_addr), ntohs(p->addr.sin_port));
}

static struct standard_paxos_peer*
make_peer(struct standard_paxos_peers* peers, int id, struct sockaddr_in* addr)
{
	struct standard_paxos_peer* p = malloc(sizeof(struct standard_paxos_peer));
	p->id = id;
	p->addr = *addr;
	p->bev = bufferevent_socket_new(peers->base, -1, BEV_OPT_CLOSE_ON_FREE);
	p->peers = peers;
	p->reconnect_ev = NULL;
	p->status = BEV_EVENT_EOF;
  //  bufferevent_add_to_rate_limit_group(p->bev, peers->rate_limit_group);
	return p;
}

static void
free_all_peers(struct standard_paxos_peer** p, int count)
{
	int i;
	for (i = 0; i < count; i++)
		free_peer(p[i]);
	if (count > 0)
		free(p);
}

static void
free_peer(struct standard_paxos_peer* p)
{
	bufferevent_free(p->bev);
	if (p->reconnect_ev != NULL)
		event_free(p->reconnect_ev);
	free(p);
}

static void
socket_set_nodelay(int fd)
{
	int flag = paxos_config.tcp_nodelay;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int));
}
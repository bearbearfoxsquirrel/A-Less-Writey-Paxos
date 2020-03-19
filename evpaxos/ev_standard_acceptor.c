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


#include "evpaxos.h"
#include "peers.h"
#include "standard_acceptor.h"
#include "message.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "string.h"
#include <event2/event.h>
#include "ballot.h"
#include <paxos_types.h>


struct ev_standard_acceptor
{
	struct peers* peers;
	struct standard_acceptor* state;
	struct event* timer_ev;
	struct timeval timer_tv;
};

static void
peer_send_paxos_message(struct peer* p, void* arg)
{
	send_paxos_message(peer_get_buffer(p), arg);
}

/*
	Received a prepare request (phase 1a).
*/
static void 
evacceptor_handle_prepare(struct peer* p, standard_paxos_message* msg, void* arg)
{
	standard_paxos_message out;
	paxos_prepare* prepare = &msg->u.prepare;
	struct ev_standard_acceptor* a = (struct ev_standard_acceptor*)arg;
	paxos_log_debug("Handle prepare for iid %d ballot %d",
		prepare->iid, prepare->ballot);
    if (standard_acceptor_receive_prepare(a->state, prepare, &out) != 0) {
		send_paxos_message(peer_get_buffer(p), &out);
        paxos_message_destroy_contents(&out);
	}
    // handle sending of chosen to sender
}

/*
	Received a accept request (phase 2a).
*/
static void 
evacceptor_handle_accept(struct peer* p, standard_paxos_message* msg, void* arg)
{	
	standard_paxos_message out;
	paxos_accept* accept = &msg->u.accept;
	struct ev_standard_acceptor* a = (struct ev_standard_acceptor*)arg;
	paxos_log_debug("Handle accept for iid %d bal %d", 
		accept->iid, accept->ballot);
    if (standard_acceptor_receive_accept(a->state, accept, &out) != 0) {
		if (out.type == PAXOS_ACCEPTED) {
		    assert(out.u.accepted.value.paxos_value_val != NULL);
		    assert(out.u.accepted.value.paxos_value_len > 1);
		    assert(strncmp(out.u.accepted.value.paxos_value_val, "", 2));

		    assert(ballot_equal(&out.u.accepted.value_ballot, out.u.accepted.promise_ballot));
			peers_foreach_client(a->peers, peer_send_paxos_message, &out);
		} else {
	        send_paxos_message(peer_get_buffer(p), &out);
	    }
        paxos_message_destroy_contents(&out);
	}
}

static void
evacceptor_handle_repeat(struct peer* p, standard_paxos_message* msg, void* arg)
{
	iid_t iid;
	struct standard_paxos_message out_msg;
	paxos_repeat* repeat = &msg->u.repeat;
	struct ev_standard_acceptor* a = (struct ev_standard_acceptor*)arg;
	paxos_log_debug("Handle repeat for iids %d-%d", repeat->from, repeat->to);
	for (iid = repeat->from; iid <= repeat->to; ++iid) {
        if (standard_acceptor_receive_repeat(a->state, iid, &out_msg)) {
            assert(out_msg.u.accepted.value_ballot.number > 0);
			send_paxos_message(peer_get_buffer(p), &out_msg);
			paxos_message_destroy_contents(&out_msg);
		}
	}
}

static void
evacceptor_handle_chosen(struct peer* p, struct standard_paxos_message* msg, void* arg){
    struct ev_standard_acceptor* a = (struct ev_standard_acceptor*)arg;
    struct paxos_chosen chosen_msg = msg->u.chosen;

    standard_acceptor_receive_chosen(a->state, &chosen_msg);
    //acceptor_cho
}

static void
evacceptor_handle_trim(struct peer* p, standard_paxos_message* msg, void* arg)
{
	paxos_trim* trim = &msg->u.trim;
	struct ev_standard_acceptor* a = (struct ev_standard_acceptor*)arg;
    standard_acceptor_receive_trim(a->state, trim);
}

static void
send_acceptor_state(int fd, short ev, void* arg)
{
	struct ev_standard_acceptor* a = (struct ev_standard_acceptor*)arg;
	standard_paxos_message msg = {.type = PAXOS_ACCEPTOR_STATE};
    standard_acceptor_set_current_state(a->state, &msg.u.state);
	peers_foreach_client(a->peers, peer_send_paxos_message, &msg);
	event_add(a->timer_ev, &a->timer_tv);
}

struct ev_standard_acceptor*
evacceptor_init_internal(int id, struct evpaxos_config* c, struct peers* p)
{
	struct ev_standard_acceptor* acceptor = calloc(1, sizeof(struct ev_standard_acceptor));
    acceptor->state = standard_acceptor_new(id);
	acceptor->peers = p;

	peers_subscribe(p, PAXOS_PREPARE, evacceptor_handle_prepare, acceptor);
	peers_subscribe(p, PAXOS_ACCEPT, evacceptor_handle_accept, acceptor);
	peers_subscribe(p, PAXOS_REPEAT, evacceptor_handle_repeat, acceptor);
	peers_subscribe(p, PAXOS_TRIM, evacceptor_handle_trim, acceptor);
	peers_subscribe(p, PAXOS_CHOSEN, evacceptor_handle_chosen, acceptor);
	
	struct event_base* base = peers_get_event_base(p);
	acceptor->timer_ev = evtimer_new(base, send_acceptor_state, acceptor);
	acceptor->timer_tv = (struct timeval){1, 0};
	event_add(acceptor->timer_ev, &acceptor->timer_tv);

	return acceptor;
}

struct ev_standard_acceptor*
evacceptor_init(int id, const char* config_file, struct event_base* base)
{
	struct evpaxos_config* config = evpaxos_config_read(config_file);
	if (config  == NULL)
		return NULL;


    int acceptor_count = evpaxos_acceptor_count(config);
	if (id < 0 || id >= acceptor_count) {
		paxos_log_error("Invalid acceptor id: %d.", id);
		paxos_log_error("Should be between 0 and %d", acceptor_count);
		evpaxos_config_free(config);
		return NULL;
	}

	struct peers* peers = peers_new(base, config);
	int port = evpaxos_acceptor_listen_port(config, id);
	if (peers_listen(peers, port) == 0)
		return NULL;


	struct ev_standard_acceptor* acceptor = evacceptor_init_internal(id, config, peers);
	evpaxos_config_free(config);
	return acceptor;
}

void
evacceptor_free_internal(struct ev_standard_acceptor* a)
{
	event_free(a->timer_ev);
    standard_acceptor_free(a->state);
	free(a);
}

void
evacceptor_free(struct ev_standard_acceptor* a)
{
	peers_free(a->peers);
	evacceptor_free_internal(a);
}

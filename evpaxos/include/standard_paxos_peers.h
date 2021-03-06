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


#ifndef _PEERS_H_
#define _PEERS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "paxos.h"
#include "evpaxos.h"
#include "paxos_types.h"
#include <event2/bufferevent.h>

struct standard_paxos_peer;
struct standard_paxos_peers;

typedef void (*peer_cb)(struct standard_paxos_peer* p, standard_paxos_message* m, void* arg);
typedef void (*peer_iter_cb)(struct standard_paxos_peer* p, void* arg);

struct standard_paxos_peers *
peers_new(struct event_base *base, struct evpaxos_config *config);
void peers_free(struct standard_paxos_peers* p);
int peers_count(struct standard_paxos_peers* p);
void peers_connect_to_acceptors(struct standard_paxos_peers* p, int partner_id);
void peers_connect_to_proposers(struct standard_paxos_peers *p, int partner_id);
void peers_connect_to_other_proposers(struct standard_paxos_peers *p, int self_id);

int peers_listen(struct standard_paxos_peers* p, int port);
void peers_subscribe(struct standard_paxos_peers* p, paxos_message_type t, peer_cb cb, void*);
void peers_foreach_proposer(struct standard_paxos_peers* p, peer_iter_cb cb, void* arg);
void peers_foreach_acceptor(struct standard_paxos_peers* p, peer_iter_cb cb, void* arg);
void peers_for_n_acceptor(struct standard_paxos_peers* p, peer_iter_cb cb, void* arg, int n);
void peers_for_n_proposers(struct standard_paxos_peers* p, peer_iter_cb cb, void* arg, int n);
void peers_send_to_proposer(struct standard_paxos_peers *p, peer_iter_cb cb, void* arg, int prop_id);
void peers_foreach_client(struct standard_paxos_peers* p, peer_iter_cb cb, void* arg);
struct standard_paxos_peer* peers_get_acceptor(struct standard_paxos_peers* p, int id);
struct event_base* peers_get_event_base(struct standard_paxos_peers* p);
int peer_get_id(struct standard_paxos_peer* p);
struct bufferevent* peer_get_buffer(struct standard_paxos_peer* p);
int peer_connected(struct standard_paxos_peer* p);

/*
void peers_connect_to_proposers(struct peers* p);
void peers_foreach_proposer(struct peers* p, peer_iter_cb cb, void* arg);
*/
#ifdef __cplusplus
}
#endif

#endif

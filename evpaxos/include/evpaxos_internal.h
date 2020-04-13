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


#ifndef _EVPAXOS_INTERNAL_H_
#define _EVPAXOS_INTERNAL_H_

#include "standard_paxos_peers.h"
#include "evpaxos.h"
#include "backoff_manager.h"

struct evlearner* evlearner_init_internal(struct evpaxos_config* config,
                                          struct standard_paxos_peers* peers, deliver_function f, void* arg);

void evlearner_free_internal(struct evlearner* l);
		
struct ev_standard_acceptor* evacceptor_init_internal(int id,
                                                      struct evpaxos_config* config, struct standard_paxos_peers* peers);

void evacceptor_free_internal(struct ev_standard_acceptor* a);

// ADDED
struct ev_write_ahead_acceptor* ev_write_ahead_acceptor_init_internal(int id, struct evpaxos_config* config, struct standard_paxos_peers* peers_proposers);

void ev_write_ahead_acceptor_free_internal(struct ev_write_ahead_acceptor* acceptor);

struct ev_less_writey_ballot_acceptor* ev_optimised_less_writey_ballot_acceptor_init_internal(int id, struct evpaxos_config* config, struct standard_paxos_peers* peers_proposers);

void ev_optimised_less_writey_ballot_acceptor_free_internal(struct ev_less_writey_ballot_acceptor* acceptor);
//
struct evproposer* evproposer_init_internal(int id, struct evpaxos_config* c, struct standard_paxos_peers* peers, struct backoff_manager* backoff_manager);


void evproposer_free_internal(struct evproposer* p);


#endif

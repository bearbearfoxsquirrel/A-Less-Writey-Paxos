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


#ifndef _PROPOSER_H_
#define _PROPOSER_H_

#include <stdlib.h>
#include "paxos.h"
#include "stdint.h"

#define INITIAL_BALLOT  (rand() % 20) + 1//(int)((double)rand() / ((double)50 + 1) * 1)
#define BALLOT_INCREMENT (rand() % 20) + 1// (int)((double)rand() / ((double)50 + 1) * 1)
#ifdef __cplusplus
extern "C" {
#endif


struct proposer;
struct timeout_iterator;

struct proposer* proposer_new(int id, int acceptors, int q1, int q2);
void proposer_free(struct proposer* p);
void proposer_add_paxos_value_to_queue(struct proposer* p, struct paxos_value* value);
int proposer_prepared_count(struct proposer* p);
int proposer_count_instance_in_accept(struct proposer* p);
void proposer_set_current_instance(struct proposer* p, iid_t iid);


void proposer_next_instance(struct proposer* p);

uint32_t proposer_get_current_instance(struct proposer* p);

uint32_t proposer_get_min_unchosen_instance(struct proposer* p);
iid_t proposer_get_next_instance_to_prepare(struct proposer* p);

// phase 1
void proposer_try_to_start_preparing_instance(struct proposer* p, iid_t instance, paxos_prepare* out);

int proposer_receive_promise(struct proposer* p, paxos_promise* ack,
	paxos_prepare* out);

// phase 2
int proposer_try_accept(struct proposer* p, paxos_accept* out);
int proposer_receive_accepted(struct proposer* p, paxos_accepted* ack, struct paxos_chosen* chosen);
int proposer_receive_chosen(struct proposer* p, struct paxos_chosen* ack);

//void proposer_preempt(struct proposer* p, struct standard_proposer_instance_info* inst, paxos_prepare* out);
int proposer_receive_preempted(struct proposer* p, struct paxos_preempted* preempted, struct paxos_prepare* out);

int is_proposer_instance_pending_and_message_return(struct proposer* p, paxos_preempted* ack,
                                                    paxos_prepare* out);

// periodic acceptor state
void proposer_receive_acceptor_state(struct proposer* p,
                                     paxos_standard_acceptor_state* state);
void proposer_receive_trim(struct proposer* p,
                                     struct paxos_trim* trim_msg);
// timeouts
struct timeout_iterator* proposer_timeout_iterator(struct proposer* p);
int timeout_iterator_prepare(struct timeout_iterator* iter, paxos_prepare* out);
int timeout_iterator_accept(struct timeout_iterator* iter, paxos_accept* out);
void timeout_iterator_free(struct timeout_iterator* iter);

#ifdef __cplusplus
}
#endif

#endif

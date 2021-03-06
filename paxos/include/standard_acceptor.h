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


#ifndef _ACCEPTOR_H_
#define _ACCEPTOR_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "paxos.h"
#include "standard_stable_storage.h"
/*
struct standard_acceptor {
    int id;
    iid_t trim_iid;
    struct standard_stable_storage stable_storage;
};
*/
struct standard_acceptor *
standard_acceptor_new(int id);

void standard_acceptor_free(struct standard_acceptor *a);

int standard_acceptor_receive_prepare(struct standard_acceptor *a, paxos_prepare *req, standard_paxos_message *out,
                                      paxos_preempted *preempted, bool *was_prev_preempted);

int standard_acceptor_receive_accept(struct standard_acceptor *a, paxos_accept *req, standard_paxos_message *out,
                                     paxos_preempted *preempted, bool *was_prev_preempted);

int standard_acceptor_receive_chosen(struct standard_acceptor* a, struct paxos_chosen *chosen);

int standard_acceptor_receive_repeat(struct standard_acceptor *a,
                                     iid_t iid, struct standard_paxos_message *out);

int standard_acceptor_receive_trim(struct standard_acceptor *a, paxos_trim *trim);

void standard_acceptor_get_current_state(struct standard_acceptor *a, paxos_standard_acceptor_state *state);


iid_t standard_acceptor_get_max_proposed_instance(struct standard_acceptor* acceptor);

#ifdef __cplusplus
}
#endif

#endif

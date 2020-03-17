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


#include "standard_acceptor.h"
#include "standard_stable_storage.h"
#include "ballot.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "khash.h"
#include <paxos_message_conversion.h>
#include <assert.h>
#include <paxos_types.h>
#include <hash_mapped_memory.h>

KHASH_MAP_INIT_INT(is_chosen, bool*)

struct standard_acceptor
{
	int id;
	iid_t trim_iid;
    struct standard_stable_storage stable_storage;
    struct paxos_storage* paxos_storage;
 //   khash_t(is_chosen)* chosen_instances;
};



struct standard_acceptor*
standard_acceptor_new(int id)
{
	struct standard_acceptor* a = calloc(1, sizeof(struct standard_acceptor));



    storage_init_lmdb_standard(&a->stable_storage, id);
    if (storage_open(&a->stable_storage) != 0) {
		free(a);
		return NULL;
	}
    if (storage_tx_begin(&a->stable_storage) != 0)
		return NULL;
	a->id = id;

	iid_t trim_iid;
    storage_get_trim_instance(&a->stable_storage, &trim_iid);
    a->trim_iid = trim_iid;

    a->paxos_storage = calloc(1, sizeof(struct paxos_storage));
    struct paxos_accepted* instances_info;
    int number_of_instances_retrieved = 0;
    storage_get_all_untrimmed_instances_info(&a->stable_storage, &instances_info, &number_of_instances_retrieved);
    init_hash_mapped_memory_from_instances_info(a->paxos_storage, instances_info, number_of_instances_retrieved, a->trim_iid, id);

    if (storage_tx_commit(&a->stable_storage) != 0)
		return NULL;
	return a;
}

void standard_acceptor_store_trim_instance(struct standard_acceptor *a, iid_t trim);

static bool standard_acceptor_is_instance_chosen(struct standard_acceptor* a, iid_t instance) {
    bool is_chosen = false;
    is_instance_chosen(a->paxos_storage, instance, &is_chosen);
    return is_chosen;
}

void
standard_acceptor_free(struct standard_acceptor *a) {
    storage_close(&a->stable_storage);
	free(a);
}

bool write_ahead_acceptor_safe_to_acknowledge_paxos_request(bool instance_previously_inited, struct ballot requested_ballot, struct ballot last_promised_ballot){
    return (!instance_previously_inited || ballot_greater_than_or_equal(requested_ballot, last_promised_ballot));
}

int
standard_acceptor_receive_prepare(struct standard_acceptor *a,
                                  paxos_prepare *req, standard_paxos_message *out)
{
	struct paxos_accepted instance_info;
	if (req->iid <= a->trim_iid) {
	    *out = (struct standard_paxos_message) {.type = PAXOS_TRIM,
             .u.trim = {.iid = a->trim_iid}
	    };
        return 1;
	}

	bool instance_chosen = standard_acceptor_is_instance_chosen(a, req->iid);
	memset(&instance_info, 0, sizeof(paxos_accepted));
    if (storage_tx_begin(&a->stable_storage) != 0)
		return 0;

    int found = storage_get_instance_info(&a->stable_storage, req->iid, &instance_info);
	if (write_ahead_acceptor_safe_to_acknowledge_paxos_request(found, req->ballot, instance_info.promise_ballot)) {
		paxos_log_debug("Preparing iid: %u, ballot: %u.%u", req->iid, req->ballot.number, req->ballot.proposer_id);
        instance_info.aid = a->id;
        instance_info.iid = req->iid;
		copy_ballot(&req->ballot, &instance_info.promise_ballot);
        if (storage_store_instance_info(&a->stable_storage, &instance_info) != 0) {
            storage_tx_abort(&a->stable_storage);
			return 0;
		}

        paxos_accepted_to_promise(&instance_info, out);
	} else {
	    if (instance_chosen) {
	        out->type = PAXOS_CHOSEN;
	        paxos_chosen_from_paxos_accepted(&out->u.chosen, &instance_info);
	    } else {
            struct paxos_prepare last_promise;
            paxos_accepted_to_prepare(&instance_info, &last_promise);
            union_paxos_prepare_and_last_acceptor_promise_to_preempted(a->id, req, &last_promise, out);
	    }

	}
    if (storage_tx_commit(&a->stable_storage) != 0)
		return 0;
	return 1;
}

int
standard_acceptor_receive_accept(struct standard_acceptor *a,
                                 paxos_accept *req, standard_paxos_message *out)
{
	paxos_accepted acc;
	if (req->iid <= a->trim_iid) {
	    out->type = PAXOS_TRIM;
	    out->u.trim = (struct paxos_trim) {a->trim_iid};
	}
	memset(&acc, 0, sizeof(paxos_accepted));
    if (storage_tx_begin(&a->stable_storage) != 0)
		return 0;
    int found = storage_get_instance_info(&a->stable_storage, req->iid, &acc);
	if (write_ahead_acceptor_safe_to_acknowledge_paxos_request(found, req->ballot, acc.promise_ballot)) {
		paxos_log_debug("Accepting iid: %u, ballot: %u.%u", req->iid, req->ballot.number, req->ballot.proposer_id);
		paxos_accept_to_accepted(a->id, req, out);
        if (storage_store_instance_info(&a->stable_storage, &(out->u.accepted)) != 0) {
            storage_tx_abort(&a->stable_storage);
			return 0;
		}
	} else {
	    bool chosen = standard_acceptor_is_instance_chosen(a, req->iid);
	    if (chosen) {
	        out->type = PAXOS_CHOSEN;
	        paxos_chosen_from_paxos_accepted(&out->u.chosen, &acc);
	    } else {
            paxos_accepted_to_preempted(a->id, &acc, out);
	    }

	}
    if (storage_tx_commit(&a->stable_storage) != 0)
        return 0;
	paxos_accepted_destroy(&acc);
	return 1;
}


int standard_acceptor_receive_chosen(struct standard_acceptor* a, struct paxos_chosen *chosen){
    set_instance_chosen(a->paxos_storage, chosen->iid);
    storage_tx_begin(&a->stable_storage);
    struct paxos_accepted to_store;
    paxos_accepted_from_paxos_chosen(&to_store, chosen);
    storage_store_instance_info(&a->stable_storage, &to_store);
    storage_tx_commit(&a->stable_storage);
    return 0;
}


int
standard_acceptor_receive_repeat(struct standard_acceptor *a, iid_t iid, struct standard_paxos_message *out)
{
    struct paxos_accepted instance_info;
    memset(&instance_info, 0, sizeof(struct paxos_accepted));
    if (storage_tx_begin(&a->stable_storage) != 0)
        return 0;
    int found = storage_get_instance_info(&a->stable_storage, iid, &instance_info);
    if (storage_tx_commit(&a->stable_storage) != 0)
        return 0;

    if (standard_acceptor_is_instance_chosen(a, iid)) {
        out->type = PAXOS_CHOSEN;
        paxos_chosen_from_paxos_accepted(&out->u.chosen, &instance_info);
        return 1;
    } else {
        if (found && (out->u.accepted.value.paxos_value_len > 0)) {
            out->type = PAXOS_ACCEPTED;
            paxos_accepted_copy(&out->u.accepted, &instance_info);
            return 1;
        } else {
            return 0;
        }
    }
}

int
standard_acceptor_receive_trim(struct standard_acceptor *a, paxos_trim *trim)
{
	if (trim->iid <= a->trim_iid)
		return 0;
    if (storage_tx_begin(&a->stable_storage) != 0)
		return 0;

    standard_acceptor_store_trim_instance(a, trim->iid);

    if (storage_tx_commit(&a->stable_storage) != 0)
		return 0;
	return 1;
}

void standard_acceptor_store_trim_instance(struct standard_acceptor *a, iid_t trim) {
    a->trim_iid = trim;
    storage_store_trim_instance(&a->stable_storage, trim);
}

void
standard_acceptor_set_current_state(struct standard_acceptor *a, paxos_standard_acceptor_state *state)
{
	state->aid = a->id;
	state->trim_iid = a->trim_iid;
}

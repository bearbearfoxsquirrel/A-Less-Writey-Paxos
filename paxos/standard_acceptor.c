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


#include "standard_stable_storage.h"
#include "standard_acceptor.h"
#include "ballot.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "khash.h"
#include <paxos_message_conversion.h>
#include <assert.h>
#include <paxos_types.h>
#include <hash_mapped_memory.h>

//KHASH_MAP_INIT_INT(is_chosen, bool*)

struct standard_acceptor
{
	int id;
	iid_t trim_iid;
    struct standard_stable_storage stable_storage;
    struct paxos_storage* paxos_storage;
 //   khash_t(is_chosen)* chosen_instances;

    iid_t max_proposed_instance;
};



iid_t standard_acceptor_get_max_proposed_instance(struct standard_acceptor* acceptor) {
    return acceptor->max_proposed_instance;
}


struct standard_acceptor *
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

  //  for (int i = 0; i < number_of_instances_retrieved; i++) {
   //     paxos_accepted_free(&instances_info[i]);
   // }

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

bool standard_acceptor_safe_to_acknowledge_paxos_request(bool instance_previously_inited, struct ballot requested_ballot, struct ballot last_promised_ballot){
    return (!instance_previously_inited || ballot_greater_than_or_equal(requested_ballot, last_promised_ballot));
}

int
standard_acceptor_receive_prepare(struct standard_acceptor *a, paxos_prepare *req, standard_paxos_message *out,
                                  paxos_preempted *preempted, bool *was_prev_preempted)
{
	struct paxos_accepted instance_info;
	if (req->iid <= a->trim_iid) {
	    out->type = PAXOS_TRIM;
	    out->u.trim = (struct paxos_trim) {.iid = a->trim_iid};
	    paxos_log_debug("Returning Trim message to iid: %u", a->trim_iid);
        return 1;
	}

	bool instance_chosen = standard_acceptor_is_instance_chosen(a, req->iid);
	memset(&instance_info, 0, sizeof(struct paxos_accepted));

	int found = get_instance_info(a->paxos_storage, req->iid, &instance_info);

    if (instance_chosen) {
       // assert(found);
        out->type = PAXOS_CHOSEN;
        paxos_chosen_from_paxos_accepted(&out->u.chosen, &instance_info);
        paxos_log_debug("Returning Chosen. Current Ballot for iid %u is %u.%u", req->iid,
                        instance_info.value_ballot.number, instance_info.value_ballot.proposer_id);
        return 1;
    }

    if (standard_acceptor_safe_to_acknowledge_paxos_request(found, req->ballot, instance_info.promise_ballot)) {
        if (req->iid > a->max_proposed_instance) {
            a->max_proposed_instance = req->iid;
        }

        if (storage_tx_begin(&a->stable_storage) != 0)
            return 0;

      //  int found_stable = storage_get_instance_info(&a->stable_storage, req->iid, &instance_info);

        if (ballot_greater_than(instance_info.promise_ballot, INVALID_BALLOT)) {
            *was_prev_preempted = true;
            *preempted = (paxos_preempted) {
                .iid = req->iid,
                .aid = a->id,
                .acceptor_current_ballot = req->ballot,
                .attempted_ballot = instance_info.promise_ballot
            };
        }

            // assert(found_stable == found);

		paxos_log_debug("Preparing iid: %u, ballot: %u.%u", req->iid, req->ballot.number, req->ballot.proposer_id);
        instance_info.aid = a->id;
        instance_info.iid = req->iid;
		copy_ballot(&req->ballot, &instance_info.promise_ballot);

        if (storage_store_instance_info(&a->stable_storage, &instance_info) != 0) {
            storage_tx_abort(&a->stable_storage);
			return 0;
		}

        store_instance_info(a->paxos_storage, &instance_info);

        if (storage_tx_commit(&a->stable_storage) != 0)
            return 0;


        out->type = PAXOS_PROMISE;
        out->u.promise = (paxos_promise) {
                instance_info.aid,
                instance_info.iid,
                instance_info.promise_ballot,
                instance_info.value_ballot,
                {0, NULL}
        };
        bool has_value_to_return = ballot_greater_than(instance_info.value_ballot, INVALID_BALLOT);
        if(has_value_to_return) {
            paxos_log_debug("Previously accepted value to give to the Proposer");

            out->u.promise.value = (struct paxos_value) {instance_info.value.paxos_value_len, instance_info.value.paxos_value_val};


           // assert(out->u.promise.value.paxos_value_len > 0);
        }

       // assert(ballot_greater_than_or_equal(instance_info.promise_ballot, instance_info.value_ballot));

	} else {

      //  } else {
            struct paxos_prepare last_promise;
            paxos_accepted_to_prepare(&instance_info, &last_promise);
            union_paxos_prepare_and_last_acceptor_promise_to_preempted(a->id, req, &last_promise, out);
            paxos_log_debug("Returning Preempted. Current Ballot for iid %u is %u.%u", req->iid, last_promise.ballot.number, last_promise.ballot.proposer_id);
	 //   }

	}

	return 1;
}

int
standard_acceptor_receive_accept(struct standard_acceptor *a, paxos_accept *req, standard_paxos_message *out,
                                 paxos_preempted *preempted, bool *was_prev_preempted)
{
	paxos_accepted instance_info;
	if (req->iid <= a->trim_iid) {
	    out->type = PAXOS_TRIM;
	    out->u.trim = (struct paxos_trim) {a->trim_iid};
        return 1;
	}
	memset(&instance_info, 0, sizeof(paxos_accepted));


    bool chosen = standard_acceptor_is_instance_chosen(a, req->iid);
    int found = get_instance_info(a->paxos_storage, req->iid, &instance_info);

    if (chosen) {
       // assert(found);
        out->type = PAXOS_CHOSEN;
       // out->u.chosen = (struct paxos_chosen) {
       //         .iid = instance_info.iid,
       //         .ballot = instance_info.value_ballot,
      //  };
      //  copy_value(&instance_info.value, &out->u.chosen.value);
         paxos_chosen_from_paxos_accepted(&out->u.chosen, &instance_info);
        return 1;
    }

	if (standard_acceptor_safe_to_acknowledge_paxos_request(found, req->ballot, instance_info.promise_ballot)) {

        if (ballot_greater_than(instance_info.promise_ballot, INVALID_BALLOT)) {
            *was_prev_preempted = true;
            *preempted = (paxos_preempted) {
                    .iid = req->iid,
                    .aid = a->id,
                    .acceptor_current_ballot = req->ballot,
                    .attempted_ballot = instance_info.promise_ballot
            };
        }



        if (req->iid > a->max_proposed_instance) {
            a->max_proposed_instance = req->iid;
        }

		paxos_log_debug("Accepting iid: %u, ballot: %u.%u", req->iid, req->ballot.number, req->ballot.proposer_id);

	//	assert(strncmp(req->value.paxos_value_val, "", req->value.paxos_value_len));


	//	int found_stable = storage_get_instance_info(&a->stable_storage, req->iid, &instance_info);
	//	assert(found_stable);

        if (storage_tx_begin(&a->stable_storage) != 0)
            return 0;

        paxos_accept_to_accepted(a->id, req, out);

        if (storage_store_instance_info(&a->stable_storage, &(out->u.accepted)) != 0) {
            storage_tx_abort(&a->stable_storage);
			return 0;
		}

        store_instance_info(a->paxos_storage, &(out->u.accepted));

        if (storage_tx_commit(&a->stable_storage) != 0)
            return 0;
       // assert(ballot_equal(out->u.accepted.promise_ballot, out->u.accepted.value_ballot));
 //      // assert(ballot_equal(instance_info.promise_ballot, instance_info.value_ballot));
	} else {

	//    } else {
	        out->type = PAXOS_PREEMPTED;
	        out->u.preempted = (struct paxos_preempted) {
	            .aid = a->id,
	            .iid = req->iid,
	            .attempted_ballot = req->ballot,
	            .acceptor_current_ballot = instance_info.promise_ballot
	        };
          //  paxos_accepted_to_preempted(a->id, &instance_info, out);
	//    }
     //   return 1;
	}

	paxos_accepted_destroy(&instance_info);
	return 1;
}




int standard_acceptor_receive_chosen(struct standard_acceptor* a, struct paxos_chosen *chosen){
    bool is_chosen;
    is_instance_chosen(a->paxos_storage, chosen->iid, &is_chosen);
    if (is_chosen){
        paxos_log_debug("Ignoring chosen message for Instance %u as it is already known to be chosen", chosen->iid);
        return 0;
    }

    set_instance_chosen(a->paxos_storage, chosen->iid);
    a->max_proposed_instance = chosen->iid > a->max_proposed_instance ? chosen->iid : a->max_proposed_instance;

    // todo update function so old promise is restored
    struct paxos_accepted instance_info;
    bool found = get_instance_info(a->paxos_storage, chosen->iid, &instance_info);

    if(instance_info.value.paxos_value_val != NULL){
        paxos_value_destroy(&instance_info.value);
    }

    instance_info = (struct paxos_accepted) {
            .iid =chosen->iid,
            .aid = a->id,
            .promise_ballot = instance_info.promise_ballot,
            .value_ballot = chosen->ballot,
            .value = chosen->value // chosen value is automatically deleted later
    };
    //    if (storage_tx_begin(&a->stable_storage) != 0) {
       //     paxos_accepted_destroy(&instance_info);
       //     return 0;
      //  }

   //     paxos_accepted_update_instance_info_with_chosen(&instance_info, chosen, a->id);
     //   if (storage_store_instance_info(&a->stable_storage, &instance_info)){
       //     storage_tx_abort(&a->stable_storage);
       //     paxos_accepted_destroy(&instance_info);
        //    return 0;
        //}

        store_instance_info(a->paxos_storage, &instance_info);

      //  if (storage_tx_commit(&a->stable_storage) != 0){
       //     storage_tx_abort(&a->stable_storage);
       //     paxos_accepted_destroy(&instance_info);
     //       return 0;
      //  }
       // assert(ballot_equal(instance_info.promise_ballot, instance_info.value_ballot));

   //§ paxos_accepted_destroy(&instance_info);

    iid_t min_unchosen_instance;
    get_min_unchosen_instance(a->paxos_storage, &min_unchosen_instance);
    if (min_unchosen_instance > a->trim_iid) {
        struct paxos_trim trim = {min_unchosen_instance - 1};
        standard_acceptor_receive_trim(a, &trim);
    }
    return 0;


}


int
standard_acceptor_receive_repeat(struct standard_acceptor *a, iid_t iid, struct standard_paxos_message *out)
{
    paxos_log_debug("Handling Repeat for Instance %u, iid");
    struct paxos_accepted instance_info;
    memset(&instance_info, 0, sizeof(struct paxos_accepted));
   // if (storage_tx_begin(&a->stable_storage) != 0)
   //     return 0;
    int found = get_instance_info(a->paxos_storage, iid, &instance_info);
  //  if (storage_tx_commit(&a->stable_storage) != 0) {
  //      storage_tx_abort(&a->stable_storage);
 //       return 0;
 //   }

    if (standard_acceptor_is_instance_chosen(a, iid)) {
       // assert(found);
        out->type = PAXOS_CHOSEN;
        paxos_chosen_from_paxos_accepted(&out->u.chosen, &instance_info);
        return 1;
    } else {

        if (found && ballot_greater_than(instance_info.value_ballot, INVALID_BALLOT)) {
            out->type = PAXOS_ACCEPTED;
            paxos_accepted_copy(&out->u.accepted, &instance_info);
            out->u.accepted.promise_ballot = out->u.accepted.value_ballot;
            return 1;
        } else {

            paxos_log_debug("Asked to repeat iid: %u but there is no record of it here.", iid);
            return 0;
        }
    }
}

int
standard_acceptor_receive_trim(struct standard_acceptor *a, paxos_trim *trim)
{

    iid_t min_unchosen_innstance;
    get_min_unchosen_instance(a->paxos_storage, &min_unchosen_innstance);

    bool new_trim = trim->iid > a->trim_iid;
    bool able_to_trim = min_unchosen_innstance > trim->iid;
    if (new_trim && able_to_trim) {
        paxos_log_debug("Storing new Trim to Instance %u", trim->iid);

        //   if (trim->iid <= a->trim_iid && trim->iid <= min_unchosen_innstance)
        //     return 0;

     //   if (storage_tx_begin(&a->stable_storage) != 0)
      //      return 0;

        paxos_log_debug("Receive new trim to Instance %u", trim->iid);
        standard_acceptor_store_trim_instance(a, trim->iid);

        long to_trim_from = trim->iid - 50000;

        if (to_trim_from > INVALID_INSTANCE) {
            paxos_log_debug("Acceptor trimming stored Instances to %u", to_trim_from);
     //       storage_trim_instances_less_than(&a->stable_storage, to_trim_from);
            trim_instances_less_than(a->paxos_storage, to_trim_from);
        }

      //  if (storage_tx_commit(&a->stable_storage) != 0)
        //    return 0;
        return 1;
    } else {
        return 0;
    }
}

void standard_acceptor_store_trim_instance(struct standard_acceptor *a, iid_t trim) {
    a->trim_iid = trim;
  //  storage_store_trim_instance(&a->stable_storage, trim);
    store_trim_instance(a->paxos_storage, trim);
}

void
standard_acceptor_get_current_state(struct standard_acceptor *a, paxos_standard_acceptor_state *state)
{
	state->aid = a->id;
	state->trim_iid = a->trim_iid;
}

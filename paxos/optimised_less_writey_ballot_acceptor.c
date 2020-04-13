//
// Created by Michael Davis on 13/04/2020.
//


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

#include "optimised_less_writey_ballot_acceptor.h"
#include "standard_acceptor.h"

#include "ballot.h"
#include "paxos_value.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include <paxos_message_conversion.h>
#include <paxos_storage.h>
#include <paxos_types.h>
#include <hash_mapped_memory.h>
#include <assert.h>
#include <optimsed_less_writey_ballot_stable_storage.h>


struct optimised_less_writey_ballot_acceptor {
    // Easy to just extend off of the standard acceptor
    // struct standard_acceptor* standard_acceptor;
    int id;
    struct optimised_less_writey_ballot_stable_storage stable_storage;
    iid_t trim_instance;


    // Volatile storage to store the "actual" promises made to proposers
    // Will be lost on restart
    // Important to ensure that any promise given to a proposer is written ahead to the
    // standard acceptor's stable storage
    struct paxos_storage* volatile_storage;


    struct ballot min_ballot_safe_to_promise;
    struct ballot max_ballot_safe_to_promise;
    struct ballot max_ballot_wittenessed;
    
    // The minimum point in which volatile storage is allowed to catch up with stable storage
    unsigned int min_ballot_catachup;
    // The number of ballots or instances that should be written ahead of the last stable storage point
    unsigned int ballot_window;
};


static iid_t get_min_usable_instance(struct optimised_less_writey_ballot_acceptor* acceptor) {
    return acceptor->trim_instance + 1;
}

/*

static bool writeahead_ballots_acceptor_is_instance_trimed(struct writeadhead_window_acceptor* acceptor) {
    //todo make into own file
}*/

static int optimised_less_writey_ballot_acceptor_store_in_stable_storage(struct optimised_less_writey_ballot_acceptor* acceptor,
                                                               const struct paxos_accepted* instance_info){
    int success = optimised_less_writey_ballot_stable_storage_store_instance_info(&acceptor->stable_storage, instance_info);
    return success;
}

iid_t optimised_less_writey_ballot_acceptor_get_trim(struct optimised_less_writey_ballot_acceptor* acceptor){
    return acceptor->trim_instance;
}

// Stores to all types of storage of the Acceptor the instance info.
// For stable storage this will be uncommitted, so the user of the method must commit the stable storage's
// transcation themselves
int
optimised_less_writey_ballot_acceptor_store_in_all_storages(struct optimised_less_writey_ballot_acceptor* acceptor,
                                                  const struct paxos_accepted* instance_info)
{
    int success = optimised_less_writey_ballot_acceptor_store_in_stable_storage(acceptor, instance_info);
    store_instance_info(acceptor->volatile_storage, instance_info);
    return success;
}

void optimised_less_writey_ballot_acceptor_store_trim(struct optimised_less_writey_ballot_acceptor* acceptor, const iid_t trim) {
    acceptor->trim_instance = trim;
    optimised_less_writey_ballot_stable_storage_store_trim_instance(&acceptor->stable_storage, trim);
    store_trim_instance(acceptor->volatile_storage, trim);
}


void optimised_less_writey_ballot_acceptor_store_prepare_in_volatile_storage(struct optimised_less_writey_ballot_acceptor* acceptor, const struct paxos_prepare* prepare){
    assert(ballot_greater_than_or_equal(prepare->ballot, acceptor->min_ballot_safe_to_promise));
    int error = store_last_prepare(acceptor->volatile_storage, prepare);
    assert(error == 0);
}

void write_new_ballot_epoch(struct optimised_less_writey_ballot_acceptor* acceptor) {
    struct ballot new_stable_promise = (struct ballot) {
            .number = acceptor->max_ballot_safe_to_promise.number + acceptor->ballot_window,
            .proposer_id = UINT32_MAX
    };

    paxos_log_debug("New Ballot Epoch needed. Writing ahead new Promise to Stable Storage for Ballot %u.%u",
                    new_stable_promise.number, new_stable_promise.proposer_id);

    optimised_less_writey_ballot_stable_storage_store_last_stable_promise(&acceptor->stable_storage,
                                                                          new_stable_promise);
    acceptor->max_ballot_safe_to_promise = new_stable_promise;
}



void transaction_to_write_new_ballot_epoch(struct optimised_less_writey_ballot_acceptor* acceptor) {
        struct ballot new_stable_promise = (struct ballot) {
            .number = acceptor->max_ballot_safe_to_promise.number + acceptor->ballot_window,
            .proposer_id = UINT32_MAX
        };

        paxos_log_debug("New Ballot Epoch needed. Writing ahead new Promise to Stable Storage for Ballot %u.%u",
                        new_stable_promise.number, new_stable_promise.proposer_id);

        optimised_less_writey_ballot_stable_storage_tx_begin(&acceptor->stable_storage);
        optimised_less_writey_ballot_stable_storage_store_last_stable_promise(&acceptor->stable_storage,
        new_stable_promise);
        optimised_less_writey_ballot_stable_storage_tx_commit(&acceptor->stable_storage);
        acceptor->max_ballot_safe_to_promise = new_stable_promise;
}

// Meant to be a method to check the windows (ballot and instance) after replying to a proposer message
// could be async but not really supported
void
optimised_less_writey_ballot_acceptor_check_and_update_written_ahead_promise_ballot(struct optimised_less_writey_ballot_acceptor* acceptor) {
    assert(ballot_greater_than_or_equal(acceptor->max_ballot_wittenessed, acceptor->min_ballot_safe_to_promise));
    assert(ballot_greater_than_or_equal(acceptor->max_ballot_safe_to_promise, acceptor->max_ballot_wittenessed)); // always written ahead
    unsigned int difference_ballot = acceptor->max_ballot_safe_to_promise.number - acceptor->max_ballot_wittenessed.number;
    if (difference_ballot < acceptor->min_ballot_catachup) {
        transaction_to_write_new_ballot_epoch(acceptor);
    }
}


void optimised_less_writey_ballot_acceptor_get_instance_info(struct optimised_less_writey_ballot_acceptor* a, iid_t instance, struct paxos_accepted* instance_info){
    int found = get_instance_info(a->volatile_storage, instance, instance_info);
    if (!found) {
        instance_info->promise_ballot = a->min_ballot_safe_to_promise;
    }
    assert(ballot_greater_than_or_equal(instance_info->promise_ballot, a->min_ballot_safe_to_promise));
    assert(ballot_greater_than_or_equal(instance_info->promise_ballot, instance_info->value_ballot));
}




/*
 * RECOVERY, INITIALISATION, DEALLOCATION AND SETTERS
 */
struct optimised_less_writey_ballot_acceptor*
optimised_less_writey_ballot_acceptor_new (int id, int min_ballot_catchup, int bal_window) {
    int error;

    struct optimised_less_writey_ballot_acceptor* acceptor =  calloc(1, sizeof(struct optimised_less_writey_ballot_acceptor));
    acceptor->id = id;

    //stable storage setup
    //   acceptor->standard_acceptor = //standard_acceptor_new(id);//optimised_less_writey_ballot_acceptor_init_standard_acceptor(id, standard_stable_storage);

    optimised_less_writey_ballot_stable_storage_init(&acceptor->stable_storage, id);

    if (optimised_less_writey_ballot_stable_storage_open(&acceptor->stable_storage) != 0) {
        free(acceptor);
        return NULL;
    }


    // get instances info from stable storage
    error = optimised_less_writey_ballot_stable_storage_tx_begin(&acceptor->stable_storage);
    paxos_accepted* instances_info ;// calloc(1, sizeof(struct paxos_accepted));//calloc(0, sizeof(struct paxos_accepted)); // not initialised as it is unknown how many instances will be retrieved
    int* number_of_instances = calloc(1, sizeof(int));
    error = optimised_less_writey_ballot_stable_storage_get_all_untrimmed_instances_info(&acceptor->stable_storage, &instances_info, number_of_instances);

    // get trim id from stable storage
    iid_t trim_id = 0;
    error = optimised_less_writey_ballot_stable_storage_get_trim_instance(&acceptor->stable_storage, &trim_id);
    acceptor->trim_instance = trim_id;

    //copy to volitile storage
    struct paxos_storage* volatile_storage = calloc(1, sizeof(struct paxos_storage));
    init_hash_mapped_memory_from_instances_info(volatile_storage, instances_info, *number_of_instances, acceptor->trim_instance, id);


    // Set up write ahead acceptor variables
    acceptor->volatile_storage = volatile_storage;
    acceptor->ballot_window = bal_window;
    acceptor->min_ballot_catachup = min_ballot_catchup;

    iid_t max_instance;

    optimised_less_writey_ballot_stable_storage_get_max_inited_instance(&acceptor->stable_storage, &max_instance);


    for (unsigned int i = get_min_usable_instance(acceptor); i < max_instance; i++){
        // volatile
        // stable
        // stable dup all equal
        struct paxos_accepted stable_stored_info;
        optimised_less_writey_ballot_stable_storage_get_instance_info(&acceptor->stable_storage, i, &stable_stored_info);

        // struct paxos_prepare volatile_prepare;
        // get_last_promise(acceptor->volatile_storage, i, &volatile_prepare);

        //  struct paxos_accept last_accept;
        //  get_last_accept(acceptor->volatile_storage, i, &last_accept);
        struct paxos_accepted volatile_inst;
        optimised_less_writey_ballot_acceptor_get_instance_info(acceptor, i, &volatile_inst);
        assert(stable_stored_info.iid == volatile_inst.iid);
        assert(ballot_equal(stable_stored_info.promise_ballot, volatile_inst.promise_ballot));
        assert(ballot_equal(stable_stored_info.value_ballot, volatile_inst.value_ballot));
        assert(is_values_equal(stable_stored_info.value, volatile_inst.value));
    }


    struct ballot next_min_ballot_to_promise;
    optimised_less_writey_ballot_stable_storage_get_last_stable_promise(&acceptor->stable_storage, &next_min_ballot_to_promise);
    next_min_ballot_to_promise.number++;
    acceptor->min_ballot_safe_to_promise = next_min_ballot_to_promise;
    acceptor->max_ballot_safe_to_promise = acceptor->min_ballot_safe_to_promise;
    acceptor->max_ballot_wittenessed = acceptor->min_ballot_safe_to_promise;
    write_new_ballot_epoch(acceptor);


    optimised_less_writey_ballot_stable_storage_tx_commit(&acceptor->stable_storage);
    return acceptor;
}





void
optimised_less_writey_ballot_acceptor_free(struct optimised_less_writey_ballot_acceptor* a)
{
    optimised_less_writey_ballot_stable_storage_close(&a->stable_storage);
    free(a);
}


void
optimised_less_writey_ballot_acceptor_get_current_state(struct optimised_less_writey_ballot_acceptor* a, paxos_standard_acceptor_state* state) {
    paxos_log_debug("Getting current state to: aid: %u, trim iid %u", a->id, a->trim_instance);
    state->aid = a->id;
    state->trim_iid = a->trim_instance;
    get_max_inited_instance(a->volatile_storage, &state->current_instance);
    //todo min possible ballot
}


/*
 * HANDLING OF MESSAGES RECEIVED
 * -----------------------------
 */

// Acceptor handler for receiving a prepare message.
// If returned 0 then there is no message/response from the acceptor,
// otherwise, there is.
int
optimised_less_writey_ballot_acceptor_receive_prepare(struct optimised_less_writey_ballot_acceptor* a, paxos_prepare* req, standard_paxos_message* out) {

    assert(!ballot_equal(req->ballot, INVALID_BALLOT));

    bool is_there_response_message = false; // Initially we don't have a message to response with

    if (req->iid <= a->trim_instance) {
        out->type = PAXOS_TRIM;
        out->u.trim = (struct paxos_trim) {.iid = a->trim_instance};
        return 1;
    }

    // If the Request is for an already trimmed instance we can just ignore it
    // We could maybe in future change this to be some sort of response to inform the Requestor
    bool instance_chosen = false;
    is_instance_chosen(a->volatile_storage, req->iid, &instance_chosen);
    if (!instance_chosen) {
        // struct paxos_prepare last_volatile_promise;// get_empty_prepare();
        //   memset(&last_volatile_promise, 0, sizeof(last_volatile_promise));
        // int found = get_last_promise(a->volatile_storage, req->iid, &last_volatile_promise);

        struct paxos_accepted instance_info;
        optimised_less_writey_ballot_acceptor_get_instance_info(a, req->iid, &instance_info);

        // Check if ballot is higher than last promised ballot (or accepted ballot)
        if (ballot_greater_than_or_equal(req->ballot, instance_info.promise_ballot)) {
            if (ballot_greater_than(req->ballot, a->max_ballot_wittenessed)){
                a->max_ballot_wittenessed = req->ballot;
            }

            if (ballot_greater_than(a->max_ballot_wittenessed, a->max_ballot_safe_to_promise)) {
                transaction_to_write_new_ballot_epoch(a);
            }


            optimised_less_writey_ballot_acceptor_store_prepare_in_volatile_storage(a, req);
            paxos_log_debug("Promise made");

            out->type = PAXOS_PROMISE;
            out->u.promise = (struct paxos_promise) {
                    .aid = a->id,
                    .iid = req->iid,
                    .ballot = req->ballot,
                    .value_ballot = instance_info.value_ballot,
                    .value = instance_info.value
            };
        } else {
            out->type = PAXOS_PREEMPTED;
            out->u.preempted = (struct paxos_preempted) {
                    .iid = req->iid,
                    .attempted_ballot = req->ballot,
                    .acceptor_current_ballot = instance_info.promise_ballot,
                    .aid = a->id
            };
        }
    } else {
        struct paxos_accept last_accept;
        memset(&last_accept, 0, sizeof(struct paxos_accept));
        get_last_accept(a->volatile_storage, req->iid, &last_accept);

        out->type = PAXOS_CHOSEN;
        out->u.chosen.iid = req->iid;
        out->u.chosen.value = last_accept.value;
        out->u.chosen.ballot = last_accept.ballot;
    }

    is_there_response_message = true;
    return is_there_response_message;
}



static int write_acceptance_to_storage(struct optimised_less_writey_ballot_acceptor* acceptor, struct paxos_accepted* acceptance){
    int success = optimised_less_writey_ballot_acceptor_store_in_all_storages(acceptor, acceptance);
    assert(ballot_equal(acceptance->promise_ballot, acceptance->value_ballot));
    return success;
}

int
optimised_less_writey_ballot_acceptor_receive_accept(struct optimised_less_writey_ballot_acceptor* acceptor,
                                           paxos_accept* request, standard_paxos_message* out)
{
    bool is_response_message = false;
    assert(request->ballot.number > 0);
    assert(request->value.paxos_value_len > 1);
    assert(request->value.paxos_value_val != "");

    bool instance_chosen = false;
    if (request->iid <= acceptor->trim_instance) {
        out->type = PAXOS_TRIM;
        out->u.trim = (struct paxos_trim) {.iid = acceptor->trim_instance};
        return 1;
    }


    is_instance_chosen(acceptor->volatile_storage, request->iid, &instance_chosen);
    if (!instance_chosen) {
        struct paxos_accepted instance_info;
        optimised_less_writey_ballot_acceptor_get_instance_info(acceptor, request->iid, &instance_info);

        if (ballot_greater_than_or_equal(request->ballot, instance_info.promise_ballot)) {
            if (ballot_greater_than(request->ballot, acceptor->max_ballot_wittenessed)){
                acceptor->max_ballot_wittenessed = request->ballot;
            }

            if (optimised_less_writey_ballot_stable_storage_tx_begin(&acceptor->stable_storage) != 0)
                return is_response_message;

            if (ballot_greater_than(acceptor->max_ballot_wittenessed, acceptor->max_ballot_safe_to_promise)) {
                write_new_ballot_epoch(acceptor);
            }

            paxos_log_debug("Accepting iid: %u, ballot: %u", request->iid, request->ballot);
            paxos_accept_to_accepted(acceptor->id, request, out);

            int failed = write_acceptance_to_storage(acceptor, &out->u.accepted);
            if (failed != 0) {
                optimised_less_writey_ballot_stable_storage_tx_abort(&acceptor->stable_storage);
                return is_response_message;
            } else {
                if (optimised_less_writey_ballot_stable_storage_tx_commit(&acceptor->stable_storage) != 0)
                    return is_response_message;
            }

            assert(ballot_equal(out->u.accepted.value_ballot, out->u.accepted.promise_ballot));
        } else {
        //    paxos_accept_request_and_last_acceptor_promise_to_preempted(acceptor->id, request, last_promise_made, out);
        out->type = PAXOS_PREEMPTED;
        out->u.preempted = (struct paxos_preempted) {
            .aid = acceptor->id,
            .iid = request->iid,
            .attempted_ballot = request->ballot,
            .acceptor_current_ballot = instance_info.promise_ballot
        };
        }
    } else {
        struct paxos_accept last_accept;
        memset(&last_accept, 0, sizeof(last_accept));
        get_last_accept(acceptor->volatile_storage, request->iid, &last_accept);
        out->type = PAXOS_CHOSEN;
        paxos_chosen_from_paxos_accept(&out->u.chosen, &last_accept);
    }
    is_response_message = true;
    return is_response_message;
}

int
optimised_less_writey_ballot_acceptor_receive_repeat(struct optimised_less_writey_ballot_acceptor* a, iid_t iid, struct standard_paxos_message* out)
{
//    paxos_log_debug("Received request to repeat instance %u", iid);
    /* if (iid <= a->trim_instance) {
         out->type = PAXOS_TRIM;
         out->u.trim = (struct paxos_trim) {.iid = a->trim_instance};
         return  1;
     }*/

    bool chosen = false;
    is_instance_chosen(a->volatile_storage, iid, &chosen);
    if (chosen) {
        out->type = PAXOS_CHOSEN;
        struct paxos_accept last_accept;
        get_last_accept(a->volatile_storage, iid, &last_accept);
        paxos_chosen_from_paxos_accept(&out->u.chosen, &last_accept);
        return 1;
    } else {
        out->type = PAXOS_ACCEPTED;
        memset(&out->u.accepted, 0, sizeof(struct paxos_accepted));
        int found = get_instance_info(a->volatile_storage, iid, &out->u.accepted);

        copy_ballot(&out->u.accepted.value_ballot, &out->u.accepted.promise_ballot);
        return found && (out->u.accepted.value.paxos_value_len > 0);
    }
}

int
optimised_less_writey_ballot_acceptor_receive_trim(struct optimised_less_writey_ballot_acceptor* a, paxos_trim* trim) {
    paxos_log_debug("Received request to trim from Instance %u", trim->iid);

    //could add mechanism here to workout if acceptor knows the final value for all trimmed instances
    // is less than its min unchosen instance
    // if so would update its trim
    // else it waits sends a repeat (or a new ballot for that instance) message to the other acceptors

    // an acceptor only trims an instances if it knows its chosen value

    iid_t min_unchosen_innstance;
    get_min_unchosen_instance(a->volatile_storage, &min_unchosen_innstance);
    if (trim->iid <= a->trim_instance && trim->iid <= min_unchosen_innstance){
        return 0;
    } else {
        paxos_log_debug("Storing new Trim Instance at Instance %u", trim->iid);
        // unsafe to trim but for experimentation can be taken out so that storages doesn't get out of hand
        if (optimised_less_writey_ballot_stable_storage_tx_begin(&a->stable_storage) != 0)
            return 0;
        optimised_less_writey_ballot_acceptor_store_trim(a, trim->iid); // this does not trim any instances from stable storage
        //optimised_less_writey_ballot_acceptor_store_trim()
        if (optimised_less_writey_ballot_stable_storage_tx_commit(&a->stable_storage) != 0)
            return 0;
        return 1;
    }
}


int optimised_less_writey_ballot_acceptor_receive_chosen(struct optimised_less_writey_ballot_acceptor* a, struct paxos_chosen *chosen){
    paxos_log_debug("Received chosen message for instance %u", chosen->iid);
    struct paxos_accepted instance_info;
//    struct ballot last_stable_promise;
    get_instance_info(a->volatile_storage, chosen->iid, &instance_info);

    //copy_ballot(&last_stable_promise, &instance_info.promise_ballot);
    //copy_ballot(&last_stable_promise, &instance_info.promise_ballot);
    if (ballot_greater_than(chosen->ballot, instance_info.value_ballot)) {
        paxos_log_debug("Storing Chosen message for instance %u");
        paxos_accepted_update_instance_info_with_chosen(&instance_info, chosen, 0);
        optimised_less_writey_ballot_stable_storage_tx_begin(&a->stable_storage);
        optimised_less_writey_ballot_acceptor_store_in_all_storages(a, &instance_info);
        optimised_less_writey_ballot_stable_storage_tx_commit(&a->stable_storage);

    }
    // All proposals higher than the chosen one will propose the same value.
    // Therefore if the proposer has accepted a higher ballot they will already know the chosen value.

    set_instance_chosen(a->volatile_storage, chosen->iid);
    return 0;
}


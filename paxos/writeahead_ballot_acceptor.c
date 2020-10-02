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

#include "writeahead_ballot_acceptor.h"
#include "standard_acceptor.h"

#include "standard_stable_storage.h"
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


struct writeahead_ballot_acceptor {
    // Easy to just extend off of the standard acceptor
  // struct standard_acceptor* standard_acceptor;
    int id;
    struct standard_stable_storage stable_storage;
    iid_t trim_instance;
    

    // Volatile storage to store the "actual" promises made to proposers
    // Will be lost on restart
    // Important to ensure that any promise given to a proposer is written ahead to the
    // standard acceptor's stable storage
    struct paxos_storage* volatile_storage;

    // A stable storage duplicate is included to allow the Acceptor to handle responding to
    // Promise Requests and Accept Requests without ever having to being a transaction in Stable Storage
    // Of course this only applies when an Acceptor doesn't have to safely storage anything
    // Need to ensure that all promises and acceptances written to stable storage are written
    // both the the standard acceptor's and the duplicate
    struct paxos_storage* stable_storage_duplicate; //  TODO add in to speed up access

    // The minimum point in which volatile storage is allowed to catch up with stable storage
    unsigned int min_ballot_catachup;
    unsigned int min_instance_catachup;

    // The number of ballots or instances that should be written ahead of the last stable storage point
    unsigned int ballot_window;
    unsigned int instance_window;

    // Here is the info necessary to determine whether or not to adjust the ballot window of the last updated instance
    // The boolean determines if it does
    // The int determines the instance to update
    // These variables are set when recieving a ballot from a particular instance
    // By flagging during that point, the ballot for the instance can be written ahead after responding to the message
    iid_t* instances_to_begin_new_ballot_epoch;
    unsigned int number_of_instances_to_begin_new_ballot_epoch;

    iid_t last_instance_responded_to; // todo change from loop to just one
    bool updating_instance_epoch;
    iid_t last_instance_epoch_end;
    iid_t last_iteration_end;
    iid_t instance_epoch_iteration_size;
    iid_t new_epoch_end;

};


iid_t get_min_usable_instance(struct writeahead_ballot_acceptor* acceptor) {
    return acceptor->trim_instance + 1;
}

/*

static bool writeahead_ballots_acceptor_is_instance_trimed(struct writeadhead_window_acceptor* acceptor) {
    //todo make into own file
}*/

static int write_ahead_window_acceptor_store_in_stable_storage(struct writeahead_ballot_acceptor* acceptor,
                                                  const struct paxos_accepted* instance_info){
    int success = storage_store_instance_info(&acceptor->stable_storage, instance_info);
    store_instance_info(acceptor->stable_storage_duplicate, instance_info);
    return success;
}

iid_t write_ahead_ballot_acceptor_get_trim(struct writeahead_ballot_acceptor* acceptor){
    return acceptor->trim_instance;
}

// Stores to all types of storage of the Acceptor the instance info.
// For stable storage this will be uncommitted, so the user of the method must commit the stable storage's
// transcation themselves
int
write_ahead_window_acceptor_store_in_all_storages(struct writeahead_ballot_acceptor* acceptor,
                                           const struct paxos_accepted* instance_info)
{
    int success = write_ahead_window_acceptor_store_in_stable_storage(acceptor, instance_info);
    store_instance_info(acceptor->volatile_storage, instance_info);
    return success;
}

void write_ahead_window_acceptor_store_trim(struct writeahead_ballot_acceptor* acceptor, const iid_t trim) {
    acceptor->trim_instance = trim;
    storage_store_trim_instance(&acceptor->stable_storage, trim);
    store_trim_instance(acceptor->stable_storage_duplicate, trim);
    store_trim_instance(acceptor->volatile_storage, trim);
}


void write_ahead_window_acceptor_store_prepare_in_volatile_storage(struct writeahead_ballot_acceptor* acceptor, const struct paxos_prepare* prepare){
    int error = store_last_prepare(acceptor->volatile_storage, prepare);
    assert(error == 0);
}

// When we want to write a head a window of promises from a specific ballot
void
write_ahead_window_acceptor_new_ballot_epoch_from_ballot_number(struct writeahead_ballot_acceptor* acceptor,
                                                                const iid_t instance,
                                                                const uint32_t ballot_number) {
    // todo error checking and reting from method
    // Initialising memory for the instance info variable
    struct paxos_accepted instance_info;
    memset(&instance_info, 0, sizeof(struct paxos_accepted));
    instance_info.iid = instance;

    // Get the instance info from stable storage
    storage_get_instance_info(&acceptor->stable_storage, instance, &instance_info);     // get the acceptor info for that instance
    instance_info.promise_ballot = (struct ballot) {.number = ballot_number + acceptor->ballot_window, .proposer_id = UINT32_MAX};    // write a new accepted message for that instance with the write ahead promise

    // Store new instance info to stable storage
    write_ahead_window_acceptor_store_in_stable_storage(acceptor, &instance_info);
    paxos_log_debug("Writing ahead new ballot epoch to instance %u; new ballot: %u.%u", instance_info.iid, instance_info.promise_ballot.number, instance_info.promise_ballot.proposer_id);
}


// Write a promise ahead from the previous stably stored ballot
// If the instance has never been initialised, then the Acceptor
// simply writes ahead a promise the size of the ballot_window
void write_ahead_window_acceptor_new_ballot_epoch_from_last_epoch(struct writeahead_ballot_acceptor* acceptor,
                                                           iid_t instance){
    //int error = -1;
    // todo error checking and returing from method

    // Initialising memory for the instance info variable
    struct paxos_accepted instance_info;//= calloc(1, sizeof(struct paxos_accepted));
    memset(&instance_info, 0, sizeof(struct paxos_accepted));
    instance_info.iid = instance;
    // Get the instance info from stable storage and update it with the new window
    storage_get_instance_info(&acceptor->stable_storage, instance, &instance_info);     // get the acceptor info for that instnace

    instance_info.promise_ballot = (struct ballot) {.number = instance_info.promise_ballot.number +
                                                              acceptor->ballot_window, .proposer_id = UINT32_MAX};    // write a new accepted message for that instance with the write ahead promise

    // Store new instance info to stable storage
    write_ahead_window_acceptor_store_in_stable_storage(acceptor, &instance_info);

    paxos_log_info("Instance: %u. Written ahead new epoch at ballot %u", instance_info.iid, instance_info.promise_ballot);
    // Cleanup
    //ree(instance_info);
}


static void update_flagged_ballot_windows(struct writeahead_ballot_acceptor *acceptor) {
    if(acceptor->number_of_instances_to_begin_new_ballot_epoch > 0){
        for (iid_t i = 0; i < acceptor->number_of_instances_to_begin_new_ballot_epoch; i++){
            iid_t current_instance = acceptor->instances_to_begin_new_ballot_epoch[i];

            struct paxos_prepare volatile_promise;
            get_last_promise(acceptor->volatile_storage, current_instance, &volatile_promise);

            // if the difference between the stably held ballot and the volatile ballot is less than the min window size
            write_ahead_window_acceptor_new_ballot_epoch_from_ballot_number(acceptor, current_instance,
                                                                            volatile_promise.ballot.number); // write ahead a window from the last volatile ballot
        }
        acceptor->number_of_instances_to_begin_new_ballot_epoch = 0;
        acceptor->instances_to_begin_new_ballot_epoch = realloc(acceptor->instances_to_begin_new_ballot_epoch, 0);
    }

}


bool write_ahead_acceptor_check_ballot_window(struct writeahead_ballot_acceptor* acceptor) {
    return acceptor->number_of_instances_to_begin_new_ballot_epoch > 0;
}

void write_ahead_acceptor_write_ballot_window(struct writeahead_ballot_acceptor* acceptor) {
    update_flagged_ballot_windows(acceptor);
   // for(int i = 0; i < acceptor->number_of_instances_to_begin_new_ballot_epoch; i++) {
    //    write_ahead_window_acceptor_new_ballot_epoch_from_last_epoch(acceptor, acceptor->instances_to_begin_new_ballot_epoch[i]);
   // }

}


/*
 *  WRITING AHEAD PROMISES AND INSTANCES
 */

// write ahead promises for all instances past trim until the end of the instance window of unititiated instnaces
// to be used after recovery
void
write_ahead_window_acceptor_on_recovery_new_epochs(struct writeahead_ballot_acceptor* acceptor) {
    int error;
    iid_t  max_inited_instance;
    error = storage_get_max_inited_instance(&acceptor->stable_storage, &max_inited_instance);

    // for all those instances between the trim and the instance window write ahead promises
    // Dont use write_ahead_window_acceptor_write_next_iteration_of_instance_epoch() here because we would have to get the max_inited_instance from storage again
    iid_t i = get_min_usable_instance(acceptor);// acceptor->trim_instance + 1;
    while (i <= (max_inited_instance + acceptor->instance_window)){
        write_ahead_window_acceptor_new_ballot_epoch_from_last_epoch(acceptor, i);
        i++;
    }

    acceptor->last_instance_epoch_end = i - 1;
    // find out what should be acceptor->last_iteration_end;
    acceptor->updating_instance_epoch = false;
}

/*


// Checks to see if the instance window needs adjusting
static bool
write_ahead_window_acceptor_does_instance_window_need_adjusting(iid_t last_instance_responded_to,
                                                                iid_t last_epoch_end,
                                                                unsigned int min_window_size)
{
    if ((last_instance_responded_to - last_epoch_end) < min_window_size) {
        return true;
    } else {
        return false;
    }
}*/


// write ahead promises for unititinated instances
void
write_ahead_window_acceptor_write_next_iteration_of_instance_epoch(struct writeahead_ballot_acceptor* acceptor) {
    iid_t new_iteration_end = acceptor->last_iteration_end + acceptor->instance_epoch_iteration_size;

    if (new_iteration_end > acceptor->new_epoch_end) // ensures the new_epoch_end is the end of the epoch
        new_iteration_end = acceptor->new_epoch_end;

    for (iid_t i = acceptor->last_iteration_end + 1; i <= new_iteration_end; i++)
        write_ahead_window_acceptor_new_ballot_epoch_from_last_epoch(acceptor, i);
    paxos_log_info("Written ahead iteration of new Epoch to Instance %u", new_iteration_end);
    acceptor->last_iteration_end = new_iteration_end;
}


/*
void check_and_update_instance_window(struct writeahead_ballot_acceptor *acceptor) {
    // Will not do if already updating
    if (write_ahead_window_acceptor_does_instance_window_need_adjusting(acceptor->last_instance_responded_to,
                                                                 acceptor->last_instance_epoch_end,
                                                                 acceptor->min_instance_catachup) && !acceptor->updating_instance_epoch) {
        acceptor->updating_instance_epoch = true;
        acceptor->new_epoch_end = acceptor->last_instance_epoch_end + acceptor->instance_window;

        // This tells the acceptor where to start writing ahead the next epoch
        acceptor->last_iteration_end = acceptor->last_instance_epoch_end;
        paxos_log_debug("Instance Window has caught up."
                        "Beginning new epoch.");
    }





}


// Meant to be a method to check the windows (ballot and instance) after replying to a proposer message
// could be async but not really supported
void
write_ahead_window_acceptor_check_and_update_write_ahead_windows(struct writeahead_ballot_acceptor* acceptor) {
    // TODO add error checking and handling
    // for each instance if the ballot last promised ballot in memory is too close to the one in stable storage then write ahead

    // for the write ahead instance
    // if the max init instance is near the the write ahead point then write initiate those instances
    update_flagged_ballot_windows(acceptor);

    // Get the maximum instance id to be initialised (promised or accepted in)
    iid_t max_inited_instance; //= calloc(1, sizeof(iid_t));
    //storage_tx_begin(&acceptor->standard_stable_storage);
    //storage_get_max_inited_instance(&acceptor->standard_stable_storage, &max_inited_instance);
    get_max_inited_instance(acceptor->stable_storage_duplicate, &max_inited_instance);
    check_and_update_instance_window(acceptor);
    //storage_tx_commit(&acceptor->standard_stable_storage);

   // free(max_inited_instance);
}

*/

/*
 * RECOVERY, INITIALISATION, DEALLOCATION AND SETTERS
 */



// This method takes all those ballots
void
write_ahead_window_acceptor_copy_to_paxos_storage_from_stable_storage(struct standard_stable_storage* stable_storage,
                                                               struct paxos_storage *store_to_copy_to,
                                                               iid_t from_instance) {
    // Todo add error handling
    int error;

    iid_t* max_ininted_instance = calloc(1, sizeof(iid_t));


    storage_tx_begin(stable_storage);
    error = storage_get_max_inited_instance(stable_storage, max_ininted_instance);


    for (unsigned int i = from_instance; i <= *max_ininted_instance; i++) {
        paxos_accepted *instance_info = NULL;
        //memset(instance_info, 0, sizeof(paxos_accepted));
        if ((error = storage_get_instance_info(stable_storage, i, instance_info)) == 0) {
            error = store_instance_info(store_to_copy_to, instance_info);
        }
    }

    storage_tx_commit(stable_storage);
    free(max_ininted_instance);
}



struct writeahead_ballot_acceptor *
write_ahead_window_acceptor_new(int id, int min_ballot_catchup, int bal_window)
{
    int error;



    // writeahead_ballot_acceptor->last_instance_responded_to = 0;
    // Some explaination about what is happening here:
    // When the Acceptor restarts it can assume that the last ballot held in stable storage is safe as it will have
    // never previously responded to a ballot higher.
    // So we can allow the acceptor to respond to ballots greater than or equal to that ballot.
    // We then Write ahead promises for previously initiated instances and
    // initiate some new instances with written ahead promises



    struct writeahead_ballot_acceptor* acceptor =  calloc(1, sizeof(struct writeahead_ballot_acceptor));
    acceptor->id = id;

    //stable storage setup
 //   acceptor->standard_acceptor = //standard_acceptor_new(id);//write_ahead_window_acceptor_init_standard_acceptor(id, standard_stable_storage);

    storage_init_lmdb_write_ahead_ballots(&acceptor->stable_storage, id);

    if (storage_open(&acceptor->stable_storage) != 0) {
        free(acceptor);
        return NULL;
    }


    // get instances info from stable storage
    error = storage_tx_begin(&acceptor->stable_storage);
    paxos_accepted* instances_info ;// calloc(1, sizeof(struct paxos_accepted));//calloc(0, sizeof(struct paxos_accepted)); // not initialised as it is unknown how many instances will be retrieved
    int* number_of_instances = calloc(1, sizeof(int));
    error = storage_get_all_untrimmed_instances_info(&acceptor->stable_storage, &instances_info, number_of_instances);

    // get trim id from stable storage
    iid_t trim_id = 0;
    error = storage_get_trim_instance(&acceptor->stable_storage, &trim_id);
    acceptor->trim_instance = trim_id;

  //  write_ahead_window_acceptor_store_trim(acceptor, trim_id);

    error = storage_tx_commit(&acceptor->stable_storage);

    //copy to ss duplicate
    struct paxos_storage* stable_storage_duplicate = calloc(1, sizeof(struct paxos_storage));
    init_hash_mapped_memory_from_instances_info(stable_storage_duplicate, instances_info, *number_of_instances, acceptor->trim_instance, id);

    //copy to volitile storage
    struct paxos_storage* volatile_storage = calloc(1, sizeof(struct paxos_storage));
    init_hash_mapped_memory_from_instances_info(volatile_storage, instances_info, *number_of_instances, acceptor->trim_instance, id);


    // Set up write ahead acceptor variables
    acceptor->stable_storage_duplicate = stable_storage_duplicate;
    acceptor->volatile_storage = volatile_storage;
    acceptor->ballot_window = bal_window;
    acceptor->min_ballot_catachup = min_ballot_catchup;

   // writeahead_ballot_acceptor->last_instance_needs_new_ballot_epoch = false;

    // Data related to writing ballot epochs
    acceptor->instances_to_begin_new_ballot_epoch = calloc(0, sizeof(iid_t));
    acceptor->number_of_instances_to_begin_new_ballot_epoch = 0;

    // Data related to writing new instance epochs
    // -- some of these values will be set again
    // after writing ahead the new instance epoch
    acceptor->last_instance_responded_to = 0;
    acceptor->updating_instance_epoch = false;
    acceptor->last_instance_epoch_end = 0;
    acceptor->new_epoch_end = 0;
    acceptor->last_iteration_end = 0;

    iid_t max_instance;

    storage_tx_begin(&acceptor->stable_storage);
//    store_trim_instance()
    storage_get_max_inited_instance(&acceptor->stable_storage, &max_instance);

    for (unsigned int i = get_min_usable_instance(acceptor); i < max_instance; i++){
        // volatile
        // stable
        // stable dup all equal
        struct paxos_accepted stable_stored_info;
        storage_get_instance_info(&acceptor->stable_storage, i, &stable_stored_info);

        struct paxos_prepare stable_dup_prepare;
        get_last_promise(acceptor->stable_storage_duplicate, i, &stable_dup_prepare);

        struct paxos_accept stable_dup_accept;
        get_last_accept(acceptor->stable_storage_duplicate, i, &stable_dup_accept);


        struct paxos_prepare volatile_prepare;
        get_last_promise(acceptor->volatile_storage, i, &volatile_prepare);


        assert(stable_stored_info.iid == stable_dup_accept.iid && stable_dup_accept.iid == stable_dup_prepare.iid && stable_dup_prepare.iid == volatile_prepare.iid);
        assert(ballot_equal(stable_stored_info.promise_ballot, stable_dup_prepare.ballot));
        assert(ballot_equal(stable_stored_info.value_ballot, stable_dup_accept.ballot));
        assert(ballot_equal(stable_stored_info.promise_ballot, stable_dup_prepare.ballot) && ballot_equal(stable_dup_prepare.ballot, volatile_prepare.ballot));
        assert(is_values_equal(stable_stored_info.value, stable_dup_accept.value));
    }

    // Write ahead promises and initiate new instances
    write_ahead_window_acceptor_on_recovery_new_epochs(acceptor);

    storage_tx_commit(&acceptor->stable_storage);
    return acceptor;
}


struct paxos_prepare get_empty_prepare();

void write_ahead_window_acceptor_check_and_flag_instance_for_new_ballot_epoch(struct writeahead_ballot_acceptor *a,
                                                                              const struct ballot last_promise,
                                                                              const struct ballot last_stable_promise,
                                                                              iid_t instance);

struct paxos_prepare get_empty_prepare() {
    struct paxos_prepare last_volatile_promise;
    memset(&last_volatile_promise, 0, sizeof(struct paxos_prepare));
    return last_volatile_promise;
}

struct paxos_accept get_empty_accept();

void
write_ahead_window_acceptor_free(struct writeahead_ballot_acceptor* a)
{
    storage_close(&a->stable_storage);
    free(a);
}

struct paxos_accept get_empty_accept() {
    struct paxos_accept last_accept;
    memset(&last_accept, 0, sizeof(struct paxos_accept));
    return last_accept;
}


void
write_ahead_window_acceptor_get_current_state(struct writeahead_ballot_acceptor* a, paxos_standard_acceptor_state* state) {
    paxos_log_debug("Getting current state to: aid: %u, trim iid %u", a->id, a->trim_instance);
    state->aid = a->id;
    state->trim_iid = a->trim_instance;
    get_max_inited_instance(a->stable_storage_duplicate, &state->current_instance);
}


/*
 * HANDLING OF MESSAGES RECEIVED
 * -----------------------------
 */

// Acceptor handler for receiving a prepare message.
// If returned 0 then there is no message/response from the acceptor,
// otherwise, there is.
int
write_ahead_window_acceptor_receive_prepare(struct writeahead_ballot_acceptor* a, paxos_prepare* req, standard_paxos_message* out) {

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
        int found = get_instance_info(a->volatile_storage, req->iid, &instance_info);

        // Check if ballot is higher than last promised ballot (or accepted ballot)
        if (!found || ballot_greater_than_or_equal(req->ballot, instance_info.promise_ballot)) {
            a->last_instance_responded_to = req->iid;

            struct paxos_prepare last_stable_promise;
            memset(&last_stable_promise, 0, sizeof(last_stable_promise));
            int written_to_ss = get_last_promise(a->stable_storage_duplicate, req->iid, &last_stable_promise);

            assert(written_to_ss);
            if (ballot_greater_than(req->ballot, last_stable_promise.ballot)) {
                paxos_log_debug("Storing new promise to stable storage");
                if (storage_tx_begin(&a->stable_storage) != 0) {
                    return is_there_response_message;
                }
                write_ahead_window_acceptor_new_ballot_epoch_from_ballot_number(a, req->iid, req->ballot.number);
                if (storage_tx_commit(&a->stable_storage) != 0) {
                    return is_there_response_message;
                }
            } else {
                write_ahead_window_acceptor_check_and_flag_instance_for_new_ballot_epoch(a, req->ballot,
                                                                                         last_stable_promise.ballot, req->iid);
            }
            write_ahead_window_acceptor_store_prepare_in_volatile_storage(a, req);
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
      //      union_paxos_prepare_and_last_acceptor_promise_to_preempted(a->id, req, &last_volatile_promise, out);
        }
    } else {
        struct paxos_accept last_accept;
        memset(&last_accept, 0, sizeof(struct paxos_accept));
        get_last_accept(a->stable_storage_duplicate, req->iid, &last_accept);

        out->type = PAXOS_CHOSEN;
        out->u.chosen.iid = req->iid;
        out->u.chosen.value = last_accept.value;
        out->u.chosen.ballot = last_accept.ballot;
     //   paxos_chosen_from_paxos_accept(&out->u.chosen, &last_accept);
    }

    is_there_response_message = true;
    return is_there_response_message;
}


// has the ballot window gotten too small?
bool
write_ahead_window_acceptor_does_instance_ballot_window_need_adjusting(uint32_t written_ahead_ballot,
                                                                       uint32_t min_ballot_window_size,
                                                                       uint32_t last_volatile_ballot)
{
    if ((written_ahead_ballot - last_volatile_ballot) < min_ballot_window_size) {
        return true;
    } else {
        return false;
    }
}


bool instance_already_flagged(struct writeahead_ballot_acceptor *a, const iid_t instance){
    for (unsigned int i = 0; i < a->number_of_instances_to_begin_new_ballot_epoch; i++)
        if (a->instances_to_begin_new_ballot_epoch[i] == instance)
            return true;

    return false;
}

void write_ahead_window_acceptor_check_and_flag_instance_for_new_ballot_epoch(struct writeahead_ballot_acceptor *a,
                                                                              const struct ballot last_promise,
                                                                              const struct ballot last_stable_promise,
                                                                              iid_t instance) {
    if(write_ahead_window_acceptor_does_instance_ballot_window_need_adjusting(last_stable_promise.number, a->min_ballot_catachup, last_promise.number) && !instance_already_flagged(a, instance)){
        paxos_log_debug("Flagged instance for writing ahead new ballot epoch");
        a->number_of_instances_to_begin_new_ballot_epoch++;
       a->instances_to_begin_new_ballot_epoch = realloc(a->instances_to_begin_new_ballot_epoch,
               sizeof(iid_t) * a->number_of_instances_to_begin_new_ballot_epoch);
      a->instances_to_begin_new_ballot_epoch[a->number_of_instances_to_begin_new_ballot_epoch - 1] = instance;
    }
}

static int write_acceptance_to_storage(struct writeahead_ballot_acceptor* acceptor, struct paxos_accepted* acceptance){
    struct paxos_accepted to_store;
    paxos_accepted_copy(&to_store, acceptance);

    // Must rewrite the ballot epoch to stable storage
    struct paxos_prepare last_stable_promise;
    get_last_promise(acceptor->stable_storage_duplicate, acceptance->iid, &last_stable_promise);
    copy_ballot(&last_stable_promise.ballot, &to_store.promise_ballot);

    assert(to_store.iid == acceptance->iid);
    assert(ballot_equal(to_store.value_ballot, acceptance->value_ballot));
    assert(ballot_equal(to_store.promise_ballot, last_stable_promise.ballot));
    assert(is_values_equal(acceptance->value, to_store.value));

    int success = write_ahead_window_acceptor_store_in_all_storages(acceptor, &to_store);


    write_ahead_window_acceptor_store_prepare_in_volatile_storage(acceptor, &(struct paxos_prepare){acceptance->iid, acceptance->value_ballot});

    assert(ballot_equal(acceptance->promise_ballot, acceptance->value_ballot));
    return success;
}

int
write_ahead_window_acceptor_receive_accept(struct writeahead_ballot_acceptor* acceptor,
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
        struct paxos_prepare last_promise_made;
        memset(&last_promise_made, 0, sizeof(last_promise_made));
        int was_previous_promise = get_last_promise(acceptor->volatile_storage, request->iid, &last_promise_made);

        if (!was_previous_promise || ballot_greater_than_or_equal(request->ballot, last_promise_made.ballot)) {
            acceptor->last_instance_responded_to = request->iid;
            if (storage_tx_begin(&acceptor->stable_storage) != 0)
                return is_response_message;

            paxos_log_debug("Accepting iid: %u, ballot: %u", request->iid, request->ballot);
            paxos_accept_to_accepted(acceptor->id, request, out);

            int failed = write_acceptance_to_storage(acceptor, &out->u.accepted);
            if (failed != 0) {
                storage_tx_abort(&acceptor->stable_storage);
                return is_response_message;
            } else {
                if (storage_tx_commit(&acceptor->stable_storage) != 0)
                    return is_response_message;
            }

            assert(ballot_equal(out->u.accepted.value_ballot, out->u.accepted.promise_ballot));
        } else {
            paxos_accept_request_and_last_acceptor_promise_to_preempted(acceptor->id, request, last_promise_made, out);
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
write_ahead_window_acceptor_receive_repeat(struct writeahead_ballot_acceptor* a, iid_t iid, struct standard_paxos_message* out)
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
        int found = get_instance_info(a->stable_storage_duplicate, iid, &out->u.accepted);

        copy_ballot(&out->u.accepted.value_ballot, &out->u.accepted.promise_ballot);
        return found && (out->u.accepted.value.paxos_value_len > 0);
    }
}

int
write_ahead_window_acceptor_receive_trim(struct writeahead_ballot_acceptor* a, paxos_trim* trim) {
    paxos_log_debug("Received request to trim from Instance %u", trim->iid);

    //could add mechanism here to workout if acceptor knows the final value for all trimmed instances 
    // is less than its min unchosen instance
    // if so would update its trim
    // else it waits sends a repeat (or a new ballot for that instance) message to the other acceptors

    // an acceptor only trims an instances if it knows its chosen value

    iid_t min_unchosen_innstance;
    get_min_unchosen_instance(a->stable_storage_duplicate, &min_unchosen_innstance);
    if (trim->iid <= a->trim_instance && trim->iid <= min_unchosen_innstance){
        return 0;
    } else {
        paxos_log_debug("Storing new Trim Instance at Instance %u", trim->iid);
        // unsafe to trim but for experimentation can be taken out so that storages doesn't get out of hand
        if (storage_tx_begin(&a->stable_storage) != 0)
            return 0;
        write_ahead_window_acceptor_store_trim(a, trim->iid); // this does not trim any instances from stable storage
        //write_ahead_window_acceptor_store_trim()
        storage_store_trim_instance(&a->stable_storage, trim->iid);
        if (storage_tx_commit(&a->stable_storage) != 0)
            return 0;
        return 1;
    }
}


int write_ahead_ballot_acceptor_receive_chosen(struct writeahead_ballot_acceptor* a, struct paxos_chosen *chosen){
    paxos_log_debug("Received chosen message for instance %u", chosen->iid);
    struct paxos_accepted instance_info;
//    struct ballot last_stable_promise;
    get_instance_info(a->stable_storage_duplicate, chosen->iid, &instance_info);

    //copy_ballot(&last_stable_promise, &instance_info.promise_ballot);
    //copy_ballot(&last_stable_promise, &instance_info.promise_ballot);
    if (ballot_greater_than(chosen->ballot, instance_info.value_ballot)) {
        paxos_log_debug("Storing Chosen message for instance %u");
        paxos_accepted_update_instance_info_with_chosen(&instance_info, chosen, 0);
        storage_tx_begin(&a->stable_storage);
        write_ahead_window_acceptor_store_in_all_storages(a, &instance_info);
        storage_tx_commit(&a->stable_storage);

    }
    // All proposals higher than the chosen one will propose the same value.
    // Therefore if the proposer has accepted a higher ballot they will already know the chosen value.

    set_instance_chosen(a->volatile_storage, chosen->iid);
    return 0;
}

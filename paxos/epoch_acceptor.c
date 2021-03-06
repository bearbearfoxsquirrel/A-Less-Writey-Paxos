//
// Created by Michael Davis on 04/02/2020.
//

#include <paxos.h>
#include <epoch_stable_storage.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <epoch_paxos_storage.h>
#include <paxos_types.h>
#include <paxos_message_conversion.h>
#include <stdbool.h>
#include <ballot.h>
#include <epoch_ballot.h>

struct epoch_acceptor {
    int id;
    struct epoch_stable_storage stable_storage;
    struct epoch_paxos_storage volatile_storage;
    iid_t trim_instance; // here for fast access
    uint32_t current_epoch;
    iid_t max_proposed_instance;
};

static int writeahead_epoch_acceptor_increase_epoch(struct epoch_acceptor* acceptor, uint32_t new_epoch){
    paxos_log_debug("Storing new Epoch %u", new_epoch);
    // stored epochs should only ever increase
   // assert(new_epoch >= acceptor->current_epoch);
    acceptor->current_epoch = new_epoch;
    return epoch_stable_storage_store_epoch(&acceptor->stable_storage, new_epoch);
}

static void writeahead_epoch_acceptor_store_trim(struct epoch_acceptor* acceptor, iid_t trim) {
    paxos_log_debug("Storing new Trim to Instance %u", trim);
   // assert(trim >= acceptor->trim_instance);
    acceptor->trim_instance = trim;
    //epoch_stable_storage_store_trim_instance(&acceptor->stable_storage, trim);
    epoch_paxos_storage_store_trim_instance(&acceptor->volatile_storage, trim);
}

static void create_epoch_notification_message(struct epoch_notification *recover_message,
                                       const struct epoch_acceptor *acceptor) {
   // recover_message->type = WRITEAHEAD_EPOCH_NOTIFICATION;
    recover_message->new_epoch = acceptor->current_epoch;
}

iid_t writeahead_epoch_acceptor_get_max_proposed_instance(struct epoch_acceptor* acceptor) {
    return acceptor->max_proposed_instance;
}


struct epoch_acceptor *
epoch_acceptor_new(int id, struct epoch_notification *recover_message, bool *has_recovery_message) {
    struct epoch_acceptor* acceptor = calloc(1, sizeof(struct epoch_acceptor));
    acceptor->id = id;
    epoch_stable_storage_init(&acceptor->stable_storage, id);

    if (epoch_stable_storage_open(&acceptor->stable_storage) != 0) {
        free(acceptor);
        return NULL;
    }

    epoch_stable_storage_tx_begin(&acceptor->stable_storage);

    // Increment epoch
    uint32_t old_epoch = INVALID_EPOCH;
    int was_previous_epoch = epoch_stable_storage_get_current_epoch(&acceptor->stable_storage, &old_epoch);
    if (was_previous_epoch != 0) {
        uint32_t recovery_epoch = old_epoch + 1;
        writeahead_epoch_acceptor_increase_epoch(acceptor, recovery_epoch);
    } else {
        writeahead_epoch_acceptor_increase_epoch(acceptor, INVALID_EPOCH);
    }

    // Recover all accepts from stable storage and copy them to the volatile storage (for faster access and no disk writes during promising)
    epoch_stable_storage_get_trim_instance(&acceptor->stable_storage, &acceptor->trim_instance);
    int number_of_accepts = 0;
    struct epoch_ballot_accept* all_accepted_epoch_ballot_accepts;
    epoch_stable_storage_get_all_untrimmed_epoch_ballot_accepts(&acceptor->stable_storage, &all_accepted_epoch_ballot_accepts, &number_of_accepts);

    epoch_paxos_storage_init_with_prepares_and_accepts(&acceptor->volatile_storage, NULL, 0,
                                                       &all_accepted_epoch_ballot_accepts, number_of_accepts, 0); // TODO check if old accepteds the min promise for the epoch is better

    iid_t trim = 0;
    epoch_stable_storage_get_trim_instance(&acceptor->stable_storage, &trim);
    writeahead_epoch_acceptor_store_trim(acceptor, trim);

    // Storage not used past here, so can commit tx
    epoch_stable_storage_tx_commit(&acceptor->stable_storage);

    // double check epoch was incremented correctly & volatile storage matches stable
    uint32_t incremented_epoch = 0;
    epoch_stable_storage_tx_begin(&acceptor->stable_storage);
    epoch_stable_storage_get_current_epoch(&acceptor->stable_storage, &incremented_epoch);

    //uint32_t max_instance = 0;
  //  epoch_stable_storage_get_max_inited_instance(&acceptor->stable_storage, &max_instance);
  //  for (unsigned int i =0; i <= max_instance; i++){
        // todo
  //  }

    epoch_stable_storage_tx_commit(&acceptor->stable_storage);

   // assert(incremented_epoch > old_epoch);

    if (acceptor->current_epoch > 0) {
        create_epoch_notification_message(recover_message, acceptor);
        *has_recovery_message = true;
    } else {
        *has_recovery_message = false;
    }

    return acceptor;
}

void writeahead_epoch_acceptor_free(struct epoch_acceptor* acceptor){
    epoch_stable_storage_close(&acceptor->stable_storage);
    free(acceptor);
}

void
epoch_ballot_promise_check_and_set_last_accepted_ballot(struct epoch_paxos_message *returned_message,
                                                        struct epoch_ballot_accept *last_accept, bool previous_accept) {
    if (previous_accept) {
        returned_message->message_contents.epoch_ballot_promise.last_accepted_ballot = (*last_accept).epoch_ballot_requested;
        returned_message->message_contents.epoch_ballot_promise.last_accepted_value = (*last_accept).value_to_accept;
    } else {
        returned_message->message_contents.epoch_ballot_promise.last_accepted_ballot = INVALID_EPOCH_BALLOT;
        returned_message->message_contents.epoch_ballot_promise.last_accepted_value = INVALID_VALUE;
    }
}

void writeahead_epoch_acceptor_set_epoch_promise(const struct epoch_acceptor *acceptor,
                                                 iid_t promised_instance,
                                                 struct epoch_ballot promised_epoch_ballot,
                                                 struct epoch_paxos_message *returned_message,
                                                 struct epoch_ballot_accept *last_accept, bool previous_accept) {
    returned_message->type = WRITEAHEAD_EPOCH_BALLOT_PROMISE;
    returned_message->message_contents.epoch_ballot_promise.instance = promised_instance;
    returned_message->message_contents.epoch_ballot_promise.acceptor_id = acceptor->id;
    returned_message->message_contents.epoch_ballot_promise.promised_epoch_ballot = promised_epoch_ballot;
    epoch_ballot_promise_check_and_set_last_accepted_ballot(returned_message, last_accept, previous_accept);
}


int handle_making_premepted(const struct epoch_acceptor *acceptor, iid_t instance, struct epoch_ballot requested_epoch_ballot,
                            struct epoch_paxos_message *returned_message, struct paxos_prepare *last_prepare, char* phase) {
    int is_a_message_returned;
    paxos_log_debug( "%s Request for Instance %u with Ballot %u.%u Preempted by another Epoch Ballot. Returning Preemption",
               phase, instance, requested_epoch_ballot.ballot.number, requested_epoch_ballot.ballot.proposer_id);

    struct epoch_ballot last_epoch_ballot = (struct epoch_ballot) {.epoch = acceptor->current_epoch, .ballot = last_prepare->ballot};

    returned_message->type = WRITEAHEAD_EPOCH_BALLOT_PREEMPTED;
    returned_message->message_contents.epoch_ballot_preempted = (struct epoch_ballot_preempted) {
       .instance = instance,
       .acceptor_id = acceptor->id,
       .requested_epoch_ballot = requested_epoch_ballot,
       .acceptors_current_epoch_ballot = last_epoch_ballot
    };

    is_a_message_returned = 1;
    return is_a_message_returned;
}

int writeahead_epoch_acceptor_receive_prepare(struct epoch_acceptor* acceptor, struct paxos_prepare* request, struct epoch_paxos_message* returned_message){
    paxos_log_debug("Handling Standard Promise Request in Instance %u for Epoch Ballot %u.%u", request->iid, request->ballot.number, request->ballot.proposer_id);
    int is_a_message_returned = 0;

    if (request->iid <= acceptor->trim_instance) {
        paxos_log_debug("Promise Request for Instance %u by Proposer %u has been Trimmed already",
                request->iid, request->ballot.proposer_id);
        returned_message->type = WRITEAHEAD_INSTANCE_TRIM;
        returned_message->message_contents.trim = (struct paxos_trim) {.iid = acceptor->trim_instance};
        is_a_message_returned = 1;
        return is_a_message_returned;
    }


    struct paxos_prepare last_prepare;
    bool previous_promise = epoch_paxos_storage_get_last_prepare(&acceptor->volatile_storage, request->iid, &last_prepare);

    struct epoch_ballot_accept last_accept;
    bool previous_accept = epoch_paxos_storage_get_last_accept(&acceptor->volatile_storage, request->iid, &last_accept);

    bool instance_chosen = false;
    epoch_paxos_storage_is_instance_chosen(&acceptor->volatile_storage, request->iid, &instance_chosen);
    if (instance_chosen) {
        paxos_log_debug("Promise Request for Instance %u by Proposer %u has been rejected. It is already Chosen",
                request->iid, request->ballot.proposer_id);
        union_epoch_ballot_chosen_from_epoch_ballot_accept(returned_message, &last_accept);
        is_a_message_returned = 1;
        return is_a_message_returned;
    }

        // Compare to instance's last prepare made

    if (ballot_greater_than_or_equal(request->ballot, last_prepare.ballot) || !previous_promise) {
        if (request->iid > acceptor->max_proposed_instance) {
            acceptor->max_proposed_instance = request->iid;
        }

        paxos_log_debug("New highest ballot (%u.%u) for instance %u. It\'s been Promised on Epoch %u",
                request->ballot.number,
                request->ballot.proposer_id,
                request->iid,
                acceptor->current_epoch);

        // Store the prepare (in volatile storage)
        epoch_paxos_storage_store_last_prepare(&acceptor->volatile_storage, request);
        struct epoch_ballot promised_epoch_ballot = (struct epoch_ballot) {.epoch = acceptor->current_epoch, .ballot = request->ballot};
        writeahead_epoch_acceptor_set_epoch_promise(acceptor, request->iid, promised_epoch_ballot, returned_message, &last_accept, previous_accept);

        is_a_message_returned = 1;
   } else {
       // return preempted
       struct epoch_ballot requested_epoch_ballot = (struct epoch_ballot) {.epoch = acceptor->current_epoch,
                                                                           .ballot = request->ballot};
        is_a_message_returned = handle_making_premepted(acceptor, request->iid, requested_epoch_ballot,
                                                        returned_message, &last_prepare, "Promise");

    }
    return is_a_message_returned;
}

int write_ahead_epoch_acceptor_transaction_to_increment_epoch(struct epoch_acceptor *acceptor,
                                                              const struct epoch_ballot *cmp_epoch_ballot) {
    if (epoch_stable_storage_tx_begin(&acceptor->stable_storage) != 0){
        return 1;
    }
    if (writeahead_epoch_acceptor_increase_epoch(acceptor, cmp_epoch_ballot->epoch) != 0) {
        return 1;
    }
    if (epoch_stable_storage_tx_commit(&acceptor->stable_storage) != 0)
    {
        return 1;
    }
    return 0;
}

int writeahead_epoch_acceptor_receive_epoch_ballot_prepare(struct epoch_acceptor *acceptor,
                                                           struct epoch_ballot_prepare *request,
                                                           struct epoch_paxos_message *returned_message,
                                                           struct epoch_ballot_preempted *preempted,
                                                           bool *previous_preempted) {
    paxos_log_debug("Handling Epoch Promise Request in Instance %u for Epoch Ballot %u.%u", request->instance, request->epoch_ballot_requested.epoch, request->epoch_ballot_requested.ballot.number, request->epoch_ballot_requested.ballot.proposer_id);
    int is_a_message_returned = 0;

    if (request->instance <= acceptor->trim_instance) {
        paxos_log_debug("Promise Request for Instance %u by Proposer %u has been Trimmed already",
                        request->instance, request->epoch_ballot_requested.ballot.proposer_id);
        returned_message->type = WRITEAHEAD_INSTANCE_TRIM;
        returned_message->message_contents.trim = (struct paxos_trim) {.iid = acceptor->trim_instance};
        is_a_message_returned = 1;
        return is_a_message_returned;
    }

    struct paxos_prepare last_prepare;
    bool was_previous_promise = epoch_paxos_storage_get_last_prepare(&acceptor->volatile_storage, request->instance, &last_prepare);

    struct epoch_ballot_accept last_accept;
    bool previous_accept = epoch_paxos_storage_get_last_accept(&acceptor->volatile_storage, request->instance, &last_accept);

    bool was_instance_chosen = false;
    bool double_check = epoch_paxos_storage_is_instance_chosen(&acceptor->volatile_storage, request->instance, &was_instance_chosen);
   // assert(was_instance_chosen == double_check);
    if (was_instance_chosen) {
        paxos_log_debug("Promise Request for Instance %u by Proposer %u has been rejected. It is already Chosen",
                        request->instance, request->epoch_ballot_requested.ballot.proposer_id);
   //     union_epoch_ballot_chosen_from_epoch_ballot_accept(returned_message, &last_accept);
        returned_message->type = WRITEAHEAD_INSTANCE_CHOSEN_AT_EPOCH_BALLOT;
        returned_message->message_contents.instance_chosen_at_epoch_ballot = (struct epoch_ballot_chosen) {
                .instance = last_accept.instance,
                .chosen_epoch_ballot = last_accept.epoch_ballot_requested
        };
        copy_value(&last_accept.value_to_accept, &returned_message->message_contents.instance_chosen_at_epoch_ballot.chosen_value);
        is_a_message_returned = 1;
        return is_a_message_returned;
    }

    // Compare to instance's last prepare made

    struct epoch_ballot min_epoch_ballot_to_give_promise_on = (struct epoch_ballot) {.epoch = acceptor->current_epoch,
                                                                                     .ballot = last_prepare.ballot};
    // Don't want to give promises on anything less than the current epoch or the current ballot
    // (even though might have given a promise on a lesser epoch ballot)
    if (epoch_ballot_greater_than_or_equal(request->epoch_ballot_requested, min_epoch_ballot_to_give_promise_on) || !was_previous_promise) {
        if (request->instance > acceptor->max_proposed_instance) {
            acceptor->max_proposed_instance = request->instance;
        }

        paxos_log_debug("New highest Epoch Ballot Promised (%u.%u.%u) for Instance %u",
                        request->epoch_ballot_requested.epoch,
                        request->epoch_ballot_requested.ballot.number,
                        request->epoch_ballot_requested.ballot.proposer_id,
                        request->instance);

        if (request->epoch_ballot_requested.epoch > acceptor->current_epoch){
           //// assert(1 != 1);
            if (write_ahead_epoch_acceptor_transaction_to_increment_epoch(acceptor, &request->epoch_ballot_requested) != 0) {
                paxos_log_debug("Failed to write epoch to disk, cancelling promise");
                return 0;
            }
        }

        if (ballot_greater_than(last_prepare.ballot, INVALID_BALLOT)) {
            *previous_preempted = true;
            preempted->instance = request->instance;
            preempted->requested_epoch_ballot = min_epoch_ballot_to_give_promise_on;
            preempted->acceptors_current_epoch_ballot = request->epoch_ballot_requested;
            preempted->acceptor_id = acceptor->id;
        }

        struct paxos_prepare to_store = (struct paxos_prepare) {
            .iid = request->instance,
            .ballot = request->epoch_ballot_requested.ballot
        };
        epoch_paxos_storage_store_last_prepare(&acceptor->volatile_storage, &to_store);
        writeahead_epoch_acceptor_set_epoch_promise(acceptor, request->instance, request->epoch_ballot_requested, returned_message, &last_accept, previous_accept);
        is_a_message_returned = 1;
    } else {
        is_a_message_returned = handle_making_premepted(acceptor, request->instance, request->epoch_ballot_requested,
                                                        returned_message, &last_prepare, "Promise");
    }
    return is_a_message_returned;
}

void writeahead_epoch_acceptor_transaction_to_store_accept(struct epoch_acceptor *acceptor,
                                                           struct epoch_ballot_accept *accept, bool is_chosen) {
    struct paxos_prepare prepare_to_store = (struct paxos_prepare) {
            .iid = accept->instance,
            .ballot = accept->epoch_ballot_requested.ballot
    };

    if (!is_chosen) {
            epoch_stable_storage_tx_begin(&acceptor->stable_storage);
            if (accept->epoch_ballot_requested.epoch > acceptor->current_epoch) {
                writeahead_epoch_acceptor_increase_epoch(acceptor, accept->epoch_ballot_requested.epoch);
            }

            epoch_stable_storage_store_epoch_ballot_accept(&acceptor->stable_storage, accept);
            epoch_stable_storage_tx_commit(&acceptor->stable_storage);

            // assert(accept->epoch_ballot_requested.epoch == acceptor->current_epoch);
    }
    bool promised = epoch_paxos_storage_store_last_prepare(&acceptor->volatile_storage, &prepare_to_store);
    bool success = epoch_paxos_storage_store_accept(&acceptor->volatile_storage, accept);
   // assert(success && promised);
}


int writeahead_epoch_acceptor_receive_epoch_ballot_accept(struct epoch_acceptor *acceptor,
                                                          struct epoch_ballot_accept *request,
                                                          struct epoch_paxos_message *response,
                                                          struct epoch_ballot_preempted *prev_preempted,
                                                          bool *was_prev_preempted) {
    paxos_log_debug("Handling Accept Request in Instance %u for Epoch Ballot %u.%u", request->instance, request->epoch_ballot_requested.epoch, request->epoch_ballot_requested.ballot.number, request->epoch_ballot_requested.ballot.proposer_id);
    int is_a_message_returned = 0;

    if (request->instance <= acceptor->trim_instance) {
        paxos_log_debug("Acceptance Request for Instance %u by Proposer %u has been Trimmed already",
                        request->instance, request->epoch_ballot_requested.ballot.proposer_id);
        response->type = WRITEAHEAD_INSTANCE_TRIM;
        response->message_contents.trim = (struct paxos_trim) {.iid = acceptor->trim_instance};
        is_a_message_returned = 1;
        return is_a_message_returned;
    }

   // assert(strncmp(request->value_to_accept.paxos_value_val, "", 1));
   // assert(request->value_to_accept.paxos_value_len > 0);
    paxos_log_debug("value in accept");
    struct paxos_prepare last_prepare;
    bool was_previous_promise = epoch_paxos_storage_get_last_prepare(&acceptor->volatile_storage, request->instance,
                                                                     &last_prepare);


    bool was_instance_chosen = false;
    epoch_paxos_storage_is_instance_chosen(&acceptor->volatile_storage, request->instance, &was_instance_chosen);
    if (was_instance_chosen) {
        struct epoch_ballot_accept last_accept;
        bool previous_accept = epoch_paxos_storage_get_last_accept(&acceptor->volatile_storage, request->instance,
                                                                   &last_accept);

        paxos_log_debug("Promise Request for Instance %u by Proposer %u has been rejected. It is already Chosen",
                        request->instance, request->epoch_ballot_requested.ballot.proposer_id);
        response->type = WRITEAHEAD_INSTANCE_CHOSEN_AT_EPOCH_BALLOT;
        response->message_contents.instance_chosen_at_epoch_ballot = (struct epoch_ballot_chosen) {
            .instance = last_accept.instance,
            .chosen_epoch_ballot = last_accept.epoch_ballot_requested
        };
        copy_value(&last_accept.value_to_accept, &response->message_contents.instance_chosen_at_epoch_ballot.chosen_value);
     //   union_epoch_ballot_chosen_from_epoch_ballot_accept(response, &last_accept);
        is_a_message_returned = 1;
        return is_a_message_returned;
    }

    // Compare to instance's last prepare made

    struct epoch_ballot min_epoch_ballot_to_accept = (struct epoch_ballot) {.epoch = acceptor->current_epoch,
            .ballot = last_prepare.ballot};
    // Don't want to give promises on anything less than the current epoch or the current ballot
    // (even though might have given a promise on a lesser epoch ballot)

    if (epoch_ballot_greater_than_or_equal(request->epoch_ballot_requested, min_epoch_ballot_to_accept) ||
            (!was_previous_promise && request->epoch_ballot_requested.epoch >= acceptor->current_epoch)) {
        if (request->instance > acceptor->max_proposed_instance) {
            acceptor->max_proposed_instance = request->instance;
        }


       // assert(epoch_ballot_greater_than_or_equal(request->epoch_ballot_requested, last_accept.epoch_ballot_requested));

        paxos_log_debug("New highest Epoch Ballot Accepted (%u.%u.%u) for Instance %u",
                        request->epoch_ballot_requested.epoch,
                        request->epoch_ballot_requested.ballot.number,
                        request->epoch_ballot_requested.ballot.proposer_id,
                        request->instance);

        writeahead_epoch_acceptor_transaction_to_store_accept(acceptor, request, false);
        paxos_log_debug("stored value");

   //     bool was_last_accept = epoch_paxos_storage_get_last_accept(&acceptor->volatile_storage, request->instance,
     //                                                              &last_accept);
       // assert(was_last_accept);
        paxos_log_debug("ensured value was stored");

        response->type = WRITEAHEAD_EPOCH_BALLOT_ACCEPTED;
        response->message_contents.epoch_ballot_accepted = (struct epoch_ballot_accepted) {
                .instance = request->instance,
                .acceptor_id = acceptor->id,
                .accepted_epoch_ballot = request->epoch_ballot_requested,
                .accepted_value = request->value_to_accept
        };

        if (ballot_greater_than(last_prepare.ballot, INVALID_BALLOT)) {
            *was_prev_preempted = true;
            prev_preempted->instance = request->instance;
            prev_preempted->requested_epoch_ballot = min_epoch_ballot_to_accept;
            prev_preempted->acceptors_current_epoch_ballot = request->epoch_ballot_requested;
            prev_preempted->acceptor_id = acceptor->id;
        }

        is_a_message_returned = 1;
       // assert(strncmp(response->message_contents.epoch_ballot_accepted.accepted_value.paxos_value_val, "", 2));
    } else {
        is_a_message_returned = handle_making_premepted(acceptor, request->instance, request->epoch_ballot_requested,
                                                        response, &last_prepare, "Acceptance");
    }
    return is_a_message_returned;
}


int  writeahead_epoch_acceptor_receive_repeat(struct epoch_acceptor* acceptor, iid_t iid, struct epoch_paxos_message* response){
    paxos_log_debug("Handling Repeat for Instance %u", iid);
    bool chosen = false;
    bool double_check = epoch_paxos_storage_is_instance_chosen(&acceptor->volatile_storage, iid, &chosen);
assert(chosen == double_check);

    struct epoch_ballot_accept last_accept;
    bool was_accept =epoch_paxos_storage_get_last_accept(&acceptor->volatile_storage, iid, &last_accept);

    struct paxos_prepare last_prepare;
    bool was_promise = epoch_paxos_storage_get_last_prepare(&acceptor->volatile_storage, iid, &last_prepare);

    if (chosen) {
       // assert(was_accept);
        response->type = WRITEAHEAD_INSTANCE_CHOSEN_AT_EPOCH_BALLOT;
        response->message_contents.instance_chosen_at_epoch_ballot = (struct epoch_ballot_chosen) {
            .instance = last_accept.instance,
            .chosen_epoch_ballot = last_accept.epoch_ballot_requested,
            .chosen_value = last_accept.value_to_accept
        };
        paxos_log_debug("Responding with Chosen");
        return 1;
    } else {
        response->type = WRITEAHEAD_EPOCH_BALLOT_ACCEPTED;
        response->message_contents.epoch_ballot_accepted = (struct epoch_ballot_accepted) {
            .acceptor_id = acceptor->id,
            .instance = last_accept.instance,
            .accepted_epoch_ballot = last_accept.epoch_ballot_requested,
            .accepted_value = last_accept.value_to_accept
        };
        bool was_response = was_accept && ballot_greater_than(last_accept.epoch_ballot_requested.ballot, INVALID_BALLOT);
    //   // assert(was_accept);
        if (!was_response) {
            paxos_log_debug("Wasn't a previous Accept so ignoring");
        } else {
            paxos_log_debug("Responding with last Accept");
        }
        return was_response;
    }
}

int  writeahead_epoch_acceptor_receive_trim(struct epoch_acceptor* acceptor, struct paxos_trim* trim){
    paxos_log_debug("Entering trim");
    iid_t  min_unchosen_instance;
    epoch_paxos_storage_get_min_unchosen_instance(&acceptor->volatile_storage, &min_unchosen_instance);
    bool new_trim = trim->iid > acceptor->trim_instance;
    bool able_to_trim = min_unchosen_instance > trim->iid;
    if (new_trim && able_to_trim) {
        paxos_log_debug("Storing new Trim to Instance %u", trim->iid);
       // epoch_stable_storage_tx_begin(&acceptor->stable_storage);
        writeahead_epoch_acceptor_store_trim(acceptor, trim->iid);

        long to_trim_from = trim->iid - 10000;
        if (to_trim_from > INVALID_INSTANCE) {
            paxos_log_debug("Acceptor trimming stored Instances to %u", to_trim_from);
            epoch_paxos_storage_trim_instances_less_than(&acceptor->volatile_storage, to_trim_from);
        //    epoch_stable_storage_trim_instances_less_than(&acceptor->stable_storage, to_trim_from);
        }

        if (trim->iid > acceptor->max_proposed_instance) {
            acceptor->max_proposed_instance = trim->iid;
        }

       // epoch_stable_storage_tx_commit(&acceptor->stable_storage);
        paxos_log_debug("Leaving trim");
        return 1;
    } else {
        if (!new_trim) {
            paxos_log_debug("Disregarding Trim request to Instance %u, it is old", trim->iid);
        } else if (!able_to_trim) { // I know, I know. Always true. But I think it reads better
            paxos_log_debug("Disregarding Trim request to Instance %u, not yet caught up to this point", trim->iid);
        }
        paxos_log_debug("Leaving trim");
        return 0;
    }
}

int  writeahead_epoch_acceptor_receive_epoch_notification(struct epoch_acceptor* acceptor, struct epoch_notification* epoch_notification){
   if (epoch_notification->new_epoch > acceptor->current_epoch) {
      writeahead_epoch_acceptor_increase_epoch(acceptor, epoch_notification->new_epoch);
       return 1;
   }
   return 0; // no message to send
}

int writeahead_epoch_acceptor_get_current_state(struct epoch_acceptor* acceptor, struct writeahead_epoch_acceptor_state* state) {
    paxos_log_debug("Entering current acceptor_state");
    state->current_epoch = acceptor->current_epoch;
    state->standard_acceptor_state.trim_iid = acceptor->trim_instance;
    state->standard_acceptor_state.aid = acceptor->id;
    state->standard_acceptor_state.current_instance = acceptor->max_proposed_instance;

   // epoch_paxos_storage_get_max_inited_instance(&acceptor->volatile_storage, &acceptor_state->standard_acceptor_state.current_instance);
    paxos_log_debug("Leaving current acceptor_state");
    return 1;
}

int writeahead_epoch_acceptor_receive_instance_chosen(struct epoch_acceptor* acceptor, struct epoch_ballot_chosen *chosen_message){
   paxos_log_debug("Entering chosen");
    struct epoch_ballot_accept last_accept;
    bool was_accept = epoch_paxos_storage_get_last_accept(&acceptor->volatile_storage, chosen_message->instance, &last_accept);

    if (chosen_message->instance > acceptor->max_proposed_instance) {
        acceptor->max_proposed_instance = chosen_message->instance;
    }

    bool is_chosen = false;
    epoch_paxos_storage_is_instance_chosen(&acceptor->volatile_storage, chosen_message->instance, &is_chosen);
    if (is_chosen) {
        paxos_log_debug("Ignoring chosen message for instance %u as it is already chosen", chosen_message->instance);
        return 0;
    }

    // todo update function so old promise is restored

    paxos_log_debug("Storing new Chosen for Instance %u");
    epoch_paxos_storage_set_instance_chosen(&acceptor->volatile_storage, chosen_message->instance);

    struct epoch_ballot_accept new_accept = (struct epoch_ballot_accept) {
        .instance = chosen_message->instance,
        .epoch_ballot_requested = chosen_message->chosen_epoch_ballot,
        .value_to_accept = chosen_message->chosen_value
    };
    writeahead_epoch_acceptor_transaction_to_store_accept(acceptor, &new_accept, true);

    struct epoch_ballot_accept stored_accept;
    bool accepted = epoch_paxos_storage_get_last_accept(&acceptor->volatile_storage, chosen_message->instance, &stored_accept);
       // assert(accepted);
       // assert(stored_accept.instance == chosen_message->instance);
       // assert(epoch_ballot_equal(stored_accept.epoch_ballot_requested, chosen_message->chosen_epoch_ballot));
     //   for (int i = 0; i < chosen_message->chosen_value.paxos_value_len; i ++){
           // assert(chosen_message->chosen_value.paxos_value_val[i] == stored_accept.value_to_accept.paxos_value_val[i]);
    //    }

     //   bool is_chosen_fo_sho = false;
     //   bool double_check = epoch_paxos_storage_is_instance_chosen(&acceptor->volatile_storage, chosen_message->instance, &is_chosen_fo_sho);
    //   // assert(is_chosen_fo_sho && double_check);


      //  bool was_last_accept = epoch_paxos_storage_get_last_accept(&acceptor->volatile_storage, chosen_message->instance,
   //                                                                &last_accept);
     //   paxos_log_debug("Checking chosen was stored");
      // // assert(was_last_accept);
       // paxos_log_debug("stored correctly");
  //  } else {
   //     paxos_log_debug("Ignoring Chosen as it is an old Chosen message");
  //  }

    iid_t min_unchosen_instance;
    epoch_paxos_storage_get_min_unchosen_instance(&acceptor->volatile_storage, &min_unchosen_instance);
    if (min_unchosen_instance > acceptor->trim_instance) {
        struct paxos_trim trim = {min_unchosen_instance - 1};
        writeahead_epoch_acceptor_receive_trim(acceptor, &trim);
    }
    paxos_log_debug("Leaving chosen");
    return 1;
}


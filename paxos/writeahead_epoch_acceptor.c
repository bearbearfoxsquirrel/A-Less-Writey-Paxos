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

struct writeahead_epoch_acceptor {
    int id;
    struct epoch_stable_storage stable_storage;
    struct epoch_paxos_storage volatile_storage;
    iid_t trim_instance; // here for fast access
    uint32_t current_epoch;

    iid_t next_instance_to_preprepare;
    iid_t preprepred_window;
    iid_t max_proposed_instance;
    iid_t max_instanes_preprepared;
};

static int writeahead_epoch_acceptor_increase_epoch(struct writeahead_epoch_acceptor* acceptor, uint32_t new_epoch){
    // stored epochs should only ever increase
    assert(new_epoch >= acceptor->current_epoch);
    acceptor->current_epoch = new_epoch;
    return epoch_stable_storage_store_epoch(&acceptor->stable_storage, new_epoch);
}

static void writeahead_epoch_acceptor_store_trim(struct writeahead_epoch_acceptor* acceptor, iid_t trim) {
    assert(trim >= acceptor->trim_instance);
    acceptor->trim_instance = trim;
    epoch_stable_storage_store_trim_instance(&acceptor->stable_storage, trim);
    epoch_paxos_storage_store_trim_instance(&acceptor->volatile_storage, trim);
}
/*
bool writeahead_epoch_acceptor_epoch_ballot_greater_than_or_equal_to(struct epoch_ballot* left, struct epoch_ballot* right) {
    if (left->epoch > right->epoch) return true;
 //   else if (left->epoch == right->epoch && left->ballot >= right->ballot) return true;
    else return false;
}*/

static void create_epoch_notification_message(struct epoch_notification *recover_message,
                                       const struct writeahead_epoch_acceptor *acceptor) {
   // recover_message->type = WRITEAHEAD_EPOCH_NOTIFICATION;
    recover_message->new_epoch = acceptor->current_epoch;
}

iid_t writeahead_epoch_acceptor_get_max_proposed_instance(struct writeahead_epoch_acceptor* acceptor) {
    return acceptor->max_proposed_instance;
}

iid_t writeahead_epoch_acceptor_get_next_instance_to_prewrite(struct writeahead_epoch_acceptor* acceptor) {
    return acceptor->next_instance_to_preprepare;
}

iid_t writeahead_epoch_acceptor_get_max_instances_prewrite(struct writeahead_epoch_acceptor* acceptor) {
    return acceptor->max_instanes_preprepared;
}

iid_t writeahead_epoch_acceptor_number_of_instance_to_prewrite_at_once(struct writeahead_epoch_acceptor* acceptor) {
    return acceptor->preprepred_window;
}

void writeahead_epoch_acceptor_prewrite_instances(struct writeahead_epoch_acceptor *acceptor, iid_t start, iid_t stop,
                                                  uint32_t dummy_value_size) {
    //todo make use a start and stop so recovery can prewrite from trim to way ahead of max inited
    // todo and then the event can only do it for a window
    // todo add avalue size
  //  if(acceptor->next_instance_to_preprepare < acceptor->max_proposed_instance + acceptor->max_instanes_preprepared){
        epoch_stable_storage_tx_begin(&acceptor->stable_storage);
        char* dummy_value = malloc(sizeof(char) * dummy_value_size);

        for (iid_t i = start; i < stop; i++) {
            struct epoch_ballot_accept accept = (struct epoch_ballot_accept) {
                    .instance = i,
                    .epoch_ballot_requested = (struct epoch_ballot) {INVALID_EPOCH, INVALID_BALLOT},
                    .value_to_accept = INVALID_VALUE
            };
            accept.value_to_accept.paxos_value_len = dummy_value_size;//paxos_value_new(doodle, 5000);
            accept.value_to_accept.paxos_value_val = dummy_value;

            struct paxos_prepare prepare_to_store = (struct paxos_prepare) {
                    .iid = i,
                    .ballot = INVALID_BALLOT
            };
            epoch_paxos_storage_store_last_prepare(&acceptor->volatile_storage, &prepare_to_store);
            epoch_stable_storage_store_epoch_ballot_accept(&acceptor->stable_storage, &accept);
            epoch_paxos_storage_store_accept(&acceptor->volatile_storage, &accept);
        }
        epoch_stable_storage_tx_commit(&acceptor->stable_storage);
        acceptor->next_instance_to_preprepare = acceptor->next_instance_to_preprepare + acceptor->preprepred_window;

        free(dummy_value);

        paxos_log_debug("Preprepared to Instance %u", acceptor->next_instance_to_preprepare);
  //  }

}



struct writeahead_epoch_acceptor *
writeahead_epoch_acceptor_new(int id, struct epoch_notification *recover_message, bool *has_recovery_message,
                              iid_t prepareparing_window_size, iid_t max_number_of_prewritten_instances,
                              uint32_t expected_value_size) {
    struct writeahead_epoch_acceptor* acceptor = calloc(1, sizeof(struct writeahead_epoch_acceptor));
    acceptor->id = id;
    epoch_stable_storage_init(&acceptor->stable_storage, id);

    if (epoch_stable_storage_open(&acceptor->stable_storage) != 0) {
        free(acceptor);
        return NULL;
    }

    epoch_stable_storage_tx_begin(&acceptor->stable_storage);

    // Increment epoch
    uint32_t old_epoch = 0;
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

    assert(incremented_epoch > old_epoch);

    acceptor->preprepred_window = prepareparing_window_size;
    epoch_paxos_storage_get_max_inited_instance(&acceptor->volatile_storage, &acceptor->next_instance_to_preprepare);
    acceptor->max_proposed_instance =  acceptor->next_instance_to_preprepare;
    acceptor->next_instance_to_preprepare++;
    acceptor->max_instanes_preprepared = max_number_of_prewritten_instances;


    writeahead_epoch_acceptor_prewrite_instances(acceptor, acceptor->trim_instance, trim + number_of_accepts + max_number_of_prewritten_instances, expected_value_size);

    if (acceptor->current_epoch > 0) {
        create_epoch_notification_message(recover_message, acceptor);
        *has_recovery_message = true;
    } else {
        *has_recovery_message = false;
    }

    return acceptor;
}

void writeahead_epoch_acceptor_free(struct writeahead_epoch_acceptor* acceptor){
    epoch_stable_storage_close(&acceptor->stable_storage);
    free(acceptor);
}

void
epoch_ballot_promise_check_and_set_last_accepted_ballot(struct writeahead_epoch_paxos_message *returned_message,
                                                        struct epoch_ballot_accept *last_accept, bool previous_accept) {
    if (previous_accept) {
        returned_message->message_contents.epoch_ballot_promise.last_accepted_ballot = (*last_accept).epoch_ballot_requested;
        returned_message->message_contents.epoch_ballot_promise.last_accepted_value = (*last_accept).value_to_accept;
    } else {
        returned_message->message_contents.epoch_ballot_promise.last_accepted_ballot = INVALID_EPOCH_BALLOT;
        returned_message->message_contents.epoch_ballot_promise.last_accepted_value = INVALID_VALUE;
    }
}

void writeahead_epoch_acceptor_set_epoch_promise(const struct writeahead_epoch_acceptor *acceptor,
                                                 iid_t promised_instance,
                                                 struct epoch_ballot promised_epoch_ballot,
                                                 struct writeahead_epoch_paxos_message *returned_message,
                                                 struct epoch_ballot_accept *last_accept, bool previous_accept) {
    returned_message->type = WRITEAHEAD_EPOCH_BALLOT_PROMISE;
    returned_message->message_contents.epoch_ballot_promise.instance = promised_instance;
    returned_message->message_contents.epoch_ballot_promise.acceptor_id = acceptor->id;
    returned_message->message_contents.epoch_ballot_promise.promised_epoch_ballot = promised_epoch_ballot;
    epoch_ballot_promise_check_and_set_last_accepted_ballot(returned_message, last_accept, previous_accept);
}


int handle_making_premepted(const struct writeahead_epoch_acceptor *acceptor, iid_t instance, struct epoch_ballot requested_epoch_ballot,
                            struct writeahead_epoch_paxos_message *returned_message, struct paxos_prepare *last_prepare, char* phase) {
    int is_a_message_returned;
    paxos_log_debug( "%s Request for Instance %u with Ballot %u.%u Preempted by another Epoch Ballot. Returning Preemption",
               phase, instance, requested_epoch_ballot.ballot.number, requested_epoch_ballot.ballot.proposer_id);

    struct epoch_ballot last_epoch_ballot = (struct epoch_ballot) {.epoch = acceptor->current_epoch, .ballot =(*last_prepare).ballot};

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

int writeahead_epoch_acceptor_receive_prepare(struct writeahead_epoch_acceptor* acceptor, struct paxos_prepare* request, struct writeahead_epoch_paxos_message* returned_message){
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

int write_ahead_epoch_acceptor_transaction_to_increment_epoch(struct writeahead_epoch_acceptor *acceptor,
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

int writeahead_epoch_acceptor_receive_epoch_ballot_prepare(struct writeahead_epoch_acceptor* acceptor, struct epoch_ballot_prepare* request, struct writeahead_epoch_paxos_message* returned_message){
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
    epoch_paxos_storage_is_instance_chosen(&acceptor->volatile_storage, request->instance, &was_instance_chosen);
    if (was_instance_chosen) {
        paxos_log_debug("Promise Request for Instance %u by Proposer %u has been rejected. It is already Chosen",
                        request->instance, request->epoch_ballot_requested.ballot.proposer_id);
        union_epoch_ballot_chosen_from_epoch_ballot_accept(returned_message, &last_accept);
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
           // assert(1 != 1);
            if (write_ahead_epoch_acceptor_transaction_to_increment_epoch(acceptor, &request->epoch_ballot_requested) != 0) {
                return 0;
            }
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

void writeahead_epoch_acceptor_transaction_to_store_accept(struct writeahead_epoch_acceptor *acceptor,
                                                           struct epoch_ballot_accept *accept, bool is_chosen) {
    struct paxos_prepare prepare_to_store = (struct paxos_prepare) {
            .iid = accept->instance,
            .ballot = accept->epoch_ballot_requested.ballot
    };

    epoch_stable_storage_tx_begin(&acceptor->stable_storage);
    if (accept->epoch_ballot_requested.epoch > acceptor->current_epoch){
        writeahead_epoch_acceptor_increase_epoch(acceptor, accept->epoch_ballot_requested.epoch);
    }
    epoch_stable_storage_store_epoch_ballot_accept(&acceptor->stable_storage, accept);
    epoch_stable_storage_tx_commit(&acceptor->stable_storage);

    if (!is_chosen) {
        assert(accept->epoch_ballot_requested.epoch == acceptor->current_epoch);
    }
    epoch_paxos_storage_store_last_prepare(&acceptor->volatile_storage, &prepare_to_store);
    epoch_paxos_storage_store_accept(&acceptor->volatile_storage, accept);
}


int writeahead_epoch_acceptor_receive_epoch_ballot_accept(struct writeahead_epoch_acceptor* acceptor, struct epoch_ballot_accept* request, struct writeahead_epoch_paxos_message* response) {
    int is_a_message_returned = 0;

    if (request->instance <= acceptor->trim_instance) {
        paxos_log_debug("Acceptance Request for Instance %u by Proposer %u has been Trimmed already",
                        request->instance, request->epoch_ballot_requested.ballot.proposer_id);
        response->type = WRITEAHEAD_INSTANCE_TRIM;
        response->message_contents.trim = (struct paxos_trim) {.iid = acceptor->trim_instance};
        is_a_message_returned = 1;
        return is_a_message_returned;
    }


    struct paxos_prepare last_prepare;
    bool was_previous_promise = epoch_paxos_storage_get_last_prepare(&acceptor->volatile_storage, request->instance,
                                                                     &last_prepare);

    struct epoch_ballot_accept last_accept;
     bool previous_accept = epoch_paxos_storage_get_last_accept(&acceptor->volatile_storage, request->instance,
                                                               &last_accept);

    bool was_instance_chosen = false;
    epoch_paxos_storage_is_instance_chosen(&acceptor->volatile_storage, request->instance, &was_instance_chosen);
    if (was_instance_chosen) {
        paxos_log_debug("Promise Request for Instance %u by Proposer %u has been rejected. It is already Chosen",
                        request->instance, request->epoch_ballot_requested.ballot.proposer_id);
        union_epoch_ballot_chosen_from_epoch_ballot_accept(response, &last_accept);
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


  //      assert(request->instance < acceptor->next_instance_to_preprepare);

        if (request->instance > acceptor->next_instance_to_preprepare){
            acceptor->next_instance_to_preprepare = request->instance;
        }



        paxos_log_debug("New highest Epoch Ballot Accepted (%u.%u.%u) for Instance %u",
                        request->epoch_ballot_requested.epoch,
                        request->epoch_ballot_requested.ballot.number,
                        request->epoch_ballot_requested.ballot.proposer_id,
                        request->instance);

        writeahead_epoch_acceptor_transaction_to_store_accept(acceptor, request, false);

        bool was_last_accept = epoch_paxos_storage_get_last_accept(&acceptor->volatile_storage, request->instance,
                                                                   &last_accept);
        assert(was_last_accept);

        response->type = WRITEAHEAD_EPOCH_BALLOT_ACCEPTED;
        response->message_contents.epoch_ballot_accepted = (struct epoch_ballot_accepted) {
                .instance = request->instance,
                .acceptor_id = acceptor->id,
                .accepted_epoch_ballot = last_accept.epoch_ballot_requested,
                .accepted_value = last_accept.value_to_accept
        };
        is_a_message_returned = 1;
        assert(strncmp(response->message_contents.epoch_ballot_accepted.accepted_value.paxos_value_val, "", 2));
    } else {
        is_a_message_returned = handle_making_premepted(acceptor, request->instance, request->epoch_ballot_requested,
                                                        response, &last_prepare, "Acceptance");
    }
    return is_a_message_returned;
}


int  writeahead_epoch_acceptor_receive_repeat(struct writeahead_epoch_acceptor* acceptor, iid_t iid, struct writeahead_epoch_paxos_message* response){
    bool chosen = false;
    epoch_paxos_storage_is_instance_chosen(&acceptor->volatile_storage, iid, &chosen);

    struct epoch_ballot_accept last_accept;
    bool was_accept =epoch_paxos_storage_get_last_accept(&acceptor->volatile_storage, iid, &last_accept);


    if (chosen) {
        assert(was_accept);
        response->type = WRITEAHEAD_INSTANCE_CHOSEN_AT_EPOCH_BALLOT;
        response->message_contents.instance_chosen_at_epoch_ballot = (struct epoch_ballot_chosen) {
            .instance = last_accept.instance,
            .chosen_epoch_ballot = last_accept.epoch_ballot_requested,
            .chosen_value = last_accept.value_to_accept
        };
        return 1;
    } else {
        response->type = WRITEAHEAD_EPOCH_BALLOT_ACCEPTED;
        response->message_contents.epoch_ballot_accepted = (struct epoch_ballot_accepted) {
            .acceptor_id = acceptor->id,
            .instance = last_accept.instance,
            .accepted_epoch_ballot = last_accept.epoch_ballot_requested,
            .accepted_value = last_accept.value_to_accept
        };
        return was_accept && ballot_greater_than(last_accept.epoch_ballot_requested.ballot, INVALID_BALLOT) ;
    }
}

int  writeahead_epoch_acceptor_receive_trim(struct writeahead_epoch_acceptor* acceptor, struct paxos_trim* trim){
    iid_t  min_unchosen_instance;
    epoch_paxos_storage_get_min_unchosen_instance(&acceptor->volatile_storage, &min_unchosen_instance);
    bool new_trim = trim->iid > acceptor->trim_instance;
    bool able_to_trim = min_unchosen_instance > trim->iid;
    if (new_trim && able_to_trim) {
        paxos_log_debug("Storing new Trim to Instance %u", trim->iid);
        epoch_stable_storage_tx_begin(&acceptor->stable_storage);
        writeahead_epoch_acceptor_store_trim(acceptor, trim->iid);
        epoch_stable_storage_tx_commit(&acceptor->stable_storage);
        return 1;
    } else {
        if (!new_trim) {
            paxos_log_debug("Disregarding Trim request to Instance %u, it is old", trim->iid);
        } else if (!able_to_trim) { // I know, I know. Always true. But I think it reads better
            paxos_log_debug("Disregarding Trim request to Instance %u, not yet caught up to this point", trim->iid);
        }
        return 0;
    }
}

int  writeahead_epoch_acceptor_receive_epoch_notification(struct writeahead_epoch_acceptor* acceptor, struct epoch_notification* epoch_notification){
   if (epoch_notification->new_epoch > acceptor->current_epoch) {
      writeahead_epoch_acceptor_increase_epoch(acceptor, epoch_notification->new_epoch);
       return 1;
   }
   return 0; // no message to send
}

int writeahead_epoch_acceptor_get_current_state(struct writeahead_epoch_acceptor* acceptor, struct writeahead_epoch_acceptor_state* state) {
    state->current_epoch = acceptor->current_epoch;
    state->standard_acceptor_state.trim_iid = acceptor->trim_instance;
    state->standard_acceptor_state.aid = acceptor->id;
    epoch_paxos_storage_get_max_inited_instance(&acceptor->volatile_storage, &state->standard_acceptor_state.current_instance);
    return 1;
}

int writeahead_epoch_acceptor_receive_instance_chosen(struct writeahead_epoch_acceptor* acceptor, struct epoch_ballot_chosen *chosen_message){
    struct epoch_ballot_accept last_accept;
    bool was_accept = epoch_paxos_storage_get_last_accept(&acceptor->volatile_storage, chosen_message->instance, &last_accept);

    if (chosen_message->instance > acceptor->max_proposed_instance) {
        acceptor->max_proposed_instance = chosen_message->instance;
    }

    epoch_paxos_storage_set_instance_chosen(&acceptor->volatile_storage, chosen_message->instance); // set to chosen always as this could have been forgotten

    if (epoch_ballot_greater_than(chosen_message->chosen_epoch_ballot, last_accept.epoch_ballot_requested) || !was_accept) {
        paxos_log_debug("Storing new Chosen for Instance %u");

        struct epoch_ballot_accept new_accept = (struct epoch_ballot_accept) {
            .instance = chosen_message->instance,
            .epoch_ballot_requested = chosen_message->chosen_epoch_ballot,
            .value_to_accept = chosen_message->chosen_value
        };
        writeahead_epoch_acceptor_transaction_to_store_accept(acceptor, &new_accept, true);
    } else {
        paxos_log_debug("Ignoring Chosen as it is an old Chosen message");
    }
    return 1;
}


//
// Created by Michael Davis on 05/04/2020.
//

#include <paxos.h>
#include <quorum.h>
#include <epoch_quorum.h>
#include <assert.h>
#include <epoch_proposer.h>
#include <epoch_ballot.h>


struct instance {
    iid_t instance;
    struct epoch_ballot most_recent_epoch_ballot;
    struct quorum quorum;
    struct paxos_value current_ballots_value;
    bool chosen;
};

KHASH_MAP_INIT_INT(instance, struct instance*)

struct epoch_learner {
    int acceptors;
    int acceptance_quorum_size;
    bool late_start;
    iid_t current_min_instance_to_execute;
    iid_t highest_instance_chosen;
    khash_t(instance)* instances_waiting_to_execute;
    iid_t trim_instance;
};

static struct instance* epoch_learner_instance_new(iid_t instance, int number_of_acceptors, int acceptance_quorum_size) {
    struct instance* inst = malloc(sizeof(struct instance));
    inst->instance = instance;
    quorum_init(&inst->quorum, number_of_acceptors, acceptance_quorum_size);
    inst->most_recent_epoch_ballot = INVALID_EPOCH_BALLOT;
    inst->current_ballots_value = INVALID_VALUE;
    inst->chosen = false;
    return inst;
}

static void epoch_learner_instance_free(struct instance** inst) {
    quorum_destroy(&(**inst).quorum);
    free(*inst);
    *inst = NULL;
}

static bool epoch_learner_get_existing_instance(struct epoch_learner* l, iid_t instance, struct instance** inst) {
    khiter_t k = kh_get_instance(l->instances_waiting_to_execute, instance);
    if (k != kh_end(l->instances_waiting_to_execute)) {
        if (kh_exist(l->instances_waiting_to_execute, k) == 1) {
            *inst = kh_value(l->instances_waiting_to_execute, k);
            return true;
        }
    }
    return false;
}

static struct instance* epoch_learner_get_or_create_instance(struct epoch_learner* l, iid_t instance) {
    struct instance* inst;
    bool instance_existing = epoch_learner_get_existing_instance(l, instance, &inst);
    if (!instance_existing) {
        int rv;
        khiter_t k = kh_put_instance(l->instances_waiting_to_execute, instance, &rv);
        assert(rv != -1);
        inst = epoch_learner_instance_new(instance, l->acceptors, l->acceptance_quorum_size);
        kh_value(l->instances_waiting_to_execute, k) = inst;
    }
    return inst;
}

static void epoch_learner_remove_instance_from_pending(struct epoch_learner* l, struct instance** inst) {
    khiter_t k = kh_get_instance(l->instances_waiting_to_execute, (**inst).instance);
    kh_del_instance(l->instances_waiting_to_execute, k);
    epoch_learner_instance_free(inst);
}


struct epoch_learner* epoch_learner_new(int acceptors){
    struct epoch_learner* learner = malloc(sizeof(struct epoch_learner));
    learner->acceptors = acceptors;
    learner->acceptance_quorum_size = paxos_config.quorum_2;
    learner->highest_instance_chosen = INVALID_INSTANCE;
    learner->current_min_instance_to_execute = 1;
    learner->trim_instance = INVALID_INSTANCE;
    learner->late_start = !paxos_config.learner_catch_up;
    learner->instances_waiting_to_execute = kh_init_instance();
    return learner;
}

void epoch_learner_free(struct epoch_learner** l){
    struct instance* inst;
    kh_foreach_value((*l)->instances_waiting_to_execute, inst, epoch_learner_instance_free(&inst));
    kh_destroy(instance, (*l)->instances_waiting_to_execute);
    free(*l);
    *l = NULL;
}

void epoch_learner_set_trim_instance(struct epoch_learner* l, iid_t trim){
    assert(trim > l->trim_instance);
    l->trim_instance = trim;
}

iid_t epoch_learner_get_trim_instance(struct epoch_learner* l) {
    return l->trim_instance;
}

void epoch_learner_set_instance_id(struct epoch_learner* l, iid_t iid){
    if (l->current_min_instance_to_execute > INVALID_INSTANCE) {
        for (iid_t i = l->current_min_instance_to_execute; i <= iid; i++) {
            struct instance *inst;
            epoch_learner_get_existing_instance(l, i, &inst);
            assert(quorum_reached(&inst->quorum));
        }
    }
    l->current_min_instance_to_execute = iid + 1;
    l->highest_instance_chosen = iid;
}

void check_and_handle_late_start(struct epoch_learner* l, iid_t instance) {
    if (l->late_start) {
        l->late_start = false;
        l->current_min_instance_to_execute = instance;
        l->trim_instance = instance - 1;
    }
}

bool epoch_learner_is_instance_outdated(struct epoch_learner* l, iid_t instance, char* message_type) {
    if (instance < l->current_min_instance_to_execute) {
        paxos_log_debug("Dropped %s for Instance %u. Instance has already been closed and executed", message_type, instance);
        return true;
    } else {
        return false;
    }
}

bool epoch_learner_is_instance_chosen(struct instance* inst, char* message_type){
    if (inst->chosen) {
        paxos_log_debug("Dropping %s Message for Instance %u. It is already Chosen.", message_type, inst->instance);
        return true;
    } else {
        return false;
    }
}

bool epoch_learner_is_epoch_ballot_outdated(const struct instance* inst, struct epoch_ballot cmp, char* message_type) {
    if (epoch_ballot_greater_than_or_equal(cmp, inst->most_recent_epoch_ballot)){
        paxos_log_debug("Received %s Message for Instance %u at Epoch Ballot %u.%u.%u", message_type,
                        inst->instance,
                        cmp.epoch,
                        cmp.ballot.number, cmp.ballot.proposer_id);
        return false;
    } else {
        paxos_log_debug("Received Acceptance for Instance is out of date. Ignoring it.", inst->instance);
        return true;
    }
}

void check_and_handle_new_ballot(struct instance* inst, struct epoch_ballot cmp, struct paxos_value value) {
    if (epoch_ballot_greater_than(cmp, inst->most_recent_epoch_ballot)) {
        if (!epoch_ballot_equal(inst->most_recent_epoch_ballot, INVALID_EPOCH_BALLOT)){
            paxos_value_destroy(&inst->current_ballots_value);
        }
        inst->most_recent_epoch_ballot = cmp;
        copy_value(&value, &inst->current_ballots_value);
        quorum_clear(&inst->quorum);
    }
}

void epoch_learner_check_and_set_highest_instance_closed(struct epoch_learner* l, iid_t instance_chosen) {
    if (instance_chosen > l->highest_instance_chosen) {
        l->highest_instance_chosen = instance_chosen;
    }
}

enum epoch_paxos_message_return_codes epoch_learner_receive_accepted(struct epoch_learner* l, struct epoch_ballot_accepted* ack, struct epoch_ballot_chosen* returned_message) {
    char phase_name[] = "Acceptance";
    check_and_handle_late_start(l, ack->instance);

    if(epoch_learner_is_instance_outdated(l, ack->instance, phase_name)) {
        return MESSAGE_IGNORED;
    }
    struct instance* inst = epoch_learner_get_or_create_instance(l, ack->instance);

    if (epoch_learner_is_instance_chosen(inst, phase_name)) {
        return MESSAGE_IGNORED;
    }

    if (epoch_learner_is_epoch_ballot_outdated(inst, ack->accepted_epoch_ballot, phase_name)){
        return MESSAGE_IGNORED;
    }

    check_and_handle_new_ballot(inst, ack->accepted_epoch_ballot, ack->accepted_value);

    bool acceptance_duplicate = !quorum_add(&inst->quorum, ack->acceptor_id);
    if(acceptance_duplicate) {
        paxos_log_debug("Duplicate %s ignored.", phase_name);
        return MESSAGE_IGNORED;
    }

    paxos_log_debug("Received new Epoch Ballot Accept from Acceptor %u for Instance %u for Epoch Ballot %u.%u.%u",
                    ack->acceptor_id, ack->instance, ack->accepted_epoch_ballot.epoch,
                    ack->accepted_epoch_ballot.ballot.number, ack->accepted_epoch_ballot.ballot.proposer_id);

    if (quorum_reached(&inst->quorum)) {
        inst->chosen = true;
        epoch_learner_check_and_set_highest_instance_closed(l, ack->instance);
        *returned_message = (struct epoch_ballot_chosen) {
            .instance = ack->instance,
            .chosen_epoch_ballot = ack->accepted_epoch_ballot,
            .chosen_value = ack->accepted_value
        };
        return QUORUM_REACHED;
    } else {
        return MESSAGE_ACKNOWLEDGED;
    }
}


enum epoch_paxos_message_return_codes epoch_learner_receive_epoch_ballot_chosen(struct epoch_learner* l, struct epoch_ballot_chosen* chosen_msg){
    char message_name[] = "Chosen";
    check_and_handle_late_start(l, chosen_msg->instance);

    if(epoch_learner_is_instance_outdated(l, chosen_msg->instance, message_name)) {
        return MESSAGE_IGNORED;
    }
    struct instance* inst = epoch_learner_get_or_create_instance(l, chosen_msg->instance);

    if (epoch_learner_is_instance_chosen(inst, message_name)) {
        return MESSAGE_IGNORED;
    }

    inst->chosen = true;
    inst->most_recent_epoch_ballot = chosen_msg->chosen_epoch_ballot;
    copy_value(&chosen_msg->chosen_value, &inst->current_ballots_value);

    epoch_learner_check_and_set_highest_instance_closed(l, chosen_msg->instance);
    return MESSAGE_ACKNOWLEDGED;
}

enum epoch_paxos_message_return_codes epoch_learner_receive_trim(struct epoch_learner* l, struct paxos_trim* trim) {
    if (trim->iid > l->trim_instance) {
        l->trim_instance = trim->iid;
        l->current_min_instance_to_execute = trim->iid + 1;
        l->highest_instance_chosen = trim->iid;
        return MESSAGE_ACKNOWLEDGED;
    } else {
        return MESSAGE_IGNORED;
    }
}

bool epoch_learner_deliver_next(struct epoch_learner* l, struct paxos_value* out){
    struct instance* inst;
    bool instance_exists = epoch_learner_get_existing_instance(l, l->current_min_instance_to_execute, &inst);

    if (instance_exists) {
        if (inst->chosen) {
            paxos_value_copy(out, &inst->current_ballots_value);
            epoch_learner_remove_instance_from_pending(l, &inst);
            l->current_min_instance_to_execute++;
            return true;
        }
    }
    return false;
}
bool epoch_learner_has_holes(struct epoch_learner* l, iid_t* from, iid_t* to){
    if (l->highest_instance_chosen > l->current_min_instance_to_execute) {
        *from = l->current_min_instance_to_execute - 1;
        *to = l->highest_instance_chosen;
        return true;
    } else {
        return false;
    }
}

iid_t epoch_learner_get_instance_to_trim(struct epoch_learner* l) {
    assert(l->highest_instance_chosen <= l->current_min_instance_to_execute);
    iid_t prev_instance = l->current_min_instance_to_execute - 1;
    return prev_instance;
}

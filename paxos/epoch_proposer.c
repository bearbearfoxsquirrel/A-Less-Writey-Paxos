//
// Created by Michael Davis on 07/02/2020.
//


#include <paxos_types.h>
#include <paxos.h>
#include <instance.h>
#include <paxos_util.h>
#include <proposer_common.h>

#include <khash.h>
#include <client_value_queue.h>
#include "epoch_ballot.h"
#include <pending_client_values.h>
#include "ballot.h"
#include <assert.h>
#include <random.h>
#include <epoch_proposer.h>
#include "timeout.h"

KHASH_MAP_INIT_INT(instance_info, struct epoch_proposer_instance_info*)
KHASH_MAP_INIT_INT(chosen_instances, bool*)



struct epoch_proposer {
    int id;
    uint32_t known_highest_epoch;
    int acceptors;
    int q1;
    int q2;

    struct client_value_queue *client_values_to_propose;
    struct pending_client_values *pending_client_values;

    iid_t max_chosen_instance;
    iid_t trim_instance;
    iid_t next_prepare_instance;

    khash_t(instance_info)* prepare_proposer_instance_infos; /* Waiting for prepare acks */
    khash_t(instance_info)* accept_proposer_instance_infos;  /* Waiting for accept acks */

    khash_t(chosen_instances)* chosen_instances;

    uint32_t ballot_increment;
};

struct epoch_proposer_timeout_iterator {
    khiter_t prepare_instance_iter, accept_instance_iter;
    struct timeval check_timeout;
    struct epoch_proposer* proposer;
};




struct epoch_proposer *epoch_proposer_new(int id, int acceptors, int q1, int q2, uint32_t max_ballot_increment) {
    struct epoch_proposer* proposer = calloc(1, sizeof(struct epoch_proposer));
    proposer->id = id;
    proposer->acceptors = acceptors;
    proposer->q1 = q1;
    proposer->q2 = q2;

    proposer->prepare_proposer_instance_infos = kh_init(instance_info);
    proposer->accept_proposer_instance_infos = kh_init(instance_info);
    proposer->chosen_instances = kh_init_chosen_instances();

    proposer->trim_instance = INVALID_INSTANCE;
    proposer->next_prepare_instance = 0;
    proposer->max_chosen_instance = INVALID_INSTANCE;

    proposer->known_highest_epoch = INVALID_EPOCH;

    proposer->client_values_to_propose = carray_new(128);
    proposer->pending_client_values = pending_client_values_new();


    proposer->ballot_increment = max_ballot_increment;

    return proposer;
}

static void epoch_proposer_instance_info_free(struct epoch_proposer_instance_info** inst) {
    proposer_common_instance_info_destroy_contents(&(**inst).common_info);
    quorum_destroy(&(**inst).quorum);
    free(*inst);
    *inst = NULL;
}

void epoch_proposer_free(struct epoch_proposer* p){
    struct epoch_proposer_instance_info* inst;
    kh_foreach_value(p->prepare_proposer_instance_infos, inst, epoch_proposer_instance_info_free(&inst));
    kh_foreach_value(p->accept_proposer_instance_infos, inst, epoch_proposer_instance_info_free(&inst));
    kh_destroy(instance_info, p->prepare_proposer_instance_infos);
    kh_destroy(instance_info, p->accept_proposer_instance_infos);
    carray_foreach(p->client_values_to_propose, carray_paxos_value_free);
    carray_free(p->client_values_to_propose);
    free(p);
}


static bool epoch_proposer_get_instance_info_in_phase(khash_t(instance_info)* phase_table, iid_t instance, struct epoch_proposer_instance_info** instance_info) {
    khiter_t key = kh_get_instance_info(phase_table, instance);
    if (key == kh_end(phase_table)) {
        return false;
    } else {
        if (kh_exist(phase_table, key) == 1) {
            *instance_info = kh_value(phase_table, key);
            return true;
        } else {
            return false;
        }
    }
}

static void epoch_proposer_move_instance_between_phase(khash_t(instance_info)* from, khash_t(instance_info)* to, struct epoch_proposer_instance_info* inst, int quorum_size) {
    int rv;
    khiter_t k;
    k = kh_get_instance_info(from, inst->common_info.iid);
    assert(k != kh_end(from));
    kh_del_instance_info(from, k);
    k = kh_put_instance_info(to, inst->common_info.iid, &rv);
    assert(rv > 0);
    kh_value(to, k) = inst;
    quorum_resize_and_reset(&inst->quorum, quorum_size);

    k = kh_get_instance_info(from, inst->common_info.iid);
    assert(k == kh_end(from));
}

static void epoch_proposer_remove_instance_from_phase(khash_t(instance_info)* phase_table, iid_t instance) {
    khiter_t key = kh_get_instance_info(phase_table, instance);
    kh_del_instance_info(phase_table, key);
}


static struct epoch_proposer_instance_info* epoch_proposer_instance_info_new(iid_t instance, struct epoch_ballot inital_epoch_ballot, int num_acceptors, int quorum_size) {
    struct epoch_proposer_instance_info* inst = malloc(sizeof(struct epoch_proposer_instance_info));
    inst->common_info = proposer_common_info_new(instance, inital_epoch_ballot.ballot);
    inst->promised_epoch = INVALID_EPOCH;
    inst->last_accepted_epoch_ballot_epoch = INVALID_EPOCH;
    quorum_init(&inst->quorum, num_acceptors, quorum_size);
    assert(inst->common_info.iid > 0);
    return inst;
}



struct epoch_ballot
epoch_proposer_instance_info_get_current_epoch_ballot(const struct epoch_proposer_instance_info *inst) {
    return (struct epoch_ballot){inst->promised_epoch , inst->common_info.ballot};
}

struct epoch_ballot epoch_proposer_instance_info_get_last_accepted_epoch_ballot (const struct epoch_proposer_instance_info* inst){
    return (struct epoch_ballot) {inst->last_accepted_epoch_ballot_epoch, inst->common_info.last_accepted_ballot};
}

void epoch_proposer_instance_info_set_current_epoch_ballot(struct epoch_proposer_instance_info* inst, struct epoch_ballot new_epoch_ballot) {
    assert(epoch_ballot_greater_than(new_epoch_ballot, epoch_proposer_instance_info_get_current_epoch_ballot(inst)));
    inst->promised_epoch = new_epoch_ballot.epoch;
    inst->common_info.ballot = new_epoch_ballot.ballot;
}

void epoch_proposer_instance_info_set_last_accepted_epoch_ballot(struct epoch_proposer_instance_info* inst, struct epoch_ballot new_last_accepted_epoch_ballot) {
    assert(epoch_ballot_greater_than(new_last_accepted_epoch_ballot, epoch_proposer_instance_info_get_last_accepted_epoch_ballot(inst)));
    inst->last_accepted_epoch_ballot_epoch = new_last_accepted_epoch_ballot.epoch;
    inst->common_info.last_accepted_ballot = new_last_accepted_epoch_ballot.ballot;
}

void epoch_proposer_instance_info_set_last_accepted_value(struct epoch_proposer_instance_info* inst, struct paxos_value value_to_save){
    assert(value_to_save.paxos_value_len > 0);
    assert(value_to_save.paxos_value_val != NULL);
    if (proposer_instance_info_has_promised_value(&inst->common_info)) {
        paxos_value_free(&inst->common_info.last_accepted_value);
    }

    inst->common_info.last_accepted_value = paxos_value_new(value_to_save.paxos_value_val, value_to_save.paxos_value_len);
}

void epoch_proposer_instance_info_save_new_last_accepted_value(struct epoch_proposer_instance_info* inst, struct epoch_ballot epoch_ballot, struct paxos_value value_to_save){
    epoch_proposer_instance_info_set_last_accepted_epoch_ballot(inst, epoch_ballot);
    epoch_proposer_instance_info_set_last_accepted_value(inst, value_to_save);
}


void epoch_proposer_check_and_set_current_epoch_from_epoch_ballot(struct epoch_proposer* p, struct epoch_ballot cmp) {
    if (p->known_highest_epoch < cmp.epoch) {
        p->known_highest_epoch = cmp.epoch;
    }
}

void epoch_proposer_add_paxos_value_to_queue(struct epoch_proposer* p, struct paxos_value value) {
    paxos_log_debug("Received new Client Value");
    struct paxos_value* value_copy = paxos_value_new(value.paxos_value_val, value.paxos_value_len);//malloc(sizeof(struct paxos_value*));
    //  copy_value(value, value_copy);
    assert(value_copy->paxos_value_len > 1);
    carray_push_back(p->client_values_to_propose, value_copy);
}

int epoch_proposer_prepare_count(struct epoch_proposer* p){
    return kh_size(p->prepare_proposer_instance_infos);
}

int epoch_proposer_acceptance_count(struct epoch_proposer* p){
    return kh_size(p->accept_proposer_instance_infos);
}

unsigned int epoch_proposer_get_current_known_epoch(struct epoch_proposer* p){
    return p->known_highest_epoch;
}

unsigned int epoch_proposer_get_id(struct epoch_proposer* p){
    return p->id;
}


void epoch_proposer_set_current_instance(struct epoch_proposer* p, iid_t instance) {
    assert(instance >= p->next_prepare_instance);
    p->next_prepare_instance = instance;

    if (instance < epoch_proposer_get_min_unchosen_instance(p)) {
        struct paxos_trim trim = {instance};
        epoch_proposer_receive_trim(p, &trim);
    }
}


static void epoch_proposer_set_current_instance_chosen(struct epoch_proposer* p, iid_t instance){
    khiter_t k = kh_get_chosen_instances(p->chosen_instances, instance);

    if (k == kh_end(p->chosen_instances)) {
        bool* chosen = calloc(1, sizeof(bool));
        *chosen = true;
        int rv;
        k = kh_put_chosen_instances(p->chosen_instances, instance, &rv);
        assert(rv > 0);
        kh_value(p->chosen_instances, k) = chosen;

        if (p->max_chosen_instance < instance) {
            p->max_chosen_instance = instance;
        }
        paxos_log_debug("Instance %u set to chosen", instance);
    }
}

static bool epoch_proposer_is_instance_chosen(const struct epoch_proposer *p, const iid_t instance) {
    khiter_t k = kh_get_chosen_instances(p->chosen_instances, instance);

    if (kh_size(p->chosen_instances) == 0) {
        return false;
    } else if (k == kh_end(p->chosen_instances)) {
        //    paxos_log_debug("Instance: %u not chosen", instance);
        return false;
    } else {
        //  paxos_log_debug("Instance %u chosen", instance);
        return true;
    }
}

void epoch_proposer_next_instance(struct epoch_proposer* p) {
    p->next_prepare_instance++;
    paxos_log_debug("Incremented next preparing instance. Next instance to prepare is %u", p->next_prepare_instance);
}

uint32_t epoch_proposer_get_current_instance(struct epoch_proposer* p){
    return p->next_prepare_instance;
}

uint32_t epoch_proposer_get_min_unchosen_instance(struct epoch_proposer* p){
    iid_t current_min_instance = p->trim_instance;

    if (kh_size(p->chosen_instances) == 0)
        return current_min_instance;

    khiter_t key = kh_get_chosen_instances(p->chosen_instances, current_min_instance);
    while(key != kh_end(p->chosen_instances)) { //kh_end also is used for not found
        current_min_instance++;
        key = kh_get_chosen_instances(p->chosen_instances, current_min_instance);
    }
    return current_min_instance;
}


// phase 1

static struct ballot epoch_proposer_get_initial_ballot(const struct epoch_proposer *p) {
    return (struct ballot){.number = random_between(1, p->ballot_increment), .proposer_id = p->id};
}

bool epoch_proposer_try_to_start_preparing_instance(struct epoch_proposer* p, iid_t instance, struct epoch_paxos_prepares *out){
    bool prepare_message_to_send = false;
    assert(instance != INVALID_INSTANCE);

    if (instance <= p->trim_instance) {
        paxos_log_debug("Instance %u has been trimmed, so skipping to next un trimmed Instance %u", instance, p->trim_instance);
        epoch_proposer_set_current_instance(p, p->trim_instance);
        return prepare_message_to_send;
    }

    if (epoch_proposer_is_instance_chosen(p, instance)){
        paxos_log_debug("Instance %u is already chosen so skipping", instance);
        epoch_proposer_next_instance(p);
        return prepare_message_to_send;
    }


    khiter_t key = kh_get_instance_info(p->prepare_proposer_instance_infos, instance);
    struct epoch_ballot begining_epoch_ballot = {0};
    if (key == kh_end(p->prepare_proposer_instance_infos)) {
        if (p->known_highest_epoch == INVALID_EPOCH) {
            out->type = STANDARD_PREPARE;
            begining_epoch_ballot = (struct epoch_ballot) {.epoch = INVALID_EPOCH,
                    .ballot = epoch_proposer_get_initial_ballot(p)};
            out->standard_prepare = (struct paxos_prepare) {
                .iid = instance,
                .ballot = begining_epoch_ballot.ballot
            };
        } else {
            out->type = EXPLICIT_EPOCH_PREPARE;
            begining_epoch_ballot = (struct epoch_ballot) {.epoch = p->known_highest_epoch,
                    .ballot = epoch_proposer_get_initial_ballot(p)};
            out->explicit_epoch_prepare = (struct epoch_ballot_prepare) {
                .instance = instance,
                .epoch_ballot_requested = begining_epoch_ballot
            };
        }

        assert(begining_epoch_ballot.ballot.number > 0);
        struct epoch_proposer_instance_info* inst = epoch_proposer_instance_info_new(instance, begining_epoch_ballot, p->acceptors, p->q1);

        int rv = -1;
        key = kh_put_instance_info(p->prepare_proposer_instance_infos, instance, &rv);
        assert(rv > 0);
        kh_value(p->prepare_proposer_instance_infos, key) = inst;
        prepare_message_to_send = true;
    }
    return prepare_message_to_send;
}

// returns true if the promise is valid
// returns false if the promise is for a higher epoch and so new epoch promise requests are issued

bool
is_current_epoch_ballot(const struct epoch_ballot_promise *ack,
                        const struct epoch_proposer_instance_info *inst) {
    return epoch_ballot_equal(epoch_proposer_instance_info_get_current_epoch_ballot(inst), ack->promised_epoch_ballot)
        || (inst->promised_epoch == INVALID_EPOCH && ballot_equal(inst->common_info.ballot, ack->promised_epoch_ballot.ballot));
}



bool is_epoch_promise_outdated(const struct epoch_ballot_promise *ack,
                                    const struct epoch_proposer_instance_info *inst) {
    return epoch_ballot_greater_than(epoch_proposer_instance_info_get_current_epoch_ballot(inst), ack->promised_epoch_ballot)
        || ballot_greater_than(inst->common_info.ballot, ack->promised_epoch_ballot.ballot);
}

void epoch_proposer_check_and_handle_promises_last_accepted_value_for_instance(struct epoch_proposer_instance_info* inst, struct epoch_ballot_promise* promise) {
    if (epoch_ballot_greater_than(promise->last_accepted_ballot, epoch_proposer_instance_info_get_last_accepted_epoch_ballot(inst))) {
        paxos_log_debug("New last accepted value by Acceptor %u for Instance %u, at Epoch Ballot %u.%u.%u",
                promise->acceptor_id,
                promise->instance,
                promise->last_accepted_ballot.epoch,
                promise->last_accepted_ballot.ballot.number,
                promise->last_accepted_ballot.ballot.proposer_id);

        epoch_proposer_instance_info_save_new_last_accepted_value(inst, promise->last_accepted_ballot, promise->last_accepted_value);

    }
}

void epoch_proposer_instance_info_update_info_from_epoch_preemption(struct epoch_proposer_instance_info *inst,
                                                                    struct epoch_ballot_preempted *preempted,
                                                                    unsigned int proposer_id,
                                                                    uint32_t ballot_increment) {
    struct epoch_ballot next_epoch_ballot = (struct epoch_ballot) {
        .epoch = preempted->acceptors_current_epoch_ballot.epoch,
        .ballot = (struct ballot) {
            .number = random_between(preempted->acceptors_current_epoch_ballot.ballot.number + 1,
                                     preempted->acceptors_current_epoch_ballot.ballot.number + 1 + ballot_increment),
            .proposer_id = proposer_id
        }
    };
    epoch_proposer_instance_info_set_current_epoch_ballot(inst, next_epoch_ballot);
    inst->common_info.proposing_value = NULL;
    quorum_clear(&inst->quorum);
    gettimeofday(&inst->common_info.created_at, NULL);
}

enum epoch_paxos_message_return_codes epoch_proposer_receive_promise(struct epoch_proposer *p, struct epoch_ballot_promise *ack, struct epoch_ballot_prepare* next_epoch_prepare) {
    assert(ack->instance > INVALID_INSTANCE);
    assert(epoch_ballot_greater_than(ack->promised_epoch_ballot, INVALID_EPOCH_BALLOT));

    if (ack->instance <= p->trim_instance) {
        paxos_log_debug("Promise dropped, Instance trimed");
        return MESSAGE_IGNORED;
    }

    if (epoch_proposer_is_instance_chosen(p, ack->instance)){
        paxos_log_debug("Promise dropped, Instance %u known to be chosen", ack->instance);
        return MESSAGE_IGNORED;
    }

    struct epoch_proposer_instance_info* inst;
    bool pending = epoch_proposer_get_instance_info_in_phase(p->prepare_proposer_instance_infos, ack->instance, &inst);

    if (!pending) {
        paxos_log_debug("Promise dropped, Instance %u not pending", ack->instance);
        return MESSAGE_IGNORED;
    }


    if (is_epoch_promise_outdated(ack, inst)) {
        paxos_log_debug("Promise dropped, too old");
        return MESSAGE_IGNORED;
    } else if (is_current_epoch_ballot(ack, inst)) {
        if (inst->promised_epoch == INVALID_EPOCH) {
            inst->promised_epoch = ack->promised_epoch_ballot.epoch;
        }

        int new_promise = quorum_add(&inst->quorum, ack->acceptor_id);

        if (new_promise == 0){
            paxos_log_debug("Duplicate promise dropped from Acceptor %d on Instance %u", ack->acceptor_id, ack->instance);
            return MESSAGE_IGNORED;
        } else {
            if (ack->promised_epoch_ballot.epoch > p->known_highest_epoch) {
                p->known_highest_epoch = ack->promised_epoch_ballot.epoch;
            }

            paxos_log_debug("Receved new promise from %d on Instance %u on Epoch Ballot %u.%u.%u",
                    ack->acceptor_id,
                    ack->instance, ack->promised_epoch_ballot.epoch,
                    ack->promised_epoch_ballot.ballot.number,
                    ack->promised_epoch_ballot.ballot.proposer_id);

            epoch_proposer_check_and_handle_promises_last_accepted_value_for_instance(inst, ack);

            if (quorum_reached(&inst->quorum)) {
                return QUORUM_REACHED;
            } else {
                return MESSAGE_ACKNOWLEDGED;
            }
        }


    } else {
        assert(ballot_equal(inst->common_info.ballot, ack->promised_epoch_ballot.ballot));

        if (ack->promised_epoch_ballot.epoch > p->known_highest_epoch){
            p->known_highest_epoch = ack->promised_epoch_ballot.epoch;
        }

        struct epoch_ballot_preempted preempted = {.acceptor_id = ack->acceptor_id,
                                                   .instance = ack->instance,
                                                   .requested_epoch_ballot = epoch_proposer_instance_info_get_current_epoch_ballot(inst),
                                                   .acceptors_current_epoch_ballot = ack->promised_epoch_ballot};

        epoch_proposer_instance_info_update_info_from_epoch_preemption(inst, &preempted, p->id, p->ballot_increment);

        // still a good promise so it will serve as the first for the quorum on the new current epoch
        // todo add a feature to ev_proposer where it will know who is in the quorum and send promise requests to those not in the quorum
        epoch_proposer_receive_promise(p, ack, next_epoch_prepare);
        next_epoch_prepare->instance = ack->instance;
        next_epoch_prepare->epoch_ballot_requested = ack->promised_epoch_ballot;
        return EPOCH_PREEMPTED;
        // then add to quorum


        // epoch preempted
        //clear quorums
        //increment epoch
        // increment known epoch if higher than current
    }
}


static bool get_min_instance_to_begin_accept_phase(struct epoch_proposer *p,
                                                   struct epoch_proposer_instance_info **to_accept_inst) {
    (*to_accept_inst) = NULL;
    khash_t(instance_info)* hash_table = p->prepare_proposer_instance_infos;
    khiter_t key;
    bool first = true;

    for (key = kh_begin(hash_table); key != kh_end(hash_table); ++key) {
        if (kh_exist(hash_table, key) == 0) {
            continue;
        } else {
            struct epoch_proposer_instance_info *current_inst = kh_value(hash_table, key);
            if (quorum_reached(&current_inst->quorum) &&
                !epoch_proposer_is_instance_chosen(p, current_inst->common_info.iid)) {
                if (first) {
                    (*to_accept_inst) = current_inst;
                    first = false;
                } else {
                    if ((*to_accept_inst)->common_info.iid > current_inst->common_info.iid) {
                        (*to_accept_inst) = current_inst;
                    }
                }
            }
        }
    }

    if((*to_accept_inst) == NULL) {
        return false;
    } else {
        return true;
    }
}


// I know bad practice to copy code but I'm too lazy to work out nice way to do this
bool epoch_proposer_try_determine_value_to_propose(struct epoch_proposer* proposer, struct epoch_proposer_instance_info* inst) {
    if (!proposer_instance_info_has_promised_value(&inst->common_info)) {
        if (!carray_empty(proposer->client_values_to_propose)) {
            assert(inst->common_info.proposing_value == NULL);
            paxos_log_debug("Proposing client value");
            struct paxos_value* value_to_propose = carray_pop_front(proposer->client_values_to_propose);
            assert(value_to_propose != NULL);
            inst->common_info.proposing_value = value_to_propose;//paxos_value_new(value_to_propose->paxos_value_val, value_to_propose->paxos_value_len);
            client_value_now_pending_at(proposer->pending_client_values, inst->common_info.iid, value_to_propose);// value_to_propose);
        } else {
            if (proposer->max_chosen_instance > inst->common_info.iid) {
                inst->common_info.proposing_value = paxos_value_new("NOP.", 5);
                paxos_log_debug("Sending NOP to fill holes");
            } else {
                paxos_log_debug("No need to propose a Value");
                return false;
            }
        }
    } else {
        paxos_log_debug("Instance has a previously proposed Value. Proposing it.");
        inst->common_info.proposing_value = inst->common_info.last_accepted_value;
        inst->common_info.last_accepted_value = NULL;
    }
    assert(inst->common_info.proposing_value != NULL);
    return true;
}


void set_epoch_ballot_accept_from_instance_info(struct epoch_ballot_accept *out,
                                                const struct epoch_proposer_instance_info *instance_to_begin_accept) {
    out->instance = instance_to_begin_accept->common_info.iid;
    out->epoch_ballot_requested = epoch_proposer_instance_info_get_current_epoch_ballot(instance_to_begin_accept);
    out->value_to_accept = *instance_to_begin_accept->common_info.proposing_value;
}

// phase 2
int epoch_proposer_try_accept(struct epoch_proposer* p, struct epoch_ballot_accept* out){
    struct epoch_proposer_instance_info* instance_to_begin_accept = NULL;
    bool instance_found = get_min_instance_to_begin_accept_phase(p, &instance_to_begin_accept);

    if (!instance_found) {
        paxos_log_debug("No Instances found to have an Epoch Promise Quorum");
        return 0;
    }

    paxos_log_debug("Instance %u is ready to Accept", instance_to_begin_accept->common_info.iid);

    bool is_value_to_propose = epoch_proposer_try_determine_value_to_propose(p, instance_to_begin_accept);

    if (is_value_to_propose){
        epoch_proposer_move_instance_between_phase(p->prepare_proposer_instance_infos, p->accept_proposer_instance_infos, instance_to_begin_accept, p->q2);
        set_epoch_ballot_accept_from_instance_info(out, instance_to_begin_accept);
    }
    return is_value_to_propose;
}

enum epoch_paxos_message_return_codes epoch_proposer_receive_accepted(struct epoch_proposer* p, struct epoch_ballot_accepted* ack, struct epoch_ballot_chosen* chosen){
    assert(ack->instance > INVALID_INSTANCE);
    assert(epoch_ballot_greater_than(ack->accepted_epoch_ballot, INVALID_EPOCH_BALLOT));

    if (ack->instance <= p->trim_instance) {
        paxos_log_debug("Acceptance dropped, Instance trimed");
        return MESSAGE_IGNORED;
    }

    if (epoch_proposer_is_instance_chosen(p, ack->instance)){
        paxos_log_debug("Acceptance dropped, Instance %u known to be chosen", ack->instance);
        return MESSAGE_IGNORED;
    }

    struct epoch_proposer_instance_info* inst;
    bool pending = epoch_proposer_get_instance_info_in_phase(p->accept_proposer_instance_infos, ack->instance, &inst);

    if (!pending) {
        paxos_log_debug("Acceptance dropped, Instance %u not pending", ack->instance);
        return MESSAGE_IGNORED;
    }


    if (epoch_ballot_greater_than(epoch_proposer_instance_info_get_current_epoch_ballot(inst), ack->accepted_epoch_ballot)) {
        paxos_log_debug("Promise dropped, too old");
        return MESSAGE_IGNORED;
    } else if (epoch_ballot_equal(ack->accepted_epoch_ballot, epoch_proposer_instance_info_get_current_epoch_ballot(inst))) {
        paxos_log_debug("Received Acceptance from Acceptor %u dropped for Instance %u for Epoch Ballot %u.%u.%u",
                ack->acceptor_id, ack->instance, ack->accepted_epoch_ballot.epoch,
                ack->accepted_epoch_ballot.ballot.number, ack->accepted_epoch_ballot.ballot.proposer_id);

        if (quorum_add(&inst->quorum, ack->acceptor_id) == 0) {
            paxos_log_debug("Duplicate Acceptance from Acceptor %u dropped for Instance %u",
                    ack->acceptor_id, ack->instance);
            return MESSAGE_IGNORED;
        } else {
            if (quorum_reached(&inst->quorum)) {
                chosen->instance = inst->common_info.iid;
                chosen->chosen_epoch_ballot = epoch_proposer_instance_info_get_current_epoch_ballot(inst);
                //have to copy value to new place and delete after sending as the proposer will destroy the instance in chosen
                paxos_value_copy(&chosen->chosen_value , &ack->accepted_value);
                // make chosen message
                // receive chosen

                epoch_proposer_receive_chosen(p, chosen);
                return QUORUM_REACHED;
            } else {
                return MESSAGE_ACKNOWLEDGED;
            }
        }

    } else{

        // shouldn't be able to have Epoch Ballot Accepted for an Epoch Ballot the Proposer made than what the Proposer knows of
        assert(1 == 2);
    }
}

void epoch_proposer_check_and_handle_client_value_from_chosen(struct epoch_proposer* p, struct epoch_proposer_instance_info* inst, struct epoch_ballot_chosen* chosen){
    struct paxos_value proposed_client_value;
    bool client_value_proposed = get_value_pending_at(p->pending_client_values, chosen->instance, &proposed_client_value);

    if (client_value_proposed) {
        remove_pending_value(p->pending_client_values, chosen->instance, NULL);

        if (is_values_equal(*inst->common_info.proposing_value, chosen->chosen_value)){
            paxos_log_debug("Pending Client Value was Chosen in Instance %u at Epoch Ballot %u.%u.%u",
                    chosen->instance,
                    chosen->chosen_epoch_ballot.epoch,
                    chosen->chosen_epoch_ballot.ballot.number,
                    chosen->chosen_epoch_ballot.ballot.proposer_id);
        } else {
            paxos_log_debug("Pending Client Value was not Chosen for Instance %u. Pushing Client Value back to Queue.",
                    chosen->instance);
            carray_push_back(p->client_values_to_propose, inst->common_info.proposing_value);
            inst->common_info.proposing_value = NULL; // do not delete because it has been moved to the queue
        }
    }
}


enum epoch_paxos_message_return_codes epoch_proposer_receive_chosen(struct epoch_proposer* p, struct epoch_ballot_chosen* ack){
    assert(ack->instance > INVALID_INSTANCE);
    assert(epoch_ballot_greater_than(ack->chosen_epoch_ballot, INVALID_EPOCH_BALLOT));

    if (ack->instance <= p->trim_instance) {
        paxos_log_debug("Chosen dropped, Instance trimed");
        return MESSAGE_IGNORED;
    }

    if (epoch_proposer_is_instance_chosen(p, ack->instance)){
        paxos_log_debug("Chosen dropped, Instance %u known to be chosen", ack->instance);
        return MESSAGE_IGNORED;
    }

    paxos_log_debug("Received chosen message for Instance %u at Epoch Ballot %u.%u.%u", ack->instance, ack->chosen_epoch_ballot.epoch, ack->chosen_epoch_ballot.ballot.number, ack->chosen_epoch_ballot.ballot.proposer_id);

    epoch_proposer_set_current_instance_chosen(p, ack->instance);

    if (ack->chosen_epoch_ballot.epoch > p->known_highest_epoch) {
        p->known_highest_epoch = ack->chosen_epoch_ballot.epoch;
    }

    struct epoch_proposer_instance_info* inst_prepare;
    bool pending_in_prepare = epoch_proposer_get_instance_info_in_phase(p->prepare_proposer_instance_infos, ack->instance, &inst_prepare);

    struct epoch_proposer_instance_info* inst_accept;
    bool pending_in_accept = epoch_proposer_get_instance_info_in_phase(p->accept_proposer_instance_infos, ack->instance, &inst_accept);

    if (!pending_in_prepare && !pending_in_accept) {
        paxos_log_debug("Chosen dropped, Instance %u not pending_in_prepare", ack->instance);
        return MESSAGE_IGNORED;
    }

    assert(!(pending_in_accept && pending_in_prepare));

    if (pending_in_prepare) {
        epoch_proposer_remove_instance_from_phase(p->prepare_proposer_instance_infos, ack->instance);
        epoch_proposer_instance_info_free(&inst_prepare);
    }

    if (pending_in_accept) {
        epoch_proposer_check_and_handle_client_value_from_chosen(p, inst_accept, ack);
        epoch_proposer_remove_instance_from_phase(p->accept_proposer_instance_infos, ack->instance);
        epoch_proposer_instance_info_free(&inst_accept);
    }

    if (epoch_proposer_get_min_unchosen_instance(p) >= ack->instance) {
        struct paxos_trim trim = (struct paxos_trim) {ack->instance};
        epoch_proposer_receive_trim(p, &trim);
    }

    return MESSAGE_ACKNOWLEDGED;
}

//void epoch_proposer_preempt(struct epoch_proposer* p, struct standard_epoch_proposer_instance_info* inst, paxos_prepare* out);

void epoch_proposer_update_instance_info_from_ballot_preempted(struct epoch_proposer_instance_info* inst, struct epoch_ballot_preempted* preempted,
                                                               unsigned int pid, uint32_t ballot_increment) {
    struct epoch_ballot next_attempting_ballot = (struct epoch_ballot) {.epoch = preempted->acceptors_current_epoch_ballot.epoch,
                                                                                   .ballot = (struct ballot) {.number = random_between(preempted->acceptors_current_epoch_ballot.ballot.number + 1,
                                                                                                                                        preempted->acceptors_current_epoch_ballot.ballot.number + ballot_increment + 1),
                                                                                                              .proposer_id = pid}};
    epoch_proposer_instance_info_set_current_epoch_ballot(inst, next_attempting_ballot);

    inst->common_info.proposing_value = NULL;
    quorum_clear(&inst->quorum);
    gettimeofday(&inst->common_info.created_at, NULL);
}


void set_epoch_ballot_prepare_from_instance_info(struct epoch_ballot_prepare *prepare,
                                                 const struct epoch_proposer_instance_info *inst) {
    prepare->instance = inst->common_info.iid;
    prepare->epoch_ballot_requested = epoch_proposer_instance_info_get_current_epoch_ballot(inst);
}

void epoch_proposer_check_and_requeue_client_value_if_was_proposed(struct epoch_proposer* p, struct epoch_proposer_instance_info* inst) {
    struct paxos_value proposed_value;
    bool inst_has_pending_client_value = get_value_pending_at(p->pending_client_values, inst->common_info.iid, &proposed_value);
    if (inst_has_pending_client_value) {
        carray_push_front(p->client_values_to_propose, inst->common_info.proposing_value);
        inst->common_info.proposing_value = NULL;
        remove_pending_value(p->pending_client_values, inst->common_info.iid, NULL);
    }
}

enum epoch_paxos_message_return_codes epoch_proposer_receive_preempted(struct epoch_proposer* p, struct epoch_ballot_preempted* preempted, struct epoch_ballot_prepare* next_prepare){
        if (preempted->instance < p->trim_instance) {
            paxos_log_debug("Ignoring Preempted, Instance %u has been trimmed", preempted->instance);
            return MESSAGE_IGNORED;
        }

        if (epoch_proposer_is_instance_chosen(p, preempted->instance)){
            paxos_log_debug("Ignoring Preempted, Instance %u known to be Chosen");
            return MESSAGE_IGNORED;
        }

        enum epoch_paxos_message_return_codes return_code = MESSAGE_IGNORED;

        struct epoch_proposer_instance_info* prepare_instance_info;
        bool in_promise_phase = epoch_proposer_get_instance_info_in_phase(p->prepare_proposer_instance_infos, preempted->instance, &prepare_instance_info);
        if (in_promise_phase) {
            if (epoch_ballot_equal(preempted->requested_epoch_ballot, epoch_proposer_instance_info_get_current_epoch_ballot(prepare_instance_info)) || (ballot_equal(preempted->requested_epoch_ballot.ballot, prepare_instance_info->common_info.ballot) && prepare_instance_info->promised_epoch == INVALID_EPOCH)) {

                if(epoch_greater_than(preempted->acceptors_current_epoch_ballot, epoch_proposer_instance_info_get_current_epoch_ballot(prepare_instance_info))){
                    // also increases ballot
                   epoch_proposer_check_and_set_current_epoch_from_epoch_ballot(p, preempted->acceptors_current_epoch_ballot);
                    epoch_proposer_instance_info_update_info_from_epoch_preemption(prepare_instance_info, preempted,
                                                                                   p->id, 0);
                   prepare_instance_info->common_info.ballot = preempted->requested_epoch_ballot.ballot; //reset the ballot to see if should backoff too
                   return_code = EPOCH_PREEMPTED;
                }

                if (epoch_ballot_greater_than(preempted->acceptors_current_epoch_ballot, epoch_proposer_instance_info_get_current_epoch_ballot(prepare_instance_info))){
                    epoch_proposer_update_instance_info_from_ballot_preempted(prepare_instance_info, preempted, p->id, p->ballot_increment);
                    return_code = BALLOT_PREEMPTED;
                }

                set_epoch_ballot_prepare_from_instance_info(next_prepare, prepare_instance_info);

                // check if the ballot was preempted
                // check if the epoch was preempted

                // update epoch if greater than
            }
        }

        struct epoch_proposer_instance_info* acceptnce_instance_info;
        bool in_acceptance_phase = epoch_proposer_get_instance_info_in_phase(p->accept_proposer_instance_infos, preempted->instance, &acceptnce_instance_info);
        if (in_acceptance_phase) {
            if (epoch_ballot_equal(preempted->requested_epoch_ballot, epoch_proposer_instance_info_get_current_epoch_ballot(acceptnce_instance_info))){

                epoch_proposer_check_and_requeue_client_value_if_was_proposed(p, acceptnce_instance_info);

                if (epoch_greater_than(preempted->acceptors_current_epoch_ballot, epoch_proposer_instance_info_get_current_epoch_ballot(acceptnce_instance_info))){
                    epoch_proposer_check_and_set_current_epoch_from_epoch_ballot(p, preempted->acceptors_current_epoch_ballot);
                    epoch_proposer_instance_info_update_info_from_epoch_preemption(acceptnce_instance_info, preempted,
                                                                                   p->id, p->ballot_increment);
                    return_code = EPOCH_PREEMPTED;
                }

                if (epoch_ballot_greater_than(preempted->acceptors_current_epoch_ballot, epoch_proposer_instance_info_get_current_epoch_ballot(acceptnce_instance_info))){
                    epoch_proposer_update_instance_info_from_ballot_preempted(acceptnce_instance_info, preempted, p->id, p->ballot_increment);
                    return_code = BALLOT_PREEMPTED;
                }



                epoch_proposer_move_instance_between_phase(p->accept_proposer_instance_infos, p->prepare_proposer_instance_infos, acceptnce_instance_info, p->q1);
                set_epoch_ballot_prepare_from_instance_info(next_prepare, acceptnce_instance_info);
                // check if the ballot was preempted
                // check if the epoch was preempted
                //update epoch if greater
                //push back client value to queue if there was one

            }
        }
    return return_code;

}


static void epoch_proposer_trim_instances_pending_in_phase(struct epoch_proposer* p, khash_t(instance_info)* h, iid_t instance){
    for (khiter_t  k = kh_begin(h); k != kh_end(h); ++k) {
        if (kh_exist(h, k) != 1)
            continue;
        struct epoch_proposer_instance_info* inst = kh_value(h, k);
        if (inst->common_info.iid <= instance) {
            if (proposer_instance_info_has_value(&inst->common_info)) {
                epoch_proposer_check_and_requeue_client_value_if_was_proposed(p, inst);
            }
            kh_del_instance_info(h, k);
            epoch_proposer_instance_info_free(&inst);
        }
    }
}



// periodic acceptor state
enum epoch_paxos_message_return_codes epoch_proposer_receive_acceptor_state(struct epoch_proposer* p,
                                                                            struct writeahead_epoch_acceptor_state* state) {
    bool new_epoch = false;
    if (state->current_epoch > p->known_highest_epoch) {
        p->known_highest_epoch = state->current_epoch;
        new_epoch = true;
    }

    struct paxos_trim trim = (struct paxos_trim) {state->standard_acceptor_state.trim_iid};
    bool new_trim = (epoch_proposer_receive_trim(p, &trim) == MESSAGE_ACKNOWLEDGED ) ? true : false;
    return (new_trim || new_epoch) ? MESSAGE_ACKNOWLEDGED : MESSAGE_IGNORED;
}

enum epoch_paxos_message_return_codes epoch_proposer_receive_trim(struct epoch_proposer* p,
                                                                  struct paxos_trim* trim_msg){
    if (trim_msg->iid > p->trim_instance) {
        paxos_log_debug("Received new Trim Message for Instance %u", trim_msg->iid);
        p->trim_instance = trim_msg->iid;
        epoch_proposer_trim_instances_pending_in_phase(p, p->prepare_proposer_instance_infos, trim_msg->iid);
        epoch_proposer_trim_instances_pending_in_phase(p, p->accept_proposer_instance_infos, trim_msg->iid);
        return MESSAGE_ACKNOWLEDGED;
    } else {
        return MESSAGE_IGNORED;
    }
}

static struct epoch_proposer_instance_info* get_next_timedout(khash_t(instance_info)* h, khiter_t* k, struct timeval* time_now) {
    for (; *k != kh_end(h); ++(*k)) {
        if (kh_exist(h, *k) != 1)
            continue;
        struct epoch_proposer_instance_info* inst = kh_value(h, *k);
        if (quorum_reached(&inst->quorum))
            continue;
        if (proposer_instance_info_has_timedout(&inst->common_info, time_now))
            return inst;
    }
    return NULL;
}

// timeouts
struct epoch_proposer_timeout_iterator* epoch_proposer_timeout_iterator_new(struct epoch_proposer* p) {
    struct epoch_proposer_timeout_iterator* iter = malloc(sizeof(struct epoch_proposer_timeout_iterator));
    iter->prepare_instance_iter = kh_begin(p->prepare_proposer_instance_infos);
    iter->accept_instance_iter = kh_begin(p->accept_proposer_instance_infos);
    iter->proposer = p;
    gettimeofday(&iter->check_timeout, NULL);
    return iter;
}
enum timeout_iterator_return_code epoch_proposer_timeout_iterator_prepare(struct epoch_proposer_timeout_iterator* iter, struct epoch_ballot_prepare* out){
    struct epoch_proposer* p = iter->proposer;
    struct epoch_proposer_instance_info* inst = get_next_timedout(p->prepare_proposer_instance_infos, &iter->prepare_instance_iter, &iter->check_timeout);
    if (inst == NULL) {
        return TIMEOUT_ITERATOR_END;
    } else {
        gettimeofday(&inst->common_info.created_at, NULL);
        out->instance = inst->common_info.iid;
        out->epoch_ballot_requested = epoch_proposer_instance_info_get_current_epoch_ballot(inst);
        return TIMEOUT_ITERATOR_CONTINUE;

    }
}
enum timeout_iterator_return_code epoch_proposer_timeout_iterator_accept(struct epoch_proposer_timeout_iterator* iter, struct epoch_ballot_accept* out){
    struct epoch_proposer* p = iter->proposer;
    struct epoch_proposer_instance_info* inst = get_next_timedout(p->accept_proposer_instance_infos, &iter->accept_instance_iter, &iter->check_timeout);
    if (inst == NULL) {
        return TIMEOUT_ITERATOR_END;
    } else {
        gettimeofday(&inst->common_info.created_at, NULL);
        out->instance = inst->common_info.iid;
        out->epoch_ballot_requested = epoch_proposer_instance_info_get_current_epoch_ballot(inst);
        out->value_to_accept = *inst->common_info.proposing_value;
        return TIMEOUT_ITERATOR_CONTINUE;
    }
}
void epoch_proposer_timeout_iterator_free(struct epoch_proposer_timeout_iterator** iter){
    free(*iter);
    *iter = NULL;
}

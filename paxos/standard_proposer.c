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


#include <paxos.h>
#include "value_proposal_manager.h"
#include "timeout.h"
#include <paxos_types.h>
#include "proposer.h"
#include <instance.h>
#include "carray.h"
#include "quorum.h"
#include "khash.h"
#include "ballot.h"
#include "ballot_giver.h"
#include "paxos_value.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <paxos_util.h>
#include <proposer_common.h>
#include <paxos_message_conversion.h>
#include <stdbool.h>
#include <pending_client_values.h>
#include <stdio.h>
#include "random.h"
#include "timeout.h"

KHASH_MAP_INIT_INT(chosen_instances, bool*)
KHASH_MAP_INIT_INT(instance_info, struct standard_proposer_instance_info*)



struct proposer
{
	unsigned int id;
	unsigned int acceptors;
	unsigned int q1;
	unsigned int q2;

	struct ballot_giver* ballot_giver;

    struct value_proposal_manager* value_proposal_manager;

    iid_t max_chosen_instance;
	iid_t trim_instance;
	iid_t next_prepare_instance;



    khash_t(instance_info)* prepare_phase_instances; /* Waiting for prepare acks */
	khash_t(instance_info)* accept_phase_instances;  /* Waiting for accept acks */
	khash_t(chosen_instances)* chosens;
};

struct timeout_iterator{
    khiter_t prepare_instance, accept_instance;
    struct timeval timeout;
    struct proposer* proposer;
};


static bool get_instance_info(khash_t(instance_info)* phase_table, iid_t instance, struct standard_proposer_instance_info** instance_info) {
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

static void remove_instance_from_phase(khash_t(instance_info)* phase_table, iid_t instance) {
    khiter_t key = kh_get_instance_info(phase_table, instance);
    kh_del_instance_info(phase_table, key);
}

//static struct ballot proposer_next_ballot(struct proposer* p, ballot_t b);
static void
proposer_update_instance_info_from_preemption(struct proposer *p, struct standard_proposer_instance_info *inst,
                                              struct paxos_preempted *preempted_message);
static void proposer_move_instance_between_phase(khash_t(instance_info)* f, khash_t(instance_info)* t,
                                                 struct standard_proposer_instance_info* inst, unsigned int quorum_size);
static void proposer_trim_proposer_instance_infos(struct proposer* p, khash_t(instance_info)* h,
	iid_t iid);


void check_and_handle_promises_value_for_instance(struct paxos_promise *ack, struct standard_proposer_instance_info *inst);


void get_prepare_from_instance_info(struct standard_proposer_instance_info *inst, struct paxos_prepare *out);

struct proposer *
proposer_new(int id, int acceptors, int q1, int q2, uint32_t ballot_increment)
{
	struct proposer *p;
	p = calloc(1, sizeof(struct proposer));
	p->id = id;
	p->acceptors = acceptors;
	p->q1 = q1;
	p->q2 = q2;

    p->ballot_giver = ballot_giver_new(p->id, ballot_increment);

    p->value_proposal_manager = value_proposal_manager_new(1000, paxos_config.repropose_values);

    p->prepare_phase_instances = kh_init(instance_info);
    p->accept_phase_instances = kh_init(instance_info);
    p->chosens = kh_init(chosen_instances);

    p->trim_instance = INVALID_INSTANCE;
	p->next_prepare_instance = INVALID_INSTANCE;
	p->max_chosen_instance = INVALID_INSTANCE;
	return p;
}

static void standard_proposer_instance_info_free(struct standard_proposer_instance_info **inst) {
    proposer_common_instance_info_destroy_contents(&(**inst).common_info);
    quorum_destroy(&(*inst)->quorum);
    free(*inst);
    *inst = NULL;
}

void
proposer_free(struct proposer* p)
{
	struct standard_proposer_instance_info* inst;
	bool* boolean;
	kh_foreach_value(p->prepare_phase_instances, inst, standard_proposer_instance_info_free(&inst));
	kh_foreach_value(p->accept_phase_instances, inst, standard_proposer_instance_info_free(&inst));
	kh_foreach_value(p->chosens, boolean, free(boolean));
	kh_destroy(instance_info, p->prepare_phase_instances);
	kh_destroy(instance_info, p->accept_phase_instances);
	value_proposal_manager_free(&p->value_proposal_manager);
	free(p);
}



static struct standard_proposer_instance_info*
proposer_instance_info_new(iid_t iid, struct ballot ballot, unsigned int acceptors, unsigned int quorum_size) {
    struct standard_proposer_instance_info* inst = malloc(sizeof(struct standard_proposer_instance_info));
    inst->common_info = proposer_common_info_new(iid, ballot);
    quorum_init(&inst->quorum, acceptors, quorum_size);
   // assert(inst->common_info.iid > 0);
    return inst;
}

void proposer_add_paxos_value_to_queue(struct proposer* p, struct paxos_value* value) {
    paxos_log_debug("Received new client value");
    struct paxos_value* value_copy = paxos_value_new(value->paxos_value_val, value->paxos_value_len);
    value_proposal_manager_enqueue(p->value_proposal_manager, &value_copy);
}

int proposer_prepare_count(struct proposer* p) {
	return kh_size(p->prepare_phase_instances);
}

int proposer_acceptance_count(struct proposer* p) {
    return kh_size(p->accept_phase_instances);
}

void proposer_next_instance(struct proposer* p) {
    p->next_prepare_instance++;
    paxos_log_debug("Incrementing current Instance. Now at instance %u", p->next_prepare_instance);
}

uint32_t proposer_get_current_instance(struct proposer* p) {
    return p->next_prepare_instance;
}


static bool proposer_is_instance_chosen(const struct proposer *p, const iid_t instance) {
    khiter_t k = kh_get_chosen_instances(p->chosens, instance);

    if (kh_size(p->chosens) == 0) {
        return false;
    } else if (k == kh_end(p->chosens)) {
    //    paxos_log_debug("Instance: %u not chosen", instance);
        return false;
    } else {
      //  paxos_log_debug("Instance %u chosen", instance);
        return true;
    }
}

iid_t proposer_get_next_instance_to_prepare(struct proposer* p) {
    // min instance that is both not chosen and not inited
    iid_t current_min_instance = p->trim_instance + 1;

    if (!proposer_is_instance_chosen(p, current_min_instance) && kh_size(p->prepare_phase_instances) == 0 && kh_size(p->accept_phase_instances) == 0)
        return current_min_instance;

    current_min_instance++;
    khiter_t preprared_key = kh_get_instance_info(p->prepare_phase_instances, current_min_instance);
    khiter_t accept_key = kh_get_instance_info(p->accept_phase_instances, current_min_instance);
    while(proposer_is_instance_chosen(p, current_min_instance) || preprared_key != kh_end(p->prepare_phase_instances) || accept_key != kh_end(p->accept_phase_instances)) { //kh_end also is used for not found
        current_min_instance++;
        preprared_key = kh_get_instance_info(p->prepare_phase_instances, current_min_instance);
        accept_key = kh_get_instance_info(p->accept_phase_instances, current_min_instance);
    }
    return current_min_instance;
}

uint32_t proposer_get_min_unchosen_instance(struct proposer* p) {
    iid_t current_min_instance = p->trim_instance + 1;

    if (kh_size(p->chosens) == 0)
        return current_min_instance;

    khiter_t key = kh_get_chosen_instances(p->chosens, current_min_instance);
    while(key != kh_end(p->chosens)) { //kh_end also is used for not found
        current_min_instance++;
        key = kh_get_chosen_instances(p->chosens, current_min_instance);
    }
    return current_min_instance;
}


static void set_instance_chosen(struct proposer* p, iid_t instance) {
    khiter_t k = kh_get_chosen_instances(p->chosens, instance);

    if (k == kh_end(p->chosens)) {
        bool* chosen = calloc(1, sizeof(bool));
        *chosen = true;
        int rv;
        k = kh_put_chosen_instances(p->chosens, instance, &rv);
       // assert(rv > 0);
        kh_value(p->chosens, k) = chosen;

        if (p->max_chosen_instance < instance) {
            p->max_chosen_instance = instance;
        }
        paxos_log_debug("Instance %u set to chosen", instance);
    }
}

static bool instance_should_requeue_value(struct standard_proposer_instance_info* inst) {
    return !is_values_equal(NOP, *inst->common_info.proposing_value) && ballot_equal(inst->common_info.last_accepted_ballot, INVALID_BALLOT);
}
bool
proposer_try_to_start_preparing_instance(struct proposer *p, iid_t instance, struct ballot initial_ballot,
                                         paxos_prepare *out) {
   // assert(instance != 0);

    if (instance <= p->trim_instance) {
        paxos_log_debug("Instance %u has been trimmed, so not begining new proposal");
        proposer_next_instance(p);
    }

    if (proposer_is_instance_chosen(p, instance)) {
        paxos_log_debug("Instance %u already chosen so skipping", instance);
        proposer_next_instance(p);
    }


    if (kh_get_instance_info(p->accept_phase_instances, instance) != kh_end(p->accept_phase_instances)) {
        paxos_log_debug("Instance %u is already pending in Acceptance Phase so skipping", instance);
        proposer_next_instance(p);
    }

    unsigned int k = kh_get_instance_info(p->prepare_phase_instances, instance);
    if (k == kh_end(p->prepare_phase_instances)) {
        // New instance
        int rv;
        struct standard_proposer_instance_info* inst = proposer_instance_info_new(instance, initial_ballot, p->acceptors, p->q1);
        k = kh_put_instance_info(p->prepare_phase_instances, instance, &rv);
       // assert(rv > 0);
        kh_value(p->prepare_phase_instances, k) = inst;
        *out = (struct paxos_prepare) {.iid = inst->common_info.iid, .ballot = inst->common_info.ballot};
        return 1;
    } else {
        proposer_next_instance(p);
        return 0;
    }

}


int
proposer_receive_promise(struct proposer *p, paxos_promise *ack)
{
   // assert(ack->iid);
    if (ack->iid <= p->trim_instance) {
        return 0;
    }
   // assert(ack->ballot.proposer_id == p->id);
    if (proposer_is_instance_chosen(p, ack->iid)) {
        paxos_log_debug("Promise dropped, Instance %u chosen", ack->iid);
        return 0;
    }

    struct standard_proposer_instance_info* inst;
    bool found = get_instance_info(p->prepare_phase_instances, ack->iid, &inst);

	if (!found){
		paxos_log_debug("Promise dropped, Instance %u not pending", ack->iid);
		return 0;
	}

	if (ballot_greater_than(inst->common_info.ballot, ack->ballot)) {
        paxos_log_debug("Promise dropped, too old");
        return 0;
    }


    // should never get a promise higher than what the proposer knows of
	//assert(ballot_equal(inst->common_info.ballot, ack->ballot));

	if (quorum_add(&inst->quorum, ack->aid) == 0) {
		paxos_log_debug("Duplicate promise dropped from: %d, iid: %u",
			ack->aid, inst->common_info.iid);
		return 0;
	}

	paxos_log_debug("Received valid promise from: %d, iid: %u",
		ack->aid, inst->common_info.iid);


    check_and_handle_promises_value_for_instance(ack, inst);

    if (quorum_reached(&inst->quorum)) {
        return 1;
    } else {
        return 0;
    }
}

void check_and_handle_promises_value_for_instance(paxos_promise *ack, struct standard_proposer_instance_info *inst) {
    if (ballot_greater_than(ack->value_ballot, inst->common_info.last_accepted_ballot)) {
//        paxos_log_debug("Promise has value");
 //       if (ballot_greater_than_or_equal(ack->value_ballot, inst->common_info.last_accepted_ballot)) {
        paxos_log_debug("New last accepted value for Instance %u", ack->iid);
           copy_ballot(&ack->value_ballot , &inst->common_info.last_accepted_ballot);

            if (proposer_instance_info_has_promised_value(&inst->common_info)) {
                paxos_value_free(&inst->common_info.last_accepted_value);
                paxos_log_debug("Value in promise saved, removed older value");
            }

    //       // assert(ack->value.paxos_value_len > 1);
    //       // assert(strcmp(ack->value.paxos_value_val, "") != 0);

           inst->common_info.last_accepted_value = paxos_value_new(ack->value.paxos_value_val, ack->value.paxos_value_len);
      //  } else {
            paxos_log_debug("Value in promise ignored");
      // }
    }
}


bool proposer_try_determine_value_to_propose(struct proposer* proposer, struct standard_proposer_instance_info* inst) {
    if (!proposer_instance_info_has_promised_value(&inst->common_info)) {
       struct paxos_value* val_to_propose = malloc(sizeof(struct paxos_value*));
       if (value_proposal_manger_get_next(proposer->value_proposal_manager, &val_to_propose, proposer->max_chosen_instance > proposer->trim_instance)){
           inst->common_info.proposing_value = val_to_propose;
           return true;
       } else {
           free(val_to_propose);
           return false;
       }
    } else {
        paxos_log_debug("Instance has a previously proposed Value. Proposing it.");
        inst->common_info.proposing_value = inst->common_info.last_accepted_value;
      //  *val_ret = inst->common_info.last_accepted_value;
//        inst->common_info.proposing_value = inst->common_info.last_accepted_value;
        inst->common_info.last_accepted_value = NULL;
        return true;
    }
}


static bool get_min_instance_to_begin_accept_phase(const struct proposer *p,
                                                   struct standard_proposer_instance_info **to_accept_inst) {
    (*to_accept_inst) = NULL;
    khash_t(instance_info)* hash_table = p->prepare_phase_instances;
    khiter_t key;
    bool first = true;

    for (key = kh_begin(hash_table); key != kh_end(hash_table); ++key) {
        if (kh_exist(hash_table, key) == 0) {
            continue;
        } else {
            struct standard_proposer_instance_info *current_inst = kh_value(hash_table, key);
                if (quorum_reached(&current_inst->quorum) &&
                    !proposer_is_instance_chosen(p, current_inst->common_info.iid)) {
                    if (first) {
                        (*to_accept_inst) = current_inst;
                 //       break;
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


int
proposer_try_accept(struct proposer* p, paxos_accept* out) {

    struct standard_proposer_instance_info *to_accept_inst = NULL;
    bool instance_found = get_min_instance_to_begin_accept_phase(p, &to_accept_inst);

    if (!instance_found) {
        paxos_log_debug("No Instances found to have a Promise Quorum");
        return 0;
    }
	paxos_log_debug("Instance %u ready to accept", to_accept_inst->common_info.iid);
	bool is_value_to_propose = proposer_try_determine_value_to_propose(p, to_accept_inst);

    if (is_value_to_propose) {
        // MOVE INSTANCE TO ACCEPTANCE PHASE
        proposer_move_instance_between_phase(p->prepare_phase_instances, p->accept_phase_instances, to_accept_inst,
                                             p->q2);
        proposer_instance_info_to_accept(&to_accept_inst->common_info, out);
       // assert(to_accept_inst->common_info.proposing_value->paxos_value_len > 1);
       // assert(out->value.paxos_value_len > 1);
       // assert(out->value.paxos_value_val != NULL);
       // assert(strncmp(out->value.paxos_value_val, "", out->value.paxos_value_len));
       // assert(out->iid != 0);
    }


    return is_value_to_propose;
}


int
proposer_receive_accepted(struct proposer* p, paxos_accepted* ack, struct paxos_chosen* chosen)
{
   // assert(ack->iid != 0);
   // assert(ack->value.paxos_value_len > 1);

    if (ack->iid <= p->trim_instance) {
        paxos_log_debug("Acceptance dropped, Instance trimmed");
        return 0;
    }


   // assert(ack->promise_ballot.proposer_id == p->id);


    if (proposer_is_instance_chosen(p, ack->iid)) {
        paxos_log_debug("Acceptance dropped, Instance %u chosen", ack->iid);
        return 0;
    }

    struct standard_proposer_instance_info* instance;
    bool in_accept_phase = get_instance_info(p->accept_phase_instances, ack->iid, &instance);
    if (!in_accept_phase) {
        paxos_log_debug("Accept ack dropped, iid: %u not pending", ack->iid);
        return 0;
    }

   // assert(ballot_equal(ack->value_ballot, ack->promise_ballot));
    if (ballot_equal(ack->value_ballot, instance->common_info.ballot)) {
        if (quorum_add(&instance->quorum, ack->aid) == 0) {
            paxos_log_debug("Duplicate accept dropped from: %d, iid: %u", ack->aid, instance->common_info.iid);
            return 0;
        }

        paxos_log_debug("Recevied new Acceptance from Acceptor %u for Instance %u for ballot %u.%u", ack->aid, ack->iid, ack->value_ballot.number, ack->value_ballot.proposer_id);

        if (quorum_reached(&instance->quorum)) {
           // assert(ballot_equal(ack->promise_ballot, ack->value_ballot));
            paxos_chosen_from_paxos_accepted(chosen, ack); //todo copy needed?
           // assert(ballot_equal(ack->promise_ballot, chosen->ballot));
           // assert(ballot_equal(ack->value_ballot, chosen->ballot));
           // assert(is_values_equal(ack->value, chosen->value));
           // assert(chosen->iid == ack->iid);
            proposer_receive_chosen(p, chosen);
           // assert(chosen->iid != 0);
           // assert(is_values_equal(ack->value, chosen->value));
            return 1;
        }
    }

    return 0;
}


int proposer_receive_chosen(struct proposer* p, struct paxos_chosen* ack) {

   // assert(ack->iid != 0);
   // assert(ack->value.paxos_value_len > 1);


   // if (ack->iid <= p->trim_instance) {
   //     return 0;
   // }


    if (proposer_is_instance_chosen(p, ack->iid)) {
        paxos_log_debug("Chosen message dropped, Instance %u already known to be chosen", ack->iid);
        return 0;
    }


    set_instance_chosen(p, ack->iid);
    bool value_closed = value_proposal_manager_close_if_outstanding(p->value_proposal_manager, &ack->value);

   // assert(proposer_is_instance_chosen(p, ack->iid));
    bool was_acked = false;
    struct standard_proposer_instance_info* inst_accept;
    bool in_accept_phase = get_instance_info(p->accept_phase_instances, ack->iid, &inst_accept);
    if (in_accept_phase) {
        if (!value_closed && instance_should_requeue_value(inst_accept)) {
            value_proposal_manager_check_and_requeue_value(p->value_proposal_manager, inst_accept->common_info.proposing_value);
        }
         remove_instance_from_phase(p->accept_phase_instances, ack->iid);
         standard_proposer_instance_info_free(&inst_accept);
         was_acked = true;
    }

    struct standard_proposer_instance_info* inst_prepare;
    bool in_prepare_phase = get_instance_info(p->prepare_phase_instances, ack->iid, &inst_prepare);
    if (in_prepare_phase) {
        remove_instance_from_phase(p->prepare_phase_instances, ack->iid);
        standard_proposer_instance_info_free(&inst_prepare);
        was_acked = true;
    }

    if (proposer_get_min_unchosen_instance(p) >= ack->iid) {
        struct paxos_trim trim = (struct paxos_trim) {.iid = ack->iid};
        proposer_receive_trim(p, &trim);
    }
    return was_acked;
}


static struct standard_proposer_instance_info*
next_timedout(khash_t(instance_info)* h, khiter_t* k, struct timeval* t)
{
	for (; *k != kh_end(h); ++(*k)) {
		if (kh_exist(h, *k) != 1)
			continue;
		struct standard_proposer_instance_info* inst = kh_value(h, *k);
            if (quorum_reached(&inst->quorum))
                continue;
            if (proposer_instance_info_has_timedout(&inst->common_info, t))
                return inst;
    }
	return NULL;
}

struct timeout_iterator*
proposer_timeout_iterator(struct proposer* p)
{
	struct timeout_iterator* iter;
	iter = malloc(sizeof(struct timeout_iterator));
	iter->prepare_instance = kh_begin(p->prepare_phase_instances);
	iter->accept_instance = kh_begin(p->accept_phase_instances);
	iter->proposer = p;
	gettimeofday(&iter->timeout, NULL);
	return iter;
}


bool proposer_is_instance_pending(struct proposer* p, iid_t instance){
    struct standard_proposer_instance_info* inst;
    bool in_promise_phase = get_instance_info(p->prepare_phase_instances, instance, &inst);
    bool in_acceptance_phase = get_instance_info(p->accept_phase_instances, instance, &inst);
    return in_acceptance_phase || in_promise_phase;
}

int proposer_receive_preempted(struct proposer* p, struct paxos_preempted* preempted, struct paxos_prepare* out) {
    if (preempted->iid <= p->trim_instance) {
        paxos_log_debug("Ignoring preempted, Instance %u trimmed", preempted->iid);
        return 0;
    }

    if (proposer_is_instance_chosen(p, preempted->iid)) {
        paxos_log_debug("Ignoring preempted, instance %u already known to be chosen", preempted->iid);
          return 0;
    }


    struct standard_proposer_instance_info* prepare_instance_info;
    bool in_promise_phase = get_instance_info(p->prepare_phase_instances, preempted->iid, &prepare_instance_info);

    bool proposed_ballot_preempted = false;
    if (in_promise_phase) {
        if (ballot_equal(preempted->attempted_ballot, prepare_instance_info->common_info.ballot)) {
            paxos_log_debug("Instance %u Preempted in the Promise Phase", preempted->iid);
            proposed_ballot_preempted = true;
            proposer_update_instance_info_from_preemption(p, prepare_instance_info, preempted);
            get_prepare_from_instance_info(prepare_instance_info, out);
        }
    }

    struct standard_proposer_instance_info* accept_instance_info;
    bool in_acceptance_phase = get_instance_info(p->accept_phase_instances, preempted->iid, &accept_instance_info);

    if (in_acceptance_phase) {
        if (ballot_equal(preempted->attempted_ballot, accept_instance_info->common_info.ballot)){
            if (instance_should_requeue_value(accept_instance_info)) {
                value_proposal_manager_check_and_requeue_value(p->value_proposal_manager, accept_instance_info->common_info.proposing_value);
            }
            proposed_ballot_preempted = true;
            paxos_log_debug("Instance %u Preempted in the Acceptance Phase", preempted->iid);
            proposer_update_instance_info_from_preemption(p, accept_instance_info, preempted);
            proposer_move_instance_between_phase(p->accept_phase_instances, p->prepare_phase_instances,
                                                 accept_instance_info, p->q2);
            get_prepare_from_instance_info(accept_instance_info, out);
        }
    }

    return proposed_ballot_preempted;
}

void get_prepare_from_instance_info(struct standard_proposer_instance_info *inst,
                                    struct paxos_prepare *out) {
    *out = (struct paxos_prepare) { .iid = inst->common_info.iid,
                             .ballot = (struct ballot) {     .number = inst->common_info.ballot.number,
                                                             .proposer_id = inst->common_info.ballot.proposer_id }
    };
}

void
proposer_update_instance_info_from_preemption(struct proposer *p, struct standard_proposer_instance_info *inst,
                                              struct paxos_preempted *preempted_message)
{

	inst->common_info.ballot = ballot_giver_next(p->ballot_giver, &preempted_message->acceptor_current_ballot);
	//inst->common_info.last_accepted_ballot = INVALID_BALLOT;

    if (proposer_instance_info_has_promised_value(&inst->common_info)) {
        paxos_value_free(&inst->common_info.last_accepted_value);
        inst->common_info.last_accepted_ballot = INVALID_BALLOT;
    }

    if (proposer_instance_info_has_value(&inst->common_info)) {
        paxos_value_free(&inst->common_info.proposing_value);
    }

 //   inst->common_info.proposing_value = NULL;
  //  inst->common_info.last_accepted_value = NULL;
	quorum_clear(&inst->quorum);
	gettimeofday(&inst->common_info.created_at, NULL);
}

static void
proposer_move_instance_between_phase(khash_t(instance_info)* f, khash_t(instance_info)* t,
                                     struct standard_proposer_instance_info* inst, unsigned int quorum_size)
{
    int rv;
    khiter_t k;
    k = kh_get_instance_info(f, inst->common_info.iid);
   // assert(k != kh_end(f));
    kh_del_instance_info(f, k);
    k = kh_put_instance_info(t, inst->common_info.iid, &rv);
   // assert(rv > 0);
    kh_value(t, k) = inst;
    quorum_resize_and_reset(&inst->quorum, quorum_size);

    k = kh_get_instance_info(f, inst->common_info.iid);
   // assert(k == kh_end(f));
}

static void
proposer_trim_proposer_instance_infos(struct proposer* p, khash_t(instance_info)* h, iid_t iid)
{
	khiter_t k;
	for (k = kh_begin(h); k != kh_end(h); ++k) {
		if (kh_exist(h, k) != 1)
			continue;
		struct standard_proposer_instance_info* inst = kh_value(h, k);
		if (inst->common_info.iid <= iid) {
			if (proposer_instance_info_has_value(&inst->common_info) && instance_should_requeue_value(inst)) {
			    value_proposal_manager_check_and_requeue_value(p->value_proposal_manager, inst->common_info.proposing_value);
			}
			kh_del_instance_info(h, k);
            standard_proposer_instance_info_free(&inst);
		}
	}
}



void
proposer_set_current_instance(struct proposer* p, iid_t iid)
{
   // assert(iid >= p->next_prepare_instance);
    p->next_prepare_instance = iid;

    if (iid < proposer_get_min_unchosen_instance(p)){
        struct paxos_trim trim = {iid};
        proposer_receive_trim(p, &trim);
    }
    /*
    if (iid >= p->next_prepare_instance) {
        p->next_prepare_instance = iid;
        // remove proposer_instance_infos older than iid
        if (iid < proposer_get_min_unchosen_instance(p)) {
            proposer_trim_proposer_instance_infos(p, p->prepare_phase_instances, iid);
            proposer_trim_proposer_instance_infos(p, p->accept_phase_instances, iid);
        }
    }*/
}

void proposer_get_state(struct proposer* p, struct proposer_state* state) {
   state->proposer_id = p->id;
   state->next_prepare_instance = p->next_prepare_instance;
   state->max_chosen_instance = p->max_chosen_instance;
   state->trim_instance = p->trim_instance;
}

void proposer_receive_proposer_state(struct proposer* p, struct proposer_state* state) {
  //  int jump = state->trim_instance > p->trim_instance ? paxos_config.fall_behind_jump: 0;
    struct paxos_trim trim = {state->trim_instance};
    proposer_receive_trim(p, &trim);
    p->max_chosen_instance = state->max_chosen_instance > p->max_chosen_instance ? state->max_chosen_instance : p->max_chosen_instance;
 //   p->next_prepare_instance = p->trim_instance + 1 == p->next_prepare_instance ? state->next_prepare_instance : p->next_prepare_instance;
}

void
proposer_receive_acceptor_state(struct proposer* p, paxos_standard_acceptor_state* state)
{
  //  if (acceptor_state->current_instance > p->max_instance_inited) {
        //   p->max_instance_inited = acceptor_state->current_instance;
 //   }
  //  if (acceptor_state->trim_iid > p->trim_instance ) {
        paxos_log_debug("Received new acceptor acceptor_state, %u trim_iid from %u", state->trim_iid, state->aid);
     //   p->trim_instance = acceptor_state->trim_iid;

    //    if (acceptor_state->trim_iid >= p->next_prepare_instance) {
      //      p->next_prepare_instance = acceptor_state->trim_iid + 1;
      //  }

        struct paxos_trim trim = (struct paxos_trim) {.iid = state->trim_iid};
        proposer_receive_trim(p, &trim);
  //  }
}

void proposer_receive_trim(struct proposer* p,
                           struct paxos_trim* trim_msg){
    if (trim_msg->iid > p->trim_instance) {
        paxos_log_debug("Received a new trim message to instance: %u", trim_msg->iid);
        p->trim_instance = trim_msg->iid;

        //if (p->trim_instance >= p->next_prepare_instance) {
       //     p->next_prepare_instance = p->trim_instance + 100;
       //     proposer_trim_proposer_instance_infos(p, p->prepare_phase_instances, p->next_prepare_instance - 1);
       //     proposer_trim_proposer_instance_infos(p, p->accept_phase_instances, p->next_prepare_instance - 1);

    //    }
        proposer_trim_proposer_instance_infos(p, p->prepare_phase_instances, trim_msg->iid);
        proposer_trim_proposer_instance_infos(p, p->accept_phase_instances, trim_msg->iid);

         p->next_prepare_instance = p->trim_instance >= p->next_prepare_instance ? p->trim_instance + 1 + paxos_config.fall_behind_jump : p->next_prepare_instance;


    }


}

int
timeout_iterator_prepare(struct timeout_iterator* iter, paxos_prepare* out)
{
	struct proposer* p = iter->proposer;
    struct standard_proposer_instance_info *inst = next_timedout(p->prepare_phase_instances, &iter->prepare_instance, &iter->timeout);
	if (inst == NULL)
		return 0;
	*out = (struct paxos_prepare){.iid = inst->common_info.iid, (struct ballot) {.number = inst->common_info.ballot.number, .proposer_id = inst->common_info.ballot.proposer_id}};
	inst->common_info.created_at = iter->timeout;
	return 1;
}

int
timeout_iterator_accept(struct timeout_iterator* iter, paxos_accept* out) {
    struct standard_proposer_instance_info *inst;
    struct proposer *p = iter->proposer;
    inst = next_timedout(p->accept_phase_instances, &iter->accept_instance, &iter->timeout);
    if (inst == NULL)
        return 0;

    gettimeofday(&inst->common_info.created_at, NULL);
    proposer_instance_info_to_accept(&inst->common_info, out);
    return 1;
}

void
timeout_iterator_free(struct timeout_iterator* iter) {
	free(iter);
}

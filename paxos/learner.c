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


#include "learner.h"
#include "khash.h"
#include "ballot.h"
#include "quorum.h"
#include "paxos_value.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <paxos_message_conversion.h>

struct instance
{
	iid_t iid;
	struct ballot last_update_ballot;
	struct quorum quorum;
//	paxos_accepted** acks;
	struct paxos_value current_ballots_value;
//	paxos_accepted* final_value;
	bool chosen;
};

KHASH_MAP_INIT_INT(instance, struct instance*)

struct learner
{
	int acceptors;
	int quorum_size;
	int late_start;
	iid_t current_min_instance_to_execute;
	iid_t highest_instance_chosen;
	khash_t(instance)* instances;
	iid_t trim_iid;
};

static bool learner_get_instance(struct learner *l, iid_t iid, struct instance **ret);
static struct instance* learner_get_current_instance(struct learner* l);
static struct instance* learner_get_instance_or_create(struct learner* l,
	iid_t iid);
static void learner_remove_instance_from_pending(struct learner* l, struct instance **inst);
static struct instance *instance_new(iid_t instance, int num_acceptors_in_quorum, int quorum_size);
static void instance_free(struct instance **inst);
static void instance_update(struct instance* i, paxos_accepted* ack, int acceptors, int quorum_size);
static int instance_has_quorum(struct instance* i, int acceptors, int quorum_size);
static void instance_add_accept(struct instance* i, paxos_accepted* ack);
static paxos_accepted* paxos_accepted_dup(paxos_accepted* ack);


struct learner*
learner_new(int acceptors)
{
	struct learner* l = malloc(sizeof(struct learner));
	l->acceptors = acceptors;
	l->quorum_size = paxos_config.quorum_2;
	l->current_min_instance_to_execute = 1;
	l->highest_instance_chosen = INVALID_INSTANCE;
    l->trim_iid = INVALID_INSTANCE;
	l->late_start = !paxos_config.learner_catch_up;
	l->instances = kh_init(instance);
	return l;
}

void
learner_free(struct learner* l)
{
	struct instance* inst;
	kh_foreach_value(l->instances, inst, instance_free(&inst));
	kh_destroy(instance, l->instances);
	free(l);
}

void
learner_set_instance_id(struct learner* l, iid_t iid)
{
	l->current_min_instance_to_execute = iid + 1;
	l->highest_instance_chosen = iid;
}

void check_and_handle_late_start(struct learner* l, iid_t instance) {
    if (l->late_start) {
        l->late_start = false;
        l->current_min_instance_to_execute = instance;
        l->trim_iid = instance - 1;
    }
}


bool learner_is_instance_outdated(struct learner* l, iid_t instance, char* message_type) {
    if (instance < l->current_min_instance_to_execute) {
        paxos_log_debug("Dropped %s for Instance %u. Instance has already been closed and executed", message_type, instance);
        return true;
    } else {
        return false;
    }
}

bool learner_is_instance_chosen(struct instance* inst, char* message_type) {
    if (inst->chosen) {
        paxos_log_debug("Dropping %s Message for Instance %u. It is already Chosen.", message_type, inst->iid);
        return true;
    } else {
        return false;
    }
}

bool learner_is_ballot_outdated(const struct instance* inst, struct ballot cmp, char* message_type) {
    if (ballot_greater_than_or_equal(cmp, inst->last_update_ballot)){
        paxos_log_debug("Received %s Message for Instance %u at Ballot %u.%u", message_type,
                        inst->iid,
                        cmp.number, cmp.proposer_id);
        return false;
    } else {
        paxos_log_debug("Received Acceptance for %u Instance is out of date. Ignoring it.", inst->iid);
        return true;
    }
}

static void check_and_handle_new_ballot(struct instance* inst, struct ballot cmp, struct paxos_value value) {
    if (ballot_greater_than(cmp, inst->last_update_ballot)) {
        if (!ballot_equal(inst->last_update_ballot, INVALID_BALLOT)){
            paxos_value_destroy(&inst->current_ballots_value);
        }
        inst->last_update_ballot = cmp;
        copy_value(&value, &inst->current_ballots_value);
        quorum_clear(&inst->quorum);
    }
}

static void learner_check_and_set_highest_instance_closed(struct learner* l, iid_t instance_chosen) {
    if (instance_chosen > l->highest_instance_chosen) {
        paxos_log_debug("New highest instance chosen %u", instance_chosen);
        l->highest_instance_chosen = instance_chosen;
    }
}


int learner_receive_chosen(struct learner* l, struct paxos_chosen* chosen){
    char phase_name[] = "Acceptance";
    check_and_handle_late_start(l, chosen->iid);

    if (learner_is_instance_outdated(l, chosen->iid, phase_name)) {
        return 0;
    }

    struct instance* inst = learner_get_instance_or_create(l, chosen->iid);

    if (learner_is_instance_chosen(inst, phase_name)) return 0;

    inst->chosen = true;
    inst->last_update_ballot = chosen->ballot;
    copy_value(&chosen->value, &inst->current_ballots_value);
    learner_check_and_set_highest_instance_closed(l, chosen->iid);
    return 1;
}

/*
void learner_receive_trim(struct learner* l, struct paxos_trim* trim_msg){
    learner_set_trim(l, trim_msg->iid);
    learner_set_instance_id(l, trim_msg->iid);
}
*/
int
learner_receive_accepted(struct learner* l, paxos_accepted* ack, struct paxos_chosen* chosen_msg)
{
    char phase_name[] = "Acceptance";
    check_and_handle_late_start(l, ack->iid);

	if (learner_is_instance_outdated(l, ack->iid, phase_name)) {
		return 0;
	}

	struct instance* inst = learner_get_instance_or_create(l, ack->iid);

    if (learner_is_instance_chosen(inst, phase_name)) return 0;

 //  // assert(ballot_equal(ack->promise_ballot, ack->value_ballot));
    if (learner_is_ballot_outdated(inst, ack->value_ballot, phase_name)) return 0;

    check_and_handle_new_ballot(inst, ack->value_ballot, ack->value);

    bool duplicate = !quorum_add(&inst->quorum, ack->aid);
    if (duplicate) {
        paxos_log_debug("Duplicate %s ignored", phase_name);
        return 0;
    }

    paxos_log_debug("Received new Acceptance from Acceptor %u for Instance %u at Ballot %u.%u",
                    ack->aid, ack->iid, ack->value_ballot.number, ack->value_ballot.proposer_id);

    if (quorum_reached(&inst->quorum)){
        inst->chosen = true;
        learner_check_and_set_highest_instance_closed(l, ack->iid);
        *chosen_msg = (struct paxos_chosen) {
                .iid = ack->iid,
                .ballot = ack->value_ballot,
                .value = ack->value
        };
       // paxos_chosen_from_paxos_accepted(chosen_msg, ack);
        paxos_log_debug("Quorum reached");
        return 1;
    } else {
        return 0;
    }
}


int
learner_deliver_next(struct learner* l, struct paxos_value *out) {
    paxos_log_debug("Entering deliver next");
    struct instance* inst;
    bool instance_exists = learner_get_instance(l, l->current_min_instance_to_execute, &inst);

    if (instance_exists) {
        if (inst->chosen) {
            paxos_log_debug("Value exists and is chosen");
            paxos_value_copy(out, &inst->current_ballots_value);
            paxos_log_debug("Copied value to deliver");
            learner_remove_instance_from_pending(l, &inst);


            paxos_log_debug("Executing Instance %u", l->current_min_instance_to_execute);
            l->current_min_instance_to_execute++;
            return true;
        }
    }
    paxos_log_debug("Unable to deliver next value");
    return false;
}

int
learner_has_holes(struct learner* l, iid_t* from, iid_t* to)
{
	if (l->highest_instance_chosen > l->current_min_instance_to_execute) {
		*from = l->current_min_instance_to_execute;
		*to = l->highest_instance_chosen;
		return 1;
	} else {
	//    *from = INVALID_INSTANCE;
	 //   *to = INVALID_INSTANCE;
//        *from = l->current_min_instance_to_execute-1; // trim behind
     //   *to = l->highest_instance_chosen;
        return 0;
    }
}

static bool
learner_get_instance(struct learner *l, iid_t iid, struct instance **ret)
{
	khiter_t k = kh_get_instance(l->instances, iid);
	if (k != kh_end(l->instances)) {
        if (kh_exist(l->instances, k) == 1) {
            *ret = kh_value(l->instances, k);
            return true;
        }
    }
  //  *ret = NULL;
    return false;
}
/*
static struct instance*
learner_get_current_instance(struct learner* l)
{

	return learner_get_instance(l, l->current_min_instance_to_execute, NULL);
}*/

static struct instance*
learner_get_instance_or_create(struct learner* l, iid_t iid)
{
	struct instance* inst;
	bool instance_existing = learner_get_instance(l, iid, &inst);
	if (!instance_existing) {
		int rv;
		khiter_t k = kh_put_instance(l->instances, iid, &rv);
		//assert(rv != -1);
		inst = instance_new(iid, l->acceptors, l->quorum_size);
		kh_value(l->instances, k) = inst;
	}
	return inst;
}

static void
learner_remove_instance_from_pending(struct learner* l, struct instance **inst)
{
    paxos_log_debug("Removing Instance %u from pending", (*inst)->iid);
    khiter_t k = kh_get_instance(l->instances, (**inst).iid);
	kh_del_instance(l->instances, k);
    instance_free(inst);
}

static struct instance *
instance_new(iid_t instance, int num_acceptors_in_quorum, int quorum_size)
{
//	int i;
	struct instance* inst = malloc(sizeof(struct instance));
	quorum_init(&inst->quorum, num_acceptors_in_quorum, quorum_size);
	inst->iid = instance;
	inst->last_update_ballot = INVALID_BALLOT;
	inst->current_ballots_value = INVALID_VALUE;
	inst->chosen = false;
	return inst;
}

static void
instance_free(struct instance **inst)
{
//	int i;
	//for (i = 0; i < acceptors; i++)
	//	if (inst->acks[i] != NULL)
	//		paxos_accepted_free(inst->acks[i]);
	//free(inst->acks);
	//free(inst);
    paxos_value_destroy(&(*inst)->current_ballots_value);
    quorum_destroy(&(**inst).quorum);
    free(*inst);
    //*inst = NULL;
}


/*
	Checks if a given instance is closed, that is if a quorum of acceptor
	accepted the same value ballot pair.
	Returns 1 if the instance is closed, 0 otherwise.
*/

/*
static int
instance_has_quorum(struct instance* inst, int acceptors, int quorum_size)
{
	paxos_accepted* curr_ack;
	int i, a_valid_index = -1, count = 0;

	if (inst->final_value != NULL)
		return 1;

	for (i = 0; i < acceptors; i++) {
		curr_ack = inst->acks[i];

		// Skip over missing acceptor acks
		if (curr_ack == NULL) continue;

		// Count the ones "agreeing" with the last added
		if (ballot_equal(curr_ack->value_ballot, inst->last_update_ballot)) {
			count++;
			a_valid_index = i;
		}
	}

	if (count >= quorum_size) {
		paxos_log_debug("Reached quorum of %u, iid: %u is closed!", quorum_size, inst->iid);
		inst->final_value = inst->acks[a_valid_index];
		return 1;
	}
	return 0;
}*/

/*
	Adds the given paxos_accepted to the given instance,
	replacing the previous paxos_accepted, if any.
*//*
static void
instance_add_accept(struct instance* inst, paxos_accepted* accepted)
{
	int acceptor_id = accepted->aid;
	if (inst->acks[acceptor_id] != NULL)
		paxos_accepted_free(inst->acks[acceptor_id]);
	inst->acks[acceptor_id] = paxos_accepted_dup(accepted);
	assert(accepted->value_ballot.number != 0);
	assert(accepted->value.paxos_value_len > 0);
	//if (ballot_greater_than(accepted->value_ballot, inst->last_update_ballot))
    	copy_ballot(&accepted->value_ballot, &inst->last_update_ballot);
}
*/
/*
	Returns a copy of it's argument.
*/
static paxos_accepted*
paxos_accepted_dup(paxos_accepted* ack)
{
	paxos_accepted* copy;
	copy = malloc(sizeof(struct paxos_accepted));
	memcpy(copy, ack, sizeof(struct paxos_accepted));
	paxos_value_copy(&copy->value, &ack->value);
	return copy;
}

iid_t learner_get_trim(struct learner* l){
    return l->trim_iid;
}

void learner_set_trim(struct learner* l, const iid_t trim){
   // assert(trim > l->trim_iid);
    l->trim_iid = trim;
}

/*
void
learner_receive_chosen(struct learner* l, struct paxos_chosen* chosen_msg) {
    // this is weird - change later
    struct instance *inst = learner_get_instance(l, chosen_msg->iid);
   // assert(inst->iid == chosen_msg->iid);

    struct paxos_accepted* accepted = calloc(1, sizeof(struct paxos_accepted));
    paxos_accepted_from_paxos_chosen(accepted, chosen_msg);

    inst->final_value = accepted;

    if(chosen_msg->iid > l->current_min_instance_to_execute)
        l->current_min_instance_to_execute++;

    if(chosen_msg->iid > l->highest_instance_chosen){
        l->highest_instance_chosen = chosen_msg->iid;
    }
}

*/


iid_t learner_get_instance_to_trim(struct learner* l) {
   // assert(l->highest_instance_chosen <= l->current_min_instance_to_execute);
    iid_t prev_instance = l->current_min_instance_to_execute - 1;
    return prev_instance;
}
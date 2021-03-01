//
// Created by Michael Davis on 16/11/2020.
//


#include "round_robin_allocator.h"
#include "ballot_giver.h"

#include <stdlib.h>

struct round_robin_allocator {
    unsigned int proposer_id;
    unsigned int proposer_count;
    struct ballot_giver* ballot_giver;
    unsigned int ballot_bias;
    bool delay_non_allocated_instances;
};

struct round_robin_allocator *
round_robin_allocator_new(unsigned int pid, unsigned int proposer_count, bool ballot_bias,
                          bool delay_non_allocated_instances,
                          unsigned int ballot_increment) {
    struct round_robin_allocator* allocator = malloc(sizeof(*allocator));
    *allocator = (struct round_robin_allocator) {
        .proposer_id = pid,
        .ballot_giver = ballot_giver_new(pid, ballot_increment),
        .proposer_count = proposer_count,
        .ballot_bias = ballot_bias * ballot_increment,
        .delay_non_allocated_instances = delay_non_allocated_instances
    };
    return allocator;
}

void round_robin_allocator_free(struct round_robin_allocator** allocator) {
    free(&allocator);
}

bool round_robin_allocator_is_allocated(struct round_robin_allocator* allocator, iid_t instance) {
    return ((int) instance % allocator->proposer_count) == allocator->proposer_id;
}

struct ballot round_robin_allocator_get_ballot(struct round_robin_allocator* allocator, iid_t instance) {
    bool allocated = round_robin_allocator_is_allocated(allocator, instance);
    struct ballot initial_ballot;
    if (allocated) {
        paxos_log_debug("Instance %u is biased for Proposer %u", instance, allocator->proposer_id);
        struct ballot bal = (struct ballot) {allocator->ballot_bias, allocator->proposer_id};
        initial_ballot = ballot_giver_next(allocator->ballot_giver, &bal);
    } else {
        struct ballot bal = INVALID_BALLOT;
        initial_ballot = ballot_giver_next(allocator->ballot_giver, &bal);
    }
    return initial_ballot;
}

bool round_robin_allocator_should_delay_proposal(struct round_robin_allocator* allocator, iid_t instance) {
    return round_robin_allocator_is_allocated(allocator, instance) && allocator->delay_non_allocated_instances;
}

//
// Created by Michael Davis on 16/11/2020.
//

#ifndef A_LESS_WRITEY_PAXOS_ROUND_ROBIN_ALLOCATION_H
#define A_LESS_WRITEY_PAXOS_ROUND_ROBIN_ALLOCATION_H

#include "paxos.h"
#include "stdbool.h"

struct round_robin_allocator;

struct round_robin_allocator *
round_robin_allocator_new(unsigned int pid, unsigned int proposer_count, bool ballot_bias,
                          bool delay_non_allocated_instances,
                          unsigned int ballot_increment);

void round_robin_allocator_free(struct round_robin_allocator** allocator);

bool round_robin_allocator_is_allocated(struct round_robin_allocator* allocator, iid_t instance);

struct ballot round_robin_allocator_get_ballot(struct round_robin_allocator* allocator, iid_t instance);

bool round_robin_allocator_should_delay_proposal(struct round_robin_allocator* allocator, iid_t instance);

#endif //A_LESS_WRITEY_PAXOS_ROUND_ROBIN_ALLOCATION_H

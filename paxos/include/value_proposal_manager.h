//
// Created by Michael Davis on 16/11/2020.
//

#ifndef A_LESS_WRITEY_PAXOS_VALUE_PROPOSAL_MANAGER_H
#define A_LESS_WRITEY_PAXOS_VALUE_PROPOSAL_MANAGER_H

#include <paxos.h>
#include <stdbool.h>

struct value_proposal_manager;

struct value_proposal_manager *value_proposal_manager_new(int initial_queue_len, bool repropose_values);

void value_proposal_manager_free(struct value_proposal_manager** manager);

void value_proposal_manager_enqueue(struct value_proposal_manager* manager, struct paxos_value** enqueuing_value_ptr);

bool value_proposal_manger_get_next(struct value_proposal_manager *manager, struct paxos_value **value_ret, bool is_holes);

bool value_proposal_manager_is_outstanding(struct value_proposal_manager* manager, struct paxos_value *value_ptr);

bool value_proposal_manager_close_if_outstanding(struct value_proposal_manager* manager, struct paxos_value *value_ptr);

bool value_proposal_manager_check_and_requeue_value(struct value_proposal_manager* manager, struct paxos_value *value);




#endif //A_LESS_WRITEY_PAXOS_VALUE_PROPOSAL_MANAGER_H

//
// Created by Michael Davis on 11/11/2020.
//

#include <paxos.h>
#include <sys/time.h>

#ifndef A_LESS_WRITEY_PAXOS_EV_PROPOSER_INSTANCE_STRATEGY_H
#define A_LESS_WRITEY_PAXOS_EV_PROPOSER_INSTANCE_STRATEGY_H

struct instance_strategy;

//struct strategy_info;

struct instance_strategy* instance_strategy_new(unsigned int avg_msg_delay_us, iid_t proposers_initial_instance);

void instance_strategy_free(struct instance_strategy** pStrategy);

void instance_strategy_update_time_proposers_instance_chosen(struct instance_strategy* strategy, struct timeval time_chosen);

void instance_strategy_update_max_instance_chosen(struct instance_strategy* strategy, iid_t max_chosen_instance);

void instance_strategy_update_max_working_instance(struct instance_strategy* strategy, iid_t max_working_instance);



iid_t instance_strategy_get_proposal_instance(struct instance_strategy* strategy);



#endif //A_LESS_WRITEY_PAXOS_EV_PROPOSER_INSTANCE_STRATEGY_H

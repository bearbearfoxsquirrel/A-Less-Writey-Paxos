//
// Created by Michael Davis on 11/11/2020.
//

#include "instance_strategy.h"
#include <stdlib.h>
#include <assert.h>

#define INVALID_TIME_CHOSEN (struct timeval) {0}
#define TIMEVAL_ZERO (struct timeval) {0}

struct instance_strategy {
    struct timeval increment_interval;
    unsigned int avg_msg_delay_us;
    iid_t proposers_max_instance;
    iid_t max_chosen_instance;
    iid_t max_working_instance;
    iid_t instance_choosing_velocity;
    struct timeval previous_velocity_calc;
    struct timeval time_proposers_instance_was_chosen;
};


struct instance_strategy* instance_strategy_new(unsigned int avg_msg_delay_us, iid_t proposers_initial_instance){
    struct instance_strategy* strategy = malloc(sizeof(*strategy));

    //add increment_interval
    struct timeval now;
    gettimeofday(&now, NULL);
    *strategy = (struct instance_strategy) {
        .avg_msg_delay_us = avg_msg_delay_us,
        .proposers_max_instance = proposers_initial_instance,
        .previous_velocity_calc = now,
        .max_chosen_instance = INVALID_INSTANCE,
        .instance_choosing_velocity = 0,
        .max_working_instance = INVALID_INSTANCE,
        .time_proposers_instance_was_chosen = INVALID_TIME_CHOSEN
    };
    return strategy;
}


void instance_strategy_free(struct instance_strategy** pStrategy) {
    free(*pStrategy);
}

void instance_strategy_update_time_proposers_instance_chosen(struct instance_strategy* strategy, struct timeval time_chosen){
    // time_chosen is strictly ascending
    struct timeval res;
    struct timeval min_diff = {0};
    timersub(&time_chosen, &strategy->time_proposers_instance_was_chosen, &res);
    assert(timercmp(&res, &min_diff, >));

    strategy->time_proposers_instance_was_chosen = time_chosen;
}


void instance_strategy_choosing_velocity(struct instance_strategy* strategy, iid_t instance){
    struct timeval current_time;
    gettimeofday(&current_time, NULL);

    struct timeval diff;
    timersub(&current_time, &strategy->previous_velocity_calc, &diff);
    unsigned long diff_us = diff.tv_sec * 1000000 + diff.tv_usec;
    strategy->instance_choosing_velocity = strategy->instance_choosing_velocity - instance / diff_us;
}

void instance_strategy_update_max_instance_chosen(struct instance_strategy* strategy, iid_t max_chosen_instance){
    assert(max_chosen_instance >= strategy->max_chosen_instance);

    strategy->max_chosen_instance = max_chosen_instance;
}


void instance_strategy_update_max_working_instance(struct instance_strategy* strategy, iid_t max_working_instance){
    assert(max_working_instance >= strategy->max_working_instance);
    strategy->max_working_instance = max_working_instance;
    instance_strategy_choosing_velocity(strategy, max_working_instance);
}



iid_t instance_strategy_get_proposal_instance(struct instance_strategy* strategy) {
    struct timeval zero = TIMEVAL_ZERO;
    if (timercmp(&strategy->time_proposers_instance_was_chosen, &zero, ==)) {
        return strategy->proposers_max_instance;
    } else {
        struct timeval now;
        gettimeofday(&now, NULL);
        struct timeval res;
        timersub(&now, &strategy->time_proposers_instance_was_chosen, &res);
        if (timercmp(&res, &strategy->increment_interval, >)){
            return strategy->max_working_instance + strategy->instance_choosing_velocity * strategy->avg_msg_delay_us;
        } else {
           return strategy->proposers_max_instance;
        };
    //    if ()
//        int velocity = strategy
 //       return
    }
}
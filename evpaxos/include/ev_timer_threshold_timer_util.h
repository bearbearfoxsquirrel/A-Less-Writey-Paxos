//
// Created by Michael Davis on 25/03/2020.
//

#ifndef A_LESS_WRITEY_PAXOS_EV_TIMER_THRESHOLD_TIMER_UTIL_H
#define A_LESS_WRITEY_PAXOS_EV_TIMER_THRESHOLD_TIMER_UTIL_H

#include "performance_threshold_timer.h"

struct performance_threshold_timer* get_promise_performance_threshold_timer_new();
struct performance_threshold_timer* get_acceptance_performance_threshold_timer_new();

void ev_performance_timer_and_log_threshold_timer(struct performance_threshold_timer *timer, char* timer_name);
void ev_performance_timer_stop_check_and_clear_timer(struct performance_threshold_timer *timer, char* timer_name);

#endif //A_LESS_WRITEY_PAXOS_EV_TIMER_THRESHOLD_TIMER_UTIL_H

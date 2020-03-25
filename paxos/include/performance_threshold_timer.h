//
// Created by Michael Davis on 25/03/2020.
//

#include <stdbool.h>
#include "sys/time.h"

#ifndef A_LESS_WRITEY_PAXOS_PERFORMANCE_TIMER_H
#define A_LESS_WRITEY_PAXOS_PERFORMANCE_TIMER_H



struct performance_threshold_timer;


struct performance_threshold_timer performance_threshold_timer_initialise(struct timespec max_time);

struct performance_threshold_timer* performance_threshold_timer_new(struct timespec max_time);

void performance_threshold_timer_begin_timing(struct performance_threshold_timer* timer);

void performance_threshold_timer_end_timing(struct performance_threshold_timer* timer);

bool performance_threshold_timer_was_threshold_exceeded(struct performance_threshold_timer* timer);

struct timespec performance_threshold_timer_get_elapsed_time(struct performance_threshold_timer* timer);

void performance_threshold_timer_clear(struct performance_threshold_timer* timer);

#endif //A_LESS_WRITEY_PAXOS_PERFORMANCE_TIMER_H

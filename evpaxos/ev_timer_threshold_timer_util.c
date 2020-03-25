//
// Created by Michael Davis on 25/03/2020.
//

#include "ballot.h"
#include "paxos_message_conversion.h"
#include <paxos_types.h>
#include <evdns.h>
#include <event2/event.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "message.h"
#include "writeahead_window_acceptor.h"
#include "peers.h"
#include "standard_stable_storage.h"
#include "evpaxos.h"
#include "ev_timer_threshold_timer_util.h"


struct performance_threshold_timer * get_acceptance_performance_threshold_timer_new() {
    return performance_threshold_timer_new((struct timespec) {.tv_sec = 0, .tv_nsec = 100000});
}

struct performance_threshold_timer * get_promise_performance_threshold_timer_new() {
    return performance_threshold_timer_new((struct timespec) {.tv_sec = 0, .tv_nsec = 1000000});
}

void ev_performance_timer_and_log_threshold_timer(struct performance_threshold_timer *timer, char* timer_name){
    if (performance_threshold_timer_was_threshold_exceeded(timer)) {
        paxos_log_debug("Time target exceeded for %s", timer_name);
    }
}

void ev_performance_timer_stop_check_and_clear_timer(struct performance_threshold_timer *timer, char* timer_name){
    performance_threshold_timer_end_timing(timer);
    ev_performance_timer_and_log_threshold_timer(timer, timer_name);
    performance_threshold_timer_clear(timer);
}
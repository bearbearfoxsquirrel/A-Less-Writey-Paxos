//
// Created by Michael Davis on 25/03/2020.
//

#include "ballot.h"
#include "paxos_message_conversion.h"
#include "evpaxos.h"
#include "ev_timer_threshold_timer_util.h"


struct performance_threshold_timer * get_chosen_performance_threshold_timer_new(){
    return performance_threshold_timer_new((struct timespec) {.tv_sec = 0, .tv_nsec = 1000});
}
struct performance_threshold_timer * get_acceptance_performance_threshold_timer_new() {
    return performance_threshold_timer_new((struct timespec) {.tv_sec = 0, .tv_nsec = 1000});
}

struct performance_threshold_timer * get_promise_performance_threshold_timer_new() {
    return performance_threshold_timer_new((struct timespec) {.tv_sec = 0, .tv_nsec = 1000});
}


struct performance_threshold_timer * get_chosen_acceptor_performance_threshold_timer_new() {
    return performance_threshold_timer_new((struct timespec) {.tv_sec = 0, .tv_nsec = 1000});
}



struct performance_threshold_timer * get_chosen_proposer_performance_threshold_timer_new() {
    return performance_threshold_timer_new((struct timespec) {.tv_sec = 0, .tv_nsec = 1000});
}



struct performance_threshold_timer * get_accepted_proposer_threshold_timer_new() {
    return performance_threshold_timer_new((struct timespec) {.tv_sec = 0, .tv_nsec = 1000});
}



struct performance_threshold_timer * get_prepare_threshold_timer_new() {
    return performance_threshold_timer_new((struct timespec) {.tv_sec = 0, .tv_nsec = 1000});
}

struct performance_threshold_timer * get_preempt_threshold_timer_new() {
    return performance_threshold_timer_new((struct timespec) {.tv_sec = 0, .tv_nsec = 1000});
}

struct performance_threshold_timer * get_accept_threshold_timer_new() {
    return performance_threshold_timer_new((struct timespec) {.tv_sec = 0, .tv_nsec = 1000});
}

void ev_performance_timer_and_log_threshold_timer(struct performance_threshold_timer *timer, char* timer_name){
    if (performance_threshold_timer_was_threshold_exceeded(timer)) {
        struct timespec elapsed_time = performance_threshold_timer_get_elapsed_time(timer);
        paxos_log_debug("Time target exceeded for %s. It took %lld.%.9ld seconds", timer_name, elapsed_time.tv_sec, elapsed_time.tv_nsec);
    }
}

void ev_performance_timer_stop_check_and_clear_timer(struct performance_threshold_timer *timer, char* timer_name){
    performance_threshold_timer_end_timing(timer);
    ev_performance_timer_and_log_threshold_timer(timer, timer_name);
    performance_threshold_timer_clear(timer);
}

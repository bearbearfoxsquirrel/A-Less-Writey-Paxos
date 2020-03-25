//
// Created by Michael Davis on 25/03/2020.
//

#include <assert.h>
#include "performance_threshold_timer.h"
#include "stdlib.h"


struct performance_threshold_timer {
    struct timespec begin_time;
    struct timespec end_time;
    struct timespec threshold_time;
};

static struct timespec get_cleared_timespec() {
    return (struct timespec) {0, 0};
}

static bool is_timespecs_equal(struct timespec lhs, struct timespec rhs) {
    return lhs.tv_sec == rhs.tv_sec && lhs.tv_nsec == rhs.tv_nsec;
}

static bool is_timespec_greater_than(struct timespec lhs, struct timespec rhs) {
    if (lhs.tv_sec > rhs.tv_sec) {
        return true;
    } else {
        if (lhs.tv_nsec > rhs.tv_nsec) {
            return true;
        } else {
            return false;
        }
    }
}

static bool is_timespec_greater_than_or_equal(struct timespec lhs, struct timespec rhs) {
    return is_timespec_greater_than(lhs, rhs) || is_timespecs_equal(lhs, rhs);
}

static struct timespec timespec_get_elapsed_time(struct timespec start, struct timespec stop) {
    assert(is_timespec_greater_than_or_equal(stop, start));
    struct timespec result;

    if ((stop.tv_nsec - start.tv_nsec) < 0) {
        result.tv_sec = stop.tv_sec - start.tv_sec - 1;
        result.tv_nsec = stop.tv_nsec - start.tv_nsec + 1000000000;
    } else {
        result.tv_sec = stop.tv_sec - start.tv_sec;
        result.tv_nsec = stop.tv_nsec - start.tv_nsec;
    }

    return result;
}

static void set_time_to_now(struct timespec* timer_to_set) {
    int error = clock_gettime(CLOCK_MONOTONIC, timer_to_set);
    assert(error != -1);
}


static bool is_timespec_cleared(struct timespec timespec_to_check) {
    return is_timespecs_equal(timespec_to_check, get_cleared_timespec());
}

void performance_threshold_timer_clear(struct performance_threshold_timer* timer){
    timer->begin_time = get_cleared_timespec();
    timer->end_time = get_cleared_timespec();
}


struct performance_threshold_timer performance_threshold_timer_initialise(struct timespec max_time) {
    struct performance_threshold_timer new_timer;
    performance_threshold_timer_clear(&new_timer);
    new_timer.threshold_time = max_time;
    return new_timer;
}



struct performance_threshold_timer* performance_threshold_timer_new(struct timespec max_time){
    struct performance_threshold_timer* new_timer = malloc(sizeof(struct performance_threshold_timer));
    performance_threshold_timer_clear(new_timer);
    new_timer->threshold_time = max_time;
    return new_timer;
}

void performance_threshold_timer_begin_timing(struct performance_threshold_timer* timer) {
    assert(is_timespec_cleared(timer->begin_time));
    assert(is_timespec_cleared(timer->end_time));

    set_time_to_now(&timer->begin_time);
}

void performance_threshold_timer_end_timing(struct performance_threshold_timer* timer){
    assert(is_timespec_cleared(timer->end_time));
    assert(!is_timespec_cleared(timer->begin_time));

    set_time_to_now(&timer->end_time);
}


bool performance_threshold_timer_was_threshold_exceeded(struct performance_threshold_timer* timer){
    assert(!is_timespecs_equal(timer->begin_time, get_cleared_timespec()));
    assert(!is_timespecs_equal(timer->end_time, get_cleared_timespec()));
    return is_timespec_greater_than(timespec_get_elapsed_time(timer->begin_time, timer->end_time), timer->threshold_time);
}

struct timespec performance_threshold_timer_get_elapsed_time(struct performance_threshold_timer* timer){
    return timespec_get_elapsed_time(timer->begin_time, timer->end_time);
}

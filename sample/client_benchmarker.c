//
// Created by Michael Davis on 06/11/2020.
//

#include "client_benchmarker.h"
#include <stdlib.h>
#include <string.h>
#include "latency_recorder.h"
#include <khash.h>
#include <stdbool.h>
#include <sys/time.h>
#include <assert.h>A
#include "paxos.h"
#include "stdio.h"


KHASH_MAP_INIT_INT(value_times, struct timeval*)


struct stats {
    long min_latency;
    long max_latency;
    long avg_latency;
    int delivered_count;
    size_t delivered_bytes;
};

struct client_benchmarker {
    struct stats stats;
    khash_t(value_times)* value_and_submission_times;
    struct latency_recorder* latency_recorder;
    uint32_t client_id;
    int num_outstanding;
};


struct client_benchmarker *
client_benchmarker_new(uint32_t client_id, unsigned int num_latencies_to_record, struct timeval settle_in_time,
                       const char *latency_output_path) {
    struct client_benchmarker* c = malloc(sizeof(*c));
    c->value_and_submission_times = kh_init_value_times();
    c->client_id = client_id;
    c->stats = (struct stats) {0};
    c->num_outstanding = 0;
    c->latency_recorder = latency_recorder_new(latency_output_path, settle_in_time, num_latencies_to_record);
    return c;
}


void client_benchmarker_free(struct client_benchmarker** benchmarker){
    struct timeval* value;
    kh_foreach_value((*benchmarker)->value_and_submission_times, value, free(value));
    kh_destroy_value_times((*benchmarker)->value_and_submission_times);

    latency_recorder_free(&(*benchmarker)->latency_recorder);
    free(*benchmarker);
}

bool client_benchmarker_register_value(struct client_benchmarker* benchmarker, const struct client_value* v){
    paxos_log_debug("Entering regestering value");
   // assert(v->client_id == benchmarker->client_id);
    uint32_t uid = client_value_get_uid(v);
    int rv = -1;
    khiter_t key = kh_put_value_times(benchmarker->value_and_submission_times, uid, &rv);
   // assert(rv > 0);
    if (rv > 0) {
        struct timeval *submission_time = malloc(sizeof(*submission_time));
        gettimeofday(submission_time, NULL);
        kh_value(benchmarker->value_and_submission_times, key) = submission_time;
        paxos_log_debug("Leaving registering value");
        benchmarker->num_outstanding++;
        return true;

    } else {
        paxos_log_debug("Leaving regestering value");
        return false;
    }
}

bool client_benchmarker_is_outstanding(struct client_benchmarker* benchmarker, const struct client_value* v){
    if (benchmarker->client_id == v->client_id) {
        uint32_t uid = client_value_get_uid(v);
        khiter_t key = kh_get_value_times(benchmarker->value_and_submission_times, uid);
        if (key != kh_end(benchmarker->value_and_submission_times)) {
            return kh_exist(benchmarker->value_and_submission_times, key) == 1;
        }
    }
    return false;
}

static bool client_benchmarker_get_submission_time(struct client_benchmarker* benchmarker, const struct client_value* v, struct timeval* submission_time){
   // assert(submission_time != NULL);
   // assert(v->client_id == benchmarker->client_id);
    unsigned int uid = client_value_get_uid(v);
    khiter_t key = kh_get_value_times(benchmarker->value_and_submission_times, uid);

    if (benchmarker->client_id == v->client_id && key != kh_end(benchmarker->value_and_submission_times)){
        if (kh_exist(benchmarker->value_and_submission_times, key) == 1) {
            struct timeval time = *kh_value(benchmarker->value_and_submission_times, key);
            *submission_time = time;
            return true;
        }
    }
    *submission_time = (struct timeval) {0, 0};
    return false;
}

// Returns lhs - rhs in microseconds.
static long timeval_diff_in_microseconds(struct timeval* lhs, struct timeval* rhs) {
    struct timeval result;
    timersub(lhs, rhs, &result);
    return result.tv_sec * 1000000 + result.tv_usec;
}

bool client_benchmarker_close_value(struct client_benchmarker* benchmarker, const struct client_value* v){
    khiter_t key = kh_get_value_times(benchmarker->value_and_submission_times, client_value_get_uid(v));
   // assert(key != kh_end(benchmarker->value_and_submission_times) && kh_exist(benchmarker->value_and_submission_times, key) == 1);
    free(kh_value(benchmarker->value_and_submission_times, key));
    kh_del_value_times(benchmarker->value_and_submission_times, key);
    benchmarker->num_outstanding--;
    return true;
}

bool client_benchmarker_close_value_and_update_stats(struct client_benchmarker* benchmarker, const struct client_value* v){
    struct timeval val_submission_time;

    bool found = client_benchmarker_get_submission_time(benchmarker, v, &val_submission_time);
   // assert(found);

    if (found) {
        
        int size = kh_size(benchmarker->value_and_submission_times);
        struct timeval now;
        gettimeofday(&now, NULL);
        long latency_microseconds = timeval_diff_in_microseconds(&now, &val_submission_time);

        benchmarker->stats.delivered_count++;
        benchmarker->stats.delivered_bytes += client_value_get_value_size(v); //TODO figure out if should be total value size or should exclude the metadata
        benchmarker->stats.avg_latency = benchmarker->stats.avg_latency +
                                         ((latency_microseconds - benchmarker->stats.avg_latency) /
                                          benchmarker->stats.delivered_count);

        if (benchmarker->stats.min_latency == 0 || latency_microseconds < benchmarker->stats.min_latency)
            benchmarker->stats.min_latency = latency_microseconds;
        if (latency_microseconds > benchmarker->stats.max_latency)
            benchmarker->stats.max_latency = latency_microseconds;

        latency_recorder_record(benchmarker->latency_recorder, latency_microseconds);
        client_benchmarker_close_value(benchmarker, v);
     //   int size_2 = kh_size(benchmarker->value_and_submission_times);
       // assert(size = size_2 + 1);

    }
    return found;
}

void client_benchmarker_print_and_reset_stats(struct client_benchmarker* benchmarker){
    double mbps = (double)(benchmarker->stats.delivered_bytes * 8) / (1024*1024);
    printf("%d value/sec, %.2f Mbps, latency min %ld us max %ld us avg %ld us\n",
           benchmarker->stats.delivered_count, mbps, benchmarker->stats.min_latency,
           benchmarker->stats.max_latency, benchmarker->stats.avg_latency);
    memset(&benchmarker->stats, 0, sizeof(struct stats));
}


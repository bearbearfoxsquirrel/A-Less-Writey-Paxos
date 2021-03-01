//
// Created by Michael Davis on 06/11/2020.
//

#ifndef A_LESS_WRITEY_PAXOS_CLIENT_BENCHMARKER_H
#define A_LESS_WRITEY_PAXOS_CLIENT_BENCHMARKER_H

#include "client_value.h"
#include <sys/time.h>
#include <stdbool.h>

struct client_benchmarker;

struct client_benchmarker *
client_benchmarker_new(uint32_t client_id, unsigned int num_latencies_to_record, struct timeval settle_in_time,
                       const char *latency_output_path);

void client_benchmarker_free(struct client_benchmarker** benchmarker);

bool client_benchmarker_register_value(struct client_benchmarker* benchmarker, const struct client_value* v);

bool client_benchmarker_is_outstanding(struct client_benchmarker* benchmarker, const struct client_value* v);

bool client_benchmarker_close_value_and_update_stats(struct client_benchmarker* benchmarker, const struct client_value* v);

void client_benchmarker_print_and_reset_stats(struct client_benchmarker* benchmarker);


#endif //A_LESS_WRITEY_PAXOS_CLIENT_BENCHMARKER_H

//
// Created by Michael Davis on 02/04/2020.
//

#ifndef A_LESS_WRITEY_PAXOS_LATENCY_RECORDER_H
#define A_LESS_WRITEY_PAXOS_LATENCY_RECORDER_H

struct latency_recorder;

struct latency_recorder *
latency_recorder_new(const char *output_file_path, struct timeval settle_in_time, u_int64_t latencies_to_record);

void latency_recorder_record(struct latency_recorder* recorder, unsigned long latency);

void latency_recorder_free(struct latency_recorder** recorder);

#endif //A_LESS_WRITEY_PAXOS_LATENCY_RECORDER_H

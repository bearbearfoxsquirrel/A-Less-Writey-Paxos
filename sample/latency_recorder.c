//
// Created by Michael Davis on 02/04/2020.
//

#include <stdbool.h>
#include <sys/time.h>
#include "stdio.h"
#include "stdlib.h"
#include "stdint.h"

#include "latency_recorder.h"

struct latency_recorder{
    FILE* record;
    struct timeval begin_time;
    bool begin_timing;
    struct timeval settle_in_time;
    uint32_t latencies_to_record;
};
struct latency_recorder *
latency_recorder_new(const char *output_file_path, struct timeval settle_in_time, uint32_t latencies_to_record) {
    struct latency_recorder* latency_recorder = malloc(sizeof(struct latency_recorder));
    latency_recorder->record = fopen(output_file_path, "w");
    latency_recorder->settle_in_time = settle_in_time;
    latency_recorder->latencies_to_record = latencies_to_record;
    latency_recorder->begin_timing = false;
    gettimeofday(&latency_recorder->begin_time, NULL);
    return latency_recorder;
}

void latency_recorder_record(struct latency_recorder* recorder, unsigned long latency){
    if (recorder->latencies_to_record > 0) {
        if (!recorder->begin_timing) {
            struct timeval time_now;
            gettimeofday(&time_now, NULL);
            struct timeval elapsed_time;
            timersub(&time_now, &recorder->begin_time, &elapsed_time);
            if (timercmp(&elapsed_time, &recorder->settle_in_time, >=)) {
                recorder->begin_timing = true;
                fprintf(recorder->record, "%lu\n", latency);
                recorder->latencies_to_record--;
            }
        } else {
            fprintf(recorder->record, "%lu\n", latency);
            recorder->latencies_to_record--;
        }
    }
}

void latency_recorder_free(struct latency_recorder** recorder){
    fclose((**recorder).record);
    free((*recorder));
    *recorder = NULL;
}

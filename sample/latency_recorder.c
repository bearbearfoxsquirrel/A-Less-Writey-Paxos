//
// Created by Michael Davis on 02/04/2020.
//

#include "stdio.h"
#include "stdlib.h"
#include "latency_recorder.h"

struct latency_recorder{
    FILE* record;
};

struct latency_recorder * latency_recorder_new(const char* output_file_path){
    struct latency_recorder* latency_recorder = malloc(sizeof(struct latency_recorder));
    latency_recorder->record = fopen(output_file_path, "w");
    return latency_recorder;
}

void latency_recorder_record(struct latency_recorder* recorder, unsigned long latency){
    fprintf(recorder->record, "%lu\n", latency);
}

void latency_recorder_free(struct latency_recorder** recorder){
    fclose((**recorder).record);
    free((*recorder));
    *recorder = NULL;
}

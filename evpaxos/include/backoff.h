//
// Created by Michael Davis on 26/03/2020.
//

#ifndef A_LESS_WRITEY_PAXOS_BACKOFF_H
#define A_LESS_WRITEY_PAXOS_BACKOFF_H

#include <stdbool.h>
#include "stdlib.h"

struct backoff_api {
    unsigned long (*backoff_min) (void* handle);
    unsigned long (*backoff_max) (void* handle);
    unsigned long (*backoff_next) (void* handle, unsigned long previous_backoff_time);
    void (*backoff_free) (void** handle_ptr);

};

struct backoff {
    void* handle;
    struct backoff_api* api;
};




static inline unsigned long backoff_get_min_backoff(struct backoff* backoff){
    return backoff->api->backoff_min(backoff->handle);
}

static inline unsigned long backoff_get_max_backoff(struct backoff* backoff){
    return backoff->api->backoff_max(backoff->handle);
}


static inline unsigned long backoff_next(struct backoff* backoff, unsigned long previous_time){
    return backoff->api->backoff_next(backoff->handle, previous_time);
}

static inline void backoff_free(struct backoff** backoff){
    (*backoff)->api->backoff_free(&(*backoff)->handle);
    free(*backoff);
    *backoff = NULL;
}






#endif //A_LESS_WRITEY_PAXOS_BACKOFF_H

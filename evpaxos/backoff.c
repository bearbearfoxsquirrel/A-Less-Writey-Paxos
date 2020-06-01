//
// Created by Michael Davis on 25/03/2020.
//


#include <random.h>
#include <assert.h>
#include "backoff.h"
#include "backoff_implementations.h"
#include "stdlib.h"



struct exponential_randomised_backoff {
    unsigned long max_backoff;
    unsigned long min_backoff;
    unsigned long max_initial_backoff;
};


struct full_jitter_backoff {
    struct backoff* base_backoff;
};



// EXPONENTIAL RANDOMISED BACKOFF
static unsigned long exponential_randomised_backoff_initial(struct exponential_randomised_backoff* handle) {
    return random_between(handle->min_backoff, handle->max_initial_backoff);
}

static unsigned long exponential_randomised_backoff_next(struct exponential_randomised_backoff* handle, int attempt_number){
    unsigned  int new_time = (exponential_randomised_backoff_initial(handle) << (unsigned int) attempt_number) % handle->max_backoff;
    if (new_time < handle->min_backoff) {
        new_time = exponential_randomised_backoff_initial(handle);
        assert(new_time >= handle->min_backoff && new_time <= handle->max_initial_backoff);
    }
    assert(new_time > 0 && new_time <= handle->max_backoff);
    return new_time;
}

static unsigned long exponential_randomised_backoff_get_max_backoff(struct exponential_randomised_backoff* handle) {
    return handle->max_backoff;
}

static unsigned long exponential_randomised_backoff_get_min_backoff(struct exponential_randomised_backoff* handle) {
    return handle->min_backoff;
}


static void exponential_randomised_backoff_free(struct exponential_randomised_backoff** handle_ptr) {
    free(*handle_ptr);
    *handle_ptr = NULL;
}

static struct exponential_randomised_backoff* exponential_randomised_backoff_new_handle(unsigned long max_backoff, unsigned long min_backoff,
                                                                                        unsigned long max_initial_backoff) {
    struct exponential_randomised_backoff* backoff_new = malloc(sizeof(struct exponential_randomised_backoff));
    *backoff_new = (struct exponential_randomised_backoff) {
            .max_backoff = max_backoff,
            .max_initial_backoff = max_initial_backoff,
            .min_backoff = min_backoff
    };
    return backoff_new;
}

struct backoff_api exponential_randomised_backoff_api[] = {{
                                                                  .backoff_max = (unsigned long (*) (void*)) &exponential_randomised_backoff_get_max_backoff,
                                                                  .backoff_min = (unsigned long (*) (void*))&exponential_randomised_backoff_get_min_backoff,
                                                                   .backoff_next =(unsigned long (*) (void*, unsigned long)) &exponential_randomised_backoff_next,
                                                                  .backoff_free =  (void (*) (void**)) &exponential_randomised_backoff_free

                                                           }};





struct backoff* exponential_randomised_backoff_new(unsigned long max_backoff, unsigned long min_backoff, unsigned long max_initial_backoff) {
    struct backoff* new_backoff = malloc(sizeof(struct backoff));
    new_backoff->handle = exponential_randomised_backoff_new_handle(max_backoff, min_backoff, max_initial_backoff);
    new_backoff->api = exponential_randomised_backoff_api;
    return new_backoff;
}






// FULL JITTER BACKOFF
static unsigned long full_jitter_backoff_get_min_backoff(struct full_jitter_backoff* handle) {
    return backoff_get_min_backoff(handle->base_backoff);
}

static unsigned long full_jitter_backoff_next(struct full_jitter_backoff* handle, int attempt_number) {
    return random_between(full_jitter_backoff_get_min_backoff(handle), backoff_next(handle->base_backoff,
                                                                                    attempt_number));
}

static unsigned long full_jitter_backoff_get_max_backoff(struct full_jitter_backoff* handle) {
    return backoff_get_max_backoff(handle->base_backoff);
}


static void full_jitter_backoff_free(struct full_jitter_backoff** handle_ptr) {
    backoff_free(&(*handle_ptr)->base_backoff);
    assert(&(*handle_ptr)->base_backoff == NULL);
    free(*handle_ptr);
    *handle_ptr = NULL;
}

static struct full_jitter_backoff* full_jitter_backoff_new_handle(unsigned long max_backoff, unsigned long min_backoff, unsigned long max_initial_backoff){
    struct full_jitter_backoff* new_handle = malloc(sizeof(struct full_jitter_backoff));
    new_handle->base_backoff = exponential_randomised_backoff_new(max_backoff, min_backoff, max_initial_backoff);
    return new_handle;
}

struct backoff_api full_jitter_backoff_api []= {{
                                                        .backoff_max = (unsigned long (*)(void*)) full_jitter_backoff_get_max_backoff,
                                                        .backoff_min = (unsigned long (*) (void*)) full_jitter_backoff_get_min_backoff,
                                                        .backoff_next = (unsigned long (*) (void*, unsigned long)) full_jitter_backoff_next,
                                                        .backoff_free = (void (*) (void**)) full_jitter_backoff_free
                                                }
};

struct backoff* full_jitter_backoff_new(unsigned long max_backoff, unsigned long min_backoff, unsigned long max_initial_backoff) {
    struct backoff* new_backoff = malloc(sizeof(struct backoff));

  *new_backoff = (struct backoff) {.handle = full_jitter_backoff_new_handle(max_backoff, min_backoff, max_initial_backoff),
          .api = full_jitter_backoff_api};
    return new_backoff;
}

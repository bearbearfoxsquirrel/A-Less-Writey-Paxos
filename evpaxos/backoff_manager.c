//
// Created by Michael Davis on 26/03/2020.
//




#include <paxos.h>
#include "backoff.h"
#include <assert.h>
#include "khash.h"
KHASH_MAP_INIT_INT(backingoff_instances, struct timeval*)

struct backoff_manager {
    khash_t(backingoff_instances)* backingoff_instances;
    struct backoff* backoff_mechanism;
};

struct backoff_manager* backoff_manager_new(struct backoff* backoff){
    struct backoff_manager* new_manager = malloc(sizeof(struct backoff_manager));
    new_manager->backingoff_instances = kh_init_backingoff_instances();
    assert(backoff != NULL);
    new_manager->backoff_mechanism = backoff;
    return new_manager;
}

void backoff_manager_free(struct backoff_manager **manager){
    struct timeval* timeval;
    kh_foreach_value((*manager)->backingoff_instances, timeval, free(timeval));
    backoff_free(&(*manager)->backoff_mechanism);
    *manager = NULL;
}

khiter_t kh_manager_put_new_instance_to_backing_off_instances_and_return_key(struct backoff_manager* manager, iid_t new_instance){
    int rv = -1;
    khiter_t key = kh_put_backingoff_instances(manager->backingoff_instances, new_instance, &rv);
    assert(rv > 0);
    struct timeval* new_backoff = malloc(sizeof(*new_backoff));
    *new_backoff = (struct timeval) {.tv_sec = 0, .tv_usec = 0};
    kh_value(manager->backingoff_instances, key) = new_backoff;
    return key;
}


const struct timeval* backoff_manager_get_backoff(struct backoff_manager* manager, iid_t instance_to_backoff){
    khiter_t key = kh_get_backingoff_instances(manager->backingoff_instances, instance_to_backoff);


    if (key == kh_end(manager->backingoff_instances)) {
        // create new
        key = kh_manager_put_new_instance_to_backing_off_instances_and_return_key(manager, instance_to_backoff);
        //kh_value(manager->backingoff_instances, key);
    } else if (kh_exist(manager->backingoff_instances, key) != 1) {
        // create new
        key = kh_manager_put_new_instance_to_backing_off_instances_and_return_key(manager, instance_to_backoff);
    }


    struct timeval* current_backoff = kh_value(manager->backingoff_instances, key);
    current_backoff->tv_usec = backoff_next(manager->backoff_mechanism, current_backoff->tv_usec);
    return current_backoff;
}

//void backoff_manager_begin_backoff(struct backoff_manager* manager, iid_t instance_to_backoff);

void backoff_manager_close_backoff_if_exists(struct backoff_manager* manager, iid_t instance_to_close){
    khiter_t key = kh_get_backingoff_instances(manager->backingoff_instances, instance_to_close);

    if (key != kh_end(manager->backingoff_instances)) {
        if (kh_exist(manager->backingoff_instances, key) == 1) {
            struct timeval *timeval = kh_value(manager->backingoff_instances, key);
            kh_del_backingoff_instances(manager->backingoff_instances, key);
            free(timeval);
        }
    }
}



static void kh_backoff_free_if_less_than_or_equal(struct backoff_manager* manager, khiter_t key, iid_t cmp){
    if (kh_exist(manager->backingoff_instances, key)) {
        if (kh_key(manager->backingoff_instances, key) <= cmp){
            backoff_manager_close_backoff_if_exists(manager, kh_key(manager->backingoff_instances, key));
        }
    }
}

void backoff_manager_close_less_than_or_equal(struct backoff_manager* manager, iid_t backoff_from){
    khiter_t key;
    struct timeval *timeval;
    kh_foreach(manager->backingoff_instances, key, timeval, kh_backoff_free_if_less_than_or_equal(manager, key, backoff_from));
}


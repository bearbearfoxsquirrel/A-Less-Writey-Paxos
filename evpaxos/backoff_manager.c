//
// Created by Michael Davis on 26/03/2020.
//




#include <paxos.h>
#include "backoff.h"
#include <assert.h>
#include "khash.h"

struct managed_backoff {
    struct timeval current_backoff;
    int attempt;
};

KHASH_MAP_INIT_INT(backingoff_instances, struct managed_backoff*)

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
    struct managed_backoff* managed_backoff;
    kh_foreach_value((*manager)->backingoff_instances, managed_backoff, free(managed_backoff));
    backoff_free(&(*manager)->backoff_mechanism);
    *manager = NULL;
}

khiter_t kh_manager_put_new_instance_to_backing_off_instances_and_return_key(struct backoff_manager* manager, iid_t new_instance){
    int rv = -1;
    khiter_t key = kh_put_backingoff_instances(manager->backingoff_instances, new_instance, &rv);
    assert(rv > 0);
    struct managed_backoff* new_backoff = malloc(sizeof(*new_backoff));
    *new_backoff = (struct managed_backoff) {
        .current_backoff = (struct timeval) {.tv_sec = 0, .tv_usec = 0},
                .attempt = 1//?
    };
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


    struct managed_backoff* current_backoff = kh_value(manager->backingoff_instances, key);
    current_backoff->current_backoff.tv_usec = backoff_next(manager->backoff_mechanism,
                                                            current_backoff->attempt);
    current_backoff->attempt++;
    return &current_backoff->current_backoff;
}

//void backoff_manager_begin_backoff(struct backoff_manager* manager, iid_t instance_to_backoff);

void backoff_manager_close_backoff_if_exists(struct backoff_manager* manager, iid_t instance_to_close){
    khiter_t key = kh_get_backingoff_instances(manager->backingoff_instances, instance_to_close);

    if (key != kh_end(manager->backingoff_instances)) {
        if (kh_exist(manager->backingoff_instances, key) == 1) {
            struct managed_backoff *instance_backoff = kh_value(manager->backingoff_instances, key);
            kh_del_backingoff_instances(manager->backingoff_instances, key);
            free(instance_backoff);
        }
    }
}



static void kh_backoff_free_if_less_than_or_equal(struct backoff_manager* manager, iid_t key, iid_t cmp){
  //  if (kh_exist(manager->backingoff_instances, key) == 1) {

    //    iid_t act_key = kh_key(manager->backingoff_instances, key);
        if (key <= cmp){
            backoff_manager_close_backoff_if_exists(manager, key);
        }
  //  }
}

void backoff_manager_close_less_than_or_equal(struct backoff_manager* manager, iid_t backoff_from){
    iid_t key;
    struct managed_backoff *current_instance;
    kh_foreach(manager->backingoff_instances, key, current_instance, kh_backoff_free_if_less_than_or_equal(manager, key, backoff_from));
}


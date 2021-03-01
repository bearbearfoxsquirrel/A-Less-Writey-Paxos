//
// Created by Michael Davis on 17/03/2020.
//

#include <assert.h>
#include <paxos.h>
#include "stdlib.h"
#include "pending_client_values.h"
#include "string.h"
#include "khash.h"

KHASH_MAP_INIT_INT(pending_instances, struct pending_value*)

struct pending_value {
    struct paxos_value client_value_pending;
};
struct pending_client_values {
    khash_t(pending_instances)* pending_instances;
};



//unsigned int array_list_number_of_pending_values(struct pending_client_values* array_list){
//    return array_list->pending_instances->size;
//}




bool value_exists(struct pending_client_values* array_list, khiter_t key) {
    if (key != kh_end(array_list->pending_instances)) {
        if (kh_exist(array_list->pending_instances, key)) {
            return true;
        }
    }
    return false;
}


bool get_value_pending_at(struct pending_client_values* array_list, iid_t i, struct paxos_value* pending_value_copy) {
    khiter_t key = kh_get_pending_instances(array_list->pending_instances, i);
    if (value_exists(array_list, key)) {
        struct pending_value *pending_value_in_instance = kh_value(array_list->pending_instances, key);
        if (pending_value_copy != NULL) {

            paxos_value_copy(pending_value_copy, &pending_value_in_instance->client_value_pending);
        }
        return true;
    } else {
        return false;
    }
}



struct pending_client_values* pending_client_values_new() {
    struct pending_client_values* a = malloc(sizeof(struct pending_client_values));
    a->pending_instances = kh_init_pending_instances();//array = malloc(sizeof(struct paxos_value) * initial_size);
    return a;
}


void client_value_now_pending_at(struct pending_client_values* array_list, iid_t instance, const struct paxos_value* copy_of_val_now_pending){
    int rv;
    khiter_t key = kh_put_pending_instances(array_list->pending_instances, instance, &rv);
   // assert(rv > 0);

    struct pending_value* value = malloc(sizeof(struct pending_value));
    copy_value(copy_of_val_now_pending, &value->client_value_pending);
    kh_value(array_list->pending_instances, key) = value;
}


bool remove_pending_value_at(struct pending_client_values* array_list, unsigned int index, struct paxos_value* pending_value_copy){
        khiter_t key = kh_get_pending_instances(array_list->pending_instances, index);
        if (value_exists(array_list, key)) {
            struct pending_value* pending_value_in_inst = kh_value(array_list->pending_instances, key);
            if (pending_value_copy != NULL) {
                paxos_value_copy(pending_value_copy, &pending_value_in_inst->client_value_pending);
            }
            kh_del_pending_instances(array_list->pending_instances, key);
            free(pending_value_in_inst->client_value_pending.paxos_value_val);
            free(pending_value_in_inst);
            return true;
        }
    return false;
}


int get_and_close_pending_value_and_its_instances_if_open(struct pending_client_values *array_list, struct paxos_value *value,
                                                          iid_t **instances_with_values_closed) {


    iid_t key;

    int num_instances_vals_closed = 0;

    paxos_log_debug("Closing all instances with same value proposed:");



    for (key = kh_begin(hash_table); key != kh_end(array_list->pending_instances); ++key) {
        if (!kh_exist(array_list->pending_instances, key)) {
            continue;
        } else {
            struct pending_value* pending_val = kh_value(array_list->pending_instances, key);
            if (is_values_equal(pending_val->client_value_pending, *value)){
                paxos_log_debug("Closing instance %u", key);

                *instances_with_values_closed = realloc(*instances_with_values_closed, num_instances_vals_closed);
                (*instances_with_values_closed)[num_instances_vals_closed - 1] = key;

                kh_del_pending_instances(array_list->pending_instances, key);
                free(pending_val->client_value_pending.paxos_value_val);
                free(pending_val);
                num_instances_vals_closed++;
//                closed_any = true;
            }
        }
    }

    return num_instances_vals_closed;


    //kh_foreach(array_list->pending_instances, key, val, close_instance_if_open(array_list, key, val, value, &closed_any))

}


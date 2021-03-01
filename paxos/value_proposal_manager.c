//
// Created by Michael Davis on 16/11/2020.
//

#include "value_proposal_manager.h"

#include <carray.h>
#include <stdlib.h>
#include <assert.h>


struct value_proposal_manager {

    struct carray *client_values_to_propose;
    struct carray *values_proposed;
    bool parallel_proposals;
//    struct carray* instances_with_client_vals_closed;


};

struct value_proposal_manager *value_proposal_manager_new(int initial_queue_len, bool repropose_values) {
    struct value_proposal_manager* manager = malloc(sizeof(*manager));
    *manager = (struct value_proposal_manager) {
        .client_values_to_propose = carray_new(initial_queue_len),
        .values_proposed = carray_new(initial_queue_len ),
        .parallel_proposals = repropose_values
//        .instances_with_client_vals_closed = carray_new(initial_queue_len / 2)
    };
    return manager;
}

void value_proposal_manager_free(struct value_proposal_manager** manager) {
    carray_free((*manager)->client_values_to_propose);
    carray_free((*manager)->values_proposed);
   // carray_free((*manager)->instances_with_client_vals_closed);
    free(*manager);
}

void value_proposal_manager_enqueue(struct value_proposal_manager* manager, struct paxos_value** enqueuing_value_ptr) {

  //  assert(!is_values_equal(NOP, **enqueuing_value_ptr));
    carray_push_back(manager->client_values_to_propose, *enqueuing_value_ptr);
}

bool value_proposal_manger_get_next(struct value_proposal_manager *manager, struct paxos_value **value_ret, bool is_holes) {

    if (!carray_empty(manager->client_values_to_propose)) {
        paxos_log_debug("Proposing client value");
        struct paxos_value* value_to_propose = carray_pop_front(manager->client_values_to_propose);
       // assert(value_to_propose != NULL);
       carray_push_back(manager->values_proposed, value_to_propose);
  //     assert(!is_values_equal(NOP, *value_to_propose));
       paxos_value_copy(*value_ret, value_to_propose);
        return true;
    } else {
        if (!carray_empty(manager->values_proposed) && manager->parallel_proposals) {
            paxos_log_debug("Reproposing client value");
            struct paxos_value *value_to_propose = carray_pop_front(manager->values_proposed);
           // assert(value_to_propose != NULL);
            carray_push_back(manager->values_proposed, value_to_propose);
            paxos_value_copy(*value_ret, value_to_propose);
            return true;
        } else {
                if (is_holes) {
                // Fill holes
                *value_ret = NOP_NEW;
                paxos_log_debug("Sending NOP to fill holes");
                    return true;
            } else {
                paxos_log_debug("No need to propose a Value");
                return false;}
        }
    }
   // return true ;
}

static bool is_in_proposal_queue(struct value_proposal_manager* manager, struct paxos_value* value_ptr) {
    struct carray* tmp = carray_new(carray_size(manager->client_values_to_propose));
//    paxos_log_debug("Checking if there are any values in the initial client value proposal queue to remove...");
    //   paxos_log_debug("Initially there are %u elements", carray_num_elements(manager->client_values_to_propose));
    bool found = false;
    while(!carray_empty(manager->client_values_to_propose)){
        struct paxos_value* cur_val = carray_pop_front(manager->client_values_to_propose);
        if (is_paxos_values_equal(cur_val, value_ptr)) {
            found = true;
        } else {
            carray_push_front(tmp, cur_val);
        }
    }

    while (!carray_empty(tmp)) {
        carray_push_front(manager->client_values_to_propose, carray_pop_front(tmp));
    }

    // paxos_log_debug("Finally there are %u elements", carray_num_elements(manager->client_values_to_propose));
    carray_free(tmp);
    return found;
}

static bool is_in_retry_queue(struct value_proposal_manager* manager, struct paxos_value* value_ptr) {
    struct carray* tmp = carray_new(carray_size(manager->values_proposed));
    //   paxos_log_debug("Checking if there are any values in the retry queue to remove...");
    paxos_log_debug("Initially there are %u elements", carray_num_elements(manager->values_proposed));
    while(!carray_empty(manager->values_proposed)){
        struct paxos_value* cur_val = carray_pop_front(manager->values_proposed);
        if (is_paxos_values_equal(cur_val, value_ptr)) {
            return true;
        } else {
            carray_push_front(tmp, cur_val);
        }
    }

    while (!carray_empty(tmp)) {
        carray_push_front(manager->values_proposed, carray_pop_front(tmp));
    }
    carray_free(tmp);
    return false;
}

bool value_proposal_manager_is_outstanding(struct value_proposal_manager* manager, struct paxos_value *value_ptr) {
    return is_in_retry_queue(manager, value_ptr) || is_in_proposal_queue(manager, value_ptr);
}


static bool remove_value_from_proposing_queue(struct value_proposal_manager* manager, struct paxos_value* v, struct paxos_value** ret) {
    struct carray* tmp = carray_new(carray_size(manager->client_values_to_propose));
//    paxos_log_debug("Checking if there are any values in the initial client value proposal queue to remove...");
 //   paxos_log_debug("Initially there are %u elements", carray_num_elements(manager->client_values_to_propose));
    bool found = false;
    while(!carray_empty(manager->client_values_to_propose)){
        struct paxos_value* cur_val = carray_pop_front(manager->client_values_to_propose);
     //   assert(cur_val != NULL);
      //  assert(cur_val->paxos_value_len > 0);
        if (is_paxos_values_equal(cur_val, v)) {
            paxos_log_debug("Removed value from Client Values to Propose");
            if (found == true ) {
                paxos_log_debug("Duplicate value found in queue");
                paxos_value_free(&cur_val);
            } else {
                *ret = cur_val;
                found = true;
            }
        } else {
            carray_push_front(tmp, cur_val);
        }
    }

    while (!carray_empty(tmp)) {
        carray_push_front(manager->client_values_to_propose, carray_pop_front(tmp));
    }

   // paxos_log_debug("Finally there are %u elements", carray_num_elements(manager->client_values_to_propose));
    carray_free(tmp);
    return found;
}


static bool remove_value_from_retry_queue(struct value_proposal_manager* manager, struct paxos_value* v, struct paxos_value** ret) {
    struct carray* tmp = carray_new(carray_size(manager->values_proposed));
 //   paxos_log_debug("Checking if there are any values in the retry queue to remove...");
    paxos_log_debug("Initially there are %u elements", carray_num_elements(manager->values_proposed));
    bool found = false;
    while(!carray_empty(manager->values_proposed)){
        struct paxos_value* cur_val = carray_pop_front(manager->values_proposed);
     //   assert(cur_val != NULL);
     //   assert(cur_val->paxos_value_len > 0);
       // assert(cur_val->paxos_value_len > 1);
        if (is_paxos_values_equal(cur_val, v)) {
            paxos_log_debug("Removed value from Values to Repropose");
            if (found == true ) {
                paxos_log_debug("Duplicate value found in queue");
                paxos_value_free(&cur_val);
            } else {
                *ret = cur_val;
                found = true;
            }
        } else {
            carray_push_front(tmp, cur_val);
        }
    }

    while (!carray_empty(tmp)) {
        carray_push_front(manager->values_proposed, carray_pop_front(tmp));
    }
  //  paxos_log_debug("Finally there are %u elements", carray_num_elements(manager->values_proposed));
    carray_free(tmp);
    return found;
}


bool value_proposal_manager_close_if_outstanding(struct value_proposal_manager* manager, struct paxos_value *value) {
    struct paxos_value* value_from_proposal_queue;
    struct paxos_value* value_form_reproposal_queue;


    bool from_proposal_queue = remove_value_from_proposing_queue(manager, value, &value_from_proposal_queue);
    bool from_reproposal_queue = remove_value_from_retry_queue(manager, value, &value_form_reproposal_queue);

    if(from_proposal_queue)
        paxos_value_free(&value_from_proposal_queue);

    if (from_reproposal_queue)
        paxos_value_free(&value_form_reproposal_queue);


    return from_reproposal_queue || from_proposal_queue;
}

bool value_proposal_manager_check_and_requeue_value(struct value_proposal_manager* manager, struct paxos_value *value) {
    struct paxos_value* value_from_proposing_queue ;//= malloc(sizeof(struct paxos_value*));
    struct paxos_value* value_from_reproposing_queue ;//= malloc(sizeof(struct paxos_value*));

    bool from_proposing_queue = remove_value_from_proposing_queue(manager, value, &value_from_proposing_queue);
    bool from_reproposing_queue = from_reproposing_queue = remove_value_from_retry_queue(manager, value, &value_from_reproposing_queue);

    if (from_reproposing_queue)
        carray_push_front(manager->client_values_to_propose, value_from_reproposing_queue);

    if (from_proposing_queue)
        carray_push_front(manager->client_values_to_propose, value_from_proposing_queue);

    if (from_proposing_queue || from_reproposing_queue) {
        return true;
    } else {
        return false;
    }

}



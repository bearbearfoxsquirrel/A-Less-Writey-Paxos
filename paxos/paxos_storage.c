//
// Created by Michael Davis on 13/11/2019.
//

#include <paxos_storage.h>
#include <hash_mapped_memory.h>
#include "paxos_storage.h"

int get_min_unchosen_instance(struct paxos_storage* paxos_storage, iid_t* min_unchosen_instance){
    return paxos_storage->api.get_min_unchosen_instance(paxos_storage->handle, min_unchosen_instance);
}

int is_instance_chosen(const struct paxos_storage* paxos_storage, iid_t instance, bool* is_chosen){
    return paxos_storage->api.is_instance_chosen(paxos_storage->handle, instance, is_chosen);
}

int set_instance_chosen(const struct paxos_storage* paxos_storage, iid_t instance){
    return paxos_storage->api.set_instance_chosen(paxos_storage->handle, instance);
}


int get_max_inited_instance(const struct paxos_storage* paxos_storage, iid_t* returned_max_inited_instance){
    return paxos_storage->api.get_max_inited_instance(paxos_storage->handle, returned_max_inited_instance);
}

int store_last_prepare(struct paxos_storage *paxos_storage, const struct paxos_prepare *last_prepare) {
    return paxos_storage->api.store_last_promise(paxos_storage->handle, last_prepare);
}

int get_last_promise(struct paxos_storage *paxos_storage, iid_t instance_id, paxos_prepare *last_promise_retrieved) {
    return paxos_storage->api.get_last_promise(paxos_storage->handle, instance_id, last_promise_retrieved);
}

int store_last_promises(struct paxos_storage *paxos_storage, paxos_prepare **last_promises, int number_of_instances) {
    return paxos_storage->api.store_last_promises(paxos_storage->handle, last_promises, number_of_instances);
}

int get_last_promises(struct paxos_storage *paxos_storage, iid_t *instance_ids, int number_of_instances_to_retrieve,
                      paxos_prepare **last_promises_retrieved, int *number_of_instances_retrieved) {
    return paxos_storage->api.get_last_promises(paxos_storage->handle, instance_ids, number_of_instances_to_retrieve,
                                                last_promises_retrieved, number_of_instances_retrieved);
}

int store_acceptance(struct paxos_storage *paxos_storage, paxos_accept *accepted_ballot) {
    return paxos_storage->api.store_last_accepted(paxos_storage->handle, accepted_ballot);
}

int get_last_accept(struct paxos_storage *volatile_storage, iid_t instance_id, paxos_accept *last_accepted_retrieved) {
    return volatile_storage->api.get_last_accepted(volatile_storage->handle, instance_id, last_accepted_retrieved);
}

int store_acceptances(struct paxos_storage *paxos_storage, paxos_accept **last_accepteds, int number_of_instances) {
    return paxos_storage->api.store_last_accepteds(paxos_storage->handle, last_accepteds, number_of_instances);
}

int get_last_accepteds(struct paxos_storage *paxos_storage, iid_t *instance_ids, int number_of_instances_to_retrieve,
                       paxos_accept **last_accepteds_retrieved) {
    return paxos_storage->api.get_last_accepteds(paxos_storage->handle, instance_ids, number_of_instances_to_retrieve,
                                                 last_accepteds_retrieved);
}

int store_trim_instance(struct paxos_storage *paxos_storage, iid_t trim_instance_id) {
    return paxos_storage->api.store_trim_instance(paxos_storage->handle, trim_instance_id);
}

int get_trim_instance(struct paxos_storage *paxos_storage, iid_t *trim_instance_id_retrieved) {
    return paxos_storage->api.get_trim_instance(paxos_storage->handle, trim_instance_id_retrieved);
}

int trim_instances_less_than(struct paxos_storage* paxos_storage, iid_t cmp) {
    return paxos_storage->api.trim_instances_less_than(paxos_storage->handle, cmp);
}


int
get_instance_info(struct paxos_storage *paxos_storage, iid_t instance_id, paxos_accepted *instance_info_retrieved) {
    return paxos_storage->api.get_instance_info(paxos_storage->handle, instance_id, instance_info_retrieved);
}

int
store_instance_info(struct paxos_storage *paxos_storage, const struct paxos_accepted* instance_info_to_store) {
    return paxos_storage->api.store_instance_info(paxos_storage->handle, instance_info_to_store);
}

int
get_all_untrimmed_instances_info(struct paxos_storage *paxos_storage, paxos_accepted **retrieved_instances_info,
                                 int *number_of_instances_retrieved) {
    return paxos_storage->api.get_all_untrimmed_instances_info(paxos_storage->handle, retrieved_instances_info,
                                                               number_of_instances_retrieved);
}

/*
void init_paxos_storage_from_instances_info(struct paxos_storage* paxos_storage, struct paxos_accepted* instances_info, iid_t trim_id){

}*/


void paxos_storage_init(struct paxos_storage* paxos_storage, int aid){
    init_hash_mapped_memory(paxos_storage, aid);
}

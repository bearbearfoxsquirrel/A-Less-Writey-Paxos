//
// Created by Michael Davis on 13/04/2020.
//

#ifndef A_LESS_WRITEY_PAXOS_OPTIMSED_LESS_WRITEY_BALLOT_STABLE_STORAGE_H
#define A_LESS_WRITEY_PAXOS_OPTIMSED_LESS_WRITEY_BALLOT_STABLE_STORAGE_H


#include "paxos.h"
#include "standard_stable_storage.h"

struct optimised_less_writey_ballot_stable_storage {
   // void* extended_handle;
    struct {
        int (*get_last_stable_promise) (void* handle, struct ballot* last_promised_ballot);
        int (*store_last_stable_promise) (void* handle, struct ballot last_promised_ballot);

    } extended_api ;
    struct standard_stable_storage standard_storage;
};




int optimised_less_writey_ballot_stable_storage_open(struct optimised_less_writey_ballot_stable_storage *stable_storage);

void optimised_less_writey_ballot_stable_storage_close(struct optimised_less_writey_ballot_stable_storage *store);

int optimised_less_writey_ballot_stable_storage_tx_begin(struct optimised_less_writey_ballot_stable_storage *store);

int optimised_less_writey_ballot_stable_storage_tx_commit(struct optimised_less_writey_ballot_stable_storage *store);

void optimised_less_writey_ballot_stable_storage_tx_abort(struct optimised_less_writey_ballot_stable_storage *store);

int optimised_less_writey_ballot_stable_storage_store_trim_instance(struct optimised_less_writey_ballot_stable_storage *stable_storage, const iid_t iid);

int optimised_less_writey_ballot_stable_storage_get_trim_instance(struct optimised_less_writey_ballot_stable_storage *store, iid_t *instance_id);

int optimised_less_writey_ballot_stable_storage_get_instance_info(struct optimised_less_writey_ballot_stable_storage *store, const iid_t instance_id, paxos_accepted *instance_info_retrieved);

int optimised_less_writey_ballot_stable_storage_store_instance_info(struct optimised_less_writey_ballot_stable_storage *store, const struct paxos_accepted *instance_info);

int optimised_less_writey_ballot_stable_storage_get_all_untrimmed_instances_info(struct optimised_less_writey_ballot_stable_storage *store, struct paxos_accepted **retrieved_instances_info,
                                             int *number_of_instances_retrieved);

int optimised_less_writey_ballot_stable_storage_get_max_inited_instance(struct optimised_less_writey_ballot_stable_storage *storage, iid_t *retrieved_instance);

int optimised_less_writey_ballot_stable_storage_get_last_stable_promise(struct optimised_less_writey_ballot_stable_storage *storage, struct ballot* last_promised_ballot);

int optimised_less_writey_ballot_stable_storage_store_last_stable_promise(struct optimised_less_writey_ballot_stable_storage *storage, struct ballot last_promised_ballot);

void optimised_less_writey_ballot_stable_storage_lmdb_init(struct optimised_less_writey_ballot_stable_storage* storage, int acceptor_id);

void optimised_less_writey_ballot_stable_storage_init(struct optimised_less_writey_ballot_stable_storage *s, int acceptor_id);

#endif //A_LESS_WRITEY_PAXOS_OPTIMSED_LESS_WRITEY_BALLOT_STABLE_STORAGE_H

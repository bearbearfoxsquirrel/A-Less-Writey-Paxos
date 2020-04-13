//
// Created by Michael Davis on 13/04/2020.
//

#include <paxos.h>
#include <standard_stable_storage.h>
#include <optimsed_less_writey_ballot_stable_storage.h>
#include "paxos_types.h"
#include "optimsed_less_writey_ballot_stable_storage.h"


int optimised_less_writey_ballot_stable_storage_open(struct optimised_less_writey_ballot_stable_storage *stable_storage){
    return stable_storage->standard_storage.api.open(stable_storage->standard_storage.handle);
}

void optimised_less_writey_ballot_stable_storage_close(struct optimised_less_writey_ballot_stable_storage *store){
    store->standard_storage.api.close(store->standard_storage.handle);
}

int optimised_less_writey_ballot_stable_storage_tx_begin(struct optimised_less_writey_ballot_stable_storage *store){
    return store->standard_storage.api.tx_begin(store->standard_storage.handle);
}

int optimised_less_writey_ballot_stable_storage_tx_commit(struct optimised_less_writey_ballot_stable_storage *store){
    return store->standard_storage.api.tx_commit(store->standard_storage.handle);
}

void optimised_less_writey_ballot_stable_storage_tx_abort(struct optimised_less_writey_ballot_stable_storage *store){
     store->standard_storage.api.tx_abort(store->standard_storage.handle);
}

int optimised_less_writey_ballot_stable_storage_store_trim_instance(struct optimised_less_writey_ballot_stable_storage *stable_storage, const iid_t iid){
    return stable_storage->standard_storage.api.store_trim_instance(stable_storage->standard_storage.handle, iid);
}

int optimised_less_writey_ballot_stable_storage_get_trim_instance(struct optimised_less_writey_ballot_stable_storage *store, iid_t *instance_id){
    return store->standard_storage.api.get_trim_instance(store->standard_storage.handle, instance_id);
}

int optimised_less_writey_ballot_stable_storage_get_instance_info(struct optimised_less_writey_ballot_stable_storage *store, const iid_t instance_id, paxos_accepted *instance_info_retrieved){
    return store->standard_storage.api.get_instance_info(store->standard_storage.handle, instance_id, instance_info_retrieved);
}

int optimised_less_writey_ballot_stable_storage_store_instance_info(struct optimised_less_writey_ballot_stable_storage *store, const struct paxos_accepted *instance_info){
    return store->standard_storage.api.store_instance_info(store->standard_storage.handle, instance_info);
}

int optimised_less_writey_ballot_stable_storage_get_all_untrimmed_instances_info(struct optimised_less_writey_ballot_stable_storage *store, struct paxos_accepted **retrieved_instances_info,
                                                                                 int *number_of_instances_retrieved){
    return store->standard_storage.api.get_all_untrimmed_instances_info(store->standard_storage.handle, retrieved_instances_info, number_of_instances_retrieved);
}

int optimised_less_writey_ballot_stable_storage_get_max_inited_instance(struct optimised_less_writey_ballot_stable_storage *storage, iid_t *retrieved_instance){
    return storage->standard_storage.api.get_max_instance(storage->standard_storage.handle, retrieved_instance);
}

int optimised_less_writey_ballot_stable_storage_get_last_stable_promise(struct optimised_less_writey_ballot_stable_storage *storage, struct ballot* last_promised_ballot){
    return storage->extended_api.get_last_stable_promise(storage->standard_storage.handle, last_promised_ballot);
}

int optimised_less_writey_ballot_stable_storage_store_last_stable_promise(struct optimised_less_writey_ballot_stable_storage *storage, struct ballot last_promised_ballot){
    return storage->extended_api.store_last_stable_promise(storage->standard_storage.handle, last_promised_ballot);
}

void optimised_less_writey_ballot_stable_storage_init(struct optimised_less_writey_ballot_stable_storage *s, int acceptor_id){
    optimised_less_writey_ballot_stable_storage_lmdb_init(s, acceptor_id);
}

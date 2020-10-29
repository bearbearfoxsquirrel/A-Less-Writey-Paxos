//
// Created by Michael Davis on 03/02/2020.
//

#include <stdint.h>
#include <assert.h>
#include <epoch_stable_storage.h>
#include <standard_stable_storage.h>
#include <paxos_message_conversion.h>
#include <stddef.h>
#include <stdlib.h>

void epoch_stable_storage_init(struct epoch_stable_storage* storage, int acceptor_id){
    epoch_stable_storage_lmdb_init(storage, acceptor_id);
}


int epoch_stable_storage_get_current_epoch(struct epoch_stable_storage* storage, uint32_t* retrieved_epoch){
    return storage->extended_api.get_current_epoch(storage->extended_handle, retrieved_epoch);
}

int epoch_stable_storage_store_epoch(struct epoch_stable_storage* storage, const uint32_t epoch){
    return storage->extended_api.store_current_epoch(storage->extended_handle, epoch);
}

// Wrappers for parent object functions
int epoch_stable_storage_open(struct epoch_stable_storage *stable_storage){
    return stable_storage->standard_storage.api.open(stable_storage->standard_storage.handle);
}

int epoch_stable_storage_trim_instances_less_than(struct epoch_stable_storage * stable_storage, const iid_t cmp){
    return stable_storage->extended_api.trim_instances_less_than(stable_storage->standard_storage.handle, cmp);
}

void epoch_stable_storage_close(struct epoch_stable_storage *store){
    store->standard_storage.api.close(store->standard_storage.handle);
}

int epoch_stable_storage_tx_begin(struct epoch_stable_storage *store){
    return store->standard_storage.api.tx_begin(store->standard_storage.handle);
}

int epoch_stable_storage_tx_commit(struct epoch_stable_storage *store){
    return store->standard_storage.api.tx_commit(store->standard_storage.handle);
}

void epoch_stable_storage_tx_abort(struct epoch_stable_storage *store){
    store->standard_storage.api.tx_abort(store->standard_storage.handle);
}

int epoch_stable_storage_store_trim_instance(struct epoch_stable_storage *stable_storage, const iid_t iid){
    return stable_storage->standard_storage.api.store_trim_instance(stable_storage->standard_storage.handle, iid);
}

int epoch_stable_storage_get_trim_instance(struct epoch_stable_storage *store, iid_t * instance_id){
    return store->standard_storage.api.get_trim_instance(store->standard_storage.handle, instance_id);
}

int epoch_stable_storage_get_epoch_ballot_accept(struct epoch_stable_storage *store, const iid_t instance_id, struct epoch_ballot_accept *retrieved_epoch_ballot){
    return store->extended_api.get_epoch_ballot_accept(store->extended_handle, instance_id, retrieved_epoch_ballot);
}

int epoch_stable_storage_store_epoch_ballot_accept(struct epoch_stable_storage *store, struct epoch_ballot_accept* epoch_ballot_accept){
    return store->extended_api.store_epoch_ballot_accept(store->extended_handle, epoch_ballot_accept);
}

// add the store method too
/*
int epoch_stable_storage_get_all_untrimmed_instances_info(struct epoch_stable_storage *store, struct paxos_accepted **retrieved_instances_info,
                                                          int *number_of_instances_retrieved){
    // get all the instances
    // then for each instance, just get its accepted epoch
    return store->standard_storage.api.get_all_untrimmed_instances_info(store->standard_storage.handle, retrieved_instances_info, number_of_instances_retrieved);
}
*/

int epoch_stable_storage_get_all_untrimmed_epoch_ballot_accepts(struct epoch_stable_storage *store, struct epoch_ballot_accept **retrieved_epoch_ballot_accepts,
                                                                int *number_of_instances_retrieved){
    return store->extended_api.get_all_untrimmed_epoch_ballots(store->extended_handle, retrieved_epoch_ballot_accepts, number_of_instances_retrieved);
    return 1;
}

int epoch_stable_storage_get_max_inited_instance(struct epoch_stable_storage *storage, iid_t *retrieved_instance){
    return storage->standard_storage.api.get_max_instance(storage->standard_storage.handle, retrieved_instance);
}

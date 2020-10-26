//
// Created by Michael Davis on 17/03/2020.
//

#ifndef A_LESS_WRITEY_PAXOS_PENDING_CLIENT_VALUES_H
#define A_LESS_WRITEY_PAXOS_PENDING_CLIENT_VALUES_H

#include "paxos_value.h"

struct pending_client_values;


struct pending_client_values* pending_client_values_new();

//unsigned int array_list_number_of_pending_values(struct pending_client_values* array_list);

bool get_value_pending_at(struct pending_client_values* array_list, iid_t i, struct paxos_value* pending_value_copy);

void client_value_now_pending_at(struct pending_client_values* array_list, iid_t instance, const struct paxos_value* copy_of_val_now_pending);

bool get_value_pending_at(struct pending_client_values* array_list, unsigned int index, struct paxos_value* pending_value_copy);

bool remove_pending_value_at(struct pending_client_values* array_list, unsigned int index, struct paxos_value* pending_value_copy);

int get_and_close_pending_value_and_its_instances_if_open(struct pending_client_values *array_list, struct paxos_value *value,
                                                          iid_t **instances_with_values_closed);

#endif //A_LESS_WRITEY_PAXOS_PENDING_CLIENT_VALUES_H

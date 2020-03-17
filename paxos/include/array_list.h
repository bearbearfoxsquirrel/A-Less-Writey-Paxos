//
// Created by Michael Davis on 17/03/2020.
//

#ifndef A_LESS_WRITEY_PAXOS_ARRAY_LIST_H
#define A_LESS_WRITEY_PAXOS_ARRAY_LIST_H

struct array_list;

struct array_list* array_list_new(unsigned int initial_size, unsigned int increase_amount);

unsigned int array_list_number_of_elements(struct array_list* array_list);

void* array_list_get_element_at(struct array_list* array_list, unsigned int i);

void array_list_append(struct array_list* array_list, void* value_to_append);

void* array_list_remove_at(struct array_list* array_list, unsigned int index);
#endif //A_LESS_WRITEY_PAXOS_ARRAY_LIST_H

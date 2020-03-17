//
// Created by Michael Davis on 17/03/2020.
//

#include <assert.h>
#include "stdlib.h"
#include "array_list.h"
#include "string.h"

struct array_list {
    unsigned int max_size;
    unsigned int num_elements;
    void** array;
    unsigned int increase_amount;
};



unsigned int array_list_number_of_elements(struct array_list* array_list){
    return array_list->num_elements;
}

void* array_list_get_element_at(struct array_list* array_list, unsigned int i){
    assert(i < array_list->num_elements);

    return array_list->array[i];
}


struct array_list* array_list_new(unsigned int initial_size, unsigned int increase_amount) {
    struct array_list* a = malloc(sizeof(struct array_list));
    a->max_size = initial_size;
    a->num_elements = 0;
    a->increase_amount = increase_amount;
    a->array = malloc(sizeof(void*) * initial_size);

    return a;
}


void array_list_append(struct array_list* array_list, void* value_to_append){
    array_list->num_elements++;
    if (array_list->num_elements > array_list->max_size) {
        unsigned int new_size = array_list->increase_amount + array_list->num_elements;
        array_list->max_size = new_size;
        array_list->array = realloc(array_list, array_list->max_size  * sizeof(struct paxos_value *));
    }
    array_list->array[array_list->num_elements - 1] = value_to_append;
}


void* array_list_remove_at(struct array_list* array_list, unsigned int index){
    assert(index < array_list->num_elements);

    void* value_returned = array_list->array[index];
    for(unsigned int i = index; i < array_list->num_elements - 1; i++)
        memmove(array_list->array[i], array_list->array[i + 1], index * sizeof(struct paxos_value*));  // Safe move
    array_list->num_elements--;
    return value_returned;
}


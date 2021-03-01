//
// Created by Michael Davis on 11/03/2020.
//

#include <assert.h>
#include <paxos_types.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <paxos.h>
#include "paxos_value.h"


#define CLIENT_VALUE_METADATA_SIZE 96


void destroy_and_replace_value(struct paxos_value* src, struct paxos_value* dst) {
    paxos_value_destroy(dst);
    copy_value(src, dst);
}

void copy_value(const struct paxos_value *src, struct paxos_value *dst) {
   // assert(dst != NULL);
    
    char *value = NULL;
    unsigned int value_size = src->paxos_value_len;
    if (value_size > 0) {
        value = malloc(value_size * sizeof(char));
        memcpy(value, src->paxos_value_val, value_size * sizeof(char));
    }

    *dst = (struct paxos_value) {value_size, value};

   // assert(is_values_equal(*src, *dst));
}

void
paxos_value_copy(struct paxos_value* dst, struct paxos_value* src)
{
   // assert(src != NULL);
   // assert(dst != NULL);
    unsigned int len = src->paxos_value_len;
    dst->paxos_value_len = len;
    if (src->paxos_value_len > 0) {
        dst->paxos_value_val = malloc(len * sizeof(char));
        memcpy(dst->paxos_value_val, src->paxos_value_val, len * sizeof(char));
    }
}


struct paxos_value*
paxos_value_new(const char* value, size_t size)
{
    struct paxos_value* v = malloc(sizeof(struct paxos_value));
    v->paxos_value_len = size;
    v->paxos_value_val = malloc(sizeof(char)*size);
    memcpy(v->paxos_value_val, value, sizeof(char)*size );
    return v;
}

void
paxos_value_free(struct paxos_value** v)
{
    if (*v != NULL) {
        if ((*v)->paxos_value_val != NULL) {
            free((*v)->paxos_value_val);
            // (*v)->paxos_value_val = NULL;
        }
        (*v)->paxos_value_len = 0;
        free(*v);
        *v = NULL;
    }
}

void
paxos_value_destroy(struct paxos_value* v)
{
    if (v != NULL) {
        if (v->paxos_value_val != NULL)
            free(v->paxos_value_val);
        v->paxos_value_val = NULL;
        v->paxos_value_len = 0;
    }

}

bool is_values_equal(struct paxos_value lhs, struct paxos_value rhs){
    if (lhs.paxos_value_len == rhs.paxos_value_len) {
        if (memcmp(lhs.paxos_value_val, rhs.paxos_value_val, lhs.paxos_value_len) == 0) {
            return true;
        } else {
            return false;
        }
    } else {
        return false;
    }
}


bool is_paxos_values_equal(struct paxos_value* lhs, struct paxos_value* rhs){

    //could cheat and cmp only client unique part
    if (lhs->paxos_value_len == rhs->paxos_value_len) {
        if (memcmp(lhs->paxos_value_val, rhs->paxos_value_val, lhs->paxos_value_len) == 0) {
            return true;
        } else {
            return false;
        }
    } else {
        return false;
    }
}
/*
int
paxos_value_cmp(struct paxos_value* v1, struct paxos_value* v2)
{
    if (v1->paxos_value_len != v2->paxos_value_len)
        return -1;
    return memcmp(v1->paxos_value_val, v2->paxos_value_val, v1->paxos_value_len);
}*/


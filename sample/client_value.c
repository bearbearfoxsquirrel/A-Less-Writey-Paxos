//
// Created by Michael Davis on 09/11/2020.
//

#include "client_value.h"

//#include "random.h"
#include <stdlib.h>
#include <assert.h>
#include <random.h>


//void fill_paxos_value_from_client_value(struct client_value* src, struct paxos_value* dst){

//}

//void fill_client_value_from_paxos_value(struct paxos_value* src, struct client_value* dst){

//}

static void random_string(char *s, const int len)
{
    int i;
    static const char alphanum[] =
            "0123456789abcdefghijklmnopqrstuvwxyz";
    for (i = 0; i < len-1; ++i)
        s[i] = alphanum[rand() % (sizeof(alphanum) - 1)];
    s[len-1] = 0;
}

void
client_value_generate(struct client_value **value_allocated, uint32_t value_size, uint32_t c_id) {
  // // assert((*value_allocated) != NULL);
    (*value_allocated)->client_id = c_id;
    (*value_allocated)->uid = rand(); //random_between(1, RAND_MAX);
    (*value_allocated)->size = value_size;
    random_string((*value_allocated)->value, (*value_allocated)->size);
}

uint32_t client_value_get_uid(const struct client_value* value){
    return value->uid;
}

uint32_t client_value_get_value_size(const struct client_value* value){
    return value->size;
}

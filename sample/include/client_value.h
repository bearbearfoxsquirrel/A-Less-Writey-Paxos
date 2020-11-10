//
// Created by Michael Davis on 11/03/2020.
//

#ifndef LIBPAXOS_CLIENT_VALUE_H
#define LIBPAXOS_CLIENT_VALUE_H

//#include "paxos_value.h"
//#include "stdio.h"

//#include <ntsid.h>
#include <stddef.h>

//public so that it can be allocated on the stack and space can be reused
struct client_value {
    unsigned int client_id;
    unsigned int uid;
    size_t size;
    char value[0];
};


struct value{
    size_t size;
    char value[0];
};
  //  unsigned int value_len;
  //  char* value;
//};
//    int client_id;
//    struct timeval submitted_at;
//    size_t value_size;
//    char* value;
//};

void fill_paxos_value_from_client_value(struct client_value* src, struct paxos_value* dst);

void fill_client_value_from_paxos_value(struct paxos_value* src, struct client_value* dst);

void client_value_generate(struct client_value **value_allocated, unsigned int value_size, unsigned int c_id);

void client_value_free(struct client_value** value);

unsigned int client_value_get_uid(const struct client_value* value);

unsigned int client_value_get_value_size(const struct client_value* value);

struct value client_value_get_value(const struct client_value* value);

//void client_value_free(struct client_value** value);

//unsigned int value_get_length(struct value* value);

//char* value_get_bytes(struct value* value);

void value_free(struct value** value);





#endif //LIBPAXOS_CLIENT_VALUE_H

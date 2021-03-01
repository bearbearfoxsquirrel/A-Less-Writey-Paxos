//
// Created by Michael Davis on 11/03/2020.
//

#ifndef LIBPAXOS_PAXOS_VALUE_H
#define LIBPAXOS_PAXOS_VALUE_H

#include <stddef.h>
#include "stdbool.h"
#include "stdint.h"

struct paxos_value
{
	unsigned int paxos_value_len;
	char *paxos_value_val;
};

#define NOP_VALUE "NOP"
#define NOP_LEN 4
#define NOP (struct paxos_value) {NOP_LEN, NOP_VALUE}
#define NOP_NEW paxos_value_new(NOP_VALUE, NOP_LEN)


struct paxos_value* paxos_value_new(const char* v, size_t s);
void paxos_value_free(struct paxos_value** v);
void paxos_value_destroy(struct paxos_value* v);
void copy_value(const struct paxos_value *src, struct paxos_value *dst);
void destroy_and_replace_value(struct paxos_value* src, struct paxos_value* dst);
void paxos_value_copy(struct paxos_value* dst, struct paxos_value* src);

bool is_values_equal(struct paxos_value lhs, struct paxos_value rhs);

bool is_paxos_values_equal(struct paxos_value* lhs, struct paxos_value* rhs);

//int paxos_value_cmp(struct paxos_value* v1, struct paxos_value* v2);

#endif //LIBPAXOS_PAXOS_VALUE_H

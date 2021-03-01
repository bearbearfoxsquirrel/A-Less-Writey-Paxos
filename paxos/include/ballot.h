//
// Created by Michael Davis on 11/03/2020.
//

#ifndef LIBPAXOS_BALLOT_H
#define LIBPAXOS_BALLOT_H

#include "stdbool.h"
#include "paxos_types.h"

#define INVALID_BALLOT (struct ballot) {0, 0}

void copy_ballot(const struct ballot *src, struct ballot *dst);

bool ballot_equal(const struct ballot lhs, const struct ballot rhs);

bool ballot_greater_than(const struct ballot lhs, const struct ballot rhs);

bool ballot_greater_than_or_equal(const struct ballot lhs, const struct ballot rhs);

bool is_ballot_legit(const struct ballot ballot);

#endif //LIBPAXOS_BALLOT_H

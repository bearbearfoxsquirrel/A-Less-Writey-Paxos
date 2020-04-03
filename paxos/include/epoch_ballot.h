//
// Created by Michael Davis on 26/03/2020.
//

#ifndef A_LESS_WRITEY_PAXOS_EPOCH_BALLOT_H
#define A_LESS_WRITEY_PAXOS_EPOCH_BALLOT_H


#include <stdbool.h>


void copy_epoch_ballot(const struct epoch_ballot *src, struct epoch_ballot *dst);

bool epoch_ballot_equal(const struct epoch_ballot lhs, const struct epoch_ballot rhs);

bool epoch_ballot_greater_than(const struct epoch_ballot lhs, const struct epoch_ballot rhs);

bool epoch_greater_than(const struct epoch_ballot lhs, const struct epoch_ballot rhs);

bool epoch_equal(const struct epoch_ballot lhs, const struct epoch_ballot rhs);

bool epoch_ballot_greater_than_or_equal(const struct epoch_ballot lhs, const struct epoch_ballot rhs);


#endif //A_LESS_WRITEY_PAXOS_EPOCH_BALLOT_H

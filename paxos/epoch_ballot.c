//
// Created by Michael Davis on 26/03/2020.
//



#include <stdbool.h>
#include <paxos_types.h>
#include "stdlib.h"
#include <assert.h>
#include "epoch_ballot.h"
#include "ballot.h"

void copy_epoch_ballot(const struct epoch_ballot *src, struct epoch_ballot *dst) {
    *dst = (struct epoch_ballot) {.epoch = src->epoch, .ballot = src->ballot};
}

bool epoch_ballot_equal(const struct epoch_ballot lhs, const struct epoch_ballot rhs){
    return lhs.epoch == rhs.epoch && ballot_equal(lhs.ballot, rhs.ballot);
}

bool epoch_ballot_greater_than(const struct epoch_ballot lhs, const struct epoch_ballot rhs){
    if (lhs.epoch > rhs.epoch) {
        return true;
    } else {
        return ballot_greater_than(lhs.ballot, rhs.ballot);
    }
}

bool epoch_greater_than(const struct epoch_ballot lhs, const struct epoch_ballot rhs) {
    return lhs.epoch > rhs.epoch;
}

bool epoch_equal(const struct epoch_ballot lhs, const struct epoch_ballot rhs) {
    return lhs.epoch == rhs.epoch;
}

bool epoch_ballot_greater_than_or_equal(const struct epoch_ballot lhs, const struct epoch_ballot rhs){
    return epoch_ballot_equal(lhs, rhs) || epoch_ballot_greater_than(lhs, rhs);
}



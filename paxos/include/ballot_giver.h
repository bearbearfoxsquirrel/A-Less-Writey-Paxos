//
// Created by Michael Davis on 16/11/2020.
//

#ifndef A_LESS_WRITEY_PAXOS_BALLOT_GIVER_H
#define A_LESS_WRITEY_PAXOS_BALLOT_GIVER_H

#include "ballot.h"
#include "paxos_types.h"

struct ballot_giver;

struct ballot_giver* ballot_giver_new(unsigned int proposer_id, unsigned int ballot_increment);

void ballot_giver_free(struct ballot_giver** ballot_giver);

struct ballot ballot_giver_next(struct ballot_giver *ballot_giver, const struct ballot* previous);



#endif //A_LESS_WRITEY_PAXOS_BALLOT_GIVER_H

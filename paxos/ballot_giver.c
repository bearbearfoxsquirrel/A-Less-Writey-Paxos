//
// Created by Michael Davis on 16/11/2020.
//

#include "ballot_giver.h"

#include <random.h>
#include <stdlib.h>

struct ballot_giver {
    unsigned int proposer_id;
    unsigned int ballot_increment;
};

struct ballot_giver* ballot_giver_new(unsigned int proposer_id, unsigned int ballot_increment) {
    struct ballot_giver* ballot_giver = malloc(sizeof(*ballot_giver));
    *ballot_giver = (struct ballot_giver) {
        .proposer_id = proposer_id,
        .ballot_increment = ballot_increment
    };
    return ballot_giver;
}

void ballot_giver_free(struct ballot_giver** ballot_giver) {
    free(&ballot_giver);
}

/// You can use other Acceptor's ballots here
struct ballot ballot_giver_next(struct ballot_giver *ballot_giver, const struct ballot* previous){
    return (struct ballot) {.number = random_between(previous->number + 1 + ballot_giver->proposer_id,
                                                     previous->number + ballot_giver->ballot_increment + 1) - ballot_giver->proposer_id,
            .proposer_id = ballot_giver->proposer_id
    };
}

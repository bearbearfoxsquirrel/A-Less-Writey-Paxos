//
// Created by Michael Davis on 12/11/2020.
//
#include "settler_iner.h"

#include <stdlib.h>

struct settler_iner {
    struct timeval began_time;
    struct timeval settle_in_time;

};

struct settler_iner* settler_iner_new(struct timeval settle_in_time){
    struct settler_iner* settler_iner = malloc(sizeof(*settler_iner));

    struct timeval now;
    gettimeofday(&now, NULL);

    *settler_iner = (struct settler_iner) {
        .began_time = now,
        .settle_in_time = settle_in_time
    };

    return settler_iner;
}

bool settler_iner_okay_to_propose_value(struct settler_iner* settler_iner) {

}

void settler_iner_free(struct settler_iner** p_settler_iner) {
    free(*p_settler_iner);
}


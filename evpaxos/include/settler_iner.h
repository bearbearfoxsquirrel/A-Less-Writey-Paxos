//
// Created by Michael Davis on 12/11/2020.
//

#ifndef A_LESS_WRITEY_PAXOS_SETTLER_INER_H
#define A_LESS_WRITEY_PAXOS_SETTLER_INER_H

#include <stdbool.h>
#include <sys/time.h>


struct settler_iner;

struct settler_iner* settler_iner_new(struct timeval settle_in_time);

bool settler_iner_okay_to_propose_value(struct settler_iner* settler_iner);

void settler_iner_free(struct settler_iner** p_settler_iner);


#endif //A_LESS_WRITEY_PAXOS_SETTLER_INER_H

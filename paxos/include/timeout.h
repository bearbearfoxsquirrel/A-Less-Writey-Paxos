//
// Created by Michael Davis on 26/03/2020.
//

#ifndef A_LESS_WRITEY_PAXOS_TIMEOUT_H
#define A_LESS_WRITEY_PAXOS_TIMEOUT_H

#include <stdlib.h>
#include "paxos.h"
#include "stdint.h"
#include "proposer.h"

struct timeout_iterator;


// timeouts
struct timeout_iterator* proposer_timeout_iterator(struct proposer* p);

int timeout_iterator_prepare(struct timeout_iterator* iter, paxos_prepare* out);

int timeout_iterator_accept(struct timeout_iterator* iter, paxos_accept* out);

void timeout_iterator_free(struct timeout_iterator* iter);

#endif //A_LESS_WRITEY_PAXOS_TIMEOUT_H

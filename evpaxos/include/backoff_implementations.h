//
// Created by Michael Davis on 26/03/2020.
//

#ifndef A_LESS_WRITEY_PAXOS_BACKOFF_IMPLEMENTATIONS_H
#define A_LESS_WRITEY_PAXOS_BACKOFF_IMPLEMENTATIONS_H

#include "backoff.h"

//todo struct decorolated_jitter;
struct exponential_randomised_backoff;
struct full_jitter_backoff;
extern struct backoff_api exponential_randomised_backoff_api[],  full_jitter_backoff_api[];

struct backoff* full_jitter_backoff_new(unsigned long max_backoff, unsigned long min_backoff, unsigned long max_initial_backoff);

struct backoff* exponential_randomised_backoff_new(unsigned long max_backoff, unsigned long min_backoff, unsigned long max_initial_backoff) ;


#endif //A_LESS_WRITEY_PAXOS_BACKOFF_IMPLEMENTATIONS_H

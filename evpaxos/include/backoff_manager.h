//
// Created by Michael Davis on 25/03/2020.
//

#ifndef A_LESS_WRITEY_PAXOS_BACKOFF_MANAGER_H
#define A_LESS_WRITEY_PAXOS_BACKOFF_MANAGER_H

#include <paxos.h>

#include "backoff.h"

struct backoff_manager;

struct backoff_manager* backoff_manager_new(struct backoff* backoff);

const struct timeval* backoff_manager_get_backoff(struct backoff_manager* manager, iid_t instance_to_backoff);
//void backoff_manager_begin_backoff(struct backoff_manager* manager, iid_t instance_to_backoff);
void backoff_manager_close_backoff_if_exists(struct backoff_manager* manager, iid_t instance_to_close);
void backoff_manager_close_less_than_or_equal(struct backoff_manager* manager, iid_t backoff_from);
void backoff_manager_free(struct backoff_manager **manager);


#endif //A_LESS_WRITEY_PAXOS_BACKOFF_MANAGER_H

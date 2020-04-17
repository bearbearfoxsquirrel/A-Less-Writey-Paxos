//
// Created by Michael Davis on 06/04/2020.
//

#ifndef A_LESS_WRITEY_PAXOS_EV_EPOCH_PAXOS_INTERNAL_H
#define A_LESS_WRITEY_PAXOS_EV_EPOCH_PAXOS_INTERNAL_H


#include "standard_paxos_peers.h"
#include "evpaxos.h"
#include "backoff_manager.h"
struct ev_epoch_learner* ev_epoch_learner_init_internal(struct evpaxos_config* config, struct writeahead_epoch_paxos_peers* peers, epoch_client_deliver_function f, void* arg);

void ev_epoch_learner_free_internal(struct ev_epoch_learner** l);

struct ev_epoch_acceptor *
ev_epoch_acceptor_init_internal(int id, struct evpaxos_config *c, struct writeahead_epoch_paxos_peers *p);

void ev_epoch_acceptor_free_internal(struct ev_epoch_acceptor** a);

struct ev_epoch_proposer* ev_epoch_proposer_init_internal(int id, struct evpaxos_config* c, struct writeahead_epoch_paxos_peers* peers, struct backoff_manager* backoff_manager);

void ev_epoch_proposer_free_internal(struct ev_epoch_proposer** p);



#endif //A_LESS_WRITEY_PAXOS_EV_EPOCH_PAXOS_INTERNAL_H

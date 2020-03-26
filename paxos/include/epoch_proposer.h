//
// Created by Michael Davis on 07/02/2020.
//

#ifndef LIBPAXOS_EPOCH_PROPOSER_H
#define LIBPAXOS_EPOCH_PROPOSER_H

#include "paxos.h"
#include "timeout.h"

struct epoch_proposer;
void epoch_proposer_add_paxos_value_to_queue(struct epoch_proposer* p, struct paxos_value* value);
int epoch_proposer_prepare_count(struct epoch_proposer* p);
int epoch_proposer_acceptance_count(struct epoch_proposer* p);
void epoch_proposer_set_current_instance(struct epoch_proposer* p, iid_t iid);


void epoch_proposer_next_instance(struct epoch_proposer* p);

uint32_t epoch_proposer_get_current_instance(struct epoch_proposer* p);

uint32_t epoch_proposer_get_min_unchosen_instance(struct epoch_proposer* p);
iid_t epoch_proposer_get_next_instance_to_prepare(struct epoch_proposer* p);

// phase 1
bool epoch_proposer_try_to_start_preparing_instance(struct epoch_proposer* p, iid_t instance, paxos_prepare* out);

int epoch_proposer_receive_promise(struct epoch_proposer* p, paxos_promise* ack,
                             paxos_prepare* out);

// phase 2
int epoch_proposer_try_accept(struct epoch_proposer* p, paxos_accept* out);
int epoch_proposer_receive_accepted(struct epoch_proposer* p, paxos_accepted* ack, struct paxos_chosen* chosen);
int epoch_proposer_receive_chosen(struct epoch_proposer* p, struct paxos_chosen* ack);

//void epoch_proposer_preempt(struct epoch_proposer* p, struct standard_epoch_proposer_instance_info* inst, paxos_prepare* out);
int epoch_proposer_receive_preempted(struct epoch_proposer* p, struct paxos_preempted* preempted, struct paxos_prepare* out);

int is_epoch_proposer_instance_pending_and_message_return(struct epoch_proposer* p, paxos_preempted* ack,
                                                    paxos_prepare* out);

// periodic acceptor state
void epoch_proposer_receive_acceptor_state(struct epoch_proposer* p,
                                     paxos_standard_acceptor_state* state);
void epoch_proposer_receive_trim(struct epoch_proposer* p,
                           struct paxos_trim* trim_msg);
// timeouts
struct timeout_iterator* epoch_proposer_timeout_iterator(struct epoch_proposer* p);
int timeout_iterator_prepare(struct timeout_iterator* iter, paxos_prepare* out);
int timeout_iterator_accept(struct timeout_iterator* iter, paxos_accept* out);
void timeout_iterator_free(struct timeout_iterator* iter);
#endif //LIBPAXOS_EPOCH_PROPOSER_H

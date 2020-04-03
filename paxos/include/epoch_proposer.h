//
// Created by Michael Davis on 07/02/2020.
//

#ifndef LIBPAXOS_EPOCH_PROPOSER_H
#define LIBPAXOS_EPOCH_PROPOSER_H

#include "paxos.h"
#include "timeout.h"

struct epoch_proposer;

enum epoch_proposer_received_message_return_codes {
    MESSAGE_IGNORED,
    EPOCH_PREEMPTED,
    BALLOT_PREEMPTED,
    MESSAGE_ACKNOWLEDGED,
    QUORUM_REACHED,
};


struct epoch_proposer* epoch_proposer_new(int id, int acceptors, int q1, int q2);
void epoch_proposer_free(struct epoch_proposer* p);
void epoch_proposer_add_paxos_value_to_queue(struct epoch_proposer* p, struct paxos_value value);
int epoch_proposer_prepare_count(struct epoch_proposer* p);
int epoch_proposer_acceptance_count(struct epoch_proposer* p);
void epoch_proposer_set_current_instance(struct epoch_proposer* p, iid_t iid);


void epoch_proposer_next_instance(struct epoch_proposer* p);

uint32_t epoch_proposer_get_current_instance(struct epoch_proposer* p);

uint32_t epoch_proposer_get_min_unchosen_instance(struct epoch_proposer* p);
iid_t epoch_proposer_get_next_instance_to_prepare(struct epoch_proposer* p);

// phase 1
bool epoch_proposer_try_to_start_preparing_instance(struct epoch_proposer* p, iid_t instance, struct epoch_paxos_prepares *out);

enum epoch_proposer_received_message_return_codes epoch_proposer_receive_promise(struct epoch_proposer* p, struct epoch_ballot_promise* ack,
                                                                                 struct epoch_ballot_prepare* out);

// phase 2
int epoch_proposer_try_accept(struct epoch_proposer* p, struct epoch_ballot_accept* out);
enum epoch_proposer_received_message_return_codes epoch_proposer_receive_accepted(struct epoch_proposer* p, struct epoch_ballot_accepted* ack, struct instance_chosen_at_epoch_ballot* chosen);
enum epoch_proposer_received_message_return_codes epoch_proposer_receive_chosen(struct epoch_proposer* p, struct instance_chosen_at_epoch_ballot* ack);

//void epoch_proposer_preempt(struct epoch_proposer* p, struct standard_epoch_proposer_instance_info* inst, paxos_prepare* out);
enum epoch_proposer_received_message_return_codes epoch_proposer_receive_preempted(struct epoch_proposer* p, struct epoch_ballot_preempted* preempted, struct epoch_ballot_prepare* next_prepare);

int is_epoch_proposer_instance_pending_and_message_return(struct epoch_proposer* p, paxos_preempted* ack,
                                                    paxos_prepare* out);

// periodic acceptor state
enum epoch_proposer_received_message_return_codes epoch_proposer_receive_acceptor_state(struct epoch_proposer* p, struct writeahead_epoch_acceptor_state* state);

enum epoch_proposer_received_message_return_codes epoch_proposer_receive_trim(struct epoch_proposer* p, struct paxos_trim* trim_msg);
#endif //LIBPAXOS_EPOCH_PROPOSER_H

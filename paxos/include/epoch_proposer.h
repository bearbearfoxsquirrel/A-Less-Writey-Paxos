//
// Created by Michael Davis on 07/02/2020.
//

#ifndef LIBPAXOS_EPOCH_PROPOSER_H
#define LIBPAXOS_EPOCH_PROPOSER_H

#include "paxos.h"
#include "timeout.h"

struct epoch_proposer;




struct epoch_proposer *epoch_proposer_new(int id, int acceptors, int q1, int q2, uint32_t max_ballot_increment);
void epoch_proposer_free(struct epoch_proposer* p);
void epoch_proposer_add_paxos_value_to_queue(struct epoch_proposer* p, struct paxos_value value);
int epoch_proposer_prepare_count(struct epoch_proposer* p);
int epoch_proposer_acceptance_count(struct epoch_proposer* p);
void epoch_proposer_set_current_instance(struct epoch_proposer* p, iid_t iid);
unsigned int epoch_proposer_get_id(struct epoch_proposer* p);

unsigned int epoch_proposer_get_current_known_epoch(struct epoch_proposer* p);
int epoch_proposer_handle_epoch_notification(struct epoch_proposer* p, struct epoch_notification* epoch_notification);


void epoch_proposer_next_instance(struct epoch_proposer* p);

uint32_t epoch_proposer_get_current_instance(struct epoch_proposer* p);

uint32_t epoch_proposer_get_min_unchosen_instance(struct epoch_proposer* p);
iid_t epoch_proposer_get_next_instance_to_prepare(struct epoch_proposer* p);

// phase 1
bool epoch_proposer_try_to_start_preparing_instance(struct epoch_proposer *p, iid_t instance,
                                                    struct epoch_ballot initial_ballot,
                                                    struct epoch_paxos_prepares *out);

struct ballot epoch_proposer_get_next_ballot(const int acceptors_last_bal_num, const uint32_t pid, const int ballot_increment);

enum epoch_paxos_message_return_codes epoch_proposer_receive_promise(struct epoch_proposer* p, struct epoch_ballot_promise* ack,
                                                                     struct epoch_paxos_message *next_epoch_prepare);

// phase 2
int epoch_proposer_try_accept(struct epoch_proposer* p, struct epoch_ballot_accept* out);
enum epoch_paxos_message_return_codes epoch_proposer_receive_accepted(struct epoch_proposer* p, struct epoch_ballot_accepted* ack, struct epoch_ballot_chosen* chosen);
enum epoch_paxos_message_return_codes epoch_proposer_receive_chosen(struct epoch_proposer* p, struct epoch_ballot_chosen* ack);

//void epoch_proposer_preempt(struct epoch_proposer* p, struct standard_epoch_proposer_instance_info* inst, paxos_prepare* out);
enum epoch_paxos_message_return_codes epoch_proposer_receive_preempted(struct epoch_proposer* p, struct epoch_ballot_preempted* preempted, struct epoch_ballot_prepare* next_prepare);

bool epoch_proposer_is_instance_pending(struct epoch_proposer *p, iid_t instance);

// periodic acceptor acceptor_state
enum epoch_paxos_message_return_codes epoch_proposer_receive_acceptor_state(struct epoch_proposer* p, struct writeahead_epoch_acceptor_state* state);

bool epoch_proposer_get_state(struct epoch_proposer* p, struct epoch_proposer_state* out);
enum epoch_paxos_message_return_codes
epoch_proposer_receive_epoch_proposer_state(struct epoch_proposer *p, struct epoch_proposer_state *state);

enum epoch_paxos_message_return_codes epoch_proposer_receive_trim(struct epoch_proposer* p, struct paxos_trim* trim_msg);



struct epoch_proposer_timeout_iterator;

struct epoch_proposer_timeout_iterator* epoch_proposer_timeout_iterator_new(struct epoch_proposer* p);

enum timeout_iterator_return_code epoch_proposer_timeout_iterator_accept(struct epoch_proposer_timeout_iterator* iter, struct epoch_ballot_accept* out);

enum timeout_iterator_return_code epoch_proposer_timeout_iterator_prepare(struct epoch_proposer_timeout_iterator* iter, struct epoch_ballot_prepare* out);

void epoch_proposer_print_counters(struct epoch_proposer* p);

void epoch_proposer_timeout_iterator_free(struct epoch_proposer_timeout_iterator** iter);

#endif //LIBPAXOS_EPOCH_PROPOSER_H

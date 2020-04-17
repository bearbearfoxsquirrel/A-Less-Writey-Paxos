//
// Created by Michael Davis on 03/02/2020.
//

#include "paxos.h"
#include "paxos_types.h"

#ifndef LIBPAXOS_WRITEAHEAD_EPOCH_ACCEPTOR_H
#define LIBPAXOS_WRITEAHEAD_EPOCH_ACCEPTOR_H


struct writeahead_epoch_acceptor;

struct writeahead_epoch_acceptor *
writeahead_epoch_acceptor_new(int id, struct epoch_notification *recover_message, bool *has_recovery_message,
                              iid_t prepareparing_window_size, iid_t max_number_of_prewritten_instances,
                              uint32_t expected_value_size);

void writeahead_epoch_acceptor_free(struct writeahead_epoch_acceptor* acceptor);

int writeahead_epoch_acceptor_receive_prepare(struct writeahead_epoch_acceptor* acceptor, struct paxos_prepare* request, struct writeahead_epoch_paxos_message* returned_message);

int writeahead_epoch_acceptor_receive_epoch_ballot_prepare(struct writeahead_epoch_acceptor* acceptor, struct epoch_ballot_prepare* request, struct writeahead_epoch_paxos_message* returned_message);

int writeahead_epoch_acceptor_receive_epoch_ballot_accept(struct writeahead_epoch_acceptor* acceptor, struct epoch_ballot_accept* request, struct writeahead_epoch_paxos_message* response);

int  writeahead_epoch_acceptor_receive_repeat(struct writeahead_epoch_acceptor* acceptor, iid_t iid, struct writeahead_epoch_paxos_message* response); //todo

int  writeahead_epoch_acceptor_receive_trim(struct writeahead_epoch_acceptor* acceptor, struct paxos_trim* trim); //todo

int  writeahead_epoch_acceptor_receive_epoch_notification(struct writeahead_epoch_acceptor* acceptor, struct epoch_notification* epoch_notification);

int writeahead_epoch_acceptor_get_current_state(struct writeahead_epoch_acceptor* acceptor, struct writeahead_epoch_acceptor_state* state);

int writeahead_epoch_acceptor_receive_instance_chosen(struct writeahead_epoch_acceptor* acceptor, struct epoch_ballot_chosen *chosen_message); // todo

void writeahead_epoch_acceptor_prewrite_instances(struct writeahead_epoch_acceptor *acceptor, iid_t start, iid_t stop,
                                                  uint32_t dummy_value_size);


iid_t writeahead_epoch_acceptor_get_max_proposed_instance(struct writeahead_epoch_acceptor* acceptor);

iid_t writeahead_epoch_acceptor_get_next_instance_to_prewrite(struct writeahead_epoch_acceptor* acceptor);

iid_t writeahead_epoch_acceptor_get_max_instances_prewrite(struct writeahead_epoch_acceptor* acceptor);

iid_t writeahead_epoch_acceptor_number_of_instance_to_prewrite_at_once(struct writeahead_epoch_acceptor* acceptor);

#endif //LIBPAXOS_WRITEAHEAD_EPOCH_ACCEPTOR_H

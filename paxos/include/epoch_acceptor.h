//
// Created by Michael Davis on 03/02/2020.
//

#include "paxos.h"
#include "paxos_types.h"

#ifndef LIBPAXOS_WRITEAHEAD_EPOCH_ACCEPTOR_H
#define LIBPAXOS_WRITEAHEAD_EPOCH_ACCEPTOR_H


struct epoch_acceptor;

struct epoch_acceptor* epoch_acceptor_new(int id, struct epoch_notification *recover_message, bool *has_recovery_message);

void writeahead_epoch_acceptor_free(struct epoch_acceptor* acceptor);

int writeahead_epoch_acceptor_receive_prepare(struct epoch_acceptor* acceptor, struct paxos_prepare* request, struct epoch_paxos_message* returned_message);

int writeahead_epoch_acceptor_receive_epoch_ballot_prepare(struct epoch_acceptor* acceptor, struct epoch_ballot_prepare* request, struct epoch_paxos_message* returned_message);

int writeahead_epoch_acceptor_receive_epoch_ballot_accept(struct epoch_acceptor* acceptor, struct epoch_ballot_accept* request, struct epoch_paxos_message* response);

int  writeahead_epoch_acceptor_receive_repeat(struct epoch_acceptor* acceptor, iid_t iid, struct epoch_paxos_message* response); //todo

int  writeahead_epoch_acceptor_receive_trim(struct epoch_acceptor* acceptor,
                                            struct paxos_trim* trim); //todo

int  writeahead_epoch_acceptor_receive_epoch_notification(struct epoch_acceptor* acceptor,
                                                          struct epoch_notification* epoch_notification);

int writeahead_epoch_acceptor_get_current_state(struct epoch_acceptor* acceptor, struct writeahead_epoch_acceptor_state* state);

int writeahead_epoch_acceptor_receive_instance_chosen(struct epoch_acceptor* acceptor, struct epoch_ballot_chosen *chosen_message); // todo

iid_t writeahead_epoch_acceptor_get_max_proposed_instance(struct epoch_acceptor* acceptor);


#endif //LIBPAXOS_WRITEAHEAD_EPOCH_ACCEPTOR_H

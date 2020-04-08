//
// Created by Michael Davis on 07/02/2020.
//

#ifndef LIBPAXOS_EPOCH_LEARNER_H
#define LIBPAXOS_EPOCH_LEARNER_H


#include "paxos.h"

struct epoch_learner;

struct epoch_learner* epoch_learner_new(int acceptors);
enum epoch_paxos_message_return_codes epoch_learner_receive_epoch_ballot_chosen(struct epoch_learner* l, struct epoch_ballot_chosen* out);
void epoch_learner_free(struct epoch_learner** l);
enum epoch_paxos_message_return_codes epoch_learner_set_instance_id(struct epoch_learner* l, iid_t iid);
enum epoch_paxos_message_return_codes epoch_learner_receive_accepted(struct epoch_learner* l, struct epoch_ballot_accepted* ack, struct epoch_ballot_chosen* returned_message);
int epoch_learner_deliver_next(struct epoch_learner* l, struct paxos_value* out);
int epoch_learner_has_holes(struct epoch_learner* l, iid_t* from, iid_t* to);
iid_t epoch_learner_get_instance_to_trim(struct epoch_learner* l);
void epoch_learner_set_trim_instance(struct epoch_learner* l, iid_t trim);
iid_t epoch_learner_get_trim_instance(struct epoch_learner* l);
enum epoch_paxos_message_return_codes epoch_learner_receive_trim(struct epoch_learner* l, struct paxos_trim* trim);
#endif //LIBPAXOS_EPOCH_LEARNER_H

//
// Created by Michael Davis on 06/04/2020.
//

#ifndef A_LESS_WRITEY_PAXOS_EPOCH_PAXOS_MESSAGE_H
#define A_LESS_WRITEY_PAXOS_EPOCH_PAXOS_MESSAGE_H


#include "paxos_types.h"
#include <event2/buffer.h>
#include <event2/bufferevent.h>

void send_epoch_paxos_message(struct bufferevent* bev, struct writeahead_epoch_paxos_message* msg);
void send_epoch_ballot_prepare(struct bufferevent* bev, struct epoch_ballot_prepare* msg);
void send_epoch_ballot_promise(struct bufferevent* bev, struct epoch_ballot_promise* msg);
void send_epoch_ballot_accept(struct bufferevent* bev, struct epoch_ballot_accept* msg);
void send_epoch_paxos_accepted(struct bufferevent* bev, struct epoch_ballot_accepted* msg);
void send_epoch_paxos_preempted(struct bufferevent* bev, struct epoch_ballot_preempted* msg);
void send_standard_paxos_prepare(struct bufferevent* bev, struct paxos_prepare* msg);
void send_epoch_paxos_trim(struct bufferevent* bev, struct paxos_trim* msg);
void send_epoch_paxos_chosen(struct bufferevent* bev, struct epoch_ballot_chosen* chosen_msg);
void epoch_paxos_submit_client_value(struct bufferevent* bev, char *data, int size);
int recv_epoch_paxos_message(struct evbuffer* in, struct writeahead_epoch_paxos_message* out);


#endif //A_LESS_WRITEY_PAXOS_EPOCH_PAXOS_MESSAGE_H

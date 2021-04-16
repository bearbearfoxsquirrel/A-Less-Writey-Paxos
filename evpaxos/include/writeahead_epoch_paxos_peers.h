//
// Created by Michael Davis on 06/04/2020.
//

#ifndef A_LESS_WRITEY_PAXOS_WRITEAHEAD_EPOCH_PAXOS_PEERS_H
#define A_LESS_WRITEY_PAXOS_WRITEAHEAD_EPOCH_PAXOS_PEERS_H

#include <paxos_types.h>
#include "evpaxos.h"
#include "paxos_types.h"
#include <event2/bufferevent.h>

struct writeahead_epoch_paxos_peer;
struct writeahead_epoch_paxos_peers;


typedef void (*writeahead_epoch_paxos_peer_cb)(struct writeahead_epoch_paxos_peer* p, struct epoch_paxos_message* m, void* arg);
typedef void (*writeahead_epoch_paxos_peer_iter_cb)(struct writeahead_epoch_paxos_peer* p, void* arg);

struct writeahead_epoch_paxos_peers *
writeahead_epoch_paxos_peers_new(struct event_base *base, struct evpaxos_config *config);
void writeahead_epoch_paxos_peers_free(struct writeahead_epoch_paxos_peers* p);
int writeahead_epoch_paxos_peers_count(struct writeahead_epoch_paxos_peers* p);
void writeahead_epoch_paxos_peers_connect_to_acceptors(struct writeahead_epoch_paxos_peers* p, int soruce_id);

void writeahead_epoch_paxos_peers_connect_to_proposers(struct writeahead_epoch_paxos_peers *p, int partner_id);
void writeahead_epoch_paxos_peers_connect_to_other_proposers(struct writeahead_epoch_paxos_peers *p, int self_id);
void writeahead_epoch_paxos_peers_connect_to_other_learners(struct writeahead_epoch_paxos_peers *p, int self_id);
void writeahead_epoch_paxos_peers_connect_to_clients(struct writeahead_epoch_paxos_peers *p, int self_id);

void writeahead_epoch_paxos_peers_foreach_proposer(struct writeahead_epoch_paxos_peers* p, writeahead_epoch_paxos_peer_iter_cb cb, void* arg);

void writeahead_epoch_paxos_peers_foreach_learner(struct writeahead_epoch_paxos_peers* p, writeahead_epoch_paxos_peer_iter_cb cb, void* arg);


    int writeahead_epoch_paxos_peers_listen(struct writeahead_epoch_paxos_peers* p, int port);
void writeahead_epoch_paxos_peers_subscribe(struct writeahead_epoch_paxos_peers* p, enum writeahead_epoch_message_type t, writeahead_epoch_paxos_peer_cb cb, void* arg);
void writeahead_epoch_paxos_peers_foreach_acceptor(struct writeahead_epoch_paxos_peers* p, writeahead_epoch_paxos_peer_iter_cb cb, void* arg);
void writeahead_epoch_paxos_peers_for_n_proposers(struct writeahead_epoch_paxos_peers* p, writeahead_epoch_paxos_peer_iter_cb cb, void* arg, int n);
void writeahead_epoch_paxos_peers_for_n_acceptor(struct writeahead_epoch_paxos_peers* p, writeahead_epoch_paxos_peer_iter_cb cb, void* arg, int n);
void writeahead_epoch_paxos_peers_for_n_actual_clients(struct writeahead_epoch_paxos_peers* p, writeahead_epoch_paxos_peer_iter_cb cb, void* arg, int n);
void writeahead_epoch_paxos_peers_to_actual_client(struct writeahead_epoch_paxos_peers* p, writeahead_epoch_paxos_peer_iter_cb cb, void* arg, int i);
void writeahead_epoch_paxos_peers_for_a_random_acceptor(struct writeahead_epoch_paxos_peers* p, writeahead_epoch_paxos_peer_iter_cb cb, void* arg);
void writeahead_epoch_paxos_peers_for_a_random_client(struct writeahead_epoch_paxos_peers* p, writeahead_epoch_paxos_peer_iter_cb cb, void* arg);
void writeahead_epoch_paxos_peers_foreach_client(struct writeahead_epoch_paxos_peers* p, writeahead_epoch_paxos_peer_iter_cb cb, void* arg);
struct writeahead_epoch_paxos_peer* writeahead_epoch_paxos_peers_get_acceptor(struct writeahead_epoch_paxos_peers* p, int id);
struct event_base* writeahead_epoch_paxos_peers_get_event_base(struct writeahead_epoch_paxos_peers* p);
int writeahead_epoch_paxos_peer_get_id(struct writeahead_epoch_paxos_peer* p);
struct bufferevent* writeahead_epoch_paxos_peer_get_buffer(struct writeahead_epoch_paxos_peer* p);
void writeahead_epoch_paxos_peers_send_to_proposer(struct writeahead_epoch_paxos_peers *p, writeahead_epoch_paxos_peer_iter_cb cb, void* arg, int prop_id);
bool writeahead_epoch_paxos_peer_is_partner_proposer(struct writeahead_epoch_paxos_peer* p, int partner_id);
int writeahead_epoch_paxos_peer_connected(struct writeahead_epoch_paxos_peer* p);

#endif //A_LESS_WRITEY_PAXOS_WRITEAHEAD_EPOCH_PAXOS_PEERS_H

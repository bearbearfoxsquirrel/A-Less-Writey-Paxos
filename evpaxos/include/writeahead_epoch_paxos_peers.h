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


typedef void (*writeahead_epoch_paxos_peer_cb)(struct writeahead_epoch_paxos_peer* p, struct writeahead_epoch_paxos_message* m, void* arg);
typedef void (*writeahead_epoch_paxos_peer_iter_cb)(struct writeahead_epoch_paxos_peer* p, void* arg);

struct writeahead_epoch_paxos_peers* writeahead_epoch_paxos_peers_new(struct event_base* base, struct evpaxos_config* config);
void writeahead_epoch_paxos_peers_free(struct writeahead_epoch_paxos_peers* p);
int writeahead_epoch_paxos_peers_count(struct writeahead_epoch_paxos_peers* p);
void writeahead_epoch_paxos_peers_connect_to_acceptors(struct writeahead_epoch_paxos_peers* p);
int writeahead_epoch_paxos_peers_listen(struct writeahead_epoch_paxos_peers* p, int port);
void writeahead_epoch_paxos_peers_subscribe(struct writeahead_epoch_paxos_peers* p, enum writeahead_epoch_message_type t, writeahead_epoch_paxos_peer_cb cb, void* arg);
void writeahead_epoch_paxos_peers_foreach_acceptor(struct writeahead_epoch_paxos_peers* p, writeahead_epoch_paxos_peer_iter_cb cb, void* arg);
void writeahead_epoch_paxos_peers_for_n_acceptor(struct writeahead_epoch_paxos_peers* p, writeahead_epoch_paxos_peer_iter_cb cb, void* arg, int n);
void writeahead_epoch_paxos_peers_foreach_client(struct writeahead_epoch_paxos_peers* p, writeahead_epoch_paxos_peer_iter_cb cb, void* arg);
struct writeahead_epoch_paxos_peer* writeahead_epoch_paxos_peers_get_acceptor(struct writeahead_epoch_paxos_peers* p, int id);
struct event_base* writeahead_epoch_paxos_peers_get_event_base(struct writeahead_epoch_paxos_peers* p);
int writeahead_epoch_paxos_peer_get_id(struct writeahead_epoch_paxos_peer* p);
struct bufferevent* writeahead_epoch_paxos_peer_get_buffer(struct writeahead_epoch_paxos_peer* p);
int writeahead_epoch_paxos_peer_connected(struct writeahead_epoch_paxos_peer* p);

#endif //A_LESS_WRITEY_PAXOS_WRITEAHEAD_EPOCH_PAXOS_PEERS_H

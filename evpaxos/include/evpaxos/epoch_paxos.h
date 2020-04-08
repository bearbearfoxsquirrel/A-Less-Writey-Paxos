//
// Created by Michael Davis on 06/04/2020.
//

#ifndef A_LESS_WRITEY_PAXOS_EPOCH_PAXOS_H
#define A_LESS_WRITEY_PAXOS_EPOCH_PAXOS_H


#include <sys/types.h>
#include <event2/event.h>
#include <event2/bufferevent.h>

struct ev_epoch_learner;
struct ev_epoch_proposer;
struct ev_epoch_acceptor;
struct ev_epoch_paxos_replica;

/**
 * When starting a learner you must pass a callback to be invoked whenev_epoch_er
 * a value has been learned.
 */
typedef void (*epoch_client_deliver_function)(
        char* value,
        size_t size,
        void* arg);

/**
 * Create a Paxos replica, consisting of a collocated Acceptor, Proposer,
 * and Learner.
 *
 * @param id the id of the replica
 * @param config path a paxos config file
 * @param cb the callback function to be called whenev_epoch_er the learner delivers.
 * This paramater may be NULL, in which case no learner is initialized.
 * @param arg an optional argument that is passed to the callback
 * @param base the underlying event_base to be used
 *
 * @return a new ev_epoch_paxos_replica on success, or NULL on failure.
 *
 * @see ev_epoch_paxos_replica_free()
 */
struct ev_epoch_paxos_replica* ev_epoch_paxos_replica_init(int id, const char* config,
                                                           epoch_client_deliver_function cb, void* arg, struct event_base* base);

/**
 * Destroy a Paxos replica and free all its memory.
 *
 * @param replica a ev_epoch_paxos_replica to be freed
 */
void ev_epoch_paxos_replica_free(struct ev_epoch_paxos_replica* replica);

/**
 * Set the starting instance id of the given replica.
 * The replica will call the delivery function for instances larger than the
 * given instance id.
 *
 * @param iid the starting instance id
 */
void ev_epoch_paxos_replica_set_instance_id(struct ev_epoch_paxos_replica* replica,
                                     unsigned iid);

/**
 * Send a trim message to all acceptors/replicas. Acceptors will trim their log
 * up the the given instance id.
 *
 * @param iid trim instance id
 */
void ev_epoch_paxos_replica_send_trim(struct ev_epoch_paxos_replica* replica, unsigned iid);

/**
 * Used by replicas to submit values.
 */
void ev_epoch_paxos_replica_submit(struct ev_epoch_paxos_replica* replica,
                            char* value, int size);

/**
 * Returns the number of replicas in the configuration.
 */
int ev_epoch_paxos_replica_count(struct ev_epoch_paxos_replica* replica);

/**
 * Initializes a learner with a given config file, a deliver callback,
 * an optional argument to that is passed to the callback, and
 * a libev_epoch_ent event_base.
 */
struct ev_epoch_learner* ev_epoch_learner_init(const char* config, epoch_client_deliver_function f,
                                 void* arg, struct event_base* base);

/**
 * Release the memory allocated by the learner
 */
void ev_epoch_learner_free(struct ev_epoch_learner** l);

/**
 * Set the starting instance id of the given learner.
 * The learner will call the delivery function for instances larger than the
 * given instance id.
 *
 * @param iid the starting instance id
 */
void ev_epoch_learner_set_instance_id(struct ev_epoch_learner* l, unsigned iid);

/**
 * Send a trim message to all acceptors/replicas. Acceptors will trim their log
 * up the the given instance id.
 *
 * @param iid trim instance id
 */
void ev_epoch_learner_send_trim(struct ev_epoch_learner* l, unsigned iid);

/**
 * Initializes a acceptor with a given id (which MUST be unique),
 * a config file and a libev_epoch_ent event_base.
 */
struct ev_epoch_acceptor* ev_epoch_acceptor_init(int id, const char* config,
                                             struct event_base* b);

/**
 * Frees the memory allocated by the acceptor.
 * This will also cleanly close the  * underlying storage.
 */
void ev_epoch_acceptor_free(struct ev_epoch_acceptor** a);


/**
 * Initializes a proposer with a given ID (which MUST be unique),
 * a config file and a libev_epoch_ent event_base.
 *
 * @param id a unique identifier, must be in the range [0...(MAX_N_OF_PROPOSERS-1)]
 */
struct ev_epoch_proposer* ev_epoch_proposer_init(int id, const char* config,
                                   struct event_base* b);

/**
 * Release the memory allocated by the proposer
 */
void ev_epoch_proposer_free(struct ev_epoch_proposer** p) ;

/**
 * This is a hint to the proposer to start from the given instance id.
 *
 * @param iid the starting instance id
 */
void ev_epoch_proposer_set_instance_id(struct ev_epoch_proposer* p, unsigned iid);

/**
 * Used by clients to submit values to proposers.
 */
void epoch_paxos_submit_client_value(struct bufferevent* bev_epoch_, char *data, int size);

#ifdef __cplusplus
}
#endif



#endif //A_LESS_WRITEY_PAXOS_EPOCH_PAXOS_H

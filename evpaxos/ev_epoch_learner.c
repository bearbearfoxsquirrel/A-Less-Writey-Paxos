//
// Created by Michael Davis on 07/04/2020.
//



#include "ev_epoch_paxos.h"
#include "epoch_learner.h"
#include "writeahead_epoch_paxos_peers.h"
#include "epoch_paxos_message.h"
#include <stdlib.h>
#include <event2/event.h>
#include <paxos_types.h>

struct ev_epoch_learner
{
    struct epoch_learner* learner;
    epoch_client_deliver_function delfun;
    void* delarg;

    struct event* hole_check_event;
    struct timeval hole_check_timer;
    struct writeahead_epoch_paxos_peers* peers;
};

static void peer_send_epoch_paxos_message(struct writeahead_epoch_paxos_peer* p, void* arg) {
    send_epoch_paxos_message(writeahead_epoch_paxos_peer_get_buffer(p), arg);
}



static void ev_epoch_learner_check_holes(__unused evutil_socket_t fd, __unused short event, void *arg)
{
    struct writeahead_epoch_paxos_message msg;
    unsigned int chunks = 10;
    struct ev_epoch_learner* l = arg;

    msg.type = WRITEAHEAD_REPEAT;

    if (epoch_learner_has_holes(l->learner, &msg.message_contents.repeat.from, &msg.message_contents.repeat.to)) {
        if ((msg.message_contents.repeat.to - msg.message_contents.repeat.from) > chunks)
            msg.message_contents.repeat.to = msg.message_contents.repeat.from + chunks;
        paxos_log_debug("Sending Repeat for Instances %u-%u", msg.message_contents.repeat.from, msg.message_contents.repeat.to);
        writeahead_epoch_paxos_peers_foreach_acceptor(l->peers, peer_send_epoch_paxos_message, &msg);
    } else {
            iid_t next_trim = epoch_learner_get_instance_to_trim(l->learner);
            iid_t current_trim =  epoch_learner_get_trim_instance(l->learner);
            if (next_trim > current_trim) {
                msg.type = WRITEAHEAD_INSTANCE_TRIM;
                msg.message_contents.trim = (struct paxos_trim) {next_trim};
                epoch_learner_set_trim_instance(l->learner, next_trim);

                paxos_log_debug("Sending Trim for Instance %u", next_trim);
                writeahead_epoch_paxos_peers_foreach_acceptor(l->peers, peer_send_epoch_paxos_message, &msg);

            }
        }
    event_add(l->hole_check_event, &l->hole_check_timer);
}

static void
ev_epoch_learner_deliver_next_closed(struct ev_epoch_learner* l)
{
    struct paxos_value deliver;
    while (epoch_learner_deliver_next(l->learner, &deliver)) {
        l->delfun(
                deliver.paxos_value_val,
                deliver.paxos_value_len,
                l->delarg);
        paxos_value_destroy(&deliver);
    }
}

static void ev_epoch_learner_handle_accepted(__unused struct writeahead_epoch_paxos_peer* p, struct writeahead_epoch_paxos_message* msg, void* arg){
    struct ev_epoch_learner* l = arg;
    struct epoch_ballot_accepted* accepted = &msg->message_contents.epoch_ballot_accepted;
    struct writeahead_epoch_paxos_message chosen;
    chosen.type = WRITEAHEAD_INSTANCE_CHOSEN_AT_EPOCH_BALLOT;

    enum epoch_paxos_message_return_codes return_code = epoch_learner_receive_accepted(l->learner, accepted, &chosen.message_contents.instance_chosen_at_epoch_ballot);

    if (return_code == QUORUM_REACHED) {
        writeahead_epoch_paxos_peers_foreach_acceptor(l->peers, peer_send_epoch_paxos_message, &chosen);
     //   writeahead_epoch_paxos_message_destroy_contents(&chosen);
    }
    ev_epoch_learner_deliver_next_closed(l);
}

static void ev_epoch_learner_handle_chosen(__unused struct writeahead_epoch_paxos_peer* p, struct writeahead_epoch_paxos_message* msg, void* arg) {
    struct ev_epoch_learner* l = arg;
    struct epoch_ballot_chosen* chosen = &msg->message_contents.instance_chosen_at_epoch_ballot;
    epoch_learner_receive_epoch_ballot_chosen(l->learner, chosen);
    ev_epoch_learner_deliver_next_closed(l);
}

static void ev_learner_handle_trim(__unused struct writeahead_epoch_paxos_peer* p, struct writeahead_epoch_paxos_message* msg, void* arg) {
    struct ev_epoch_learner* l = arg;
    struct paxos_trim* trim = &msg->message_contents.trim;
    epoch_learner_set_trim_instance(l->learner, trim->iid);
}


struct ev_epoch_learner* ev_epoch_learner_init_internal(struct evpaxos_config* config, struct writeahead_epoch_paxos_peers* peers, epoch_client_deliver_function f, void* arg) {
    struct ev_epoch_learner* learner = malloc(sizeof(struct ev_epoch_learner));
    int acceptor_count = evpaxos_acceptor_count(config);

    learner->learner = epoch_learner_new(acceptor_count);

    struct event_base* base = writeahead_epoch_paxos_peers_get_event_base(peers);

    learner->delfun = f;
    learner->delarg = arg;
    learner->peers = peers;

    writeahead_epoch_paxos_peers_subscribe(peers, WRITEAHEAD_EPOCH_BALLOT_ACCEPTED, ev_epoch_learner_handle_accepted, learner);
    writeahead_epoch_paxos_peers_subscribe(peers, WRITEAHEAD_INSTANCE_CHOSEN_AT_EPOCH_BALLOT, ev_epoch_learner_handle_chosen, learner);
    writeahead_epoch_paxos_peers_subscribe(peers, WRITEAHEAD_INSTANCE_TRIM, ev_learner_handle_trim, learner);
    // setup hole checking timer
    learner->hole_check_timer.tv_sec = 0;
    learner->hole_check_timer.tv_usec = 100000;
    learner->hole_check_event = evtimer_new(base, ev_epoch_learner_check_holes, learner);
    event_add(learner->hole_check_event, &learner->hole_check_timer);

    return learner;
}

struct ev_epoch_learner* ev_epoch_learner_init(const char* config_file, epoch_client_deliver_function f, void* arg, struct event_base* b){
    struct evpaxos_config* c = evpaxos_config_read(config_file);
    if (c == NULL) return NULL;

    struct writeahead_epoch_paxos_peers* peers = writeahead_epoch_paxos_peers_new(b, c);
    writeahead_epoch_paxos_peers_connect_to_acceptors(peers);
    //peers_connect_to_proposers(peers);

    struct ev_epoch_learner* l = ev_epoch_learner_init_internal(c, peers, f, arg);

    evpaxos_config_free(c);
    return l;
}

void ev_epoch_learner_free_internal(struct ev_epoch_learner** l) {
    event_free((**l).hole_check_event);
    epoch_learner_free(&(**l).learner);
    free(*l);
    *l = NULL;
}

void ev_epoch_learner_free(struct ev_epoch_learner** l) {
    writeahead_epoch_paxos_peers_free((**l).peers);
    ev_epoch_learner_free_internal(l);
}

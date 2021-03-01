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
#include <assert.h>

struct ev_epoch_learner
{
    struct epoch_learner* learner;
    epoch_client_deliver_function delfun;
    void* delarg;

    int comm_prop_num_trim;
    int comm_acc_num_trim;

    int comm_prop_num_chosen;
    int comm_acc_num_chosen;

    int min_chunks_missing;
    struct event* hole_check_event;
    struct timeval hole_check_timer;
    struct writeahead_epoch_paxos_peers* peers;
};

static void peer_send_epoch_paxos_message(struct writeahead_epoch_paxos_peer* p, void* arg) {
    send_epoch_paxos_message(writeahead_epoch_paxos_peer_get_buffer(p), arg);
}



static void ev_epoch_learner_check_holes(evutil_socket_t fd, short event, void *arg)
{
    struct epoch_paxos_message msg;
    struct ev_epoch_learner* l = arg;

    uint32_t chunks = l->min_chunks_missing;
    msg.type = WRITEAHEAD_REPEAT;

    if (epoch_learner_has_holes(l->learner, &msg.message_contents.repeat.from, &msg.message_contents.repeat.to)) {
        if ((msg.message_contents.repeat.to - msg.message_contents.repeat.from) > chunks) {
            msg.message_contents.repeat.to = msg.message_contents.repeat.from + chunks;
            // determine gaps then
            paxos_log_debug("Sending Repeat for Instances %u-%u", msg.message_contents.repeat.from,
                            msg.message_contents.repeat.to);
         writeahead_epoch_paxos_peers_foreach_acceptor(l->peers, peer_send_epoch_paxos_message, &msg);

        }
    } else {
            iid_t next_trim = epoch_learner_get_instance_to_trim(l->learner);
            iid_t current_trim =  epoch_learner_get_trim_instance(l->learner);
            if (next_trim > current_trim) {
                msg.type = WRITEAHEAD_INSTANCE_TRIM;
                msg.message_contents.trim = (struct paxos_trim) {next_trim};
                epoch_learner_set_trim_instance(l->learner, next_trim);

                paxos_log_debug("Sending Trim for Instance %u", next_trim);

                writeahead_epoch_paxos_peers_for_n_acceptor(l->peers, peer_send_epoch_paxos_message, &msg, l->comm_acc_num_trim);
                writeahead_epoch_paxos_peers_for_n_proposers(l->peers, peer_send_epoch_paxos_message, &msg, l->comm_prop_num_trim);
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
        paxos_log_debug("Destroyed delivered value");
    }
}

static void ev_epoch_learner_handle_accepted(struct writeahead_epoch_paxos_peer* p, struct epoch_paxos_message* msg, void* arg){
    struct ev_epoch_learner* l = arg;
    struct epoch_ballot_accepted* accepted = &msg->message_contents.epoch_ballot_accepted;
    struct epoch_paxos_message chosen;
    chosen.type = WRITEAHEAD_INSTANCE_CHOSEN_AT_EPOCH_BALLOT;
   // assert(accepted->accepted_epoch_ballot.ballot.number > 0);
    enum epoch_paxos_message_return_codes return_code = epoch_learner_receive_accepted(l->learner, accepted, &chosen.message_contents.instance_chosen_at_epoch_ballot);

    if (return_code == QUORUM_REACHED) {
       // assert(chosen.message_contents.instance_chosen_at_epoch_ballot.chosen_epoch_ballot.ballot.number >= 1);
        writeahead_epoch_paxos_peers_for_n_acceptor(l->peers, peer_send_epoch_paxos_message, &chosen, l->comm_acc_num_chosen);
    //    paxos_value_destroy(&chosen.message_contents.instance_chosen_at_epoch_ballot.chosen_value);
        writeahead_epoch_paxos_peers_for_n_proposers(l->peers, peer_send_epoch_paxos_message, &chosen, l->comm_prop_num_chosen);
   //     writeahead_epoch_paxos_peers_foreach_client(l->peers, peer_send_epoch_paxos_message, &chosen);

        ev_epoch_learner_deliver_next_closed(l);
    //    writeahead_epoch_paxos_message_destroy_contents(&chosen);
    }
}

static void ev_epoch_learner_handle_chosen(struct writeahead_epoch_paxos_peer* p, struct epoch_paxos_message* msg, void* arg) {
    struct ev_epoch_learner* l = arg;
    struct epoch_ballot_chosen* chosen = &msg->message_contents.instance_chosen_at_epoch_ballot;
   // assert(chosen->chosen_epoch_ballot.ballot.number > 0);
    if (epoch_learner_receive_epoch_ballot_chosen(l->learner, chosen) == MESSAGE_ACKNOWLEDGED)
        ev_epoch_learner_deliver_next_closed(l);
}

static void ev_learner_handle_trim(struct writeahead_epoch_paxos_peer* p, struct epoch_paxos_message* msg, void* arg) {
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

    learner->comm_acc_num_chosen = paxos_config.lnr_comm_all_acc_chosen ? evpaxos_acceptor_count(config) : 1;
    learner->comm_prop_num_chosen = paxos_config.lnr_comm_all_prop_chosen ? evpaxos_proposer_count(config) : 1;

    learner->comm_acc_num_trim = paxos_config.lnr_comm_all_acc_trim ? evpaxos_acceptor_count(config) : 1;
    learner->comm_prop_num_trim = paxos_config.lnr_comm_all_prop_trim ? evpaxos_proposer_count(config) : 1;

    learner->min_chunks_missing = paxos_config.lnr_missing_chunks_before_repeats;


    learner->hole_check_event = evtimer_new(base, ev_epoch_learner_check_holes, learner);
    event_add(learner->hole_check_event, &learner->hole_check_timer);

    return learner;
}

struct ev_epoch_learner *
ev_epoch_learner_init(const char *config, epoch_client_deliver_function f, void *arg, struct event_base *base,
                      int partner_id) {
    struct evpaxos_config* c = evpaxos_config_read(config);
    if (c == NULL) return NULL;

    struct writeahead_epoch_paxos_peers* peers = writeahead_epoch_paxos_peers_new(base, c);
    writeahead_epoch_paxos_peers_connect_to_acceptors(peers, partner_id);
    writeahead_epoch_paxos_peers_connect_to_proposers(peers, partner_id);
//    writeahead_epoch_paxos_peers_connect_to_other_learners(peers, learner_id);

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

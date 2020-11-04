//
// Created by Michael Davis on 07/04/2020.
//

#include "ev_epoch_paxos.h"
#include <stdlib.h>
#include <assert.h>
#include "string.h"
#include <event2/event.h>
#include "epoch_acceptor.h"
#include "writeahead_epoch_paxos_peers.h"
#include "epoch_paxos_message.h"
#include "ballot.h"
#include <paxos_types.h>
#include <random.h>
#include "performance_threshold_timer.h"
#include "ev_timer_threshold_timer_util.h"


struct ev_epoch_acceptor {
    struct writeahead_epoch_paxos_peers* peers;
    struct epoch_acceptor* acceptor;

    struct event* send_state_event;
    struct timeval send_state_timer;

    struct performance_threshold_timer* promise_timer;
    struct performance_threshold_timer* acceptance_timer;

    struct performance_threshold_timer* chosen_timer;
    uint32_t expected_value_size;
};

static void peer_send_epoch_paxos_message(struct writeahead_epoch_paxos_peer* p, void* arg){
    send_epoch_paxos_message(writeahead_epoch_paxos_peer_get_buffer(p), arg);
}

static void ev_epoch_acceptor_handle_standard_prepare(struct writeahead_epoch_paxos_peer* p, struct epoch_paxos_message* msg, void* arg) {
    struct epoch_paxos_message out;
    struct paxos_prepare* prepare = &msg->message_contents.standard_prepare;
    struct ev_epoch_acceptor* acceptor = arg;

    paxos_log_debug("Handing Standard Prepare for Instance %u, Ballot %u.%u",
            prepare->iid, prepare->ballot.number, prepare->ballot.proposer_id);

 //   performance_threshold_timer_begin_timing(acceptor->promise_timer);

    if (writeahead_epoch_acceptor_receive_prepare(acceptor->acceptor, prepare, &out)){
        if (out.type == WRITEAHEAD_INSTANCE_TRIM) {
            paxos_log_debug("Sending trim to proposer");
        }
        send_epoch_paxos_message(writeahead_epoch_paxos_peer_get_buffer(p), &out);
      //  writeahead_epoch_paxos_message_destroy_contents(&out);
    }
  //  ev_performance_timer_stop_check_and_clear_timer(acceptor->promise_timer, "Promise");
}

static void ev_epoch_acceptor_handle_epoch_ballot_prepare(struct writeahead_epoch_paxos_peer* p, struct epoch_paxos_message* msg, void* arg) {
    struct epoch_paxos_message out;
    struct epoch_ballot_prepare* prepare = &msg->message_contents.epoch_ballot_prepare;
    struct ev_epoch_acceptor* acceptor = arg;

    paxos_log_debug("Handing Epoch Ballot Prepare for Instance %u, Epoch Ballot %u.%u.%u",
                    prepare->instance,
                    prepare->epoch_ballot_requested.epoch,
                    prepare->epoch_ballot_requested.ballot.number, prepare->epoch_ballot_requested.ballot.proposer_id);

   // performance_threshold_timer_begin_timing(acceptor->promise_timer);

    if (writeahead_epoch_acceptor_receive_epoch_ballot_prepare(acceptor->acceptor, prepare, &out)){
        send_epoch_paxos_message(writeahead_epoch_paxos_peer_get_buffer(p), &out);
        writeahead_epoch_paxos_message_destroy_contents(&out);
    }
  //  ev_performance_timer_stop_check_and_clear_timer(acceptor->promise_timer, "Promise");

}

static void ev_epoch_acceptor_handle_epoch_ballot_accept(struct writeahead_epoch_paxos_peer* p, struct epoch_paxos_message* msg, void* arg) {
    struct epoch_paxos_message out;
    struct epoch_ballot_accept* accept = &msg->message_contents.epoch_ballot_accept;
    struct ev_epoch_acceptor* acceptor = arg;
    paxos_log_debug("Handing Epoch Ballot Accept for Instance %u, Epoch Ballot %u.%u.%u",
                    accept->instance,
                    accept->epoch_ballot_requested.epoch,
                    accept->epoch_ballot_requested.ballot.number, accept->epoch_ballot_requested.ballot.proposer_id);

 //   performance_threshold_timer_begin_timing(acceptor->acceptance_timer);

    if (writeahead_epoch_acceptor_receive_epoch_ballot_accept(acceptor->acceptor, accept, &out)) {
        if (out.type == WRITEAHEAD_EPOCH_BALLOT_ACCEPTED) {
            assert(out.message_contents.epoch_ballot_accepted.accepted_value.paxos_value_val != NULL);
            assert(out.message_contents.epoch_ballot_accepted.accepted_value.paxos_value_len > 1);
            assert(strncmp(out.message_contents.epoch_ballot_accepted.accepted_value.paxos_value_val, "", 2));

            writeahead_epoch_paxos_peers_foreach_client(acceptor->peers, peer_send_epoch_paxos_message, &out);
            paxos_log_debug("sent message");
        } else {
            send_epoch_paxos_message(writeahead_epoch_paxos_peer_get_buffer(p), &out);
        }
        writeahead_epoch_paxos_message_destroy_contents(&out);
    }
  //  ev_performance_timer_stop_check_and_clear_timer(acceptor->acceptance_timer, "Acceptance");

}

static void ev_epoch_acceptor_handle_epoch_notification(struct writeahead_epoch_paxos_peer* p, struct epoch_paxos_message* msg, void* arg){
    struct epoch_notification* notification = &msg->message_contents.epoch_notification;
    struct ev_epoch_acceptor* acceptor = arg;
    writeahead_epoch_acceptor_receive_epoch_notification(acceptor->acceptor, notification);
}

static void ev_epoch_acceptor_handle_repeat(struct writeahead_epoch_paxos_peer* p, struct epoch_paxos_message* msg, void* arg){
    struct paxos_repeat* repeat = &msg->message_contents.repeat;
    struct ev_epoch_acceptor* acceptor = arg;
    struct epoch_paxos_message out_msg;
    paxos_log_debug("Handle repeat for Instances %d-%d", repeat->from, repeat->to);
    for (iid_t instance = repeat->from; instance <= repeat->to; instance++) {
        if (writeahead_epoch_acceptor_receive_repeat(acceptor->acceptor, instance, &out_msg)) {
            //assert(out_msg.message_contents.epoch_ballot_accepted.accepted_epoch_ballot.ballot.number > 0);
            peer_send_epoch_paxos_message(p, &out_msg);
            writeahead_epoch_paxos_message_destroy_contents(&out_msg);
        }
    }
}

static void ev_epoch_acceptor_handle_epoch_ballot_chosen(struct writeahead_epoch_paxos_peer* p, struct epoch_paxos_message* msg, void* arg) {
    struct ev_epoch_acceptor* acceptor = arg;
    struct epoch_ballot_chosen* chosen = &msg->message_contents.instance_chosen_at_epoch_ballot;

   // performance_threshold_timer_begin_timing(acceptor->chosen_timer);

    writeahead_epoch_acceptor_receive_instance_chosen(acceptor->acceptor, chosen);
   // ev_performance_timer_stop_check_and_clear_timer(acceptor->chosen_timer, "Chosen");

}

static void ev_epoch_acceptor_handle_trim(struct writeahead_epoch_paxos_peer* p, struct epoch_paxos_message* msg, void* arg) {
    struct ev_epoch_acceptor* acceptor = arg;
    struct paxos_trim* trim = &msg->message_contents.trim;
    writeahead_epoch_acceptor_receive_trim(acceptor->acceptor, trim);
}

static void send_epoch_acceptor_state( int fd,  short ev, void* arg) {
    struct ev_epoch_acceptor* acceptor = arg;
    struct epoch_paxos_message msg;
    msg.type = WRITEAHEAD_ACCEPTOR_STATE;
    writeahead_epoch_acceptor_get_current_state(acceptor->acceptor, &msg.message_contents.state);
    writeahead_epoch_paxos_peers_foreach_client(acceptor->peers, peer_send_epoch_paxos_message, &msg);
    event_add(acceptor->send_state_event, &acceptor->send_state_timer);
}

struct ev_epoch_acceptor *
ev_epoch_acceptor_init_internal(int id, struct evpaxos_config *c, struct writeahead_epoch_paxos_peers *p) {
    struct ev_epoch_acceptor* acceptor = malloc(sizeof(struct ev_epoch_acceptor));
    struct epoch_notification epoch_notification;
    bool new_epoch;

    acceptor->acceptor = epoch_acceptor_new(id, &epoch_notification, &new_epoch);
    acceptor->peers = p;

    writeahead_epoch_paxos_peers_subscribe(p, WRITEAHEAD_EPOCH_BALLOT_ACCEPT, ev_epoch_acceptor_handle_epoch_ballot_accept, acceptor);
    writeahead_epoch_paxos_peers_subscribe(p, WRITEAHEAD_STANDARD_PREPARE, ev_epoch_acceptor_handle_standard_prepare, acceptor);
    writeahead_epoch_paxos_peers_subscribe(p, WRITEAHEAD_EPOCH_BALLOT_PREPARE, ev_epoch_acceptor_handle_epoch_ballot_prepare, acceptor);
    writeahead_epoch_paxos_peers_subscribe(p, WRITEAHEAD_INSTANCE_CHOSEN_AT_EPOCH_BALLOT,ev_epoch_acceptor_handle_epoch_ballot_chosen, acceptor );
    writeahead_epoch_paxos_peers_subscribe(p, WRITEAHEAD_EPOCH_NOTIFICATION, ev_epoch_acceptor_handle_epoch_notification, acceptor);
    writeahead_epoch_paxos_peers_subscribe(p, WRITEAHEAD_REPEAT, ev_epoch_acceptor_handle_repeat, acceptor);
    writeahead_epoch_paxos_peers_subscribe(p, WRITEAHEAD_INSTANCE_TRIM, ev_epoch_acceptor_handle_trim, acceptor);



    acceptor->promise_timer = get_promise_performance_threshold_timer_new();
    acceptor->acceptance_timer = get_acceptance_performance_threshold_timer_new();
    acceptor->chosen_timer = get_chosen_acceptor_performance_threshold_timer_new();

    struct event_base* base = writeahead_epoch_paxos_peers_get_event_base(p);
    acceptor->send_state_event = evtimer_new(base,send_epoch_acceptor_state, acceptor);
    acceptor->send_state_timer = (struct timeval) {1, 0};
    event_add(acceptor->send_state_event, &acceptor->send_state_timer);

    struct epoch_paxos_message msg = {
            .type = WRITEAHEAD_EPOCH_NOTIFICATION,
            .message_contents.epoch_notification = epoch_notification
    };
    writeahead_epoch_paxos_peers_foreach_client(acceptor->peers, peer_send_epoch_paxos_message, &msg);
    //todo send epoch_notifcation to all actors

    return acceptor;
}

struct ev_epoch_acceptor* ev_epoch_acceptor_init(int id, const char* config_fig, struct event_base* base) {
    struct evpaxos_config* config = evpaxos_config_read(config_fig);
    if (config == NULL) {
        return NULL;
    }

    int acceptor_count = evpaxos_acceptor_count(config);
    if (id < 0 || id >= acceptor_count) {
        paxos_log_error("Invalid acceptor id: %d.", id);
        paxos_log_error("Should be between 0 and %d", acceptor_count);
        evpaxos_config_free(config);
        return NULL;
    }

    struct writeahead_epoch_paxos_peers* peers = writeahead_epoch_paxos_peers_new(base, config);
    int port = evpaxos_acceptor_listen_port(config, id);
    if (writeahead_epoch_paxos_peers_listen(peers, port) == 0)
        return NULL;


    struct ev_epoch_acceptor* acceptor = ev_epoch_acceptor_init_internal(id, config, peers);

    evpaxos_config_free(config);
    return acceptor;
}

void ev_epoch_acceptor_free_internal(struct ev_epoch_acceptor** a){
    event_free((**a).send_state_event);
    writeahead_epoch_acceptor_free((**a).acceptor);
    free(*a);
    *a = NULL;
}

void ev_epoch_acceptor_free(struct ev_epoch_acceptor** a) {
    writeahead_epoch_paxos_peers_free((**a).peers);
    ev_epoch_acceptor_free_internal(a);
}

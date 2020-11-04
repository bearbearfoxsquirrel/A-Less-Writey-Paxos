//
// Created by Michael Davis on 06/04/2020.
//

#include "epoch_paxos_message.h"
#include "paxos.h"
#include "paxos_types_pack.h"
#include <string.h>
#include <paxos_types.h>

static int bufferevent_pack_data(void* data, const char* buf, size_t len) { //lazy shouldn't copy from std paxos
    struct bufferevent* bev = (struct bufferevent*)data;
    bufferevent_write(bev, buf, len);
    return 0;
}

void send_epoch_paxos_message(struct bufferevent* bev, struct epoch_paxos_message* msg){
    msgpack_packer* packer;
    packer = msgpack_packer_new(bev, bufferevent_pack_data);
    msgpack_pack_writeahead_epoch_paxos_message(packer, msg);
    msgpack_packer_free(packer);
}

void send_standard_paxos_prepare(struct bufferevent* bev, struct paxos_prepare* msg) {
    struct epoch_paxos_message send_msg = (struct epoch_paxos_message) {
            .type = WRITEAHEAD_STANDARD_PREPARE,
            .message_contents.standard_prepare = *msg
    };

    send_epoch_paxos_message(bev, &send_msg);
    paxos_log_debug("Send Standard Ballot Prepare for Instance %d Ballot %u.%u",
                    msg->iid,
                    msg->ballot.number, msg->ballot.proposer_id);
}

void send_epoch_paxos_trim(struct bufferevent* bev, struct paxos_trim* msg) {
    struct epoch_paxos_message send_msg = (struct epoch_paxos_message) {
            .type = WRITEAHEAD_INSTANCE_TRIM,
            .message_contents.trim = *msg
    };

    send_epoch_paxos_message(bev, &send_msg);
    paxos_log_debug("Send Trim for Instance", msg->iid);
}

void send_epoch_ballot_prepare(struct bufferevent* bev, struct epoch_ballot_prepare* msg){
    struct epoch_paxos_message send_msg = (struct epoch_paxos_message) {
        .type = WRITEAHEAD_EPOCH_BALLOT_PREPARE,
        .message_contents.epoch_ballot_prepare = *msg
    };

    send_epoch_paxos_message(bev, &send_msg);
    paxos_log_debug("Send Epoch Ballot Prepare for Instance %d Epoch Ballot %u.%u.%u",
            msg->instance,
            msg->epoch_ballot_requested.epoch,
            msg->epoch_ballot_requested.ballot.number, msg->epoch_ballot_requested.ballot.proposer_id);
}


void send_epoch_ballot_promise(struct bufferevent* bev, struct epoch_ballot_promise* msg){
    struct epoch_paxos_message send_msg = (struct epoch_paxos_message) {
        .type = WRITEAHEAD_EPOCH_BALLOT_PROMISE,
        .message_contents.epoch_ballot_promise = *msg
    };

    send_epoch_paxos_message(bev, &send_msg);

    paxos_log_debug("Send Epoch Ballot Promise for Instance %d Epoch Ballot %u.%u.%u",
                    msg->instance,
                    msg->promised_epoch_ballot.epoch,
                    msg->promised_epoch_ballot.ballot.number, msg->promised_epoch_ballot.ballot.proposer_id);
}


void send_epoch_ballot_accept(struct bufferevent* bev, struct epoch_ballot_accept* msg){
    struct epoch_paxos_message send_msg = (struct epoch_paxos_message) {
        .type = WRITEAHEAD_EPOCH_BALLOT_ACCEPT,
        .message_contents.epoch_ballot_accept = *msg
    };

    send_epoch_paxos_message(bev, &send_msg);
    paxos_log_debug("Send Epoch Ballot Accept for Instance %d Epoch Ballot %u.%u.%u",
                    msg->instance,
                    msg->epoch_ballot_requested.epoch,
                    msg->epoch_ballot_requested.ballot.number, msg->epoch_ballot_requested.ballot.proposer_id);
}


void send_epoch_paxos_accepted(struct bufferevent* bev, struct epoch_ballot_accepted* msg){
    struct epoch_paxos_message send_msg = (struct epoch_paxos_message) {
            .type = WRITEAHEAD_EPOCH_BALLOT_ACCEPTED,
            .message_contents.epoch_ballot_accepted = *msg
    };
    send_epoch_paxos_message(bev, &send_msg);

    paxos_log_debug("Send Epoch Ballot Accepted for Instance %d Epoch Ballot %u.%u.%u",
                    msg->instance,
                    msg->accepted_epoch_ballot.epoch,
                    msg->accepted_epoch_ballot.ballot.number, msg->accepted_epoch_ballot.ballot.proposer_id);
}

void send_epoch_paxos_preempted(struct bufferevent* bev, struct epoch_ballot_preempted* msg){
    struct epoch_paxos_message send_msg = (struct epoch_paxos_message) {
        .type = WRITEAHEAD_EPOCH_BALLOT_PREEMPTED,
        .message_contents.epoch_ballot_preempted = *msg
    };
    send_epoch_paxos_message(bev, &send_msg);

    paxos_log_debug("Send Epoch Ballot Preempted for Instance %d Epoch Ballot %u.%u.%u",
                    msg->instance,
                    msg->requested_epoch_ballot.epoch,
                    msg->requested_epoch_ballot.ballot.number, msg->requested_epoch_ballot.ballot.proposer_id);
}

void send_epoch_paxos_chosen(struct bufferevent* bev, struct epoch_ballot_chosen* chosen_msg){

    struct epoch_paxos_message send_msg = (struct epoch_paxos_message) {
        .type = WRITEAHEAD_INSTANCE_CHOSEN_AT_EPOCH_BALLOT,
        .message_contents.instance_chosen_at_epoch_ballot = *chosen_msg
    };
    send_epoch_paxos_message(bev, &send_msg);


    paxos_log_debug("Send Epoch Ballot Chosen for Instance %d Epoch Ballot %u.%u.%u",
                    chosen_msg->instance,
                    chosen_msg->chosen_epoch_ballot.epoch,
                    chosen_msg->chosen_epoch_ballot.ballot.number, chosen_msg->chosen_epoch_ballot.ballot.proposer_id);
}

void epoch_paxos_submit_client_value(struct bufferevent* bev, char *data, int size){
    struct epoch_paxos_message msg = {
            .type = WRITEAHEAD_CLIENT_VALUE,
            .message_contents.client_value.paxos_value_len = size,
            .message_contents.client_value.paxos_value_val = data };
    send_epoch_paxos_message(bev, &msg);
}

int recv_epoch_paxos_message(struct evbuffer* in, struct epoch_paxos_message* out){
    int rv = 0;
    char* buffer;
    size_t size, offset = 0;
    msgpack_unpacked msg;

    size = evbuffer_get_length(in);
    if (size == 0)
        return rv;

    msgpack_unpacked_init(&msg);
    buffer = (char*)evbuffer_pullup(in, size);

    if (msgpack_unpack_next(&msg, buffer, size, &offset)) {
        msgpack_unpack_writeahead_epoch_paxos_message(&msg.data, out);
        evbuffer_drain(in, offset);
        rv = 1;
    }

    msgpack_unpacked_destroy(&msg);
    return rv;
}


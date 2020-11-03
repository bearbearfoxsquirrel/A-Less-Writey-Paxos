//
// Created by Michael Davis on 02/11/2020.
//

#ifndef A_LESS_WRITEY_PAXOS_PROPOSER_MESSAGE_AND_RESPONSE_COUNTERS_H
#define A_LESS_WRITEY_PAXOS_PROPOSER_MESSAGE_AND_RESPONSE_COUNTERS_H


#include <count_logger.h>
#include <stdlib.h>
#include <string.h>

struct proposer_message_and_response_counters {
    struct count_logger* instances_opened_counter;
    struct count_logger* instances_closed_counter;

    struct count_logger* prepare_counter;
    struct count_logger* promise_counter;
    struct count_logger* preempted_prepare_counter;
    struct count_logger* promised_acked_counter;
    struct count_logger* ignored_promise_counter;

    struct count_logger* accept_counter;
    struct count_logger* accepted_counter;
    struct count_logger* preempted_accept_counter;
    struct count_logger* accepted_acked_counter;
    struct count_logger* accepted_ignored_counter;

    struct count_logger* preempted_ignored_counter;

    struct count_logger* chosen_counter;
    struct count_logger* chosen_acked_counter;
    struct count_logger* chosen_ignored_counter;

    struct count_logger* trim_counter;
    struct count_logger* trim_acked_counter;
    struct count_logger* trim_ignored_counter;

    struct count_logger* chosen_c_val_counter;

    struct count_logger* c_val_received;
    struct count_logger* c_val_proposed;
    struct count_logger* c_val_in_initial_queue;
    struct count_logger* c_val_reproposed;
    struct count_logger* c_val_requeued;
    struct count_logger* unique_c_val_proposed_but_not_yet_chosen;
    struct count_logger* c_val_in_retry_queue;
};


struct proposer_message_and_response_counters prop_msg_and_resp_counters_new();

void prop_msg_and_resp_counters_destroy(struct proposer_message_and_response_counters to_free);

void proposer_print_all_counters(struct proposer_message_and_response_counters counters);

#endif //A_LESS_WRITEY_PAXOS_PROPOSER_MESSAGE_AND_RESPONSE_COUNTERS_H

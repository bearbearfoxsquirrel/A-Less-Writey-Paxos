//
// Created by Michael Davis on 02/11/2020.
//

#include <paxos.h>
#include "proposer_message_and_response_counters.h"

static int real_str_len(char* str) {
    return (int) strlen(str) + 1;
}
struct proposer_message_and_response_counters prop_msg_and_resp_counters_new(){
    char* instances_open_name = "Instances opened";
    char* instances_closed_name = "Instances closed";
    char* prepare_counter_name = "Prepares made";
    char* promise_counter_name = "Promises received";
    char* preempt_prep_counter_name = "Promises preempted";
    char* promise_ack_counter_name = "Promises acknowledged";
    char* promise_ignored_counter_name = "Promises ignored";
    char* accept_counter_name = "Accepts made";
    char* accepted_counter_name = "Acceptances received";
    char* accept_preempted_name = "Accepts preempted";
    char* accepted_acked_name = "Accepted acknowledged";
    char* accepted_ignored_name = "Accepted ignored";
    char* preempted_ignored_name = "Preempted ignored";
    char* chosen_name = "Chosens received";
    char* chosen_acked_name = "Chosens acknowledged";
    char* chosen_ignored_name = "Chosens ignored";
    char* trim_name = "Trims received";
    char* trim_acked_name = "Trims acknowledged";
    char* trim_ignored_name = "Trims ignored";
    char* chosen_c_val_name = "Client values chosen";
    char* c_val_received = "Client values received";
    char* c_val_in_queue_name = "Client values in queue";
    char* c_val_proposed = "Client values proposed";
    char* c_val_reproposed_name = "Client values reproposed";
    char* c_val_requeued_name = "Client values requeued (either trimmed or preempted)";
    char* c_val_in_transit_name = "Client values in transit (Proposed but not known to be chosen or preempted)";
    char* c_val_in_reprop_queue_name = "Client values in repropsing queue";

    struct proposer_message_and_response_counters counters = {
            .preempted_prepare_counter = count_logger_new(preempt_prep_counter_name, real_str_len(preempt_prep_counter_name)),
            .instances_closed_counter = count_logger_new(instances_closed_name, real_str_len(instances_closed_name)),
            .prepare_counter = count_logger_new(prepare_counter_name, real_str_len(prepare_counter_name)),
            .promise_counter = count_logger_new(promise_counter_name, real_str_len(promise_counter_name)),
            .instances_opened_counter = count_logger_new(instances_open_name, real_str_len(instances_open_name)),
            .promised_acked_counter = count_logger_new(promise_ack_counter_name, real_str_len(promise_counter_name)),
            .ignored_promise_counter = count_logger_new(promise_ignored_counter_name, real_str_len(promise_ignored_counter_name)),
            .accept_counter = count_logger_new(accept_counter_name, real_str_len(accept_counter_name)),
            .accepted_counter = count_logger_new(accepted_counter_name, real_str_len(accepted_counter_name)),
            .preempted_accept_counter = count_logger_new(accept_preempted_name, real_str_len(accept_preempted_name)),
            .accepted_acked_counter = count_logger_new(accepted_acked_name, real_str_len(accepted_acked_name)),
            .accepted_ignored_counter = count_logger_new(accepted_ignored_name, real_str_len(accepted_ignored_name)),
            .preempted_ignored_counter = count_logger_new(preempted_ignored_name, real_str_len(preempted_ignored_name)),
            .chosen_counter = count_logger_new(chosen_name, real_str_len(chosen_name)),
            .chosen_acked_counter = count_logger_new(chosen_acked_name, real_str_len(chosen_acked_name)),
            .chosen_ignored_counter = count_logger_new(chosen_ignored_name, real_str_len(chosen_ignored_name)),
            .trim_counter = count_logger_new(trim_name, real_str_len(trim_name)),
            .trim_acked_counter = count_logger_new(trim_acked_name, real_str_len(trim_acked_name)),
            .trim_ignored_counter = count_logger_new(trim_ignored_name, real_str_len(trim_ignored_name)),
            .chosen_c_val_counter = count_logger_new(chosen_c_val_name, real_str_len(chosen_c_val_name)),
            .c_val_received = count_logger_new(c_val_received, real_str_len(c_val_received)),
            .c_val_in_initial_queue = count_logger_new(c_val_in_queue_name, real_str_len(c_val_in_queue_name)),
            .c_val_proposed = count_logger_new(c_val_proposed, real_str_len(c_val_proposed)),
            .c_val_reproposed = count_logger_new(c_val_reproposed_name, real_str_len(c_val_reproposed_name)),
            .c_val_requeued = count_logger_new(c_val_requeued_name, real_str_len(c_val_requeued_name)),
            .unique_c_val_proposed_but_not_yet_chosen = count_logger_new(c_val_in_transit_name, real_str_len(c_val_in_transit_name)),
            .c_val_in_retry_queue = count_logger_new(c_val_in_reprop_queue_name, real_str_len(c_val_in_reprop_queue_name)),
    };
    return counters;
}

void proposer_print_all_counters(struct proposer_message_and_response_counters counters) {
    paxos_log_debug("\n"
                    "Stats from the Proposer"
                        "\n -----------------------------------------------------------");
    count_logger_print_and_clear(counters.instances_opened_counter);
    count_logger_print_and_clear(counters.instances_closed_counter);
    count_logger_print_and_clear(counters.prepare_counter);
    count_logger_print_and_clear(counters.promise_counter);
    count_logger_print_and_clear(counters.preempted_prepare_counter);
    count_logger_print_and_clear(counters.promised_acked_counter);
    count_logger_print_and_clear(counters.ignored_promise_counter);

    count_logger_print_and_clear(counters.accept_counter);
    count_logger_print_and_clear(counters.accepted_counter);
    count_logger_print_and_clear(counters.preempted_accept_counter);
    count_logger_print_and_clear(counters.accepted_acked_counter);
    count_logger_print_and_clear(counters.accepted_ignored_counter);

    count_logger_print_and_clear(counters.chosen_counter);
    count_logger_print_and_clear(counters.chosen_acked_counter);
    count_logger_print_and_clear(counters.chosen_ignored_counter);

    count_logger_print_and_clear(counters.trim_counter);
    count_logger_print_and_clear(counters.trim_acked_counter);
    count_logger_print_and_clear(counters.trim_ignored_counter);

    count_logger_print_and_clear(counters.c_val_received);
    count_logger_print_and_clear(counters.c_val_proposed);
    count_logger_print_and_clear(counters.c_val_requeued);
    count_logger_print_and_clear(counters.c_val_reproposed);
    count_logger_print(counters.c_val_in_initial_queue);
    count_logger_print(counters.unique_c_val_proposed_but_not_yet_chosen);

    count_logger_print_and_clear(counters.chosen_c_val_counter);
    paxos_log_debug("----------------------------------------------------\n");
}

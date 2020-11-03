//
// Created by Michael Davis on 02/11/2020.
//

#include <paxos.h>
#include "proposer_message_and_response_counters.h"


struct proposer_message_and_response_counters prop_msg_and_resp_counters_new(){
    struct proposer_message_and_response_counters counters;

    char* instances_open_name = "Instances opened";
    counters.instances_opened_counter = count_logger_new(instances_open_name, strlen(instances_open_name));
    char* instances_closed_name = "Instances closed";
    counters.instances_closed_counter = count_logger_new(instances_closed_name, strlen(instances_closed_name));

    char* prepare_counter_name = "Prepares made";
    counters.prepare_counter = count_logger_new(prepare_counter_name, strlen(prepare_counter_name));
    char* promise_counter_name = "Promises received";
    counters.promise_counter = count_logger_new(promise_counter_name, strlen(promise_counter_name));
    char* preempt_prep_counter_name = "Promises preempted";
    counters.preempted_prepare_counter = count_logger_new(preempt_prep_counter_name, strlen(preempt_prep_counter_name));
    char* promise_ack_counter_name = "Promises acknowledged";
    counters.promised_acked_counter = count_logger_new(promise_ack_counter_name, strlen(promise_counter_name));
    char* promise_ignored_counter_name = "Promises ignored";
    counters.ignored_promise_counter = count_logger_new(promise_ignored_counter_name, strlen(promise_ignored_counter_name));

    char* accept_counter_name = "Accepts made";
    counters.accept_counter = count_logger_new(accept_counter_name, strlen(accept_counter_name));
    char* accepted_counter_name = "Acceptances received";
    counters.accepted_counter = count_logger_new(accepted_counter_name, strlen(accepted_counter_name));
    char* accept_preempted_name = "Accepts preempted";
    counters.preempted_accept_counter = count_logger_new(accept_preempted_name, strlen(accept_preempted_name));
    char* accepted_acked_name = "Accepted acknowledged";
    counters.accepted_acked_counter = count_logger_new(accepted_acked_name, strlen(accepted_acked_name));
    char* accepted_ignored_name = "Accepted ignored";
    counters.accepted_ignored_counter = count_logger_new(accepted_ignored_name, strlen(accepted_ignored_name));

    char* preempted_ignored_name = "Preempted ignored";
    counters.preempted_ignored_counter = count_logger_new(preempted_ignored_name, strlen(preempted_ignored_name));

    char* chosen_name = "Chosens received";
    counters.chosen_counter = count_logger_new(chosen_name, strlen(chosen_name));
    char* chosen_acked_name = "Chosens acknowledged";
    counters.chosen_acked_counter = count_logger_new(chosen_acked_name, strlen(chosen_acked_name));
    char* chosen_ignored_name = "Chosens ignored";
    counters.chosen_ignored_counter = count_logger_new(chosen_ignored_name, strlen(chosen_ignored_name));

    char* trim_name = "Trims received";
    counters.trim_counter = count_logger_new(trim_name, strlen(trim_name));
    char* trim_acked_name = "Trims acknowledged";
    counters.trim_acked_counter = count_logger_new(trim_acked_name, strlen(trim_acked_name));
    char* trim_ignored_name = "Trims ignored";
    counters.trim_ignored_counter = count_logger_new(trim_ignored_name, strlen(trim_ignored_name));

    char* chosen_c_val_name = "Client values chosen";
    counters.chosen_c_val_counter = count_logger_new(chosen_c_val_name, strlen(chosen_c_val_name));

    char* c_val_received = "Client values received";
    counters.c_val_received = count_logger_new(c_val_received, strlen(c_val_received));
    char* c_val_in_queue_name = "Client values in queue";
    counters.c_val_in_initial_queue = count_logger_new(c_val_in_queue_name, strlen(c_val_in_queue_name));
    char* c_val_proposed = "Client values proposed";
    counters.c_val_proposed = count_logger_new(c_val_proposed, strlen(c_val_proposed));
    char* c_val_reproposed_name = "Client values reproposed";
    counters.c_val_reproposed = count_logger_new(c_val_reproposed_name, strlen(c_val_reproposed_name));
    char* c_val_requeued_name = "Client values requeued (either trimmed or preempted)";
    counters.c_val_requeued = count_logger_new(c_val_requeued_name, strlen(c_val_requeued_name));
    char* c_val_in_transit_name = "Client values in transit (Proposed but not known to be chosen or preempted)";
    counters.unique_c_val_proposed_but_not_yet_chosen = count_logger_new(c_val_in_transit_name, strlen(c_val_in_transit_name));
    char* c_val_in_reprop_queue_name = "Client values in repropsing queue";
    counters.c_val_in_retry_queue = count_logger_new(c_val_in_reprop_queue_name, strlen(c_val_in_reprop_queue_name));


    return counters;
}

void proposer_print_all_counters(struct proposer_message_and_response_counters counters) {
    paxos_log_debug("\n-----------------------------------------------------------\n"
                    "                        Stats from the Proposer "
                        "\n ----------------------------------------------------------- ");
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

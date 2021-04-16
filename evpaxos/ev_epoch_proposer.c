//
// Created by Michael Davis on 06/04/2020.
//

#include "epoch_proposer.h"
#include <unistd.h>

#include "epoch_paxos_message.h"
#include "round_robin_allocator.h"
#include "ev_epoch_paxos.h"
#include <string.h>
#include "khash.h"
#include <stdlib.h>
#include <event2/event.h>
#include "paxos_message_conversion.h"
#include "paxos_value.h"
#include <paxos_types.h>
#include <assert.h>
#include <writeahead_epoch_paxos_peers.h>
#include "time.h"
#include "backoff_implementations.h"
#include "backoff_manager.h"
#include "random.h"
#include "stdio.h"
#include "timeout.h"
#include "performance_threshold_timer.h"
#include "epoch_ballot.h"
#include "ev_timer_threshold_timer_util.h"



//KHASH_MAP_INIT_INT(retries, iid_t*)

struct ev_epoch_proposer {
    int acceptor_count;
    struct timeval* last_heard_from_acceptor;

    int max_num_open_instances;
    struct epoch_proposer* proposer;
    struct writeahead_epoch_paxos_peers* peers;


    struct timeval timeout_time;
    struct event* timeout_event;

    struct backoff_manager* backoff_manager;

    struct round_robin_allocator* round_robin_allocator;

    struct event* random_seed_event;
    struct timeval random_seed_time;

    struct event* count_timer_print_event;
    struct timeval count_timer_print_time;

    iid_t* instances_to_skip;
    int num_instances_to_skip;

    struct event* proposer_state_event;
    struct timeval proposer_state_time;

    struct timeval acceptor_timeout_check_time;
    struct event* acceptor_timeout_check_event;

    struct timeval try_accept_time;
    struct event* try_accept_event;

    struct performance_threshold_timer* preprare_timer;
    struct performance_threshold_timer* accept_timer;
    struct performance_threshold_timer* preempt_timer;
    struct performance_threshold_timer* accepted_timer;
    struct performance_threshold_timer *chosen_timer;

  //  struct timeval proposers_value_chosen_at;

    
    
    struct performance_threshold_timer *promise_timer;
    int proposer_count;
    int id;
};


size_t ev_proposer_get_max_instances_to_skip(struct ev_epoch_proposer *proposer) {
    return proposer->proposer_count  * proposer->max_num_open_instances;
}

static bool check_and_add_instance_chosen_to_skip(struct ev_epoch_proposer* proposer, iid_t instance) {
   // assert(ev_proposer_get_max_instances_to_skip(proposer) >= proposer->num_instances_to_skip);
    assert(epoch_proposer_get_current_instance(proposer->proposer) <= instance);
    for (int i = 0; i < proposer->num_instances_to_skip; i++) {
        if (proposer->instances_to_skip[i] == instance)
            return false;
    }

    proposer->instances_to_skip[proposer->num_instances_to_skip] = instance;
    proposer->num_instances_to_skip += 1;
    return true;
}

static void trim_instances_to_skip(struct ev_epoch_proposer* proposer, iid_t trim) {
    for (int i = 0; i < proposer->num_instances_to_skip; i++) {
        if (proposer->instances_to_skip[i] <= trim) {
            proposer->num_instances_to_skip -= 1;
            proposer->instances_to_skip[i] = -1;
        }
    }
}

static bool should_skip_instance(struct ev_epoch_proposer* proposer, iid_t cmp){
    if (proposer->num_instances_to_skip > 0) {
        for (int i = 0; i < proposer->num_instances_to_skip; i++) {
            if (proposer->instances_to_skip[i] == cmp) {
                paxos_log_info("Skipping instance %u, known to be chosen", cmp);
                return true;
            }
        }
    }
    return false;
}

static void
peer_send_chosen(struct writeahead_epoch_paxos_peer* p, void* arg){
    send_epoch_paxos_chosen(writeahead_epoch_paxos_peer_get_buffer(p), arg);
}

static void
peer_send_standard_prepare(struct writeahead_epoch_paxos_peer* p, void* arg)
{
    send_standard_paxos_prepare(writeahead_epoch_paxos_peer_get_buffer(p), arg);
}


static void
peer_send_epoch_ballot_prepare(struct writeahead_epoch_paxos_peer* p, void* arg)
{
    send_epoch_ballot_prepare(writeahead_epoch_paxos_peer_get_buffer(p), arg);
}



static void
peer_send_epoch_ballot_accept(struct writeahead_epoch_paxos_peer* p, void* arg)
{
    send_epoch_ballot_accept(writeahead_epoch_paxos_peer_get_buffer(p), arg);
}

static void
peer_send_trim(struct writeahead_epoch_paxos_peer* p, void* arg) {
    send_epoch_paxos_trim(writeahead_epoch_paxos_peer_get_buffer(p), arg);
}


struct retry {
    struct ev_epoch_proposer* proposer;
    struct epoch_paxos_prepares* prepare;
};

static void ev_epoch_proposer_try_begin_new_instances(struct ev_epoch_proposer* p);



static void ev_epoch_proposer_try_accept(struct ev_epoch_proposer* p) {
    paxos_log_debug("Beginning to try Accept Phase of one or more Instances");
    struct epoch_ballot_accept accept;
  //  performance_threshold_timer_begin_timing(p->accept_timer);


    while (epoch_proposer_try_accept(p->proposer, &accept)) {
       // assert(&accept.value_to_accept != NULL);
       // assert(accept.value_to_accept.paxos_value_val != NULL);
       // assert(accept.value_to_accept.paxos_value_len > 0);
   //    // assert(strncmp(accept.value_to_accept.paxos_value_val, "", 2));

            writeahead_epoch_paxos_peers_for_n_acceptor(p->peers, peer_send_epoch_ballot_accept, &accept,
                                                        paxos_config.group_2);
            //todo delete accept value
    }
}


static void ev_epoch_proposer_try_higher_ballot( evutil_socket_t fd,  short event, void* arg) {
    paxos_log_debug("Triggered event to try some next Epoch Ballot");
    struct retry* args = arg;
    struct ev_epoch_proposer* proposer = args->proposer;

    switch(args->prepare->type) {
        case STANDARD_PREPARE:
         //   assert(args->prepare->standard_prepare.iid > 0);
       //     assert(args->prepare->standard_prepare.ballot.number > 0);
         //   assert(args->prepare->standard_prepare.ballot.proposer_id == proposer->id);
            if  (epoch_proposer_is_instance_pending(proposer->proposer, args->prepare->standard_prepare.iid)) { // may have been chosen or trimmed by the time backoff is over
            //    paxos_log_debug("Trying to Prepare next Standard Ballot for Instance %u with Ballot %u.%u",
             //                   args->prepare->standard_prepare.iid,
              //                  args->prepare->standard_prepare.ballot.number,
              //                  args->prepare->standard_prepare.ballot.proposer_id);
                writeahead_epoch_paxos_peers_for_n_acceptor(proposer->peers, peer_send_standard_prepare, &args->prepare->standard_prepare,
                                                            paxos_config.group_1);

            } else {
                backoff_manager_close_backoff_if_exists(proposer->backoff_manager, args->prepare->standard_prepare.iid);
                ev_epoch_proposer_try_begin_new_instances(proposer);
            }
                break;

            case EXPLICIT_EPOCH_PREPARE:
             //   assert(args->prepare->explicit_epoch_prepare.instance > 0);
              //  assert(args->prepare->explicit_epoch_prepare.epoch_ballot_requested.ballot.number >0);
              //  assert(args->prepare->explicit_epoch_prepare.epoch_ballot_requested.ballot.proposer_id == proposer->id);
                if  (epoch_proposer_is_instance_pending(proposer->proposer, args->prepare->explicit_epoch_prepare.instance)) { // may have been chosen or trimmed by the time backoff is over
                //    paxos_log_debug("Trying to Prepare next Epoch Ballot for Instance %u with Epoch Ballot %u.%u.%u",
                //                    args->prepare->explicit_epoch_prepare.instance,
                 //                   args->prepare->explicit_epoch_prepare.epoch_ballot_requested.epoch,
                 //                   args->prepare->explicit_epoch_prepare.epoch_ballot_requested.ballot.number,
                 //                   args->prepare->explicit_epoch_prepare.epoch_ballot_requested.ballot.proposer_id);
                    writeahead_epoch_paxos_peers_for_n_acceptor(proposer->peers, peer_send_epoch_ballot_prepare, &args->prepare->explicit_epoch_prepare,
                                                                paxos_config.group_1);

                } else {
                    backoff_manager_close_backoff_if_exists(proposer->backoff_manager, args->prepare->standard_prepare.iid);
                    ev_epoch_proposer_try_begin_new_instances(proposer);
                }
            break;
        }

    free(args->prepare);
    free(args);
    //ev_epoch_proposer_try_accept(proposer);
}


static void ev_epoch_proposer_try_begin_new_instances(struct ev_epoch_proposer* p) {
    paxos_log_debug("Beginning to try open new Instances");
    int number_of_instances_to_open = p->max_num_open_instances - (epoch_proposer_prepare_count(p->proposer) + epoch_proposer_acceptance_count(p->proposer));
    paxos_log_debug("Opening %u new Instances", number_of_instances_to_open);

    while (number_of_instances_to_open > 0) {

        struct epoch_paxos_prepares* prepare = malloc(sizeof(*prepare));
        iid_t current_instance = epoch_proposer_get_current_instance(p->proposer);

        if (!should_skip_instance(p, current_instance)) {
            // assert(current_instance != INVALID_INSTANCE);
            paxos_log_debug("Current Proposing Instance is %u", current_instance);

            struct epoch_ballot initial_ballot = {epoch_proposer_get_current_known_epoch(p->proposer),
                                                  round_robin_allocator_get_ballot(p->round_robin_allocator,
                                                                                   current_instance)};

            bool new_instance_to_prepare = epoch_proposer_try_to_start_preparing_instance(p->proposer, current_instance,
                                                                                          initial_ballot, prepare);
            if (new_instance_to_prepare) {
                number_of_instances_to_open--;
                bool should_delay = round_robin_allocator_should_delay_proposal(p->round_robin_allocator,
                                                                                current_instance);
           //     if (!should_delay) {
                    switch (prepare->type) {
                        case STANDARD_PREPARE:
                            //         paxos_log_debug("Sending Standard Prepare for Instance %u, Ballot %u.%u",
                            //                         prepare.standard_prepare.iid, prepare.standard_prepare.ballot.number,
                            //                         prepare.standard_prepare.ballot.proposer_id);
                            writeahead_epoch_paxos_peers_for_n_acceptor(p->peers, peer_send_standard_prepare,
                                                                        &prepare->standard_prepare,
                                                                        paxos_config.group_1);
                            //free(prepare);
                            break;
                        case EXPLICIT_EPOCH_PREPARE:
                            //    paxos_log_debug("Sending Epoch Ballot Prepare for Instance %u, Epoch Ballot %u.%u.%u",
                            //                   prepare.explicit_epoch_prepare.instance,
                            //                   prepare.explicit_epoch_prepare.epoch_ballot_requested.epoch,
                            //                  prepare.explicit_epoch_prepare.epoch_ballot_requested.ballot.number,
                            //                  prepare.explicit_epoch_prepare.epoch_ballot_requested.ballot.proposer_id);
                            writeahead_epoch_paxos_peers_for_n_acceptor(p->peers, peer_send_epoch_ballot_prepare,
                                                                        &prepare->explicit_epoch_prepare,
                                                                        paxos_config.group_1);
                            //    free(prepare);
                            break;
                    }
                } else {
                    const struct timeval *current_backoff = backoff_manager_get_backoff(p->backoff_manager,
                                                                                        current_instance);//(struct timeval) {0, random_between(5000, 10000)};
                    struct retry *retry_args = malloc(sizeof(struct retry));
                    retry_args->proposer = p;
                    retry_args->prepare = prepare;
                    struct event *ev = evtimer_new(writeahead_epoch_paxos_peers_get_event_base(p->peers),
                                                   ev_epoch_proposer_try_higher_ballot,
                                                   retry_args);
                    event_add(ev, current_backoff);
                }
            }
     //   }
        epoch_proposer_next_instance(p->proposer);
    }
}




static void ev_epoch_proposer_handle_promise(struct writeahead_epoch_paxos_peer* p, struct epoch_paxos_message* msg, void* arg) {
    paxos_log_debug("Handling Promise message");
    struct ev_epoch_proposer* proposer = arg;
    struct epoch_ballot_promise* promise = &msg->message_contents.epoch_ballot_promise;
    struct epoch_paxos_message ret_msg = {0};

    if (promise->promised_epoch_ballot.ballot.proposer_id != epoch_proposer_get_id(proposer->proposer))
        return;

   // performance_threshold_timer_begin_timing(proposer->promise_timer);
    enum epoch_paxos_message_return_codes return_code = epoch_proposer_receive_promise(proposer->proposer, promise, &ret_msg);
  //  ev_performance_timer_stop_check_and_clear_timer(proposer->promise_timer, "Promise");

    if (return_code == EPOCH_PREEMPTED) {
       // assert(next_prepare.instance != INVALID_INSTANCE);
        writeahead_epoch_paxos_peers_for_n_acceptor(proposer->peers, peer_send_epoch_ballot_prepare, &ret_msg.message_contents.epoch_ballot_prepare, paxos_config.group_1);
    } else if (return_code == QUORUM_REACHED) {
        ev_epoch_proposer_try_accept(proposer);
    } else if (return_code == INSTANCE_CHOSEN) {
//        writeahead_epoch_paxos_peers_for_n_acceptor(proposer->peers, peer_send_chosen, &ret_msg.message_contents.instance_chosen_at_epoch_ballot, paxos_config.group_1);
//writeahead_epoch_paxos_message_destroy_contents(&ret_msg);
    }
}

static void ev_epoch_proposer_handle_accepted(struct writeahead_epoch_paxos_peer* p, struct epoch_paxos_message* msg, void* arg){
    paxos_log_debug("Handling Accepted message");
    struct ev_epoch_proposer* proposer = arg;
    struct epoch_ballot_accepted* accepted = &msg->message_contents.epoch_ballot_accepted;
    struct epoch_ballot_chosen chosen_msg;

    if (accepted->accepted_epoch_ballot.ballot.proposer_id != epoch_proposer_get_id(proposer->proposer)) {
        return; //todo check for chosen and preempted
    }

   // performance_threshold_timer_begin_timing(proposer->accepted_timer);
    enum epoch_paxos_message_return_codes return_code = epoch_proposer_receive_accepted(proposer->proposer, accepted, &chosen_msg);
  //  ev_performance_timer_stop_check_and_clear_timer(proposer->accepted_timer, "Accepted");

    if (return_code == QUORUM_REACHED) {

     //   writeahead_epoch_paxos_peers_for_n_actual_clients(proposer->peers, peer_send_chosen, &chosen_msg, 1);
       // assert(chosen_msg.instance == accepted->instance);
       // assert(epoch_ballot_equal(chosen_msg.chosen_epoch_ballot, accepted->accepted_epoch_ballot));
       // assert(is_values_equal(chosen_msg.chosen_value, accepted->accepted_value));

        //send chosen
        //  ev_epoch_proposer_try_begin_new_instances(proposer);
        //  } else {
        ev_epoch_proposer_try_begin_new_instances(proposer);
    }
       // ev_epoch_proposer_try_accept(proposer);
  //  }
}

static void ev_epoch_proposer_handle_chosen(struct writeahead_epoch_paxos_peer* p, struct epoch_paxos_message* msg, void* arg) {
    paxos_log_debug("Handling Chosen message");
    struct ev_epoch_proposer* proposer = arg;
    struct epoch_ballot_chosen* chosen_msg = &msg->message_contents.instance_chosen_at_epoch_ballot;

//    performance_threshold_timer_begin_timing(proposer->chosen_timer);
    enum epoch_paxos_message_return_codes return_code = epoch_proposer_receive_chosen(proposer->proposer, chosen_msg);
//    ev_performance_timer_stop_check_and_clear_timer(proposer->chosen_timer, "Chosen");

    if (return_code == MESSAGE_ACKNOWLEDGED) {
        paxos_log_info("Choosing instance took %u attempts", chosen_msg->chosen_epoch_ballot.ballot.number / paxos_config.max_ballot_increment);
        //backoff_manager_close_backoff_if_exists(proposer->backoff_manager, chosen_msg->instance);
        ev_epoch_proposer_try_begin_new_instances(proposer);
    }

    // todo add check for more than trim
    if (epoch_proposer_get_current_instance(proposer->proposer) < chosen_msg->instance) {
        check_and_add_instance_chosen_to_skip(proposer, chosen_msg->instance);
    }


  //  ev_epoch_proposer_try_begin_new_instances(proposer);
//   ev_epoch_proposer_try_accept(proposer); //needed?
}


static void ev_epoch_proposer_handle_preempted(struct writeahead_epoch_paxos_peer* p, struct epoch_paxos_message* msg, void* arg) {
    paxos_log_debug("Handling Preempted message");
    struct ev_epoch_proposer* proposer = arg;
    struct epoch_ballot_preempted preempted_msg = msg->message_contents.epoch_ballot_preempted;

    if (preempted_msg.requested_epoch_ballot.ballot.proposer_id != epoch_proposer_get_id(proposer->proposer)) return;

    struct epoch_ballot_prepare next_prepare;// = malloc(sizeof(*next_prepare));
   // assert(epoch_ballot_greater_than(preempted_msg.acceptors_current_epoch_ballot, preempted_msg.requested_epoch_ballot));

 //   performance_threshold_timer_begin_timing(proposer->preempt_timer);
    enum epoch_paxos_message_return_codes return_code = epoch_proposer_receive_preempted(proposer->proposer, &preempted_msg, &next_prepare);
  //  ev_performance_timer_stop_check_and_clear_timer(proposer->preempt_timer, "Preempt");

    if (return_code == BALLOT_PREEMPTED) {
       // assert(next_prepare.instance != 0);
       // assert(epoch_ballot_greater_than(next_prepare.epoch_ballot_requested, preempted_msg.acceptors_current_epoch_ballot));
        
        const struct timeval* current_backoff = backoff_manager_get_backoff(proposer->backoff_manager, preempted_msg.instance);
      //  paxos_log_debug("Trying next Ballot for Instance %u, %u.%u in %ld microseconds",
       //         next_prepare->instance,
        //        next_prepare->epoch_ballot_requested.ballot.number,
         //       next_prepare->epoch_ballot_requested.ballot.proposer_id,
         //       current_backoff->tv_usec);

        struct retry* retry_args = malloc(sizeof(struct retry));
        struct epoch_paxos_prepares* prepare = malloc(sizeof(*prepare));
        prepare->type = EXPLICIT_EPOCH_PREPARE;
        prepare->explicit_epoch_prepare = next_prepare;
        *retry_args = (struct retry) {.proposer = proposer, .prepare = prepare};
        struct event* ev = evtimer_new(writeahead_epoch_paxos_peers_get_event_base(proposer->peers), ev_epoch_proposer_try_higher_ballot, retry_args);
        event_add(ev, current_backoff);
        paxos_log_debug("Next proposal now queued");
    } else if (return_code == EPOCH_PREEMPTED) {
        writeahead_epoch_paxos_peers_for_n_acceptor(proposer->peers, peer_send_epoch_ballot_prepare, &next_prepare, paxos_config.quorum_1);
    }
}


static void
ev_epoch_proposer_handle_trim(struct writeahead_epoch_paxos_peer* p, struct epoch_paxos_message* msg, void* arg) {
    paxos_log_debug("Handling Trim message");
    struct ev_epoch_proposer* proposer = arg;
    struct paxos_trim* trim_msg = &msg->message_contents.trim;
    enum epoch_paxos_message_return_codes returned = epoch_proposer_receive_trim(proposer->proposer, trim_msg);

    if (returned == FALLEN_BEHIND) {
        paxos_log_info("I have fallen behind");
    }

    backoff_manager_close_less_than_or_equal(proposer->backoff_manager, trim_msg->iid);
    trim_instances_to_skip(proposer, trim_msg->iid);
    ev_epoch_proposer_try_begin_new_instances(proposer);
}

static void
ev_epoch_proposer_handle_client_value(struct writeahead_epoch_paxos_peer* p, struct epoch_paxos_message* msg, void* arg)
{
    paxos_log_debug("Handling Client Value message");
    struct ev_epoch_proposer* proposer = arg;
    struct paxos_value* v = &msg->message_contents.client_value;

   // assert(v->paxos_value_len > 1);
   // assert(v->paxos_value_val != NULL);
   // assert(v->paxos_value_val != "");


    epoch_proposer_add_paxos_value_to_queue(proposer->proposer, *v);
    ev_epoch_proposer_try_accept(proposer);
}

static bool all_up(struct ev_epoch_proposer* proposer) {
    for (int i = 0; i < proposer->acceptor_count; i++) {
        struct timeval now;
        gettimeofday(&now, NULL);

        struct timeval res;
        timersub(&now, &proposer->last_heard_from_acceptor[i], &res);

        struct timeval cmp = {paxos_config.acceptor_timeout, 0};
        if (timercmp(&res, &cmp, >)) {
            return false;
        }
    }
    return true;
}


static void
ev_epoch_proposer_handle_acceptor_state(struct writeahead_epoch_paxos_peer* p, struct epoch_paxos_message* msg, void* arg)
{
    paxos_log_debug("Handing Acceptor State message");
    struct ev_epoch_proposer* proposer = arg;
    struct writeahead_epoch_acceptor_state* acc_state = &msg->message_contents.acceptor_state;

    epoch_proposer_receive_acceptor_state(proposer->proposer, acc_state);

    trim_instances_to_skip(proposer, acc_state->standard_acceptor_state.trim_iid);
  //  ev_epoch_proposer_try_accept(proposer);

    backoff_manager_close_less_than_or_equal(proposer->backoff_manager, acc_state->standard_acceptor_state.trim_iid);



    gettimeofday(&proposer->last_heard_from_acceptor[acc_state->standard_acceptor_state.aid], NULL);
    if (all_up(proposer))
        proposer->max_num_open_instances = paxos_config.proposer_preexec_window_max;


    ev_epoch_proposer_try_begin_new_instances(proposer);
}

static void ev_epoch_proposer_check_acceptor_timeouts(evutil_socket_t fd, short event, void* arg) {
    struct ev_epoch_proposer* proposer = arg;

    if (!all_up(proposer))
        proposer->max_num_open_instances = paxos_config.proposer_preexec_window_min;

    event_add(proposer->acceptor_timeout_check_event, &proposer->acceptor_timeout_check_time);
}

static void ev_epoch_proposer_handle_epoch_proposer_state(struct writeahead_epoch_paxos_peer * p, struct epoch_paxos_message* msg, void* arg){
    struct ev_epoch_proposer* proposer = arg;
    struct epoch_proposer_state* proposer_state = &msg->message_contents.epoch_proposer_state;

    epoch_proposer_receive_epoch_proposer_state(proposer->proposer, proposer_state);
    backoff_manager_close_less_than_or_equal(proposer->backoff_manager, proposer_state->proposer_state.trim_instance);
    ev_epoch_proposer_try_begin_new_instances(proposer);
}


static void ev_epoch_proposer_handle_epoch_notification(struct writeahead_epoch_paxos_peer* p, struct epoch_paxos_message* msg, void* arg) {
    struct ev_epoch_proposer* proposer = arg;
    struct epoch_notification notification = msg->message_contents.epoch_notification;
    epoch_proposer_handle_epoch_notification(proposer->proposer, &notification);
}

static void
ev_epoch_proposer_check_timeouts( evutil_socket_t fd,  short event, void *arg)
{
    paxos_log_debug("Checking timeouts...");
    struct ev_epoch_proposer* p = arg;
    struct epoch_proposer_timeout_iterator* iter = epoch_proposer_timeout_iterator_new(p->proposer);


    struct epoch_ballot_prepare pr;

    while (epoch_proposer_timeout_iterator_prepare(iter, &pr) != TIMEOUT_ITERATOR_END) {
        paxos_log_info("Instance %d timed out in phase 1.", pr.instance);
        if (pr.epoch_ballot_requested.epoch == INVALID_EPOCH) {
            struct paxos_prepare undef_epoch_pr = (struct paxos_prepare) {
                .iid = pr.instance,
                .ballot = pr.epoch_ballot_requested.ballot
            };
            writeahead_epoch_paxos_peers_for_n_acceptor(p->peers, peer_send_standard_prepare, &undef_epoch_pr, paxos_config.group_1);
        } else {
            writeahead_epoch_paxos_peers_for_n_acceptor(p->peers, peer_send_epoch_ballot_prepare, &pr, paxos_config.group_1);
        }
    }
    struct epoch_ballot_accept accept_msg;
    while (epoch_proposer_timeout_iterator_accept(iter, &accept_msg) != TIMEOUT_ITERATOR_END) {
        paxos_log_info("Instance %d timed out in phase 2.", accept_msg.instance);
        writeahead_epoch_paxos_peers_for_n_acceptor(p->peers, peer_send_epoch_ballot_accept, &accept_msg,
                                                    paxos_config.group_2);
    }

    epoch_proposer_timeout_iterator_free(&iter);
    event_add(p->timeout_event, &p->timeout_time);
}

static void
ev_epoch_proposer_preexec_once( evutil_socket_t fd, short event, void *arg)
{
    struct ev_epoch_proposer* p = arg;
    ev_epoch_proposer_try_begin_new_instances(p);
}

static void random_seed_from_dev_rand() {
    paxos_log_debug("Generating new randomness seed");
    FILE *randomData = fopen("/dev/urandom", "rb");
    if (randomData < 0)
    {
        // something went wrong
        struct timespec time;
        clock_gettime(CLOCK_MONOTONIC, &time);
        srand(time.tv_sec * time.tv_nsec + getpid());
    }
    else
    {
        char myRandomData[sizeof(unsigned int)];
        ssize_t result = fread(myRandomData, sizeof(unsigned int), 1,  randomData);
        if (result < 0)
        {
            struct timespec time;
            clock_gettime(CLOCK_MONOTONIC, &time);
            srand(time.tv_sec * time.tv_nsec + getpid());
        } else {
            srand((unsigned int) myRandomData);
        }
    }
    fclose(randomData);
}

static void ev_epoch_proposer_gen_random_seed( evutil_socket_t fd,  short event, void* arg)  {
    paxos_log_debug("Triggered event to generate new random seed");
    struct ev_epoch_proposer* p = arg;
    random_seed_from_dev_rand();
    event_add(p->random_seed_event, &p->random_seed_time);
}

static void ev_epoch_proposer_print_counters(evutil_socket_t fd, short event, void* arg){
    paxos_log_debug("Triggered event to print Proposer stats");
    struct ev_epoch_proposer* p = arg;
   epoch_proposer_print_counters(p->proposer);
    event_add(p->count_timer_print_event, &p->count_timer_print_time);
}

static void peer_send_epoch_paxos_message(struct writeahead_epoch_paxos_peer* p, void* arg) {
    send_epoch_paxos_message(writeahead_epoch_paxos_peer_get_buffer(p), arg);
}


static void ev_epoch_proposer_state_event(evutil_socket_t fd, short event, void* arg) {
    struct ev_epoch_proposer* p = arg;
    struct epoch_paxos_message msg;
    msg.type = WRITEAHEAD_PAXOS_PROPOSER_STATE;
  //  paxos_log_info("Next instance to begin: %u", epoch_proposer_get_current_instance(p->proposer));
//    paxos_log_info("Current known epoch: ")
  //  struct epoch_proposer_state state;
    epoch_proposer_get_state(p->proposer, &msg.message_contents.epoch_proposer_state);
    writeahead_epoch_paxos_peers_foreach_proposer(p->peers, peer_send_epoch_paxos_message, &msg);
    event_add(p->proposer_state_event, &p->proposer_state_time);

  //  ev_epoch_proposer_try_accept(p);
   // ev_epoch_proposer_try_begin_new_instances(p);
}


static void try_accept_event(evutil_socket_t fd, short event, void* arg) {
    struct ev_epoch_proposer* p = arg;
        paxos_log_debug("Beginning to try Accept Phase of one or more Instances");
        struct epoch_ballot_accept accept;
        //  performance_threshold_timer_begin_timing(p->accept_timer);


        while (epoch_proposer_try_accept(p->proposer, &accept)) {
            // assert(&accept.value_to_accept != NULL);
            // assert(accept.value_to_accept.paxos_value_val != NULL);
            // assert(accept.value_to_accept.paxos_value_len > 0);
            //    // assert(strncmp(accept.value_to_accept.paxos_value_val, "", 2));

            writeahead_epoch_paxos_peers_for_n_acceptor(p->peers, peer_send_epoch_ballot_accept, &accept,
                                                        paxos_config.group_2);
            //todo delete accept value
        }
        event_add(p->try_accept_event, &p->try_accept_time);
    }

struct ev_epoch_proposer *
ev_epoch_proposer_init_internal(int id, struct evpaxos_config *c, struct writeahead_epoch_paxos_peers *peers,
                                int proposer_count, int acceptor_count) {

    struct backoff *backoff;
    if(strncmp(paxos_config.backoff_type, "full-jitter", 11) == 0) {
        backoff = full_jitter_backoff_new(paxos_config.max_backoff_microseconds,
                                          paxos_config.min_backoff_microseconds,
                                          paxos_config.max_initial_backff_microseconds);
    } else if (strncmp(paxos_config.backoff_type, "exponential", 11) == 0) {
        backoff = exponential_randomised_backoff_new(paxos_config.max_backoff_microseconds, paxos_config.min_backoff_microseconds, paxos_config.max_initial_backff_microseconds);
    } else {
        //default
        backoff = exponential_randomised_backoff_new(paxos_config.max_backoff_microseconds, paxos_config.min_backoff_microseconds, paxos_config.max_initial_backff_microseconds);
    }

    struct backoff_manager* backoff_manager = backoff_manager_new(backoff);
    struct ev_epoch_proposer* proposer = malloc(sizeof(struct ev_epoch_proposer));
    proposer->id = id;
    proposer->proposer = epoch_proposer_new(id, evpaxos_acceptor_count(c), paxos_config.quorum_1, paxos_config.quorum_2,
                                            paxos_config.max_ballot_increment);
    epoch_proposer_set_current_instance(proposer->proposer, 1);
    proposer->max_num_open_instances = paxos_config.proposer_preexec_window_max;
    proposer->proposer_count = proposer_count;
    proposer->acceptor_count = acceptor_count;

    proposer->instances_to_skip = malloc(sizeof(int) * ev_proposer_get_max_instances_to_skip(proposer));
    proposer->num_instances_to_skip = 0;

    for (unsigned int i = 0; i < ev_proposer_get_max_instances_to_skip(proposer); i++) {
        proposer->instances_to_skip[i] = -1; // force errors
    }

    writeahead_epoch_paxos_peers_subscribe(peers, WRITEAHEAD_EPOCH_BALLOT_PROMISE, ev_epoch_proposer_handle_promise, proposer);
    writeahead_epoch_paxos_peers_subscribe(peers, WRITEAHEAD_EPOCH_BALLOT_ACCEPTED, ev_epoch_proposer_handle_accepted, proposer);
    writeahead_epoch_paxos_peers_subscribe(peers, WRITEAHEAD_EPOCH_BALLOT_PREEMPTED, ev_epoch_proposer_handle_preempted, proposer);
    writeahead_epoch_paxos_peers_subscribe(peers, WRITEAHEAD_CLIENT_VALUE, ev_epoch_proposer_handle_client_value, proposer);
    writeahead_epoch_paxos_peers_subscribe(peers, WRITEAHEAD_ACCEPTOR_STATE, ev_epoch_proposer_handle_acceptor_state, proposer);
    writeahead_epoch_paxos_peers_subscribe(peers, WRITEAHEAD_INSTANCE_CHOSEN_AT_EPOCH_BALLOT, ev_epoch_proposer_handle_chosen, proposer);
    writeahead_epoch_paxos_peers_subscribe(peers, WRITEAHEAD_EPOCH_NOTIFICATION, ev_epoch_proposer_handle_epoch_notification, proposer);
    writeahead_epoch_paxos_peers_subscribe(peers, WRITEAHEAD_INSTANCE_TRIM, ev_epoch_proposer_handle_trim, proposer);
    writeahead_epoch_paxos_peers_subscribe(peers, WRITEAHEAD_PAXOS_PROPOSER_STATE, ev_epoch_proposer_handle_epoch_proposer_state, proposer);

    struct event_base* base = writeahead_epoch_paxos_peers_get_event_base(peers);

    proposer->preprare_timer = get_prepare_threshold_timer_new();
    proposer->promise_timer = get_promise_performance_threshold_timer_new();
    proposer->accept_timer = get_accept_threshold_timer_new();
    proposer->accepted_timer = get_accepted_proposer_threshold_timer_new();
    proposer->preempt_timer = get_preempt_threshold_timer_new();
    proposer->chosen_timer = get_chosen_acceptor_performance_threshold_timer_new();

    proposer->last_heard_from_acceptor = malloc(sizeof(struct timeval) * acceptor_count);

    unsigned int ballot_bias = paxos_config.round_robin_ballot_bias ? paxos_config.max_ballot_increment : 1;
    proposer->round_robin_allocator = round_robin_allocator_new(id, proposer_count,
                                                                ballot_bias, paxos_config.round_robin_backoff, paxos_config.max_ballot_increment);

    proposer->acceptor_timeout_check_time = (struct timeval) {0, 500000};
    proposer->acceptor_timeout_check_event = evtimer_new(base, ev_epoch_proposer_check_acceptor_timeouts, proposer);
    event_add(proposer->acceptor_timeout_check_event, &proposer->acceptor_timeout_check_time);

    proposer->timeout_time.tv_sec = paxos_config.proposer_timeout;
    proposer->timeout_time.tv_usec = 0;
    proposer->timeout_event = evtimer_new(base, ev_epoch_proposer_check_timeouts, proposer);
    event_add(proposer->timeout_event, &proposer->timeout_time);


    proposer->random_seed_time = (struct timeval) {.tv_sec = random_between(30, 60), .tv_usec = 0};
    proposer->random_seed_event = evtimer_new(base, ev_epoch_proposer_gen_random_seed, proposer);
    event_add(proposer->random_seed_event, &proposer->random_seed_time);

    //proposer->count_timer_print_time = (struct timeval) {.tv_sec = 1, .tv_usec = 0};
  //  proposer->count_timer_print_event = evtimer_new(base, ev_epoch_proposer_print_counters, proposer);
   // event_add(proposer->count_timer_print_event, &proposer->count_timer_print_time);

    proposer->proposer_state_time = (struct timeval) {.tv_sec = 0, .tv_usec = 50000};
    proposer->proposer_state_event = evtimer_new(base, ev_epoch_proposer_state_event, proposer);
    event_add(proposer->proposer_state_event, &proposer->proposer_state_time);

    proposer->try_accept_time = (struct timeval) {0, 500};
    proposer->try_accept_event = evtimer_new(base, try_accept_event, proposer);
    event_add(proposer->try_accept_event, &proposer->try_accept_time);

    random_seed_from_dev_rand();

    proposer->peers = peers;
    proposer->backoff_manager = backoff_manager;
//..    proposer->instance_strategy = instance_strategy_new(500, 1);
    event_base_once(base, 0, EV_TIMEOUT, ev_epoch_proposer_preexec_once, proposer, NULL);
    return proposer;
}

struct ev_epoch_proposer* ev_epoch_proposer_init(int id, const char* config_file, struct event_base* base) {
    struct evpaxos_config* config = evpaxos_config_read(config_file);

    if (config == NULL)
        return NULL;

    // Check id validity of proposer_id
    if (id < 0) {
        paxos_log_error("Invalid proposer id: %d", id);
        return NULL;
    }

    struct writeahead_epoch_paxos_peers* peers = writeahead_epoch_paxos_peers_new(base, config);
    writeahead_epoch_paxos_peers_connect_to_acceptors(peers, id);
    writeahead_epoch_paxos_peers_connect_to_other_proposers(peers, id);
//    writeahead_epoch_paxos_peers_connect_to_clients(peers, id);
    int port = evpaxos_proposer_listen_port(config, id);
    int rv = writeahead_epoch_paxos_peers_listen(peers, port);
    if (rv == 0 ) // failure
        return NULL;

    struct ev_epoch_proposer* p = ev_epoch_proposer_init_internal(id, config, peers, evpaxos_proposer_count(config), evpaxos_acceptor_count(config));
    evpaxos_config_free(config);
    return p;
}

void ev_epoch_proposer_free_internal(struct ev_epoch_proposer** p) {
    event_free((**p).timeout_event);
    event_free((**p).random_seed_event);
    backoff_manager_free(&(**p).backoff_manager);
    epoch_proposer_free((**p).proposer);
    free(*p);
    *p = NULL;
}

void ev_epoch_proposer_free(struct ev_epoch_proposer** p) {
    writeahead_epoch_paxos_peers_free((**p).peers);
    ev_epoch_proposer_free_internal(p);
}


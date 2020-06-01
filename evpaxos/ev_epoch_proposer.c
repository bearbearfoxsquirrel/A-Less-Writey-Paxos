//
// Created by Michael Davis on 06/04/2020.
//

#include <unistd.h>

#include "epoch_paxos_message.h"
#include "ev_epoch_paxos.h"
#include <string.h>
#include "khash.h"
#include <stdlib.h>
#include <event2/event.h>
#include "paxos_message_conversion.h"
#include "ballot.h"
#include "paxos_value.h"
#include <paxos_types.h>
#include "epoch_proposer.h"
#include <assert.h>
#include <fcntl.h>
#include <standard_paxos_peers.h>
#include <writeahead_epoch_paxos_peers.h>
#include "time.h"
#include "backoff_implementations.h"
#include "backoff_manager.h"
#include "stdlib.h"
#include "random.h"
#include "stdio.h"
#include "timeout.h"
#include "performance_threshold_timer.h"
#include "epoch_ballot.h"
#include "ev_epoch_paxos_internal.h"
#include "ev_timer_threshold_timer_util.h"



KHASH_MAP_INIT_INT(retries, iid_t*)

struct ev_epoch_proposer {
    int max_num_open_instances;
    struct epoch_proposer* proposer;
    struct writeahead_epoch_paxos_peers* peers;

    struct timeval timeout_time;
    struct event* timeout_event;

    struct backoff_manager* backoff_manager;

    struct event* random_seed_event;
    struct timeval random_seed_time;

    struct performance_threshold_timer* preprare_timer;
    struct performance_threshold_timer* accept_timer;
    struct performance_threshold_timer* preempt_timer;
    struct performance_threshold_timer* accepted_timer;
    struct performance_threshold_timer *chosen_timer;
    struct performance_threshold_timer *promise_timer;
};


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
    struct epoch_ballot_accept accept;
    performance_threshold_timer_begin_timing(p->accept_timer);
    while (epoch_proposer_try_accept(p->proposer, &accept)) {
        assert(&accept.value_to_accept != NULL);
        assert(accept.value_to_accept.paxos_value_val != NULL);
        assert(accept.value_to_accept.paxos_value_len > 0);
        assert(strncmp(accept.value_to_accept.paxos_value_val, "", 2));
        writeahead_epoch_paxos_peers_for_n_acceptor(p->peers, peer_send_epoch_ballot_accept, &accept, paxos_config.group_2);
    }
    ev_performance_timer_stop_check_and_clear_timer(p->accept_timer, "Accept Request");

    ev_epoch_proposer_try_begin_new_instances(p);
}


static void ev_epoch_proposer_try_higher_ballot( evutil_socket_t fd,  short event, void* arg) {
    struct retry* args = arg;
    struct ev_epoch_proposer* proposer = args->proposer;
    struct epoch_paxos_prepares* next_prepare = args->prepare;
    
    switch (next_prepare->type) {
        case STANDARD_PREPARE:
            paxos_log_debug("Trying to Prepare next Epoch Ballot for Instance %u with Ballot %u.%u",
                            next_prepare->standard_prepare.iid,
                            next_prepare->standard_prepare.ballot.number,
                            next_prepare->standard_prepare.ballot.proposer_id);
            writeahead_epoch_paxos_peers_for_n_acceptor(proposer->peers, peer_send_standard_prepare, &next_prepare->standard_prepare,
                                                        paxos_config.group_1); 
            break;
        case EXPLICIT_EPOCH_PREPARE:


            paxos_log_debug("Trying to Prepare next Epoch Ballot for Instance %u with Epoch Ballot %u.%u.%u",
                        next_prepare->explicit_epoch_prepare.instance,
                        next_prepare->explicit_epoch_prepare.epoch_ballot_requested.epoch,
                        next_prepare->explicit_epoch_prepare.epoch_ballot_requested.ballot.number,
                        next_prepare->explicit_epoch_prepare.epoch_ballot_requested.ballot.proposer_id);
        writeahead_epoch_paxos_peers_for_n_acceptor(proposer->peers, peer_send_epoch_ballot_prepare, &next_prepare->explicit_epoch_prepare,
                                                    paxos_config.group_1);
        break;
    }
    
    free(next_prepare);
    free(args);
    ev_epoch_proposer_try_accept(proposer);
}





static void ev_epoch_proposer_try_begin_new_instances(struct ev_epoch_proposer* p) {
    struct epoch_paxos_prepares prepare;

    unsigned int number_of_instances_to_open = p->max_num_open_instances - epoch_proposer_prepare_count(p->proposer) - epoch_proposer_acceptance_count(p->proposer);

    paxos_log_debug("Opening %u new Instances", number_of_instances_to_open);
    performance_threshold_timer_begin_timing(p->preprare_timer);

    while (number_of_instances_to_open > 0) {
        iid_t current_instance = epoch_proposer_get_current_instance(p->proposer);
        assert(current_instance != INVALID_INSTANCE);
        paxos_log_debug("Current Proposing Instance is %u", current_instance);

        bool new_instance_to_prepare = epoch_proposer_try_to_start_preparing_instance(p->proposer, current_instance, &prepare);

        epoch_proposer_next_instance(p->proposer);

        if (new_instance_to_prepare) {
            number_of_instances_to_open--;

            if (prepare.standard_prepare.iid % 3 == 0) {


                struct timeval current_backoff = (struct timeval) {0, random_between(5000, 10000)};



                struct retry *retry_args = calloc(1, sizeof(struct retry));
                struct epoch_paxos_prepares *delayed_prepare = malloc(sizeof(struct paxos_prepare));

                *retry_args = (struct retry) {.proposer = p, .prepare = delayed_prepare};

                switch (prepare.type) {

                    case STANDARD_PREPARE:
                        delayed_prepare->standard_prepare = prepare.standard_prepare;
                        break;
                    case EXPLICIT_EPOCH_PREPARE:
                        delayed_prepare->explicit_epoch_prepare = prepare.explicit_epoch_prepare;
                        break;
                }

                struct event *ev = evtimer_new(writeahead_epoch_paxos_peers_get_event_base(p->peers), ev_epoch_proposer_try_higher_ballot,
                                               retry_args);
                event_add(ev, &current_backoff);

            } else {
                switch (prepare.type) {


                    case STANDARD_PREPARE:

                        paxos_log_debug("Sending Standard Prepare for Instance %u, Ballot %u.%u",
                                        prepare.standard_prepare.iid, prepare.standard_prepare.ballot.number,
                                        prepare.standard_prepare.ballot.proposer_id);

                        writeahead_epoch_paxos_peers_for_n_acceptor(p->peers, peer_send_standard_prepare,
                                                                    &prepare.standard_prepare,
                                                                    paxos_config.group_1);

                        break;
                    case EXPLICIT_EPOCH_PREPARE:
                        paxos_log_debug("Sending Epoch Ballot Prepare for Instance %u, Epoch Ballot %u.%u.%u",
                                        prepare.explicit_epoch_prepare.instance,
                                        prepare.explicit_epoch_prepare.epoch_ballot_requested.epoch,
                                        prepare.explicit_epoch_prepare.epoch_ballot_requested.ballot.number,
                                        prepare.explicit_epoch_prepare.epoch_ballot_requested.ballot.proposer_id);

                        writeahead_epoch_paxos_peers_for_n_acceptor(p->peers, peer_send_epoch_ballot_prepare,
                                                                    &prepare.explicit_epoch_prepare,
                                                                    paxos_config.group_1);
                        break;
                }
            }
        }
    }
    ev_performance_timer_stop_check_and_clear_timer(p->preprare_timer, "Prepare");

}




static void ev_epoch_proposer_handle_promise( struct writeahead_epoch_paxos_peer* p, struct writeahead_epoch_paxos_message* msg, void* arg) {
    struct ev_epoch_proposer* proposer = arg;
    struct epoch_ballot_promise* promise = &msg->message_contents.epoch_ballot_promise;
    struct epoch_ballot_prepare next_prepare = {0};

    if (promise->promised_epoch_ballot.ballot.proposer_id != epoch_proposer_get_id(proposer->proposer))
        return;

    performance_threshold_timer_begin_timing(proposer->promise_timer);
    enum epoch_paxos_message_return_codes return_code = epoch_proposer_receive_promise(proposer->proposer, promise, &next_prepare);
    ev_performance_timer_stop_check_and_clear_timer(proposer->promise_timer, "Promise");

    if (return_code == EPOCH_PREEMPTED) {
        assert(next_prepare.instance != INVALID_INSTANCE);
        writeahead_epoch_paxos_peers_for_n_acceptor(proposer->peers, peer_send_epoch_ballot_prepare, &next_prepare, paxos_config.group_1);
    } else if (return_code == QUORUM_REACHED) {
        ev_epoch_proposer_try_accept(proposer);
    }
}

static void ev_epoch_proposer_handle_accepted( struct writeahead_epoch_paxos_peer* p, struct writeahead_epoch_paxos_message* msg, void* arg){
    struct ev_epoch_proposer* proposer = arg;
    struct epoch_ballot_accepted* accepted = &msg->message_contents.epoch_ballot_accepted;
    struct epoch_ballot_chosen chosen_msg;

    if (accepted->accepted_epoch_ballot.ballot.proposer_id != epoch_proposer_get_id(proposer->proposer)) {
        return;
    }

    performance_threshold_timer_begin_timing(proposer->accepted_timer);
    enum epoch_paxos_message_return_codes return_code = epoch_proposer_receive_accepted(proposer->proposer, accepted, &chosen_msg);
    ev_performance_timer_stop_check_and_clear_timer(proposer->accepted_timer, "Accepted");

    if (return_code == QUORUM_REACHED) {
        assert(chosen_msg.instance == accepted->instance);
        assert(epoch_ballot_equal(chosen_msg.chosen_epoch_ballot, accepted->accepted_epoch_ballot));
        assert(is_values_equal(chosen_msg.chosen_value, accepted->accepted_value));

        //send chosen
        //  ev_epoch_proposer_try_begin_new_instances(proposer);
        //  } else {
    }
        ev_epoch_proposer_try_accept(proposer);
  //  }
}

static void ev_epoch_proposer_handle_chosen( struct writeahead_epoch_paxos_peer* p, struct writeahead_epoch_paxos_message* msg, void* arg) {
    struct ev_epoch_proposer* proposer = arg;
    struct epoch_ballot_chosen* chosen_msg = &msg->message_contents.instance_chosen_at_epoch_ballot;

    performance_threshold_timer_begin_timing(proposer->chosen_timer);
    enum epoch_paxos_message_return_codes return_code = epoch_proposer_receive_chosen(proposer->proposer, chosen_msg);
    ev_performance_timer_stop_check_and_clear_timer(proposer->chosen_timer, "Chosen");

    if (return_code == MESSAGE_ACKNOWLEDGED) {
        backoff_manager_close_backoff_if_exists(proposer->backoff_manager, chosen_msg->instance);
        ev_epoch_proposer_try_begin_new_instances(proposer);
    }
   ev_epoch_proposer_try_accept(proposer); //needed?
}


static void ev_epoch_proposer_handle_preempted( struct writeahead_epoch_paxos_peer* p, struct writeahead_epoch_paxos_message* msg, void* arg) {
    struct ev_epoch_proposer* proposer = arg;
    struct epoch_ballot_preempted preempted_msg = msg->message_contents.epoch_ballot_preempted;

    if (preempted_msg.requested_epoch_ballot.ballot.proposer_id != epoch_proposer_get_id(proposer->proposer)) return;

    struct epoch_ballot_prepare* next_prepare = malloc(sizeof(struct epoch_ballot_prepare));
    assert(epoch_ballot_greater_than(preempted_msg.acceptors_current_epoch_ballot, preempted_msg.requested_epoch_ballot));

    performance_threshold_timer_begin_timing(proposer->preempt_timer);
    enum epoch_paxos_message_return_codes return_code = epoch_proposer_receive_preempted(proposer->proposer, &preempted_msg, next_prepare);
    ev_performance_timer_stop_check_and_clear_timer(proposer->preempt_timer, "Preempt");

    if (return_code == BALLOT_PREEMPTED) {
        assert(next_prepare->instance != 0);
        assert(epoch_ballot_greater_than(next_prepare->epoch_ballot_requested, preempted_msg.acceptors_current_epoch_ballot));
        
        const struct timeval* current_backoff = backoff_manager_get_backoff(proposer->backoff_manager, preempted_msg.instance);
        paxos_log_debug("Trying next Ballot for Instance %u, %u.%u in %u microseconds", 
                next_prepare->instance, 
                next_prepare->epoch_ballot_requested.ballot.number, 
                next_prepare->epoch_ballot_requested.ballot.proposer_id, 
                current_backoff->tv_usec);

        struct retry* retry_args = calloc(1, sizeof(struct retry));
        *retry_args = (struct retry) {.proposer = proposer, .prepare = next_prepare};
        struct event* ev = evtimer_new(writeahead_epoch_paxos_peers_get_event_base(proposer->peers), ev_epoch_proposer_try_higher_ballot, retry_args);
        event_add(ev, current_backoff);
    } else if (return_code == EPOCH_PREEMPTED) {
        writeahead_epoch_paxos_peers_for_n_acceptor(proposer->peers, peer_send_epoch_ballot_prepare, next_prepare, paxos_config.quorum_1);
    }
}


static void
ev_epoch_proposer_handle_trim( struct writeahead_epoch_paxos_peer* p, struct writeahead_epoch_paxos_message* msg, void* arg) {

    struct ev_epoch_proposer* proposer = arg;
    struct paxos_trim* trim_msg = &msg->message_contents.trim;


    epoch_proposer_receive_trim(proposer->proposer, trim_msg);

    backoff_manager_close_less_than_or_equal(proposer->backoff_manager, trim_msg->iid);

    ev_epoch_proposer_try_begin_new_instances(proposer);
}

static void
ev_epoch_proposer_handle_client_value( struct writeahead_epoch_paxos_peer* p, struct writeahead_epoch_paxos_message* msg, void* arg)
{
    struct ev_epoch_proposer* proposer = arg;
    struct paxos_value* v = &msg->message_contents.client_value;

    assert(v->paxos_value_len > 1);
    assert(v->paxos_value_val != NULL);
    assert(v->paxos_value_val != "");

    epoch_proposer_add_paxos_value_to_queue(proposer->proposer, *v);
    ev_epoch_proposer_try_accept(proposer);
}

static void
ev_epoch_proposer_handle_acceptor_state( struct writeahead_epoch_paxos_peer* p, struct writeahead_epoch_paxos_message* msg, void* arg)
{
    struct ev_epoch_proposer* proposer = arg;
    struct writeahead_epoch_acceptor_state* acc_state = &msg->message_contents.state;
    epoch_proposer_receive_acceptor_state(proposer->proposer, acc_state);
    ev_epoch_proposer_try_accept(proposer);

    backoff_manager_close_less_than_or_equal(proposer->backoff_manager, acc_state->standard_acceptor_state.trim_iid);

}


static void
ev_epoch_proposer_check_timeouts( evutil_socket_t fd,  short event, void *arg)
{
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
    struct ev_epoch_proposer* p = arg;
    random_seed_from_dev_rand();
    event_add(p->random_seed_event, &p->random_seed_time);
}

struct ev_epoch_proposer* ev_epoch_proposer_init_internal(int id, struct evpaxos_config* c, struct writeahead_epoch_paxos_peers* peers, struct backoff_manager* backoff_manager) {
    struct ev_epoch_proposer* proposer = malloc(sizeof(struct ev_epoch_proposer));
    proposer->proposer = epoch_proposer_new(id, evpaxos_acceptor_count(c), paxos_config.quorum_1, paxos_config.quorum_2,
                                            paxos_config.max_ballot_increment);
    epoch_proposer_set_current_instance(proposer->proposer, 1);
    proposer->max_num_open_instances = paxos_config.proposer_preexec_window;

    writeahead_epoch_paxos_peers_subscribe(peers, WRITEAHEAD_EPOCH_BALLOT_PROMISE, ev_epoch_proposer_handle_promise, proposer);
    writeahead_epoch_paxos_peers_subscribe(peers, WRITEAHEAD_EPOCH_BALLOT_ACCEPTED, ev_epoch_proposer_handle_accepted, proposer);
    writeahead_epoch_paxos_peers_subscribe(peers, WRITEAHEAD_EPOCH_BALLOT_PREEMPTED, ev_epoch_proposer_handle_preempted, proposer);
    writeahead_epoch_paxos_peers_subscribe(peers, WRITEAHEAD_CLIENT_VALUE, ev_epoch_proposer_handle_client_value, proposer);
    writeahead_epoch_paxos_peers_subscribe(peers, WRITEAHEAD_ACCEPTOR_STATE, ev_epoch_proposer_handle_acceptor_state, proposer);
    writeahead_epoch_paxos_peers_subscribe(peers, WRITEAHEAD_INSTANCE_CHOSEN_AT_EPOCH_BALLOT, ev_epoch_proposer_handle_chosen, proposer);
   // writeahead_epoch_paxos_peers_subscribe(peers, WRITEAHEAD_EPOCH_NOTIFICATION, ev_epoch_proposer_handle_epoch_notification);
    writeahead_epoch_paxos_peers_subscribe(peers, WRITEAHEAD_INSTANCE_TRIM, ev_epoch_proposer_handle_trim, proposer);

    struct event_base* base = writeahead_epoch_paxos_peers_get_event_base(peers);

    proposer->preprare_timer = get_prepare_threshold_timer_new();
    proposer->promise_timer = get_promise_performance_threshold_timer_new();
    proposer->accept_timer = get_accept_threshold_timer_new();
    proposer->accepted_timer = get_accepted_proposer_threshold_timer_new();
    proposer->preempt_timer = get_preempt_threshold_timer_new();
    proposer->chosen_timer = get_chosen_acceptor_performance_threshold_timer_new();


    proposer->timeout_time.tv_sec = paxos_config.proposer_timeout;
    proposer->timeout_time.tv_usec = 0;
    proposer->timeout_event = evtimer_new(base, ev_epoch_proposer_check_timeouts, proposer);
    event_add(proposer->timeout_event, &proposer->timeout_time);


    proposer->random_seed_time = (struct timeval) {.tv_sec = random_between(30, 60), .tv_usec = 0};
    proposer->random_seed_event = evtimer_new(base, ev_epoch_proposer_gen_random_seed, proposer);

    event_add(proposer->random_seed_event, &proposer->random_seed_time);
    random_seed_from_dev_rand();

    proposer->peers = peers;
    proposer->backoff_manager = backoff_manager;

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

    struct writeahead_epoch_paxos_peers* peers = writeahead_epoch_paxos_peers_new(base, config,
                                                                                  paxos_config.messages_batched_average,
                                                                                  paxos_config.messages_batched_max, paxos_config.max_expected_value_size);
    writeahead_epoch_paxos_peers_connect_to_acceptors(peers, id);
    int port = evpaxos_proposer_listen_port(config, id);
    int rv = writeahead_epoch_paxos_peers_listen(peers, port);
    if (rv == 0 ) // failure
        return NULL;

    //todo config mechanism to work out backoff type
    struct backoff* backoff = full_jitter_backoff_new(paxos_config.max_backoff_microseconds, paxos_config.min_backoff_microseconds, paxos_config.max_initial_backff_microseconds);
    struct backoff_manager* backoff_manager = backoff_manager_new(backoff);

    struct ev_epoch_proposer* p = ev_epoch_proposer_init_internal(id, config, peers, backoff_manager);
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


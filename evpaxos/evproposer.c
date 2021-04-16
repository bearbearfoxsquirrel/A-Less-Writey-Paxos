/*
 * Copyright (c) 2013-2014, University of Lugano
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the copyright holders nor the names of it
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <unistd.h>

#include "evpaxos.h"
#include "standard_paxos_peers.h"
#include "proposer.h"
#include <string.h>
#include "khash.h"
#include <stdlib.h>
#include <event2/event.h>
#include "paxos_message_conversion.h"
#include "round_robin_allocator.h"
#include "ballot.h"
#include "paxos_value.h"
#include <paxos_types.h>
#include <assert.h>
#include <standard_paxos_message.h>
#include <random.h>
#include <ev_timer_threshold_timer_util.h>
#include "time.h"
#include "backoff_implementations.h"
#include "backoff_manager.h"
#include "stdio.h"
#include "performance_threshold_timer.h"
#include <count_logger.h>


KHASH_MAP_INIT_INT(backoffs, struct timeval*)
KHASH_MAP_INIT_INT(retries, iid_t*)


struct evproposer {
    int acceptor_count;
    struct timeval* last_heard_from_acceptor;


    uint32_t id;
	int preexec_window;
	struct proposer* state;
	struct standard_paxos_peers* peers;
	struct timeval tv;
	struct event* timeout_ev;

	struct round_robin_allocator* round_robin_allocator;
	struct backoff_manager* backoff_manager;


    int proposers_count;
    struct event *random_seed_event;
    struct timeval random_seed_timer;


    struct performance_threshold_timer* preexecute_timer;
    struct performance_threshold_timer* promise_timer;
    struct performance_threshold_timer* acceptance_timer;
    struct performance_threshold_timer* propose_timer;
    struct performance_threshold_timer* trim_timer;
    struct performance_threshold_timer* preemption_timer;
    struct performance_threshold_timer* chosen_timer;


    struct event *proposer_state_event;
    struct timeval proposer_state_timer;

    struct timeval try_accept_time;
    struct event* try_accept_event;



    struct timeval acceptor_timeout_check_time;
    struct event* acceptor_timeout_check_event;

    bool fake_leader;
};


struct retry {
    struct evproposer* proposer;
    struct paxos_prepare* prepare;
};



static void
peer_send_chosen(struct standard_paxos_peer* p, void* arg){
    send_paxos_chosen(peer_get_buffer(p), arg);
}

static void
peer_send_prepare(struct standard_paxos_peer* p, void* arg)
{
	send_paxos_prepare(peer_get_buffer(p), arg);
}


static void
peer_send_accept(struct standard_paxos_peer* p, void* arg)
{
	send_paxos_accept(peer_get_buffer(p), arg);
}

static void
peer_send_trim(struct standard_paxos_peer* p, void* arg) {
    send_paxos_trim(peer_get_buffer(p), arg);
}

static void try_accept(struct evproposer* p);

static void
evproposer_try_higher_ballot( evutil_socket_t fd,  short event, void* arg);




static void
fake_leader_preexecute(struct evproposer* p) {
    int count = p->preexec_window - (proposer_prepare_count(p->state) + proposer_acceptance_count(p->state));

    while(count > 0) {
        struct paxos_prepare pr;
        iid_t current_instance = proposer_get_current_instance(p->state);
        paxos_log_debug("current proposing instance: %u", current_instance);

        struct ballot initial_ballot = {1, p->id};
        bool prepred = proposer_try_to_start_preparing_instance(p->state, current_instance, initial_ballot, &pr);
        if (prepred) {
            count--;
            try_accept(p);
        }
        proposer_next_instance(p->state);
    }
    paxos_log_debug("Opened %d new instances", count);
}

// Begins one or more instances, defined by the preexec_window
static void
proposer_preexecute(struct evproposer* p) {

    int count = p->preexec_window - (proposer_prepare_count(p->state) + proposer_acceptance_count(p->state));

    while(count > 0) {
        struct paxos_prepare* pr = malloc(sizeof(*pr));
        iid_t current_instance = proposer_get_current_instance(p->state);//proposer_get_next_instance_to_prepare(p->acceptor_state);
        paxos_log_debug("current proposing instance: %u", current_instance);

        struct ballot initial_ballot = round_robin_allocator_get_ballot(p->round_robin_allocator, current_instance);

        bool prepred = proposer_try_to_start_preparing_instance(p->state, current_instance, initial_ballot, pr);

        if (prepred) {
            count--;
            bool should_delay = round_robin_allocator_should_delay_proposal(p->round_robin_allocator, current_instance);

            if (should_delay) {
                const struct timeval *current_backoff = backoff_manager_get_backoff(p->backoff_manager,
                                                                                    pr->iid);


                paxos_log_debug("Trying next Ballot for Instance %u, %u.%u in %u microseconds", pr->iid,
                                pr->ballot.number, pr->ballot.proposer_id, current_backoff->tv_usec);

                struct retry *retry_args = malloc(sizeof(struct retry));
                *retry_args = (struct retry) {.proposer = p, .prepare = pr};

                struct event *ev = evtimer_new(peers_get_event_base(p->peers), evproposer_try_higher_ballot,
                                               retry_args);
                event_add(ev, current_backoff);
            } else {
               // assert(pr.ballot.proposer_id < 5);
                peers_for_n_acceptor(p->peers, peer_send_prepare, pr, paxos_config.group_1);
            }
        }
        proposer_next_instance(p->state);
    }
    //ev_performance_timer_stop_check_and_clear_timer(p->preexecute_timer, "Preexecuting");
    paxos_log_debug("Opened %d new instances", count);
}


static void
evproposer_try_higher_ballot( evutil_socket_t fd,  short event, void* arg) {
    struct retry* args =  arg;
    struct evproposer* proposer = args->proposer;

    paxos_log_debug("Trying next ballot for Instance %u, %u.%u", args->prepare->iid, args->prepare->ballot.number, args->prepare->ballot.proposer_id);
   // assert(args->prepare.iid > 0);
    if (proposer_is_instance_pending(proposer->state, args->prepare->iid)) { // may have been chosen or trimmed by the time backoff is over
       // assert(args->prepare.ballot.proposer_id < 5);
        peers_for_n_acceptor(proposer->peers, peer_send_prepare, args->prepare, paxos_config.group_1);
    } else {
        backoff_manager_close_backoff_if_exists(proposer->backoff_manager, args->prepare->iid);
        proposer_preexecute(proposer);
    }
//    paxos_prepare_free(args->prepare);
  //  free(args->prepare);
  free(args->prepare);
    free(args);

    // try_accept(proposer);
}


static void
try_accept(struct evproposer* p)
{
	paxos_accept accept;

//	 performance_threshold_timer_begin_timing(p->propose_timer);
    while (proposer_try_accept(p->state, &accept)) {
       // assert(accept.ballot.proposer_id < 3);
       // assert(&accept.value != NULL);
       // assert(accept.value.paxos_value_val != NULL);
       // assert(accept.value.paxos_value_len > 0);
       // assert(strncmp(accept.value.paxos_value_val, "", 2));
        peers_for_n_acceptor(p->peers, peer_send_accept, &accept, paxos_config.group_2);
        // todo delete accept value
    }

    // ev_performance_timer_stop_check_and_clear_timer(p->propose_timer, "Proposing Values");

   // proposer_preexecute(p); // ?
}


static void
evproposer_handle_promise( struct standard_paxos_peer* p, standard_paxos_message* msg, void* arg)
{
	struct evproposer* proposer = arg;
	struct paxos_promise* promise = &msg->u.promise;

	if (promise->ballot.proposer_id != proposer->id)
	    return;

	//performance_threshold_timer_begin_timing(proposer->promise_timer);
	int quorum_reached = proposer_receive_promise(proposer->state, promise);
//	 ev_performance_timer_stop_check_and_clear_timer(proposer->promise_timer, "Handling a Promise");
	if (quorum_reached)
	    try_accept(proposer);
}

static void
evproposer_handle_accepted( struct standard_paxos_peer* p, standard_paxos_message* msg, void* arg)
{
	struct evproposer* proposer = arg;
	paxos_accepted* acc = &msg->u.accepted;

    if (acc->promise_ballot.proposer_id != proposer->id){
        return;
    }

    struct paxos_chosen chosen_msg;
  //  memset(&chosen_msg, 0, sizeof(struct paxos_chosen));

   // assert(acc->value.paxos_value_len > 1);
   // assert(acc->value_ballot.number > 0);

  //  performance_threshold_timer_begin_timing(proposer->acceptance_timer);
    bool quorum_reached = proposer_receive_accepted(proposer->state, acc, &chosen_msg);
   // ev_performance_timer_stop_check_and_clear_timer(proposer->acceptance_timer, "Handing an Acceptance");

    if (quorum_reached){
     //   peers_foreach_acceptor(proposer->peers, peer_send_chosen, &chosen_msg);
        //peers_foreach_client(proposer->peers, peer_send_chosen, &chosen_msg);

       // if (proposer_get_min_unchosen_instance(proposer->acceptor_state) > chosen_msg.iid) {
         //   struct paxos_trim trim_msg = {.iid = chosen_msg.iid};
          //  peers_foreach_acceptor(proposer->peers, peer_send_trim, &trim_msg);
      //  }
       // assert(chosen_msg.iid == acc->iid);
       // assert(ballot_equal(chosen_msg.ballot, acc->promise_ballot));
       // assert(ballot_equal(chosen_msg.ballot, acc->value_ballot));
       // assert(is_values_equal(chosen_msg.value, acc->value));
        if (proposer->fake_leader)
            fake_leader_preexecute(proposer);
        else
            proposer_preexecute(proposer);
    }
 //   try_accept(proposer);
}


static void
evproposer_handle_chosen( struct standard_paxos_peer* p, struct standard_paxos_message* msg, void* arg) {
    struct evproposer* proposer = arg;
    struct paxos_chosen* chosen_msg = &msg->u.chosen;

  //  performance_threshold_timer_begin_timing(proposer->chosen_timer);
   // assert(chosen_msg->iid != INVALID_INSTANCE);
    if (proposer_receive_chosen(proposer->state, chosen_msg)) {
        //  ev_performance_timer_stop_check_and_clear_timer(proposer->chosen_timer, "Chosen");

        if (proposer->fake_leader)
            fake_leader_preexecute(proposer);
        else {
        //    backoff_manager_close_backoff_if_exists(proposer->backoff_manager, chosen_msg->iid);
            proposer_preexecute(proposer);
        }
    }

   // try_accept(proposer);
}



static void
evproposer_handle_preempted( struct standard_paxos_peer* p, standard_paxos_message* msg, void* arg)
{
    struct evproposer* proposer = arg;
    struct paxos_preempted preempted_msg = msg->u.preempted;

    if (preempted_msg.attempted_ballot.proposer_id != proposer->id) return;

 //   struct paxos_prepare* next_prepare = malloc(sizeof(struct paxos_prepare));
    struct paxos_prepare* next_prepare = malloc(sizeof(*next_prepare));
   // assert(ballot_greater_than(preempted_msg.acceptor_current_ballot, preempted_msg.attempted_ballot));

  //  performance_threshold_timer_begin_timing(proposer->preemption_timer);
    bool new_preemption = proposer_receive_preempted(proposer->state, &preempted_msg, next_prepare);
  //  ev_performance_timer_stop_check_and_clear_timer(proposer->preemption_timer, "preemption");
    if (new_preemption) {
       // assert(next_prepare.iid != 0);
       // assert(ballot_greater_than(next_prepare.ballot, preempted_msg.acceptor_current_ballot));


        const struct timeval* current_backoff = backoff_manager_get_backoff(proposer->backoff_manager, preempted_msg.iid);


        paxos_log_debug("Trying next Ballot for Instance %u, %u.%u in %u microseconds", next_prepare->iid, next_prepare->ballot.number, next_prepare->ballot.proposer_id, current_backoff->tv_usec);

        struct retry* retry_args = malloc(sizeof(struct retry));
        *retry_args = (struct retry) {.proposer = proposer, .prepare = next_prepare};

        struct event* ev = evtimer_new(peers_get_event_base(proposer->peers), evproposer_try_higher_ballot, retry_args);
        event_add(ev, current_backoff);
    } else {
        free(next_prepare);
    }
}


static void
evproposer_handle_trim( struct standard_paxos_peer* p, standard_paxos_message* msg, void* arg) {
    struct evproposer* proposer = arg;
    struct paxos_trim* trim_msg = &msg->u.trim;

    proposer_receive_trim(proposer->state, trim_msg);

    if (proposer->fake_leader)
        fake_leader_preexecute(proposer);
    else {
        backoff_manager_close_less_than_or_equal(proposer->backoff_manager, trim_msg->iid);
        proposer_preexecute(proposer);
    }
}

static void
evproposer_handle_client_value( struct standard_paxos_peer* p, standard_paxos_message* msg, void* arg)
{
    struct evproposer* proposer = arg;
	struct paxos_value* v = &msg->u.client_value;
//	assert(v->paxos_value_len > 1);
//	assert(v->paxos_value_val != NULL);
//	assert(v->paxos_value_val != "");
	//assert(v->paxos_value_val != "");
    proposer_add_paxos_value_to_queue(proposer->state, v);
	try_accept(proposer);
}


static bool all_up(struct evproposer* proposer) {
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
evproposer_handle_acceptor_state( struct standard_paxos_peer* p, standard_paxos_message* msg, void* arg)
{
	struct evproposer* proposer = arg;
	struct paxos_standard_acceptor_state* acc_state = &msg->u.acceptor_state;
	proposer_receive_acceptor_state(proposer->state, acc_state);
	//try_accept(proposer);

    if (proposer->fake_leader)
        fake_leader_preexecute(proposer);
    else {
        backoff_manager_close_less_than_or_equal(proposer->backoff_manager, acc_state->trim_iid);

        if (all_up(proposer)){
            proposer->preexec_window = paxos_config.proposer_preexec_window_max;

        }
        proposer_preexecute(proposer);
    }
}


static void ev_proposer_check_acceptor_timeouts(evutil_socket_t fd, short event, void* arg) {
    struct evproposer* proposer = arg;

    if (!all_up(proposer))
        proposer->preexec_window = paxos_config.proposer_preexec_window_min;

    event_add(proposer->acceptor_timeout_check_event, &proposer->acceptor_timeout_check_time);
}

static void
evproposer_check_timeouts( evutil_socket_t fd,  short event, void *arg) {
	struct evproposer* p = arg;
	struct timeout_iterator* iter = proposer_timeout_iterator(p->state);

	paxos_prepare pr;
	while (timeout_iterator_prepare(iter, &pr)) {
		paxos_log_info("Instance %d timed out in phase 1.", pr.iid);
		peers_for_n_acceptor(p->peers, peer_send_prepare, &pr, paxos_config.group_1);
	}

	paxos_accept accept_msg;
	while (timeout_iterator_accept(iter, &accept_msg)) {
		paxos_log_info("Instance %d timed out in phase 2.", accept_msg.iid);
		peers_for_n_acceptor(p->peers, peer_send_accept, &accept_msg, paxos_config.group_2);
	}

	timeout_iterator_free(iter);
	event_add(p->timeout_ev, &p->tv);
}

static void
evproposer_preexec_once( evutil_socket_t fd, short event, void *arg)
{
	struct evproposer* p = arg;
	proposer_preexecute(p);
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

static void evproposer_gen_random_seed( evutil_socket_t fd,  short event, void* arg)  {
    struct evproposer* p = arg;
    random_seed_from_dev_rand();
    event_add(p->random_seed_event, &p->random_seed_timer);
}


static void
peer_send_paxos_message(struct standard_paxos_peer* p, void* arg)
{
    send_paxos_message(peer_get_buffer(p), arg);
}

static void evproposer_handle_proposer_state(struct standard_paxos_peer* p, standard_paxos_message* msg, void* arg) {
    struct evproposer* proposer = arg;
    struct proposer_state* state = &msg->u.proposer_state;
    proposer_receive_proposer_state(proposer->state, state);
    backoff_manager_close_less_than_or_equal(proposer->backoff_manager, state->trim_instance);
    proposer_preexecute(proposer);
}

static void evproposer_proposer_state_event( evutil_socket_t fd,  short event, void* arg)  {
    struct evproposer* p = arg;
    struct standard_paxos_message msg;
    msg.type = PAXOS_PROPOSER_STATE;
    proposer_get_state(p->state, &msg.u.proposer_state);
    peers_foreach_proposer(p->peers, peer_send_paxos_message, &msg);
    event_add(p->proposer_state_event, &p->proposer_state_timer);
}

static void try_accept_event(evutil_socket_t fd, short event, void* arg) {
    struct evproposer* p = arg;
    paxos_log_debug("Beginning to try Accept Phase of one or more Instances");
    struct paxos_accept accept;
    //  performance_threshold_timer_begin_timing(p->accept_timer);


    while (proposer_try_accept(p->state, &accept)) {
        // assert(&accept.value_to_accept != NULL);
        // assert(accept.value_to_accept.paxos_value_val != NULL);
        // assert(accept.value_to_accept.paxos_value_len > 0);
        //    // assert(strncmp(accept.value_to_accept.paxos_value_val, "", 2));

        peers_for_n_acceptor(p->peers, peer_send_accept, &accept,
                                                    paxos_config.group_2);
        //todo delete accept value
    }
    event_add(p->try_accept_event, &p->try_accept_time);
}


struct evproposer*
evproposer_init_internal(int id, struct evpaxos_config* c, struct standard_paxos_peers* peers, struct backoff_manager* backoff_manager)
{
	struct evproposer* p;
	int acceptor_count = evpaxos_acceptor_count(c);

	p = malloc(sizeof(struct evproposer));
	p->id = id;
	p->proposers_count = evpaxos_proposer_count(c);
	p->acceptor_count = evpaxos_acceptor_count(c);
	p->fake_leader = false;

    p->last_heard_from_acceptor = malloc(sizeof(struct timeval) * acceptor_count);
    p->state = proposer_new(p->id, acceptor_count, paxos_config.quorum_1, paxos_config.quorum_2, paxos_config.max_ballot_increment);
	p->preexec_window = paxos_config.proposer_preexec_window_max;
	proposer_set_current_instance(p->state, 1);

	unsigned int ballot_bias = paxos_config.round_robin_ballot_bias ? paxos_config.max_ballot_increment : 1;
	p->round_robin_allocator = round_robin_allocator_new(id, p->proposers_count, ballot_bias,
                                                         paxos_config.round_robin_backoff, paxos_config.max_ballot_increment);

	peers_subscribe(peers, PAXOS_PROMISE, evproposer_handle_promise, p);
	peers_subscribe(peers, PAXOS_ACCEPTED, evproposer_handle_accepted, p);
	peers_subscribe(peers, PAXOS_PREEMPTED, evproposer_handle_preempted, p);
	peers_subscribe(peers, PAXOS_CLIENT_VALUE, evproposer_handle_client_value, p);
	peers_subscribe(peers, PAXOS_ACCEPTOR_STATE, evproposer_handle_acceptor_state, p);
	peers_subscribe(peers, PAXOS_CHOSEN, evproposer_handle_chosen, p);
	peers_subscribe(peers, PAXOS_TRIM, evproposer_handle_trim, p);
    peers_subscribe(peers, PAXOS_PROPOSER_STATE, evproposer_handle_proposer_state, p);

	// Setup check_timeout
	struct event_base* base = peers_get_event_base(peers);
	p->tv.tv_sec = paxos_config.proposer_timeout;
	p->tv.tv_usec = 0;
	p->timeout_ev = evtimer_new(base, evproposer_check_timeouts, p);
	event_add(p->timeout_ev, &p->tv);

    p->acceptor_timeout_check_time = (struct timeval) {0, 500000};
    p->acceptor_timeout_check_event = evtimer_new(base, ev_proposer_check_acceptor_timeouts, p);
    event_add(p->acceptor_timeout_check_event, &p->acceptor_timeout_check_time);

    p->random_seed_timer = (struct timeval) {.tv_sec = random_between(30, 60), .tv_usec = 0};

	p->random_seed_event = evtimer_new(base, evproposer_gen_random_seed, p);

    event_add(p->random_seed_event, &p->random_seed_timer);
    random_seed_from_dev_rand();

	p->peers = peers;

	p->backoff_manager = backoff_manager;

	p->proposer_state_event = evtimer_new(base, evproposer_proposer_state_event, p);
	p->proposer_state_timer = (struct timeval) {.tv_sec = 0, .tv_usec = 50000};
	event_add(p->proposer_state_event, &p->proposer_state_timer);

	p->preexecute_timer = performance_threshold_timer_new((struct timespec) {0, 50});
	p->promise_timer = performance_threshold_timer_new((struct timespec) {0, 50});
	p->propose_timer = performance_threshold_timer_new((struct timespec) {0, 50});
	p->acceptance_timer = performance_threshold_timer_new((struct timespec) {0, 50});
	p->preemption_timer = get_preempt_threshold_timer_new();
	p->chosen_timer = get_chosen_acceptor_performance_threshold_timer_new();

    p->try_accept_time = (struct timeval) {0, 5000};
    p->try_accept_event = evtimer_new(base, try_accept_event, p);
    event_add(p->try_accept_event, &p->try_accept_time);

    // This initiates the first Paxos Event in the Proposer-Acceptor communication
	event_base_once(base, 0, EV_TIMEOUT, evproposer_preexec_once, p, NULL);

	return p;
}

struct evproposer*
evproposer_init(int id, const char* config_file, struct event_base* base)
{
	struct evpaxos_config* config = evpaxos_config_read(config_file);

	if (config == NULL)
		return NULL;

	// Check id validity of proposer_id
	if (id < 0) {
		paxos_log_error("Invalid proposer id: %d", id);
		return NULL;
	}

    struct standard_paxos_peers* peers = peers_new(base, config);
	peers_connect_to_acceptors(peers, id);
	peers_connect_to_other_proposers(peers, id);
	int port = evpaxos_proposer_listen_port(config, id);
	int rv = peers_listen(peers, port);
	if (rv == 0 ) // failure
		return NULL;



    struct backoff* backoff;//todo config mechanism to work out backoff type
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

	struct evproposer* p = evproposer_init_internal(id, config, peers, backoff_manager);


	evpaxos_config_free(config);
	return p;
}

void
evproposer_free_internal(struct evproposer* p)
{
	event_free(p->timeout_ev);
	backoff_manager_free(&p->backoff_manager);
	proposer_free(p->state);
	free(p);
}

void
evproposer_free(struct evproposer* p)
{
	peers_free(p->peers);
	evproposer_free_internal(p);
}


void
evproposer_set_instance_id(struct evproposer* p, unsigned iid) {
    proposer_set_current_instance(p->state, iid);
}




static void
ev_fake_leader_preexec_once( evutil_socket_t fd, short event, void *arg)
{
    struct evproposer* p = arg;
    fake_leader_preexecute(p);
}

struct evproposer*
ev_fake_leader_init_internal(int id, struct evpaxos_config* c, struct standard_paxos_peers* peers)
{
    struct evproposer* p;
    int acceptor_count = evpaxos_acceptor_count(c);

    p = malloc(sizeof(struct evproposer));
    p->id = id;
    p->proposers_count = evpaxos_proposer_count(c);

    p->state = proposer_new(p->id, acceptor_count, 0, paxos_config.quorum_2, paxos_config.max_ballot_increment);
    p->preexec_window = paxos_config.proposer_preexec_window_max;

    p->fake_leader = true;
    proposer_set_current_instance(p->state, 1);


    peers_subscribe(peers, PAXOS_ACCEPTED, evproposer_handle_accepted, p);
    peers_subscribe(peers, PAXOS_CLIENT_VALUE, evproposer_handle_client_value, p);
    peers_subscribe(peers, PAXOS_ACCEPTOR_STATE, evproposer_handle_acceptor_state, p);
    peers_subscribe(peers, PAXOS_CHOSEN, evproposer_handle_chosen, p);
    peers_subscribe(peers, PAXOS_TRIM, evproposer_handle_trim, p);

    // Setup check_timeout
    struct event_base* base = peers_get_event_base(peers);
    p->tv.tv_sec = paxos_config.proposer_timeout;
    p->tv.tv_usec = 0;
    p->timeout_ev = evtimer_new(base, evproposer_check_timeouts, p);
    event_add(p->timeout_ev, &p->tv);



    p->peers = peers;

    // This initiates the first Paxos Event in the Proposer-Acceptor communication
    event_base_once(base, 0, EV_TIMEOUT, ev_fake_leader_preexec_once, p, NULL);

    return p;
}

struct evproposer*
ev_fake_leader_init(int id, const char* config_file, struct event_base* base) {
    struct evpaxos_config* config = evpaxos_config_read(config_file);

    if (config == NULL)
        return NULL;

    // Check id validity of proposer_id
    if (id < 0) {
        paxos_log_error("Invalid proposer id: %d", id);
        return NULL;
    }

    struct standard_paxos_peers* peers = peers_new(base, config);
    peers_connect_to_acceptors(peers, -1);
    int port = evpaxos_proposer_listen_port(config, id);
    int rv = peers_listen(peers, port);
    if (rv == 0 ) // failure
        return NULL;


    struct evproposer* p = ev_fake_leader_init_internal(id, config, peers);
    evpaxos_config_free(config);
    return p;
}



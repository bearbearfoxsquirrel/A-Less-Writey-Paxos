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

#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include "evpaxos.h"
#include "peers.h"
#include "message.h"
#include "proposer.h"
#include <string.h>
#include "khash.h"
#include <stdlib.h>
#include <event2/event.h>
#include "paxos_message_conversion.h"
#include "ballot.h"
#include "paxos_value.h"
#include <paxos_types.h>
#include <assert.h>
#include <fcntl.h>
#include "time.h"
#include "backoff_implementations.h"
#include "backoff_manager.h"
#include "stdlib.h"
#include "random.h"
#include "stdio.h"
#include "timeout.h"

#define MIN_BACKOFF_TIME 15000
#define MAX_BACKOFF_TIME 1000000
#define MAX_INITIAL_BACKOFF_TIME 30000

#define MIN(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })

KHASH_MAP_INIT_INT(backoffs, struct timeval*)
KHASH_MAP_INIT_INT(retries, iid_t*)


struct evproposer {
	uint32_t id;
	int preexec_window;
	struct proposer* state;
	struct peers* peers;
	struct timeval tv;
	struct event* timeout_ev;

	struct backoff_manager* backoff_manager;
//	khash_t(backoffs)* current_backoffs;

    int proposers_count;
    struct event *random_seed_event;
    struct timeval random_seed_timer;
};

static void
peer_send_chosen(struct peer* p, void* arg){
    send_paxos_chosen(peer_get_buffer(p), arg);
}

static void
peer_send_prepare(struct peer* p, void* arg)
{
	send_paxos_prepare(peer_get_buffer(p), arg);
}


static void
peer_send_accept(struct peer* p, void* arg)
{
	send_paxos_accept(peer_get_buffer(p), arg);
}

static void
peer_send_trim(struct peer* p, void* arg) {
    send_paxos_trim(peer_get_buffer(p), arg);
}


// Begins one or more instances, defined by the preexec_window
static void
proposer_preexecute(struct evproposer* p) {
    int i;
    struct paxos_prepare pr;
    int count = p->preexec_window - proposer_prepare_count(p->state) - proposer_acceptance_count(p->state);

    if (count <= 0) return;
    for (i = 0; i < count; i++) {
        iid_t current_instance = proposer_get_next_instance_to_prepare(p->state);
        assert(current_instance != 0);
        proposer_set_current_instance(p->state, current_instance);
        paxos_log_debug("current proposing instance: %u", current_instance);
        bool prepred = proposer_try_to_start_preparing_instance(p->state, proposer_get_current_instance(p->state), &pr);
        if (prepred) {
            peers_for_n_acceptor(p->peers, peer_send_prepare, &pr, paxos_config.group_1);
        }
    }
    paxos_log_debug("Opened %d new instances", count);
}


/*
// Begins one or more instances, defined by the preexec_window
static void
proposer_preexecute(struct evproposer* p) {
	int i;
	struct paxos_prepare pr;
	int count = (p->preexec_window / p->proposers_count) - proposer_prepared_count(p->state) - proposer_acceptance_count(p->state);

    struct timeval t1;
    gettimeofday(&t1, NULL);
    srand(t1.tv_usec * t1.tv_sec);


    if (count <= 0) return;
    iid_t* tried_preparing_instances = malloc(sizeof(iid_t) * count);
    unsigned int tried_index = 0;
    unsigned int num_prepreed= 0;

    int min_possible_inst = proposer_get_next_instance_to_prepare(p->state);
    while (count > 0 || tried_index < count * 2) {
	    iid_t current_instance = random_between_excluding(min_possible_inst, min_possible_inst + p->preexec_window, tried_preparing_instances, tried_index);
	    assert(current_instance != 0);
	    proposer_set_current_instance(p->state, current_instance);
        paxos_log_debug("current proposing instance: %u", current_instance);
        bool new_prepare_for_instance = proposer_try_to_start_preparing_instance(p->state, proposer_get_current_instance(p->state), &pr);
        if (new_prepare_for_instance) {
            peers_for_n_acceptor(p->peers, peer_send_prepare, &pr, paxos_config.group_1);

            count--;
        }

        tried_preparing_instances[tried_index++] = current_instance;
	}

	free(tried_preparing_instances);
	paxos_log_debug("Opened %d new instances", count);
}
*/

static void
try_accept(struct evproposer* p)
{
	paxos_accept accept;
    while (proposer_try_accept(p->state, &accept)) {
        assert(&accept.value != NULL);
        assert(accept.value.paxos_value_val != NULL);
        assert(accept.value.paxos_value_len > 0);
        peers_for_n_acceptor(p->peers, peer_send_accept, &accept, paxos_config.group_2);
    }
    proposer_preexecute(p);
}


static void
evproposer_handle_promise(__unused struct peer* p, standard_paxos_message* msg, void* arg)
{
	struct evproposer* proposer = arg;
	struct paxos_prepare prepare;
	struct paxos_promise* promise = &msg->u.promise;

	if (promise->ballot.proposer_id != proposer->id)
	    return;

	int quorum_reached = proposer_receive_promise(proposer->state, promise, &prepare);
	if (quorum_reached)
	    try_accept(proposer);
}

static void
evproposer_handle_accepted(struct peer* p, standard_paxos_message* msg, void* arg)
{
	struct evproposer* proposer = arg;
	paxos_accepted* acc = &msg->u.accepted;

    if (acc->promise_ballot.proposer_id != proposer->id){
        return;
    }

    struct paxos_chosen chosen_msg;
    memset(&chosen_msg, 0, sizeof(struct paxos_chosen));

    assert(acc->value.paxos_value_len > 1);
    assert(acc->value_ballot.number > 0);

    if (proposer_receive_accepted(proposer->state, acc, &chosen_msg)){
        peers_foreach_acceptor(proposer->peers, peer_send_chosen, &chosen_msg);
        //peers_foreach_client(proposer->peers, peer_send_chosen, &chosen_msg);

        if (proposer_get_min_unchosen_instance(proposer->state) > chosen_msg.iid) {
            struct paxos_trim trim_msg = {.iid = chosen_msg.iid};
            peers_foreach_acceptor(proposer->peers, peer_send_trim, &trim_msg);
        }
        assert(chosen_msg.iid == acc->iid);
        assert(ballot_equal(&chosen_msg.ballot, acc->promise_ballot));
        assert(ballot_equal(&chosen_msg.ballot, acc->value_ballot));
        assert(is_values_equal(chosen_msg.value, acc->value));
    }
    try_accept(proposer);
}


static void
evproposer_handle_chosen(__unused struct peer* p, struct standard_paxos_message* msg, void* arg) {
    struct evproposer* proposer = arg;
    struct paxos_chosen* chosen_msg = &msg->u.chosen;

    proposer_receive_chosen(proposer->state, chosen_msg);

    backoff_manager_close_backoff_if_exists(proposer->backoff_manager, chosen_msg->iid);

    try_accept(proposer);
}

struct retry {
    struct evproposer* proposer;
    struct paxos_prepare* prepare;
    iid_t instance;
};

static void
evproposer_try_higher_ballot(evutil_socket_t fd, short event, void* arg) {
    struct retry* args =  arg;
    struct evproposer* proposer = args->proposer;
    iid_t instance = args->instance;

    paxos_log_debug("Trying next ballot %u, %u.%u", args->prepare->iid, args->prepare->ballot.number, args->prepare->ballot.proposer_id);
    peers_for_n_acceptor(proposer->peers, peer_send_prepare, args->prepare, paxos_config.group_1);
   paxos_prepare_free(args->prepare);

   try_accept(proposer);
}


static void
evproposer_handle_preempted(struct peer* p, standard_paxos_message* msg, void* arg)
{
    struct evproposer* proposer = arg;
    struct paxos_preempted preempted_msg = msg->u.preempted;

    if (preempted_msg.attempted_ballot.proposer_id != proposer->id) return;

    struct paxos_prepare* next_prepare = calloc(1, sizeof(struct paxos_prepare));
    assert(ballot_greater_than(preempted_msg.acceptor_current_ballot, preempted_msg.attempted_ballot));
    if (proposer_receive_preempted(proposer->state, &preempted_msg, next_prepare)) {
        assert(next_prepare->iid != 0);
        assert(ballot_greater_than(next_prepare->ballot, preempted_msg.acceptor_current_ballot));


        const struct timeval* current_backoff = backoff_manager_get_backoff(proposer->backoff_manager, preempted_msg.iid);


        paxos_log_debug("Trying next Ballot for Instance %u, %u.%u in %u microseconds", next_prepare->iid, next_prepare->ballot.number, next_prepare->ballot.proposer_id, current_backoff->tv_usec);

        struct retry* retry_args = calloc(1, sizeof(struct retry));
        *retry_args = (struct retry) {.proposer = proposer, .prepare = next_prepare, .instance = preempted_msg.iid};

        struct event* ev = evtimer_new(peers_get_event_base(proposer->peers), evproposer_try_higher_ballot, retry_args);
        event_add(ev, current_backoff);
    }
}


static void
evproposer_handle_trim(__unused struct peer* p, standard_paxos_message* msg, void* arg) {

    struct evproposer* proposer = arg;
    struct paxos_trim* trim_msg = &msg->u.trim;
    proposer_receive_trim(proposer->state, trim_msg);
    proposer_preexecute(proposer);

    backoff_manager_close_less_than_or_equal(proposer->backoff_manager, trim_msg->iid);
}

static void
evproposer_handle_client_value(__unused struct peer* p, standard_paxos_message* msg, void* arg)
{
    struct evproposer* proposer = arg;
	struct paxos_value* v = &msg->u.client_value;
	assert(v->paxos_value_len > 1);
	assert(v->paxos_value_val != NULL);
	assert(v->paxos_value_val != "");
	//assert(v->paxos_value_val != "");
    proposer_add_paxos_value_to_queue(proposer->state, v);
	try_accept(proposer);
}

static void
evproposer_handle_acceptor_state(__unused struct peer* p, standard_paxos_message* msg, void* arg)
{
	struct evproposer* proposer = arg;
	struct paxos_standard_acceptor_state* acc_state = &msg->u.state;
	proposer_receive_acceptor_state(proposer->state, acc_state);
	try_accept(proposer);

    backoff_manager_close_less_than_or_equal(proposer->backoff_manager, acc_state->trim_iid);

}

static void
evproposer_check_timeouts(__unused evutil_socket_t fd, __unused short event, void *arg)
{
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
evproposer_preexec_once(__unused evutil_socket_t fd,__unused short event, void *arg)
{
	struct evproposer* p = arg;
	proposer_preexecute(p);
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

static void evproposer_gen_random_seed(evutil_socket_t fd, short event, void* arg)  {
    struct evproposer* p = arg;
    random_seed_from_dev_rand();
    event_add(p->random_seed_event, &p->random_seed_timer);
}


struct evproposer*
evproposer_init_internal(int id, struct evpaxos_config* c, struct peers* peers, struct backoff_manager* backoff_manager)
{
	struct evproposer* p;
	int acceptor_count = evpaxos_acceptor_count(c);

	p = malloc(sizeof(struct evproposer));
	p->id = id;
	p->proposers_count = evpaxos_proposer_count(c);

    p->state = proposer_new(p->id, acceptor_count,paxos_config.quorum_1,paxos_config.quorum_2);
	p->preexec_window = paxos_config.proposer_preexec_window;

//	p->current_backoffs = kh_init_backoffs();
//	kh_init_retries();


	peers_subscribe(peers, PAXOS_PROMISE, evproposer_handle_promise, p);
	peers_subscribe(peers, PAXOS_ACCEPTED, evproposer_handle_accepted, p);
	peers_subscribe(peers, PAXOS_PREEMPTED, evproposer_handle_preempted, p);
	peers_subscribe(peers, PAXOS_CLIENT_VALUE, evproposer_handle_client_value, p);
	peers_subscribe(peers, PAXOS_ACCEPTOR_STATE, evproposer_handle_acceptor_state, p);
	peers_subscribe(peers, PAXOS_CHOSEN, evproposer_handle_chosen, p);
	peers_subscribe(peers, PAXOS_TRIM, evproposer_handle_trim, p);

	// Setup timeout
	struct event_base* base = peers_get_event_base(peers);
	p->tv.tv_sec = paxos_config.proposer_timeout;
	p->tv.tv_usec = 0;
	p->timeout_ev = evtimer_new(base, evproposer_check_timeouts, p);
	event_add(p->timeout_ev, &p->tv);

	p->random_seed_timer = (struct timeval) {.tv_sec = 30 + (rand() % 30), .tv_usec = 0};
	p->random_seed_event = evtimer_new(base, evproposer_gen_random_seed, p);

    event_add(p->random_seed_event, &p->random_seed_timer);
    random_seed_from_dev_rand();

	p->peers = peers;

	p->backoff_manager = backoff_manager;
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
	if (id < 0 || id >= MAX_N_OF_PROPOSERS) {
		paxos_log_error("Invalid proposer id: %d", id);
		return NULL;
	}

	struct peers* peers = peers_new(base, config);
	peers_connect_to_acceptors(peers);
	int port = evpaxos_proposer_listen_port(config, id);
	int rv = peers_listen(peers, port);
	if (rv == 0 ) // failure
		return NULL;

	//todo config mechanism to work out backoff type
	struct backoff* backoff = full_jitter_backoff_new(MAX_BACKOFF_TIME, MIN_BACKOFF_TIME, MAX_INITIAL_BACKOFF_TIME);
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


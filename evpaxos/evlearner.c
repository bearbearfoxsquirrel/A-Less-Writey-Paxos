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


#include "evpaxos.h"
#include "learner.h"
#include "standard_paxos_peers.h"
#include "standard_paxos_message.h"
#include <stdlib.h>
#include <event2/event.h>
#include <paxos_types.h>

struct evlearner
{
	struct learner* state;      /* The actual learner */
	deliver_function delfun;    /* Delivery callback */
	void* delarg;               /* The argument to the delivery callback */

    int min_chunks_missing;

    int comm_prop_num_trim;
    int comm_acc_num_trim;

    int comm_prop_num_chosen;
    int comm_acc_num_chosen;

	struct event* hole_timer;   /* Timer to check for holes */
	struct timeval tv;          /* Check for holes every tv units of time */
	struct standard_paxos_peers* peers;    /* Connections to acceptors */
};

static void
peer_send_trim(struct standard_paxos_peer* p, void* arg)
{
    send_paxos_trim(peer_get_buffer(p), arg);
}

static void
peer_send_repeat(struct standard_paxos_peer* p, void* arg)
{
	send_paxos_repeat(peer_get_buffer(p), arg);
}
static void

peer_send_chosen(struct standard_paxos_peer* p, void* arg){
    send_paxos_chosen(peer_get_buffer(p), arg);
}

static void
evlearner_check_holes( evutil_socket_t fd,  short event, void *arg)
{
	struct paxos_repeat msg;
	struct evlearner* l = arg;
    unsigned int chunks = l->min_chunks_missing;

	if (learner_has_holes(l->state, &msg.from, &msg.to) == 1) {
	    // hole here refers to the size of gap
		if ((msg.to - msg.from) > chunks)
			msg.to = msg.from + chunks;
		peers_foreach_acceptor(l->peers, peer_send_repeat, &msg);
	} else {
        iid_t next_trim = learner_get_instance_to_trim(l->state);
        iid_t current_trim =  learner_get_trim(l->state);
        if (next_trim > current_trim) {
            learner_set_trim(l->state, next_trim);
            struct paxos_trim trim_msg = (struct paxos_trim) {next_trim};
            paxos_log_debug("Sending Trim for Instance %u", next_trim);
            peers_for_n_acceptor(l->peers, peer_send_trim, &trim_msg, l->comm_acc_num_trim);
            peers_for_n_proposers(l->peers, peer_send_trim, &trim_msg, l->comm_prop_num_trim);
        }
        // can trim from the "from" in holes msg
    //    struct paxos_trim trim_msg = {.iid = msg.from};

      //  if (trim_msg.iid > l->trim_iid) {
            // set trim
            // is new trim?
        //    if (learner_get_trim(l->acceptor_state) < trim_msg.iid){
         //       learner_set_trim(l->acceptor_state, trim_msg.iid);
         //       peers_for_n_acceptor(l->peers, peer_send_trim, &trim_msg, l->comm_acc_num_trim);
         //       peers_for_n_proposers(l->peers, peer_send_trim, &trim_msg, l->comm_prop_num_trim);
         //   }
      //  }
	}
	event_add(l->hole_timer, &l->tv);
}

static void 
evlearner_deliver_next_closed(struct evlearner* l)
{
	paxos_accepted deliver;
	while (learner_deliver_next(l->state, &deliver.value)) {
		l->delfun(
		//	deliver.iid,
			deliver.value.paxos_value_val,
			deliver.value.paxos_value_len,
			l->delarg);
		paxos_accepted_destroy(&deliver);
	}
}

/*
	Called when an accept_ack is received, the learner will update it's status
    for that instance and afterwards check if the instance is closed
*/
static void
evlearner_handle_accepted( struct standard_paxos_peer* p, standard_paxos_message* msg, void* arg)
{
	struct evlearner* l = arg;
	struct paxos_chosen chosen_msg;
	paxos_log_debug("Recieved Acceptance for iid: %u from Acceptor %u at Ballot %u.%u", msg->u.accepted.iid,msg->u.accepted.aid, msg->u.accepted.value_ballot.number, msg->u.accepted.value_ballot.proposer_id);
	int chosen = learner_receive_accepted(l->state, &msg->u.accepted, &chosen_msg);

	if (chosen) {
     //   peers_foreach_proposer(l->peers, peer_send_chosen, &chosen_msg);
     peers_for_n_proposers(l->peers, peer_send_chosen, &chosen_msg, l->comm_prop_num_chosen);
     peers_for_n_acceptor(l->peers, peer_send_chosen, &chosen_msg, l->comm_acc_num_chosen);
//        peers_foreach_acceptor(l->peers, peer_send_chosen, &chosen_msg);
  //      paxos_chosen_destroy(&chosen_msg);

        evlearner_deliver_next_closed(l);
    }

}
/*
static void
evlearner_handle_trim( struct standard_paxos_peer* p, struct standard_paxos_message* msg, void* arg) {
    // not recevied at the moment
    struct evlearner* l = arg;
    struct paxos_trim trim_msg= msg->u.trim;
    learner_receive_trim(l->acceptor_state, &trim_msg);
}
*/
static void
evlearner_handle_chosen( struct standard_paxos_peer* p, struct standard_paxos_message* msg, void* arg){
    struct evlearner* l = arg;
    learner_receive_chosen(l->state, &msg->u.chosen);
    evlearner_deliver_next_closed(l);
}

static void evlearner_handle_trim(struct standard_paxos_peer* p, struct standard_paxos_message* msg, void* arg) {
    struct evlearner* l = arg;
    struct paxos_trim* trim = &msg->u.trim;
    learner_set_trim(l->state, trim->iid);
}

struct evlearner*
evlearner_init_internal(struct evpaxos_config* config, struct standard_paxos_peers* peers,
	deliver_function f, void* arg)
{
    struct evlearner* learner = malloc(sizeof(struct evlearner));
    int acceptor_count = evpaxos_acceptor_count(config);
    learner->state = learner_new(acceptor_count);

	struct event_base* base = peers_get_event_base(peers);

	learner->delfun = f;
	learner->delarg = arg;
	learner->peers = peers;
	
	peers_subscribe(peers, PAXOS_ACCEPTED, evlearner_handle_accepted, learner);
	peers_subscribe(peers, PAXOS_CHOSEN, evlearner_handle_chosen, learner);
	peers_subscribe(peers, PAXOS_TRIM, evlearner_handle_trim, learner);

    learner->comm_acc_num_chosen = paxos_config.lnr_comm_all_acc_chosen ? evpaxos_acceptor_count(config) : 1;
    learner->comm_prop_num_chosen = paxos_config.lnr_comm_all_prop_chosen ? evpaxos_proposer_count(config) : 1;

    learner->comm_acc_num_trim = paxos_config.lnr_comm_all_acc_trim ? evpaxos_acceptor_count(config) : 1;
    learner->comm_prop_num_trim = paxos_config.lnr_comm_all_prop_trim ? evpaxos_proposer_count(config) : 1;

    learner->min_chunks_missing = paxos_config.lnr_missing_chunks_before_repeats;

	// setup hole checking timer
	learner->tv.tv_sec = 0;
	learner->tv.tv_usec = 100000;
	learner->hole_timer = evtimer_new(base, evlearner_check_holes, learner);
	event_add(learner->hole_timer, &learner->tv);
	
	return learner;
}

struct evlearner *
evlearner_init(const char *config_file, deliver_function f, void *arg, struct event_base *b, int partner_id)
{
	struct evpaxos_config* c = evpaxos_config_read(config_file);
	if (c == NULL) return NULL;

    struct standard_paxos_peers* peers = peers_new(b, c);
	peers_connect_to_acceptors(peers, partner_id);
	peers_connect_to_proposers(peers, partner_id);

	struct evlearner* l = evlearner_init_internal(c, peers, f, arg);

	evpaxos_config_free(c);
	return l;
}

void
evlearner_free_internal(struct evlearner* l)
{
	event_free(l->hole_timer);
	learner_free(l->state);
	free(l);
}

void
evlearner_free(struct evlearner* l)
{
	peers_free(l->peers);
	evlearner_free_internal(l);
}

void
evlearner_set_instance_id(struct evlearner* l, unsigned iid)
{
	learner_set_instance_id(l->state, iid);
}


/*
void
evlearner_send_trim(struct evlearner* l, unsigned iid)
{
	paxos_trim trim = {iid};
	peers_foreach_acceptor(l->peers, peer_send_trim, &trim);
}*/


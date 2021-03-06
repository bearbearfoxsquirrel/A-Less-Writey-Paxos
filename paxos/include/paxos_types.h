/*
 * Copyright (c) 2013-2015, University of Lugano
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


#ifndef _PAXOS_TYPES_H_
#define _PAXOS_TYPES_H_

#include <stdint.h>
#include <stdbool.h>
#include "paxos_value.h"

#define INVALID_EPOCH 0
#define INVALID_INSTANCE 0
#define INVALID_BALLOT (struct ballot) {0, 0}
#define INVALID_EPOCH_BALLOT (struct epoch_ballot) {0, INVALID_BALLOT}
#define INVALID_VALUE (struct paxos_value) {0, NULL}

enum epoch_paxos_message_return_codes {
    MESSAGE_IGNORED,
    EPOCH_PREEMPTED,
    BALLOT_PREEMPTED,
    MESSAGE_ACKNOWLEDGED,
    QUORUM_REACHED,
    INSTANCE_CHOSEN,
    FALLEN_BEHIND
};

struct ballot {
    uint32_t number;
    uint32_t proposer_id;
};

struct paxos_prepare
{
	uint32_t iid;
	struct ballot ballot;
};

// This is used for both epoch write ahead paxos and the standard paxos message flow
typedef struct paxos_prepare paxos_prepare;

struct paxos_promise
{
	uint32_t aid;
	uint32_t iid;
	struct ballot ballot;
	struct ballot value_ballot;
	struct paxos_value value;
};
typedef struct paxos_promise paxos_promise;

struct paxos_accept
{
	uint32_t iid;
	struct ballot ballot;
	struct paxos_value value;
};
typedef struct paxos_accept paxos_accept;

struct paxos_accepted
{
	uint32_t aid;
	uint32_t iid;
	struct ballot promise_ballot;
	struct ballot value_ballot;
	struct paxos_value value;
};

typedef struct paxos_accepted paxos_accepted;


struct paxos_preempted
{
	uint32_t aid;
	uint32_t iid;
	struct ballot attempted_ballot;
	struct ballot acceptor_current_ballot;
};
typedef struct paxos_preempted paxos_preempted;

struct paxos_chosen {
    uint32_t iid;
    struct ballot ballot;
    struct paxos_value value;
};

struct paxos_repeat
{
	uint32_t from;
	uint32_t to;
};
typedef struct paxos_repeat paxos_repeat;

struct paxos_trim
{
	uint32_t iid;
};
typedef struct paxos_trim paxos_trim;

struct paxos_standard_acceptor_state
{
	uint32_t aid;
	uint32_t trim_iid;
    uint32_t current_instance;
};

struct proposer_state {
    uint32_t proposer_id;
    uint32_t trim_instance;
    uint32_t max_chosen_instance;
    uint32_t next_prepare_instance;
};

typedef struct paxos_standard_acceptor_state paxos_standard_acceptor_state;


enum paxos_message_type
{
	PAXOS_PREPARE,
	PAXOS_PROMISE,
	PAXOS_ACCEPT,
	PAXOS_ACCEPTED,
    PAXOS_CHOSEN,
	PAXOS_PREEMPTED,
	PAXOS_REPEAT,
	PAXOS_TRIM,
	PAXOS_PROPOSER_STATE,
	PAXOS_ACCEPTOR_STATE,
    PAXOS_CLIENT_VALUE,
};

typedef enum paxos_message_type paxos_message_type;

struct standard_paxos_message
{
	paxos_message_type type;
	union
	{
		struct paxos_prepare prepare;
		struct paxos_promise promise;
		struct paxos_accept accept;
		struct paxos_accepted accepted;
		struct paxos_preempted preempted;
		struct paxos_repeat repeat;
		struct paxos_trim trim;
		struct proposer_state proposer_state;
		struct paxos_standard_acceptor_state acceptor_state;
		struct paxos_value client_value;
		struct paxos_chosen chosen;
	} u;
};

typedef struct standard_paxos_message standard_paxos_message;


// EPOCH PAXOS MESSAGES


//Todo work out if requestor id is needed for responses?

struct epoch_ballot {
     uint32_t epoch;
     struct ballot ballot;
};


struct epoch_ballot_prepare {
    uint32_t instance;
    struct epoch_ballot epoch_ballot_requested;
};

struct epoch_ballot_promise {
    uint32_t acceptor_id;
    uint32_t instance;
    struct epoch_ballot promised_epoch_ballot;
    struct epoch_ballot last_accepted_ballot;
    struct paxos_value last_accepted_value;
};

struct epoch_ballot_accept {
    uint32_t instance;
    struct epoch_ballot epoch_ballot_requested;
    struct paxos_value value_to_accept;
};

struct epoch_ballot_accepted {
    uint32_t acceptor_id;
    uint32_t instance;
    struct epoch_ballot accepted_epoch_ballot;
    struct paxos_value accepted_value;
};

struct epoch_ballot_preempted {
    uint32_t acceptor_id;
    uint32_t instance;
    struct epoch_ballot requested_epoch_ballot;
    struct epoch_ballot acceptors_current_epoch_ballot;
};

struct epoch_notification {
    uint32_t new_epoch;
};

struct epoch_ballot_chosen {
    uint32_t instance;
    struct epoch_ballot chosen_epoch_ballot;
    struct paxos_value chosen_value;
};

struct writeahead_epoch_acceptor_state {
    struct paxos_standard_acceptor_state standard_acceptor_state;
    uint32_t current_epoch;
};

struct epoch_proposer_state {
    struct proposer_state proposer_state;
    uint32_t current_epoch;
};

enum writeahead_epoch_message_type {
    WRITEAHEAD_STANDARD_PREPARE,
    WRITEAHEAD_EPOCH_BALLOT_PREPARE,
    WRITEAHEAD_EPOCH_BALLOT_PROMISE,
    WRITEAHEAD_EPOCH_BALLOT_ACCEPT,
    WRITEAHEAD_EPOCH_BALLOT_ACCEPTED,
    WRITEAHEAD_EPOCH_BALLOT_PREEMPTED,
    WRITEAHEAD_EPOCH_NOTIFICATION,
    WRITEAHEAD_INSTANCE_CHOSEN_AT_EPOCH_BALLOT,
    WRITEAHEAD_PAXOS_PROPOSER_STATE,
    WRITEAHEAD_REPEAT,
    WRITEAHEAD_INSTANCE_TRIM,
    WRITEAHEAD_ACCEPTOR_STATE,
    WRITEAHEAD_CLIENT_VALUE,
};


enum epoch_paxos_prepare_type {
    STANDARD_PREPARE,
    EXPLICIT_EPOCH_PREPARE
};

struct  epoch_paxos_prepares {
    enum epoch_paxos_prepare_type type;
    union {
        struct paxos_prepare standard_prepare;
        struct epoch_ballot_prepare explicit_epoch_prepare;
    };
};

struct epoch_paxos_message {
    enum writeahead_epoch_message_type type;
    union {
        struct paxos_prepare standard_prepare;
        struct epoch_ballot_prepare epoch_ballot_prepare;
        struct epoch_ballot_promise epoch_ballot_promise;
        struct epoch_ballot_accept epoch_ballot_accept;
        struct epoch_ballot_accepted epoch_ballot_accepted;
        struct epoch_ballot_preempted epoch_ballot_preempted;
        struct epoch_ballot_chosen instance_chosen_at_epoch_ballot;
        struct epoch_notification epoch_notification;
        struct epoch_proposer_state epoch_proposer_state;

        struct paxos_repeat repeat;
        struct paxos_trim trim;
        struct writeahead_epoch_acceptor_state acceptor_state;
        struct paxos_value client_value;
    } message_contents ;
};


#endif

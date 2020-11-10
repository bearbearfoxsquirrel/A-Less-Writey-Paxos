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


#ifndef _LIBPAXOS_H_
#define _LIBPAXOS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <sys/types.h>
#include <paxos_types.h>
#include "paxos_value.h"

/* Paxos instance ids and ballots */
typedef uint32_t iid_t;
typedef uint32_t ballot_t;

/* Logging and verbosity levels */
typedef enum
{
	PAXOS_LOG_QUIET = 0,
	PAXOS_LOG_ERROR = 1,
	PAXOS_LOG_INFO  = 2,
	PAXOS_LOG_DEBUG = 3
} paxos_log_level;

/* Supported standard_stable_storage backends */
typedef enum
{
	PAXOS_MEM_STORAGE = 0,
	PAXOS_LMDB_STORAGE = 1
} paxos_storage_backend;

/* Configuration */
struct paxos_config
{
	/* General configuration */
	paxos_log_level verbosity;
	int tcp_nodelay;

	/* Learner */
	int learner_catch_up;

	/* Proposer */
	int proposer_timeout;
	int proposer_preexec_window;

	/* Acceptor */
	paxos_storage_backend storage_backend;
	int trash_files;
	int quorum_1;
	int quorum_2;
	int group_1;
	int group_2;

    /* lmdb standard_stable_storage configuration */
	int lmdb_sync;
	char *lmdb_env_path;
	size_t lmdb_mapsize;

    // writing ahead ballots for less writey ballots
    uint32_t ballots_written_ahead;
    uint32_t ballot_catchup;
    uint32_t ballot_windows_check_timer_microseconds;
    uint32_t ballot_windows_check_timer_seconds;

    // backoff variables
    uint32_t max_backoff_microseconds;
    uint32_t max_initial_backff_microseconds;
    uint32_t min_backoff_microseconds;
    uint32_t max_ballot_increment;

    // client benchmark variables
    uint32_t settle_in_time;
    uint32_t number_of_latencies_to_record;

    int messages_batched_average;
    int messages_batched_max;

    bool lnr_comm_all_prop_trim;
    bool lnr_comm_all_acc_trim;
    bool lnr_comm_all_prop_chosen;
    bool lnr_comm_all_acc_chosen;
    int lnr_missing_chunks_before_repeats;

    char* backoff_type;
    bool pessimistic_proposing;
    int reproposal_rate;
    bool repropose_values;
    bool round_robin_ballot_bias;
    bool round_robin_backoff;
};

extern struct paxos_config paxos_config;

/* Core functions */
struct paxos_value* paxos_value_new(const char* v, size_t s);
void paxos_value_free(struct paxos_value** v);
void paxos_value_destroy(struct paxos_value* v);
void paxos_promise_destroy(paxos_promise* p);
void paxos_accept_destroy(paxos_accept* a);
void paxos_accepted_destroy(paxos_accepted* a);
void paxos_chosen_destroy(struct paxos_chosen* chosen);
void paxos_message_destroy_contents(standard_paxos_message* m);

void writeahead_epoch_paxos_message_destroy_contents(struct epoch_paxos_message* m);

void paxos_accepted_free(paxos_accepted* a);
void paxos_prepare_free(struct paxos_prepare* prepare);
void paxos_accept_free(struct paxos_accept* accept);
void paxos_log(unsigned int level, const char* format, va_list ap);
void paxos_log_error(const char* format, ...);
void paxos_log_info(const char* format, ...);
void paxos_log_debug(const char* format, ...);


#ifdef __cplusplus
}
#endif

#endif

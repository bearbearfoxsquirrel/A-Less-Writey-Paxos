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


#include "paxos.h"
#include "paxos_value.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <paxos_types.h>

struct paxos_config paxos_config =
{
	.verbosity = PAXOS_LOG_INFO,
	.tcp_nodelay = 1,
	.learner_catch_up = 1,
	.proposer_timeout = 1,
	.proposer_preexec_window = 128,
	.storage_backend = PAXOS_MEM_STORAGE,
	.trash_files = 0,
	.lmdb_sync = 0,
	.quorum_1 = 2,
	.quorum_2 = 2,
	.group_1 = 2,
	.group_2 = 2,
	.lmdb_env_path = "/tmp/acceptor",
	.lmdb_mapsize = 10*1024*1024
};


struct paxos_value*
paxos_value_new(const char* value, size_t size)
{
	struct paxos_value* v = malloc(sizeof(struct paxos_value));
	v->paxos_value_len = size;
	v->paxos_value_val = malloc(sizeof(char)*size);
	memcpy(v->paxos_value_val, value, sizeof(char)*size );
	return v;
}

void
paxos_value_free(struct paxos_value** v)
{
    if (*v != NULL) {
        if ((*v)->paxos_value_val != NULL) {
            free((*v)->paxos_value_val);
            (*v)->paxos_value_val = NULL;
        }
        (*v)->paxos_value_len = 0;
        free(*v);
        *v = NULL;
    }
}

void
paxos_value_destroy(struct paxos_value* v)
{
    if (v != NULL) {
        if (v->paxos_value_val != NULL)
            free(v->paxos_value_val);
        v->paxos_value_val = NULL;
        v->paxos_value_len = 0;
    }

}

void
paxos_accepted_free(struct paxos_accepted* a)
{
	paxos_accepted_destroy(a);
	free(a);
}

void
paxos_accept_free(struct paxos_accept* accept){
    paxos_value_destroy(&accept->value);
    free(accept);
}


void
paxos_prepare_free(struct paxos_prepare* prepare) {
    free(prepare);
}

void
paxos_promise_destroy(struct paxos_promise* p)
{
	paxos_value_destroy(&p->value);
}

void
paxos_accept_destroy(struct paxos_accept* p)
{
	paxos_value_destroy(&p->value);
}

void
paxos_accepted_destroy(paxos_accepted* p)
{
	paxos_value_destroy(&p->value);
}

void
paxos_client_value_destroy(struct paxos_value* p)
{
	paxos_value_destroy(p);
}

void paxos_chosen_destroy(struct paxos_chosen* chosen) {
    paxos_value_destroy(&chosen->value);
}
void
paxos_message_destroy_contents(standard_paxos_message* m)
{
	switch (m->type) {
	case PAXOS_PROMISE:
		paxos_promise_destroy(&m->u.promise);
		break;
	case PAXOS_ACCEPT:
		paxos_accept_destroy(&m->u.accept);
		break;
	case PAXOS_ACCEPTED:
		paxos_accepted_destroy(&m->u.accepted);
		break;
	case PAXOS_CLIENT_VALUE:
		paxos_client_value_destroy(&m->u.client_value);
		break;
    case PAXOS_CHOSEN:
        paxos_chosen_destroy(&m->u.chosen);
        break;
	default: break;
	}
}

void epoch_ballot_promise_destroy(struct epoch_ballot_promise* promise) {
    paxos_value_destroy(&promise->last_accepted_value);
}

void epoch_ballot_accept_destroy(struct epoch_ballot_accept* accept) {
    paxos_value_destroy(&accept->value_to_accept);
}

void epoch_ballot_accepted_destroy(struct epoch_ballot_accepted* accepted) {
    paxos_value_destroy(&accepted->accepted_value);
}

void epoch_ballot_chosen_in_instance_destroy(struct epoch_ballot_chosen* chosen) {
    paxos_value_destroy(&chosen->chosen_value);
}

void writeahead_epoch_paxos_message_destroy_contents(struct writeahead_epoch_paxos_message* m) {
    switch (m->type) {

        case WRITEAHEAD_STANDARD_PREPARE:
            break;
        case WRITEAHEAD_EPOCH_BALLOT_PREPARE:
            break;
        case WRITEAHEAD_EPOCH_BALLOT_PROMISE:
            epoch_ballot_promise_destroy(&m->message_contents.epoch_ballot_promise);
            break;
        case WRITEAHEAD_EPOCH_BALLOT_ACCEPT:
            epoch_ballot_accept_destroy(&m->message_contents.epoch_ballot_accept);
            break;
        case WRITEAHEAD_EPOCH_BALLOT_ACCEPTED:
            epoch_ballot_accepted_destroy(&m->message_contents.epoch_ballot_accepted);
            break;
        case WRITEAHEAD_EPOCH_BALLOT_PREEMPTED:
            break;
        case WRITEAHEAD_EPOCH_NOTIFICATION:
            break;
        case WRITEAHEAD_INSTANCE_CHOSEN_AT_EPOCH_BALLOT:
            epoch_ballot_chosen_in_instance_destroy(&m->message_contents.instance_chosen_at_epoch_ballot);
            break;
        case WRITEAHEAD_REPEAT:
            break;
        case WRITEAHEAD_INSTANCE_TRIM:
            break;
        case WRITEAHEAD_ACCEPTOR_STATE:
            break;
        case WRITEAHEAD_CLIENT_VALUE:
            paxos_client_value_destroy(&m->message_contents.client_value);
            break;
    }
}

void
paxos_log(unsigned int level, const char* format, va_list ap)
{
	int off;
	char msg[1024];
	struct timeval tv;

	if (level > paxos_config.verbosity)
		return;

	gettimeofday(&tv,NULL);
	off = strftime(msg, sizeof(msg), "%d %b %H:%M:%S", localtime(&tv.tv_sec));
	tv.tv_usec %= 1000;
    sprintf(&msg[off],":%06ld. ", tv.tv_usec);
    off += 9;
//	strcat(off, )
	vsnprintf(msg+off, sizeof(msg)-off, format, ap);
	fprintf(stdout,"%s\n", msg);
}

void
paxos_log_error(const char* format, ...)
{
	va_list ap;
	va_start(ap, format);
	paxos_log(PAXOS_LOG_ERROR, format, ap);
	va_end(ap);
}

void
paxos_log_info(const char* format, ...)
{
	va_list ap;
	va_start(ap, format);
	paxos_log(PAXOS_LOG_INFO, format, ap);
	va_end(ap);
}

void
paxos_log_debug(const char* format, ...)
{
	va_list ap;
	va_start(ap, format);
	paxos_log(PAXOS_LOG_DEBUG, format, ap);
	va_end(ap);
}

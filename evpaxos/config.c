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
#include "evpaxos.h"
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <arpa/inet.h>

struct address
{
	char* addr;
	int port;
};

struct evpaxos_config
{
	int proposers_count;
	int acceptors_count;
	int client_count;

	// todo add learners so they can update each other

	struct address proposers[500]; //todo not use this
	struct address acceptors[500];
	struct address clients[500];
};

enum option_type
{
	option_boolean,
	option_integer,
	option_string,
	option_verbosity,
	option_backend,
	option_bytes
};

struct option
{
	const char* name;
	void* value;
	enum option_type type;
};
// TODO
// need to add in the ability to choose between standard paxos, window promises, and epoch promises
// need to add in the ability to choose between standard leadership and randomised back off
struct option options[] =
{
        { "verbosity", &paxos_config.verbosity, option_verbosity },
        {"tcp-nodelay",             &paxos_config.tcp_nodelay,             option_boolean },

        {"quorum-1",                &paxos_config.quorum_1,                option_integer },
        {"quorum-2",                &paxos_config.quorum_2,                option_integer },
        {"group-1",                 &paxos_config.group_1,                 option_integer },
        {"group-2",                 &paxos_config.group_2,                 option_integer },
        {"learner-catch-up",        &paxos_config.learner_catch_up,        option_boolean },
        {"proposer-check_timeout",        &paxos_config.proposer_timeout,        option_integer },
        {"acceptor-timeout", &paxos_config.acceptor_timeout, option_integer},

        {"proposer-preexec-window-max", &paxos_config.proposer_preexec_window_max,                         option_integer },
        {"proposer-preexec-window-min", &paxos_config.proposer_preexec_window_min, option_integer},

        {"max-ballot-increment", &paxos_config.max_ballot_increment,                               option_integer},
        {"min-backoff-microseconds", &paxos_config.min_backoff_microseconds,                       option_integer},
        {"max-initial-backoff-microseconds", &paxos_config.max_initial_backff_microseconds,        option_integer},
        {"max-backoff-microseconds", &paxos_config.max_backoff_microseconds,                       option_integer},

        {"promised-ballots-catchup", &paxos_config.ballot_catchup, option_integer},
        {"ballots-to-write-ahead", &paxos_config.ballots_written_ahead, option_integer},
        {"ballot-windows-check-timer-seconds", &paxos_config.ballot_windows_check_timer_seconds, option_integer},
        {"ballot-windows-check-timer-microseconds", &paxos_config.ballot_windows_check_timer_microseconds, option_integer},

        {"client-settle-in-time", &paxos_config.settle_in_time, option_integer},
        {"client-num-latencies-to-record", &paxos_config.number_of_latencies_to_record, option_integer},

        {"messages-batched-average", &paxos_config.messages_batched_average,                       option_integer},
        {"messages-batched-max", &paxos_config.messages_batched_max,                               option_integer},

        {"lnr-comm-all-acceptors-trim", &paxos_config.lnr_comm_all_acc_trim,                       option_boolean},
        {"lnr-comm-all-proposers-trim", &paxos_config.lnr_comm_all_prop_trim,                      option_boolean},

        {"lnr-comm-all-acceptors-chosen", &paxos_config.lnr_comm_all_acc_chosen,                   option_boolean},
        {"lnr-comm-all-proposers-chosen", &paxos_config.lnr_comm_all_prop_chosen,                  option_boolean},

        {"lnr-min-chunks-missing-before-repeats", &paxos_config.lnr_missing_chunks_before_repeats, option_integer},
        {"proposer-round-robin-ballot-bias", &paxos_config.round_robin_ballot_bias,                        option_boolean},
        {"proposer-round-robin-backoff", &paxos_config.round_robin_backoff, option_boolean},


        {"proposer-propose-values-in-parallel-instances", &paxos_config.repropose_values,          option_boolean},
        {"proposer-reproposal-rate-microseconds", &paxos_config.reproposal_rate,                   option_integer},

        {"stable-storage-backend",  &paxos_config.storage_backend,     option_backend},
        {"acceptor-trash-files",    &paxos_config.trash_files,         option_boolean },
        {"lmdb-sync",               &paxos_config.lmdb_sync,           option_boolean },
        {"lmdb-env-path",           &paxos_config.lmdb_env_path,       option_string },
        {"lmdb-mapsize",            &paxos_config.lmdb_mapsize,        option_bytes },

        {"fall-behind-jump", &paxos_config.fall_behind_jump,           option_integer},

        {"backoff-type", &paxos_config.backoff_type,                   option_string},
        {"pessimistic-proposing", &paxos_config.pessimistic_proposing, option_boolean},
        {"noop-timeout-us", &paxos_config.noop_timeout_us,             option_integer},
        {"preemptive-preempt", &paxos_config.premptive_preempt,         option_boolean},
        {"noop-await-holes", &paxos_config.await_holes, option_boolean},
        {0 }
};

static int parse_line(struct evpaxos_config* c, char* line);
static void address_init(struct address* a, char* addr, int port);
static void address_free(struct address* a);
static void address_copy(struct address* src, struct address* dst);
static struct sockaddr_in address_to_sockaddr(struct address* a);


struct evpaxos_config*
evpaxos_config_read(const char* path)
{
	struct stat sb;
	FILE* f = NULL;
	char line[512];
	int linenumber = 1;
	struct evpaxos_config* c = NULL;

	if ((f = fopen(path, "r")) == NULL) {
		perror("fopen");
		goto failure;
	}

	if (stat(path, &sb) == -1) {
		perror("stat");
		goto failure;
	}

	if (!S_ISREG(sb.st_mode)) {
		paxos_log_error("Error: %s is not a regular file\n", path);
		goto failure;
	}

	c = malloc(sizeof(struct evpaxos_config));
	if (c == NULL) {
		perror("malloc");
		goto failure;
	}
	memset(c, 0, sizeof(struct evpaxos_config));

    // check each line
	while (fgets(line, sizeof(line), f) != NULL) {
        // if line is a comment (#) or isn't a blank line (\n) then skip
        if (line[0] != '#' && line[0] != '\n') {
			if (parse_line(c, line) == 0) {
				paxos_log_error("Please, check line %d\n", linenumber);
				paxos_log_error("Error parsing config file %s\n", path);
				goto failure;
			}
		}
		linenumber++;
	}

	fclose(f);
	return c;

failure:
	free(c);
	if (f != NULL) fclose(f);
	return NULL;
}

void
evpaxos_config_free(struct evpaxos_config* config)
{
	int i;
	for (i = 0; i < config->proposers_count; ++i)
		address_free(&config->proposers[i]);
	for (i = 0; i < config->acceptors_count; ++i)
		address_free(&config->acceptors[i]);
	for (i = 0; i < config->client_count; i++)
		address_free(&config->clients[i]);
	free(config);
}

struct sockaddr_in
evpaxos_proposer_address(struct evpaxos_config* config, int i)
{
	return address_to_sockaddr(&config->proposers[i]);
}

struct sockaddr_in evpaxos_client_address(struct evpaxos_config* config, int i){
	return address_to_sockaddr(&config->clients[i]);
}

int
evpaxos_proposer_listen_port(struct evpaxos_config* config, int i)
{
	return config->proposers[i].port;
}

int
evpaxos_client_listen_port(struct evpaxos_config* config, int i)
{
	return config->clients[i].port;
}

// Reads the number of acceptors defined in the config
// file to determine how many aceptors it should expect to connect to
int
evpaxos_acceptor_count(struct evpaxos_config* config)
{
    return config->acceptors_count;
}

int evpaxos_proposer_count(struct evpaxos_config* config) {
    return config->proposers_count;
}

int evpaxos_client_count(struct evpaxos_config* config) {
	return config->client_count;
}

struct sockaddr_in
evpaxos_acceptor_address(struct evpaxos_config* config, int i)
{
	return address_to_sockaddr(&config->acceptors[i]);
}

int
evpaxos_acceptor_listen_port(struct evpaxos_config* config, int i)
{
	return config->acceptors[i].port;
}

static char*
strtrim(char* string)
{
	char *s, *t;
	for (s = string; isspace(*s); s++)
		;
	if (*s == 0)
		return s;
	t = s + strlen(s) - 1;
	while (t > s && isspace(*t))
		t--;
	*++t = '\0';
	return s;
}

static int
parse_bytes(char* str, size_t* bytes)
{
	char* end;
	errno = 0; /* To distinguish strtoll's return value 0 */
	*bytes = strtoull(str, &end, 10);
	if (errno != 0) return 0;
	while (isspace(*end)) end++;
	if (*end != '\0') {
		if (strcasecmp(end, "kb") == 0) *bytes *= 1024;
		else if (strcasecmp(end, "mb") == 0) *bytes *= 1024 * 1024;
		else if (strcasecmp(end, "gb") == 0) *bytes *= 1024 * 1024 * 1024;
		else return 0;
	}
	return 1;
}

static int
parse_boolean(char* str, int* boolean)
{
	if (str == NULL) return 0;
	if (strcasecmp(str, "yes") == 0) {
		*boolean = 1;
		return 1;
	}
	if (strcasecmp(str, "no") == 0) {
		*boolean = 0;
		return 1;
	}
	return 0;
}

static int
parse_integer(char* str, int* integer)
{
	int n;
	char* end;
	if (str == NULL) return 0;
	n = strtol(str, &end, 10);
	if (end == str) return 0;
	*integer = n;
	return 1;
}

static int
parse_string(char* str, char** string)
{
	if (str == NULL || str[0] == '\0' || str[0] == '\n')
		return 0;
	*string = strdup(str);
	// mem leak
	return 1;
}

static int
parse_address(char* str, struct address* addr)
{
	int id;
	int port;
	char address[128];
	int rv = sscanf(str, "%d %s %d", &id, address, &port);
	if (rv == 3) {
		address_init(addr, address, port);
		return 1;
	}
	return 0;
}

static int
parse_verbosity(char* str, paxos_log_level* verbosity)
{
	if (strcasecmp(str, "quiet") == 0) *verbosity = PAXOS_LOG_QUIET;
	else if (strcasecmp(str, "error") == 0) *verbosity = PAXOS_LOG_ERROR;
	else if (strcasecmp(str, "info") == 0) *verbosity = PAXOS_LOG_INFO;
	else if (strcasecmp(str, "debug") == 0) *verbosity = PAXOS_LOG_DEBUG;
	else return 0;
	return 1;
}

static int
parse_backend(char* str, paxos_storage_backend* backend)
{
	if (strcasecmp(str, "memory") == 0) *backend = PAXOS_MEM_STORAGE;
	else if (strcasecmp(str, "lmdb") == 0) *backend = PAXOS_LMDB_STORAGE;
	else return 0;
	return 1;
}

static struct option*
lookup_option(char* opt)
{
	int i = 0;
	while (options[i].name != NULL) {
		if (strcasecmp(options[i].name, opt) == 0)
			return &options[i];
		i++;
	}
	return NULL;
}

static int
parse_line(struct evpaxos_config* c, char* line)
{
	int rv;
	char* tok;
	char* sep = " ";
	struct option* opt;

	line = strtrim(line);
	tok = strsep(&line, sep);

	if (strcasecmp(tok, "a") == 0 || strcasecmp(tok, "acceptor") == 0) {

		struct address* addr = &c->acceptors[c->acceptors_count];
		c->acceptors_count++;
		return parse_address(line, addr);
	}

	if (strcasecmp(tok, "p") == 0 || strcasecmp(tok, "proposer") == 0) {

		struct address* addr = &c->proposers[c->proposers_count];
        c->proposers_count++;
		return parse_address(line, addr);
	}

	if (strcasecmp(tok, "c") == 0 || strcasecmp(tok, "client") == 0) {

		struct address* addr = &c->clients[c->client_count];
		c->client_count++;
		return parse_address(line, addr);
	}



	if (strcasecmp(tok, "r") == 0 || strcasecmp(tok, "replica") == 0) {

		struct address* pro_addr = &c->proposers[c->proposers_count++];
		struct address* acc_addr = &c->acceptors[c->acceptors_count++];
		rv = parse_address(line, pro_addr);
		address_copy(pro_addr, acc_addr);
		return rv;
	}

	line = strtrim(line);
	opt = lookup_option(tok);
	if (opt == NULL)
		return 0;

	switch (opt->type) {
		case option_boolean:
			rv = parse_boolean(line, opt->value);
			if (rv == 0) paxos_log_error("Expected 'yes' or 'no'\n");
			break;
		case option_integer:
			rv = parse_integer(line, opt->value);
			if (rv == 0) paxos_log_error("Expected number\n");
			break;
		case option_string:
			rv = parse_string(line, opt->value);
			if (rv == 0) paxos_log_error("Expected string\n");
			break;
		case option_verbosity:
			rv = parse_verbosity(line, opt->value);
			if (rv == 0) paxos_log_error("Expected quiet, error, info, or debug\n");
			break;
		case option_backend:
			rv = parse_backend(line, opt->value);
			if (rv == 0) paxos_log_error("Expected memory or lmdb\n");
			break;
		case option_bytes:
			rv = parse_bytes(line, opt->value);
			if (rv == 0) paxos_log_error("Expected number of bytes.\n");
	}

	return rv;
}

static void
address_init(struct address* a, char* addr, int port)
{
	a->addr = strdup(addr); // todo mem leak?
	a->port = port;
}

static void
address_free(struct address* a)
{
	free(a->addr);
}

static void
address_copy(struct address* src, struct address* dst)
{
	address_init(dst, src->addr, src->port);
}

static struct sockaddr_in
address_to_sockaddr(struct address* a)
{
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(a->port);
	addr.sin_addr.s_addr = inet_addr(a->addr);
	return addr;
}

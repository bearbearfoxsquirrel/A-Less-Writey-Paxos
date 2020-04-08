//
// Created by Michael Davis on 07/04/2020.
//


#include <stdlib.h>
#include <stdio.h>
#include <ev_epoch_paxos.h>
#include <signal.h>

static void
handle_sigint(int sig, short ev, void* arg)
{
    struct event_base* base = arg;
    printf("Caught signal %d\n", sig);
    event_base_loopexit(base, NULL);
}

static void
start_epoch_proposer(const char* config, int id)
{
    struct event* sig;
    struct event_base* base;
    struct ev_epoch_proposer* prop;

    base = event_base_new();
    sig = evsignal_new(base, SIGINT, handle_sigint, base);
    evsignal_add(sig, NULL);

    // todo read from the config file here which type of proposer to make

    prop = ev_epoch_proposer_init(id, config, base);
    if (prop == NULL) {
        printf("Could not start the proposer!\n");
        exit(1);
    }

    signal(SIGPIPE, SIG_IGN);
    event_base_dispatch(base);

    event_free(sig);
    ev_epoch_proposer_free(&prop);
    event_base_free(base);
}

int
main (int argc, char const *argv[])
{
    int id;
    const char* config = "../paxos.conf";

    if (argc != 2 && argc != 3) {
        printf("Usage: %s id [path/to/paxos.conf]\n", argv[0]);
        exit(1);
    }

    id = atoi(argv[1]);
    if (argc == 3)
        config = argv[2];

    start_epoch_proposer(config, id);

    return 0;
}

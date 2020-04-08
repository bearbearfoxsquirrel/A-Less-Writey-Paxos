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
start_acceptor(int id, const char* config)
{
    struct event_base* base;
    struct event* sig;

    base = event_base_new();
    sig = evsignal_new(base, SIGINT, handle_sigint, base);
    evsignal_add(sig, NULL);

    struct ev_epoch_acceptor* acc = ev_epoch_acceptor_init(id, config, base);
    if (acc == NULL) {
        printf("Could not start the acceptor\n");
        return;
    }

    signal(SIGPIPE, SIG_IGN);
    event_base_dispatch(base);

    event_free(sig);
    ev_epoch_acceptor_free(&acc);
    event_base_free(base);
}

int
main(int argc, char const *argv[])
{
    int id;
    const char* config = "../paxos.conf";

    if (argc != 2 && argc != 3) {
        printf("Usage: %s id [path/to/paxos.conf]\n", argv[0]);
        exit(0);
    }

    id = atoi(argv[1]);
    if (argc >= 3)
        config = argv[2];

    start_acceptor(id, config);

    return 1;
}

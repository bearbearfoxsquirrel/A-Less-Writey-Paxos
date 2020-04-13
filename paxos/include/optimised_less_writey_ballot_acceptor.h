//
// Created by Michael Davis on 13/04/2020.
//

#ifndef A_LESS_WRITEY_PAXOS_OPTIMISED_LESS_WRITEY_BALLOT_ACCEPTOR_H
#define A_LESS_WRITEY_PAXOS_OPTIMISED_LESS_WRITEY_BALLOT_ACCEPTOR_H
//
// Created by Michael Davis on 18/12/2019.
//



#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "paxos.h"
#include "standard_stable_storage.h"


struct optimised_less_writey_ballot_acceptor;

struct optimised_less_writey_ballot_acceptor* optimised_less_writey_ballot_acceptor_new(int id, int min_ballot_catchup, int bal_window);

iid_t optimised_less_writey_ballot_acceptor_get_trim(struct optimised_less_writey_ballot_acceptor* acceptor);

void optimised_less_writey_ballot_acceptor_free(struct optimised_less_writey_ballot_acceptor *a);

int optimised_less_writey_ballot_acceptor_receive_prepare(struct optimised_less_writey_ballot_acceptor *a,
                                                paxos_prepare *req, standard_paxos_message *out);

int optimised_less_writey_ballot_acceptor_receive_accept(struct optimised_less_writey_ballot_acceptor *a,
                                               paxos_accept *req, standard_paxos_message *out);

int optimised_less_writey_ballot_acceptor_receive_repeat(struct optimised_less_writey_ballot_acceptor *a,
                                     iid_t iid, struct standard_paxos_message *out);

int optimised_less_writey_ballot_acceptor_receive_trim(struct optimised_less_writey_ballot_acceptor *a, paxos_trim *trim);

void optimised_less_writey_ballot_acceptor_get_current_state(struct optimised_less_writey_ballot_acceptor *a, paxos_standard_acceptor_state *state);

void optimised_less_writey_ballot_acceptor_check_and_update_written_ahead_promise_ballot(struct optimised_less_writey_ballot_acceptor* acceptor);



int optimised_less_writey_ballot_acceptor_receive_chosen(struct optimised_less_writey_ballot_acceptor* a, struct paxos_chosen *chosen);


bool optimised_less_writey_ballot_acceptor_check_ballot_window(struct optimised_less_writey_ballot_acceptor* acceptor);


#ifdef __cplusplus
}
#endif




#endif //A_LESS_WRITEY_PAXOS_OPTIMISED_LESS_WRITEY_BALLOT_ACCEPTOR_H

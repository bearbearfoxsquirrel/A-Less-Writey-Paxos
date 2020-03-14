//
// Created by Michael Davis on 21/01/2020.
//

#include "paxos_types.h"
#include "paxos.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <paxos_types.h>
//#include <writeahead_epoch_acceptor.h>
#include "writeahead_window_acceptor.h"
#include "paxos_message_conversion.h"
#include "learner.h"
#include "proposer.h"


//void populate(struct paxos_accepted **to_populate, int* number) {
//    number = calloc(1, sizeof(int *)) ;
//    *number = 3;
//
//    *to_populate = calloc(1, sizeof(struct paxos_accepted));
//    (*to_populate)[0].ballot = 3;
//
//    (*to_populate) = realloc((*to_populate), 2* sizeof(struct paxos_accepted));
//    memset(&(*to_populate)[1], 0, sizeof(struct paxos_accepted));
//    (*to_populate)[1].ballot = 15;
//
//    (*to_populate) = realloc((*to_populate), 3* sizeof(struct paxos_accepted));
//    memset(&(*to_populate)[2], 0, sizeof(struct paxos_accepted));
//    (*to_populate)[2].ballot = 131234;
//}

int main(int argc, char const *argv[]){
//
//    struct writeahead_epoch_acceptor* acceptor = writeahead_epoch_acceptor_init(0);
//
//    struct paxos_prepare test_prepare_1 = {
//            .iid = 1,
//            .ballot = 5
//    };
//
//    struct paxos_prepare test_prepare_2 = {
//            .iid = 1,
//            .ballot = 10
//    };
//
//
//    struct epoch_ballot_prepare test_prepare_3 = {
//            .instance = 1,
//            .epoch_ballot_requested = {
//                    .epoch = 6,
//                    .ballot = 6
//            }
//    };
//
//
//    struct writeahead_epoch_paxos_message returned_message_1;
//    struct writeahead_epoch_paxos_message returned_message_2;
//    struct writeahead_epoch_paxos_message returned_message_3;
//    struct writeahead_epoch_paxos_message returned_message_4;
//    struct writeahead_epoch_paxos_message returned_message_5;
//
//    writeahead_epoch_acceptor_receive_prepare(acceptor, &test_prepare_1,&returned_message_1);
//    writeahead_epoch_acceptor_receive_epoch_ballot_prepare(acceptor, &test_prepare_3, & returned_message_4);
//
//    struct epoch_ballot_accept test_accept_1 = {
//            .instance = 1,
//            .epoch_ballot_requested = {.epoch = 6,
//                                       .ballot = 6
//            },
//            .value_to_accept = {
//                    .paxos_value_len = 1,
//                    .paxos_value_val = "y"
//            }
//    };
//
//    writeahead_epoch_acceptor_receive_epoch_ballot_accept(acceptor, &test_accept_1, &returned_message_5);
//    writeahead_epoch_acceptor_receive_prepare(acceptor, &test_prepare_2, &returned_message_2);
//    writeahead_epoch_acceptor_receive_prepare(acceptor, &test_prepare_1, &returned_message_3);
//
//
//    printf("%u" , returned_message_1.type);
//
//
//    return 0;

    struct proposer* proposer = proposer_new(0, 1, 1, 1);
    struct writeahead_window_acceptor* acceptor = write_ahead_window_acceptor_new(0, 1, 1, 5, 5, 5);
    struct learner* learner = learner_new(1);

    struct paxos_prepare prepare;
    proposer_try_to_start_preparing_instance(proposer, 3, &prepare);
    struct standard_paxos_message promise1;
    write_ahead_window_acceptor_receive_prepare(acceptor, &prepare, &promise1);
    proposer_receive_promise(proposer, &promise1.u.promise, NULL);

    struct paxos_value value = {2, "OK"};
    proposer_add_paxos_value_to_queue(proposer, &value);
    struct paxos_accept accept1;
    proposer_try_accept(proposer, &accept1); // fix

    struct standard_paxos_message accepted1;
    write_ahead_window_acceptor_receive_accept(acceptor, &accept1, &accepted1);


    struct paxos_chosen chosen;
    proposer_receive_accepted(proposer, &accepted1.u.accepted, &chosen);

    struct paxos_chosen learner_chosen;
    learner_receive_accepted(learner, &accepted1.u.accepted, &learner_chosen);

    write_ahead_ballot_acceptor_receive_chosen(acceptor, &chosen);

    learner_receive_chosen(learner, &chosen);





    paxos_log_debug("All done.");
}

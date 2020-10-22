//
// Created by Michael Davis on 07/02/2020.
//

#ifndef LIBPAXOS_INSTANCE_H
#define LIBPAXOS_INSTANCE_H


#include "paxos_types.h"
#include "paxos.h"
#include <sys/time.h>
#include "quorum.h"
#include "epoch_quorum.h"
#include "paxos_value.h"

struct proposer_common_instance_info {
    iid_t iid;
    struct ballot ballot;
    struct paxos_value* proposing_value;
    struct paxos_value* last_accepted_value;
    struct ballot last_accepted_ballot;
    struct timeval created_at;
};

struct standard_proposer_instance_info
{
    struct proposer_common_instance_info common_info;
    struct quorum quorum;
};

struct epoch_proposer_instance_info {
    struct proposer_common_instance_info common_info;
    struct quorum quorum;
    unsigned int current_epoch;
    unsigned int last_accepted_epoch_ballot_epoch;
   // struct epoch_quorum epoch_quorum;
};



#endif //LIBPAXOS_INSTANCE_H

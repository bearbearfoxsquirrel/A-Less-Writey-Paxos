#include "paxos.h"
#include "paxos_value.h"
#include <instance.h>

int
proposer_instance_info_has_value(struct proposer_common_instance_info *inst)
{
    return inst->proposing_value != NULL;
}

int
proposer_instance_info_has_promised_value(struct proposer_common_instance_info* inst)
{
    return inst->last_accepted_value != NULL;
}

bool
proposer_instance_info_has_timedout(struct proposer_common_instance_info* inst, struct timeval* now)
{
    long diff = now->tv_sec - inst->created_at.tv_sec;
    return diff >= paxos_config.proposer_timeout;
}

struct proposer_common_instance_info proposer_common_info_new(iid_t iid, struct ballot ballot) {
    struct proposer_common_instance_info common_info;
    common_info.iid = iid;
    common_info.ballot = (struct ballot) {.number = ballot.number, .proposer_id = ballot.proposer_id};
    common_info.last_accepted_ballot = (struct ballot) {.number = 0, .proposer_id = 0};
    common_info.proposing_value = NULL;
    common_info.last_accepted_value = NULL;
    gettimeofday(&common_info.created_at, NULL);
    return common_info;
}

void
proposer_common_instance_info_destroy_contents(struct proposer_common_instance_info* inst)
{
    if (proposer_instance_info_has_value(inst)) {
        paxos_value_free(&inst->proposing_value);
        inst->proposing_value = NULL;
    }
    if (proposer_instance_info_has_promised_value(inst)) {
        paxos_value_free(&inst->last_accepted_value);
        inst->proposing_value = NULL;
    }
}

void
proposer_instance_info_to_accept(struct proposer_common_instance_info* inst, paxos_accept* accept)
{
   // struct paxos_value* value_copy = paxos_value_new(inst->proposing_value->paxos_value_val, inst->proposing_value->paxos_value_len);
    *accept = (struct paxos_accept) {
            .iid = inst->iid,
            .ballot = (struct ballot) {.number = inst->ballot.number, .proposer_id = inst->ballot.proposer_id},
                    .value = (struct paxos_value) {.paxos_value_val = inst->proposing_value->paxos_value_val, .paxos_value_len = inst->proposing_value->paxos_value_len}
           // .value = (struct paxos_value) {.paxos_value_len = value_copy->paxos_value_len, .paxos_value_val = value_copy->paxos_value_val}
    };
}




#include "paxos.h"
#include "paxos_value.h"
#include <instance.h>

int
proposer_instance_info_has_value(struct proposer_common_instance_info *inst)
{
    return inst->value_to_propose != NULL;
}

int
proposer_instance_info_has_promised_value(struct proposer_common_instance_info* inst)
{
    return inst->last_promised_value != NULL;
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
    common_info.last_promised_values_ballot = (struct ballot) {.number = 0, .proposer_id = 0};
    common_info.value_to_propose = NULL;
    common_info.last_promised_value = NULL;
    gettimeofday(&common_info.created_at, NULL);
    return common_info;
}

void
proposer_common_instance_info_free(struct proposer_common_instance_info* inst)
{
    if (proposer_instance_info_has_value(inst)) {
        paxos_value_free(inst->value_to_propose);
    }
    if (proposer_instance_info_has_promised_value(inst))
        paxos_value_free(inst->last_promised_value);
    free(inst);
}

void
proposer_instance_info_to_accept(struct proposer_common_instance_info* inst, paxos_accept* accept)
{
    *accept = (struct paxos_accept) {
            .iid = inst->iid,
            .ballot = (struct ballot) {.number = inst->ballot.number, .proposer_id = inst->ballot.proposer_id},
            .value = (struct paxos_value) { inst->value_to_propose->paxos_value_len,
              inst->value_to_propose->paxos_value_val }
    };
}




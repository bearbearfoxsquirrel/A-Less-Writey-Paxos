----------------------- MODULE EpochMaxPromisesPaxos -----------------------

EXTENDS Integers

CONSTANTS Rounds, Acceptors, ProposableValues, 
            PromiseQuorums, AcceptQuorums, Epoch
            
VARIABLES acceptor_states, learnt, sent_messages

None == "Pineapple" \* CHOOSE v : v \notin ProposableValues


ASSUME /\ Rounds \subseteq Nat
       /\ AcceptQuorums \subseteq SUBSET Acceptors
       /\ PromiseQuorums \subseteq SUBSET Acceptors
       /\ \A promise_quorum \in PromiseQuorums, accept_quorum \in AcceptQuorums: 
                                    (promise_quorum \intersect accept_quorum) /= {} 
       /\ None \notin ProposableValues 


AllowedMessages ==
           [type : {"promise request"}, 
             round : Rounds] 
    \union [type : {"promise"}, 
             acceptor : Acceptors, 
             round : Rounds, 
             max_accepted_round : Rounds \union {-1}, 
             max_accepted_value : ProposableValues \union {None}] 
    \union [type : {"accept request"}, 
             round : Rounds, 
             value : ProposableValues]
    \union [type : {"accepted"}, 
             acceptor : Acceptors, 
             round : Rounds,  
             value : ProposableValues] 
    \union [type : {"increment ballot number"}, 
             new_max_round : Rounds] 



PaxosTypeInvariant == 
    /\ acceptor_states \in [Acceptors -> [max_round_promised : Rounds \union {-1} \union{1},
                                          max_round_before_crash : Rounds, 
                                          max_accepted_round : Rounds \union {-1},
                                          max_accepted_value : ProposableValues \union {None}]] \* acceptor states
    /\ sent_messages \subseteq AllowedMessages
    /\ learnt \subseteq ProposableValues

------------------------------------------------------------------------------

InitialState == 
    /\ sent_messages = {} \* no messages sent
    /\ acceptor_states = [acceptor \in Acceptors |-> [max_round_promised |-> -1,
                                                      max_round_before_crash |-> -1,
                                                      max_accepted_round |-> -1,
                                                      max_accepted_value |-> None
                                                    ]]\* no ballots promised 
    /\ learnt = {} \* no values learnt



Send(message) == sent_messages' = sent_messages \union {message}

MessageSent(message) == \E sent_message \in sent_messages: sent_message = message



SendPromiseRequests(requesting_round) == 
    /\ ~MessageSent([type |-> "promise request", round |-> requesting_round])
    /\ Send([type |-> "promise request", round |-> requesting_round])
    /\ UNCHANGED <<learnt, acceptor_states>>


SendPromise(acceptor, requested_round) == 
    /\ MessageSent([type |-> "promise request", round |-> requested_round])
    /\ acceptor_states[acceptor].max_round_promised =< requested_round 
    /\ IF acceptor_states[acceptor].max_round_before_crash =< requested_round THEN 
            acceptor_states' = [acceptor_states EXCEPT ![acceptor].max_round_promised = requested_round,
                                                       ![acceptor].max_round_before_crash = requested_round + Epoch]
       ELSE
            acceptor_states' = [acceptor_states EXCEPT ![acceptor].max_round_promised = requested_round]
    /\ Send([type |-> "promise", 
             acceptor |-> acceptor, 
             round |-> requested_round,
             max_accepted_round |-> acceptor_states[acceptor].max_accepted_round,
             max_accepted_value |-> acceptor_states[acceptor].max_accepted_value
             ])
   /\ UNCHANGED learnt


IsPromiseQuorum(round, quorum) == 
    \A acceptor \in quorum:
        \E message \in sent_messages: /\ message.type = "promise"
                                      /\ message.acceptor = acceptor   
                                      /\ message.round = round       
                                                                                                    
               
\* PromiseQuorumMessages 
\* all promises in the quorum contain no value accepteds
\* or the highest ordered ballot accepted has the value of the one being proposed                                                                                                                
IsValueSafeInPromiseQuorum(quorum, round, value) == 
    \/ \E message_a \in sent_messages: /\ message_a.type = "promise"
                                       /\ message_a.round = round 
                                       /\ message_a.max_accepted_value = value
                                       /\ message_a.acceptor \in quorum
                                       /\ \A message_b \in sent_messages \ {message_a}: 
                                            (/\ message_b.type = "promise" 
                                             /\ message_b.max_accepted_value /= message_a.max_accepted_value 
                                             /\ message_b.acceptor \in quorum
                                             /\ message_b.round = round) 
                                         => (message_b.max_accepted_round < message_a.max_accepted_round)              
                                             
    \/ \A acceptor \in quorum:
            \E message \in sent_messages: /\ message.type = "promise"
                                            /\ message.acceptor = acceptor
                                            /\ message.round = round 
                                            /\ message.max_accepted_round = -1
                                            /\ message.max_accepted_value = None          
                                            
AcceptRequestAlreadyMadeWithDifferentValue(round, value) ==
    \E other_value \in ProposableValues: MessageSent([type |-> "accept request",
                                                      round |-> round,
                                                      value |-> other_value ])
                                              
                                                                
                                                                                                                                                                                                       
                                             
SendAcceptRequest(round, value) == 
       /\ ~AcceptRequestAlreadyMadeWithDifferentValue(round, value)
       /\ \E promise_quorum \in PromiseQuorums: /\ IsPromiseQuorum(round, promise_quorum) 
                                                /\ IsValueSafeInPromiseQuorum(promise_quorum, round, value)   \* exists a promise quroum where the value is the max accepted value
       /\ Send([type |-> "accept request",
                round |-> round,
                value |-> value])  
       /\ UNCHANGED <<acceptor_states, learnt>>                               
           
SendAcceptedMessage(acceptor, round) == 
        /\ acceptor_states[acceptor].max_round_promised =< round
        /\ \E message \in sent_messages:    
             /\ message.type = "accept request"
             /\ message.round = round   
             /\ IF acceptor_states[acceptor].max_round_before_crash < round THEN 
                acceptor_states' = [acceptor_states EXCEPT ![acceptor].max_accepted_round = round,
                                                           ![acceptor].max_accepted_value = message.value,
                                                           ![acceptor].max_round_promised = round,
                                                           ![acceptor].max_round_before_crash = round + Epoch]
                ELSE    
                acceptor_states' = [acceptor_states EXCEPT ![acceptor].max_accepted_round = round,
                                                           ![acceptor].max_accepted_value = message.value,
                                                           ![acceptor].max_round_promised = round]                                          
             /\ Send([type |-> "accepted", 
                      acceptor |-> acceptor, 
                      round |-> round, 
                      value |-> message.value])
         /\ UNCHANGED <<learnt>>                                           
                                                                                                                                                                            
                                                           
Restart(acceptor) ==
        /\ acceptor_states' = [acceptor_states EXCEPT ![acceptor].max_round_before_crash = acceptor_states[acceptor].max_round_before_crash + Epoch,
                                                      ![acceptor].max_round_promised = acceptor_states[acceptor].max_round_before_crash]
        /\ UNCHANGED <<sent_messages, learnt>>  
        
Learn(round, value) ==
        /\ \E accept_quorum \in AcceptQuorums: 
            \A acceptor \in accept_quorum: 
                MessageSent([type |-> "accepted", 
                            acceptor |-> acceptor, 
                            round |-> round,
                            value |-> value])
       /\ learnt' = learnt \union {value}
       /\ UNCHANGED <<acceptor_states, sent_messages>>
       
NextState == \/ \E round \in Rounds: 
                    \/ SendPromiseRequests(round)
                    \/ \E acceptor \in Acceptors: 
                        \/ SendPromise(acceptor, round)
                        \/ SendAcceptedMessage(acceptor, round)
                    \/ \E value \in ProposableValues:
                        \/ SendAcceptRequest(round, value)
                        \/ Learn(round, value)
             \/ \E acceptor \in Acceptors: 
                    Restart(acceptor)
            
PaxosSpecification == InitialState \/ [][NextState]_<<acceptor_states, sent_messages, learnt>>

-----------------------------------------------------------------------------------------------------------------
INSTANCE Consensus


THEOREM PaxosImplementsConsensus == 
    PaxosSpecification => ConsensusSpecification 

THEOREM PaxosTypeInvariantMaintained == 
    PaxosSpecification => []PaxosTypeInvariant
    
THEOREM ConsensusAchieved == 
    PaxosSpecification => []/\ \/ learnt = {} 
                               \/ \E value \in ProposableValues: learnt = {value}
                            /\ PaxosTypeInvariant
                                              
ConsensusOK == 
    \/ learnt = {} 
    \/ \E value \in ProposableValues: learnt = {value}
=============================================================================
\* Modification History
\* Last modified Sat Sep 28 14:51:14 BST 2019 by Michael
\* Created Mon Sep 23 12:27:35 BST 2019 by Michael

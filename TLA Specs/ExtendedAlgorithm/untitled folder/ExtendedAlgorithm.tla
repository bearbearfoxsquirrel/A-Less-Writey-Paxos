 ----------------------------- MODULE PrefixPaxos -----------------------------
EXTENDS Integers

CONSTANTS Rounds, Acceptors, ProposableValues, 
            PromiseQuorums, AcceptQuorums, Prefixes
            
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
    \union [type : {"prefix promise"}, 
             acceptor : Acceptors, 
             round : Rounds, 
             prefix : Prefixes,
             max_accepted_prefix : Prefixes \union {-1}, 
             max_accepted_round : Rounds \union {-1}, 
             max_accepted_value : ProposableValues \union {None}] 
    \union [type : {"accept request"}, 
             prefix: Prefixes, 
             round : Rounds, 
             value : ProposableValues]
    \union [type : {"accepted"}, 
             acceptor : Acceptors, 
             prefix: Prefixes, 
             round : Rounds,  
             value : ProposableValues] 
    \union [type : {"new prefix"}, 
             prefix : Prefixes] 



PrefixPaxosTypeInvariant == 
    /\ acceptor_states \in [Acceptors -> [current_prefix : Prefixes,
                                          max_round_promised : Rounds \union {-1} \union{1},
                                          max_accepted_prefix : Prefixes \union {-1},
                                          max_accepted_round : Rounds \union {-1},
                                          max_accepted_value : ProposableValues \union {None}]] \* acceptor states
    /\ sent_messages \subseteq AllowedMessages
    /\ learnt \subseteq ProposableValues

------------------------------------------------------------------------------

InitalState == 
    /\ sent_messages = {} \* no messages sent
    /\ acceptor_states = [acceptor \in Acceptors |-> [current_prefix |-> 1, 
                                                      max_round_promised |-> -1,
                                                      max_accepted_prefix |-> -1,
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


SendPrefixPromise(acceptor, requested_round) == 
    /\ MessageSent([type |-> "promise request", round |-> requested_round])
    /\ acceptor_states[acceptor].max_round_promised =< requested_round 
    /\ acceptor_states' = [acceptor_states EXCEPT ![acceptor].max_round_promised = requested_round]
    /\ Send([type |-> "prefix promise", 
             acceptor |-> acceptor, 
             round |-> requested_round,
             prefix |-> acceptor_states[acceptor].current_prefix,
             max_accepted_prefix |-> acceptor_states[acceptor].max_accepted_prefix,
             max_accepted_round |-> acceptor_states[acceptor].max_accepted_round,
             max_accepted_value |-> acceptor_states[acceptor].max_accepted_value
             ])
   /\ UNCHANGED learnt


IsPrefixPromiseQuorum(prefix, round, quorum) == 
    \A acceptor \in quorum:
        \E message \in sent_messages: /\ message.type = "prefix promise"
                                      /\ message.acceptor = acceptor   
                                      /\ message.prefix = prefix      
                                      /\ message.round = round       
                                       
\* no higher ordered ballot accepted by any other acceptor in the promise quorum
\*                                                                                                                
               
\*PromiseQuorumMessages 
\* all promises in the quorum contain no value accepteds
\* or the highest ordered prefix ballot accepted has the value of the one being proposed                                                                                                                
IsValueSafeInPrefixPromiseQuorum(quorum, prefix, round, value) == 
    \/ \E message_a \in sent_messages: /\ message_a.type = "prefix promise"
                                       /\ message_a.prefix = prefix      
                                       /\ message_a.round = round 
                                       /\ message_a.max_accepted_value = value
                                       /\ message_a.acceptor \in quorum
                                       /\ \A message_b \in sent_messages \ {message_a}: 
                                            (/\ message_b.type = "prefix promise" 
                                             /\ message_b.max_accepted_value /= message_a.max_accepted_value 
                                             /\ message_b.acceptor \in quorum
                                             /\ message_b.round = round 
                                             /\ message_b.prefix = prefix) 
                                         => (\/  /\ message_b.max_accepted_round < message_a.max_accepted_round
                                                 /\ message_b.max_accepted_prefix = message_a.max_accepted_prefix 
                                             \/ message_b.max_accepted_prefix < message_a.max_accepted_prefix)                \*((message_b.type = "prefix promise" /\ message_b.prefix = prefix /\ message_b.round = round /\ message_b.acceptor \in quorum) => (message_a.max_accepted_prefix > message_b.max_accepted_prefix \/ message_a.max_accepted_round > message_b.max_accepted_round \/ message_a.max_accepted_value = message_b.max_accepted_value))
    \/ \A acceptor \in quorum:  
            \E message \in sent_messages: /\ message.type = "prefix promise"
                                          /\ message.acceptor = acceptor
                                          /\ message.prefix = prefix      
                                          /\ message.round = round 
                                          /\ message.max_accepted_prefix = -1
                                          /\ message.max_accepted_round = -1
                                          /\ message.max_accepted_value = None          
                                            
AcceptRequestAlreadyMadeWithDifferentValue(prefix, round, value) ==
    \E other_value \in ProposableValues: MessageSent([type |-> "accept request",
                                                      prefix |-> prefix,
                                                      round |-> round,
                                                      value |-> other_value ])
                                               
  (*  \E message \in sent_messages:        /\ message.type = "accept request"
                                        /\ message.prefix = prefix
                                         /\ message.round = round
                                         /\ message.value /= value     
                                         
      Other formulation of condition <- one used should be faster to check                                   *)
                                         
                                                                
                                                                                                                                                                                                       
                                             
SendAcceptRequest(prefix, round, value) == 
       /\ ~AcceptRequestAlreadyMadeWithDifferentValue(prefix, round, value)
       /\ \E promise_quorum \in PromiseQuorums: /\ IsPrefixPromiseQuorum(prefix, round, promise_quorum) 
                                                /\ IsValueSafeInPrefixPromiseQuorum(promise_quorum, prefix, round, value)   \* exists a promise quroum where the value is the max accepted value
       /\ Send([type |-> "accept request",
                prefix |-> prefix,
                round |-> round,
                value |-> value])  
       /\ UNCHANGED <<acceptor_states, learnt>>                               
           
SendAcceptedMessage(acceptor, prefix, round) == 
        /\ acceptor_states[acceptor].current_prefix =< prefix 
        /\ acceptor_states[acceptor].max_round_promised =< round
        /\ \E message \in sent_messages:    
             /\ message.type = "accept request"
             /\ message.prefix = prefix
             /\ message.round = round   
             /\ acceptor_states' = [acceptor_states EXCEPT ![acceptor].max_accepted_round = round,
                                                           ![acceptor].max_accepted_prefix = prefix,
                                                           ![acceptor].max_accepted_value = message.value,
                                                           ![acceptor].max_round_promised = round,
                                                           ![acceptor].current_prefix = prefix]
             /\ Send([type |-> "accepted", 
                      acceptor |-> acceptor, 
                      prefix |-> prefix, 
                      round |-> round, 
                      value |-> message.value])
         /\ UNCHANGED <<learnt>>                                           
                                                          
Restart(acceptor, prefix) ==
	/\ prefix >  acceptor_states[acceptor].current_prefix 
	/\ acceptor_states' = [acceptor_states EXCEPT ![acceptor].current_prefix = prefix,
						      ![acceptor].max_round_promised = -1]
	/\ MessageSent([type |-> "new prefix", prefix |-> prefix])
        /\ UNCHANGED <<learnt>>
                                                  
BeginNewPrefix(prefix) ==
        /\ \A acceptor_b \in Acceptors: acceptor_states[acceptor_b].current_prefix < prefix
        /\ \E acceptor_b \in Acceptors: acceptor_states[acceptor_b].current_prefix = (prefix - 1) \* prefix 1 higher than old current prefix - not really needed 
        /\ Send([type |-> "new prefix", prefix |-> prefix])   
        /\ UNCHANGED <<learnt, acceptor_states>>                                                                                                                            
                                                           
RecievePrefixNotification(acceptor, prefix) ==
        /\ acceptor_states[acceptor].current_prefix < prefix
        /\ MessageSent([type |-> "new prefix", prefix |-> prefix])
        /\ acceptor_states' = [acceptor_states EXCEPT ![acceptor].current_prefix = prefix,
                                                      ![acceptor].max_round_promised = -1]
        /\ UNCHANGED <<sent_messages, learnt>>  
        
Learn(prefix, round, value) ==
        /\ \E accept_quorum \in AcceptQuorums: 
            \A acceptor \in accept_quorum: 
                MessageSent([type |-> "accepted", 
                            acceptor |-> acceptor, 
                            prefix |-> prefix, 
                            round |-> round,
                            value |-> value])
       /\ learnt' = learnt \union {value}
       /\ UNCHANGED <<acceptor_states, sent_messages>>
       
NextState == \/ \E round \in Rounds: 
                    \/ SendPromiseRequests(round)
                    \/ \E acceptor \in Acceptors: 
                        \/ SendPrefixPromise(acceptor, round)
                        \/ \E prefix \in Prefixes: SendAcceptedMessage(acceptor, prefix, round)
                    \/ \E prefix \in Prefixes, value \in ProposableValues:
                        \/ SendAcceptRequest(prefix, round, value)
                        \/ Learn(prefix, round, value)
             \/ \E acceptor \in Acceptors, prefix \in Prefixes: 
                    \/ Restart(acceptor, prefix)
			\*\/ BeginNewPrefix(prefix)
                    \/ RecievePrefixNotification(acceptor, prefix)
            
PrefixPaxosSpecification == InitalState \/ [][NextState]_<<acceptor_states, sent_messages, learnt>>

-----------------------------------------------------------------------------------------------------------------
INSTANCE Consensus

THEOREM PrefixPaxosImplementsConsensus == 
    PrefixPaxosSpecification => ConsensusSpecification 

THEOREM PrefixPaxosTypeInvariantMaintained == 
    PrefixPaxosSpecification => []PrefixPaxosTypeInvariant
    
THEOREM ConsensusAchieved == 
    PrefixPaxosSpecification => []/\ \/ learnt = {} 
                                    \/ \E value \in ProposableValues: learnt = {value}
                                 /\ PrefixPaxosTypeInvariant
                                              
ConsensusOK == /\ PrefixPaxosTypeInvariant
	       /\ \/ learnt = {}
	          \/ \E value \in ProposableValues: learnt = {value}
    
 
=============================================================================
\* Modification History
\* Last modified Thu Aug 15 18:03:30 BST 2019 by Michael
\* Created Fri Aug 09 16:48:26 BST 2019 by Michael

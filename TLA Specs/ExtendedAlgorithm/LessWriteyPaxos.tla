 ----------------------------- MODULE LessWriteyPaxos -----------------------------
EXTENDS Integers

CONSTANTS Rounds, Acceptors, ProposableValues, 
            PromiseQuorums, AcceptQuorums, Epochs
            
VARIABLES acceptor_states, learnt, sent_messages

None == "NONE" \* CHOOSE v : v \notin ProposableValues


ASSUME /\ Rounds \subseteq Nat
       /\ AcceptQuorums \subseteq SUBSET Acceptors
       /\ PromiseQuorums \subseteq SUBSET Acceptors
       /\ \A promise_quorum \in PromiseQuorums, accept_quorum \in AcceptQuorums: 
                                    (promise_quorum \intersect accept_quorum) /= {} 
       /\ None \notin ProposableValues 



AllowedMessages ==
           [type : {"promise request"}, 
             round : Rounds] 
    \union [type : {"Epoch promise"}, 
             acceptor : Acceptors, 
             round : Rounds, 
             Epoch : Epochs,
             max_accepted_Epoch : Epochs \union {-1}, 
             max_accepted_round : Rounds \union {-1}, 
             max_accepted_value : ProposableValues \union {None}] 
    \union [type : {"accept request"}, 
             Epoch: Epochs, 
             round : Rounds, 
             value : ProposableValues]
    \union [type : {"accepted"}, 
             acceptor : Acceptors, 
             Epoch: Epochs, 
             round : Rounds,  
             value : ProposableValues] 
    \union [type : {"new Epoch"}, 
             Epoch : Epochs] 



EpochPaxosTypeInvariant == 
    /\ acceptor_states \in [Acceptors -> [current_Epoch : Epochs,
                                          max_round_promised : Rounds \union {-1} \union{1},
                                          max_accepted_Epoch : Epochs \union {-1},
                                          max_accepted_round : Rounds \union {-1},
                                          max_accepted_value : ProposableValues \union {None}]] \* acceptor states
    /\ sent_messages \subseteq AllowedMessages
    /\ learnt \subseteq ProposableValues

------------------------------------------------------------------------------

InitalState == 
    /\ sent_messages = {} \* no messages sent
    /\ acceptor_states = [acceptor \in Acceptors |-> [current_Epoch |-> 1, 
                                                      max_round_promised |-> -1,
                                                      max_accepted_Epoch |-> -1,
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


SendEpochPromise(acceptor, requested_round) == 
    /\ MessageSent([type |-> "promise request", round |-> requested_round])
    /\ acceptor_states[acceptor].max_round_promised =< requested_round 
    /\ acceptor_states' = [acceptor_states EXCEPT ![acceptor].max_round_promised = requested_round]
    /\ Send([type |-> "Epoch promise", 
             acceptor |-> acceptor, 
             round |-> requested_round,
             Epoch |-> acceptor_states[acceptor].current_Epoch,
             max_accepted_Epoch |-> acceptor_states[acceptor].max_accepted_Epoch,
             max_accepted_round |-> acceptor_states[acceptor].max_accepted_round,
             max_accepted_value |-> acceptor_states[acceptor].max_accepted_value
             ])
   /\ UNCHANGED learnt


IsEpochPromiseQuorum(Epoch, round, quorum) == 
    \A acceptor \in quorum:
        \E message \in sent_messages: /\ message.type = "Epoch promise"
                                      /\ message.acceptor = acceptor   
                                      /\ message.Epoch = Epoch      
                                      /\ message.round = round       
                                       
\* no higher ordered ballot accepted by any other acceptor in the promise quorum
\*                                                                                                                
               
\*PromiseQuorumMessages 
\* all promises in the quorum contain no value accepteds
\* or the highest ordered Epoch ballot accepted has the value of the one being proposed                                                                                                                
IsValueSafeInEpochPromiseQuorum(quorum, Epoch, round, value) == 
    \/ \E message_a \in sent_messages: /\ message_a.type = "Epoch promise"
                                       /\ message_a.Epoch = Epoch      
                                       /\ message_a.round = round 
                                       /\ message_a.max_accepted_value = value
                                       /\ message_a.acceptor \in quorum
                                       /\ \A message_b \in sent_messages \ {message_a}: 
                                            (/\ message_b.type = "Epoch promise" 
                                             /\ message_b.max_accepted_value /= message_a.max_accepted_value 
                                             /\ message_b.acceptor \in quorum
                                             /\ message_b.round = round 
                                             /\ message_b.Epoch = Epoch) 
                                         => (\/  /\ message_b.max_accepted_round < message_a.max_accepted_round
                                                 /\ message_b.max_accepted_Epoch = message_a.max_accepted_Epoch 
                                             \/ message_b.max_accepted_Epoch < message_a.max_accepted_Epoch)                \*((message_b.type = "Epoch promise" /\ message_b.Epoch = Epoch /\ message_b.round = round /\ message_b.acceptor \in quorum) => (message_a.max_accepted_Epoch > message_b.max_accepted_Epoch \/ message_a.max_accepted_round > message_b.max_accepted_round \/ message_a.max_accepted_value = message_b.max_accepted_value))
    \/ \A acceptor \in quorum:  
            \E message \in sent_messages: /\ message.type = "Epoch promise"
                                          /\ message.acceptor = acceptor
                                          /\ message.Epoch = Epoch      
                                          /\ message.round = round 
                                          /\ message.max_accepted_Epoch = -1
                                          /\ message.max_accepted_round = -1
                                          /\ message.max_accepted_value = None          
                                            
AcceptRequestAlreadyMadeWithDifferentValue(Epoch, round, value) ==
    \E other_value \in ProposableValues: MessageSent([type |-> "accept request",
                                                      Epoch |-> Epoch,
                                                      round |-> round,
                                                      value |-> other_value ])
                                               

                                                                
                                                                                                                                                                                                       
                                             
SendAcceptRequest(Epoch, round, value) == 
       /\ ~AcceptRequestAlreadyMadeWithDifferentValue(Epoch, round, value)
       /\ \E promise_quorum \in PromiseQuorums: /\ IsEpochPromiseQuorum(Epoch, round, promise_quorum) 
                                                /\ IsValueSafeInEpochPromiseQuorum(promise_quorum, Epoch, round, value)   \* exists a promise quroum where the value is the max accepted value
       /\ Send([type |-> "accept request",
                Epoch |-> Epoch,
                round |-> round,
                value |-> value])  
       /\ UNCHANGED <<acceptor_states, learnt>>                               
           
SendAcceptedMessage(acceptor, Epoch, round) == 
        /\ acceptor_states[acceptor].current_Epoch =< Epoch 
        /\ acceptor_states[acceptor].max_round_promised =< round
        /\ \E message \in sent_messages:    
             /\ message.type = "accept request"
             /\ message.Epoch = Epoch
             /\ message.round = round   
             /\ acceptor_states' = [acceptor_states EXCEPT ![acceptor].max_accepted_round = round,
                                                           ![acceptor].max_accepted_Epoch = Epoch,
                                                           ![acceptor].max_accepted_value = message.value,
                                                           ![acceptor].max_round_promised = round,
                                                           ![acceptor].current_Epoch = Epoch]
             /\ Send([type |-> "accepted", 
                      acceptor |-> acceptor, 
                      Epoch |-> Epoch, 
                      round |-> round, 
                      value |-> message.value])
         /\ UNCHANGED <<learnt>>                                           
                                                          
Restart(acceptor, Epoch) ==
	/\ Epoch >  acceptor_states[acceptor].current_Epoch 
	/\ acceptor_states' = [acceptor_states EXCEPT ![acceptor].current_Epoch = Epoch,
						      ![acceptor].max_round_promised = -1]
	/\ MessageSent([type |-> "new Epoch", Epoch |-> Epoch])
        /\ UNCHANGED <<learnt>>
                                                  
BeginNewEpoch(Epoch) ==
        /\ \A acceptor_b \in Acceptors: acceptor_states[acceptor_b].current_Epoch < Epoch
        /\ \E acceptor_b \in Acceptors: acceptor_states[acceptor_b].current_Epoch = (Epoch - 1) \* Epoch 1 higher than old current Epoch - not really needed 
        /\ Send([type |-> "new Epoch", Epoch |-> Epoch])   
        /\ UNCHANGED <<learnt, acceptor_states>>                                                                                                                            
                                                           
RecieveEpochNotification(acceptor, Epoch) ==
        /\ acceptor_states[acceptor].current_Epoch < Epoch
        /\ MessageSent([type |-> "new Epoch", Epoch |-> Epoch])
        /\ acceptor_states' = [acceptor_states EXCEPT ![acceptor].current_Epoch = Epoch,
                                                      ![acceptor].max_round_promised = -1]
        /\ UNCHANGED <<sent_messages, learnt>>  
        
Learn(Epoch, round, value) ==
        /\ \E accept_quorum \in AcceptQuorums: 
            \A acceptor \in accept_quorum: 
                MessageSent([type |-> "accepted", 
                            acceptor |-> acceptor, 
                            Epoch |-> Epoch, 
                            round |-> round,
                            value |-> value])
       /\ learnt' = learnt \union {value}
       /\ UNCHANGED <<acceptor_states, sent_messages>>
       
NextState == \/ \E round \in Rounds: 
                    \/ SendPromiseRequests(round)
                    \/ \E acceptor \in Acceptors: 
                        \/ SendEpochPromise(acceptor, round)
                        \/ \E Epoch \in Epochs: SendAcceptedMessage(acceptor, Epoch, round)
                    \/ \E Epoch \in Epochs, value \in ProposableValues:
                        \/ SendAcceptRequest(Epoch, round, value)
                        \/ Learn(Epoch, round, value)
             \/ \E acceptor \in Acceptors, Epoch \in Epochs: 
                    \/ Restart(acceptor, Epoch)
			\*\/ BeginNewEpoch(Epoch)
                    \/ RecieveEpochNotification(acceptor, Epoch)
            
EpochPaxosSpecification == InitalState \/ [][NextState]_<<acceptor_states, sent_messages, learnt>>

-----------------------------------------------------------------------------------------------------------------

                                              
ConsensusOK == /\ EpochPaxosTypeInvariant
	       /\ \/ learnt = {}
	          \/ \E value \in ProposableValues: learnt = {value}
    
 
=============================================================================
\* Modification History
\* Last modified Fri May 15 08:24:41 BST 2020 by Michael
\* Created Fri Aug 09 16:48:26 BST 2019 by Michael

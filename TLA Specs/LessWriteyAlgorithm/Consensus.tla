----------------------------- MODULE Consensus -----------------------------

CONSTANTS ProposableValues
VARIABLES learnt

(* Initally no values are learnt *)
ConsensusInitalState == learnt = {}


(* There was no value learnt, and in the new acceptor_state a value from the set of
   ProposableValues is the only element in learnt *)
ConsensusNextState == /\ learnt = {}
             /\ \exists value \in ProposableValues: {value} = learnt'
             
ConsensusSpecification == ConsensusInitalState \/ [][ConsensusNextState]_learnt

=============================================================================
\* Modification History
\* Last modified Mon Sep 23 12:50:04 BST 2019 by Michael
\* Created Mon Sep 23 12:49:55 BST 2019 by Michael

----------------------------- MODULE Consensus -----------------------------

CONSTANTS ProposableValues
VARIABLES learnt

(* Initally no values are learnt *)
ConsensusInitalState == learnt = {}


(* There was no value learnt, and in the new state a value from the set of
   ProposableValues is the only element in learnt *)
ConsensusNextState == /\ learnt = {}
             /\ \exists value \in ProposableValues: {value} = learnt'
             
ConsensusSpecification == ConsensusInitalState \/ [][ConsensusNextState]_learnt

=============================================================================
\* Modification History
\* Last modified Tue Aug 13 13:18:46 BST 2019 by Michael
\* Created Fri Aug 09 18:31:39 BST 2019 by Michael

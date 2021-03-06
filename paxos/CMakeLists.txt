INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/paxos/include)

SET(SRCS paxos.c standard_acceptor.c learner.c standard_proposer.c carray.c quorum.c ballot.c paxos_value.c
        standard_stable_storage.c storage_utils.c stable_storage_but_not_really.c paxos_storage.c hash_mapped_memory.c
		writeahead_ballot_acceptor.c paxos_message_conversion.c include/epoch_stable_storage.h epoch_stable_storage.c
        include/epoch_acceptor.h epoch_acceptor.c include/epoch_paxos_storage.h epoch_paxos_storage.c
		epoch_proposer.c include/epoch_proposer.h include/epoch_learner.h include/epoch_quorum.h include/instance.h
		include/paxos_util.h paxos_util.c include/proposer_common.h proposer_common.c include/pending_client_values.h
		pending_client_values.c include/random.h random.c include/performance_threshold_timer.h performance_threshold_timer.c include/timeout.h include/epoch_ballot.h epoch_ballot.c include/InstanceValuesManager.h epoch_learner.c include/count_logger.h count_logger.c include/value_proposal_manager.h value_proposal_manager.c include/ballot_giver.h ballot_giver.c)


IF (LMDB_FOUND)
	LIST(APPEND SRCS stable_storage_lmdb.c)
ENDIF ()

ADD_LIBRARY(paxos STATIC ${SRCS})
TARGET_LINK_LIBRARIES(paxos ${LIBPAXOS_LINKER_LIBS})

IF (LMDB_FOUND)
	INCLUDE_DIRECTORIES(${LMDB_INCLUDE_DIRS})
	TARGET_LINK_LIBRARIES(paxos ${LMDB_LIBRARIES})
ENDIF ()

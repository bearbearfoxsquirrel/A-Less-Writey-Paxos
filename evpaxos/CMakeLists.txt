include_directories(${CMAKE_SOURCE_DIR}/paxos/include)
include_directories(${CMAKE_SOURCE_DIR}/evpaxos/include)
include_directories(${LIBEVENT_INCLUDE_DIRS} ${MSGPACK_INCLUDE_DIRS})

set(LOCAL_SOURCES config.c standard_paxos_message.c paxos_types_pack.c standard_paxos_peers.c ev_write_ahead_ballot_acceptor.c
		ev_standard_acceptor.c evlearner.c evproposer.c evreplica.c ev_timer_threshold_timer_util.c include/backoff_manager.h backoff.c
		include/backoff.h backoff_manager.c include/backoff_implementations.h include/ev_epoch_paxos_internal.h include/evpaxos/epoch_paxos.h
		include/ev_epoch_paxos.h ev_epoch_proposer.c include/epoch_paxos_message.h epoch_paxos_message.c include/writeahead_epoch_paxos_peers.h
		writeahead_epoch_paxos_peers.c ev_epoch_acceptor.c ../paxos/include/random.h ../paxos/random.c ev_epoch_learner.c ../paxos/include/proposer_message_and_response_counters.h ../paxos/message_and_response_counters.c include/instance_strategy.h instance_strategy.c include/settler_iner.h settler_iner.c)

add_library(evpaxos SHARED ${LOCAL_SOURCES})

target_link_libraries(evpaxos paxos ${LIBPAXOS_LINKER_LIBS}
	${LIBEVENT_LIBRARIES} ${MSGPACK_LIBRARIES})

set_target_properties(evpaxos PROPERTIES
	INSTALL_NAME_DIR "${CMAKE_INSTALL_PREFIX}/lib")



INSTALL(TARGETS evpaxos DESTINATION lib)
INSTALL(FILES include/ev_epoch_paxos.h include/evpaxos.h DESTINATION include)
INSTALL(DIRECTORY  include/evpaxos DESTINATION include)

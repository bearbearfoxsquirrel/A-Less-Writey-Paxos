include_directories(${CMAKE_SOURCE_DIR}/sample/include)
include_directories(${CMAKE_SOURCE_DIR}/evpaxos/include)
include_directories(${CMAKE_SOURCE_DIR}/paxos/include)
include_directories(${LIBEVENT_INCLUDE_DIRS})

SET(SRCS include/client_value.h include/latency_recorder.h latency_recorder.c include/client_benchmarker.h client_benchmarker.c client_value.c)

#link_libraries(sample shared ${LIBPAXOS_LINKER_LIBS})

set(TEST_SOURCES fake_leader.c epoch_acceptor.c epoch_client.c epoch_proposer.c learner.c standard_acceptor.c proposer.c client.c replica.c counter.c write_ahead_window_acceptor.c)

ADD_LIBRARY(sample SHARED ${SRCS})
target_link_libraries(sample evpaxos ${LIBPAXOS_LINKER_LIBS})

#set(HEADER_FILES ${CMAKE_SOURCE_DIR}/sample/include/client_value.h)



foreach(p ${TEST_SOURCES})
	get_filename_component(target "${p}" NAME_WE)
	add_executable(${target} ${p})
	target_link_libraries(${target} evpaxos sample ${LIBPAXOS_LINKER_LIBS})
endforeach(p)

INSTALL(PROGRAMS
	${CMAKE_CURRENT_BINARY_DIR}/acceptor
	${CMAKE_CURRENT_BINARY_DIR}/proposer
	${CMAKE_CURRENT_BINARY_DIR}/replica
	${CMAKE_CURRENT_BINARY_DIR}/client
		${CMAKE_CURRENT_BINARY_DIR}/epoch_proposer
		${CMAKE_CURRENT_BINARY_DIR}/epoch_client
		${CMAKE_CURRENT_BINARY_DIR}}/fake_leader
	DESTINATION bin)

include_directories(${CMAKE_SOURCE_DIR}/sample/include)
include_directories(${CMAKE_SOURCE_DIR}/evpaxos/include)
include_directories(${CMAKE_SOURCE_DIR}/paxos/include)
include_directories(${LIBEVENT_INCLUDE_DIRS})

SET(SRCS latency_recorder.c)

set(TEST_SOURCES learner.c  standard_acceptor.c proposer.c client.c replica.c counter.c test.c write_ahead_window_acceptor.c)

#set(HEADER_FILES ${CMAKE_SOURCE_DIR}/sample/include/client_value.h)

ADD_LIBRARY(sample SHARED ${SRCS})


foreach(p ${TEST_SOURCES})
	get_filename_component(target "${p}" NAME_WE)
	add_executable(${target} ${p})
	target_link_libraries(${target} evpaxos sample ${LIBPAXOS_LINKER_LIBS})
endforeach(p)

INSTALL(PROGRAMS
	${CMAKE_CURRENT_BINARY_DIR}/acceptor
	${CMAKE_CURRENT_BINARY_DIR}/proposer
	${CMAKE_CURRENT_BINARY_DIR}/replica
		${CMAKE_CURRENT_BINARY_DIR}/test
	${CMAKE_CURRENT_BINARY_DIR}/client
	DESTINATION bin)

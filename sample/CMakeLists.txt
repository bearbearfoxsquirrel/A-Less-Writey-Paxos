include_directories(${CMAKE_SOURCE_DIR}/sample/include)
include_directories(${CMAKE_SOURCE_DIR}/evpaxos/include)
include_directories(${CMAKE_SOURCE_DIR}/paxos/include)
include_directories(${LIBEVENT_INCLUDE_DIRS})
include_directories(${LIBEVENT_PTHREADS_INCLUDE_DIRS})

SET(SRCS latency_recorder.c )

set(TEST_SOURCES epoch_acceptor.c epoch_client.c epoch_proposer.c learner.c  standard_acceptor.c proposer.c client.c replica.c counter.c write_ahead_window_acceptor.c)

#set(HEADER_FILES ${CMAKE_SOURCE_DIR}/sample/include/client_value.h)

ADD_LIBRARY(sample SHARED ${SRCS})

find_library(LIBEVENT_PTHREADS_LIB libevent_pthreads)
#add_library( LIBEVENT_PTHREADS SHARED ${LIBEVENT_THREADS_LIB})


set(THREADS_PREFER_PTHREAD_FLAG ON)

find_package(Threads REQUIRED)
foreach(p ${TEST_SOURCES})
	get_filename_component(target "${p}" NAME_WE)
	add_executable(${target} ${p})

	target_link_libraries(${target} evpaxos sample  ${LIBEVENT_THREADS} ${LIBPAXOS_LINKER_LIBS} "/usr/local/lib/libevent_pthreads.a")
endforeach(p)

INSTALL(PROGRAMS
	${CMAKE_CURRENT_BINARY_DIR}/acceptor
	${CMAKE_CURRENT_BINARY_DIR}/proposer
	${CMAKE_CURRENT_BINARY_DIR}/replica
	${CMAKE_CURRENT_BINARY_DIR}/client
		${CMAKE_CURRENT_BINARY_DIR}/epoch_proposer
		${CMAKE_CURRENT_BINARY_DIR}/epoch_client
	DESTINATION bin)

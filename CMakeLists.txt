cmake_minimum_required(VERSION 2.6)
project(a-less-writey-paxos)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu11 -O0 -fPIC -Wall -g -march=native -Wextra -pedantic -Wpedantic -Wshadow -Wstrict-overflow -fno-strict-aliasing -Wno-missing-field-initializers")
set(CMAKE_MACOSX_RPATH 1)

find_package(LIBEVENT REQUIRED)
find_package(MSGPACK REQUIRED)
find_package(LMDB REQUIRED)

if (LMDB_FOUND)
	add_definitions(-DHAS_LMDB)
endif ()

if (NOT APPLE)
	set(LIBPAXOS_LINKER_LIBS ${LIBPAXOS_LINKER_LIBS} m rt)
endif ()

add_subdirectory(paxos)
add_subdirectory(evpaxos)
add_subdirectory(sample)




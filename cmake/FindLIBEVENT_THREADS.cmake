FIND_LIBRARY(LIBEVENT_PTHREADS_LIB
        names libevent_pthreads libevent_threads
        hints ${LIBEVENT_ROOT})

find_library(libevent_pthreads libevent_pthreads)
add_library(libevent_pthreads SHARED IMPORTED GLOBAL) # GLOBAL -> if outside src tree
set_property(TARGET libevent_pthreads PROPERTY IMPORTED_LOCATION ${lib})
set_property(TARGET libevent_pthreads APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/include)


#set(LIBEVENT_PTHREADS_ROOT "" CACHE STRING "LIBEVENT_PTHREADS root directory")
#find_path(LIBEVENT_PTHREAD_INCLUDE_DIR libevent_threads libevent_pthreads HINTS "${LIBEVENT_PTHREADS_ROOT}/include")
#find_library(LIBEVENT_PTHREAD_LIBRARY NAMES thread libevent_threads libevent_pthreads pthreads threads HINTS "${LIBEVENT_PTHREAD}/lib")

#set(LIBEVENT_PTHREAD_LIBRARIES ${LIBEVENT_PTHREAD_LIBRARY})
#set(LIBEVENT_PTHREAD_INCLUDE_DIRS ${LIBEVENT_PTHREAD_INCLUDE_DIR})






#include(FindPackageHandleStandardArgs)

# handle the QUIETLY and REQUIRED arguments and set LIBEVENT_FOUND to TRUE
# if all listed variables are TRUE
#find_package_handle_standard_args(LIBEVENT_PTHREADS DEFAULT_MSG
#        LIBEVENT_LIBRARY LIBEVENT_INCLUDE_DIR)



#mark_as_advanced(LIBEVENT_PTHREAD_INCLUDE_DIR LIBEVENT_PTHREAD_LIBRARY)


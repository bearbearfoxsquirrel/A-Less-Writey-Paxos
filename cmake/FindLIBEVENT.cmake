# - Find LIBEVENT
# Find the native LibEvent includes and library
#
# LIBEVENT_INCLUDES - where to find event.h
# LIBEVENT_LIBRARIES - List of libraries when using LIBEVENT.
# LIBEVENT_FOUND - True if LibEvent found.


set(LIBEVENT_ROOT "" CACHE STRING "LIBEVENT root directory")

find_path(LIBEVENT_INCLUDE_DIR event.h thread.h
    HINTS "${LIBEVENT_ROOT}/include")
#evthread
find_library(LIBEVENT_LIBRARY
   NAMES event evthread thread
   HINTS "${LIBEVENT_ROOT}/lib")

set(LIBEVENT_LIBRARIES ${LIBEVENT_LIBRARY})
set(LIBEVENT_INCLUDE_DIRS ${LIBEVENT_INCLUDE_DIR})



FIND_LIBRARY(LIBEVENT_PTHREADS
        names libevent_pthreads
        hints ${LIBEVENT_ROOT})




include(FindPackageHandleStandardArgs)

# handle the QUIETLY and REQUIRED arguments and set LIBEVENT_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(LIBEVENT DEFAULT_MSG
                                  LIBEVENT_LIBRARY LIBEVENT_INCLUDE_DIR)



mark_as_advanced(LIBEVENT_PTHREAD_INCLUDE_DIR LIBEVENT_PTHREAD_LIBRARY)

mark_as_advanced(LIBEVENT_INCLUDE_DIR LIBEVENT_LIBRARY)

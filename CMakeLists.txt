cmake_minimum_required(VERSION 3.13)
project(INDRA_Stream_Test C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 14)

# Define location of cmake modules
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules")

SET(GCC_COMPILE_FLAGS    "-Wall -g -O2 -DPARALLEL=32 -DUSEZMQ=1 -DNDEBUG=1 -fPIC")

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

find_package(ZeroMQ REQUIRED)
find_package(CZMQ REQUIRED)

include_directories(. ${ZEROMQ_INCLUDE_DIRS} ${CMZQ_INCLUDE_DIRS})

add_definitions(${GCC_COMPILE_FLAGS})

add_executable(stream_router
        stream_router.c
        stream_tools.c
        stream_tools.h)

target_link_libraries(stream_router ${ZEROMQ_LIBRARIES} ${CZMQ_LIBRARIES} m Threads::Threads)

add_executable(stream_test_source
        stream_test_source.c
        stream_tools.c
        stream_tools.h)

target_link_libraries(stream_test_source ${ZEROMQ_LIBRARIES} ${CZMQ_LIBRARIES} m Threads::Threads)

add_executable(stream_test_subscriber
        stream_test_subscriber.c
        stream_tools.c
        stream_tools.h)

target_link_libraries(stream_test_subscriber ${ZEROMQ_LIBRARIES} ${CZMQ_LIBRARIES} m Threads::Threads)
cmake_minimum_required(VERSION 3.13)
project(INDRA_Stream_Test C)

set(CMAKE_C_STANDARD 11)

include_directories(.)

SET(GCC_COMPILE_FLAGS    "-Wall -g -O2 -DPARALLEL=32 -DUSEZMQ=1 -DNDEBUG=1 -fPIC")

add_definitions(${GCC_COMPILE_FLAGS})

add_executable(stream_router
        stream_router.c
        stream_tools.c
        stream_tools.h)

target_link_libraries(stream_router  libczmq.a libzmq.a )

add_executable(stream_test_source
        stream_test_source.c
        stream_tools.c
        stream_tools.h)

target_link_libraries(stream_test_source  libczmq.a libzmq.a )

add_executable(stream_test_subscriber
        test_subscriber.c
        stream_tools.c
        stream_tools.h)

target_link_libraries(stream_test_subscriber  libczmq.a libzmq.a )
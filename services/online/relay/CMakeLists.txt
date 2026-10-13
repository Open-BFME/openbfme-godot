# OpenBFME online relay (lane ONLINE-2): builds alone on a relay host (cmake -S services/online/relay -B build && cmake --build build), or as part of the
# engine build (engine/cmake/Online2.cmake adds this directory). It compiles the two engine files it shares with the client (the envelope and ticket code,
# SHA-256) and nothing else of the engine.
cmake_minimum_required(VERSION 3.20)
project(openbfme_relay CXX)
if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()
get_filename_component(OPENBFME_RELAY_ENGINE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../../engine/src" ABSOLUTE)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "openbfme_relay: Linux only (recvmmsg / sendmmsg); not built on ${CMAKE_SYSTEM_NAME}")
    return()
endif()
add_executable(openbfme_relay
    relay_main.cpp
    RelayCore.cpp
    ${OPENBFME_RELAY_ENGINE_SRC}/GameNetwork/RelayProtocol.cpp
    ${OPENBFME_RELAY_ENGINE_SRC}/Common/Sha256.cpp)
target_include_directories(openbfme_relay PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${OPENBFME_RELAY_ENGINE_SRC})
target_compile_options(openbfme_relay PRIVATE -Wall -Wextra)

# OpenBFME online relay (lane ONLINE-2): builds alone on a relay host (cmake -S services/online/relay -B build && cmake --build build), or as part of the
# engine build (engine/cmake/Online2.cmake adds this directory). It compiles the two engine files it shares with the client (the envelope and ticket code,
# SHA-256) and nothing else of the engine.
cmake_minimum_required(VERSION 3.20)
project(openbfme_relay CXX C)
include("${CMAKE_CURRENT_SOURCE_DIR}/../../../engine/cmake/Sanitizers.cmake")
find_package(Threads REQUIRED)
if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()
get_filename_component(OPENBFME_RELAY_ENGINE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../../engine/src" ABSOLUTE)
get_filename_component(OPENBFME_RELAY_ENGINE_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/../../../engine/tests" ABSOLUTE)
get_filename_component(OPENBFME_RELAY_DOCTEST "${CMAKE_CURRENT_SOURCE_DIR}/../../../engine/thirdparty/doctest" ABSOLUTE)
get_filename_component(OPENBFME_RELAY_THIRDPARTY "${CMAKE_CURRENT_SOURCE_DIR}/../../../engine/thirdparty" ABSOLUTE)
# vendored Monocypher (the packet authentication's keyed BLAKE2b), shared by relay, client and sim
set(OPENBFME_RELAY_CRYPTO_SOURCES
    ${OPENBFME_RELAY_THIRDPARTY}/monocypher/monocypher.c)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    message(STATUS "openbfme_relay: requires Linux or macOS sockets; not built on ${CMAKE_SYSTEM_NAME}")
    return()
endif()
# the relay's logic (everything of openbfme_relay but relay_main.cpp): the relay, its resource tests and the envelope fuzzer compile exactly this
set(OPENBFME_RELAY_CORE_SOURCES
    ${OPENBFME_RELAY_ENGINE_SRC}/GameNetwork/RelayAuth.cpp
    RelayCore.cpp
    RelayCanonicalAuthority.cpp
    ${OPENBFME_RELAY_ENGINE_SRC}/GameNetwork/RelayBundleChannel.cpp
    ${OPENBFME_RELAY_ENGINE_SRC}/GameNetwork/RelayProtocol.cpp
    ${OPENBFME_RELAY_ENGINE_SRC}/GameNetwork/RelayCommit.cpp
    ${OPENBFME_RELAY_ENGINE_SRC}/GameNetwork/RelayMembership.cpp
    ${OPENBFME_RELAY_ENGINE_SRC}/GameNetwork/RelayFrameBundle.cpp
    ${OPENBFME_RELAY_ENGINE_SRC}/GameNetwork/RelayFrameScheduler.cpp
    ${OPENBFME_RELAY_ENGINE_SRC}/GameNetwork/RelayCanonicalInput.cpp
    ${OPENBFME_RELAY_ENGINE_SRC}/Common/Sha256.cpp
    ${OPENBFME_RELAY_ENGINE_SRC}/Common/System/Xfer.cpp
    ${OPENBFME_RELAY_CRYPTO_SOURCES})
add_executable(openbfme_relay relay_main.cpp ${OPENBFME_RELAY_CORE_SOURCES})
target_include_directories(openbfme_relay PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${OPENBFME_RELAY_ENGINE_SRC} ${OPENBFME_RELAY_THIRDPARTY})
target_compile_options(openbfme_relay PRIVATE -Wall -Wextra)

add_executable(openbfme_relay_resource_tests
    ${OPENBFME_RELAY_ENGINE_SRC}/../tests/test_main.cpp
    ${OPENBFME_RELAY_ENGINE_SRC}/../tests/test_relay_resources.cpp
    ${OPENBFME_RELAY_ENGINE_SRC}/Common/System/PathIO.cpp # test_main.cpp starts through PathIO::runMain (UTF-8 arguments)
    ${OPENBFME_RELAY_CORE_SOURCES})
target_include_directories(openbfme_relay_resource_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${OPENBFME_RELAY_ENGINE_SRC}
    ${OPENBFME_RELAY_ENGINE_SRC}/../thirdparty/doctest
    ${OPENBFME_RELAY_THIRDPARTY})
target_compile_options(openbfme_relay_resource_tests PRIVATE -Wall -Wextra)

# lane NETDIAG-1: the envelope fuzzer (engine/tests/fuzz_network.cpp, OPENBFME_FUZZ_KIND 3: engine/cmake/Online2.cmake's list) over the relay's
# own sources (+ MiniJson.cpp: it checks the health snapshot parses); OPENBFME_FUZZ=ON with Clang, never in a default build
if(OPENBFME_FUZZ)
    add_executable(fuzz_envelope ${OPENBFME_RELAY_ENGINE_TESTS}/fuzz_network.cpp ${OPENBFME_RELAY_CORE_SOURCES} ${OPENBFME_RELAY_ENGINE_SRC}/Common/MiniJson.cpp)
    target_compile_definitions(fuzz_envelope PRIVATE OPENBFME_FUZZ_KIND=3)
    target_include_directories(fuzz_envelope PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${OPENBFME_RELAY_ENGINE_SRC} ${OPENBFME_RELAY_THIRDPARTY})
    target_compile_options(fuzz_envelope PRIVATE -fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
    target_link_options(fuzz_envelope PRIVATE -fsanitize=fuzzer,address,undefined)
endif()

# NET-SYNC: the STEP-3 measurement sim (worldwide link matrix over the real authority + channels);
# ROUTER-1: no RelayCommitJournal.cpp — the ported commit journal is dropped, the game log is the durable append
add_executable(openbfme_net_sync_sim
    ${OPENBFME_RELAY_ENGINE_SRC}/GameNetwork/RelayAuth.cpp
    sim/net_sync_sim.cpp
    RelayCore.cpp
    RelayCanonicalAuthority.cpp
    ${OPENBFME_RELAY_ENGINE_SRC}/GameNetwork/RelayBundleChannel.cpp
    ${OPENBFME_RELAY_ENGINE_SRC}/GameNetwork/RelayProtocol.cpp
    ${OPENBFME_RELAY_ENGINE_SRC}/GameNetwork/RelayCommit.cpp
    ${OPENBFME_RELAY_ENGINE_SRC}/GameNetwork/RelayMembership.cpp
    ${OPENBFME_RELAY_ENGINE_SRC}/GameNetwork/RelayFrameBundle.cpp
    ${OPENBFME_RELAY_ENGINE_SRC}/GameNetwork/RelayFrameScheduler.cpp
    ${OPENBFME_RELAY_ENGINE_SRC}/GameNetwork/RelayCanonicalInput.cpp
    ${OPENBFME_RELAY_ENGINE_SRC}/GameNetwork/RelayCanonicalClient.cpp
    ${OPENBFME_RELAY_ENGINE_SRC}/GameNetwork/NetPacket.cpp
    ${OPENBFME_RELAY_ENGINE_SRC}/GameLogic/GameMessage.cpp
    ${OPENBFME_RELAY_ENGINE_SRC}/Common/Sha256.cpp
    ${OPENBFME_RELAY_ENGINE_SRC}/Common/System/Xfer.cpp
    ${OPENBFME_RELAY_ENGINE_SRC}/Common/MpGameRules.cpp
    ${OPENBFME_RELAY_ENGINE_SRC}/Common/CreateAHeroRecord.cpp
    ${OPENBFME_RELAY_CRYPTO_SOURCES})
target_include_directories(openbfme_net_sync_sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${OPENBFME_RELAY_ENGINE_SRC} ${OPENBFME_RELAY_THIRDPARTY})
target_compile_options(openbfme_net_sync_sim PRIVATE -Wall -Wextra)
target_link_libraries(openbfme_net_sync_sim PRIVATE Threads::Threads)

add_executable(openbfme_relay_membership_tests
    ${OPENBFME_RELAY_ENGINE_SRC}/../tests/test_main.cpp
    ${OPENBFME_RELAY_ENGINE_SRC}/../tests/test_relay_membership.cpp
    ${OPENBFME_RELAY_ENGINE_SRC}/Common/System/PathIO.cpp # test_main.cpp starts through PathIO::runMain (UTF-8 arguments)
    ${OPENBFME_RELAY_ENGINE_SRC}/GameNetwork/RelayCommit.cpp
    ${OPENBFME_RELAY_ENGINE_SRC}/GameNetwork/RelayMembership.cpp
    ${OPENBFME_RELAY_ENGINE_SRC}/GameNetwork/RelayFrameBundle.cpp
    ${OPENBFME_RELAY_ENGINE_SRC}/Common/Sha256.cpp
    ${OPENBFME_RELAY_ENGINE_SRC}/Common/System/Xfer.cpp)
target_include_directories(openbfme_relay_membership_tests PRIVATE
    ${OPENBFME_RELAY_ENGINE_SRC}
    ${OPENBFME_RELAY_ENGINE_SRC}/../thirdparty/doctest)
target_compile_options(openbfme_relay_membership_tests PRIVATE -Wall -Wextra)

include(CTest)
if(BUILD_TESTING)
    add_executable(openbfme_relay_tests
        relay_bundle_test_main.cpp
        ${OPENBFME_RELAY_ENGINE_SRC}/GameNetwork/RelayBundleChannel.cpp
        ${OPENBFME_RELAY_ENGINE_SRC}/GameNetwork/RelayCanonicalInput.cpp
        ${OPENBFME_RELAY_ENGINE_SRC}/GameNetwork/RelayCommit.cpp
        ${OPENBFME_RELAY_ENGINE_SRC}/GameNetwork/RelayMembership.cpp
        ${OPENBFME_RELAY_ENGINE_SRC}/GameNetwork/RelayFrameBundle.cpp
        ${OPENBFME_RELAY_ENGINE_SRC}/GameNetwork/RelayFrameScheduler.cpp
        ${OPENBFME_RELAY_ENGINE_SRC}/Common/Sha256.cpp
    ${OPENBFME_RELAY_ENGINE_SRC}/Common/System/Xfer.cpp
        ${OPENBFME_RELAY_ENGINE_TESTS}/test_relay_bundle.cpp
        ${OPENBFME_RELAY_ENGINE_TESTS}/test_relay_canonical_channel.cpp
        ${OPENBFME_RELAY_ENGINE_TESTS}/test_relay_canonical_input.cpp
        ${OPENBFME_RELAY_ENGINE_TESTS}/test_relay_frame_clock.cpp
        ${OPENBFME_RELAY_ENGINE_TESTS}/test_relay_frame_bundle.cpp
        ${OPENBFME_RELAY_ENGINE_TESTS}/test_relay_frame_scheduler.cpp
        ${OPENBFME_RELAY_ENGINE_TESTS}/test_relay_budget_metrics.cpp)
    target_include_directories(openbfme_relay_tests PRIVATE
        ${OPENBFME_RELAY_ENGINE_SRC}
        ${OPENBFME_RELAY_DOCTEST})
    set_target_properties(openbfme_relay_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    target_compile_options(openbfme_relay_tests PRIVATE -Wall -Wextra)
    target_link_libraries(openbfme_relay_tests PRIVATE Threads::Threads)
    if(TARGET openbfme_sim_fp)
        target_link_libraries(openbfme_relay_tests PRIVATE openbfme_sim_fp)
    endif()
    add_test(NAME openbfme_relay_bundle_tests COMMAND openbfme_relay_tests)
endif()

# Lane ONLINE-2 (online multiplayer phase O1: the forwarding relay, the START from the lobby service, the relay's command log -> replay; docs/ONLINE.md):
# sources and tests. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# client / host sources (excluded from the simulation audit in tools/sim/sim_policy.json): the relay envelope and tickets, SHA-256, the online START, the
# relay log reader
set(OPENBFME_ONLINE2_HOST_SOURCES
    src/Common/Sha256.cpp
    src/GameNetwork/RelayProtocol.cpp
    src/GameNetwork/RelayAuth.cpp
    src/GameNetwork/RelayCommit.cpp
    src/GameNetwork/RelayMembership.cpp
    src/GameNetwork/RelayBundleChannel.cpp
    src/GameNetwork/RelayCanonicalClient.cpp
    src/GameNetwork/RelayCanonicalInput.cpp
    src/GameNetwork/RelayFrameBundle.cpp
    src/GameNetwork/RelayFrameScheduler.cpp
    src/GameNetwork/OnlineStart.cpp
    src/GameNetwork/JoinBuild.cpp
    src/GameNetwork/RelayLog.cpp)
set(OPENBFME_ONLINE2_TESTS
    tests/test_online_relay.cpp
    tests/test_relay_resources.cpp
    tests/test_relay_bundle.cpp
    tests/test_relay_canonical_channel.cpp
    tests/test_relay_budget_metrics.cpp
    tests/test_relay_canonical_client.cpp
    tests/test_relay_canonical_input.cpp
    tests/test_relay_canonical_ticket.cpp
    tests/test_relay_auth.cpp
    tests/test_online16_fix.cpp
    tests/test_relay_frame_clock.cpp
    tests/test_relay_frame_bundle.cpp
    tests/test_relay_frame_scheduler.cpp
    tests/test_network_canonical.cpp
    tests/test_relay_membership.cpp
    tests/test_joinver1.cpp)
get_filename_component(OPENBFME_RELAY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../services/online/relay" ABSOLUTE)
# vendored Monocypher (the packet authentication's keyed BLAKE2b); part of core
set(OPENBFME_ONLINE2_HOST_SOURCES ${OPENBFME_ONLINE2_HOST_SOURCES}
    thirdparty/monocypher/monocypher.c)

# called once openbfme_core, openbfme_tests and openbfme_peer exist
macro(openbfme_online2_targets)
    target_sources(openbfme_core PRIVATE ${OPENBFME_ONLINE2_HOST_SOURCES})
    # the relay's core runs in the unit tests on every platform; the relay process runs on Linux and macOS
    if(TARGET openbfme_tests)
        target_sources(openbfme_tests PRIVATE ${OPENBFME_ONLINE2_TESTS} ${OPENBFME_RELAY_DIR}/RelayCore.cpp
        ${OPENBFME_RELAY_DIR}/RelayCanonicalAuthority.cpp)
        target_include_directories(openbfme_tests PRIVATE ${OPENBFME_RELAY_DIR})
    endif()
    if(OPENBFME_FUZZ)
        # lane NETDIAG-1: one libFuzzer executable per decoder (OPENBFME_FUZZ_KIND = the index in this list); `envelope` (index 3) drives the relay
        # and is built by services/online/relay/CMakeLists.txt from the relay's own source list, not from openbfme_core. The replay decoder's
        # target is fuzz_net_replay: the data-file parser targets (cmake/Fuzz.cmake) already have a fuzz_replay
        set(OPENBFME_NETWORK_FUZZERS
            command message ticket envelope status relay_log replay transport lan_start online_start desync platform_api lan_control lobby_response)
        set(_fuzz_index 0)
        foreach(_fuzz_name IN LISTS OPENBFME_NETWORK_FUZZERS)
            if(NOT _fuzz_name STREQUAL "envelope")
                set(_fuzz_target fuzz_${_fuzz_name})
                if(_fuzz_name STREQUAL "replay")
                    set(_fuzz_target fuzz_net_replay)
                endif()
                add_executable(${_fuzz_target} tests/fuzz_network.cpp)
                target_compile_definitions(${_fuzz_target} PRIVATE OPENBFME_FUZZ_KIND=${_fuzz_index})
                target_link_libraries(${_fuzz_target} PRIVATE openbfme_core)
                target_link_options(${_fuzz_target} PRIVATE -fsanitize=fuzzer,address,undefined)
            endif()
            math(EXPR _fuzz_index "${_fuzz_index} + 1")
        endforeach()
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        add_subdirectory(${OPENBFME_RELAY_DIR} ${CMAKE_BINARY_DIR}/relay)
        # the simulation audit's flag rule covers every translation unit of the build (tools/sim/sim_audit.py): the relay has no floating point,
        # it takes the same flags anyway
        target_link_libraries(openbfme_relay PRIVATE openbfme_sim_fp)
        target_link_libraries(openbfme_relay_resource_tests PRIVATE openbfme_sim_fp)
        target_link_libraries(openbfme_relay_membership_tests PRIVATE openbfme_sim_fp)
        if(TARGET openbfme_net_sync_sim)
            target_link_libraries(openbfme_net_sync_sim PRIVATE openbfme_sim_fp)
        endif()
        if(TARGET openbfme_tests)
            add_dependencies(openbfme_tests openbfme_relay)
            target_compile_definitions(openbfme_tests PRIVATE OPENBFME_RELAY_EXE="$<TARGET_FILE:openbfme_relay>")
        endif()
    endif()
endmacro()

# Lane ONLINE-2 (online multiplayer phase O1: the forwarding relay, the START from the lobby service, the relay's command log -> replay; docs/ONLINE.md):
# sources and tests. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# client / host sources (excluded from the simulation audit in tools/sim/sim_policy.json): the relay envelope and tickets, SHA-256, the online START, the
# relay log reader
set(OPENBFME_ONLINE2_HOST_SOURCES
    src/Common/Sha256.cpp
    src/GameNetwork/RelayProtocol.cpp
    src/GameNetwork/OnlineStart.cpp
    src/GameNetwork/RelayLog.cpp)
set(OPENBFME_ONLINE2_TESTS
    tests/test_online_relay.cpp)
get_filename_component(OPENBFME_RELAY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../services/online/relay" ABSOLUTE)

# called once openbfme_core, openbfme_tests and openbfme_peer exist
macro(openbfme_online2_targets)
    target_sources(openbfme_core PRIVATE ${OPENBFME_ONLINE2_HOST_SOURCES})
    # the relay's core runs in the unit tests on every platform (driven datagram by datagram); the relay process is Linux only
    target_sources(openbfme_tests PRIVATE ${OPENBFME_ONLINE2_TESTS} ${OPENBFME_RELAY_DIR}/RelayCore.cpp)
    target_include_directories(openbfme_tests PRIVATE ${OPENBFME_RELAY_DIR})
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_subdirectory(${OPENBFME_RELAY_DIR} ${CMAKE_BINARY_DIR}/relay)
        # the simulation audit's flag rule covers every translation unit of the build (tools/sim/sim_audit.py): the relay has no floating point,
        # it takes the same flags anyway
        target_link_libraries(openbfme_relay PRIVATE openbfme_sim_fp)
        add_dependencies(openbfme_tests openbfme_relay)
        target_compile_definitions(openbfme_tests PRIVATE OPENBFME_RELAY_EXE="$<TARGET_FILE:openbfme_relay>")
    endif()
endmacro()

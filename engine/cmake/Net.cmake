# Lane MP-1 (lockstep multiplayer: the network command transport, the LAN lobby, the CRC exchange and desync reports, replays, the headless peer): sources and
# tests. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources: what decides which commands a logic frame runs with (and the replay of it); appended to OPENBFME_SIM_SOURCES
set(OPENBFME_NET_SOURCES
    src/GameNetwork/NetPacket.cpp
    src/GameNetwork/Network.cpp
    src/GameNetwork/LockstepDriver.cpp
    src/GameNetwork/NetGameSession.cpp
    src/Common/Recorder.cpp)
# the socket, the reliable transport and the lobby: they decide when bytes arrive, never what a frame contains (excluded in tools/sim/sim_policy.json)
set(OPENBFME_NET_HOST_SOURCES
    src/GameNetwork/Transport.cpp
    src/GameNetwork/LANLobby.cpp
    src/GameNetwork/ScriptedPlayer.cpp
    src/GameNetwork/ProfileIdentity.cpp)
set(OPENBFME_NET_TESTS
    tests/test_net_core.cpp
    tests/test_net_retail.cpp)

# called once openbfme_core and openbfme_tests exist: the host sources, the headless peer and the tests' path to it
# The engine line of the profile identity (GameNetwork/ProfileIdentity.h): cmake/EngineId.cmake hashes the simulation sources and configuration (or honours
# -DOPENBFME_ENGINE_ID=<64 hex> from a release pipeline); the Git commit is provenance only. Editing a hashed file re-runs the configure step.
include(cmake/EngineId.cmake)
set(OPENBFME_ENGINE_ID "" CACHE STRING "Release override of the engine compatibility id (64 hex digits, from the pristine tree); empty = computed from the sources")
set(OPENBFME_ENGINE_ID_OPTIONS "" CACHE STRING "Extra simulation-affecting build options named in the engine id")
if(OPENBFME_ENGINE_ID)
    string(TOLOWER "${OPENBFME_ENGINE_ID}" OPENBFME_ENGINE_ID_VALUE)
    string(LENGTH "${OPENBFME_ENGINE_ID_VALUE}" _id_len)
    if(NOT _id_len EQUAL 64 OR OPENBFME_ENGINE_ID_VALUE MATCHES "[^0-9a-f]")
        message(FATAL_ERROR "OPENBFME_ENGINE_ID must be 64 hex digits (the SHA-256 the release pipeline computed), got '${OPENBFME_ENGINE_ID}'")
    endif()
    set(OPENBFME_ENGINE_ID_SOURCE "release override")
else()
    openbfme_compute_engine_id("${CMAKE_CURRENT_SOURCE_DIR}" "${OPENBFME_SIM_X86_32}" "${OPENBFME_ENGINE_ID_OPTIONS}" OPENBFME_ENGINE_ID_VALUE OPENBFME_ENGINE_ID_FILES)
    set(OPENBFME_ENGINE_ID_SOURCE "computed from ${CMAKE_CURRENT_SOURCE_DIR}")
    foreach(_f IN LISTS OPENBFME_ENGINE_ID_FILES)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${_f}")
    endforeach()
endif()
# provenance (never part of the id): the commit the build was configured from, "none" without Git
set(OPENBFME_GIT_PROVENANCE "none")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE _sha OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE _rc)
    if(_rc EQUAL 0 AND _sha)
        set(OPENBFME_GIT_PROVENANCE "${_sha}")
    endif()
endif()
message(STATUS "OpenBFME engine id: ${OPENBFME_ENGINE_ID_VALUE} (${OPENBFME_ENGINE_ID_SOURCE}); provenance ${OPENBFME_GIT_PROVENANCE}")
set_source_files_properties(src/GameNetwork/ProfileIdentity.cpp PROPERTIES
    COMPILE_DEFINITIONS "OPENBFME_ENGINE_ID=\"${OPENBFME_ENGINE_ID_VALUE}\";OPENBFME_GIT_PROVENANCE=\"${OPENBFME_GIT_PROVENANCE}\"")

macro(openbfme_net_targets)
    target_sources(openbfme_core PRIVATE ${OPENBFME_NET_HOST_SOURCES})
    if(WIN32)
        target_link_libraries(openbfme_core PUBLIC ws2_32)
    endif()
    add_executable(openbfme_peer src/Main/openbfme_peer.cpp)
    target_link_libraries(openbfme_peer PRIVATE openbfme_core)
    if(MSVC)
        target_compile_options(openbfme_peer PRIVATE /utf-8)
    endif()
    add_dependencies(openbfme_tests openbfme_peer)
    target_compile_definitions(openbfme_tests PRIVATE OPENBFME_PEER_EXE="$<TARGET_FILE:openbfme_peer>")
endmacro()

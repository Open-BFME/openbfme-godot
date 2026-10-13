# Lane ONLINE-4 (online phase O2: RotWK's online screens on the OpenBFME lobby service; docs/ONLINE.md section 5): sources and tests. Included from
# engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# client sources (excluded from the simulation audit in tools/sim/sim_policy.json): the lobby service's client and OnlineShell.apt's engine side
set(OPENBFME_ONLINE4_CLIENT_SOURCES
    src/GameNetwork/OnlineLobbyClient.cpp
    src/GameClient/GUI/AptScreens/AptOnlineShell.cpp)
set(OPENBFME_ONLINE4_TESTS
    tests/test_online4_screens.cpp)

# called once openbfme_core and openbfme_tests exist
macro(openbfme_online4_targets)
    target_sources(openbfme_core PRIVATE ${OPENBFME_ONLINE4_CLIENT_SOURCES})
    target_sources(openbfme_tests PRIVATE ${OPENBFME_ONLINE4_TESTS})
    get_filename_component(OPENBFME_LOBBY_SERVER "${CMAKE_CURRENT_SOURCE_DIR}/../services/online/lobby/src/server.mjs" ABSOLUTE)
    target_compile_definitions(openbfme_tests PRIVATE OPENBFME_LOBBY_SERVER="${OPENBFME_LOBBY_SERVER}")
endmacro()

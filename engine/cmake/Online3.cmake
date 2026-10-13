# Lane ONLINE-3 (online multiplayer phase O3, the reconnect half: hold, rejoin, the relay's STATUS and log reads, the multiplayer pause; docs/ONLINE.md):
# sources and tests. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation source: the catch-up decides which commands the re-simulated frames run with (the relay log's), as the replay playback does (audited)
set(OPENBFME_ONLINE3_SOURCES
    src/GameNetwork/Rejoin.cpp)
list(APPEND OPENBFME_SIM_SOURCES ${OPENBFME_ONLINE3_SOURCES})
set(OPENBFME_ONLINE3_TESTS
    tests/test_online3_rejoin.cpp
    tests/test_online3_spectate.cpp)

# called once openbfme_tests exists
macro(openbfme_online3_targets)
    target_sources(openbfme_tests PRIVATE ${OPENBFME_ONLINE3_TESTS})
endmacro()

# PORT-17 (issue #73, ThreatFinderUpdate + SET_COUNTER_TO_THREAT_FINDER_THREAT): sources and tests of the openbfme_core library and the test
# executable. The module is src/GameLogic/Module/ThreatFinderUpdate.cpp (slot 45 RW 0x7EDECE, update RW 0x7EDDB6), the finder and its manager
# src/GameLogic/System/ThreatFinder.cpp (RW 0xDE8888, the answer RW 0x7EE1F4), the script action case 556 (RW 0x7C68AA) is in
# src/GameLogic/ScriptEngine/ScriptActionsThreatFinder.cpp (SCRIPT-1's list). Included from engine/CMakeLists.txt.

# simulation sources (sim audit manifest): the finders are logic state and they travel in the object's snapshot (xfer RW 0x7EDF70)
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/System/ThreatFinder.cpp
    src/GameLogic/Module/ThreatFinderUpdate.cpp
)
set(OPENBFME_PORT17_TESTS
    tests/test_port_17.cpp
)

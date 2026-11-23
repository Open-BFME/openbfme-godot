# Lane THREATLEVEL-1 (the map scripts' threat level: conditions UNIT_THREAT_LEVEL 115 / TEAM_THREAT_LEVEL 116, actions SET_COUNTER_TO_UNIT_THREAT 439 /
# SET_COUNTER_TO_TEAM_THREAT 440, RW 0x608BCA): sources of the openbfme_core library and tests of the test executable. Included from engine/CMakeLists.txt
# so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): the cached threats are logic state (Object + 0x464 / + 0x468, Team + 0x120 / + 0x124)
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/ScriptEngine/ScriptThreat.cpp
)
set(OPENBFME_THREATLEVEL1_TESTS
    tests/test_script_threat.cpp
)

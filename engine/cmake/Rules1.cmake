# Lane RULES-1 (the lobby's Rules tab: RotWK's game rules end to end, and OpenBFME's Legendary Heroes rule): sources and tests of the openbfme_core library and
# the test executable. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): the rule tables are game setup the logic reads (start money, command points, the ring heroes game mode); the
# Legendary Heroes rule changes object state
list(APPEND OPENBFME_SIM_SOURCES
    src/Common/MpGameRules.cpp
    src/GameLogic/LegendaryHeroesGame.cpp
)
# client-only sources (excluded from the simulation audit in tools/sim/sim_policy.json: the Rules tab's gadgets)
set(OPENBFME_RULES1_SOURCES
    src/GameClient/GUI/AptScreens/AptSkirmishRules.cpp
)
set(OPENBFME_RULES1_TESTS
    tests/test_rules1.cpp
)

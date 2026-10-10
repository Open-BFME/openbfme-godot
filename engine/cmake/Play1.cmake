# Lane PLAY-1 (the owner's first hands-on skirmish): sources and tests of the openbfme_core library and the test executable. Included from
# engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): the tribute, MSG_GIVE_MONEY (RW 0x6264E1)
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/TributeCommands.cpp
)
# client-only sources (excluded from the simulation audit in tools/sim/sim_policy.json)
set(OPENBFME_PLAY1_SOURCES
    src/GameClient/PlacementGhost.cpp
)
set(OPENBFME_PLAY1_TESTS
    tests/test_play1_tribute.cpp
    tests/test_play1_tribute_screen.cpp
    tests/test_play1_placement_ghost.cpp
    tests/test_play1_hud_upgrade.cpp
)

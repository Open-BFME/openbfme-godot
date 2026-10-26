# Lane CHEAT-1 (the open Cheat Mode of custom games): sources and tests of the openbfme_core library and the test executable. Included from
# engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): MSG_OPENBFME_CHEAT, run by every peer
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/CheatMode.cpp
)
# the GDExtension's view of Cheat Mode (excluded from the simulation audit in tools/sim/sim_policy.json: it queues the panel's commands, reads display data)
set(OPENBFME_CHEAT1_GODOT_SOURCES
    src/GodotDevice/GodotGameWorldCheats.cpp
)
set(OPENBFME_CHEAT1_TESTS
    tests/test_cheat1_mode.cpp
)

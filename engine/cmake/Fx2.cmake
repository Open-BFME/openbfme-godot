# Lane FX-2 (effects in the live game): sources and tests of the openbfme_core library and the test executable. Included from engine/CMakeLists.txt so
# the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): the logic's effect calls (FXEventLog) live in GameLogic and are called from simulation code
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/FXEvents.cpp
)
# client-only sources (excluded from the simulation audit in tools/sim/sim_policy.json): the live game's effect player
set(OPENBFME_FX2_SOURCES
    src/GameClient/LiveFX.cpp
)
set(OPENBFME_FX2_TESTS
    tests/test_fx2_events.cpp
)

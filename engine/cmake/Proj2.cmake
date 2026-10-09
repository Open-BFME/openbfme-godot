# Lane PROJ-2 (siege projectiles that look like retail: the WeaponStatusHelper, the launch frame, the client fade-in): sources and tests of the openbfme_core library and the
# test executable. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): the helper sets the object's weapon model conditions (lockstep state)
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/Module/WeaponStatusHelper.cpp
)
# client-only sources (excluded from the simulation audit in tools/sim/sim_policy.json): the drawable's fade
set(OPENBFME_PROJ2_SOURCES
    src/GameClient/DrawableFade.cpp
)
set(OPENBFME_PROJ2_TESTS
    tests/test_hud_proj2_retail.cpp
    tests/test_hud_proj2_arrows.cpp
)

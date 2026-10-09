# Lane RENDER-2 (projectile launch bones, the construction look): sources and tests of the openbfme_core library and the test executable. Included from
# engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): the drawable's launch bone answer enters the projectile's launch transform (RW 0x6CAB85), so it is simulation code
list(APPEND OPENBFME_SIM_SOURCES
    src/GameClient/DrawableLaunchBones.cpp
    src/GameLogic/Object/PristinePose.cpp
)
# client-only sources (excluded from the simulation audit in tools/sim/sim_policy.json): the construction look of a drawable
set(OPENBFME_RENDER2_SOURCES
    src/GameClient/DrawableConstruction.cpp
)
set(OPENBFME_RENDER2_TESTS
    tests/test_render2_launch.cpp
    tests/test_hud_render2_retail.cpp
    tests/test_build_render2_construction.cpp
    tests/test_render2_fx.cpp
)

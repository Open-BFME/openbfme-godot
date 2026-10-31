# Lane FOGFX-1 (fog of war: effects in the shroud, the shroud's fade, build placement): sources and tests of the openbfme_core library and the test
# executable. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# client-only sources (excluded from the simulation audit in tools/sim/sim_policy.json): W3DShroud's fog level interpolation
set(OPENBFME_FOGFX1_SOURCES
    src/GameClient/ShroudFade.cpp
)
set(OPENBFME_FOGFX1_TESTS
    tests/test_fogfx1_shroud_fade.cpp
)

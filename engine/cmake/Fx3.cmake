# Lane FX-3 (the particle and draw gaps every game shows, QA-1 U6 .. U9): sources and tests of the openbfme_core library and the test executable.
# Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# client-only sources (excluded from the simulation audit in tools/sim/sim_policy.json): the draw scripts' view of the object's target
set(OPENBFME_FX3_SOURCES
    src/GameClient/DrawableScriptTarget.cpp
)
set(OPENBFME_FX3_TESTS
    tests/test_fx3_particles.cpp
)

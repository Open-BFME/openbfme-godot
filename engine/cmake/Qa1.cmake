# Lane QA-1 (the whole skirmish played end to end, and the fixes it led to): sources of the openbfme_core library and tests of the test executable.
# Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

set(OPENBFME_QA1_SOURCES
    src/GameClient/DrawablePick.cpp
)
set(OPENBFME_QA1_TESTS
    tests/test_qa1_pick.cpp
)
# DrawablePick is the HUD's pick (client input interpretation): excluded from the simulation audit in tools/sim/sim_policy.json with its reason.

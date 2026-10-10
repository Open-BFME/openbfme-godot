# Lane RADAR-1 (the Palantir radar as RotWK draws it: the object overlay, the view box band): the tests of the test executable. The sources are the HUD's
# (src/GameClient/Radar.cpp, client state, listed in tools/sim/sim_policy.json). Included from engine/CMakeLists.txt so the lane's file list stays out of the shared list.

set(OPENBFME_RADAR1_TESTS
    tests/test_radar1.cpp
    tests/test_radar1_retail.cpp
)

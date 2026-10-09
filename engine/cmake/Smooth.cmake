# Lane SMOOTH-1 (smooth motion: render interpolation, animation timing, frame pacing): sources and tests of the openbfme_core library and the test executable.
# Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# client-only sources (excluded from the simulation audit in tools/sim/sim_policy.json): the render pose between two logic frames
set(OPENBFME_SMOOTH_SOURCES
    src/GameClient/RenderInterpolation.cpp
    src/GameClient/ClientEvents.cpp
    src/GameClient/LogicSnapshot.cpp
    src/GameClient/LiveGameRunner.cpp
)
set(OPENBFME_SMOOTH_TESTS
    tests/test_smooth_interpolation.cpp
    tests/test_smooth_threaded.cpp
    tests/test_smooth_footprints.cpp
    tests/test_smooth_live.cpp
    tests/test_smooth_races.cpp
    tests/test_smooth_net.cpp
)

# lane SMOOTH-2 (hordes and cavalry move smoothly)
list(APPEND OPENBFME_SMOOTH_TESTS
    tests/test_smooth2_horde_motion.cpp
)

# lane SMOOTH-3 (the battalions' motion, second round)
list(APPEND OPENBFME_SMOOTH_TESTS
    tests/test_smooth3_motion.cpp
)

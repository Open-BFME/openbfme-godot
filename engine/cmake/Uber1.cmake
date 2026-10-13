# Lane UBER-1 (the Uber performance mode, docs/UBER.md: the opt-in simulation profile that spreads the logic over every core): sources and tests of the
# openbfme_core library and the test executable. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

set(OPENBFME_UBER1_TESTS
    tests/test_uber1.cpp
    tests/test_uber1_bench.cpp
)

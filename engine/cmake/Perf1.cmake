# Lane PERF-1 (performance: the benchmarks and the determinism proof of the optimisations): tests of the test executable.
# Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.
set(OPENBFME_PERF1_TESTS
    tests/test_perf1_bench.cpp
    tests/test_perf1_caches.cpp
)

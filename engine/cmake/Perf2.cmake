# Lane PERF-2 (deterministic multithreading: the job system and the parallel phases of the logic, the client and the loading): sources and tests of the
# openbfme_core library and the test executable. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): the job system runs the pure parallel phases of the logic frame
set(OPENBFME_PERF2_SOURCES
    src/Common/System/JobSystem.cpp
)
list(APPEND OPENBFME_SIM_SOURCES ${OPENBFME_PERF2_SOURCES})
set(OPENBFME_PERF2_TESTS
    tests/test_perf2_jobs.cpp
    tests/test_perf2_determinism.cpp
    tests/test_perf2_bench.cpp
    tests/test_perf2_client.cpp
    tests/test_perf2_loading.cpp
)

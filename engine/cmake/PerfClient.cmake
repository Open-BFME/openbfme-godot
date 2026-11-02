# Lane PERF-CLIENT (the render frame's work on the client job pool, and the pool's size while the logic worker is behind): tests of the test
# executable. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list. No new sources (ClientPoolPolicy.h is
# header-only).
set(OPENBFME_PERFCLIENT_TESTS
    tests/test_client_pool_policy.cpp
)

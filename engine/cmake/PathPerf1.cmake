# Lane PATHPERF-1 (the cost of a big move order: findPath's hierarchical corridor, the render side's wait): sources of openbfme_core and tests of the
# test executable. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): the hierarchical block search of findPath (RW 0x6F8601)
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/AI/AIPathfindHierarchical.cpp
)
set(OPENBFME_PATHPERF1_TESTS
    tests/test_pathperf1_bench.cpp
    tests/test_pathperf1_hier.cpp
)

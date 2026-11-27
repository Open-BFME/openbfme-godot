# GiantBirdSlowDeathBehavior (RW 0x86511C): sources and tests of the openbfme_core library and the test executable. Included from engine/CMakeLists.txt.

# simulation sources (sim audit manifest): the module flings the body and scans the partition
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/Module/GiantBirdSlowDeathBehavior.cpp
)
set(OPENBFME_GIANTBIRDSLOWDEATH_TESTS
    tests/test_giant_bird_slow_death.cpp
)

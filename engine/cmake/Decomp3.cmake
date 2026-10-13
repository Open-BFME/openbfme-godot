# Lane DECOMP-3 (the skirmish AI's player info record, AIStatCollector RW 0x8E46D9): the lane's own tests. Its source is in the skirmish AI list
# (cmake/SkirmishAI.cmake, simulation code). Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.
set(OPENBFME_DECOMP3_TESTS
    tests/test_decomp3.cpp
)

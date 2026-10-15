# Lane BACKLOG6 (the skirmish AI's fortress rebuild RW 0x9BC882, AIBuilder::update RW 0x8F0EB4, the unit priority RW 0x9EDA17 and AIGroup RW 0x774897's
# gate and stealth delay): the lane's own tests. Its sources are existing simulation files (cmake/SkirmishAI.cmake, the AI list). Included from
# engine/CMakeLists.txt so the lane's file lists stay out of the shared list.
set(OPENBFME_BACKLOG6_TESTS
    tests/test_backlog6.cpp
)

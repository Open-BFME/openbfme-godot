# Lane MOVE-3 (community feedback FB-0012 / FB-0006: hostile hordes passing through each other, hordes stacking on one spot): sources and tests. Included from
# engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): RotWK's path patch of a blocked unit (RW 0x6F7938 / 0x6ED46C / 0x6EDFD7)
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/AI/AIPathfindPatch.cpp
)
set(OPENBFME_MOVE3_TESTS
    tests/test_move3_horde_spacing.cpp
    tests/test_move3_review.cpp
)

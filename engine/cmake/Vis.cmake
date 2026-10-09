# VIS-1 (the shroud, fog of war and what each player can see): sources and tests for the openbfme_core library.
# Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list. Every source here is simulation code (the audit manifest).

set(OPENBFME_VIS_SOURCES
    src/GameLogic/System/ShroudManager.cpp
    src/GameLogic/System/VisionSettings.cpp)

set(OPENBFME_VIS_TESTS
    tests/test_shroud.cpp)

# Lane FLY-1 (the GIANT_BIRD flyers: the aerial path, the GIANT_BIRD movers, GiantBirdAIUpdate): sources and tests of the openbfme_core library and the test
# executable. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest)
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/AI/AerialPathfinder.cpp
    src/GameLogic/Object/LocomotorGiantBird.cpp
    src/GameLogic/Module/GiantBirdAIUpdate.cpp
)
set(OPENBFME_FLY1_TESTS
    tests/test_fly1_giant_bird.cpp
)

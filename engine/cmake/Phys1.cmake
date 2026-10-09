# Lane PHYS-1 (solid collision between units and structures, PhysicsBehavior): sources and tests. Included from engine/CMakeLists.txt so the lane's file lists
# stay out of the shared list.

# simulation sources (sim audit manifest): RotWK's PhysicsBehavior (the fling flight)
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/Module/PhysicsBehavior.cpp
    src/GameLogic/AI/AIPathfindCrowd.cpp
    src/GameLogic/AI/AIPathfindAttack.cpp
    src/GameLogic/AI/AIPathfindMoveAway.cpp
    src/GameLogic/AI/AIPathfindMelee.cpp
    src/GameLogic/AI/AIAttackMelee.cpp
    src/GameLogic/Module/AIUpdateAllies.cpp
)
set(OPENBFME_PHYS1_TESTS
    tests/test_phys1_overlap.cpp
    tests/test_phys1_physics.cpp
    tests/test_phys1_rank_odr.cpp
    tests/test_phys1_pathfind.cpp
    tests/test_phys1_approach.cpp
    tests/test_phys1_melee.cpp
    tests/test_phys1_fire_gate.cpp
)

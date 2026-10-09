# COMBAT-2 (structures fight and die: the body classes without a runtime, the collapse, victory and defeat): sources and tests for the openbfme_core library.
# Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list. Every source here is simulation code (the audit manifest).

set(OPENBFME_STRUCTURE_SOURCES
    src/GameLogic/StructureStops.cpp
    src/GameLogic/VictoryConditions.cpp
    src/GameLogic/Object/ObjectGeometry.cpp
    src/GameLogic/Module/StructureModules.cpp)

set(OPENBFME_STRUCTURE_TESTS
    tests/test_structure_bodies.cpp
    tests/test_structure_victory.cpp
    tests/test_structure_hash.cpp
    tests/test_structure_review.cpp
    tests/test_hud_combat_structure.cpp)

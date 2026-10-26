# FLANK-1 (flanking at fire time, crush contact and the squish state's re-acquisition, projectiles that meet objects in flight, the miss scatter): tests. Included
# from engine/CMakeLists.txt so the lane's file lists stay out of the shared list. The lane's code changes live in existing simulation sources (ProjectileModules.cpp,
# CombatState.cpp, WeaponDelivery.cpp, SquishCollide.cpp, HordeAIUpdateCombat.cpp, HordeFlank.cpp, ObjectGeometry.cpp), already in the audit manifest.

set(OPENBFME_FLANK1_TESTS
    tests/test_flank1.cpp
    tests/test_siege1_gate.cpp)   # lane SIEGE-1: the in-flight contacts' pair rule against the fortress gates

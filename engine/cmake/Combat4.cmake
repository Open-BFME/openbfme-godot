# COMBAT-4 (impacts that throw units, siege crews in motion, corpses that sink: community FB-0004 / FB-0003 / FB-0002 / FB-0011): tests. Included from
# engine/CMakeLists.txt so the lane's file lists stay out of the shared list. The lane's code changes live in existing simulation sources (PhysicsBehavior.cpp,
# WeaponDelivery.cpp, Object.cpp, SquishCollide.cpp, ...), already in the audit manifest.

set(OPENBFME_COMBAT4_TESTS
    tests/test_hud_combat4_retail.cpp)

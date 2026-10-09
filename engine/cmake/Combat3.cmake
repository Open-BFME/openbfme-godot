# COMBAT-3 (cavalry charges and siege crews: the crush knockback, the shock stun, the crush modifiers, the formations' AttributeModifiers, the passengers' bone
# conditions): tests. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list. The lane's code changes live in existing simulation
# sources (PhysicsBehavior.cpp, SquishCollide.cpp, HordeFormation.cpp, OpenContainRuntime.cpp, ...), already in the audit manifest.

set(OPENBFME_COMBAT3_TESTS
    tests/test_hud_combat3_retail.cpp)

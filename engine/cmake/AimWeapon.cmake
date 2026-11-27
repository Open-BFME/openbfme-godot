# AimWeaponBehavior (RW 0x85E30B): sources and tests of the openbfme_core library and the test executable. Included from engine/CMakeLists.txt.

# simulation sources (sim audit manifest): the module sets an object's model conditions, which are hashed and travel in its snapshot
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/Module/AimWeaponBehavior.cpp
)
set(OPENBFME_AIMWEAPON_TESTS
    tests/test_aim_weapon.cpp
)

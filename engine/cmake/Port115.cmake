# PORT-115: the archers' Barrage — Command_ArcherBombard (commandbutton.ini:1077), a horde's FIRE_WEAPON at a position
# (issue fork issue 299, PROBLEMS.md P-60). The lane's engine change is the group weapon lock and its release
# (AIGroup::setWeaponLockForGroup RW 0x76FAB7 / releaseWeaponLockForGroup RW 0x76FB3F, which call Object::setWeaponLock
# RW 0x69121A / Object::releaseWeaponLock RW 0x68DF11 and so reach the contain's slots 0x168 / 0x16C): files other lists
# already carry. RW analysis: PORT115_RW_ANALYSIS.md.

set(OPENBFME_PORT115_TESTS
    tests/test_port_115.cpp
)
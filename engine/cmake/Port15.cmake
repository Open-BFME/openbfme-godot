# PORT-15 (issue #71, LockWeaponCreate/Object::setWeaponLock — the lock goes through RW 0x69121A, contain pass-through included): tests for the
# openbfme_tests executable. The lane's engine change is in a file other lists already carry (src/GameLogic/Module/ExtraCreateModules.cpp,
# LockWeaponCreate::onBuildComplete RW 0x8BCE0A -> RW 0x69121A).

set(OPENBFME_PORT15_TESTS
    tests/test_port_15.cpp)

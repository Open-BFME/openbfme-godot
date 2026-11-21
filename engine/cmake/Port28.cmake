# PORT-28 (issue fork issue 100): Weapon/RotatingTurret, the turret aim vector. The block RW 0x6CAD3F .. 0x6CB43B inside
# Weapon::calcProjectileLaunchPosition (RW 0x6CAB85) turns the launch transform of a projectile by the turret's angle and pitch
# (the flag WeaponTemplate + 0x16C, the turret of RW 0x66246B); its engine changes are in files other lists already carry
# (ProjectileModules, AIUpdate, Weapon.h, TurretAI.h).
set(OPENBFME_PORT28_TESTS
    tests/test_port_28.cpp)
# PORT-222: the experience a LevelGrantSpecialPower gives lands as RotWK 2.01 gives it — a grant on a horde fills the horde contain's
# experience pool (RW 0x8C6E82 -> RW 0x873A02) instead of each member's own tracker.
# The engine change under test is LevelGrantSpecialPower::grant (GameLogic/Module/SpecialAbilityModules.cpp); the ability update, the module
# trigger and HordeContain::addExperience are not given a second handler.

set(OPENBFME_PORT222_TESTS
    tests/test_port_222.cpp)
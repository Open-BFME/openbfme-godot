# PORT-65 (issue fork issue 177, SpellRechargeModifierUpgrade): tests of the class. The lane's engine changes are in files other lists already carry
# (src/GameLogic/Module/UpgradeModules.cpp / .h: the class, its data and the field table RW 0xC6F0D0; src/Common/PlayerScience.cpp / .h: the controlling
# player's counter + 0x71C and its discount + 0x718, RW 0x8BA139 / 0x8B9FE0 / 0x8BA05E).

set(OPENBFME_PORT65_TESTS
    tests/test_port_65.cpp)
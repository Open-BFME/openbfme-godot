# PORT-206 (fork issue 488): WeaponBonusUpgrade (the PLAYER_UPGRADE writer, RW 0x8B980D).

list(APPEND OPENBFME_SIM_SOURCES src/GameLogic/Module/WeaponBonusUpgrade.cpp)

set(OPENBFME_PORT206_TESTS
    tests/test_port_206.cpp)

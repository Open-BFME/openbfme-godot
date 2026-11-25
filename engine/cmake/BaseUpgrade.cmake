# BaseUpgrade (LOCAL-36, fork issue 286): spawns BuildingTemplateName on upgrade grant (RW 0x8B7933).

list(APPEND OPENBFME_SIM_SOURCES src/GameLogic/Module/BaseUpgrade.cpp)

set(OPENBFME_BASE_UPGRADE_TESTS
    tests/test_base_upgrade.cpp)

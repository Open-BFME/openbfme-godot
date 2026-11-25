# TooltipUpgrade (MODGAP-2): custom tooltip DisplayName/Description on upgrade grant/removal (RW 0x8BBBCC / 0x8BBC0A).

list(APPEND OPENBFME_SIM_SOURCES src/GameLogic/Module/TooltipUpgrade.cpp)

set(OPENBFME_TOOLTIP_UPGRADE_TESTS
    tests/test_modgap2_tooltip_upgrade.cpp)

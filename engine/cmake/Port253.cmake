# PORT-253: Upgrade modules: SubObjectsUpgrade (logic side), GeometryUpgrade, ReplaceSelfUpgrade, CostModifierUpgrade, InheritUpgradeCreate, SpellRechargeModifierUpgrade (fork issue 601)

set(OPENBFME_PORT253_TESTS
    tests/test_port_253.cpp)

set(OPENBFME_PORT253_SOURCES
    src/GameLogic/Module/UpgradeModuleClasses.cpp
    src/GameLogic/Module/GeometryUpgrade.cpp
    src/GameLogic/Module/ReplaceSelfUpgrade.cpp
    src/GameLogic/Module/UpgradeModules.cpp
    src/GameLogic/Module/ExtraCreateModules.cpp)

# Lane UPGRADE-1 (the UpgradeCenter, the upgrade module base and the upgrade module classes, upgrade research): sources and tests.
# Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

set(OPENBFME_UPGRADE_SOURCES
    src/Common/Upgrade.cpp
    src/GameLogic/Module/UpgradeModule.cpp
    src/GameLogic/Module/UpgradeModuleClasses.cpp
    src/GameLogic/UpgradeCommands.cpp
)
set(OPENBFME_UPGRADE_TESTS
    tests/test_upgrade_center.cpp
    tests/test_upgrade_retail.cpp
    tests/test_upgrade_modules.cpp
)

# Lane PROD-1 (command sets, production queues, exits, rally points): sources and tests of the openbfme_core library and the test executable.
# Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

set(OPENBFME_PROD_SOURCES
    src/GameLogic/GameMessage.cpp
    src/GameLogic/GameLogicDispatch.cpp
    src/GameLogic/Module/SMCHelper.cpp
    src/GameLogic/AI/AICommandSink.cpp
    src/GameLogic/Module/QueueProductionExitUpdate.cpp
    src/GameLogic/Module/ProductionUpdate.cpp
    src/GameLogic/Module/ProductionExitModules.cpp
    src/GameLogic/ProductionSettings.cpp
    src/GameLogic/UpgradeTypes.cpp
    src/Common/BuildAssistant.cpp
    src/GameClient/ControlBarCommands.cpp
)
set(OPENBFME_PROD_TESTS
    tests/test_prod_object.cpp
    tests/test_prod_commands.cpp
    tests/test_prod_production.cpp
    tests/test_prod_hash.cpp
    tests/test_prod_retail.cpp
)

# Lane WOTR-1 (War of the Ring: the Living World data, the strategic logic, the battle hand-off): sources and tests of the openbfme_core library
# and the test executable. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): the strategic game's state (regions, armies, treasuries, turns) is logic state (hashed)
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/LivingWorld/LivingWorldData.cpp
    src/GameLogic/LivingWorld/LivingWorldLogic.cpp
    src/GameLogic/LivingWorld/LivingWorldUnits.cpp
    src/GameLogic/LivingWorld/LivingWorldAutoResolveData.cpp
    src/GameLogic/LivingWorld/LivingWorldAutoResolveBattle.cpp
    src/GameLogic/LivingWorld/SciencePurchasePointTracker.cpp
    src/GameLogic/LivingWorld/LivingWorldLogicXfer.cpp
    src/GameLogic/LivingWorld/LivingWorldLogicUpgrades.cpp
    src/GameLogic/LivingWorld/LivingWorldLogicScore.cpp
    src/GameLogic/LivingWorld/LivingWorldLogicMessages.cpp
    src/GameLogic/LivingWorld/LivingWorldAI.cpp
    src/GameLogic/LivingWorld/LivingWorldAIInformation.cpp
    src/GameLogic/LivingWorld/LivingWorldAITroopProducer.cpp
    src/GameLogic/LivingWorld/LivingWorldAIBuilder.cpp
    src/GameLogic/LivingWorld/LivingWorldAIArmyMover.cpp
    src/GameLogic/LivingWorld/LivingWorldAIGarrisonMover.cpp
    src/GameLogic/LivingWorld/BattleResultCapture.cpp
    src/GameNetwork/StrategicSession.cpp
)
# client-only sources (the GUI globs of tools/sim/sim_policy.json)
set(OPENBFME_WOTR1_SOURCES
    src/GameClient/GUI/AptScreens/AptStrategicHUD.cpp
    src/GameClient/GUI/AptScreens/AptStrategicConflictResults.cpp
    src/GameClient/GUI/AptScreens/AptBannerUI.cpp
    src/GameClient/LivingWorldScoreScreen.cpp
    src/GameClient/GUI/Skirmish/LivingWorldSetupText.cpp
    src/GameClient/GUI/AptScreens/StrategicHUDData.cpp
)
set(OPENBFME_WOTR1_TESTS
    tests/test_wotr1.cpp
    tests/test_wotr2.cpp
    tests/test_wotr4.cpp
    tests/test_wotr8_net.cpp
    tests/test_wotr9_online.cpp
)

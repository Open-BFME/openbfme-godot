# Lane BFME2CAMP1 (the BFME2 Good / Evil campaigns and the tutorials under RotWK): tests of the test executable. Included from engine/CMakeLists.txt so the
# lane's file lists stay out of the shared list.
# simulation sources (sim audit manifest): the campaign's carry-over units are objects and player state of the game
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/CampaignCarryover.cpp
    src/GameLogic/Module/RebuildHoleModules.cpp
)
set(OPENBFME_BFME2CAMP1_TESTS
    tests/test_bfme2camp1.cpp
    tests/test_bfme2camp1_m3.cpp
    tests/test_bfme2camp1_m4.cpp
)
# client-only sources (GameClient: excluded from the simulation audit): GameData's tutorial records (AptMainMenu::OnTutorial)
list(APPEND OPENBFME_CAMP1_SOURCES
    src/GameClient/TutorialSettings.cpp
)

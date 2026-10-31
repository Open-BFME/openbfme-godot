# Lane CAMPUI-1 (the campaign presentation as retail: the intro camera, the military caption, the stance sub-menu, the mission objectives): sources and
# tests. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): TheMissionObjectiveTracker (the script actions change it)
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/ScriptEngine/MissionObjectiveTracker.cpp
)
# client sources: the objectives screen (Objectives.apt)
list(APPEND OPENBFME_APT_SOURCES
    src/GameClient/GUI/AptScreens/AptObjectivesMenu.cpp
    src/GameClient/GUI/AptScreens/InGameNotificationBox.cpp
    src/GameClient/GUI/AptScreens/AptPalantirObjectives.cpp
)
set(OPENBFME_CAMPUI1_TESTS
    tests/test_campui1_objectives.cpp
    tests/test_campui1_palantir.cpp
)

# Lane SAVE-2 (the command-log save for single player, docs/SAVES.md): sources and tests of the openbfme_core library and the test executable. Included from
# engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# client-only sources (excluded from the simulation audit in tools/sim/sim_policy.json): the save container (its SHA-256 is ONLINE-2's Common/Sha256), the
# catch-up that plays a save's log back (it decides which commands run, the log's, through the audited replay player Common/Recorder.cpp)
set(OPENBFME_SAVE2_CLIENT_SOURCES
    src/Common/System/SaveGame/SaveGameFile.cpp
    src/GameClient/CommandLogLoad.cpp
)
# the GDExtension's side (client: save the running game, list the Save folder, load a save with its catch-up)
set(OPENBFME_SAVE2_GODOT_SOURCES
    src/GodotDevice/GodotGameWorldSave.cpp
)
set(OPENBFME_SAVE2_TESTS
    tests/test_save2.cpp
    tests/test_save2_retail.cpp
    tests/test_save2_menu.cpp
)
# lane SAVE-UI1: the in-game Save / Load screen flow (the quit menu's backgrounds, SaveLoad's Escape and its return to the game)
list(APPEND OPENBFME_SAVE2_TESTS tests/test_saveui1.cpp)
# lane INGAMEBG-1: the in-game background behind the quit menu, the Powers screen and the objectives (on SAVE-UI1's retail shell fixture)
list(APPEND OPENBFME_SAVE2_TESTS tests/test_ingamebg1.cpp)

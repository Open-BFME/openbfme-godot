# Lane INPUT-1: RotWK's input layer (the control groups of SelectionTranslator RW 0x83C29E, the camera bookmarks of LookAtTranslator RW 0x83AC4A, the
# CommandTranslator's meta cases RW 0x81F8D8, the CommandMap field table RW 0xBF0E70). Client only: the sources it changes belong to the HUD lists; the logic
# handler of MSG_ADD_TO_TEAM0..9 is in PlayerCommands.cpp (simulation list). Included from engine/CMakeLists.txt so the lane's file lists stay out of the
# shared list.
# client sources (the HotKeyTranslator presses command buttons through ControlBar::pressButton, whose messages ControlBarCommandProcessing.cpp builds:
# excluded from the simulation audit with its reason in tools/sim/sim_policy.json)
list(APPEND OPENBFME_HUD_SOURCES
    src/GameClient/MessageStream/HotKey.cpp
)
set(OPENBFME_INPUT1_TESTS
    tests/test_input1.cpp
)

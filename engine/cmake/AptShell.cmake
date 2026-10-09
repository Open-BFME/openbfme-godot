# Shell, WindowManager, native gadgets and the Skirmish setup screen (APT build order A4-A6; lane APT-4).
# Included from cmake/Apt.cmake so the lane's file lists stay out of the shared engine/CMakeLists.txt.

list(APPEND OPENBFME_APT_SOURCES
    src/GameClient/GUI/WindowManager.cpp
    src/GameClient/GUI/AptScreen.cpp
    src/GameClient/GUI/Shell/Shell.cpp
    src/GameClient/GUI/AptScreens/AptMainMenu.cpp
    src/GameClient/GUI/AptScreens/AptSimpleScreens.cpp
    src/GameClient/GUI/AptScreens/AptSkirmish.cpp
    src/GameClient/GUI/AptScreens/AptScreenFactories.cpp
    src/GameClient/GUI/Image.cpp
    src/GameClient/GUI/HeaderTemplate.cpp
    src/GameClient/GUI/GameWindow.cpp
    src/GameClient/GUI/GameWindowManager.cpp
    src/GameClient/GUI/GameWindowManagerScript.cpp
    src/GameClient/GUI/AptGadgetLayer.cpp
    src/GameClient/GUI/Gadget/GadgetPushButton.cpp
    src/GameClient/GUI/Gadget/GadgetCheckBox.cpp
    src/GameClient/GUI/Gadget/GadgetSlider.cpp
    src/GameClient/GUI/Gadget/GadgetListBox.cpp
    src/GameClient/GUI/Gadget/GadgetComboBox.cpp
    src/GameClient/GUI/Gadget/GadgetImageComboBox.cpp
    src/GameClient/GUI/Gadget/GadgetTextEntry.cpp)

# The skirmish setup's slot logic produces the GameInfo (map, slots, factions, colours, start positions) that the logic consumes at game start:
# a simulation input, so these three sources are simulation sources (the Apt screens around them are UI and excluded in tools/sim/sim_policy.json).
list(APPEND OPENBFME_SIM_SOURCES
    src/GameClient/GUI/Skirmish/SkirmishSetup.cpp
    src/GameClient/GUI/Skirmish/SkirmishGameSetup.cpp
    src/GameClient/GUI/Skirmish/IniSkirmishSetupSource.cpp)

list(APPEND OPENBFME_APT_TESTS
    tests/test_apt_shell.cpp
    tests/test_apt_gadgets.cpp
    tests/test_apt_skirmish.cpp
    tests/test_apt_shell_walk.cpp)

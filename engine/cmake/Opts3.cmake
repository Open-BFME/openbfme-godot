# Lane OPTS-3: the OpenBFME page of the Options screen (not retail), built from RotWK's own APT gadgets and drawn by the gadget layer. Client only
# (the AptScreens glob of tools/sim/sim_policy.json). Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.
list(APPEND OPENBFME_APT_SOURCES
    src/GameClient/GUI/AptScreens/OpenBFMEOptionsPage.cpp)
# Tests: engine/tests/test_fb7_menus.cpp ("opts3 ..." cases; they share its retail Options.apt shell fixture).

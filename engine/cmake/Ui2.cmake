# Lane UI-2 (the owner's menu feedback FEEDBACK-1 F1, F2, F5 and the soft-particles option): client-only sources and tests. Included from
# engine/CMakeLists.txt so the lane's file lists stay out of the shared list.
set(OPENBFME_UI2_CLIENT_SOURCES
    src/GameClient/GUI/Gadget/GadgetMapPreview.cpp
    src/GameClient/AptView3D.cpp
    src/GameClient/OptionPreferences.cpp
)
set(OPENBFME_UI2_GODOT_SOURCES
    src/GodotDevice/GodotAptView3D.cpp
)
set(OPENBFME_UI2_TESTS
    tests/test_ui2_apt_colour.cpp
    tests/test_ui2_lobby.cpp
    tests/test_ui2_endgame.cpp
    tests/test_ui2_options.cpp
)

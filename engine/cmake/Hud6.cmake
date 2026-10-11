# Lane HUD-6: RotWK's in-game UI pieces still missing: the radial command bubbles over a structure, the side command bar's frame (SetButtonState), the help
# box. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# client sources (sim audit: excluded in tools/sim/sim_policy.json as user interface)
set(OPENBFME_HUD6_CLIENT_SOURCES
    src/GameClient/GUI/AptScreens/AptPalantirSideBar.cpp
    src/GameClient/ControlBarRadialMenu.cpp
    src/GameClient/CommandButtonHelp.cpp
    src/GameClient/InGameHelpBox.cpp
    src/GameClient/InGameHudHud6.cpp
)
set(OPENBFME_HUD6_TESTS
    tests/test_hud6.cpp
)
# the device layer (GDExtension)
set(OPENBFME_HUD6_GODOT_SOURCES
    src/GodotDevice/GodotHud6Draw.cpp
)

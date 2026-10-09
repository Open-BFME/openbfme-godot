# Lane HUD-1 (the in-game HUD and player input: message stream and translators, InGameUI, control bar, Palantir, radar): sources and tests of the
# openbfme_core library and the test executable. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

set(OPENBFME_HUD_SOURCES
    src/GameClient/MessageStream/MessageStream.cpp
    src/GameClient/MessageStream/MetaEvent.cpp
    src/GameClient/MessageStream/SelectionXlat.cpp
    src/GameClient/MessageStream/GUICommandTranslator.cpp
    src/GameClient/MessageStream/CommandXlat.cpp
    src/GameClient/TacticalView.cpp
    src/GameClient/CameraSettings.cpp
    src/GameClient/TacticalCamera.cpp
    src/GameClient/MessageStream/LookAtXlat.cpp
    src/GameClient/InGameUI.cpp
    src/GameClient/HudObjects.cpp
    src/GameClient/HudContext.cpp
    src/GameClient/CursorFile.cpp
    src/GameClient/HudInput.cpp
    src/GameClient/InGameHud.cpp
    src/GameClient/ControlBar.cpp
    src/GameClient/Radar.cpp
    src/GameClient/ControlBarCommandProcessing.cpp
    src/GameClient/GUI/AptScreens/AptPalantir.cpp
    src/GameLogic/PlayerCommands.cpp
)
set(OPENBFME_HUD_TESTS
    tests/test_hud_input.cpp
    tests/test_hud_palantir.cpp
    tests/test_hud_cursors.cpp
    tests/test_camera.cpp
)

# Simulation audit (tools/sim): the code that builds GameMessages or changes the players' simulation state is manifested; the camera, the picking maths, the control bar
# view, the Palantir screen and the device layer are client-side and excluded with their reasons in tools/sim/sim_policy.json.
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/PlayerCommands.cpp
    src/GameClient/MessageStream/MessageStream.cpp
    src/GameClient/MessageStream/CommandXlat.cpp
    src/GameClient/MessageStream/GUICommandTranslator.cpp
    src/GameClient/MessageStream/SelectionXlat.cpp
    src/GameClient/MessageStream/PlaceEventTranslator.cpp
    src/GameClient/InGameUI.cpp
    src/GameClient/HudInput.cpp
    src/GameClient/ControlBarCommandProcessing.cpp
)

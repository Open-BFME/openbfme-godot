# Lane UI-1 (the interface gaps other lanes reported: the lobby map list, the disconnect screen's bars, the transport / garrison HUD, the lobby extras,
# the missing-texture fallback): client-only sources and tests. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.
set(OPENBFME_UI1_CLIENT_SOURCES
    src/GameClient/GUI/AptMessageBox.cpp
)
set(OPENBFME_UI1_TESTS
    tests/test_ui1_retail.cpp
    tests/test_hud_ui1_inventory.cpp
    tests/test_ui1_disconnect_bars.cpp
    tests/test_ui1_message_box.cpp
    tests/test_ui1_apt_size.cpp
)

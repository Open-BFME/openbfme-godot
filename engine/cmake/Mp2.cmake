# Lane MP-2 (robust multiplayer: the disconnect path, a dropped player's assets, the desync dump, the LAN lobby and replay screens): sources and tests of the
# openbfme_core library and the test executable. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): what decides a frame's commands (the disconnect path's DESTROYPLAYER and frame gate) and the logic's
# MSG_SELF_DESTRUCT executor
set(OPENBFME_MP2_SOURCES
    src/GameNetwork/DisconnectManager.cpp
    src/GameNetwork/NetworkSettings.cpp
    src/GameLogic/SelfDestruct.cpp
)
list(APPEND OPENBFME_SIM_SOURCES ${OPENBFME_MP2_SOURCES})
# client-only sources (excluded from the simulation audit in tools/sim/sim_policy.json)
set(OPENBFME_MP2_CLIENT_SOURCES
    src/GameClient/GUI/AptScreens/AptDisconnectScreen.cpp
    src/GameNetwork/DesyncDump.cpp
    src/GameClient/GUI/AptScreens/AptSaveLoad.cpp
    src/GameNetwork/LANAPI.cpp
    src/GameClient/GUI/AptScreens/AptLanLobby.cpp
)
set(OPENBFME_MP2_TESTS
    tests/test_mp2_disconnect.cpp
    tests/test_mp2_net_retail.cpp
    tests/test_mp2_disconnect_screen.cpp
    tests/test_mp2_replay_menu.cpp
    tests/test_mp2_lan.cpp
    tests/test_mp2_lan_lobby.cpp
    tests/test_mp2_authority.cpp
)

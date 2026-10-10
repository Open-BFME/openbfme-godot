# Lane HUD-5 (the owner's first Windows play session): the players screen's Status page, the power points, construction progress, levels, fortress maps,
# gates. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): the gates (GateOpenAndCloseBehavior, AIGateUpdate, MSG_OPEN_GATE / MSG_CLOSE_GATE)
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/Module/GateModules.cpp
)
# client sources (sim audit: excluded in tools/sim/sim_policy.json as user interface)
set(OPENBFME_HUD5_CLIENT_SOURCES
    src/GameClient/GUI/PlayerStatusInfo.cpp
    src/GameClient/DrawableIconUI.cpp
)
set(OPENBFME_HUD5_TESTS
    tests/test_hud5_players.cpp
    tests/test_hud5_fortress.cpp
    tests/test_hud5_palantir.cpp
    tests/test_hud5_gates.cpp
    tests/test_hud5_icons.cpp
    tests/test_hud5_gatebutton.cpp
    tests/test_hud5_pick.cpp
)

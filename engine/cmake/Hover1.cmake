# Lane HOVER-1: every on-hover text from the game's own files: the remaining branches of the command button help, the hero bar's and the spell book's
# help, the unit help over a hero portrait. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.
# Lane HOVER-2 adds the mouse-over tool tip over an object in the world (MouseoverTooltip.cpp, test_hover2_mouseover.cpp).

# client sources (sim audit: excluded in tools/sim/sim_policy.json as user interface)
set(OPENBFME_HOVER1_CLIENT_SOURCES
    src/GameClient/GUI/AptHoverInventory.cpp
    src/GameClient/GUI/CursorTooltip.cpp
    src/GameClient/MouseoverTooltip.cpp
    src/GameClient/UnitHelpSource.cpp
)
set(OPENBFME_HOVER1_TESTS
    tests/test_hover1.cpp
    tests/test_hover1_menus.cpp
    tests/test_hover2_mouseover.cpp
)

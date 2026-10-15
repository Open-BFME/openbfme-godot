# Lane HOVER-1: every on-hover text from the game's own files: the remaining branches of the command button help, the hero bar's and the spell book's
# help, the unit help over a hero portrait. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# client sources (sim audit: excluded in tools/sim/sim_policy.json as user interface)
set(OPENBFME_HOVER1_CLIENT_SOURCES
    src/GameClient/GUI/AptHoverInventory.cpp
    src/GameClient/GUI/CursorTooltip.cpp
    src/GameClient/UnitHelpSource.cpp
)
set(OPENBFME_HOVER1_TESTS
    tests/test_hover1.cpp
    tests/test_hover1_menus.cpp
)

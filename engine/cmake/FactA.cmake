# FACT-A (Men / Elves / Dwarves: every unit, hero, upgrade and power as in retail): tests and the lane's simulation sources. Included from
# engine/CMakeLists.txt so the lane's file lists stay out of the shared list. Simulation sources are appended to OPENBFME_SIM_SOURCES (the audit manifest).

list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/BuildAssistantSell.cpp
    src/GameLogic/AI/WeaponCommands.cpp
    src/GameLogic/Module/ObjectCreationUpgrade.cpp
    src/GameLogic/Module/ReplaceSelfUpgrade.cpp)

set(OPENBFME_FACTA_TESTS
    tests/FactAStart.cpp
    tests/test_hud_facta_inventory.cpp
    tests/test_hud_facta_sell.cpp
    tests/test_hud_facta_production.cpp
    tests/test_hud_facta_abilities.cpp
    tests/test_hud_facta_stops.cpp
    tests/test_hud_facta_commands.cpp)

# CastleUpgrade is shared with lane FACT-B (one implementation, FACT-B's, registered by CastleModules::registerAll): listed once whichever lane's file
# comes first
if(NOT "src/GameLogic/Module/CastleUpgrade.cpp" IN_LIST OPENBFME_SIM_SOURCES)
    list(APPEND OPENBFME_SIM_SOURCES src/GameLogic/Module/CastleUpgrade.cpp)
endif()

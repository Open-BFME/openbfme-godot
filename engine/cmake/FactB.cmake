# Lane FACT-B (every unit, hero, upgrade and power of Isengard and Mordor as in retail): tests.
# Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

set(OPENBFME_FACTB_TESTS
    tests/test_hud_factb_inventory.cpp
    tests/test_hud_factb_production.cpp
    tests/test_hud_factb_units.cpp
)

# simulation sources (sim audit manifest): WeaponChangeSpecialPowerModule (the Berserker's torch)
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/Module/WeaponChangeSpecialPower.cpp
    src/GameLogic/Module/CastleUpgrade.cpp
)

# EvacuateDamage (RW 0x8BF12E): sources and tests of the openbfme_core library and the test executable. Included from engine/CMakeLists.txt.

# simulation sources (sim audit manifest): the module orders the occupants of a garrison out
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/Module/EvacuateDamage.cpp
)
set(OPENBFME_EVACUATEDAMAGE_TESTS
    tests/test_evacuate_damage.cpp
    tests/test_hud_evacuate_retail.cpp
)

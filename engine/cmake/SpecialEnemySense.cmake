# SpecialEnemySenseUpdate (RW 0x89D7E6): sources and tests of the openbfme_core library and the test executable. Included from engine/CMakeLists.txt.

# simulation sources (sim audit manifest): the module scans the partition and sets a model condition
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/Module/SpecialEnemySenseUpdate.cpp
)
set(OPENBFME_SPECIALENEMYSENSE_TESTS
    tests/test_special_enemy_sense.cpp
    tests/test_hud_special_enemy_retail.cpp
)

# Lane MODULES-1 (the remaining behaviour modules the base game uses): sources and tests of the openbfme_core library and the test executable. Included
# from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): every module here is object state that enters the lockstep hash
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/Module/ExtraModules.cpp
    src/GameLogic/Module/ExtraUpgradeModules.cpp
    src/GameLogic/Module/ExtraDieModules.cpp
    src/GameLogic/Module/ExtraUpdateModules.cpp
    src/GameLogic/Module/ExtraCreateModules.cpp
    src/GameLogic/Module/FlammableUpdate.cpp
)
set(OPENBFME_MOD1_TESTS
    tests/test_module_coverage.cpp
    tests/test_mod1_retail.cpp
    tests/test_mod1_parse.cpp
)

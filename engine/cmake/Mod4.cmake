# Lane MOD-4 (QA-1 U2: the modules nobody ported yet): sources and tests of the openbfme_core library and the test executable. Included from
# engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): the modules are object state that enters the lockstep hash
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/Module/AISpecialPowerUpdate.cpp
    src/GameLogic/Module/RepairSpecialPower.cpp
    src/GameLogic/Module/SpawnBehavior.cpp
    src/GameLogic/Module/SlavedUpdate.cpp
    src/GameLogic/Module/Mod4Stops.cpp # the registered stops (report text only)
)
set(OPENBFME_MOD4_TESTS
    tests/test_mod4_modules.cpp
)

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

# lane MODCOMPAT-MOD1: PickupStuffUpdate and the AI's temporary state machine it runs (AIUpdate.cpp)
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/Module/PickupStuffUpdate.cpp
)
list(APPEND OPENBFME_MOD4_TESTS
    tests/test_pickup_modules.cpp
)

# lane MODCOMPAT-MOD1: AutoPickUpUpdate (the WAKE_AUTO_PICKUP button: ObjectDoCommandButton.cpp)
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/Module/AutoPickUpUpdate.cpp
)
list(APPEND OPENBFME_MOD4_TESTS
    tests/test_autopickup_modules.cpp
)

# lane MODCOMPAT-MOD1: BoredUpdate (and the ActionManager's at-object test it asks, ActionManager.cpp)
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/Module/BoredUpdate.cpp
)
list(APPEND OPENBFME_MOD4_TESTS
    tests/test_bored_modules.cpp
)

# lane MODCOMPAT-MOD1: SalvageCrateCollide and its CrateCollide base (S-260; the crate contact pass is AIWorld.cpp's)
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/Module/SalvageCrateCollide.cpp
)
list(APPEND OPENBFME_MOD4_TESTS
    tests/test_salvage_crate.cpp
)

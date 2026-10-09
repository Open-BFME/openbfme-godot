# Lane STEALTH-1 (stealth, detection and invisibility: the InvisibilityManager, InvisibilityUpdate, StealthDetectorUpdate): sources and tests of the openbfme_core
# library and the test executable. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): the manager's entries and the modules are object state that enters the lockstep hash
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/System/InvisibilityManager.cpp
    src/GameLogic/Module/InvisibilityModules.cpp
    src/GameLogic/Module/StealthAbilityModules.cpp
)
# client-only sources (excluded from the simulation audit in tools/sim/sim_policy.json): the stealth look of the drawables
set(OPENBFME_STEALTH_SOURCES
    src/GameClient/StealthLook.cpp
)
set(OPENBFME_STEALTH_TESTS
    tests/test_stealth_parse.cpp
    tests/test_stealth_retail.cpp
    tests/test_hud_stealth_retail.cpp
    tests/test_hud_stealth_abilities_retail.cpp
)

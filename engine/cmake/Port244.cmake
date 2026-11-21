# PORT-244 (fork issue 568): SpecialAbilityUpdate PreTriggerUnstealthTime in handlePackingProcessing (RW 0x851E9F).
# The lane's engine changes are in files other lists already carry (GameLogic/Module/SpecialAbilityModules.cpp, GameLogic/Module/InvisibilityModules.cpp);
# this file adds the lane's tests only.

set(OPENBFME_PORT244_TESTS
    tests/test_port_244.cpp)
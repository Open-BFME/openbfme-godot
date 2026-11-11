# PORT-6 (StealthDetectorUpdate/CanDetectWhileGarrisoned): synthetic tests for the openbfme_tests executable. The engine change is in
# src/GameLogic/Module/InvisibilityModules.cpp, which the STEALTH-1 lane already carries; the decision is RW 0x8A6477 .. RW 0x8A64AB.

set(OPENBFME_PORT6_TESTS
    tests/test_port_6.cpp)

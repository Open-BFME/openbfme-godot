# PORT-249 (fork issue 577): OCLSpecialPower's answer for the special power interface's slot 0x48 (RW 0x8C744B, its callees RW 0x8C72C3 / RW 0x5EFB95 /
# RW 0x5F08C2). Tests for the openbfme_tests executable; the engine changes are in files other lists already carry (ActionManager,
# Module/SpecialPowerModules).

set(OPENBFME_PORT249_TESTS
    tests/test_port_249.cpp)
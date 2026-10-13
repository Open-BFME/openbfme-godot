# PLAY-4 (the Discord tester's gameplay report: production under construction, structure model states, the heroes' attacks, the stances): tests for the
# openbfme_tests executable. The lane's engine changes are in files other lists already carry.

set(OPENBFME_PLAY4_TESTS
    tests/test_play4_approach.cpp
    tests/test_play4_heroes_retail.cpp
    tests/test_play4_construction_retail.cpp
    tests/test_play4_stances_retail.cpp)

# simulation sources (sim audit manifest): RotWK's AI guard machine (the stances' guard, RW 0x662A21 / 0x949A10)
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/AI/AIGuard.cpp
)

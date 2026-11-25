# FreeLifeBody: lethal hit restores object to FreeLifeHealthPercent after FreeLifeTime cooldown (RW 0x8C5AE6 / 0x8C5B86).

list(APPEND OPENBFME_SIM_SOURCES src/GameLogic/Module/FreeLifeBody.cpp)

set(OPENBFME_FREE_LIFE_BODY_TESTS
    tests/test_free_life_body.cpp)

# PORT-196 (fork issue 463): the AI spell book's ARMY_BREAKER decision (RW 0x9E92CA), its taken-record timer RW 0x9E9222 and the
# constructor's reset RW 0x9E91EA. The engine change lives in GameLogic/Module/AISpecialPowerDecisions.cpp,
# GameLogic/Module/AISpecialPowerUpdate.cpp and GameLogic/SkirmishAI/ (the manager's taken list and its snapshot); this lane adds the tests.

set(OPENBFME_PORT196_TESTS
    tests/test_port_196.cpp)
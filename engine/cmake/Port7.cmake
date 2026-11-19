# PORT-7 (AnimalAIUpdate's enemy scan): synthetic tests for the openbfme_tests executable. The engine change is in
# src/GameLogic/Module/AmbientAIUpdate.cpp, the enemy scan of RW AnimalAIUpdate::update (RW 0x882058) with its UpdateTimer
# gate (RW 0x882288 .. RW 0x8822A6) and the flee step that stands in for aiFleeFromObject (RW 0x881FF3, S-5910).

set(OPENBFME_PORT7_TESTS
    tests/test_port_7.cpp)

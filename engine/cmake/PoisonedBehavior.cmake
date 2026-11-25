# PoisonedBehavior (LOCAL-97, fork issue 762): periodic poison ticks, poisoned tint hooks, and cure on healing (RW 0x008863B0 / 0x0088638E).

list(APPEND OPENBFME_SIM_SOURCES src/GameLogic/Module/PoisonedBehavior.cpp)

set(OPENBFME_POISONED_BEHAVIOR_TESTS
    tests/test_poisoned_behavior.cpp)

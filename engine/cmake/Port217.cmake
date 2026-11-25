# PORT-217 (fork issue 507, P-123): a builder turns to face the structure it builds or repairs.
# The lane's engine change is in a file other lists already carry (GameLogic/Module/DozerAIUpdate.cpp: the dozer's dock sub
# task 2 issues the AI's face command 0x26, RW 0x88DD3E / 0x88DA7A); this file adds the lane's tests only.

set(OPENBFME_PORT217_TESTS
    tests/test_port_217.cpp)

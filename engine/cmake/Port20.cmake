# PORT-20 (issue #76, the contain's vslot 0x64 onSelling, the sale's call at RW 0x796806 -> RW 0x86561F): tests for the openbfme_tests executable.
# The lane's engine changes are in files other lists already carry (src/GameLogic/Module/BehaviorModule.h, src/GameLogic/Object/Contain/OpenContainRuntime.*,
# src/GameLogic/BuildAssistantSell.cpp).

set(OPENBFME_PORT20_TESTS
    tests/test_port_20.cpp)

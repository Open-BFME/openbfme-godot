# PORT-13 (issue #68, AIData/AttackMoveUsesFormations — AIData + 0xBA, RW 0x774E2F / 0x77348C / 0x66BC68 / state 61 RW 0x74FC9B): tests for the
# openbfme_tests executable. The lane's engine changes live in files other lists already carry (GameLogicSettings.cpp, AIGroup.cpp, AIGroupMarch.cpp,
# AIFollowPathAsTeam.cpp, AICommands.cpp, AIUpdate.cpp, AIUpdateCombat.cpp, AITacticalAI.cpp).

set(OPENBFME_PORT13_TESTS
    tests/test_port13_attack_move_formations.cpp)

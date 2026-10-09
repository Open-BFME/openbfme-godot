# Lane MOVE-1 (units and hordes move: AIUpdateInterface on live objects, the member pass, the move commands): sources and tests of the openbfme_core library and
# the test executable. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

set(OPENBFME_MOVE_SOURCES
    src/GameLogic/AI/AIStateMachine.cpp
    src/GameLogic/AI/AIGroup.cpp
    src/GameLogic/AI/AICommands.cpp
    src/GameLogic/AI/AIWorld.cpp
    src/GameLogic/Module/AIUpdate.cpp
    src/GameLogic/Module/HordeAIUpdate.cpp
    src/GameLogic/Object/Contain/HordeMemberPass.cpp
)
# simulation sources (sim audit manifest): the AI, the horde member pass and the move commands enter the lockstep state
list(APPEND OPENBFME_SIM_SOURCES ${OPENBFME_MOVE_SOURCES})
set(OPENBFME_MOVE_TESTS
    tests/test_move_ai.cpp
    tests/test_move_horde.cpp
    tests/test_move_commands.cpp
    tests/test_move_hash.cpp
    tests/test_move_hash_fields.cpp
    tests/test_move_retail.cpp
)

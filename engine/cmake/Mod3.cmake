# Lane MODULES-3 (the emotion AI states, the safe path, the horde's emotion slots, the crush warning): sources and tests of the openbfme_core library and the test
# executable. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): the states, the horde records and the crush warning are logic state that enters the lockstep hash
set(OPENBFME_MOD3_SOURCES
    src/GameLogic/AI/AIPathfindSafe.cpp
    src/GameLogic/AI/AIMoveSafe.cpp
    src/GameLogic/AI/AIEmotionStates.cpp
    src/GameLogic/Module/AIUpdateEmotion.cpp
    src/GameLogic/Object/Contain/HordeEmotion.cpp
    src/GameLogic/Module/NotifyCrushModules.cpp
)
list(APPEND OPENBFME_SIM_SOURCES ${OPENBFME_MOD3_SOURCES})
set(OPENBFME_MOD3_TESTS
    tests/test_emotion_ai.cpp
)

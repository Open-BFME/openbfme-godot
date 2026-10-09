# AI-1 (the skirmish AI player: its data, the manager and the computer players' brains): sources and tests for the openbfme_core library.
# Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list. Every source here is simulation code (the audit manifest).

set(OPENBFME_SKIRMISH_AI_SOURCES
    src/GameLogic/SkirmishAI/SkirmishAIData.cpp
    src/GameLogic/SkirmishAI/SkirmishAIManager.cpp
    src/GameLogic/SkirmishAI/AIBaseBuilder.cpp
    src/GameLogic/SkirmishAI/AITacticalAI.cpp
    src/GameLogic/SkirmishAI/AIThreatFinder.cpp)

set(OPENBFME_SKIRMISH_AI_TESTS
    tests/test_skirmish_ai_data.cpp
    tests/test_skirmish_ai_manager.cpp
    tests/test_skirmish_ai_base.cpp
    tests/test_skirmish_ai_attack.cpp
    tests/test_skirmish_ai_vs_ai.cpp
    tests/test_skirmish_ai_u22.cpp
    tests/test_horde_cavalry_retail.cpp)

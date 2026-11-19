# DECOMPFIX-1109 (retail corrections from the 2026-11-09 decomp re-diff: FLEECHASE-1, ENTERADJUST-1, REQTHROTTLE-1): tests. Included from engine/CMakeLists.txt so
# the lane's file lists stay out of the shared list. The code changes live in existing simulation sources (HordeAIUpdateCombat.cpp, AIAttackMelee.cpp,
# AIGarrisonStates.cpp, AIMove.cpp, AIMoveSafe.cpp), already in the audit manifest.

set(OPENBFME_DECOMPFIX1109_TESTS
    tests/test_fleechase1.cpp
    tests/test_enteradjust1.cpp
    tests/test_reqthrottle1.cpp)

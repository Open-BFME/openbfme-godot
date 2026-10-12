# Lane AIPOW-1 (stop S-1421: computer players use all their powers): sources and tests of the openbfme_core library and the test executable. Included from
# engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): the decisions and the ActionManager tests run in the lockstep simulation
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/Module/AISpecialPowerDecisions.cpp
    src/GameLogic/ActionManager.cpp
)
set(OPENBFME_AIPOW1_TESTS
    tests/test_aipow1.cpp
)

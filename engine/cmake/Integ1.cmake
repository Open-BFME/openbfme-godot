# Lane INTEG-1 (the seams between HORDE-2, XP-1 and SPELL-1): sources and tests of the openbfme_core library and the test executable. Included from
# engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): StancesBehavior and the StanceTemplate store (the stances' ModifierLists and melee behaviours enter the state)
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/Module/StancesBehavior.cpp
)
set(OPENBFME_INTEG1_TESTS
    tests/test_integ1_stances.cpp
)

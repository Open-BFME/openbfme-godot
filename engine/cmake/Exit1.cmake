# Lane EXIT-1 (QA-2 findings 1 and 2: produced members stuck at their barracks' exit, idle units keeping MOVING): sources and tests of the openbfme_core library
# and the test executable. The sources it changes otherwise belong to the HORDE / AI / locomotor lists. Included from engine/CMakeLists.txt so the lane's file
# lists stay out of the shared list.

# simulation sources (sim audit manifest): the horde member update of AIUpdateInterface (RW 0x66C748)
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/Module/AIUpdateHordeMember.cpp
)
set(OPENBFME_EXIT1_TESTS
    tests/test_exit1_motion.cpp
    tests/test_idle1_turns.cpp # lane IDLE-1 r2: turning members end the frame without MOVING (building attack, cavalry melee)
)

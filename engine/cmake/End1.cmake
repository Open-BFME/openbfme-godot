# Lanes END-1 / END-2 (the end of a skirmish game: the ScoreKeeper statistics, the end sequence, the score screen TimeLine.apt, the way back to the menu): sources and tests
# of the openbfme_core library and the test executable. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): the score keeper is player state that enters the lockstep hash
list(APPEND OPENBFME_SIM_SOURCES
    src/Common/ScoreKeeper.cpp
)
# client-only sources (excluded from the simulation audit in tools/sim/sim_policy.json)
set(OPENBFME_END1_SOURCES
    src/GameClient/EndGame.cpp
    src/GameClient/GUI/AptScreens/AptTimeLine.cpp
    src/GameClient/GUI/AptScreens/AptQuitMenu.cpp
)
set(OPENBFME_END1_TESTS
    tests/test_end1.cpp
    tests/test_end2.cpp
)

# Lane BUGS-1 (backlog bugs: the production exits of every faction, the AI's hero requests): tests. Included from engine/CMakeLists.txt so the lane's file
# lists stay out of the shared list.
set(OPENBFME_BUGS1_TESTS
    tests/test_bugs1_exit.cpp
    tests/test_bugs1_hero.cpp
    tests/test_bugs1_help.cpp
    tests/test_bugs1_apt_seek.cpp
)

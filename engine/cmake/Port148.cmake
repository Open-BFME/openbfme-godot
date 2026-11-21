# PORT-148 (fork issue 365): the GIANT_BIRD flyer's ordered attack on a structure; tests for openbfme_tests.
# The engine change under test is AerialPathfinder::separation's 3D would-collide filter, shared by every GIANT_BIRD flyer;
# the attack / weapon-choice path and CastleBehavior are not given a second handler.

set(OPENBFME_PORT148_TESTS
    tests/test_port_148.cpp)
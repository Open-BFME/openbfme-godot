# PORT-22 (issue #78, the siege deploy's approach offset — Pathfinder::adjustMeleeOffset RW 0x6EA0EC, called by the approach point RW 0x8C9D06 at RW 0x8C9E7A):
# tests for the openbfme_tests executable. The engine change is one pathfinder function in src/GameLogic/AI/AIPathfindMelee.cpp, which the melee engagement
# (RW 0x6F3905) keeps calling, and the call in src/GameLogic/Module/SiegeModules.cpp (computeApproachPoint); both files are already in other lists.

set(OPENBFME_PORT22_TESTS
    tests/test_port_22.cpp)

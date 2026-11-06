# PLAY-2 (the force-attack commands, the Options Advanced page): tests for the openbfme_tests executable. The lane's engine changes are in files other
# lists already carry (AICommands, AIGroup, AIUpdateCombat, AIAttack).

set(OPENBFME_PLAY2_TESTS
    tests/test_force_attack_play2.cpp
    tests/test_force_attack_play2_retail.cpp
    tests/test_atkreset1.cpp # lane ATKRESET-1: the attack commands end the FiringDuration lock
    tests/test_atkstate1.cpp) # lane ATKSTATE-1a: the attack model conditions and failure gates of AIAttackState::update

# client only (the Options screen's advanced page): listed in tools/sim/sim_policy.json's exclusions
set(OPENBFME_PLAY2_SOURCES
    src/GameClient/GameLODManager.cpp)

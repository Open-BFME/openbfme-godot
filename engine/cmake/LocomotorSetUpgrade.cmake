# LocomotorSetUpgrade (MODGAP-2): switches AI normal locomotor set on upgrade grant/removal (RW 0x8B80D3 / 0x8B80F6).

list(APPEND OPENBFME_SIM_SOURCES src/GameLogic/Module/LocomotorSetUpgrade.cpp)

set(OPENBFME_LOCOMOTOR_SET_UPGRADE_TESTS
    tests/test_modgap2_locomotor_upgrade.cpp)

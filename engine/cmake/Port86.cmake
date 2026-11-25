# PORT-86 (Unit voice: VoiceDesperateAttack, VoiceRapidFire, VoiceCaptureBuildingComplete, messages 0x7DB .. 0x7DD):
# synthetic tests for the openbfme_tests executable. The engine changes live in files other lanes already carry
# (UnitVoiceResponse, AIEmotionStates, AIUpdateEmotion, WeaponState, ObjectWeapons, SpecialAbilityModules).

set(OPENBFME_PORT86_TESTS
    tests/test_port_86.cpp)

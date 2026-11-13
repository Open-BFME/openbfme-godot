# PORT-46: ToggleMountedSpecialAbilityUpdate / OpacityTarget - the drawable fade while mounting and dismounting
# (RW 0x8B1142 writes the PC24 curve to drawable + 0xB0, RW 0x8B1219 / 0x8B125F reset it; the twins RW 0x8B44F0 / 0x8B4545
# of SpecialDisguiseUpdate and the resets RW 0x8B2036 / 0x8B208D of SummonReplacementSpecialAbilityUpdate deliver through
# the same ObjectClientHooks::setDrawableOpacity)
set(OPENBFME_PORT46_TESTS
    tests/test_port_46.cpp)

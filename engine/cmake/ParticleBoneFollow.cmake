# ParticleBoneFollow: ParticleSysBone systems follow the animated bones (client / draw only)
target_sources(openbfme_core PRIVATE
    src/GameClient/ParticleBoneFollow.cpp
)
set(OPENBFME_PARTICLEBONEFOLLOW_TESTS
    tests/test_particle_bone_follow.cpp
)

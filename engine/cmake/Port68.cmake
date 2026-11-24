# PORT-68 (Weapon/FXTrigger): synthetic-INI tests for openbfme_tests.
# RW 0x692223 (`ObjectKnockback::apply` maps the projectile name through RW 0xDA08FC and stores it via RW 0x68B56A),
# RW 0x6968BC (the shockwave handler clears Drawable + 0x360 at RW 0x6968D7 and writes DamageInfo + 0x2C at RW 0x696E39)
# and RW 0x4C662D (the ParticleSysBone FXTrigger gate of RW 0x4C6514).
set(OPENBFME_PORT68_TESTS
    tests/test_port_68.cpp)

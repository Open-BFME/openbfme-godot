# PORT-198: the spawn interface's attack / idle orders (RW 0xC58D94 slots 3 .. 6, 8, 10) and
# Player::onUnitCreated (RW 0x6AA688 -> RW 0x6C77C9 -> RW 0x8F06FF). Synthetic-INI tests only.

set(OPENBFME_PORT198_TESTS
    tests/test_port_198.cpp)

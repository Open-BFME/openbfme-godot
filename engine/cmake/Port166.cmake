# PORT-166 (fork issue 395, P-64 "Erkenbrand's Time to Strike: the cooldown starts on the order, no charge, slow afterwards"): the ordered
# LOCKED_TEMPORARILY slot lock and fireCurrentWeapon's shot (RW 0x6920AD .. 0x69212B) - the release is gated only by privateFireWeapon's reloaded
# answer (RW 0x692122), the ZH-era `|| IdleAfterFiringDelay == -1` clause released the lock after every shot of a clipless weapon and the next
# chooseWeapon flipped the slot into the "locked weapon changed" gate (RW 0x751482). Synthetic-INI tests for openbfme_tests, no game files.
set(OPENBFME_PORT166_TESTS
    tests/test_port_166.cpp)

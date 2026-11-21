# PORT-95 (issue fork issue 243): KeepObjectDie/StayOnRadar — the remains of a destroyed object stay on the minimap. The reader in RotWK: KeepObjectDie::onDie
# RW 0x889FA7 adds the object to the radar (Radar::addObject RW 0x6D9042 at RW 0x88A01A) when StayOnRadar (data + 0x3C, row RW 0x42E558 of the table RW 0xC06C2C)
# is set and the object is not on it yet (Object + 0x268, RW 0x88A00A); Object::onDie RW 0x698F80 removes the record before the die modules run. Tests on a
# synthetic INI: no game files.
set(OPENBFME_PORT95_TESTS
    tests/test_port_95.cpp)
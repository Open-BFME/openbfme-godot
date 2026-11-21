# PORT-114 (issue fork issue 298): GameData's global WeaponBonus set — the row RW 0xC004D0 (parser RW 0x6CB559, GlobalData +0xAD0) that builds it and the object side
# that adds it (RW 0x6CA7AC -> 0x6CA7CF load, 0x6CA4B5 / 0x6CA434 add). The engine changes are in files other lanes already build (GameLogicSettings,
# ObjectWeapons); this lane adds only its tests.
set(OPENBFME_PORT114_TESTS
    tests/test_port_114.cpp)
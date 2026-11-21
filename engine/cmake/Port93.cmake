# PORT-93: ArrowStormUpdate's ActiveLoopSound / SpecialAbilityUpdate's active loop (issue fork issue 241) — the loop starts at the ability's
# trigger (RW 0x854008 .. 0x85405B, handle at module + 0x38) and stops in finishAbility (RW 0x85311B .. 0x853131). The engine changes
# are in files other lanes already build (SpecialAbilityModules, AudioEntryPoints); this lane adds only its tests.

set(OPENBFME_PORT93_TESTS
    tests/test_port_93.cpp
)
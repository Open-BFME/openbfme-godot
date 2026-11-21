# PORT-117 (issue fork issue 301, owner report P-57): RotWK's build assistant completes a KindOf HORDE object WITHOUT its producer (buildObjectNow RW 0x797796:
# RW 0x7979D4 .. 0x7979E8 clears it, RW 0x7979EF runs Object::onBuildComplete, RW 0x7979F4 .. 0x797A02 gives it back), so the horde the Thrall Master
# summons (RW 0x8B2302) stands with its whole InitialPayload at once (HordeContain makes none for an object with a producer, RW 0x871BAB). The fix is
# in src/GameLogic/Construction.cpp, which the build already compiles: this lane adds only its tests (PORT117_RW_ANALYSIS.md).

set(OPENBFME_PORT117_TESTS
    tests/test_port_117.cpp
)
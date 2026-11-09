# PORT-38 (issue #123, the AIData team build tempo: TeamSeconds row RW 0xC1CB30 + 0x8, Wealthy RW 0xC1CB40 + 0xC, Poor RW 0xC1CB50 + 0x10,
# TeamsWealthyRate RW 0xC1CB80 + 0x1C, TeamsPoorRate RW 0xC1CBA0 + 0x24, TeamResourcesToStart RW 0xC1CBB0 + 0x28; the readers RW 0x8F7FF4 .. 0x8F8001,
# RW 0x8F8219 .. 0x8F825E and RW 0x8F55BF .. 0x8F55E3): tests for the openbfme_tests executable. The lane's engine changes live in files other lists
# already carry (GameLogicSettings.cpp, GameLogic.h) plus the lane's own source src/GameLogic/SkirmishAI/AITeamTempo.cpp, listed in
# cmake/SkirmishAI.cmake.

set(OPENBFME_PORT38_TESTS
    tests/test_port_38.cpp)

# PORT-194 (fork issue 461): the crowd response of the unit voice -- RotWK's CrowdResponse block parser (RW 0x826674, field table VA 0xC5184C,
# Threshold record RW 0x8264D2 / 0x825B52) and the threshold rule (RW 0x825C45 with the objects table's element count handed over at RW 0x8DEFF4). The
# lane's engine changes are in a file another list already carries (GameClient/UnitVoiceResponse.cpp).

set(OPENBFME_PORT194_TESTS
    tests/test_port_194.cpp)

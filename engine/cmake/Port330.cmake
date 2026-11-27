# PORT-330 (fork issue 847): the FXList block's parse-time name checks against a world's TheAudio / TheEva tables (stop S-190).
# The engine change is in files other lists already carry (GameClient/FXList.cpp, GameClient/FXList.h, GameClient/FXListParseServices.cpp,
# GameLogic/Object/RetailObjectWorld.cpp/.h).
set(OPENBFME_PORT330_TESTS
    tests/test_port_330.cpp)

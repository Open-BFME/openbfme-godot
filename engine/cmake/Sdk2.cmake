# Lane SDK-2 (the modding SDK, piece 2: Luau mod scripts, GameLogic/Sdk/LuauScriptState.h): sources and tests of the openbfme_core library
# and the test executable. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.
# the script state runs sim role scripts in lockstep: a simulation source (the audit scans it)
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/Sdk/LuauScriptState.cpp
    src/GameLogic/Sdk/LuauSimMath.cpp
    src/GameLogic/Sdk/SdkManifest.cpp
    src/GameLogic/Sdk/SdkScriptHost.cpp
)
set(OPENBFME_SDK2_SOURCES)
set(OPENBFME_SDK2_TESTS
    tests/test_sdk2_luau.cpp
    tests/test_sdk2_game.cpp
    tests/test_sdk2_net_retail.cpp
    tests/test_sdk2b_retail.cpp
)

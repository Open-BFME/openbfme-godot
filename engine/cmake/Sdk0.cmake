# Lane SDK-0 (the modding SDK, piece 1: cosmetic asset packs, GameClient/AssetPacks.h): sources and tests of the openbfme_core library and the
# test executable. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# client-only sources (excluded from the simulation audit in tools/sim/sim_policy.json: drawing assets, never simulation state)
set(OPENBFME_SDK0_SOURCES
    src/GameClient/AssetPacks.cpp
)
set(OPENBFME_SDK0_TESTS
    tests/test_sdk0_packs.cpp
    tests/test_mods3_engine.cpp
)

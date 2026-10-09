# Lane HUD-2 (the radar's terrain picture as W3DRadar builds it, the Palantir's globe, portrait and power-cap state): sources and tests of the openbfme_core
# library and the test executable. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

set(OPENBFME_HUD2_SOURCES
    src/GameClient/PalantirCommandUI.cpp
)
set(OPENBFME_HUD2_TESTS
    tests/test_hud2_palantir.cpp
    tests/test_hud2_radar.cpp
    tests/test_hud3_palantir_layout.cpp
)

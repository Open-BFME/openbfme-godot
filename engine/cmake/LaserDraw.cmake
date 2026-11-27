# LaserDraw: W3DLaserDraw module data and beam geometry, the LaserUpdate client update (client / draw only)
target_sources(openbfme_core PRIVATE
    src/GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DLaserDraw.cpp
    src/GameClient/LaserUpdate.cpp
)
set(OPENBFME_LASERDRAW_TESTS
    tests/test_w3d_laser_draw.cpp
    tests/test_laser_update.cpp
    tests/test_w3d_laser_draw_clamps.cpp
)

# W3DQuadrupedDraw: the four foot locations of the huge four-legged beasts (client / draw only)
target_sources(openbfme_core PRIVATE
    src/GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DQuadrupedDraw.cpp
)
set(OPENBFME_W3DQUADRUPEDDRAW_TESTS
    tests/test_w3d_quadruped_draw.cpp
)

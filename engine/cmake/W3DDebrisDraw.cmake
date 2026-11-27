# W3DDebrisDraw: the flying debris piece's draw module (client / draw only)
target_sources(openbfme_core PRIVATE
    src/GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDebrisDraw.cpp
)
set(OPENBFME_W3DDEBRISDRAW_TESTS
    tests/test_w3d_debris_draw.cpp
)

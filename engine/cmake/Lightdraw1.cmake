# Lane LIGHTDRAW-1 (W3DLightDraw: the dynamic light a drawable holds, the torches of the castle walls, fires and spells): sources and tests.
# Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list. Client / draw code only: the source sits under
# src/GameEngineDevice/W3DDevice/ (excluded from the simulation audit by tools/sim/sim_policy.json) and never touches simulation state.
set(OPENBFME_LIGHTDRAW1_SOURCES
    src/GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DLightDraw.cpp
)
set(OPENBFME_LIGHTDRAW1_TESTS
    tests/test_lightdraw1.cpp
)

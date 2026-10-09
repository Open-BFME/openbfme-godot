# RENDER-1 (units and buildings that look like retail: the light environment of the retail effects, the streak draw): sources and
# tests for the openbfme_core library. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.
# Client / draw code only: the sources sit under src/GameEngineDevice/W3DDevice/ (excluded from the simulation audit by
# tools/sim/sim_policy.json) and never touch simulation state.

set(OPENBFME_RENDER_SOURCES
    src/GameEngineDevice/W3DDevice/GameClient/W3DObjectLighting.cpp
    src/GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DStreakDraw.cpp)

set(OPENBFME_RENDER_TESTS
    tests/test_render_lighting.cpp
    tests/test_render_streak.cpp)

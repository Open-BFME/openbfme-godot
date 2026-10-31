# Lane GLOW-1 (retail's glow pass and the effects-LOD gates on the post chain): client / draw code only. Included from engine/CMakeLists.txt so the
# lane's file lists stay out of the shared list. The core sources sit under src/GameEngineDevice/W3DDevice/ (excluded from the simulation audit by
# tools/sim/sim_policy.json); the GDExtension source is listed there with its reason.
set(OPENBFME_GLOW1_SOURCES
    src/GameEngineDevice/W3DDevice/GameClient/GlowEffect.cpp
    src/GameEngineDevice/W3DDevice/GameClient/ScreenHilightFilter.cpp
    src/GameEngineDevice/W3DDevice/GameClient/W3DViewDrawPostPass.cpp
)
set(OPENBFME_GLOW1_TESTS
    tests/test_glow1.cpp
)
set(OPENBFME_GLOW1_GODOT_SOURCES
    src/GodotDevice/GodotGlowEffect.cpp
)

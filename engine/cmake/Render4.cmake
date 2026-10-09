# Lane RENDER-4 (the map's hardware fog on models and particles, the map's colour grade): client / draw code only. Included from engine/CMakeLists.txt
# so the lane's file lists stay out of the shared list. The core sources sit under src/GameEngineDevice/W3DDevice/ (excluded from the simulation audit
# by tools/sim/sim_policy.json); the GDExtension source is listed there with its reason.
set(OPENBFME_RENDER4_SOURCES
    src/GameEngineDevice/W3DDevice/GameClient/W3DHardwareFog.cpp
    src/GameEngineDevice/W3DDevice/GameClient/W3DLookupTablePostEffect.cpp
)
set(OPENBFME_RENDER4_TESTS
    tests/test_render4.cpp
)
set(OPENBFME_RENDER4_GODOT_SOURCES
    src/GodotDevice/GodotPostEffects.cpp
)

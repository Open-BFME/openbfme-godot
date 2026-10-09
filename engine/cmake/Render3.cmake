# Lane RENDER-3 (the construction look of structures: build-up selection, frame drive, interpolation): tests of the test executable. Included from
# engine/CMakeLists.txt so the lane's file lists stay out of the shared list.
set(OPENBFME_RENDER3_TESTS
    tests/test_render3_construction.cpp
)
# GDExtension sources (client rendering only, excluded from the simulation audit in tools/sim/sim_policy.json): the gamma-space transparent pass (S-831)
set(OPENBFME_RENDER3_GODOT_SOURCES
    src/GodotDevice/GodotGammaComposite.cpp
)

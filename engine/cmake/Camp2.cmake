# Lane CAMP-2 (the campaign as a player sees it: the movies' picture): sources and tests of the openbfme_core library, the GDExtension and the test
# executable. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# client-only sources (excluded from the simulation audit in tools/sim/sim_policy.json)
set(OPENBFME_CAMP2_SOURCES
    src/GameClient/VP6Decoder.cpp
)
# the GDExtension's movie stream (VP6MovieStream)
set(OPENBFME_CAMP2_GODOT_SOURCES
    src/GodotDevice/GodotVideoStream.cpp
)
set(OPENBFME_CAMP2_TESTS
    tests/test_camp2_vp6.cpp
    tests/test_camp2_shell.cpp
)

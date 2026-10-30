# Lane OCCLUDE-1 (the behind-building markers: units hidden behind a building drawn as a silhouette in their player's colour, RotWK RW 0x470176 / 0x470B83):
# sources and tests of the openbfme_core library and the test executable. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# client-only sources (excluded from the simulation audit in tools/sim/sim_policy.json): which drawables are occluders / occludees, their colours
set(OPENBFME_OCCLUDE1_SOURCES
    src/GameClient/RTS3DSceneOcclusion.cpp
)
set(OPENBFME_OCCLUDE1_TESTS
    tests/test_occlude1.cpp
)

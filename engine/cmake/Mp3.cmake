# Lane MP-3 (multiplayer on real networks: the network test harness NET-4, soaks, smoothness, players leaving): sources and tests of the openbfme_core
# library and the test executable. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# client / host-only sources (excluded from the simulation audit in tools/sim/sim_policy.json): the transport's fault injector
set(OPENBFME_MP3_CLIENT_SOURCES
    src/GameNetwork/NetImpairment.cpp
    src/GameClient/FrameCensus.cpp
)
set(OPENBFME_MP3_TESTS
    tests/test_mp3_impairment.cpp
    tests/test_mp3_net_retail.cpp
)

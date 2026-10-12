# Lane DECOMP-2 (open stops closed with the RotWK -> BFME2 -> Open-BFME-2 map): the lane's own tests. The sources it changes belong to the AI / object / castle lists.
# Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.
# client presentation (listed in tools/sim/sim_policy.json's exclusions): the sound selector ClientBehavior modules (S-701)
set(OPENBFME_DECOMP2_CLIENT_SOURCES
    src/GameClient/SoundSelectorClientBehavior.cpp
)
set(OPENBFME_DECOMP2_TESTS
    tests/test_decomp2.cpp
)

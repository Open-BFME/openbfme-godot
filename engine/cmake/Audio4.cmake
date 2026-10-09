# Lane AUDIO-4 (QA-1 U10, U20, U21, LargeGroupAudioUpdate, footsteps): the LargeGroupAudioUpdate module (simulation source) and the tests of the lane. Included from engine/CMakeLists.txt so the lane's file lists
# stay out of the shared list.
set(OPENBFME_AUDIO4_SOURCES
    src/GameLogic/LargeGroupAudioLink.cpp
    src/GameLogic/Module/LargeGroupAudioUpdate.cpp
)
list(APPEND OPENBFME_SIM_SOURCES ${OPENBFME_AUDIO4_SOURCES})
# client presentation (listed in tools/sim/sim_policy.json's exclusions)
set(OPENBFME_AUDIO4_CLIENT_SOURCES
    src/GameClient/AnimationSoundClientBehavior.cpp
)
set(OPENBFME_AUDIO4_TESTS
    tests/test_audio4.cpp
)

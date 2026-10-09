# Lane AUDIO-3 (sounds that play when they should): the UnitSpecificSounds lookup the logic uses (simulation source) and the tests of the request log, the
# building sounds and the diagnostic skirmish. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.
set(OPENBFME_AUDIO3_SOURCES
    src/GameLogic/UnitSpecificSound.cpp
)
list(APPEND OPENBFME_SIM_SOURCES ${OPENBFME_AUDIO3_SOURCES})
set(OPENBFME_AUDIO3_TESTS
    tests/test_audio3.cpp
)

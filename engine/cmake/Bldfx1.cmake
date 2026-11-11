# Lane FEAT-BLDFX-1 (building damage effects and the FXList nugget sinks): client sources and tests for openbfme_core / openbfme_tests.
# Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

set(OPENBFME_BLDFX1_SOURCES
    src/GameClient/ClientEffects.cpp
    src/GameClient/CameraShaker.cpp
)

set(OPENBFME_BLDFX1_TESTS
    tests/test_bldfx1_sinks.cpp
)

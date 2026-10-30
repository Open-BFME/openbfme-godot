# Lane CAMANIM-1 (MOVE_CAMERA_BY_ANIMATION: the map's CameraAnimationList played by the tactical view): sources and tests. Included from
# engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# client-only sources: the camera animation tracks, interpolation and the view's playback (never read by the logic)
set(OPENBFME_CAMANIM1_SOURCES
    src/GameClient/CameraAnimation.cpp
)
set(OPENBFME_CAMANIM1_TESTS
    tests/test_camanim1.cpp
)

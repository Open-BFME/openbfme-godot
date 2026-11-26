# SegLineGeometry: the camera-space ribbon of WW3D's SegmentedLineClass (client / draw only)
target_sources(openbfme_core PRIVATE
    src/GameClient/SegLineGeometry.cpp
)
set(OPENBFME_SEGLINEGEOMETRY_TESTS
    tests/test_seg_line_geometry.cpp
)

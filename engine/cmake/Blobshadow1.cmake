# Lane BLOBSHADOW-1: retail's SHADOW_DECAL blob shadows (ShadowLOD Low and Medium): the data (GameClient/BlobShadow.cpp), the device layer (a
# top-down accumulation viewport the terrain multiplies) and the GameWorld pass. Included from engine/CMakeLists.txt so the lane's file lists stay out
# of the shared list. Client only (sim audit: its three sources are excluded by name in tools/sim/sim_policy.json as presentation).
set(OPENBFME_BLOBSHADOW1_CLIENT_SOURCES
    src/GameClient/BlobShadow.cpp
)
set(OPENBFME_BLOBSHADOW1_DEVICE_SOURCES
    src/GodotDevice/GodotBlobShadows.cpp
    src/GodotDevice/GodotGameWorldBlobs.cpp
)
set(OPENBFME_BLOBSHADOW1_TESTS
    tests/test_blobshadow1.cpp
)

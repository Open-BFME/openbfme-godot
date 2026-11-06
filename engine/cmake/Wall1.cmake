# Lane WALL-1 (walkable walls: the wall tops and ramps of Helm's Deep, the castle and fortress walls in the pathfinder): sources and tests. Included from
# engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): the wall surfaces enter the pathfinder's grid and the units' heights, so the mesh reading is simulation code too
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/AI/AIPathfindWalls.cpp
    src/GameClient/DrawableWallMeshes.cpp
)
set(OPENBFME_WALL1_TESTS
    tests/test_wall1_walls.cpp
    tests/test_wall1_battle.cpp
    tests/test_wallfoot1_footprints.cpp # lane WALLFOOT-1: footprints stamp only ground-layer cells; the gate's keeping removal
    tests/test_wallhorde1_layers.cpp # lane WALLHORDE-1: a horde's member slots on walls (RW 0x6EA5B2's layer rule and line test)
)

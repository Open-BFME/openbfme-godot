# Lane WALL-2 (castle and fortress walls: the pathfinder's portals, the portal modules, raised walkways, life on the walls): sources and tests. Included from
# engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest)
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/AI/AIPathfindPortals.cpp
    src/GameLogic/AI/AIPathfindRaised.cpp
    src/GameLogic/AI/AIPathfindSnapshotWalls.cpp
    src/GameLogic/Module/PortalModules.cpp
)
set(OPENBFME_WALL2_TESTS
    tests/test_wall2_portals.cpp
    tests/test_wall2_modules.cpp
    tests/test_wall2_raised.cpp
    tests/test_wall2_fortress.cpp
    tests/test_walls_snapshot.cpp
)

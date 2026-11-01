# Lane TREES1 (the map's trees: units crush them, construction clears them, the harvest check and the tree buffer's sink): the tests of the test
# executable. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list. The lane's code lives in sources other lanes own
# (TerrainTrees, Object, DozerAIUpdate, AIHarvest, TreeTopple), already in their lists.
set(OPENBFME_TREES1_TESTS
    tests/test_trees1.cpp
)

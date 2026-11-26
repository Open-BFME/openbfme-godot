# TerrainScorches: the terrain's scorch buffer (addScorch, updateScorches, vertex colour; client / draw only)
target_sources(openbfme_core PRIVATE
    src/GameEngineDevice/W3DDevice/GameClient/TerrainScorches.cpp
)
set(OPENBFME_TERRAINSCORCHES_TESTS
    tests/test_terrain_scorches.cpp
)

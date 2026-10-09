# Lane START-1 (start a skirmish from the real menu: the new-game consumer in the logic, the world-backed lobby source, the load screen model):
# sources and tests of the openbfme_core library and the test executable. Included from engine/CMakeLists.txt so the lane's lists stay out of
# the shared list.

# the new-game consumer: its results (players, sides, random starts / factions / colours, starting bases) enter the lockstep state, and the world-backed
# lobby source builds the GameInfo the logic consumes; both are simulation sources (sim audit manifest)
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/NewGame/SkirmishRandom.cpp
    src/GameLogic/NewGame/SkirmishSides.cpp
    src/GameLogic/NewGame/StartingBase.cpp
    src/GameLogic/NewGame/NewGame.cpp
    src/GameClient/GUI/Skirmish/WorldSkirmishSetupSource.cpp
)
# the load screen model (client only: player cards and the progress bar)
set(OPENBFME_START_SOURCES
    src/GameClient/GUI/LoadScreenInfo.cpp
)
set(OPENBFME_START_TESTS
    tests/StartTestUtil.cpp
    tests/test_start_random.cpp
    tests/test_start_retail.cpp
)
# the Godot device classes of the lane (added to the openbfme extension)
set(OPENBFME_START_GODOT_SOURCES
    src/GodotDevice/GodotTiming.cpp
)

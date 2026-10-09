# Lane GARRISON-1 (garrisons: GarrisonContain / HordeGarrisonContain, the AI's enter / exit states, the garrison messages): sources and tests of the openbfme_core
# library and the test executable. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): the contain modules, the AI states and the dispatcher cases are lockstep state
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/Object/Contain/GarrisonContainData.cpp
    src/GameLogic/Object/Contain/GarrisonContain.cpp
    src/GameLogic/Object/Contain/HordeGarrisonLink.cpp
    src/GameLogic/AI/AIGarrisonStates.cpp
    src/GameLogic/AI/GarrisonCommands.cpp
)
set(OPENBFME_GARRISON_TESTS
    tests/test_garrison_data.cpp
    tests/test_hud_garrison_retail.cpp
    tests/test_hud_garrison_input.cpp
)

# Lane LUMBER1 (the evil lumber mills: the supply half of WorkerAIUpdate, the harvest and dock AI states, DockUpdate / SupplyCenterDockUpdate,
# SupplyCenterCreate, the players' ResourceGatheringManager): sources and tests of the openbfme_core library and the test executable. Included from
# engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest)
list(APPEND OPENBFME_SIM_SOURCES
    src/Common/ResourceGatheringManager.cpp
    src/GameLogic/AI/AIHarvest.cpp
    src/GameLogic/AI/AIDock.cpp
    src/GameLogic/Module/DockUpdate.cpp
    src/GameLogic/Module/SupplyCenterCreate.cpp
    src/GameLogic/Module/WorkerAIUpdate.cpp
)
set(OPENBFME_LUMBER1_TESTS
    tests/test_lumber1.cpp
)

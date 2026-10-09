# Lane MODULES-2 (the partition manager, the emotion system, the area-scan modules): sources and tests of the openbfme_core library and the test executable.
# Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): ThePartitionManager's trees and the modules here are logic state that enters the lockstep hash
set(OPENBFME_MOD2_SOURCES
    src/GameLogic/Object/PartitionManager.cpp
    src/GameLogic/System/EmotionSystem.cpp
    src/GameLogic/Module/EmotionModules.cpp
    src/GameLogic/Module/AreaScanModules.cpp
    src/GameLogic/Module/HitReactionBehavior.cpp
)
list(APPEND OPENBFME_SIM_SOURCES ${OPENBFME_MOD2_SOURCES})
set(OPENBFME_MOD2_TESTS
    tests/test_partition_manager.cpp
    tests/test_emotion_core.cpp
    tests/test_emotion_retail.cpp
    tests/test_areascan_retail.cpp
    tests/test_partition_bench.cpp
    tests/test_numeric_fastpath.cpp
)

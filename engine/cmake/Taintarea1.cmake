# Lane TAINTAREA-1 (TheTaintManager: the taint / elven wood grid the Taint and Elven Wood spells paint, read by the TAINT / ELVEN_WOOD aura
# condition): sources of the openbfme_core library and tests of the test executable. Included from engine/CMakeLists.txt so the lane's file lists
# stay out of the shared list.

# simulation sources (sim audit manifest)
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/System/TaintManager.cpp
    src/GameLogic/Map/TerrainTaint.cpp
)
set(OPENBFME_TAINTAREA1_TESTS
    tests/test_taintarea1.cpp
)

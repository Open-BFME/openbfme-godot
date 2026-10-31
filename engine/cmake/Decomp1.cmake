# Lane DECOMP-1 (open stops closed with the Open-BFME-2 decompilation's progress, confirmed in RotWK's binary): sources and tests of the openbfme_core library
# and the test executable. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): the DOTManager's records are simulation state that enters the lockstep hash
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/System/DOTManager.cpp
)
set(OPENBFME_DECOMP1_TESTS
    tests/test_hud_decomp1_retail.cpp
    tests/test_hud_nugget1_retail.cpp   # lane NUGGET-1: the weapon nuggets delivered with their retail weapons (S-321)
    tests/test_hud_radiusheight1_retail.cpp # lane RADIUSHEIGHT-1: radius damage's DamageMaxHeight / DamageMaxHeightAboveTerrain (RW 0x90DEF0)
)

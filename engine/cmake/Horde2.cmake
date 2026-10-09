# HORDE-2 (horde combat as in RotWK 2.01: crush / trample, flanking, the horde melee machine, re-forming): sources and tests for the openbfme_core library.
# Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list. Every source here is simulation code (the audit manifest).

set(OPENBFME_HORDE2_SOURCES
    src/GameLogic/Object/Collide/SquishCollide.cpp
    src/GameLogic/Object/Contain/HordeFlank.cpp
    src/GameLogic/Object/Contain/HordeBanner.cpp
    src/GameLogic/Object/Contain/HordeFormation.cpp)

set(OPENBFME_HORDE2_TESTS
    tests/test_horde2_crush.cpp
    tests/test_horde2_banner.cpp
    tests/test_horde2_hash.cpp
    tests/test_hud_horde2_retail.cpp)

# COMBAT-1 (units fight: weapon sets on objects, damage and death, the attack AI, horde combat): sources and tests for the openbfme_core library.
# Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list. Every source here is simulation code (the audit manifest).

set(OPENBFME_COMBAT_SOURCES
    src/GameLogic/Combat/CombatNames.cpp
    src/GameLogic/Combat/CombatQueries.cpp
    src/GameLogic/Combat/CombatLineOfSight.cpp
    src/GameLogic/Combat/CombatState.cpp
    src/GameLogic/Combat/TargetFinder.cpp
    src/GameLogic/AI/AIClosestEnemy.cpp
    src/GameLogic/ScriptEngine/AttackPriority.cpp
    src/GameLogic/Combat/ObjectWeapons.cpp
    src/GameLogic/Combat/WeaponDelivery.cpp
    src/GameLogic/AI/AIAttack.cpp
    src/GameLogic/Module/AIUpdateCombat.cpp
    src/GameLogic/Module/HordeAIUpdateCombat.cpp
    src/GameLogic/Module/CombatModules.cpp
    src/GameLogic/Combat/BezierSegment.cpp
    src/GameLogic/Combat/ProjectileLauncher.cpp
    src/GameLogic/Module/ProjectileModules.cpp)

set(OPENBFME_COMBAT_TESTS
    tests/test_combat_weapons.cpp
    tests/test_combat_damage.cpp
    tests/test_siegefall1.cpp
    tests/test_siegefall1_retail.cpp
    tests/test_startrange1.cpp # lane STARTRANGE-1: CHUNK_VENDOR box distance, the grab's -400 (RW 0x85207A)
    tests/test_locopick1.cpp # lane LOCOPICK-1: the locomotor fallback (RW 0x663D70)
    tests/test_aimaim1.cpp # lane AIMAIM-1: the attack aim (RW 0x75232F)
    tests/test_killsrc1.cpp
    tests/test_invul1.cpp
    tests/test_kindfix1.cpp
    tests/test_combat_ai.cpp
    tests/test_attack_approach_retail.cpp
    tests/test_attack_aim_turn.cpp
    tests/test_target1_acquire.cpp
    tests/test_los1_terrain.cpp
    tests/test_combat_horde.cpp
    tests/test_archer_spread.cpp
    tests/test_archer_spread_retail.cpp
    tests/test_hud_combat_retail.cpp
    tests/test_hud_combat_input.cpp
    tests/test_combat_hash.cpp
    tests/test_proj_arc.cpp
    tests/test_proj_flight.cpp
    tests/test_proj_hash.cpp
    tests/test_hud_proj_retail.cpp)

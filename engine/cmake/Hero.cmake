# Lane HERO-1 (heroes: the recruit / revive list, RespawnUpdate, the hero production entries, hero abilities): sources and tests of the openbfme_core library
# and the test executable. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): every file here enters the lockstep state (the hero list is Player state, the modules are object state)
list(APPEND OPENBFME_SIM_SOURCES
    src/Common/PlayerHeroList.cpp
    src/GameLogic/HeroSystem.cpp
    src/GameLogic/Module/HeroModules.cpp
    src/GameLogic/Module/SpecialAbilityModules.cpp
    src/GameLogic/Module/HeroAbilityModules.cpp
    # HERO-2: Create-a-Hero (the record is loaded into the game state: its upgrades, command sets and the build surcharge)
    src/Common/CreateAHeroRecord.cpp
    src/GameLogic/CreateAHeroSystem.cpp
)
set(OPENBFME_HERO_TESTS
    tests/test_hero_core.cpp
    tests/test_hero_retail.cpp
    tests/test_hero_hud.cpp
    tests/test_hero_expiry.cpp
    tests/test_hero2_core.cpp
    tests/test_create_a_hero.cpp
)

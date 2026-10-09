# SPELL-1 (sciences, ranks and skill points, the spell book, special powers): sources and tests for the openbfme_core library.
# Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list. Every source here is simulation code (the audit manifest).

set(OPENBFME_SPELL_SOURCES
    src/Common/Science.cpp
    src/Common/SpecialPower.cpp
    src/Common/PlayerScience.cpp
    src/GameLogic/RankInfo.cpp
    src/GameLogic/SpellStores.cpp
    src/GameLogic/SpellCommands.cpp
    src/GameLogic/SpellStops.cpp
    src/GameLogic/Module/SpecialPowerModules.cpp
    src/GameLogic/ObjectCreationList.cpp
    # lane SPELL-2
    src/GameLogic/GlobalWeatherSystem.cpp
    src/GameLogic/Module/SpellBookPowers.cpp
    src/GameLogic/Module/SpellEffectModules.cpp)

# lane SPELL-2: the spell book screens' engine side (client: sim_policy.json excludes it)
set(OPENBFME_SPELL_CLIENT_SOURCES
    src/GameClient/SpellBookUI.cpp
    src/GameClient/GUI/AptScreens/AptSpellStore.cpp)

set(OPENBFME_SPELL_TESTS
    tests/test_spell_stores.cpp
    tests/test_spell_player.cpp
    tests/test_spell_retail.cpp
    tests/test_spell_heal.cpp
    tests/test_spell_cast.cpp
    tests/test_spell_book_powers.cpp
    tests/test_spell_book_ui.cpp
    tests/test_spell_book_hud.cpp)

# Lane AUTOCAST-1 (owner, 2026-10-10): the hero ability autocast toggle (right press on the ability button, MSG_DO_AUTO_ABILITY / _WEAPON, the
# scripts' SET_AUTO_ABILITY) and the button's glow. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.
# The lane's code is in files already listed (HeroAbilityModules.cpp, ScriptActionsUnits.cpp, ControlBar*.cpp, AptPalantir.cpp, InGameHud.cpp).
set(OPENBFME_AUTOCAST1_TESTS
    tests/test_autocast1_hud.cpp
    tests/test_autocast1_game.cpp
)

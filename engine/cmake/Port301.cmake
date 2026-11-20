# PORT-301: ShareExperienceBehavior — the hero-experience-sharing switch (TheGameLogic + 0x9F) and the behaviour's snapshot layout. The engine changes are in
# GameLogic/GameLogic.h (the flag), GameLogic/ScriptEngine/ScriptActions.cpp (the script action 538 SET_HERO_EXPERIENCE_SHARING) and
# GameLogic/Module/HeroAbilityModules.cpp (the share routine's reader and the xfer).

set(OPENBFME_PORT301_TESTS
    tests/test_port_301.cpp)

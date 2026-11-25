# ReflectDamage: reflects a share of matching incoming damage back to the attacker (RW 0x8BED43).

list(APPEND OPENBFME_SIM_SOURCES src/GameLogic/Module/ReflectDamage.cpp)

set(OPENBFME_REFLECT_DAMAGE_TESTS
    tests/test_modgap1_reflect.cpp)

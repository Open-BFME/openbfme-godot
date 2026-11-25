# StoreObjectsSpecialPower + TeleportToCasterSpecialPower (MODGAP-2): store and teleport filtered unit lists (RW 0x8D1EA7 / 0x8D1AEB).

list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/Module/StoreObjectsSpecialPower.cpp
    src/GameLogic/Module/TeleportToCasterSpecialPower.cpp)

set(OPENBFME_STORE_TELEPORT_TESTS
    tests/test_modgap2_store_objects.cpp
    tests/test_modgap2_store_teleport.cpp)

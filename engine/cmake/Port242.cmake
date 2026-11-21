# PORT-242 (fork issue 566): SpecialPowerModule::requirementsMet (RW 0x8969E5) selects RequirementsFilterStrategic (+0x3C)
# when GameLogic + 0x114 != 3 and passes filters with +0x88 flag == 0 (NONE, RW 0x762977). Included from engine/CMakeLists.txt.

set(OPENBFME_PORT242_TESTS
    tests/test_port_242.cpp
)
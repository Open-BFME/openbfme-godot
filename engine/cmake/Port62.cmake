# PORT-62 (TransportContain/FireGrabWeaponOnVictim): synthetic tests for the openbfme_tests executable. The engine change is in
# src/GameLogic/Object/Contain/TransportContainRuntime.cpp (lane GARRISON-2, which already carries the file): the GrabWeapon fire of RW 0x86AA4B .. 0x86AAA2
# inside TransportContain::onContaining (RW 0x86A943), and FireGrabWeaponOnVictim's UNATTACKABLE bracket around that one fire (RW 0x86AA62 / RW 0x86AA8F).

set(OPENBFME_PORT62_TESTS
    tests/test_port_62.cpp)

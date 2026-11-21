# PORT-37 (issue fork issue 122, DamageNugget/CylinderAOE): tests for the openbfme_tests executable. The lane's engine changes are in Combat/WeaponDelivery.cpp
# (the area distance type of RW 0x90DEF0 at RW 0x90DF35 .. 0x90DF77) and Object/PartitionManager (withinRange, the query's distance test for an object
# the caller holds).

set(OPENBFME_PORT37_TESTS
    tests/test_port_37.cpp)
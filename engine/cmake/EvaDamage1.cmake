# Lane EVADAMAGE-1 (S-709's damage half): the Eva half of RotWK's drawable damage notice RW 0x67B4B7. Included from engine/CMakeLists.txt so the lane's file
# lists stay out of the shared list.
# the logic half (reads only: the attacker's visibility RW 0x694CCC, the scan)
list(APPEND OPENBFME_SIM_SOURCES src/GameLogic/Object/EvaDamageNotice.cpp)
# client presentation (listed in tools/sim/sim_policy.json's exclusions)
set(OPENBFME_EVADAMAGE1_CLIENT_SOURCES
    src/GameClient/EvaDamage.cpp
)
set(OPENBFME_EVADAMAGE1_TESTS
    tests/test_evadamage1.cpp
)

# Lane BACKLOG2 (the RotWK functions behind the castle, camp and construction stops S-951, S-303, S-950, S-207, S-142, S-653, ported from Open-BFME-2's
# byte-matched source): sources and tests of the openbfme_core library and the test executable. Included from engine/CMakeLists.txt so the lane's file lists
# stay out of the shared list.

# simulation sources (sim audit manifest)
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/ConstructionSite.cpp
    src/GameLogic/CastleCommands.cpp
)
set(OPENBFME_BACKLOG2_TESTS
    tests/test_backlog2.cpp
    tests/test_backlog2_retail.cpp
)

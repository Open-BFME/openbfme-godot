# Lane SHIP-1 (ships and hover units: the SHIP and HOVER locomotor movers, SEA_LEVEL): tests of the test executable. Included from engine/CMakeLists.txt so the
# lane's file lists stay out of the shared list (the movers are in LocomotorMove.cpp, already a simulation source).
set(OPENBFME_SHIP1_TESTS
    tests/test_ship1_ships.cpp
)

# simulation sources (sim audit manifest)
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/AI/AIPathfindShore.cpp
)

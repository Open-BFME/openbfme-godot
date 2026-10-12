# Lane FACT-C (the Goblins / Angmar content, the creeps, lairs, inns and neutral structures of the skirmish maps): tests. Included from engine/CMakeLists.txt so
# the lane's file lists stay out of the shared list.
set(OPENBFME_FACTC_TESTS
    tests/test_factc_sweep.cpp
    tests/test_factc_started.cpp
)

# simulation sources (sim audit manifest): the modules are object state that enters the lockstep hash
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/Module/HordeDispatchSpecialPower.cpp
    src/GameLogic/Module/ObjectCreationUpgrade.cpp
)

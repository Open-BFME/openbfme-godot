# Lane SIEGE-1 (the siege units: rams, catapults and trebuchets, Grond, the siege ladders and towers against walls and gates): sources and tests. Included
# from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): the siege deploy, the Demolisher deploy (DeployStyleAIUpdate), the wall's dock points, the stop power, the siege AI's gate and the face state
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/Module/SiegeModules.cpp
    src/GameLogic/Module/DeployStyleAIUpdate.cpp
    src/GameLogic/ScriptEngine/ScriptActionsSiege.cpp
)
set(OPENBFME_SIEGE1_TESTS
    tests/test_siege1_survey.cpp
    tests/test_siege1_deploy.cpp
    tests/test_siege1_attack.cpp
    tests/test_siege1_demolisher.cpp
)

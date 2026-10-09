# Lane CAMP-1 (the campaigns: TheLinearCampaignManager, the campaign flow, the mission end-to-end tests): sources and tests of the openbfme_core
# library and the test executable. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest)
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/Module/AttachUpdate.cpp
    src/GameLogic/ScriptEngine/ScriptActionsCampaign.cpp
)
# client-only sources (excluded from the simulation audit in tools/sim/sim_policy.json)
set(OPENBFME_CAMP1_SOURCES
    src/GameClient/LinearCampaign.cpp
    src/GameClient/VideoPlayer.cpp
)
# the GDExtension's view of the campaigns (client: the campaign list, a mission as the game, its end, the progress sidecar)
set(OPENBFME_CAMP1_GODOT_SOURCES
    src/GodotDevice/GodotGameWorldCampaign.cpp
)
set(OPENBFME_CAMP1_TESTS
    tests/test_camp1.cpp
    tests/test_camp1_missions.cpp
    tests/test_camp1h.cpp
)

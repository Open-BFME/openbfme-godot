# Lane SCRIPT-1 (the map script engine: the condition / action template registries, the RotWK type re-match, the logic ScriptEngine with its
# conditions and actions): sources and tests of the openbfme_core library and the test executable. Included from engine/CMakeLists.txt so the
# lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): the script engine's counters, flags, timers and script states are logic state (hashed)
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/ScriptEngine/ScriptTemplates.cpp
    src/GameLogic/ScriptEngine/ScriptEngine.cpp
    src/GameLogic/ScriptEngine/ScriptConditions.cpp
    src/GameLogic/ScriptEngine/ScriptActions.cpp
    src/GameLogic/ScriptEngine/ScriptActionsUnits.cpp
    src/GameLogic/AI/AIHunt.cpp
    src/GameLogic/AI/AIWaypointPath.cpp
    src/GameClient/MapScriptSetup.cpp
)
# client-only sources (excluded from the simulation audit in tools/sim/sim_policy.json)
set(OPENBFME_SCRIPT1_SOURCES
    src/GameClient/ScriptCameraDirector.cpp
    src/GameClient/ScriptAudioLength.cpp
)
# the GDExtension's view of the script engine (client: requests, the script camera, the report)
set(OPENBFME_SCRIPT1_GODOT_SOURCES
    src/GodotDevice/GodotGameWorldScripts.cpp
)
set(OPENBFME_SCRIPT1_TESTS
    tests/test_script1.cpp
    tests/test_script1_live.cpp
    tests/test_script2.cpp
    tests/test_script2_angmar.cpp
    tests/test_script2_client.cpp
    tests/test_script3.cpp
    tests/test_script3_mission.cpp
)

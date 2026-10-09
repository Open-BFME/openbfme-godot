# Lane GARRISON-2 (transports and siege carriers: OpenContain's runtime base, TransportContain / HordeTransportContain, SiegeEngineContain /
# HordeSiegeEngineContain, the AI turrets): sources and tests of the openbfme_core library and the test executable. Included from engine/CMakeLists.txt.

# simulation sources (sim audit manifest)
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/Object/Contain/OpenContainRuntime.cpp
    src/GameLogic/Object/Contain/TransportContainData.cpp
    src/GameLogic/Object/Contain/TransportContainRuntime.cpp
    src/GameLogic/Object/Contain/SiegeEngineContainRuntime.cpp
    src/GameLogic/AI/TurretAI.cpp
    src/GameLogic/Object/Contain/TunnelContainRuntime.cpp
)
set(OPENBFME_TRANSPORT_TESTS
    tests/test_hud_transport_retail.cpp
    tests/test_hud_turret_retail.cpp
    tests/test_hud_tunnel_retail.cpp
)

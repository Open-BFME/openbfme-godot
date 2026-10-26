# Lane ONLINEHUB-1 (Quick Match, tournaments and statistics in the online menu; docs/PLATFORM_API.md): sources and tests. Included from
# engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# client sources (excluded from the simulation audit in tools/sim/sim_policy.json): the platform API's client, the local game records and the pages
set(OPENBFME_ONLINEHUB1_CLIENT_SOURCES
    src/GameNetwork/PlatformApiClient.cpp
    src/GameClient/GameRecords.cpp
    src/GameClient/GUI/AptScreens/AptStats.cpp
    src/GameClient/GUI/AptScreens/AptOnlineHub.cpp)
set(OPENBFME_ONLINEHUB1_TESTS
    tests/test_onlinehub1_platform.cpp
    tests/test_onlinehub1_screens.cpp)

# called once openbfme_core and the optional openbfme_tests exist
macro(openbfme_onlinehub1_targets)
    target_sources(openbfme_core PRIVATE ${OPENBFME_ONLINEHUB1_CLIENT_SOURCES})
    if(TARGET openbfme_tests)
        target_sources(openbfme_tests PRIVATE ${OPENBFME_ONLINEHUB1_TESTS})
        get_filename_component(OPENBFME_FAKE_PLATFORM "${CMAKE_CURRENT_SOURCE_DIR}/../tools/platform_fake/fake_server.py" ABSOLUTE)
        target_compile_definitions(openbfme_tests PRIVATE OPENBFME_FAKE_PLATFORM="${OPENBFME_FAKE_PLATFORM}")
    endif()
endmacro()

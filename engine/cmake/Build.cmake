# Lane BUILD-1 (construction, the starting fortress, build plots, dozers, the castle base layouts of Bases.big): sources and tests of the openbfme_core library and the
# test executable. Included from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): placement legality, the construction flow, the command handlers and the construction / castle / dozer modules
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/BuildPlacement.cpp
    src/GameLogic/BuildModeFilter.cpp
    src/GameLogic/FactionBuildPlots.cpp
    src/GameLogic/Construction.cpp
    src/GameLogic/BuildCommands.cpp
    src/GameLogic/BuildStops.cpp
    src/GameLogic/Map/CastleTemplates.cpp
    src/GameLogic/Module/ConstructionModules.cpp
    src/GameLogic/Module/CastleModules.cpp
    src/GameLogic/Module/DozerAIUpdate.cpp
    src/GameLogic/WallSpan.cpp # lane BUILD-2
    src/GameLogic/WallBalance.cpp # optional BFME2Lua wall rules
    src/GameLogic/FindPositionAround.cpp # lane BUILD-3
)
set(OPENBFME_BUILD_TESTS
    tests/test_build_castle.cpp
    tests/test_build_core.cpp
    tests/test_build_construct.cpp
    tests/test_build_hud.cpp
    tests/test_build_rate.cpp # lane BUILD-2
    tests/test_build_heal.cpp # lane BUILD-2
    tests/test_build3_builders.cpp # lane BUILD-3
    tests/test_build4_walls.cpp # lane BUILD-4
    tests/test_build5_placement.cpp # lane BUILD-5
)

# Focused optional-rule checks; a separate target avoids rebuilding unrelated test suites.
add_executable(openbfme_wall_balance_tests tests/test_wall_balance.cpp)
target_include_directories(openbfme_wall_balance_tests PRIVATE tests thirdparty/doctest)
target_link_libraries(openbfme_wall_balance_tests PRIVATE openbfme_core)

add_executable(openbfme_faction_plot_tests tests/test_faction_build_plots.cpp)
target_include_directories(openbfme_faction_plot_tests PRIVATE tests thirdparty/doctest)
target_link_libraries(openbfme_faction_plot_tests PRIVATE openbfme_core)

add_executable(openbfme_build_filter_tests tests/test_build_mode_filter.cpp tests/test_bfme_lua.cpp)
target_include_directories(openbfme_build_filter_tests PRIVATE tests thirdparty/doctest)
target_link_libraries(openbfme_build_filter_tests PRIVATE openbfme_core)

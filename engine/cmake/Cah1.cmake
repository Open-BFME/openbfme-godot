# Lane CAH-1 (the Create-a-Hero screens: the builder CreateAHero.apt with its pages, the saved / system hero list, the lobby's hero choice): sources and
# tests of the openbfme_core library, the GDExtension and the test executable. Included from engine/CMakeLists.txt so the lane's file lists stay out of the
# shared list.

# client-only sources (excluded from the simulation audit in tools/sim/sim_policy.json: menus and files, never simulation state)
set(OPENBFME_CAH1_SOURCES
    src/GameClient/CreateAHeroHeroList.cpp
    src/GameClient/GUI/CreateAHero/AptMyHero.cpp
    src/GameClient/GUI/AptScreens/AptCreateAHero.cpp
    src/GameClient/UserDataFolder.cpp
    src/GameClient/GUI/AptColorPicker.cpp
)
# the GDExtension's side (the builder's 3D view)
set(OPENBFME_CAH1_GODOT_SOURCES
    src/GodotDevice/GodotGameWorldCah.cpp
)
set(OPENBFME_CAH1_TESTS
    tests/test_cah1.cpp
    tests/test_cah2.cpp
)

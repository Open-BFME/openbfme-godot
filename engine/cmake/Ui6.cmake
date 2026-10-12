# Lane UI-6 (a Discord tester's menu findings on v0.3.0-preview.6, and the widescreen view): client-only sources and tests. Included from
# engine/CMakeLists.txt so the lane's file lists stay out of the shared list.
set(OPENBFME_UI6_CLIENT_SOURCES
    src/GameClient/GdiFontMetrics.cpp
    src/GameClient/GUI/Skirmish/SkirmishGameRecord.cpp
)
set(OPENBFME_UI6_GODOT_SOURCES
)
set(OPENBFME_UI6_TESTS
    tests/test_ui6_menus.cpp
)

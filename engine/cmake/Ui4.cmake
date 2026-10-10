# Lane UI-4 (the owner's menu and HUD findings on v0.3.0-preview.1): client-only sources and tests. Included from engine/CMakeLists.txt so
# the lane's file lists stay out of the shared list.
set(OPENBFME_UI4_CLIENT_SOURCES
    src/GameClient/GlobalLanguage.cpp
    src/GameClient/Credits.cpp
)
set(OPENBFME_UI4_GODOT_SOURCES
)
set(OPENBFME_UI4_TESTS
    tests/test_ui4_credits.cpp
    tests/test_ui4_portraits.cpp
)

# Lane HUD-4 (QA-1 U0 / U1 / U11 / U13 / U15 / U18 / U19 and the weapon set toggle): sources and tests. Included from engine/CMakeLists.txt so the
# lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): MSG_WEAPONSET_TOGGLE and Object::setWeaponSetFlag (RW 0x77B529 / 0x691059)
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/WeaponSetToggle.cpp
)
set(OPENBFME_HUD4_TESTS
    tests/test_hud_hud4_toggle.cpp
    tests/test_hud_hud4_commands.cpp
)
# the device side (GDExtension): the packed texture loader the Apt player and the HUD share
set(OPENBFME_HUD4_GODOT_SOURCES
    src/GodotDevice/GodotPackedTexture.cpp
)

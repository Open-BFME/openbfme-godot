# Lane MODULES-1 (the remaining behaviour modules the base game uses): sources and tests of the openbfme_core library and the test executable. Included
# from engine/CMakeLists.txt so the lane's file lists stay out of the shared list.

# simulation sources (sim audit manifest): every module here is object state that enters the lockstep hash
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/Module/VeterancyCrateCollide.cpp
    src/GameLogic/Module/ExtraModules.cpp
    src/GameLogic/Module/ExtraUpgradeModules.cpp
    src/GameLogic/Module/ExtraDieModules.cpp
    src/GameLogic/Module/ExtraUpdateModules.cpp
    src/GameLogic/Module/ExtraCreateModules.cpp
    src/GameLogic/Module/FlammableUpdate.cpp
    src/GameLogic/Module/FireWeaponWhenDamagedBehavior.cpp # lane MODS-3
    src/GameLogic/Module/GeometryUpgrade.cpp # lane MODCOMPAT-MOD1
    src/GameLogic/Module/RebuildHoleModules.cpp # lane MODCOMPAT-MOD1: RebuildHoleExposeDie, RebuildHoleBehavior
    src/GameLogic/Module/SlaveWatcherBehavior.cpp # lane MODCOMPAT-MOD1
    src/GameLogic/Module/CommandButtonHuntUpdate.cpp # lane MODCOMPAT-MOD1
    src/GameLogic/Module/GrabPassengerSpecialPower.cpp # lane MODCOMPAT-MOD1
    src/GameLogic/Module/CritterEmitterUpdate.cpp # lane MODCOMPAT-MOD1
    src/GameLogic/Module/AudioLoopUpgrade.cpp # lane MODCOMPAT-MOD1
    src/GameLogic/Module/ClickReactionBehavior.cpp # lane MODCOMPAT-MOD1
)
set(OPENBFME_MOD1_TESTS
    tests/test_module_coverage.cpp
    tests/test_mod1_retail.cpp
    tests/test_mod1_parse.cpp
    tests/test_fwwd.cpp # lane MODS-3: FireWeaponWhenDamagedBehavior (synthetic and retail data)
    tests/test_geometry_upgrade.cpp # lane MODCOMPAT-MOD1: GeometryUpgrade and the object's own geometry (retail data)
    tests/test_rebuildhole.cpp # lane MODCOMPAT-MOD1: RebuildHoleExposeDie / RebuildHoleBehavior (retail data, binary facts)
    tests/test_slavewatcher.cpp # lane MODCOMPAT-MOD1: SlaveWatcherBehavior and its readers (retail data, binary facts)
    tests/test_cbhunt.cpp # lane MODCOMPAT-MOD1: CommandButtonHuntUpdate and TEAM_HUNT_WITH_COMMAND_BUTTON
    tests/test_grabpassenger.cpp # lane MODCOMPAT-MOD1: GrabPassengerSpecialPower
    tests/test_critter_emitter.cpp # lane MODCOMPAT-MOD1: CritterEmitterUpdate (synthetic and retail data)
    tests/test_audio_loop_upgrade.cpp # lane MODCOMPAT-MOD1: AudioLoopUpgrade (synthetic and retail data)
    tests/test_click_reaction.cpp # lane MODCOMPAT-MOD1: ClickReactionBehavior (synthetic and retail data)
    tests/test_castle_unpack_script.cpp # lane CASTLE-UNPACK: the castle base script commands (NAMED_BASE_UNPACK*, PLAYER_HAS_N_OR_FEWER_BASES)
    tests/test_modcompat_ai_script.cpp # lane MODCOMPAT-AI: the script conditions and actions the mods' AI scripts need
)

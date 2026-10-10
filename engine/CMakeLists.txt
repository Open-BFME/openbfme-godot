cmake_minimum_required(VERSION 3.17)

# OpenBFME engine: game logic as a Godot GDExtension, device layer on Godot.
#   openbfme_core   - engine code with no Godot dependency (SAGE ports); unit-tested
#   openbfme        - the GDExtension (core + GodotDevice), written to godot/bin/
#   openbfme_tests  - doctest unit tests over openbfme_core
project(OpenBFME LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# compile_commands.json for tools/sim/sim_audit.py: set BEFORE any subdirectory, the setting is read when a target is created
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# WINCRASH-1: the llvm-mingw Windows build (tools/release/build_windows.sh) writes CodeView line tables to a PDB next to the GDExtension DLL
# (and its test executables): tools/release/archive_symbols.sh keeps it per version, so a crash report's DLL offsets map to functions and
# lines (llvm-symbolizer). It is not packaged: Godot 4.7's own Windows crash handler cannot read it (stop S-1923): it symbolizes with
# libbacktrace for its executable only (checked on JonathanPC: bare DLL offsets with a PDB and with DWARF in the DLL). Line tables only (function names, files, lines; no types) keep the
# PDB small; Clang's -g is meant not to change the code (e204772c with and without it: 24135 of 24141 functions keep their size), and
# godot-cpp's release `-s` still strips the DLL (with --pdb, lld drops only the DWARF / symbol table). Set BEFORE godot-cpp so its frames
# get line tables too. No absolute build path goes into the binaries or the PDB's debug info: the repository / toolchain / build prefixes are
# remapped (__FILE__ too), the compiler command lines are left out, the DLL records only the PDB's file name (pdbaltpath). The linker still
# records the toolchain's library paths in the PDB: build_windows.sh blanks their home folder prefix (tools/release/pdb_tools.py scrub).
if(MINGW AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    get_filename_component(OPENBFME_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
    # the most specific prefix last (the last matching map wins); the build folder may lie inside the repository
    set(OPENBFME_PREFIX_MAPS "-ffile-prefix-map=${OPENBFME_REPO_ROOT}/=")
    if(LLVM_MINGW_ROOT)
        list(APPEND OPENBFME_PREFIX_MAPS "-ffile-prefix-map=${LLVM_MINGW_ROOT}/=llvm-mingw/")
    endif()
    list(APPEND OPENBFME_PREFIX_MAPS "-ffile-prefix-map=${CMAKE_BINARY_DIR}/=build/")
    add_compile_options(-gcodeview -gline-tables-only -gno-codeview-command-line -fdebug-compilation-dir=. -fcoverage-compilation-dir=.
                        ${OPENBFME_PREFIX_MAPS})
    add_link_options("-Wl,--pdb=" "-Wl,-Xlink=-pdbaltpath:%_PDB%" "-Wl,-Xlink=-pdbsourcepath:C:/openbfme")
endif()
# WINCRASH-1: a release build for Linux testers can keep the GDExtension's symbols: -DOPENBFME_LINUX_DEBUG_FILE=ON compiles with -g (paths
# remapped as above) and splits them into godot/bin/<lib>.so.debug after the link; the .so itself is stripped as godot-cpp's `-s` strips
# it and points at the file (.gnu_debuglink). The .debug stays with the builder (tools/release/archive_symbols.sh), it is not packaged.
# Off by default: -g costs compile memory (the Deck builds with -j3).
option(OPENBFME_LINUX_DEBUG_FILE "Linux: split the GDExtension's debug info into godot/bin/<lib>.so.debug" OFF)
if(OPENBFME_LINUX_DEBUG_FILE AND UNIX AND NOT APPLE)
    get_filename_component(OPENBFME_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
    # the build folder twice: with a slash for its files, bare for the compilation folder (DW_AT_comp_dir)
    add_compile_options(-g "-ffile-prefix-map=${OPENBFME_REPO_ROOT}/=" "-ffile-prefix-map=${CMAKE_BINARY_DIR}/=build/"
                        "-fdebug-prefix-map=${CMAKE_BINARY_DIR}=build")
endif()

# godot-cpp 10.x generates bindings for a chosen Godot API; the project targets Godot 4.7.
set(GODOTCPP_API_VERSION "4.7" CACHE STRING "Godot API version for godot-cpp bindings")
# -DOPENBFME_BUILD_GODOT=OFF builds only openbfme_core + openbfme_tests (no godot-cpp), e.g. to run the
# unit tests on Linux/WSL where the Godot toolchain is not set up.
option(OPENBFME_BUILD_GODOT "Build the godot-cpp GDExtension" ON)
if(OPENBFME_BUILD_GODOT)
add_subdirectory(thirdparty/godot-cpp EXCLUDE_FROM_ALL)
endif()
# godot-cpp sets CMAKE_MSVC_RUNTIME_LIBRARY (static CRT) in the cache; every target below
# inherits it, so the core library links cleanly into the extension.

# ---- embedded engine data (retail archive policies) -------------------------------------
set(OPENBFME_DATA_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/data/retail-archives/rotwk-201-english-archives.json
    ${CMAKE_CURRENT_SOURCE_DIR}/data/retail-archives/rotwk-201-excluded-archives.json
    ${CMAKE_CURRENT_SOURCE_DIR}/data/retail-archives/bfme2-106-english-archives.json
    ${CMAKE_CURRENT_SOURCE_DIR}/data/retail-archives/bfme2-106-excluded-archives.json
    # OBJ-1: the RotWK module registry and field-table grammar (generated by tools/rw_object_model)
    ${CMAKE_CURRENT_SOURCE_DIR}/data/rotwk-201/module-registry.json
    ${CMAKE_CURRENT_SOURCE_DIR}/data/rotwk-201/field-tables.json)
set(OPENBFME_EMBEDDED_CPP ${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedData.gen.cpp)
add_custom_command(
    OUTPUT ${OPENBFME_EMBEDDED_CPP}
    COMMAND ${CMAKE_COMMAND} -DOUTPUT=${OPENBFME_EMBEDDED_CPP} "-DINPUTS=${OPENBFME_DATA_FILES}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFiles.cmake
    DEPENDS ${OPENBFME_DATA_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFiles.cmake
    COMMENT "Embedding engine data files"
    VERBATIM) # the ';'-separated INPUTS list must reach cmake as one argument (a POSIX shell split it)

# ---- core ---------------------------------------------------------------------------------
include(cmake/SimFp.cmake) # openbfme_sim_fp (the simulation floating-point contract) and the simulation manifest helpers
include(cmake/Apt.cmake) # OPENBFME_APT_SOURCES / OPENBFME_APT_TESTS (EA Apt player lane)
include(cmake/Lua.cmake) # openbfme_lua: Lua 4.0.1 + EA's fork (lane LUA-1)
include(cmake/Fx.cmake)  # OPENBFME_FX_SOURCES / OPENBFME_FX_TESTS (FX-1: FXList, particles, emitters)
include(cmake/Production.cmake) # OPENBFME_PROD_SOURCES / OPENBFME_PROD_TESTS (lane PROD-1)
include(cmake/Audio.cmake) # OPENBFME_AUDIO_SOURCES / OPENBFME_AUDIO_TESTS (lane AUDIO-1)
include(cmake/Move.cmake) # OPENBFME_MOVE_SOURCES / OPENBFME_MOVE_TESTS (lane MOVE-1)
include(cmake/Hud.cmake) # OPENBFME_HUD_SOURCES / OPENBFME_HUD_TESTS (lane HUD-1)
include(cmake/Start.cmake)      # OPENBFME_START_SOURCES / OPENBFME_START_TESTS (lane START-1)
include(cmake/Build.cmake)      # BUILD-1: construction, castles, dozers (appends to OPENBFME_SIM_SOURCES; OPENBFME_BUILD_TESTS)
include(cmake/Combat.cmake)     # OPENBFME_COMBAT_SOURCES / OPENBFME_COMBAT_TESTS (lane COMBAT-1)
include(cmake/Structure.cmake)  # OPENBFME_STRUCTURE_SOURCES / OPENBFME_STRUCTURE_TESTS (lane COMBAT-2)
include(cmake/Spell.cmake)      # OPENBFME_SPELL_SOURCES / OPENBFME_SPELL_TESTS (lane SPELL-1)
include(cmake/Render.cmake)     # OPENBFME_RENDER_SOURCES / OPENBFME_RENDER_TESTS (lane RENDER-1, client / draw only)
include(cmake/SkirmishAI.cmake) # OPENBFME_SKIRMISH_AI_SOURCES / OPENBFME_SKIRMISH_AI_TESTS (lane AI-1)
include(cmake/Vis.cmake)        # OPENBFME_VIS_SOURCES / OPENBFME_VIS_TESTS (lane VIS-1)
include(cmake/Upgrade.cmake)    # OPENBFME_UPGRADE_SOURCES / OPENBFME_UPGRADE_TESTS (lane UPGRADE-1)
include(cmake/Render2.cmake)    # RENDER-2: launch bones, construction look (appends to OPENBFME_SIM_SOURCES; OPENBFME_RENDER2_TESTS)
include(cmake/Render3.cmake)    # OPENBFME_RENDER3_TESTS (lane RENDER-3, client / draw only)
include(cmake/Render4.cmake)    # OPENBFME_RENDER4_SOURCES / _TESTS / _GODOT_SOURCES (lane RENDER-4, client / draw only)
include(cmake/Path2.cmake)      # OPENBFME_PATH2_SOURCES / OPENBFME_PATH2_TESTS (lane PATH-2)
include(cmake/Net.cmake)        # OPENBFME_NET_SOURCES / OPENBFME_NET_TESTS, openbfme_peer (lane MP-1)
include(cmake/Experience.cmake) # OPENBFME_XP_SOURCES / OPENBFME_XP_TESTS (lane XP-1)
include(cmake/Horde2.cmake)     # OPENBFME_HORDE2_SOURCES / OPENBFME_HORDE2_TESTS (lane HORDE-2)
include(cmake/Integ1.cmake)     # INTEG-1: StancesBehavior (appends to OPENBFME_SIM_SOURCES; OPENBFME_INTEG1_TESTS)
include(cmake/Fx2.cmake)        # FX-2: effects in the live game (appends to OPENBFME_SIM_SOURCES; OPENBFME_FX2_SOURCES / OPENBFME_FX2_TESTS)
include(cmake/Fx3.cmake)        # FX-3: particle and draw gaps (OPENBFME_FX3_SOURCES / OPENBFME_FX3_TESTS)
include(cmake/Hero.cmake)       # HERO-1: heroes (appends to OPENBFME_SIM_SOURCES; OPENBFME_HERO_TESTS)
include(cmake/Proj2.cmake)      # PROJ-2: siege projectiles (appends to OPENBFME_SIM_SOURCES; OPENBFME_PROJ2_TESTS)
include(cmake/Castle1.cmake)    # OPENBFME_CASTLE1_TESTS (lane CASTLE-1: the castle after its unpack)
include(cmake/Mod1.cmake)       # MODULES-1: the remaining behaviour modules (appends to OPENBFME_SIM_SOURCES; OPENBFME_MOD1_TESTS)
include(cmake/Mod2.cmake)       # MODULES-2: partition manager, emotions, area-scan modules (appends to OPENBFME_SIM_SOURCES; OPENBFME_MOD2_TESTS)
include(cmake/Mod3.cmake)       # MODULES-3: the emotion AI states, the safe path, the crush warning (appends to OPENBFME_SIM_SOURCES; OPENBFME_MOD3_TESTS)
include(cmake/Stealth.cmake)    # STEALTH-1: invisibility and detection (appends to OPENBFME_SIM_SOURCES; OPENBFME_STEALTH_TESTS)
include(cmake/Garrison.cmake)   # GARRISON-1: garrisons (appends to OPENBFME_SIM_SOURCES; OPENBFME_GARRISON_TESTS)
include(cmake/Transport.cmake)  # GARRISON-2: transports, siege carriers, turrets (appends to OPENBFME_SIM_SOURCES; OPENBFME_TRANSPORT_TESTS)
include(cmake/Hud2.cmake)       # OPENBFME_HUD2_SOURCES / OPENBFME_HUD2_TESTS (lane HUD-2: radar terrain picture, Palantir globe and power cap)
include(cmake/Radar1.cmake)     # OPENBFME_RADAR1_TESTS (lane RADAR-1: the radar's objects and view box as RotWK draws them)
include(cmake/Smooth.cmake)     # OPENBFME_SMOOTH_SOURCES / OPENBFME_SMOOTH_TESTS (lane SMOOTH-1, client only)
include(cmake/Phys1.cmake)      # PHYS-1: collision and separation, PhysicsBehavior (appends to OPENBFME_SIM_SOURCES; OPENBFME_PHYS1_TESTS)
include(cmake/Perf1.cmake)      # OPENBFME_PERF1_TESTS (lane PERF-1: benchmarks, optimisation determinism)
include(cmake/Perf2.cmake)      # PERF-2: the job system, parallel phases (appends to OPENBFME_SIM_SOURCES; OPENBFME_PERF2_TESTS)
include(cmake/Perf3.cmake)      # PERF-3: client frame-time work (OPENBFME_PERF3_TESTS)
include(cmake/Audio3.cmake)     # AUDIO-3: the sound request log, building sounds (appends to OPENBFME_SIM_SOURCES; OPENBFME_AUDIO3_TESTS)
include(cmake/Audio4.cmake)     # AUDIO-4: QA-1's sound follow-ups (OPENBFME_AUDIO4_TESTS)
include(cmake/End1.cmake)       # END-1: the score keeper, the end sequence and the score screen (appends to OPENBFME_SIM_SOURCES; OPENBFME_END1_SOURCES / _TESTS)
include(cmake/Qa1.cmake)       # OPENBFME_QA1_SOURCES / OPENBFME_QA1_TESTS (lane QA-1: the drawn-model pick)
include(cmake/Mp2.cmake)        # MP-2: the disconnect path, a dropped player's assets (appends to OPENBFME_SIM_SOURCES; OPENBFME_MP2_TESTS)
include(cmake/Script1.cmake)    # SCRIPT-1: the map script engine (appends to OPENBFME_SIM_SOURCES; OPENBFME_SCRIPT1_TESTS)
include(cmake/Camp1.cmake)      # CAMP-1: the campaigns (OPENBFME_CAMP1_SOURCES / OPENBFME_CAMP1_TESTS)
include(cmake/Camp2.cmake)      # CAMP-2: the movies' VP6 picture (OPENBFME_CAMP2_SOURCES / _GODOT_SOURCES / _TESTS)
include(cmake/Ui1.cmake)        # UI-1: interface gaps (client only; OPENBFME_UI1_CLIENT_SOURCES / OPENBFME_UI1_TESTS)
include(cmake/Release1.cmake)   # RELEASE-1: install discovery, version, session log (client only; OPENBFME_RELEASE1_SOURCES / _GODOT_SOURCES / _TESTS)
include(cmake/Mod4.cmake)       # MOD-4: AISpecialPowerUpdate, RepairSpecialPower, ... (appends to OPENBFME_SIM_SOURCES; OPENBFME_MOD4_TESTS)
include(cmake/Ui2.cmake)        # UI-2: owner menu feedback (client only; OPENBFME_UI2_CLIENT_SOURCES / OPENBFME_UI2_TESTS)
include(cmake/Anim1.cmake)      # OPENBFME_ANIM1_TESTS (lane ANIM-1: combat animation)
include(cmake/Win1.cmake)       # WIN-1: the MSVCR71 real parser (appends to OPENBFME_SIM_SOURCES; OPENBFME_WIN1_TESTS: cross-OS lockstep)
include(cmake/Garrison3.cmake)  # GARRISON-3: the garrison exit (OPENBFME_GARRISON3_TESTS)
include(cmake/Combat3.cmake)    # COMBAT-3: cavalry charges and siege crews (OPENBFME_COMBAT3_TESTS)
include(cmake/Combat4.cmake)    # COMBAT-4: shockwaves, siege crews in motion, corpses (OPENBFME_COMBAT4_TESTS)
include(cmake/Hud4.cmake)       # HUD-4: every command reachable, every hotkey (appends to OPENBFME_SIM_SOURCES; OPENBFME_HUD4_TESTS)
include(cmake/Qa2fix.cmake)     # QA2-FIX: a player's wall span through the HUD, the lobby after a game (OPENBFME_QA2FIX_TESTS)
include(cmake/Hud5.cmake)       # HUD-5: the owner's first Windows session (OPENBFME_HUD5_CLIENT_SOURCES / OPENBFME_HUD5_TESTS)
include(cmake/Exit1.cmake)      # EXIT-1: the horde member update, stale MOVING (appends to OPENBFME_SIM_SOURCES; OPENBFME_EXIT1_TESTS)
include(cmake/Cah1.cmake)       # CAH-1: the Create-a-Hero screens (client only; OPENBFME_CAH1_SOURCES / _GODOT_SOURCES / _TESTS)
include(cmake/Move3.cmake)      # MOVE-3: horde spacing, the blocked path patch (appends to OPENBFME_SIM_SOURCES; OPENBFME_MOVE3_TESTS)
include(cmake/Input1.cmake)     # INPUT-1: RotWK's input layer (OPENBFME_INPUT1_TESTS)
add_library(openbfme_core STATIC
    ${OPENBFME_APT_SOURCES}
    ${OPENBFME_AUDIO4_CLIENT_SOURCES}
    ${OPENBFME_FX_SOURCES}
    ${OPENBFME_PROD_SOURCES}
    ${OPENBFME_AUDIO_SOURCES}
    ${OPENBFME_START_SOURCES}
    ${OPENBFME_MOVE_SOURCES}
    ${OPENBFME_HUD_SOURCES}
    ${OPENBFME_QA1_SOURCES}
    ${OPENBFME_HUD2_SOURCES}
    ${OPENBFME_COMBAT_SOURCES}
    ${OPENBFME_STRUCTURE_SOURCES}
    ${OPENBFME_RENDER_SOURCES}
    ${OPENBFME_RENDER4_SOURCES}
    ${OPENBFME_RENDER2_SOURCES}
    ${OPENBFME_FX2_SOURCES}
    ${OPENBFME_FX3_SOURCES}
    ${OPENBFME_SMOOTH_SOURCES}
    ${OPENBFME_PROJ2_SOURCES}
    ${OPENBFME_STEALTH_SOURCES}
    ${OPENBFME_END1_SOURCES}
    ${OPENBFME_MP2_CLIENT_SOURCES}
    ${OPENBFME_UI1_CLIENT_SOURCES}
    ${OPENBFME_RELEASE1_SOURCES}
    ${OPENBFME_UI2_CLIENT_SOURCES}
    ${OPENBFME_SCRIPT1_SOURCES}
    ${OPENBFME_CAMP1_SOURCES}
    ${OPENBFME_HUD5_CLIENT_SOURCES}
    ${OPENBFME_CAH1_SOURCES}
    ${OPENBFME_CAMP2_SOURCES}
    src/Common/AsciiString.cpp
    src/Common/LogicFrameClock.cpp
    src/Common/MD5.cpp
    src/Common/MiniJson.cpp
    src/Common/System/LooseFileScan.cpp
    # OBJ-1: object model
    src/GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.cpp
    src/GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawServices.cpp
    src/GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDrawBones.cpp
    src/GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.cpp
    src/Libraries/file/TextFile.cpp
    src/Libraries/file/Win32Path.cpp
    src/Common/System/ArchiveFile.cpp
    src/Common/System/ArchiveFileSystem.cpp
    src/Common/System/RetailArchivePolicy.cpp
    src/GameClient/MapCache.cpp
    src/GameClient/MapWeather.cpp
    src/GameClient/TGAFile.cpp
    src/GameClient/TerrainAtlas.cpp
    src/GameClient/TerrainRoads.cpp
    src/GameClient/WaterGeometry.cpp
    src/GameEngineDevice/W3DDevice/GameClient/AssetCatalog.cpp
    src/GameEngineDevice/W3DDevice/GameClient/HeightMapMesh.cpp
    src/GameEngineDevice/W3DDevice/GameClient/TerrainComposite.cpp
    src/GameEngineDevice/W3DDevice/GameClient/WorldHeightMap.cpp
    src/GameEngineDevice/W3DDevice/GameClient/WorldHeightMapUV.cpp
    src/Libraries/Compression/CompressionManager.cpp
    src/Libraries/Compression/EAC/refdecode.cpp
    src/GameEngineDevice/Win32Device/Common/ArchiveHandle.cpp
    src/GameEngineDevice/Win32Device/Common/Win32BIGFile.cpp
    src/GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.cpp
    src/Libraries/WWVegas/WWLib/chunkio.cpp
    src/Libraries/WWVegas/WW3D2/assetmgr.cpp
    src/Libraries/WWVegas/WW3D2/hcanim.cpp
    src/Libraries/WWVegas/WW3D2/hlod.cpp
    src/Libraries/WWVegas/WW3D2/hrawanim.cpp
    src/Libraries/WWVegas/WW3D2/mapper.cpp
    src/Libraries/WWVegas/WW3D2/meshrender.cpp
    src/Libraries/WWVegas/WW3D2/w3dstops.cpp
    src/Libraries/WWVegas/WW3D2/w3dshadergen.cpp
    src/Libraries/WWVegas/WW3D2/textureloader.cpp
    src/GameEngineDevice/W3DDevice/GameClient/HouseColor.cpp
    src/Libraries/WWVegas/WW3D2/shader.cpp
    src/Libraries/WWVegas/WW3D2/htree.cpp
    src/Libraries/WWVegas/WW3D2/meshmdlio.cpp
    src/Libraries/WWVegas/WW3D2/motchan.cpp
    src/Libraries/WWVegas/WW3D2/part_ldr.cpp
    src/Libraries/WWVegas/WW3D2/shdmesh.cpp
    src/Libraries/WWVegas/WW3D2/w3dchunks.cpp
    ${OPENBFME_EMBEDDED_CPP})
# ---- the simulation manifest -------------------------------------------------------------------------------------------------------
# OPENBFME_SIM_SOURCES is THE list of simulation sources (code whose results enter the lockstep state hash, and the loaders of the numbers that feed it). Every
# translation unit of the build is either in it or in the reviewed exclusion list of tools/sim/sim_policy.json (UI, rendering, archive I/O, host pacing,
# each with its reason): a new source in neither fails tools/sim/sim_audit.py. A lane that adds simulation
# code appends its sources to a list(APPEND OPENBFME_SIM_SOURCES ...) block below, never to a plain target_sources(): the list is registered
# once and written to <build>/sim_manifest.json, and tools/sim/sim_audit.py (run it after configure/build) fails for a source under a
# simulation path that is not listed here, for a compile command without the flags of cmake/SimFp.cmake, and for floating-point arithmetic
# outside the numeric facade. The floating-point flags themselves are NOT per file: openbfme_sim_fp is linked PUBLIC by openbfme_core.
# ---- shared numeric facade and simulation data (the numeric state, the map, the sides, the object name index, the script data) ----
list(APPEND OPENBFME_SIM_SOURCES
    src/Common/System/NumericState.cpp
    src/GameLogic/Map/SidesList.cpp
    src/GameLogic/Map/TerrainLogic.cpp
    src/GameLogic/Object/ObjectNameIndex.cpp
    src/GameLogic/ScriptEngine/Scripts.cpp)
# ---- data loaders that feed the simulation (INI parsers, object / module templates, map chunks, the retail object world): their numbers are simulation inputs ----
list(APPEND OPENBFME_SIM_SOURCES
    src/Common/INI/INI.cpp
    src/Common/INI/INIBlockStubs.cpp
    src/Common/INI/INIFieldParsers.cpp
    src/Common/INI/INIMacro.cpp
    src/Common/NameKeyGenerator.cpp
    src/Common/ModelState.cpp
    src/Common/System/DataChunk.cpp
    src/Common/System/Dict.cpp
    src/Common/System/SubsystemInitOrder.cpp
    src/Common/System/SubsystemLegend.cpp
    src/Common/Thing/ModuleFactory.cpp
    src/Common/Thing/RwFieldTables.cpp
    src/Common/Thing/RwGrammar.cpp
    src/Common/Thing/ThingFactory.cpp
    src/Common/Thing/ThingTemplate.cpp
    src/GameClient/MapChunks.cpp
    src/GameClient/MapStops.cpp
    src/GameClient/MapUtil.cpp
    src/GameClient/TerrainTypes.cpp
    src/GameClient/MapObjectRuntime.cpp
    src/GameClient/MapCreationHooks.cpp
    src/GameClient/MapHordeSpawn.cpp
    src/GameLogic/Object/RetailObjectWorld.cpp)
# ---- HORDE-1 sources (horde and locomotor data, kinematics, slot logic) ----
list(APPEND OPENBFME_SIM_SOURCES
    src/Common/RandomValue.cpp
    src/GameLogic/Object/Locomotor.cpp
    src/GameLogic/Object/LocomotorMove.cpp
    src/GameLogic/BitFlags.cpp
    src/GameLogic/BitFlagNames.cpp
    src/GameLogic/ObjectFilter.cpp
    src/GameLogic/Object/Contain/ContainParseHooks.cpp
    src/GameLogic/Object/Contain/OpenContain.cpp
    src/GameLogic/Object/Contain/TransportContain.cpp
    src/GameLogic/Object/Contain/HordeContain.cpp
    src/GameLogic/Object/Contain/HordeContainCore.cpp)
# ---- MAPOBJ-1 sources (the map's objects on the terrain: draw module data; client side) ----
target_sources(openbfme_core PRIVATE
    src/GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DTreeDraw.cpp
    src/GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDrawVariants.cpp
    src/GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawModules.cpp
    src/GameClient/MapObjectDrawables.cpp
    src/GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DLuaDrawScriptHost.cpp)
# ---- LUA-1 sources (Lua scripting: XML reader, event registry, engine, bindings) ----
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/ScriptEngine/LuaXml.cpp
    src/GameLogic/ScriptEngine/LuaRuntime.cpp
    src/GameLogic/ScriptEngine/LuaDrawableState.cpp
    src/GameLogic/ScriptEngine/LuaScriptEvents.cpp
    src/GameLogic/ScriptEngine/LuaScriptEngine.cpp
    src/GameLogic/ScriptEngine/LuaBindings.cpp)
# ---- LOGIC-1 sources (live objects, module runtime, scheduler, players) ----
list(APPEND OPENBFME_SIM_SOURCES
    src/Common/Thing/Thing.cpp
    src/Common/Thing/ThingFactoryObjects.cpp
    src/Common/ModuleRuntime.cpp
    src/Common/PlayerTemplate.cpp
    src/Common/Player.cpp
    src/Common/PlayerList.cpp
    src/Common/Team.cpp
    src/GameLogic/GameLogic.cpp
    src/GameLogic/ObjectTemplateInfo.cpp
    src/GameLogic/Object/Object.cpp
    src/GameLogic/Module/UpdateModule.cpp
    src/GameLogic/Module/UnportedModule.cpp
    src/GameLogic/Module/ObjectHelper.cpp
    src/GameLogic/Module/ActiveBody.cpp
    src/GameLogic/Module/LogicModules.cpp
    src/GameLogic/Object/Contain/HordeContainRuntime.cpp
    src/GameLogic/GameLogicSettings.cpp
    src/GameLogic/MapObjectLoop.cpp
    src/GameClient/Drawable.cpp
    src/GameClient/DrawableManager.cpp
    src/GameClient/LiveGame.cpp
    src/Common/Thing/KindOfTokens.cpp
    src/GameClient/LiveScripting.cpp
    src/GameClient/MapClassification.cpp)
# ---- COMBAT-1 sources (weapon sets on objects, damage, death, the attack AI, horde combat; cmake/Combat.cmake) ----
list(APPEND OPENBFME_SIM_SOURCES ${OPENBFME_COMBAT_SOURCES})
# ---- COMBAT-2 sources (structure bodies, the collapse, victory; cmake/Structure.cmake) ----
list(APPEND OPENBFME_SIM_SOURCES ${OPENBFME_STRUCTURE_SOURCES})
# ---- AI-1 sources (the skirmish AI: data, manager, players; cmake/SkirmishAI.cmake) ----
list(APPEND OPENBFME_SIM_SOURCES ${OPENBFME_SKIRMISH_AI_SOURCES})
# ---- HORDE-2 sources (crush, flanking, horde melee; cmake/Horde2.cmake) ----
list(APPEND OPENBFME_SIM_SOURCES ${OPENBFME_HORDE2_SOURCES})
# ---- SPELL-1 sources (sciences, ranks, special powers; cmake/Spell.cmake) ----
list(APPEND OPENBFME_SIM_SOURCES ${OPENBFME_SPELL_SOURCES})
# ---- VIS-1 sources (the shroud manager and its settings; cmake/Vis.cmake) ----
list(APPEND OPENBFME_SIM_SOURCES ${OPENBFME_VIS_SOURCES})
# ---- PROD-1 sources (command sets, production queues, exits, rally points, the command dispatcher; cmake/Production.cmake) ----
list(APPEND OPENBFME_SIM_SOURCES ${OPENBFME_PROD_SOURCES})
# ---- WEAPON-1 sources (weapons, armour and damage: data and the deterministic core) ----
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/WeaponNames.cpp
    src/GameLogic/Object/Weapon.cpp
    src/GameLogic/Object/WeaponNugget.cpp
    src/GameLogic/Object/WeaponSet.cpp
    src/GameLogic/Object/Armor.cpp
    src/GameLogic/Object/ArmorSet.cpp
    src/GameLogic/Object/DamageFX.cpp
    src/GameLogic/Object/Damage.cpp
    src/GameLogic/Object/WeaponStores.cpp
    src/GameLogic/Object/WeaponState.cpp)
# ---- ECON-1 sources (the economy: resources, income, command points, cost modifiers; the settings and filter loaders feed it) ----
list(APPEND OPENBFME_SIM_SOURCES
    src/Common/CommandPoints.cpp
    src/GameLogic/ObjectFilterMatch.cpp
    src/GameLogic/EconomySettings.cpp
    src/GameLogic/Economy.cpp
    src/GameLogic/System/TerrainResourceManager.cpp
    src/GameLogic/System/PathfinderResourceTerrain.cpp
    src/GameLogic/Module/TerrainResourceBehavior.cpp
    src/GameLogic/Module/AutoDepositUpdate.cpp
    src/GameLogic/Module/UpgradeModules.cpp
    src/GameLogic/Module/MoneyEventModules.cpp
    src/GameLogic/Module/EconomyModules.cpp)
# ---- UPGRADE-1 sources (the UpgradeCenter, the upgrade modules; cmake/Upgrade.cmake) ----
list(APPEND OPENBFME_SIM_SOURCES ${OPENBFME_UPGRADE_SOURCES})
list(APPEND OPENBFME_SIM_SOURCES ${OPENBFME_PATH2_SOURCES})
list(APPEND OPENBFME_SIM_SOURCES ${OPENBFME_NET_SOURCES}) # MP-1
# ---- XP-1 sources (experience, veterancy, attribute modifiers; cmake/Experience.cmake) ----
list(APPEND OPENBFME_SIM_SOURCES ${OPENBFME_XP_SOURCES})
# ---- PATH-1 sources (pathfinder grid, zones, A*, AI move path) ----
list(APPEND OPENBFME_SIM_SOURCES
    src/GameLogic/AI/AIPathfind.cpp
    src/GameLogic/AI/AIPathfindCell.cpp
    src/GameLogic/AI/AIPathfindZones.cpp
    src/GameLogic/AI/AIPathfindFootprints.cpp
    src/GameLogic/AI/AIPathfindUnits.cpp
    src/GameLogic/AI/AIPathfindSearch.cpp
    src/GameLogic/AI/AIPathfindPath.cpp
    src/GameLogic/AI/AIPathfindConfig.cpp
    src/GameLogic/AI/AIMove.cpp
    src/GameLogic/Map/TerrainPathfindSource.cpp
    src/GameClient/MapPathfindObjects.cpp)
# ---- register the manifest once (after every lane's block above) and write it for the audit ----
list(REMOVE_DUPLICATES OPENBFME_SIM_SOURCES)
target_sources(openbfme_core PRIVATE ${OPENBFME_SIM_SOURCES})
target_sources(openbfme_core PRIVATE ${OPENBFME_SPELL_CLIENT_SOURCES}) # lane SPELL-2 (client sources, Spell.cmake)
# Simulation sources of the GDExtension (they need godot-cpp, so they are registered with the `openbfme` target, not the core): GodotGameWorld.cpp calls the core
# game start (GameLogic/NewGame, a thin caller since START-1), creates the logic objects and queues the player's commands (HUD-1 moves the commands into core);
# GodotGameStart.cpp marshals the lobby's NewGameMessage between Godot Dictionaries and the logic's struct (the values the logic consumes), so it is audited too.
if(OPENBFME_BUILD_GODOT)
    set(OPENBFME_SIM_SOURCES_GODOT src/GodotDevice/GodotGameWorld.cpp src/GodotDevice/GodotGameStart.cpp src/GodotDevice/GodotGameWorldHeroes.cpp src/GodotDevice/GodotGameWorldStealth.cpp src/GodotDevice/GodotGameWorldGarrison.cpp src/GodotDevice/GodotGameWorldCah.cpp) # CAH-1: the builder's map mode; HERO-1: the hero commands; STEALTH-1: the stealth viewer helpers; GARRISON-1: the garrison commands
endif()
openbfme_sim_write_manifest(${CMAKE_CURRENT_BINARY_DIR}/sim_manifest.json)
# `cmake --build <dir> --target sim_audit` (after the build): the flags of every compile command, the manifest and the type-aware AST scan (tools/sim/sim_audit.py)
find_program(OPENBFME_PYTHON NAMES python3 python)
if(OPENBFME_PYTHON)
    add_custom_target(sim_audit
        COMMAND ${OPENBFME_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/sim/sim_audit.py --build ${CMAKE_BINARY_DIR}
        COMMENT "Auditing the simulation floating-point contract (tools/sim/sim_audit.py)"
        VERBATIM USES_TERMINAL)
endif()
target_include_directories(openbfme_core PUBLIC src)
target_link_libraries(openbfme_core PUBLIC openbfme_lua)
add_dependencies(openbfme_core openbfme_build_version) # RELEASE-1: the generated BuildVersion.gen.cpp
openbfme_sim_register_target(openbfme_core)
set_target_properties(openbfme_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(openbfme_core PRIVATE /W4 /permissive- /utf-8)
    target_compile_definitions(openbfme_core PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

# ---- GDExtension -------------------------------------------------------------------------
if(OPENBFME_BUILD_GODOT)
set(OPENBFME_GODOT_BIN ${CMAKE_CURRENT_SOURCE_DIR}/../godot/bin)
add_library(openbfme SHARED
    src/GodotDevice/register_types.cpp
    src/GodotDevice/GodotRetailFileSystem.cpp
    src/GodotDevice/GodotW3DModelBuilder.cpp
    src/GodotDevice/GodotW3DMaterial.cpp
    src/GodotDevice/GodotW3DInstancer.cpp
    src/GodotDevice/GodotLogicClock.cpp
    src/GodotDevice/GodotMapTerrain.cpp
    src/GodotDevice/GodotMapObjectBuilder.cpp
    # APT-3: the retail menus on screen
    src/GodotDevice/GodotAptPlayer.cpp
    src/GodotDevice/GodotInGameHud.cpp
    src/GodotDevice/GodotPathfindView.cpp
    # FX-1: FXLists and particle systems
    src/GodotDevice/GodotFXPlayer.cpp
    # START-1: the new-game message and the load screen as Godot data
    ${OPENBFME_START_GODOT_SOURCES}
    ${OPENBFME_RENDER3_GODOT_SOURCES}
    ${OPENBFME_RENDER4_GODOT_SOURCES}
    ${OPENBFME_AUDIO_GODOT_SOURCES}
    ${OPENBFME_SCRIPT1_GODOT_SOURCES}
    ${OPENBFME_CAMP1_GODOT_SOURCES}
    ${OPENBFME_CAMP2_GODOT_SOURCES}
    ${OPENBFME_RELEASE1_GODOT_SOURCES}
    ${OPENBFME_UI2_GODOT_SOURCES}
    ${OPENBFME_HUD4_GODOT_SOURCES}
    ${OPENBFME_CAH1_GODOT_SOURCES})
target_sources(openbfme PRIVATE ${OPENBFME_SIM_SOURCES_GODOT}) # the simulation sources of the extension (listed in the manifest)
target_link_libraries(openbfme PRIVATE openbfme_core godot-cpp)
if(UNIX AND NOT APPLE)
    # The extension links libstdc++ statically; without this its exported locale facet ids collide with the system runtime that Mesa loads into a
    # windowed Godot, and the first stream integer extraction (std::istream >> int) dispatches into the wrong codecvt and crashes (PROD-1 review r1).
    target_link_options(openbfme PRIVATE "-Wl,--exclude-libs,ALL")
endif()
if(OPENBFME_LINUX_DEBUG_FILE AND UNIX AND NOT APPLE)
    # godot-cpp's interface holds only its release `-s` link option (cmake/common_compiler_flags.cmake): the strip happens after the split
    set_property(TARGET godot-cpp PROPERTY INTERFACE_LINK_OPTIONS "")
    find_program(OPENBFME_OBJCOPY NAMES objcopy llvm-objcopy REQUIRED)
    add_custom_command(TARGET openbfme POST_BUILD
        COMMAND "${OPENBFME_OBJCOPY}" --only-keep-debug "$<TARGET_FILE:openbfme>" "$<TARGET_FILE:openbfme>.debug"
        COMMAND "${OPENBFME_OBJCOPY}" --strip-all "$<TARGET_FILE:openbfme>"
        COMMAND "${CMAKE_COMMAND}" -E chdir "$<TARGET_FILE_DIR:openbfme>" "${OPENBFME_OBJCOPY}" "--add-gnu-debuglink=$<TARGET_FILE_NAME:openbfme>.debug"
                "$<TARGET_FILE_NAME:openbfme>"
        VERBATIM)
endif()
get_target_property(GODOTCPP_SUFFIX godot-cpp GODOTCPP_SUFFIX)
set_target_properties(openbfme PROPERTIES
    PREFIX ""
    OUTPUT_NAME "openbfme${GODOTCPP_SUFFIX}"
    LIBRARY_OUTPUT_DIRECTORY "$<1:${OPENBFME_GODOT_BIN}>"
    RUNTIME_OUTPUT_DIRECTORY "$<1:${OPENBFME_GODOT_BIN}>"
    PDB_OUTPUT_DIRECTORY "$<1:${OPENBFME_GODOT_BIN}>")
if(MSVC)
    target_compile_options(openbfme PRIVATE /utf-8)
endif()
endif()

# ---- unit tests --------------------------------------------------------------------------
add_executable(openbfme_tests
    ${OPENBFME_WIN1_TESTS}
    ${OPENBFME_SMOOTH_TESTS}
    ${OPENBFME_ANIM1_TESTS}
    ${OPENBFME_UPGRADE_TESTS}
    ${OPENBFME_NET_TESTS}
    ${OPENBFME_PATH2_TESTS}
    ${OPENBFME_XP_TESTS}
    ${OPENBFME_APT_TESTS}
    ${OPENBFME_FX_TESTS}
    ${OPENBFME_PROD_TESTS}
    ${OPENBFME_AUDIO_TESTS}
    ${OPENBFME_START_TESTS}
    ${OPENBFME_MOVE_TESTS}
    ${OPENBFME_HUD_TESTS}
    ${OPENBFME_QA1_TESTS}
    ${OPENBFME_HUD2_TESTS}
    ${OPENBFME_RADAR1_TESTS}
    ${OPENBFME_COMBAT_TESTS}
    ${OPENBFME_STRUCTURE_TESTS}
    ${OPENBFME_SPELL_TESTS}
    ${OPENBFME_RENDER_TESTS}
    ${OPENBFME_SKIRMISH_AI_TESTS}
    ${OPENBFME_VIS_TESTS}
    ${OPENBFME_RENDER2_TESTS}
    ${OPENBFME_HORDE2_TESTS}
    ${OPENBFME_INTEG1_TESTS}
    ${OPENBFME_RENDER3_TESTS}
    ${OPENBFME_RENDER4_TESTS}
    ${OPENBFME_FX2_TESTS}
    ${OPENBFME_FX3_TESTS}
    ${OPENBFME_HERO_TESTS}
    ${OPENBFME_MOD1_TESTS}
    ${OPENBFME_MOD2_TESTS}
    ${OPENBFME_MOD3_TESTS}
    ${OPENBFME_PHYS1_TESTS}
    ${OPENBFME_PERF1_TESTS}
    ${OPENBFME_PERF2_TESTS}
    ${OPENBFME_PERF3_TESTS}
    ${OPENBFME_AUDIO3_TESTS}
    ${OPENBFME_AUDIO4_TESTS}
    ${OPENBFME_PROJ2_TESTS}
    ${OPENBFME_STEALTH_TESTS}
    ${OPENBFME_GARRISON_TESTS}
    ${OPENBFME_TRANSPORT_TESTS}
    ${OPENBFME_BUILD_TESTS}
    ${OPENBFME_CASTLE1_TESTS}
    ${OPENBFME_END1_TESTS}
    ${OPENBFME_MP2_TESTS}
    ${OPENBFME_SCRIPT1_TESTS}
    ${OPENBFME_CAMP1_TESTS}
    ${OPENBFME_CAMP2_TESTS}
    ${OPENBFME_UI1_TESTS}
    ${OPENBFME_RELEASE1_TESTS}
    ${OPENBFME_MOD4_TESTS}
    ${OPENBFME_UI2_TESTS}
    ${OPENBFME_GARRISON3_TESTS}
    ${OPENBFME_COMBAT3_TESTS}
    ${OPENBFME_COMBAT4_TESTS}
    ${OPENBFME_HUD4_TESTS}
    ${OPENBFME_QA2FIX_TESTS}
    ${OPENBFME_EXIT1_TESTS}
    ${OPENBFME_HUD5_TESTS}
    ${OPENBFME_CAH1_TESTS}
    ${OPENBFME_MOVE3_TESTS}
    ${OPENBFME_INPUT1_TESTS}
    tests/test_main.cpp
    tests/test_big_archive.cpp
    tests/test_archive_order.cpp
    tests/test_logic_clock.cpp
    tests/test_chunkio.cpp
    tests/test_w3d_htree.cpp
    tests/test_w3d_mesh.cpp
    tests/test_w3d_emitter_shdmesh.cpp
    tests/test_w3d_anim.cpp
    tests/test_w3d_pose.cpp
    tests/test_workerpool.cpp
    tests/test_w3d_mapper.cpp
    tests/test_w3d_shader.cpp
    tests/test_w3d_shadergen.cpp
    tests/test_w3d_stops.cpp
    tests/test_w3d_render.cpp
    tests/test_w3d_retail_models.cpp
    tests/test_w3d_render_corpus.cpp
    tests/test_asset_catalog.cpp
    tests/test_asset_catalog_oracle.cpp
    tests/test_w3d_corpus.cpp
    tests/test_map_refpack.cpp
    tests/test_map_chunks.cpp
    tests/test_map_corpus.cpp
    tests/test_map_cache.cpp
    tests/test_map_terrain.cpp
    tests/test_map_render.cpp
    tests/RetailTestMount.cpp
    tests/test_md5_json.cpp
    tests/test_textfile.cpp
    tests/test_ini_core.cpp
    tests/test_ini_scanners.cpp
    tests/test_ini_macro.cpp
    tests/test_numeric_state.cpp
    tests/test_win32path.cpp
    tests/test_mount_handles.cpp
    tests/test_subsystem_legend.cpp
    tests/test_ini_retail_golden.cpp
    # OBJ-1
    tests/test_name_key.cpp
    tests/test_object_model.cpp
    tests/test_object_model_retail.cpp
    # ---- HORDE-1 (horde and locomotor data, kinematics, slot logic) ----
    tests/test_game_logic_random.cpp
    tests/test_locomotor.cpp
    tests/test_horde_contain_data.cpp
    tests/test_locomotor_kinematics.cpp
    tests/test_horde_contain_core.cpp
    # ---- WEAPON-1 (weapons, armour and damage) ----
    tests/test_weapon_data.cpp
    tests/test_damage_core.cpp
    tests/test_weapon_state.cpp
    tests/test_header_hygiene.cpp
    tests/test_numeric_facade.cpp
    # lane DRAW-1: model condition states and animation selection
    tests/test_modelstate.cpp
    tests/test_w3d_draw_data.cpp
    tests/W3DDrawRetail.cpp
    tests/test_w3d_draw_retail.cpp
    tests/test_w3d_draw_runtime.cpp
    tests/test_w3d_draw_retail_units.cpp
    # lane MAPOBJ-1
    tests/test_mapobj_draw_data.cpp
    tests/test_mapobj_creation.cpp
    tests/test_mapobj_shadergen.cpp
    tests/test_mapobj_retail.cpp
    # ---- LUA-1 (Lua scripting) ----
    tests/test_lua_runtime.cpp
    tests/test_lua_events.cpp
    tests/test_lua_engine.cpp
    tests/test_lua_retail.cpp
    tests/test_lua_draw.cpp
    tests/test_lua_binary_facts.cpp
    tests/test_lua_stops.cpp
    # ---- PATH-1 (pathfinder, AI move path) ----
    tests/test_pathfind_cells.cpp
    tests/test_pathfind_grid.cpp
    tests/test_pathfind_path.cpp
    tests/test_pathfind_retail.cpp
    tests/test_pathfind_retail_search.cpp
    tests/test_pathfind_move.cpp
    tests/test_pathfind_numerics.cpp
    # lane LOGIC-1
    tests/test_logic_scheduler.cpp
    tests/test_logic_objects.cpp
    tests/test_logic_players.cpp
    tests/test_logic_modules.cpp
    tests/test_logic_hash.cpp
    tests/test_logic_settings.cpp
    tests/test_logic_retail.cpp
    # shared numeric facade (WEAPON-1 e75dd8c, imported unchanged: wide PC24 results, SSE binary32 ops, retail integer conversions)
    tests/test_numeric_facade.cpp
    # lane ECON-1: the economy
    tests/test_econ_core.cpp
    tests/test_econ_resources.cpp
    tests/test_econ_hash.cpp
    tests/test_econ_retail.cpp)
target_link_libraries(openbfme_tests PRIVATE openbfme_core)
openbfme_net_targets() # MP-1: the network host sources, the headless peer
# a separate process for the independent-process determinism test of lua_pushusertag userdata (tests/test_lua_runtime.cpp)
add_executable(openbfme_lua_probe tests/lua_userdata_probe.cpp)
target_link_libraries(openbfme_lua_probe PRIVATE openbfme_lua)
add_dependencies(openbfme_tests openbfme_lua_probe)
target_compile_definitions(openbfme_tests PRIVATE OPENBFME_LUA_PROBE_EXE="$<TARGET_FILE:openbfme_lua_probe>")
if(WIN32)
    # the real shlwapi path calls are the oracle for Win32Path (tests/test_win32path.cpp)
    target_link_libraries(openbfme_tests PRIVATE shlwapi)
endif()
# golden tables written by tools/ini_macro_oracle.py (independent oracle for the INI macro tests)
target_compile_definitions(openbfme_tests PRIVATE OPENBFME_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
# 32-bit x87 hardware oracle (tools/x87_oracle/build.bat); the tests skip loudly when it is not built
get_filename_component(OPENBFME_X87_ORACLE_EXE "${CMAKE_CURRENT_SOURCE_DIR}/../tools/x87_oracle/build/x87_oracle.exe" ABSOLUTE)
target_compile_definitions(openbfme_tests PRIVATE OPENBFME_X87_ORACLE_DEFAULT="${OPENBFME_X87_ORACLE_EXE}")
# what the exe is built from ('|' separated): the tests fail with "stale helper: rebuild with ..." when it is older
get_filename_component(OPENBFME_X87_ORACLE_CPP "${CMAKE_CURRENT_SOURCE_DIR}/../tools/x87_oracle/x87_oracle.cpp" ABSOLUTE)
get_filename_component(OPENBFME_X87_ORACLE_BAT "${CMAKE_CURRENT_SOURCE_DIR}/../tools/x87_oracle/build.bat" ABSOLUTE)
target_compile_definitions(openbfme_tests PRIVATE OPENBFME_X87_ORACLE_SOURCES="${OPENBFME_X87_ORACLE_CPP}|${OPENBFME_X87_ORACLE_BAT}")
openbfme_apt_configure_tests(openbfme_tests)
target_include_directories(openbfme_tests PRIVATE thirdparty/doctest)
target_compile_definitions(openbfme_tests PRIVATE
    OPENBFME_LUA_PRISTINE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/lua-4.0.1"
    OPENBFME_LUA_PATCH_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/Libraries/Lua")
target_compile_definitions(openbfme_tests PRIVATE OPENBFME_AUDIO_FIXTURE_DIR="${OPENBFME_AUDIO_FIXTURE_DIR}")
if(TARGET audio_mp3_fixtures)
    add_dependencies(openbfme_tests audio_mp3_fixtures)
endif()
target_compile_definitions(openbfme_tests PRIVATE OPENBFME_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data"
    OPENBFME_DOCS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../docs")
if(MSVC)
    target_compile_options(openbfme_tests PRIVATE /utf-8)
endif()

# the contract holds for every target that carries simulation code or links it: nothing added to a target after its registration may override it
openbfme_sim_check_targets()

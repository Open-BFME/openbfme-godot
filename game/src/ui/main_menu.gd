extends Control
## Player-facing menu shell. Proof-stage breadth remains available, but it no
## longer competes with the vertical slice on the main page.

signal skirmish_options_ready

## WHAT THIS FILE MAY PRELOAD, AND WHY THE LIST IS SHORT.
##
## `preload` resolves at COMPILE time, so every script named here is compiled
## before this scene can be instantiated - which is before the menu draws a
## single button, behind a stationary loading screen. This file used to preload
## the tactical vertical slice, the War of the Ring strategic screen, the GAME
## SETUP screen, the 3D strategic map view and the multiplayer lockstep session.
## MEASURED: 91 files / ~90,100 lines of GDScript compiled to show a menu, of
## which ~83,000 lines belonged to surfaces the player had not navigated to.
## Time from the first drawn frame to an interactive menu was 8.8-9.7 s.
##
## The rule is now: preload ONLY what the menu itself needs to lay itself out.
## Everything a player reaches by NAVIGATING is fetched by runtime `load()` at
## the moment they navigate, through `_lazy_script()` below, and cached.
##
## The four eager UI scripts are small and are on the layout path (measured
## isolated compile: theme 21 ms, nav diamonds 3 ms, shell flyout 12 ms, APT
## runtime 24 ms, pack capability 12 ms). `SliceIds` is a leaf constants file
## with no preloads at all - see retail_slice_ids.gd for why it exists.
const BootProfile = preload("res://src/core/boot_profile.gd")
const ThemeScript = preload("res://src/ui/openbfme_theme.gd")
const NavDiamondsScript = preload("res://src/ui/openbfme_nav_diamonds.gd")
const ShellFlyoutScript = preload("res://src/ui/openbfme_shell_flyout.gd")
const ShellAptRuntimeScript = preload("res://src/ui/retail_shell_apt_runtime.gd")
const PackCapabilityScript = preload("res://src/content/pack_capability.gd")
const SliceIds = preload("res://src/retail_slice/retail_slice_ids.gd")
const CahHeroesScript = preload("res://src/content/cah_heroes.gd")
## Preloaded only for its canonical hero-document spelling: a skirmish pick and a
## lobby announcement must reach the roster in the SAME bytes, so there is one
## canonicalizer and both callers use it.
const SessionScript = preload("res://src/retail_slice/retail_lockstep_session.gd")
## DELIBERATELY STILL EAGER. `_locate_wotr_document()` runs inside `_ready()` -
## the SOLO PLAY flyout is built with War of the Ring's real state rather than a
## placeholder a later refresh has to correct - and it calls
## `WotrSession.locate_document()`. Deferring it would only move the same cost,
## so it is honest to leave it here where the cost is visible. Its chain is 10
## files / ~6,800 lines (~340 ms isolated), and it already carries wotr_state and
## wotr_battle, so naming those two here costs nothing extra.
const WotrSessionScript = preload("res://src/wotr/wotr_session.gd")
const WotrStateScript = preload("res://src/wotr/wotr_state.gd")
const WotrBattleScript = preload("res://src/wotr/wotr_battle.gd")

## LAZILY COMPILED, AT THE POINT OF NAVIGATION. Keyed so a caller names an
## intent, not a path, and so `_lazy_script()` can report a missing one by name.
##
## Isolated compile cost and transitive chain of each (measured, warm cache):
##   slice             57 files / 60,421 lines - the whole tactical simulation
##   faction_manifest   3 files / 13,300 lines - reached only by the availability
##                                               sweep, which is off the boot path
##   wotr_screen       26 files / 22,265 lines - includes the 3D strategic map
##   wotr_setup_screen 20 files / 13,714 lines
##   multiplayer_lobby  3 files /  2,515 lines
##   lockstep_session   2 files /  1,713 lines
##   wotr_world                                - probe world for a setup/session
## Loaded on first press rather than preloaded: MY HEROES is a rarely-visited
## page and its script has no bearing on boot.
const MY_HEROES_SCREEN_PATH := "res://src/ui/my_heroes_screen.gd"
const LAZY_SLICE := "slice"
const LAZY_FACTION_MANIFEST := "faction_manifest"
const LAZY_WOTR_SCREEN := "wotr_screen"
const LAZY_WOTR_SETUP_SCREEN := "wotr_setup_screen"
const LAZY_WOTR_WORLD := "wotr_world"
const LAZY_MULTIPLAYER_LOBBY := "multiplayer_lobby"
const LAZY_LOCKSTEP_SESSION := "lockstep_session"
const LAZY_SCRIPT_PATHS := {
	LAZY_SLICE: "res://src/retail_slice/retail_vertical_slice.gd",
	LAZY_FACTION_MANIFEST: "res://src/retail_slice/retail_faction_manifest.gd",
	LAZY_WOTR_SCREEN: "res://src/ui/wotr_screen.gd",
	LAZY_WOTR_SETUP_SCREEN: "res://src/ui/wotr_setup_screen.gd",
	LAZY_WOTR_WORLD: "res://src/wotr/wotr_world.gd",
	LAZY_MULTIPLAYER_LOBBY: "res://src/ui/multiplayer_lobby.gd",
	LAZY_LOCKSTEP_SESSION: "res://src/retail_slice/retail_lockstep_session.gd",
}

const PAGE_MAIN := "main"
const PAGE_SOLO := "solo"
const PAGE_MULTIPLAYER := "multiplayer"
const PAGE_MP_LOBBY := "mp_lobby"
const PAGE_WOTR := "wotr"
## Retail's GAME SETUP screen, which now stands between the WAR OF THE RING
## entry and the strategic map. `PAGE_WOTR` is unchanged and still opens the map
## directly - the round trip and the battle return both go through it.
const PAGE_WOTR_SETUP := "wotr_setup"
const PAGE_OPTIONS := "options"
const PAGE_DEVELOPER := "developer"
const PAGE_STATS := "stats"
const PAGE_MY_HEROES := "my_heroes"

## Skirmish factions: the six BFME2 factions plus RotWK's Angmar. `id` is the
## lowercase source object-id prefix the retail slice resolves through
## RetailFactionManifest; `name` is the player-facing retail label.
const RETAIL_FACTIONS: Array[Dictionary] = [
	{"id": "men", "name": "Men"},
	{"id": "elves", "name": "Elves"},
	{"id": "dwarves", "name": "Dwarves"},
	{"id": "isengard", "name": "Isengard"},
	{"id": "mordor", "name": "Mordor"},
	{"id": "wild", "name": "Goblins"},
	{"id": "angmar", "name": "Angmar"},
]
const RETAIL_MAP_NAME := "Fords of Isen II"
const NOT_CONVERTED_SUFFIX := " (not converted)"
## Selectable retail vertical-slice maps (five-maps pack + host entry map).
const RETAIL_MAP_CHOICES: Array[Dictionary] = [
	{"id": "bfme2.map.fords-of-isen-ii", "name": "Fords of Isen II"},
	{"id": "bfme2.map.rivendell", "name": "Rivendell"},
	{"id": "bfme2.map.mount-doom", "name": "Mount Doom"},
	{"id": "bfme2.map.dagorlad", "name": "Dagorlad"},
	{"id": "bfme2.map.mordor", "name": "Mordor"},
]
## RULES tab values (retail-typical ladders; 1200/1X are the slice's authored
## defaults, so a fresh setup matches the historical slice exactly).
const RULES_RESOURCE_VALUES: Array[int] = [500, 1000, 1200, 2000, 5000, 10000, 50000]
const RULES_DEFAULT_RESOURCES := 1200
const RULES_FACTOR_VALUES: Array[float] = [0.5, 1.0, 2.0, 4.0]
const RULES_DEFAULT_FACTOR := 1.0
## Build Mode default: false = BFME2 freeform placement (byte-identical default).
const RULES_DEFAULT_BUILD_PLOTS_ONLY := false
## BFME2 1.06 house-color rows for the setup's Color dropdowns. Defaults are
## Blue for the player and Red for the AI — the authored slice team colors.
const HOUSE_COLORS: Array[Dictionary] = [
	{"name": "Blue", "color": Color8(45, 77, 172)},
	{"name": "Red", "color": Color8(166, 32, 28)},
	{"name": "Green", "color": Color8(46, 125, 50)},
	{"name": "Yellow", "color": Color8(214, 198, 46)},
	{"name": "Orange", "color": Color8(217, 124, 30)},
	{"name": "Purple", "color": Color8(124, 63, 160)},
	{"name": "Teal", "color": Color8(46, 158, 155)},
	{"name": "Pink", "color": Color8(214, 107, 168)},
]
## AI difficulty tiers, matching the sim's AI_DIFFICULTY_PROFILES (easy/medium/
## hard/brutal/morgoth). Medium is the sim's AI_DEFAULT_DIFFICULTY, so a default
## AI row is byte-identical to the legacy single-AI setup.
const RETAIL_AI_DIFFICULTIES: Array[Dictionary] = [
	{"id": "easy", "name": "Easy"},
	{"id": "medium", "name": "Medium"},
	{"id": "hard", "name": "Hard"},
	{"id": "brutal", "name": "Brutal"},
	{"id": "morgoth", "name": "Morgoth"},
]
const RETAIL_AI_DEFAULT_DIFFICULTY := "medium"
## Simulation team ids assigned to player rows, skipping NEUTRAL_TEAM (2) which
## the sim reserves for capturable/prop owners. Row i takes TEAM_ID_POOL[i].
const NEUTRAL_TEAM_ID := 2
const TEAM_ID_POOL: Array[int] = [0, 1, 3, 4, 5, 6, 7, 8]
const CONTROLLER_HUMAN := "human"
const CONTROLLER_AI := "ai"

## Retail shell bar (REF-07): six stone caps along the bottom edge in the retail
## order TUTORIALS / SOLO PLAY / MULTIPLAYER / OPTIONS / MY HEROES / QUIT. Each
## carries the hover tooltip retail shows (REF-06 documents the QUIT one
## verbatim); entries whose feature does not exist in Open BFME are present but
## disabled with the honest reason, never a button that silently does nothing.
const BAR_TOOLTIPS := {
	"tutorials": "Guided tutorial missions",
	"solo": "Play by yourself against the computer",
	"multiplayer": "Play against other people over a network",
	"options": "Change your audio and video settings",
	"my_heroes": "Create and manage custom heroes",
	"quit": "Quit to desktop",
}
## Upward flyout contents (REF-02 SOLO PLAY, REF-04 TUTORIALS, REF-05 OPTIONS).
## Retail's row set is reproduced in full; `enabled` reflects what Open BFME has
## actually converted, and every disabled row states why.
const TUTORIALS_FLYOUT_ITEMS: Array = [
	{"id": "basic", "label": "BASIC TUTORIAL", "enabled": false,
		"tooltip": "No tutorial mission scripting has been converted yet"},
	{"id": "advanced", "label": "ADVANCED TUTORIAL", "enabled": false,
		"tooltip": "No tutorial mission scripting has been converted yet"},
	{"id": "wotr", "label": "WAR OF THE RING TUTORIAL", "enabled": false,
		"tooltip": "No tutorial mission scripting has been converted yet; the War of the Ring campaign layer itself is playable from SOLO PLAY"},
]
## SOLO PLAY rows OTHER than WAR OF THE RING, whose availability is decided at
## runtime by the living-world document search — see `_solo_flyout_items()`.
const SOLO_SKIRMISH_ITEM := {"id": "skirmish", "label": "SKIRMISH", "enabled": true,
	"tooltip": "Set up a skirmish against the computer"}
const SOLO_TAIL_ITEMS: Array = [
	{"id": "evil_campaign", "label": "EVIL CAMPAIGN", "enabled": false,
		"tooltip": "No campaign missions have been converted"},
	{"id": "good_campaign", "label": "GOOD CAMPAIGN", "enabled": false,
		"tooltip": "No campaign missions have been converted"},
	{"id": "load_game", "label": "LOAD GAME", "enabled": false,
		"tooltip": "Saved games exist in the simulation but no load browser is wired into the shell yet"},
]
const OPTIONS_FLYOUT_ITEMS: Array = [
	{"id": "settings", "label": "SETTINGS", "enabled": true,
		"tooltip": "Change your audio and video settings"},
	{"id": "custom_settings", "label": "CUSTOM SETTINGS", "enabled": false,
		"tooltip": "The per-detail graphics sliders (REF-15) are not implemented; SETTINGS exposes the quality preset instead"},
	{"id": "credits", "label": "CREDITS", "enabled": false,
		"tooltip": "No credits screen has been authored yet"},
]
## Content-pack ids a converted retail shell backdrop would register under in a
## pack's uiManifest. No converted pack ships one today, so the procedural
## Atmosphere drawing stands in; the first pack to publish one takes over
## automatically. Nothing here copies retail art into the repository.
const BACKDROP_IMAGE_IDS: Array[String] = [
	"shellmapbackdrop", "mainmenubackdrop", "shellbackdrop", "mainmenu",
]
## ProjectSettings keys that name this build. Read, never written here: the
## version belongs to one place (game/project.godot) so a playtester filing a
## report and the shell they were looking at cannot disagree.
const VERSION_SETTING := "application/config/version"
const BUILD_ID_SETTING := "application/config/build_id"
const BuildInfoScript = preload("res://src/core/build_info.gd")
## Original OpenBFME menu atmosphere stills (not retail shellmaps). When present
## under res:// they replace the procedural Atmosphere drawing. Cycle is stable
## per process so a single session does not flash between variants.
const LOCAL_MENU_BACKDROPS: Array[String] = [
	"res://data/base/assets/ui/menu/backdrop_gorge_dawn.png",
	"res://data/base/assets/ui/menu/backdrop_fortress_dusk.png",
	"res://data/base/assets/ui/menu/backdrop_misty_pass.png",
	"res://data/base/assets/ui/menu/backdrop_argonath_haze.png",
	"res://data/base/assets/ui/menu/backdrop_rivendell_vale.png",
	"res://data/base/assets/ui/menu/backdrop_mordor_gate.png",
]
const BACKDROP_CYCLE_SECONDS := 12.0

@onready var center: Control = $Center
@onready var backdrop_art: TextureRect = $BackdropArt
@onready var tutorials_btn: Button = $Center/Tutorials
@onready var solo_btn: Button = $Center/Solo
@onready var multiplayer_btn: Button = $Center/Multiplayer
@onready var options_btn: Button = $Center/Options
@onready var my_heroes_btn: Button = $Center/MyHeroes
@onready var quit_btn: Button = $Center/Quit
## WAR OF THE RING's canonical menu entry. It is NOT on the bottom bar — the
## retail shell has no such cap and the player reaches the campaign through the
## SOLO PLAY flyout — but it remains the single object that carries the entry's
## state (enabled / label / the reason it is shut). `_refresh_wotr_entry()`
## writes it and mirrors it onto the flyout row, so the two can never drift, and
## the round-trip runner asserts against this one node rather than against
## whichever widget the shell happens to render this month.
@onready var wotr_btn: Button = $Center/WarOfTheRing

@onready var solo_flyout: Panel = $Center/SoloFlyout
@onready var multiplayer_flyout: Panel = $Center/MultiplayerFlyout
@onready var stats_screen: Panel = $Center/StatsScreen

@onready var options_screen = $Center/OptionsScreen

@onready var developer_frame: Panel = $Center/DeveloperFrame
@onready var developer_heading: Label = $Center/DeveloperHeading
@onready var developer_back_btn: Button = $Center/DeveloperBack
@onready var developer_access_btn: Button = $DeveloperAccess
@onready var status: Label = $Center/Status

var current_page := PAGE_MAIN
## Where `options_screen.closed` returns to. See `_open_options()`.
var _options_return_page := PAGE_MAIN
var _skirmish_availability: Dictionary = {}
var _skirmish_map_notes: Dictionary = {}
## Mounted catalog map rows for the current sweep (cached so arm/build agree).
var _skirmish_map_choice_rows: Array[Dictionary] = []
var _nav_diamonds: Control
var _slice_probe_instance = null
var _shell_font: Font = null
var _content_db: Node
var _launch_in_progress := false
var _game_state: Node
## GAME LOBBY panel (built in _ready over the NETWORK flyout's rectangle) and
## the pre-game lockstep session it drives. The session lives here only between
## a successful host/join and the lobby's launch/leave; the lobby's _process
## polls it — the menu never does.
var multiplayer_lobby: Panel
var _lobby_session
## WAR OF THE RING. The screen is built over the SOLO flyout's rectangle; the
## session is the live strategic campaign, and `_wotr_unavailable_reason` is the
## honest sentence shown when no living-world document can be found. When that
## reason is non-empty the menu entry REFUSES rather than opening an empty map -
## a War of the Ring button that led to a fabricated Middle-earth would be
## exactly the silent fallback this project has been removing.
## The Create-a-Hero front end, built on first press of MY HEROES.
var my_heroes_screen: Panel
var wotr_screen: Panel
## Retail's GAME SETUP screen. It CHOOSES; `WotrSession` still decides.
var wotr_setup_screen: Panel
## Upward shell flyouts keyed by their anchor button's bar id.
var _shell_flyouts: Dictionary = {}
## Retail APT shell presentation layer; null until a mounted pack ships the
## `shellScene` bundle. The hand-built chrome stays authoritative otherwise.
var _shell_apt_runtime: Control = null
var _shell_apt_metadata: Dictionary = {}
var _wotr_session = null
## Compiled-on-demand navigation scripts, keyed by LAZY_* id, and the named
## reason each one that FAILED could not be compiled. See `_lazy_script()`.
var _lazy_scripts: Dictionary = {}
var _lazy_script_failures: Dictionary = {}
## FAULT-INJECTION SEAM, written only by tests (`set_lazy_script_path_for_test`).
##
## It exists because the thing that has to be proven cannot be proven any other
## way. Moving these compiles from `preload` to navigation time moved their
## failure from startup to navigation, and the whole point of the checking below
## is that such a failure stays LOUD. A test that only asserts the happy path
## proves nothing about that; a test that deletes a real script to force the
## failure would break every other runner in the suite. So the path is
## redirectable, per instance, and nothing in the shell's own code ever writes it.
var _lazy_path_overrides: Dictionary = {}
## Lazy ids whose compile has been handed to the background resource loader and
## not yet collected. See `_warm_lazy_script()`.
var _lazy_warm_requested: Dictionary = {}
var _wotr_unavailable_reason := ""
var _wotr_document: Dictionary = {}
var _wotr_document_path := ""
var _wotr_document_source := ""
## The one Create-a-Hero document chosen on the WotR setup surface. Empty means
## the human seat deliberately brings no created hero.
var _wotr_selected_hero_document := ""
## Local menu stills that are actually present on disk, for cycle order.
var _local_backdrop_paths: Array[String] = []
var _local_backdrop_index := 0
var _local_backdrop_timer := 0.0
var _backdrop_fade: TextureRect = null
var _backdrop_fading := false


func _ready() -> void:
	# Closes the gap between the last autoload and this scene: loading boot.tscn
	# and COMPILING this script's preload chain. That chain is now 18 files /
	# ~10,900 lines - only what the menu draws - where it used to be 91 files /
	# ~90,100 lines, because everything reachable by NAVIGATION is compiled at the
	# navigation instead. That cost lands here, not in any autoload.
	BootProfile.mark("main_scene_load+preload_compile")
	# Guard: any scene arriving here must find an unpaused tree (a pause-open
	# exit from the slice must never leave the menu frozen).
	get_tree().paused = false
	_build_version_label()
	_content_db = get_node_or_null("/root/ContentDB")
	_game_state = get_node_or_null("/root/GameState")
	if _content_db == null or _game_state == null:
		push_error("OpenBFME menu requires the ContentDB and GameState autoloads.")
		return
	_shell_font = _load_retail_font()
	BootProfile.mark("menu:load_retail_font")
	theme = ThemeScript.create_theme(_shell_font)
	BootProfile.mark("menu:create_theme")
	# THE GAME LOBBY, THE WAR OF THE RING SCREEN AND RETAIL'S GAME SETUP SCREEN
	# ARE NOT BUILT HERE ANY MORE. All three are hidden on the front page and
	# unreachable without a navigation, and constructing them here forced their
	# scripts - ~38,500 lines between them, including the 3D strategic map view -
	# to be compiled before the menu could draw. They are built by
	# `_ensure_multiplayer_lobby()`, `_ensure_wotr_screen()` and
	# `_ensure_wotr_setup_screen()` at the point of navigation, and each of those
	# fails LOUDLY and by name rather than leaving a control that does nothing.
	#
	# DEFERRED OFF BOOT, NEVER SKIPPED. Populating the skirmish options runs the
	# slice's real per-faction roster classification and per-map resolution for
	# seven factions and five maps - a MEASURED 4,341 ms originally, 1.6-2.8 s
	# today, and it also owns the ONLY `RetailVerticalSlice.new()` the shell makes,
	# so it now carries that class's ~60k-line compile too. All of it fills the
	# SOLO PLAY flyout, which is hidden on the front page and cannot be looked at
	# until the player opens it.
	#
	# It used to be a single one-shot on the next `process_frame`, which put the
	# whole sweep inside the frame BEFORE the menu's first drawn one - measured
	# ordering: `menu:skirmish_options` at 13,362 ms, `menu_first_frame` 14 ms
	# later. The menu was therefore not interactive until the sweep finished. It is
	# now stepped one unit of work per idle frame from `_process`, starting after
	# the menu has been presented, so no single frame carries the whole cost.
	#
	# THE WORK IS NOT OPTIONAL AND IS NOT WEAKENED. The worker evaluates every
	# faction and map into private result dictionaries. The menu publishes both
	# complete tables together on the main thread; navigation awaits that publish,
	# while synchronous launch readers fail closed with a loading reason.
	_arm_skirmish_sweep()
	# Start the slice's compile on the loader thread NOW, so it overlaps the few
	# frames the shell needs to present itself and the loading screen needs to
	# fade. By the time the stepped sweep needs it, most or all of a measured
	# 3.1-3.9 s compile has already happened off the main thread. See
	# `_warm_lazy_script()` - this only changes WHEN the compile runs, never
	# whether its result is checked.
	_warm_lazy_script(LAZY_SLICE)
	_warm_lazy_script(LAZY_FACTION_MANIFEST)
	# The living-world search runs BEFORE the flyouts are built so the SOLO PLAY
	# list is constructed with War of the Ring's real state, not a placeholder
	# that a later refresh has to correct.
	_locate_wotr_document()
	BootProfile.mark("menu:locate_wotr_document")
	_apply_converted_backdrop()
	BootProfile.mark("menu:apply_converted_backdrop")
	_configure_shell_apt_presentation()
	BootProfile.mark("menu:configure_shell_apt")
	_build_shell_flyouts()
	_connect_actions()
	options_screen.configure({"font": _shell_font})
	# OPTIONS COMES BACK TO WHERE IT WAS OPENED FROM, not to the front page. It used
	# to always return to PAGE_MAIN, which was harmless while OPTIONS could only be
	# reached from the bottom bar - and became a campaign-destroying bug the moment
	# the War of the Ring pause shell gained an OPTIONS capsule, because leaving the
	# settings screen would have dropped the player on the main menu with their
	# strategic session still seated behind it. `_options_return_page` is set by
	# `_open_options()` and read here.
	options_screen.closed.connect(func(_applied: bool) -> void: _show_page(_options_return_page))
	_build_nav_diamonds()
	_refresh_wotr_entry()
	_show_page(PAGE_MAIN)
	BootProfile.mark("menu:flyouts+chrome+show_page")
	# A campaign returning from its tactical battle resumes on the strategic map,
	# with the result applied, rather than dropping the player on the front page
	# with a battle silently still in flight.
	_resume_wotr_after_battle()
	# Stored display/graphics settings apply from the first frame onward so the
	# shell and the slice share one window/quality state.
	call_deferred("_apply_boot_settings")
	# MENU MUSIC. One call, and deliberately the whole of the menu's part in it:
	# WHICH track plays is retail's declaration (miscaudio.ini's
	# `LowLODShellMusic`, resolved through MusicDirector), and the handoff to a
	# match is taken by RetailSliceAudio.configure(). Deferred so a missing or
	# malformed music pack cannot cost the shell its first frame; it fails
	# closed and silent, and says why in GameAudio.shell_music_diagnostics.
	call_deferred("_start_shell_music")
	status.text = "Content: %d units, %d buildings, %d factions, %d maps, %d powers" % [
		(_content_db.get("units") as Dictionary).size(), (_content_db.get("buildings") as Dictionary).size(),
		(_content_db.get("factions") as Dictionary).size(), (_content_db.get("maps") as Dictionary).size(),
		(_content_db.get("powers") as Dictionary).size()
	]


## First _process tick after the MENU's _ready. Deliberately NOT called
## "first_frame": that name belongs to startup_boot.gd's mark, which is the frame
## the player first sees anything at all, and by the time this one fires the
## loading surface has been up for several seconds. The gap between the two is
## the shell's own load-and-build cost.
var _first_frame_marked := false
## Idle frames since the menu entered the tree. The availability sweep starts
## stepping at SKIRMISH_SWEEP_FIRST_FRAME rather than immediately: the startup
## loading surface fades over two frames after the shell's first (see
## startup_boot.gd `_fade_screen_after_shell_frame`), and a heavy step landing
## inside those two would push `shell_visible` - the moment the player actually
## has the menu - back out again.
const SKIRMISH_SWEEP_FIRST_FRAME := 3
var _menu_frames := 0


func _process(delta: float) -> void:
	_menu_frames += 1
	_tick_backdrop_cycle(delta)
	if not _first_frame_marked:
		_first_frame_marked = true
		BootProfile.mark("menu_first_frame")
		return
	if _menu_frames < SKIRMISH_SWEEP_FIRST_FRAME:
		return
	var sweep_frame_started := Time.get_ticks_usec()
	# THE LIST FIRST, THE VALIDATION AFTER. The instant build only reads pack
	# manifests and ContentDB registries, so it drains in a handful of budgeted
	# frames; the full availability sweep is a background warmer that the menu
	# never waits on.
	var sweep_did_main_thread_work := _step_skirmish_instant_build(sweep_frame_started)
	if not sweep_did_main_thread_work:
		_step_skirmish_sweep()
	if sweep_did_main_thread_work:
		var elapsed := Time.get_ticks_usec() - sweep_frame_started
		if elapsed > _skirmish_sweep_worst_frame_usec:
			_skirmish_sweep_worst_frame_usec = elapsed
			_skirmish_sweep_worst_frame_name = _skirmish_current_main_work_name
	_update_skirmish_busy_label()
	if _skirmish_options_ready and (_skirmish_sweep_complete or _skirmish_sweep_failed):
		# Nothing else in this menu needs a per-frame tick.
		set_process(false)


# --- lazy navigation scripts -------------------------------------------------
#
# THE DANGER THIS CODE EXISTS TO CLOSE. With `preload`, a missing or broken
# script was a COMPILE error at startup: loud, early, and impossible to ship past.
# Moving those compiles to navigation time moves that failure with them, and the
# obvious lazy-loading shape - `var s = load(p); if s: s.new()` - turns it into a
# button that silently does nothing, which is exactly the failure mode this
# repository keeps deleting.
#
# So NOTHING here fails open. Every lazy load is checked, a failure is pushed as
# an engine error AND recorded under a named reason, and every caller is
# responsible for putting that reason on a surface the player can read: the WAR
# OF THE RING entry adopts it as its unavailable reason (so the button disables
# and says why), the NETWORK panel prints it into its status line, and the
# developer status label carries it as a fallback. `lazy_script_failure()` exposes
# it so a runner can assert the reason exists rather than assert a button moved.


func _lazy_script(key: String):
	## The compiled script for a LAZY_* id, or null with a named failure recorded.
	## Cached per menu instance: navigating to the same screen twice compiles once.
	if _lazy_scripts.has(key):
		return _lazy_scripts[key]
	var path := String(_lazy_path_overrides.get(key, LAZY_SCRIPT_PATHS.get(key, "")))
	if path == "":
		_record_lazy_failure(key, "no script path is registered for lazy id '%s'" % key)
		return null
	# A warm request already in flight is COLLECTED here rather than raced:
	# `load_threaded_get` blocks until the loader thread is done, so a reader that
	# arrives mid-warm still returns with the real script and never with null.
	var resource: Resource = null
	var warm_path := String(_lazy_warm_requested.get(key, ""))
	_lazy_warm_requested.erase(key)
	if warm_path == path:
		resource = ResourceLoader.load_threaded_get(path)
		if resource == null:
			# A failed threaded compile is cached by the engine, so retrying with
			# load() would hand back null again. Say so by name instead.
			_record_lazy_failure(key, "%s failed to compile on the background loader thread" % path)
			return null
	else:
		resource = load(path)
	if resource == null:
		_record_lazy_failure(key, "%s could not be loaded" % path)
		return null
	if not (resource is Script):
		_record_lazy_failure(key, "%s loaded as %s, not a Script" % [path, resource.get_class()])
		return null
	_lazy_scripts[key] = resource
	_lazy_script_failures.erase(key)
	return resource


func _record_lazy_failure(key: String, reason: String) -> void:
	## Loud, named, and remembered. push_error puts it in the log and in the
	## debugger; the recorded sentence is what a surface shows the player.
	var sentence := "the '%s' script could not be compiled: %s" % [key.replace("_", " "), reason]
	_lazy_script_failures[key] = sentence
	push_error("[OpenBFME menu] %s" % sentence)
	if status != null:
		status.text = sentence


func lazy_script_failure(key: String) -> String:
	## "" when the script compiled (or has not been asked for yet), else the named
	## reason. Exposed for runners: a failed lazy load must be observable as a
	## sentence, never as a control that quietly does nothing.
	return String(_lazy_script_failures.get(key, ""))


func _warm_lazy_script(key: String) -> void:
	## Hand a lazy script's compile to the BACKGROUND RESOURCE LOADER.
	##
	## This is what makes the availability sweep cheap. The sweep's one
	## `RetailVerticalSlice.new()` drags in 57 files / ~60,400 lines, measured at
	## 3.1-3.9 s, and on the main thread that is a single frozen frame - with the
	## menu already up, which is the worst possible moment for it. Requested here
	## instead, it compiles on another thread while the player looks at a menu that
	## keeps drawing and taking input.
	##
	## THIS WAS NOT POSSIBLE BEFORE. `startup_boot.gd` documents the measured Godot
	## limitation: a GDScript that `preload`s a `.gdshader` cannot be compiled on
	## the loader thread. `wotr_map_view.gd` preloaded four, and the whole shell
	## depended on it. Those are runtime `load()` now, and nothing in the chains
	## below preloads a non-script resource - verified, not assumed.
	##
	## FAIL-CLOSED, NOT FAIL-OPEN. Nothing here decides anything: the warm only
	## moves WHEN a compile happens. `_lazy_script()` still collects the result,
	## still null-checks it, and still records a named failure - and a threaded
	## compile that fails is reported as such rather than silently retried,
	## because the engine caches the failure and a retry would return null too.
	if _lazy_scripts.has(key) or _lazy_warm_requested.has(key):
		return
	var path := String(_lazy_path_overrides.get(key, LAZY_SCRIPT_PATHS.get(key, "")))
	if path == "":
		return
	if ResourceLoader.load_threaded_request(path) != OK:
		# Not a failure worth reporting: the on-demand `load()` path is still
		# there and will report by name if the script genuinely cannot compile.
		return
	_lazy_warm_requested[key] = path


func _lazy_warm_is_pending(key: String) -> bool:
	## True while the loader thread is still working on `key`. Callers that can
	## afford to wait a frame use this to avoid blocking on `load_threaded_get`.
	if not _lazy_warm_requested.has(key):
		return false
	var status := ResourceLoader.load_threaded_get_status(String(_lazy_warm_requested[key]))
	return status == ResourceLoader.THREAD_LOAD_IN_PROGRESS


func set_lazy_script_path_for_test(key: String, path: String) -> void:
	## Point one lazy id at a different path, so a runner can exercise the FAILURE
	## path for real. Clears any cached script and recorded failure for that id so
	## the next navigation re-resolves it. Tests only - see `_lazy_path_overrides`.
	_lazy_path_overrides[key] = path
	_lazy_scripts.erase(key)
	_lazy_script_failures.erase(key)
	# Any warm request in flight was for the OLD path and must not be collected
	# under the new one.
	_lazy_warm_requested.erase(key)


func lazy_script_is_compiled(key: String) -> bool:
	## Whether this menu has already paid for a lazy screen. The boot runner uses
	## it to pin that navigating twice does not compile twice.
	return _lazy_scripts.has(key)


## True while `_wotr_unavailable_reason` is holding a SCRIPT COMPILE failure
## rather than a living-world document verdict. The two are both honest refusals
## and both belong on the same surface, but only one of them can be withdrawn:
## a document that was not found stays not found, whereas a script that compiles
## on a later attempt means the earlier refusal no longer applies and the real
## document verdict must be restored rather than left buried under a stale
## sentence.
var _wotr_reason_is_script_failure := false


func _adopt_wotr_script_failure(key: String) -> void:
	_wotr_unavailable_reason = lazy_script_failure(key)
	_wotr_reason_is_script_failure = true
	_refresh_wotr_entry()


func _clear_wotr_script_failure() -> void:
	if not _wotr_reason_is_script_failure:
		return
	_wotr_reason_is_script_failure = false
	# Re-ask the real question rather than blanking the reason: the campaign may
	# still be unavailable for its own, document-shaped reason.
	_locate_wotr_document()
	_refresh_wotr_entry()


func _ensure_wotr_screen() -> bool:
	## Builds the WAR OF THE RING strategic screen the first time the player
	## navigates to it. 26 files / ~22,300 lines including the 3D strategic map
	## view - none of it needed to draw a menu, all of it needed the moment this
	## page opens.
	##
	## Returns false with `_wotr_unavailable_reason` set to the named compile
	## failure, which `_refresh_wotr_entry()` then writes onto BOTH the WAR OF THE
	## RING button and the SOLO PLAY flyout row: the entry disables and states why,
	## instead of being a live control that opens nothing.
	if wotr_screen != null:
		return true
	var script = _lazy_script(LAZY_WOTR_SCREEN)
	if script == null:
		_adopt_wotr_script_failure(LAZY_WOTR_SCREEN)
		return false
	_clear_wotr_script_failure()
	# THE WHOLE WINDOW, NOT THE SOLO FLYOUT'S RECTANGLE.
	#
	# This used to be `position = solo_flyout.position; size = solo_flyout.size`,
	# and that one pair of lines is why the owner said "there is no way to have
	# this fullscreen inside of the game engine itself". The strategic screen is
	# the GAME - a full-bleed 3D Middle-earth with HUD islands floating over it -
	# and it was being seated in the 1864x790 rectangle the SOLO PLAY list occupies,
	# inside the shell's backdrop, under the shell's "OPEN BFME" title, with the
	# version line and the engine line framing it. Every visual review of this
	# screen was run against `wotr_capture_runner`, which built the screen
	# STANDALONE at full window size, so nothing ever photographed what the player
	# was actually handed.
	#
	# FULL_RECT anchors rather than a copied rectangle: the page must follow the
	# window when it is resized or put fullscreen, and a copied rectangle cannot.
	# `_show_page` hides the shell's own chrome while this page is up (see
	# `_shell_chrome_nodes`), so the screen is the only thing on the glass.
	wotr_screen = script.new()
	wotr_screen.name = "WotrScreen"
	wotr_screen.visible = false
	# NO `FlyoutPanel` VARIATION. That variation is the shell's thorn-bordered list
	# panel; drawn behind a full-bleed map it contributes a border around the edge
	# of the window and nothing else. The screen paints its own ground.
	wotr_screen.add_theme_stylebox_override("panel", StyleBoxEmpty.new())
	center.add_child(wotr_screen)
	# AFTER the reparent, so the preset is resolved against `center`'s rectangle
	# rather than against nothing, and so the first `_relayout()` the screen runs
	# is computed for the window it is actually in.
	wotr_screen.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	wotr_screen.back_requested.connect(func() -> void: _show_page(PAGE_MAIN))
	wotr_screen.battle_committed.connect(_on_wotr_battle_committed)
	# The pause shell's OPTIONS capsule. It opens the shell's ONE options screen
	# over the campaign and comes straight back to it - see `_open_options()`.
	wotr_screen.options_requested.connect(func() -> void: _open_options(PAGE_WOTR))
	BootProfile.mark("menu:wotr_screen_construct")
	return true


func _ensure_wotr_setup_screen() -> bool:
	## Retail's GAME SETUP screen, built on the same rectangle when the player
	## navigates to it. 20 files / ~13,700 lines.
	if wotr_setup_screen != null:
		return true
	var script = _lazy_script(LAZY_WOTR_SETUP_SCREEN)
	if script == null:
		_adopt_wotr_script_failure(LAZY_WOTR_SETUP_SCREEN)
		return false
	_clear_wotr_script_failure()
	wotr_setup_screen = script.new()
	wotr_setup_screen.name = "WotrSetupScreen"
	wotr_setup_screen.position = solo_flyout.position
	wotr_setup_screen.size = solo_flyout.size
	wotr_setup_screen.visible = false
	wotr_setup_screen.theme_type_variation = "FlyoutPanel"
	center.add_child(wotr_setup_screen)
	wotr_setup_screen.back_requested.connect(func() -> void: _show_page(PAGE_MAIN))
	wotr_setup_screen.play_requested.connect(_on_wotr_setup_play)
	BootProfile.mark("menu:wotr_setup_screen_construct")
	return true


func ensure_my_heroes_screen() -> bool:
	## The Create-a-Hero front end, built on the SOLO PLAY rectangle the first
	## time MY HEROES is pressed.
	##
	## Public, and built even when no pack carries the class table: the screen's
	## job in that case is to NAME the missing content and the command that
	## produces it. A shell that silently refused to open would leave the player
	## with the same dead button they had before.
	if my_heroes_screen != null:
		_layout_my_heroes_screen()
		return true
	var script = load(MY_HEROES_SCREEN_PATH)
	if script == null:
		push_error("MY HEROES: failed to load %s" % MY_HEROES_SCREEN_PATH)
		return false
	my_heroes_screen = script.new()
	my_heroes_screen.name = "MyHeroesScreen"
	my_heroes_screen.visible = false
	# NO `FlyoutPanel` VARIATION, for the same reason the strategic screen refuses
	# it: that variation is the shell's thorn-bordered list panel, and drawn behind
	# a full-window screen it contributes a green rectangle around the edge of the
	# window and nothing else. Create-a-Hero is a screen, not a flyout.
	my_heroes_screen.add_theme_stylebox_override("panel", StyleBoxEmpty.new())
	my_heroes_screen.mouse_filter = Control.MOUSE_FILTER_STOP
	center.add_child(my_heroes_screen)
	_layout_my_heroes_screen()
	my_heroes_screen.back_requested.connect(func() -> void: _show_page(PAGE_MAIN))
	my_heroes_screen.configure(_cah_system_runtime())
	BootProfile.mark("menu:my_heroes_screen_construct")
	return true


func _layout_my_heroes_screen() -> void:
	## THE WHOLE WINDOW, ANCHORED - not a copy of the SOLO flyout's rectangle.
	##
	## Copying that rectangle is what put Create-a-Hero on screen as a bordered
	## panel inset from the left and top and 180px short of the bottom, floating
	## over a dimmed main menu: a menu inside a menu, with the hero preview given
	## half of it and every control squeezed into what was left. FULL_RECT anchors
	## rather than a copied rectangle for the same reason the strategic screen uses
	## them - the screen must follow the window when it is resized or put
	## fullscreen, and a copied rectangle cannot.
	if my_heroes_screen == null:
		return
	my_heroes_screen.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	if my_heroes_screen.has_method("set_backdrop_texture"):
		my_heroes_screen.set_backdrop_texture(backdrop_art.texture if backdrop_art != null else null)


func _cah_system_runtime() -> Dictionary:
	## The mounted Create-a-Hero class table, or {} when nothing provides one.
	## Read fresh on every open rather than cached: content selection can change
	## between visits to the shell, and a cached empty table would keep the
	## screen dead after the player mounted a pack that fixes it.
	if _content_db == null:
		return {}
	var value: Variant = _content_db.get("cah_system_runtime")
	return value as Dictionary if typeof(value) == TYPE_DICTIONARY else {}


func _cah_system_snapshot() -> Dictionary:
	## A PRIVATE COPY of the class table, taken on the main thread, for the
	## skirmish sweep to carry onto its worker. The worker may not reach an
	## autoload - Godot refuses node lookups off the main thread - so without
	## this the slice it builds classifies every faction with no created heroes
	## and the availability answer disagrees with the match the player then gets.
	return _cah_system_runtime().duplicate(true)


func _on_my_heroes_pressed() -> void:
	_close_shell_flyouts()
	if not ensure_my_heroes_screen():
		status.text = "MY HEROES is unavailable: the screen script failed to compile."
		return
	_layout_my_heroes_screen()
	my_heroes_screen.configure(_cah_system_runtime())
	my_heroes_screen.visible = true
	_show_page(PAGE_MY_HEROES)
	status.text = "Create-a-Hero"


func ensure_multiplayer_lobby() -> bool:
	## The GAME LOBBY panel, on the NETWORK flyout's rectangle, built when a
	## host/join is actually attempted. Public because the multiplayer runners
	## drive the lobby directly and must be able to reach it without a live
	## socket; nothing in the shell's own paths depends on that.
	##
	## Returns false with the named reason recorded; `_launch_multiplayer()` puts
	## it straight into the NETWORK panel's status line, so a failed compile is a
	## refusal the player can read rather than a host button that does nothing.
	if multiplayer_lobby != null:
		return true
	var script = _lazy_script(LAZY_MULTIPLAYER_LOBBY)
	if script == null:
		return false
	multiplayer_lobby = script.new()
	multiplayer_lobby.name = "MultiplayerLobby"
	multiplayer_lobby.position = multiplayer_flyout.position
	multiplayer_lobby.size = multiplayer_flyout.size
	multiplayer_lobby.visible = false
	center.add_child(multiplayer_lobby)
	multiplayer_lobby.launch_confirmed.connect(_on_lobby_launch_confirmed)
	multiplayer_lobby.leave_requested.connect(_on_lobby_leave)
	BootProfile.mark("menu:multiplayer_lobby_construct")
	return true


func _slice_script():
	## The tactical slice class. Reached ONLY for `new()` - every slice constant
	## the shell needs is in `SliceIds`, which costs nothing to compile.
	return _lazy_script(LAZY_SLICE)


func _faction_manifest_script():
	return _lazy_script(LAZY_FACTION_MANIFEST)


func _new_wotr_world():
	## A bare probe world for the setup screen and the session start. Runtime
	## load() rather than preload for the same reason as everything else here, and
	## null-checked rather than assumed: this used to be an unchecked
	## `load(...).new()`, which would have crashed on a missing file.
	var script = _lazy_script(LAZY_WOTR_WORLD)
	if script == null:
		return null
	return script.new()


func _apply_boot_settings() -> void:
	options_screen.apply_stored_settings()


func _start_shell_music() -> void:
	## Ask the shell audio owner for the authored menu playlist. Everything
	## about WHICH tracks those are, whether they shuffle and whether they loop
	## is read from the installed music pack (retail's MiscAudio + Multisound
	## declarations); this call site only says "the menu is up".
	var shell_audio: Node = get_node_or_null("/root/GameAudio")
	if shell_audio == null or not shell_audio.has_method("set_music_state"):
		return
	shell_audio.call("set_music_state", "shell")
	var diagnostics: Variant = shell_audio.get("shell_music_diagnostics")
	if typeof(diagnostics) == TYPE_ARRAY and not (diagnostics as Array).is_empty():
		# Named, never substituted: a silent menu says why it is silent.
		print("MENU_MUSIC_GAP %s" % ", ".join(PackedStringArray(diagnostics as Array)))


func _exit_tree() -> void:
	# The worker is bound to this menu's validation helpers. Cancel between
	# validations and join before the Node can be freed; no deferred callback can
	# ever target a dead menu.
	if _skirmish_worker_task_id >= 0:
		_skirmish_worker_cancel_mutex.lock()
		_skirmish_worker_cancelled = true
		_skirmish_worker_cancel_mutex.unlock()
		WorkerThreadPool.wait_for_task_completion(_skirmish_worker_task_id)
		_skirmish_worker_task_id = -1
	# The classification/map-resolution probe never enters the tree, so it
	# cannot rely on automatic cleanup.
	if _slice_probe_instance != null:
		_slice_probe_instance.free()
		_slice_probe_instance = null
	# A menu freed with a live pre-game session (external scene change) must
	# release the socket; the peer receives the notified disconnect.
	if _lobby_session != null:
		_lobby_session.close()
		_lobby_session = null


func _load_retail_font() -> Font:
	## The converted packs ship Albertus MT under assets/ui/palantir/fonts.
	## Using it at runtime keeps the shell on converted retail art without
	## copying font bytes into the repository; when no pack carries the face,
	## the theme simply keeps Godot's default font (styled UI, not a fake).
	for pack_root in _font_pack_root_candidates():
		var fonts_dir := pack_root.path_join("assets/ui/palantir/fonts")
		var dir := DirAccess.open(fonts_dir)
		if dir == null:
			continue
		for file in dir.get_files():
			if file.get_extension() != "otf" and file.get_extension() != "ttf":
				continue
			var font := FontFile.new()
			if font.load_dynamic_font(fonts_dir.path_join(file)) == OK:
				return font
	return null


func _font_pack_root_candidates() -> Array[String]:
	var roots: Dictionary = {}
	var member := _content_db.call("get_bundle_object", SliceIds.SOLDIER_OBJECT_ID) as Dictionary
	roots[String(member.get("_pack_root", ""))] = true
	for registry in [_content_db.call("get_playable_unit_runtimes"), _content_db.call("get_playable_structure_runtimes")]:
		for document_value in (registry as Dictionary).values():
			roots[String((document_value as Dictionary).get("_pack_root", ""))] = true
	var ordered: Array[String] = []
	for root_value in roots.keys():
		var root := String(root_value)
		if root != "":
			ordered.append(root)
	ordered.sort()
	return ordered


func _build_nav_diamonds() -> void:
	## Retail marks every bar button that opens a sub-surface (REF-07: TUTORIALS
	## / SOLO PLAY / MULTIPLAYER / OPTIONS carry one; MY HEROES and QUIT act
	## immediately and do not).
	_nav_diamonds = NavDiamondsScript.new()
	_nav_diamonds.name = "NavDiamonds"
	_nav_diamonds.z_index = 3
	add_child(_nav_diamonds)
	var nav_buttons: Array[Button] = [tutorials_btn, solo_btn, multiplayer_btn, options_btn]
	_nav_diamonds.watch(nav_buttons)


func _bar_buttons() -> Array[Button]:
	return [tutorials_btn, solo_btn, multiplayer_btn, options_btn, my_heroes_btn, quit_btn]


func _configure_shell_apt_presentation() -> bool:
	## Retail's shell is an APT movie (MainMenu.apt + the MenuExport library),
	## exactly like the in-game palantir. When a mounted pack ships the cooked
	## `shellScene` bundle we present those retail triangles; when no pack does
	## -- the state of every pack that predates the shell ingress lane -- the
	## hand-built chrome below stays authoritative. This is additive: a missing
	## or rejected contract never removes the fallback shell.
	_shell_apt_metadata = {}
	if _shell_apt_runtime != null:
		_shell_apt_runtime.queue_free()
		_shell_apt_runtime = null
	var runtime: Control = ShellAptRuntimeScript.new()
	runtime.name = "RetailShellApt"
	runtime.set_anchors_preset(Control.PRESET_FULL_RECT)
	runtime.mouse_filter = Control.MOUSE_FILTER_IGNORE
	runtime.visible = false
	# Behind the authored chrome so a partial static subset never hides the
	# working shell; it is promoted to the front only once it presents.
	add_child(runtime)
	move_child(runtime, 0)
	var presented := false
	for pack_root in _font_pack_root_candidates():
		if not runtime.call("configure_from_pack", pack_root, true):
			continue
		if not bool(runtime.get("presentation_ready")):
			continue
		presented = true
		_shell_apt_metadata = runtime.call("runtime_metadata") as Dictionary
		break
	if not presented:
		runtime.queue_free()
		return false
	_shell_apt_runtime = runtime
	runtime.visible = true
	# Ordering, deliberately: the retail draws sit above the backdrop and below
	# `Center`. Two things force that. The retail backdrop is a live 3D shellmap
	# behind a native View3D gadget with no APT payload, so the authored
	# backdrop must stay underneath rather than be replaced by nothing; and the
	# cooked contract binds no button action programs (`parityReady` is false),
	# so the authored bar remains the interactive surface. Once the contract
	# reports parity the authored chrome can step aside.
	var center_index := center.get_index()
	move_child(runtime, maxi(0, center_index))
	return true


func shell_apt_metadata() -> Dictionary:
	return _shell_apt_metadata.duplicate(true)


func _apply_converted_backdrop() -> void:
	## Backdrop priority:
	##   1. Converted retail shell backdrop from a mounted pack (private parity)
	##   2. Authored OpenBFME stills under res://data/base/assets/ui/menu/
	##   3. Procedural Atmosphere drawing (Argonath-inspired code art)
	## Nothing in this path copies a retail screenshot into the repository.
	if _try_pack_shell_backdrop():
		return
	if _try_local_menu_backdrop():
		return


func _try_pack_shell_backdrop() -> bool:
	if _content_db == null or not _content_db.has_method("resolve_retail_ui_image_path"):
		return false
	for image_id in BACKDROP_IMAGE_IDS:
		var path := String(_content_db.call("resolve_retail_ui_image_path", image_id))
		if path == "" or not FileAccess.file_exists(path):
			continue
		if _show_backdrop_image_path(path):
			return true
	return false


func _try_local_menu_backdrop() -> bool:
	_local_backdrop_paths.clear()
	for path in LOCAL_MENU_BACKDROPS:
		if ResourceLoader.exists(path):
			_local_backdrop_paths.append(path)
			continue
		var absolute := ProjectSettings.globalize_path(path)
		if FileAccess.file_exists(absolute):
			_local_backdrop_paths.append(absolute)
	if _local_backdrop_paths.is_empty():
		return false
	_local_backdrop_index = 0
	_local_backdrop_timer = 0.0
	return _apply_local_backdrop_index(_local_backdrop_index, false)


func _tick_backdrop_cycle(delta: float) -> void:
	if _local_backdrop_paths.size() < 2 or _backdrop_fading:
		return
	if current_page != PAGE_MAIN:
		return
	if backdrop_art == null or not backdrop_art.visible:
		return
	_local_backdrop_timer += delta
	if _local_backdrop_timer < BACKDROP_CYCLE_SECONDS:
		return
	_local_backdrop_timer = 0.0
	_local_backdrop_index = (_local_backdrop_index + 1) % _local_backdrop_paths.size()
	_apply_local_backdrop_index(_local_backdrop_index, true)


func _apply_local_backdrop_index(index: int, fade: bool) -> bool:
	if index < 0 or index >= _local_backdrop_paths.size():
		return false
	var path := _local_backdrop_paths[index]
	var texture := _load_backdrop_texture(path)
	if texture == null:
		return false
	var atmosphere := get_node_or_null("Atmosphere") as Control
	if atmosphere != null:
		atmosphere.visible = false
	if not fade or backdrop_art.texture == null:
		backdrop_art.texture = texture
		backdrop_art.visible = true
		backdrop_art.modulate = Color.WHITE
		return true
	_crossfade_backdrop(texture)
	return true


func _load_backdrop_texture(path: String) -> Texture2D:
	if path.begins_with("res://") and ResourceLoader.exists(path):
		var resource := load(path)
		if resource is Texture2D:
			return resource as Texture2D
	var absolute := path
	if path.begins_with("res://"):
		absolute = ProjectSettings.globalize_path(path)
	if not FileAccess.file_exists(absolute):
		return null
	var image := Image.load_from_file(absolute)
	if image == null or image.is_empty():
		return null
	return ImageTexture.create_from_image(image)


func _crossfade_backdrop(next_texture: Texture2D) -> void:
	if backdrop_art == null or next_texture == null:
		return
	if _backdrop_fade == null:
		_backdrop_fade = TextureRect.new()
		_backdrop_fade.name = "BackdropArtFade"
		_backdrop_fade.mouse_filter = Control.MOUSE_FILTER_IGNORE
		_backdrop_fade.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
		_backdrop_fade.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		_backdrop_fade.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_COVERED
		backdrop_art.add_sibling(_backdrop_fade)
		# Keep fade under chrome, above base backdrop.
		move_child(_backdrop_fade, backdrop_art.get_index() + 1)
	_backdrop_fading = true
	_backdrop_fade.texture = next_texture
	_backdrop_fade.visible = true
	_backdrop_fade.modulate = Color(1, 1, 1, 0)
	var tween := create_tween()
	tween.tween_property(_backdrop_fade, "modulate:a", 1.0, 1.35)
	tween.tween_callback(func() -> void:
		backdrop_art.texture = next_texture
		backdrop_art.modulate = Color.WHITE
		_backdrop_fade.visible = false
		_backdrop_fading = false
	)


func _show_backdrop_image_path(path: String) -> bool:
	var texture := _load_backdrop_texture(path)
	if texture == null:
		return false
	backdrop_art.texture = texture
	backdrop_art.visible = true
	var atmosphere := get_node_or_null("Atmosphere") as Control
	if atmosphere != null:
		atmosphere.visible = false
	return true


## The SOLO PLAY list, in retail's order. WAR OF THE RING is a real, playable
## row here — it is the strategic campaign this branch ships, not a placeholder —
## and it is enabled exactly when a living-world document was found. When one was
## not, the row STAYS LISTED and carries the search's own sentence, which names
## the pack file it looked for, the environment variable it checked and the
## command that generates a document.
func _solo_flyout_items() -> Array:
	var items: Array = [SOLO_SKIRMISH_ITEM]
	items.append(_wotr_flyout_item())
	items.append_array(SOLO_TAIL_ITEMS)
	return items


func _wotr_flyout_item() -> Dictionary:
	var blocked := _wotr_unavailable_reason != ""
	return {
		"id": "wotr",
		"label": "WAR OF THE RING",
		"enabled": not blocked,
		"tooltip": _wotr_unavailable_reason if blocked else "Play the strategic War of the Ring campaign",
	}


func _build_shell_flyouts() -> void:
	## One upward flyout per bar button that owns a list in retail. They are
	## siblings of the bar buttons inside Center so OpenBFMEShellFlyout's
	## anchor-relative placement resolves in the same coordinate space, and they
	## re-anchor themselves on every viewport resize (no fixed pixel layout).
	_add_shell_flyout("tutorials", tutorials_btn, TUTORIALS_FLYOUT_ITEMS)
	_add_shell_flyout("solo", solo_btn, _solo_flyout_items())
	_add_shell_flyout("options", options_btn, OPTIONS_FLYOUT_ITEMS)


func _add_shell_flyout(bar_id: String, anchor: Button, items: Array) -> void:
	var flyout = ShellFlyoutScript.build(anchor, items)
	center.add_child(flyout)
	flyout.item_selected.connect(_on_shell_flyout_item.bind(bar_id))
	_shell_flyouts[bar_id] = flyout


func shell_flyout(bar_id: String):
	## Exposed so a runner can assert on the shell's real rows rather than on a
	## reconstruction of them.
	return _shell_flyouts.get(bar_id, null)


func _toggle_shell_flyout(bar_id: String) -> void:
	var target = _shell_flyouts.get(bar_id, null)
	var reopen: bool = target != null and not target.visible
	_close_shell_flyouts()
	if reopen:
		target.open()
		_set_nav_active(target.anchor_button)


func _close_shell_flyouts() -> void:
	for flyout in _shell_flyouts.values():
		flyout.visible = false
	_set_nav_active(null)


func _set_nav_active(button: Button) -> void:
	# The active-marker highlight arrived with the audit branch's nav diamonds;
	# on a build without it the markers simply stay uniform.
	if _nav_diamonds != null and _nav_diamonds.has_method("set_active"):
		_nav_diamonds.call("set_active", button)


func _shell_flyout_is_open() -> bool:
	for flyout in _shell_flyouts.values():
		if flyout.visible:
			return true
	return false


func _on_shell_flyout_item(item_id: String, bar_id: String) -> void:
	match [bar_id, item_id]:
		["solo", "skirmish"]:
			if _skirmish_sweep_failed:
				retry_skirmish_sweep()
			if not _skirmish_options_ready:
				await _wait_for_skirmish_options()
			_close_shell_flyouts()
			_show_page(PAGE_SOLO)
		["solo", "wotr"]:
			_close_shell_flyouts()
			_on_wotr_pressed()
		["options", "settings"]:
			_close_shell_flyouts()
			_on_options()
		_:
			# Every other retail row is listed but disabled, so it cannot emit.
			push_warning("OpenBFME shell: unhandled flyout route %s/%s" % [bar_id, item_id])


func _set_skirmish_busy_state(busy: bool) -> void:
	if not solo_btn.has_meta("skirmish_idle_text"):
		solo_btn.set_meta("skirmish_idle_text", solo_btn.text)
	solo_btn.disabled = busy
	if not busy:
		solo_btn.text = String(solo_btn.get_meta("skirmish_idle_text"))
	var flyout = _shell_flyouts.get("solo", null)
	if flyout != null:
		flyout.set_item_state(
			"skirmish", not busy,
			"Checking every faction and map in the mounted content packs" if busy
			else String(SOLO_SKIRMISH_ITEM["tooltip"]))


func _update_skirmish_busy_label() -> void:
	if not _skirmish_busy_visible:
		return
	var percent := 0
	if _skirmish_sweep_total_units > 0:
		percent = int(round(100.0 * float(skirmish_sweep_completed_units()) / float(_skirmish_sweep_total_units)))
	var spinner: String = String(["◐", "◓", "◑", "◒"][int(Time.get_ticks_msec() / 160) % 4])
	solo_btn.text = "%s  %s %d%%" % [String(solo_btn.get_meta("skirmish_idle_text", "SOLO PLAY")), spinner, percent]
	status.text = "Checking skirmish content… %d%%" % percent


func _build_version_label() -> void:
	## Build identity lives in ONE place: under the OPEN BFME title, with the
	## brand.
	##
	## It used to be printed twice - there and again bottom-left above the bar -
	## with the same text in both. Two copies of one string is not redundancy, it
	## is clutter that a reader has to check against itself. The bottom-left copy
	## is removed here rather than left hidden, and any node a previous build left
	## behind is taken down with it.
	var identity := _build_identity_text()
	var title_version := center.get_node_or_null("TitleVersion") as Label if center != null else null
	if title_version != null:
		title_version.text = identity
	if center == null:
		return
	var legacy := center.get_node_or_null("BuildVersion")
	if legacy != null:
		center.remove_child(legacy)
		legacy.queue_free()


func _build_identity_text() -> String:
	## `v0.2.0-alpha  ·  build 412 (bfb414a)`.
	##
	## The build part is the repository's own count and commit, published by
	## `tools/Write-BuildInfo.ps1` into `res://data/build_info.json` and read by
	## `build_info.gd`. `OPENBFME_BUILD_ID` and the project setting still override
	## it, in that order, so a packaging job can stamp its own id; what none of
	## them may do is leave the shell advertising a constant.
	var version := String(ProjectSettings.get_setting(VERSION_SETTING, "")).strip_edges()
	var build_id := String(OS.get_environment("OPENBFME_BUILD_ID")).strip_edges()
	if build_id == "":
		var setting := String(ProjectSettings.get_setting(BUILD_ID_SETTING, "")).strip_edges()
		# `dev` is the checked-in placeholder, not a stamp; it must not win over a
		# real build number.
		if setting != "" and setting != BuildInfoScript.FALLBACK_BUILD_ID:
			build_id = setting
	if build_id == "":
		build_id = BuildInfoScript.build_id()
	if version == "":
		return "build version not set (%s)  ·  build %s" % [VERSION_SETTING, build_id]
	return "v%s  ·  build %s" % [version, build_id]


## THE DEFERRED-BUT-NEVER-SKIPPED CONTRACT.
##
## `_skirmish_options_ready` is the OUTWARD promise and flips only when the last
## step has run - `tests/boot_deferred_options_runner.gd` reads it directly, and a
## reader must never see `true` over a half-filled availability table.
## `_skirmish_sweep_steps` contains main-thread Control finalization only.
## `_skirmish_sweep_running` is the recursion guard for signals emitted while
## those controls are populated.
var _skirmish_options_ready := false
var _skirmish_sweep_failed := false
var _skirmish_sweep_running := false
var _skirmish_sweep_armed := false
var _skirmish_sweep_steps: Array[Dictionary] = []
var _skirmish_worker_task_id := -1
var _skirmish_worker_result_box: Dictionary = {}
var _skirmish_worker_cancelled := false
var _skirmish_worker_cancel_mutex := Mutex.new()
var _skirmish_sweep_started_usec := 0
var _skirmish_sweep_worst_frame_usec := 0
var _skirmish_sweep_worst_frame_name := ""
var _skirmish_current_main_work_name := ""
var _skirmish_worker_compute_ms := 0
var _skirmish_sweep_completed_units := 0
var _skirmish_sweep_total_units := 0
var _skirmish_busy_visible := false
var _skirmish_finalize_map_index := 0
## Real cost of the stepped sweep, reported through `BootProfile.measure()`:
## the SUM of the steps is the work actually done, and the WORST single step is
## the longest frame a player could feel while the menu is already up. The gap
## between the surrounding marks is neither of those, which is why both are
## measured explicitly instead of being inferred.
var _skirmish_sweep_total_ms := 0
var _skirmish_sweep_worst_ms := 0
var _skirmish_progress_mutex := Mutex.new()

## Persisted availability table. Validating 7 factions plus a full terrain load
## of every catalog map costs ~94 s cold and minutes under load, and it was paid
## on EVERY launch even when nothing about the mounted content had changed. The
## cache key is the exact content identity the sweep is a function of: the
## selection.json bytes plus every mounted pack root's bundle sha. Any pack
## republish, any selection edit, any added/removed pack changes the key and the
## sweep runs again — there is no staleness window and no partial reuse.
const SKIRMISH_CACHE_PATH := "user://skirmish_availability_cache.json"
const SKIRMISH_CACHE_SCHEMA := "openbfme.skirmish-availability-cache"
const SKIRMISH_CACHE_SCHEMA_VERSION := 0
## BUMP THIS WHENEVER THE AVAILABILITY COMPUTATION CHANGES.
## The cache key is a promise that "this content produced this table". That
## promise is only true for a fixed algorithm: edit `_compute_faction_availability`,
## `_compute_map_availability`, the map choice set, the faction list, or any
## verdict wording, and every stored entry becomes a lie about content that did
## not change. Incrementing this constant invalidates every persisted entry.
const SKIRMISH_AVAILABILITY_ALGORITHM_VERSION := 1
## The verdict `_compute_faction_availability` returns when the faction manifest
## script could not be loaded at all. This is an ENVIRONMENTAL failure (a lazy
## script compile that did not land), not evidence that a faction is genuinely
## unplayable, so a sweep made entirely of it must never be persisted.
const SKIRMISH_MANIFEST_UNAVAILABLE_VERDICT := "the faction manifest script is unavailable"
var _skirmish_sweep_cache_hit := false
var _skirmish_sweep_cache_key := ""
var _skirmish_sweep_rejected_poisoned_cache := false


func skirmish_sweep_cache_hit() -> bool:
	return _skirmish_sweep_cache_hit


func skirmish_sweep_cache_key() -> String:
	return _skirmish_sweep_cache_key


static func clear_skirmish_availability_cache() -> void:
	if FileAccess.file_exists(SKIRMISH_CACHE_PATH):
		DirAccess.remove_absolute(ProjectSettings.globalize_path(SKIRMISH_CACHE_PATH))


func skirmish_content_identity_facts_for_test() -> Dictionary:
	## The exact facts this launch's key was computed from, so a runner can mutate
	## one fact and prove the key is a function of it.
	return _skirmish_content_identity_facts()


func store_skirmish_sweep_cache_for_test(result: Dictionary) -> void:
	_store_skirmish_sweep_cache(_skirmish_content_identity(), result)


func _skirmish_content_identity() -> String:
	## sha256 over selection.json's bytes and every mounted pack root, in load
	## order. Returns "" when the identity cannot be established, which disables
	## the cache entirely rather than keying it on partial evidence.
	if _content_db == null:
		return ""
	var facts := _skirmish_content_identity_facts()
	if facts.is_empty():
		return ""
	return compute_skirmish_content_identity(facts)


func _skirmish_content_identity_facts() -> Dictionary:
	## Gathers the observable content facts the key is a function of. Split out
	## from the hashing so the key formula is a pure, directly testable function
	## of stated facts. An empty Dictionary means the identity cannot be
	## established and the cache must stay disabled.
	##
	## The facts are deliberately WIDER than "which bundle sha is mounted". A
	## pack-id/bundle-sha pair is NOT a content digest: the identical pair exists
	## under the repo workspace and under the durable user cache, and when
	## OPENBFME_CONTENT is unset ModLoader picks between those two trees at
	## runtime. Keying on the pair alone let a workspace run and a durable run
	## share one cache entry. So the key also carries the RESOLVED selection
	## source (its name, its absolute path and its bytes) and, per bundle, the
	## sha256 of its pack.json plus the absolute prefix it was mounted from.
	if _content_db == null:
		return {}
	var facts: Dictionary = {
		"algorithm_version": SKIRMISH_AVAILABILITY_ALGORITHM_VERSION,
		"selection_required": false,
		"selection_source": String(ModLoader.active_content_source),
		"selection_path": String(ModLoader.active_selection_path),
		"selection_sha": "",
		"bundles": [],
	}
	var content_root := OS.get_environment("OPENBFME_CONTENT").strip_edges()
	facts["selection_required"] = content_root != ""
	var selection_path := String(facts["selection_path"])
	if selection_path != "" and FileAccess.file_exists(selection_path):
		var selection_bytes := FileAccess.get_file_as_bytes(selection_path)
		if selection_bytes.is_empty():
			return {}
		facts["selection_sha"] = selection_bytes.get_string_from_utf8().sha256_text()
	elif content_root != "":
		# An explicit override that resolved no readable selection document is
		# partial evidence; refuse to key a cache on it, exactly as before.
		var fallback := content_root.path_join("selection.json")
		if not FileAccess.file_exists(fallback):
			return {}
		var fallback_bytes := FileAccess.get_file_as_bytes(fallback)
		if fallback_bytes.is_empty():
			return {}
		facts["selection_path"] = fallback
		facts["selection_sha"] = fallback_bytes.get_string_from_utf8().sha256_text()
	var roots: Array = (_content_db.get("pack_roots") as Array)
	if roots == null or roots.is_empty():
		return {}
	var bundles: Array = []
	for root_value in roots:
		# The pack root's final path component IS its bundle sha; the parent is
		# the pack id. The prefix is everything above the pack id, which is what
		# distinguishes the workspace tree from the durable user cache.
		var root := String(root_value)
		var pack_json_sha := ""
		var pack_json_path := ModLoader.resolve_pack_path(root, "pack.json")
		if pack_json_path != "" and FileAccess.file_exists(pack_json_path):
			var pack_bytes := FileAccess.get_file_as_bytes(pack_json_path)
			pack_json_sha = pack_bytes.get_string_from_utf8().sha256_text()
		bundles.append({
			"pack_id": root.get_base_dir().get_file(),
			"bundle_sha": root.get_file(),
			"prefix": ProjectSettings.globalize_path(root.get_base_dir().get_base_dir()),
			"pack_json_sha": pack_json_sha,
		})
	facts["bundles"] = bundles
	return facts


static func compute_skirmish_content_identity(facts: Dictionary) -> String:
	## Pure key formula over gathered content facts. Every fact below is part of
	## the content identity; changing ANY of them must change the key.
	var context := PackedStringArray()
	context.append("algorithm:%d" % int(
		facts.get("algorithm_version", SKIRMISH_AVAILABILITY_ALGORITHM_VERSION)))
	if bool(facts.get("selection_required", false)) and String(facts.get("selection_sha", "")) == "":
		return ""
	context.append("source:%s" % String(facts.get("selection_source", "")))
	context.append("selection-path:%s" % String(facts.get("selection_path", "")))
	context.append("selection:%s" % String(facts.get("selection_sha", "")))
	var bundles: Array = facts.get("bundles", []) as Array
	if bundles == null or bundles.is_empty():
		return ""
	for bundle_value in bundles:
		var bundle := bundle_value as Dictionary
		context.append("pack:%s/%s@%s#%s" % [
			String(bundle.get("pack_id", "")),
			String(bundle.get("bundle_sha", "")),
			String(bundle.get("prefix", "")),
			String(bundle.get("pack_json_sha", "")),
		])
	return "\n".join(context).sha256_text()


func _load_skirmish_sweep_cache(key: String) -> Dictionary:
	if key == "" or not FileAccess.file_exists(SKIRMISH_CACHE_PATH):
		return {}
	var file := FileAccess.open(SKIRMISH_CACHE_PATH, FileAccess.READ)
	if file == null:
		return {}
	var raw: Variant = JSON.parse_string(file.get_as_text())
	file.close()
	if typeof(raw) != TYPE_DICTIONARY:
		return {}
	var document := raw as Dictionary
	if (
		String(document.get("schema", "")) != SKIRMISH_CACHE_SCHEMA
		or int(document.get("schemaVersion", -1)) != SKIRMISH_CACHE_SCHEMA_VERSION
		or String(document.get("contentIdentity", "")) != key
		or typeof(document.get("availability")) != TYPE_DICTIONARY
		or typeof(document.get("map_notes")) != TYPE_DICTIONARY
	):
		return {}
	# Re-key both tables into fresh String->String dictionaries so a cache hit is
	# byte-identical to a computed table under var_to_bytes().
	#
	# PARTIAL ENTRIES ARE LEGAL NOW. The menu no longer waits for a full sweep
	# before it draws: it renders instantly from the pack manifests and validates
	# the map and factions the player actually picked. Those per-pick verdicts are
	# memoized into this same file under this same content key, so a repeat pick
	# is instant. `complete` is what the FULL-sweep short-circuit rests on and is
	# derived from coverage rather than trusted from the document: an entry that
	# covers every faction and every catalog map is a whole sweep and the warmer
	# can be skipped; anything less is knowledge, not a substitute for the sweep.
	var stored_availability := document["availability"] as Dictionary
	var stored_notes := document["map_notes"] as Dictionary
	var availability: Dictionary = {}
	var factions_covered := 0
	for faction_value in RETAIL_FACTIONS:
		var faction_id := String((faction_value as Dictionary)["id"])
		if not stored_availability.has(faction_id):
			continue
		availability[faction_id] = String(stored_availability[faction_id])
		factions_covered += 1
	var notes: Dictionary = {}
	var maps_covered := 0
	for choice_value in _skirmish_map_choice_rows:
		var map_id := String((choice_value as Dictionary)["id"])
		if not stored_notes.has(map_id):
			continue
		notes[map_id] = String(stored_notes[map_id])
		maps_covered += 1
	if availability.is_empty() and notes.is_empty():
		return {}
	var complete := (
		factions_covered == RETAIL_FACTIONS.size()
		and maps_covered == _skirmish_map_choice_rows.size()
		and stored_availability.size() == availability.size()
		and stored_notes.size() == notes.size()
	)
	if factions_covered == RETAIL_FACTIONS.size() and skirmish_sweep_is_environmental_failure({"availability": availability}):
		# Revalidation path for entries an older build (or an older run) poisoned
		# with an all-manifest-failure table. Drop the file so this launch and
		# every later one recompute instead of serving the poisoned negative.
		# Only asked of a FULL faction table: one memoized per-pick verdict that
		# happens to be the manifest failure is not evidence the whole run failed.
		_skirmish_sweep_rejected_poisoned_cache = true
		clear_skirmish_availability_cache()
		return {}
	return {"availability": availability, "map_notes": notes, "worker_ms": 0, "complete": complete}


func skirmish_sweep_rejected_poisoned_cache() -> bool:
	return _skirmish_sweep_rejected_poisoned_cache


func skirmish_sweep_is_environmental_failure(result: Dictionary) -> bool:
	## True when a sweep resolved NOTHING: every faction verdict is the
	## manifest-unavailable verdict. That is a transient/environmental failure of
	## this process (the lazy faction-manifest script never compiled), not a
	## finding about the mounted content, and persisting it would hand every
	## later launch a durable "no faction is playable" answer that no content
	## change can ever invalidate.
	var availability: Dictionary = result.get("availability", {}) as Dictionary
	if availability == null or availability.is_empty():
		return true
	var manifest_failure := lazy_script_failure(LAZY_FACTION_MANIFEST)
	for verdict_value in availability.values():
		var verdict := String(verdict_value)
		if verdict == SKIRMISH_MANIFEST_UNAVAILABLE_VERDICT:
			continue
		if manifest_failure != "" and verdict == manifest_failure:
			continue
		return false
	return true


func _store_skirmish_sweep_cache(key: String, result: Dictionary) -> void:
	if key == "" or _skirmish_sweep_failed:
		return
	var stored_availability := result.get("availability", {}) as Dictionary
	var stored_notes := result.get("map_notes", {}) as Dictionary
	if stored_availability.is_empty() and stored_notes.is_empty():
		return
	# A map-only memo carries no faction claim at all, so the "nothing is
	# playable" guard below has nothing to judge and must not swallow it.
	if not stored_availability.is_empty() and skirmish_sweep_is_environmental_failure(result):
		# Fail-closed for THIS run, open for the next one: publish the failure to
		# the player but leave no durable negative entry behind.
		return
	var file := FileAccess.open(SKIRMISH_CACHE_PATH, FileAccess.WRITE)
	if file == null:
		return
	file.store_string(JSON.stringify({
		"schema": SKIRMISH_CACHE_SCHEMA,
		"schemaVersion": SKIRMISH_CACHE_SCHEMA_VERSION,
		"contentIdentity": key,
		"availability": result.get("availability", {}),
		"map_notes": result.get("map_notes", {}),
	}, "\t"))
	file.close()


func _arm_skirmish_sweep() -> void:
	## Cache the exact map set once. The worker validates every faction and map;
	## only Control construction is later chunked on the main thread.
	if _skirmish_sweep_armed:
		return
	_skirmish_sweep_armed = true
	_skirmish_map_choices()
	_skirmish_sweep_total_units = RETAIL_FACTIONS.size() + _skirmish_map_choice_rows.size()


## ---------------------------------------------------------------------------
## THE INSTANT LIST.
##
## What this replaces: opening SOLO PLAY used to mean deep-validating seven
## factions AND terrain-loading every catalog map — ~80 of them, ~90 s cold —
## before a single row was drawn. Caching it helped exactly once per content
## revision: the key is (deliberately) a function of the availability algorithm,
## so every engine change threw the table away and the player paid the ~90 s
## again. The wait was never the point; the ANSWER was, and the answer is only
## needed for the ONE map and the FEW factions a player actually picks.
##
## So the list is now built from what the packs already declare — names, player
## counts, catalog registration — with NO validation at all, and the fail-closed
## launch gate moved to the pick: `retail_launch_error()` validates the selected
## map and the faction verdicts on demand, through the SAME
## `_compute_map_availability` / `_compute_faction_availability` the sweep runs,
## and memoizes each answer under the same content key. The full sweep survives
## as a BACKGROUND WARMER (below) whose only job is to make those per-pick
## lookups already-answered. Nothing ever waits on it.
## ---------------------------------------------------------------------------

## Per-frame ceiling for the instant build's Control steps. Each step is a single
## OptionButton item or one map row; draining several per frame is what turns a
## ~200-step build from ~200 frames into a handful, while still bounding the
## longest frame a player could feel.
const SKIRMISH_INSTANT_STEP_BUDGET_USEC := 4000
## True once the full background sweep has published (or a cache entry covering
## every faction and map was reused). Distinct from `_skirmish_options_ready`,
## which is now the INSTANT list's promise.
var _skirmish_sweep_complete := false
var _skirmish_warm_sweep_wall_ms := 0
var _skirmish_known_verdicts_loaded := false


func _step_skirmish_instant_build(frame_started_usec: int) -> bool:
	## Drains the instant list's Control steps within this frame's budget.
	## Returns true when it did main-thread work, so `_process` attributes the
	## frame cost to it and the warmer stays off the same frame.
	if _skirmish_options_ready or _skirmish_sweep_running:
		return false
	_arm_skirmish_sweep()
	_load_known_skirmish_verdicts()
	if _skirmish_sweep_steps.is_empty():
		_build_skirmish_option_controls_steps()
	_skirmish_sweep_running = true
	while not _skirmish_sweep_steps.is_empty():
		_run_skirmish_sweep_step()
		if Time.get_ticks_usec() - frame_started_usec >= SKIRMISH_INSTANT_STEP_BUDGET_USEC:
			break
	_skirmish_sweep_running = false
	return true


func _build_skirmish_option_controls_steps() -> void:
	## The list itself: army/difficulty/team/color dropdowns, one row per catalog
	## map, the rules block. Every verdict it reads is whatever is already KNOWN
	## (cache memo, or nothing at all on a cold first run) — it never validates.
	_skirmish_sweep_steps = []
	for row in solo_flyout.row_army_opts.size():
		_append_row_control_sweep_steps(row)
	_skirmish_finalize_map_index = 0
	_skirmish_sweep_steps.append({"name": "map_rows", "run": _sweep_step_build_next_map_row})
	_skirmish_sweep_steps.append({"name": "select_map", "run": _select_first_available_map_row_without_refresh})
	_skirmish_sweep_steps.append({"name": "rules", "run": _populate_rules_options})
	_skirmish_sweep_steps.append({"name": "finish", "run": _finish_skirmish_sweep})


func _load_known_skirmish_verdicts() -> void:
	## One bounded JSON read. Establishes the content key and adopts whatever
	## verdicts were already memoized for this exact content — a whole earlier
	## sweep, or just the maps this player has picked before. Never validates.
	if _skirmish_known_verdicts_loaded:
		return
	_skirmish_known_verdicts_loaded = true
	_skirmish_sweep_cache_key = _skirmish_content_identity()
	var cached := _load_skirmish_sweep_cache(_skirmish_sweep_cache_key)
	if cached.is_empty():
		return
	_merge_skirmish_verdicts(
		cached.get("availability", {}) as Dictionary,
		cached.get("map_notes", {}) as Dictionary
	)
	if bool(cached.get("complete", false)):
		# A whole sweep for this exact content is already on disk. The warmer has
		# nothing left to warm.
		_skirmish_sweep_cache_hit = true
		_skirmish_sweep_complete = true
		_skirmish_progress_mutex.lock()
		_skirmish_sweep_completed_units = _skirmish_sweep_total_units
		_skirmish_progress_mutex.unlock()


func _merge_skirmish_verdicts(availability: Dictionary, notes: Dictionary) -> void:
	for faction_id in availability.keys():
		_skirmish_availability[String(faction_id)] = String(availability[faction_id])
	for map_id in notes.keys():
		_skirmish_map_notes[String(map_id)] = String(notes[map_id])


func _step_skirmish_sweep() -> bool:
	## The BACKGROUND WARMER. Expensive validation runs on WorkerThreadPool; this
	## only launches it and reaps it. It starts only AFTER the instant list is up,
	## and no navigation, no page and no button ever awaits it.
	if _skirmish_sweep_complete or _skirmish_sweep_failed or _skirmish_sweep_running:
		return false
	if not _skirmish_options_ready:
		return false
	_arm_skirmish_sweep()
	var worker_was_active := _skirmish_worker_task_id >= 0
	_poll_skirmish_worker()
	# Reaping/publishing is its own frame. Do not compound that boundary with
	# even the first cold Control mutation.
	if worker_was_active and _skirmish_worker_task_id == -1:
		return false
	if _skirmish_worker_task_id == -1:
		_start_skirmish_worker()
	return false


func _ensure_skirmish_options() -> bool:
	## A synchronous reader that arrives before the stepped build finishes drains
	## the REST of the instant list here and now. That is cheap (manifest reads and
	## Control construction only) and it is what keeps the fail-closed launch gate
	## honest: `retail_launch_error()` is never judged against an unpopulated list.
	## It does NOT run, and never waits on, the availability sweep.
	if _skirmish_options_ready:
		return true
	if _skirmish_sweep_running:
		# Re-entered from a signal emitted while the list is being populated.
		return false
	_arm_skirmish_sweep()
	_load_known_skirmish_verdicts()
	if _skirmish_sweep_steps.is_empty():
		_build_skirmish_option_controls_steps()
	_skirmish_sweep_running = true
	while not _skirmish_sweep_steps.is_empty():
		_run_skirmish_sweep_step()
	_skirmish_sweep_running = false
	return _skirmish_options_ready


func _wait_for_skirmish_options() -> void:
	if _skirmish_options_ready:
		return
	_skirmish_busy_visible = true
	_set_skirmish_busy_state(true)
	_ensure_skirmish_options()
	while not _skirmish_options_ready:
		await get_tree().process_frame
	_skirmish_busy_visible = false
	_set_skirmish_busy_state(false)


func _start_skirmish_worker() -> void:
	if _skirmish_worker_task_id != -1 or _skirmish_sweep_complete:
		return
	# Collect threaded script loads only after they are complete. Calling
	# load_threaded_get while pending is exactly the click freeze this fixes.
	if _lazy_warm_is_pending(LAZY_SLICE) or _lazy_warm_is_pending(LAZY_FACTION_MANIFEST):
		return
	_load_known_skirmish_verdicts()
	if _skirmish_sweep_complete:
		return
	var slice_script = _slice_script()
	var manifest_script = _faction_manifest_script()
	var map_data_script = load("res://src/retail_slice/retail_map_data.gd")
	var choices := _skirmish_map_choice_rows.duplicate(true)
	# Warm every lazy ContentDB registry on the main thread, then give the worker
	# private copies. No first-use cache write or live pack_meta/structure alias is
	# allowed after dispatch.
	var unit_snapshot := (_content_db.call("get_playable_unit_runtimes") as Dictionary).duplicate(true)
	var structure_snapshot := (_content_db.call("get_playable_structure_runtimes") as Dictionary).duplicate(true)
	var pack_index_snapshot := (_content_db.call("get_playable_unit_runtime_pack_index") as Dictionary).duplicate(true)
	var maps_snapshot := (_content_db.get("bundle_maps") as Dictionary).duplicate(true)
	var pack_meta_snapshot := (_content_db.get("pack_meta") as Array).duplicate(true)
	var host_resolution: Dictionary = PackCapabilityScript.resolve_host_slice_pack(pack_meta_snapshot)
	var host_root := String(host_resolution.get("root", ""))
	var selected_root := _selected_faction_pack_root()
	var men_gate_error := _men_pack_gate_error_from_snapshot(
		pack_meta_snapshot, maps_snapshot, host_resolution, unit_snapshot
	)
	# The Create-a-Hero table, captured HERE on the main thread. The worker
	# cannot reach an autoload - Godot refuses node lookups off the main thread -
	# so a slice built there would classify a faction with no created heroes on
	# it and the sweep would answer with a roster the match then contradicts.
	var cah_snapshot := _cah_system_snapshot()
	_skirmish_worker_cancel_mutex.lock()
	_skirmish_worker_cancelled = false
	_skirmish_worker_cancel_mutex.unlock()
	_skirmish_sweep_started_usec = Time.get_ticks_usec()
	_skirmish_worker_result_box = {}
	_skirmish_worker_task_id = WorkerThreadPool.add_task(
		_skirmish_worker_entry.bind(
			slice_script, manifest_script, map_data_script, choices, host_root, selected_root,
			unit_snapshot, structure_snapshot, pack_index_snapshot, maps_snapshot, men_gate_error,
			lazy_script_failure(LAZY_SLICE), lazy_script_failure(LAZY_FACTION_MANIFEST),
			cah_snapshot, _skirmish_worker_result_box
		), false, "OpenBFME skirmish availability")


func _skirmish_worker_entry(slice_script, manifest_script, map_data_script,
		choices: Array, host_root: String, selected_root: String, unit_snapshot: Dictionary,
		structure_snapshot: Dictionary, pack_index_snapshot: Dictionary, maps_snapshot: Dictionary,
		men_gate_error: String, slice_failure: String, manifest_failure: String,
		cah_snapshot: Dictionary, result_box: Dictionary) -> void:
	# Wrapper is the finally-equivalent: an inner script error returns here, and
	# the pooled thread has checks restored before its result is observed.
	Thread.set_thread_safety_checks_enabled(false)
	var result = _compute_skirmish_sweep_worker(
		slice_script, manifest_script, map_data_script, choices, host_root, selected_root,
		unit_snapshot, structure_snapshot, pack_index_snapshot, maps_snapshot,
		men_gate_error, slice_failure, manifest_failure, cah_snapshot
	)
	Thread.set_thread_safety_checks_enabled(true)
	# WorkerThreadPool discards task return values. This private box is read only
	# after is_task_completed() establishes the handoff boundary.
	result_box["result"] = result


func _compute_skirmish_sweep_worker(slice_script, manifest_script, map_data_script,
		choices: Array, host_root: String, selected_root: String, unit_snapshot: Dictionary,
		structure_snapshot: Dictionary, pack_index_snapshot: Dictionary, maps_snapshot: Dictionary,
		men_gate_error: String, slice_failure: String, manifest_failure: String,
		cah_snapshot: Dictionary = {}) -> Dictionary:
	## THREAD-SAFETY CONTRACT: this job creates and mutates only its private probe
	## and result dictionaries. ContentDB/pack calls here are read-only snapshots,
	## plus bounded file reads. Shared menu dictionaries and all Controls are only
	## touched by `_accept_skirmish_sweep_result()` on the main thread.
	# Godot's checker rejects even read-only Node/SceneTree calls from workers.
	# This task only invokes ContentDB getters and reads immutable pack resources;
	# every object it mutates (probe, map validator, result tables) is task-local.
	var started := Time.get_ticks_msec()
	var availability: Dictionary = {}
	var notes: Dictionary = {}
	var probe = slice_script.new() if slice_script != null else null
	for faction_value in RETAIL_FACTIONS:
		if _skirmish_worker_is_cancelled():
			if probe != null:
				probe.free()
			return {}
		var faction_id := String((faction_value as Dictionary)["id"])
		availability[faction_id] = _compute_faction_availability(
			faction_id, probe, manifest_script, unit_snapshot, structure_snapshot,
			pack_index_snapshot, men_gate_error, slice_failure, manifest_failure,
			cah_snapshot)
		_note_skirmish_worker_progress()
		if _skirmish_worker_is_cancelled():
			if probe != null:
				probe.free()
			return {}
	for choice_value in choices:
		if _skirmish_worker_is_cancelled():
			if probe != null:
				probe.free()
			return {}
		var map_id := String((choice_value as Dictionary)["id"])
		notes[map_id] = _compute_map_availability(
			map_id, probe, map_data_script, host_root, selected_root, maps_snapshot, slice_failure)
		_note_skirmish_worker_progress()
		if _skirmish_worker_is_cancelled():
			if probe != null:
				probe.free()
			return {}
	var result := {
		"availability": availability,
		"map_notes": notes,
		"worker_ms": Time.get_ticks_msec() - started,
	}
	if probe != null:
		probe.free()
	return result


func _compute_faction_availability(faction_id: String, probe, manifest_script,
		unit_snapshot: Dictionary, structures: Dictionary, pack_index_snapshot: Dictionary,
		men_gate_error: String, slice_failure: String, manifest_failure: String,
		cah_snapshot: Dictionary = {}) -> String:
	if manifest_script == null:
		return manifest_failure if manifest_failure != "" else SKIRMISH_MANIFEST_UNAVAILABLE_VERDICT
	if faction_id == manifest_script.DEFAULT_FACTION:
		if men_gate_error != "":
			return men_gate_error
	var fieldable: Dictionary = {}
	if probe != null:
		if _skirmish_worker_is_cancelled():
			return "validation-cancelled"
		probe._classify_faction_units(
			faction_id, unit_snapshot, structures, pack_index_snapshot, null, cah_snapshot
		)
		if _skirmish_worker_is_cancelled():
			return "validation-cancelled"
		fieldable = (probe.get("fieldable_unit_runtimes") as Dictionary).duplicate(true)
	elif slice_failure != "":
		return slice_failure
	if faction_id == manifest_script.DEFAULT_FACTION and fieldable.is_empty() and structures.is_empty():
		return ""
	if _skirmish_worker_is_cancelled():
		return "validation-cancelled"
	var manifest: Dictionary = manifest_script.from_registries(faction_id, fieldable, structures)
	return String(manifest.get("_error", ""))


func _compute_map_availability(map_id: String, probe, map_data_script,
		host_root: String, selected_root: String, maps_snapshot: Dictionary,
		slice_failure: String) -> String:
	if probe == null:
		return "Open BFME map resolution is unavailable - %s" % slice_failure
	if _skirmish_worker_is_cancelled():
		return "validation-cancelled"
	probe.selected_pack_root = host_root if host_root != "" else selected_root
	var resolved: Dictionary = probe._resolve_slice_map_definition(map_id, maps_snapshot)
	if _skirmish_worker_is_cancelled():
		return "validation-cancelled"
	if not resolved.is_empty():
		var pack_root := String(resolved.get("_pack_root", ""))
		if pack_root == "":
			return "the resolved map does not identify its owning content pack"
		if map_data_script == null:
			return "the cooked map validator could not be loaded"
		if _skirmish_worker_is_cancelled():
			return "validation-cancelled"
		var map_data = map_data_script.new()
		var loaded := bool(map_data.load_from_pack(
			pack_root, resolved, _skirmish_worker_is_cancelled
		))
		var validation_error := String(map_data.error)
		var castle_blockers: Array[String] = map_data.castle_gameplay_blockers.duplicate()
		# RefCounted validators created in a pooled task must drop their final
		# reference on that same task; waiting for VM local cleanup leaked them.
		map_data = null
		if _skirmish_worker_is_cancelled():
			return "validation-cancelled"
		if not loaded:
			return "cooked map data failed validation: %s" % validation_error
		if not castle_blockers.is_empty():
			return "castle gameplay unsupported: " + ", ".join(castle_blockers)
		return ""
	if map_id == SliceIds.MAP_ID:
		if host_root == "":
			return "no host pack with files.entryMap is mounted; Open BFME boots Fords from the host capability pack"
		return "the host pack's files.entryMap is missing or invalid; Open BFME boots Fords only from the host capability pack"
	var content_root := OS.get_environment("OPENBFME_CONTENT").strip_edges()
	if content_root == "" or not DirAccess.dir_exists_absolute(content_root):
		return "not registered in selection.json and OPENBFME_CONTENT is unset; Open BFME requires it for catalog maps"
	return "not registered in the mounted map catalog (selection.json); cook/select a map pack that publishes this id"


func _note_skirmish_worker_progress() -> void:
	## Called from the pooled worker after EACH faction and EACH map, so the
	## "Checking skirmish content… N%" label advances within the batch. Before
	## this existed the counter was never incremented at all and a player
	## watched a literal 0% for the whole sweep (~94 s cold, minutes under
	## load). Mutex-guarded because the writer is the worker thread and the
	## reader is `_update_skirmish_busy_label()` on the main thread.
	_skirmish_progress_mutex.lock()
	_skirmish_sweep_completed_units = mini(
		_skirmish_sweep_completed_units + 1, _skirmish_sweep_total_units)
	_skirmish_progress_mutex.unlock()


func skirmish_sweep_completed_units() -> int:
	_skirmish_progress_mutex.lock()
	var value := _skirmish_sweep_completed_units
	_skirmish_progress_mutex.unlock()
	return value


func _accept_skirmish_sweep_result(result: Dictionary) -> void:
	## The WARMER's publication. The list already exists, so this does not build
	## anything: it adopts the verdicts and re-states the rows that the verdicts
	## now have something to say about. A verdict the on-pick path already
	## computed is overwritten with the sweep's own value for the same input,
	## which is the same value — both run `_compute_*_availability`.
	var publication_started := Time.get_ticks_usec()
	_skirmish_worker_compute_ms = int(result.get("worker_ms", 0))
	_merge_skirmish_verdicts(
		result.get("availability", {}) as Dictionary,
		result.get("map_notes", {}) as Dictionary
	)
	_skirmish_sweep_complete = true
	_skirmish_warm_sweep_wall_ms = int((Time.get_ticks_usec() - _skirmish_sweep_started_usec) / 1000) if _skirmish_sweep_started_usec > 0 else 0
	BootProfile.measure("menu:skirmish_warm_sweep_total", _skirmish_warm_sweep_wall_ms)
	_refresh_skirmish_verdict_presentation()
	var publication_elapsed := Time.get_ticks_usec() - publication_started
	if publication_elapsed > _skirmish_sweep_worst_frame_usec:
		_skirmish_sweep_worst_frame_usec = publication_elapsed
		_skirmish_sweep_worst_frame_name = "publish"


func _refresh_skirmish_verdict_presentation() -> void:
	## Re-states the list against the verdicts known right now. Cheap: it mutates
	## existing Controls' disabled/tooltip/label state and builds nothing.
	if not _skirmish_options_ready or _skirmish_sweep_running:
		return
	_skirmish_sweep_running = true
	for row in range(solo_flyout.row_army_opts.size()):
		_refresh_row_army_states(row)
	_refresh_map_row_states()
	_skirmish_sweep_running = false
	_refresh_skirmish_launch_state()


func _refresh_row_army_states(row: int) -> void:
	var option: OptionButton = solo_flyout.row_army_opts[row]
	var selected_became_unavailable := false
	for index in range(option.item_count):
		var faction_id := String(option.get_item_metadata(index))
		var note := String(_skirmish_availability.get(faction_id, ""))
		option.set_item_text(index, _retail_faction_display_name(faction_id) + (NOT_CONVERTED_SUFFIX if note != "" else ""))
		option.set_item_disabled(index, note != "")
		option.set_item_tooltip(index, "Not converted: %s" % note if note != "" else "")
		if note != "" and option.selected == index:
			selected_became_unavailable = true
	if selected_became_unavailable:
		_select_first_enabled(option)


func _refresh_map_row_states() -> void:
	for entry_value in solo_flyout.map_rows:
		var entry := entry_value as Dictionary
		var button := entry["button"] as Button
		if button == null or not is_instance_valid(button):
			continue
		var map_id := String(entry["map_id"])
		var note := String(_skirmish_map_notes.get(map_id, ""))
		var available := note == ""
		button.disabled = not available
		button.text = _retail_map_display_name(map_id) + ("" if available else " (unavailable)")
		button.tooltip_text = "" if available else "Unavailable: %s" % note


func _poll_skirmish_worker() -> void:
	if _skirmish_worker_task_id < 0 or not WorkerThreadPool.is_task_completed(_skirmish_worker_task_id):
		return
	var task_id := _skirmish_worker_task_id
	WorkerThreadPool.wait_for_task_completion(task_id)
	_skirmish_worker_task_id = -1
	var result: Variant = _skirmish_worker_result_box.get("result")
	_skirmish_worker_result_box = {}
	if typeof(result) == TYPE_DICTIONARY and (result as Dictionary).has("availability") and (result as Dictionary).has("map_notes"):
		_store_skirmish_sweep_cache(_skirmish_sweep_cache_key, result as Dictionary)
		_accept_skirmish_sweep_result(result as Dictionary)
		return
	_publish_skirmish_worker_failure(
		"availability worker completed without a result; choose SKIRMISH again to retry"
	)


func _publish_skirmish_worker_failure(reason: String) -> void:
	## Fail closed over everything still UNKNOWN. A verdict already computed — by
	## an earlier warm sweep or by the on-pick validator — is a real answer about
	## real content and a dead background worker is no reason to retract it; a
	## faction or map nobody has validated becomes the named refusal, so the gate
	## can never wave through something on the strength of an absent answer.
	_skirmish_sweep_failed = true
	var availability: Dictionary = {}
	var notes: Dictionary = {}
	for faction_value in RETAIL_FACTIONS:
		var faction_id := String((faction_value as Dictionary)["id"])
		if not _skirmish_availability.has(faction_id):
			availability[faction_id] = reason
	for choice_value in _skirmish_map_choice_rows:
		var map_id := String((choice_value as Dictionary)["id"])
		if not _skirmish_map_notes.has(map_id):
			notes[map_id] = reason
	_accept_skirmish_sweep_result({
		"availability": availability,
		"map_notes": notes,
		"worker_ms": 0,
	})


func retry_skirmish_sweep() -> void:
	## Re-arms the BACKGROUND WARMER only. The instant list is already up and is
	## not torn down: a warmer that died is a lost head start, not a lost menu.
	if _skirmish_worker_task_id >= 0:
		return
	_skirmish_sweep_failed = false
	_skirmish_sweep_complete = false
	_skirmish_sweep_cache_hit = false
	_skirmish_progress_mutex.lock()
	_skirmish_sweep_completed_units = 0
	_skirmish_progress_mutex.unlock()
	_skirmish_worker_compute_ms = 0
	_skirmish_sweep_started_usec = 0
	set_process(true)


func start_dead_skirmish_worker_for_test() -> void:
	if _skirmish_worker_task_id >= 0:
		return
	# The list stays up; it is the WARMER that is being made to die here.
	_ensure_skirmish_options()
	_skirmish_sweep_complete = false
	_skirmish_sweep_failed = false
	_skirmish_worker_result_box = {}
	_skirmish_worker_task_id = WorkerThreadPool.add_task(
		_dead_skirmish_worker_entry, false, "OpenBFME dead sweep fixture"
	)
	set_process(true)


func _dead_skirmish_worker_entry() -> Variant:
	Thread.set_thread_safety_checks_enabled(false)
	Thread.set_thread_safety_checks_enabled(true)
	return null


func _skirmish_worker_is_cancelled() -> bool:
	_skirmish_worker_cancel_mutex.lock()
	var cancelled := _skirmish_worker_cancelled
	_skirmish_worker_cancel_mutex.unlock()
	return cancelled


func _run_skirmish_sweep_step() -> void:
	var step: Dictionary = _skirmish_sweep_steps.pop_front()
	_skirmish_current_main_work_name = String(step["name"])
	var started := Time.get_ticks_msec()
	(step["run"] as Callable).call()
	var cost := Time.get_ticks_msec() - started
	_skirmish_sweep_total_ms += cost
	_skirmish_sweep_worst_ms = maxi(_skirmish_sweep_worst_ms, cost)
	# Per-step attribution, profiled runs only. Zero cost otherwise.
	BootProfile.measure("menu:sweep/%s" % String(step["name"]), cost)


func _finish_skirmish_sweep() -> void:
	## Last step of the INSTANT build. `_skirmish_options_ready` is the promise
	## that the skirmish list exists and can be shown and judged — it is no longer
	## a promise that every faction and map has been deep-validated, because that
	## promise cost ~90 s and is now kept per pick instead.
	if _skirmish_options_ready:
		return
	_skirmish_options_ready = true
	_skirmish_instant_build_ready_usec = Time.get_ticks_usec()
	_skirmish_sweep_worst_ms = int(ceil(float(_skirmish_sweep_worst_frame_usec) / 1000.0))
	# Stable labels on purpose: tests/boot_startup_runner.gd matches them exactly.
	# They now measure the instant build, which is the time to an interactive
	# SOLO PLAY list — the thing the budget was always about. The background
	# warmer reports separately under `menu:skirmish_warm_sweep_total`.
	BootProfile.mark("menu:skirmish_options")
	BootProfile.measure("menu:skirmish_sweep_total", _skirmish_sweep_total_ms)
	BootProfile.measure("menu:skirmish_sweep_worst_step", _skirmish_sweep_worst_ms)
	skirmish_options_ready.emit()


## ---------------------------------------------------------------------------
## VALIDATE ON PICK.
##
## The SAME two functions the sweep runs, invoked for one map / one faction on
## the main thread and memoized under the same content key. There is deliberately
## no second implementation here: a per-pick verdict that could disagree with the
## sweep's verdict for the same input would be a launch gate that says yes to a
## match the slice then refuses, which is the exact class of bug this repository
## keeps deleting. `menu_instant_runner.gd` pins the equality directly.
## ---------------------------------------------------------------------------

var _skirmish_instant_build_ready_usec := 0
var _skirmish_validation_context_cache: Dictionary = {}
var _skirmish_on_pick_validation_ms := 0


func _skirmish_validation_context(fresh: bool = false) -> Dictionary:
	## Exactly the inputs `_start_skirmish_worker()` hands the pooled task, built
	## once on the main thread. Same snapshots, same probe class, same host/pack
	## resolution — so the two paths cannot drift on their inputs either.
	##
	## `fresh` re-reads the registries instead of reusing the snapshot. The
	## memoized on-pick path never wants that (its answers are keyed on a content
	## identity that cannot have moved), but the uncached public readers
	## `retail_map_availability()` / `_retail_faction_availability()` do: they are
	## asked deliberately about content a caller has just changed underneath them.
	if not fresh and not _skirmish_validation_context_cache.is_empty():
		return _skirmish_validation_context_cache
	var pack_meta_snapshot := (_content_db.get("pack_meta") as Array).duplicate(true)
	var host_resolution: Dictionary = PackCapabilityScript.resolve_host_slice_pack(pack_meta_snapshot)
	var maps_snapshot := (_content_db.get("bundle_maps") as Dictionary).duplicate(true)
	var context: Dictionary = {
		"probe": _slice_probe(),
		"manifest_script": _faction_manifest_script(),
		"map_data_script": load("res://src/retail_slice/retail_map_data.gd"),
		"units": (_content_db.call("get_playable_unit_runtimes") as Dictionary).duplicate(true),
		"structures": (_content_db.call("get_playable_structure_runtimes") as Dictionary).duplicate(true),
		"pack_index": (_content_db.call("get_playable_unit_runtime_pack_index") as Dictionary).duplicate(true),
		"maps": maps_snapshot,
		"host_root": String(host_resolution.get("root", "")),
		"selected_root": _selected_faction_pack_root(),
		"men_gate_error": _men_pack_gate_error_from_snapshot(
			pack_meta_snapshot, maps_snapshot, host_resolution,
			_content_db.call("get_playable_unit_runtimes") as Dictionary
		),
		"cah_system": _cah_system_snapshot(),
	}
	if fresh:
		return context
	_skirmish_validation_context_cache = context
	return _skirmish_validation_context_cache


func validate_skirmish_faction(faction_id: String) -> String:
	## "" when this faction can be fielded, else the named refusal. Memoized.
	if _skirmish_availability.has(faction_id):
		return String(_skirmish_availability[faction_id])
	var started := Time.get_ticks_msec()
	var context := _skirmish_validation_context()
	var verdict := _compute_faction_availability(
		faction_id, context["probe"], context["manifest_script"],
		context["units"] as Dictionary, context["structures"] as Dictionary,
		context["pack_index"] as Dictionary, String(context["men_gate_error"]),
		lazy_script_failure(LAZY_SLICE), lazy_script_failure(LAZY_FACTION_MANIFEST),
		context.get("cah_system", {}) as Dictionary
	)
	_skirmish_availability[faction_id] = verdict
	_skirmish_on_pick_validation_ms += Time.get_ticks_msec() - started
	_memoize_skirmish_verdicts()
	return verdict


func validate_skirmish_map(map_id: String) -> String:
	## "" when this map's cooked terrain loads and validates, else the named
	## refusal. ONE map — the pick — instead of the whole catalog. Memoized.
	if _skirmish_map_notes.has(map_id):
		return String(_skirmish_map_notes[map_id])
	var started := Time.get_ticks_msec()
	var context := _skirmish_validation_context()
	var verdict := _compute_map_availability(
		map_id, context["probe"], context["map_data_script"],
		String(context["host_root"]), String(context["selected_root"]),
		context["maps"] as Dictionary, lazy_script_failure(LAZY_SLICE)
	)
	_skirmish_map_notes[map_id] = verdict
	_skirmish_on_pick_validation_ms += Time.get_ticks_msec() - started
	_memoize_skirmish_verdicts()
	return verdict


func _ensure_all_faction_verdicts() -> void:
	## Every faction, not just the picked ones: the army dropdowns have to be able
	## to grey out what cannot be fielded, and the seven of them together cost a
	## fraction of one map's terrain load. Each is memoized after the first ask.
	for faction_value in RETAIL_FACTIONS:
		validate_skirmish_faction(String((faction_value as Dictionary)["id"]))


func _memoize_skirmish_verdicts() -> void:
	## Persist what has been learned under the same content key the full sweep
	## uses, so a repeat pick of the same map is instant on the next launch too.
	if _skirmish_sweep_cache_key == "":
		return
	_store_skirmish_sweep_cache(_skirmish_sweep_cache_key, {
		"availability": _skirmish_availability,
		"map_notes": _skirmish_map_notes,
	})


func skirmish_on_pick_validation_ms() -> int:
	return _skirmish_on_pick_validation_ms


func skirmish_sweep_is_complete() -> bool:
	return _skirmish_sweep_complete


func skirmish_instant_build_ready_usec() -> int:
	return _skirmish_instant_build_ready_usec


func _skirmish_selection_is_fully_validated() -> bool:
	## True when `retail_launch_error()` can answer without validating anything.
	if not _skirmish_options_ready:
		return false
	for faction_value in RETAIL_FACTIONS:
		if not _skirmish_availability.has(String((faction_value as Dictionary)["id"])):
			return false
	var map_id := _selected_skirmish_map()
	return map_id == "" or _skirmish_map_notes.has(map_id)


func skirmish_sweep_worst_step_ms() -> int:
	## The longest single step of the availability sweep. Exposed so a runner can
	## pin the interactivity property directly: no frame of the stepped sweep may
	## grow back into a stall the player would feel as the menu freezing.
	return _skirmish_sweep_worst_ms


func skirmish_sweep_total_wall_ms() -> int:
	return _skirmish_sweep_total_ms


func skirmish_sweep_worker_compute_ms() -> int:
	return _skirmish_worker_compute_ms


func skirmish_sweep_worst_frame_name() -> String:
	return _skirmish_sweep_worst_frame_name


func compute_skirmish_synchronous_baseline_for_test() -> Dictionary:
	## Test-only oracle for the pre-change scheduling: identical validators and
	## ordering, run serially on the caller thread, with no publication or Control
	## mutation. The menu sweep runner compares its bytes with the worker result.
	_arm_skirmish_sweep()
	var started := Time.get_ticks_msec()
	var worst := 0
	var availability: Dictionary = {}
	var notes: Dictionary = {}
	var slice_script = _slice_script()
	var manifest_script = _faction_manifest_script()
	var map_data_script = load("res://src/retail_slice/retail_map_data.gd")
	var probe = slice_script.new() if slice_script != null else null
	var unit_snapshot := (_content_db.call("get_playable_unit_runtimes") as Dictionary).duplicate(true)
	var structure_snapshot := (_content_db.call("get_playable_structure_runtimes") as Dictionary).duplicate(true)
	var pack_index_snapshot := (_content_db.call("get_playable_unit_runtime_pack_index") as Dictionary).duplicate(true)
	var maps_snapshot := (_content_db.get("bundle_maps") as Dictionary).duplicate(true)
	var pack_meta_snapshot := (_content_db.get("pack_meta") as Array).duplicate(true)
	var host_resolution: Dictionary = PackCapabilityScript.resolve_host_slice_pack(pack_meta_snapshot)
	var host_root := String(host_resolution.get("root", ""))
	var selected_root := _selected_faction_pack_root()
	var men_gate_error := _men_pack_gate_error_from_snapshot(
		pack_meta_snapshot, maps_snapshot, host_resolution, unit_snapshot
	)
	for faction_value in RETAIL_FACTIONS:
		var step_started := Time.get_ticks_msec()
		var faction_id := String((faction_value as Dictionary)["id"])
		availability[faction_id] = _compute_faction_availability(
			faction_id, probe, manifest_script, unit_snapshot, structure_snapshot,
			pack_index_snapshot, men_gate_error,
			lazy_script_failure(LAZY_SLICE), lazy_script_failure(LAZY_FACTION_MANIFEST),
			_cah_system_snapshot())
		worst = maxi(worst, Time.get_ticks_msec() - step_started)
	for choice_value in _skirmish_map_choice_rows:
		var step_started := Time.get_ticks_msec()
		var map_id := String((choice_value as Dictionary)["id"])
		notes[map_id] = _compute_map_availability(
			map_id, probe, map_data_script, host_root, selected_root, maps_snapshot,
			lazy_script_failure(LAZY_SLICE))
		worst = maxi(worst, Time.get_ticks_msec() - step_started)
	var result := {
		"availability": availability,
		"map_notes": notes,
		"wall_ms": Time.get_ticks_msec() - started,
		"worst_step_ms": worst,
	}
	if probe != null:
		probe.free()
	return result


func _populate_one_row_controls(row: int) -> void:
	_populate_row_army(row)
	_populate_row_difficulty(row)
	_populate_row_team(row)
	_populate_row_color(row)
	_apply_row_controller(row)


func _append_row_control_sweep_steps(row: int) -> void:
	## OptionButton theme/layout work is not constant-time as an entire dropdown.
	## Build one item per frame so a cold glyph/theme cache cannot turn the first
	## row into a visible hitch.
	var army: OptionButton = solo_flyout.row_army_opts[row]
	_skirmish_sweep_steps.append({"name": "row_army_clear/%d" % row, "run": _sweep_clear_option.bind(army)})
	for faction_value in RETAIL_FACTIONS:
		_skirmish_sweep_steps.append({
			"name": "row_army_item/%d" % row,
			"run": _sweep_add_army_item.bind(army, faction_value as Dictionary),
		})
	_skirmish_sweep_steps.append({
		"name": "row_army_select/%d" % row, "run": _select_first_enabled.bind(army),
	})
	var difficulty: OptionButton = solo_flyout.row_difficulty_opts[row]
	_skirmish_sweep_steps.append({"name": "row_difficulty_clear/%d" % row, "run": _sweep_clear_option.bind(difficulty)})
	for tier_value in RETAIL_AI_DIFFICULTIES:
		_skirmish_sweep_steps.append({
			"name": "row_difficulty_item/%d" % row,
			"run": _sweep_add_metadata_item.bind(difficulty, String(tier_value["name"]), String(tier_value["id"])),
		})
	_skirmish_sweep_steps.append({
		"name": "row_difficulty_select/%d" % row,
		"run": _select_option_by_metadata_value.bind(difficulty, RETAIL_AI_DEFAULT_DIFFICULTY),
	})
	var team: OptionButton = solo_flyout.team_dropdowns[row]
	_skirmish_sweep_steps.append({"name": "row_team_clear/%d" % row, "run": _sweep_begin_team.bind(team)})
	for number in range(1, solo_flyout.MAX_PLAYER_ROWS + 1):
		_skirmish_sweep_steps.append({
			"name": "row_team_item/%d" % row,
			"run": _sweep_add_metadata_item.bind(team, str(number), number),
		})
	_skirmish_sweep_steps.append({"name": "row_team_select/%d" % row, "run": _sweep_finish_team.bind(team, row)})
	var color: OptionButton = solo_flyout.color_dropdowns[row]
	_skirmish_sweep_steps.append({"name": "row_color_clear/%d" % row, "run": _sweep_clear_option.bind(color)})
	for entry_value in HOUSE_COLORS:
		_skirmish_sweep_steps.append({
			"name": "row_color_item/%d" % row,
			"run": _sweep_add_metadata_item.bind(color, String(entry_value["name"]), entry_value["color"]),
		})
	_skirmish_sweep_steps.append({"name": "row_color_select/%d" % row, "run": _sweep_finish_color.bind(color, row)})
	_skirmish_sweep_steps.append({"name": "row_controller/%d" % row, "run": _apply_row_controller.bind(row)})


func _sweep_add_army_item(option: OptionButton, faction: Dictionary) -> void:
	var faction_id := String(faction["id"])
	var note := String(_skirmish_availability.get(faction_id, ""))
	option.add_item(String(faction["name"]) + (NOT_CONVERTED_SUFFIX if note != "" else ""))
	var index := option.item_count - 1
	option.set_item_metadata(index, faction_id)
	option.set_item_disabled(index, note != "")
	if note != "":
		option.set_item_tooltip(index, "Not converted: %s" % note)


func _sweep_clear_option(option: OptionButton) -> void:
	option.clear()


func _sweep_add_metadata_item(option: OptionButton, label: String, metadata: Variant) -> void:
	option.add_item(label)
	option.set_item_metadata(option.item_count - 1, metadata)


func _sweep_begin_team(option: OptionButton) -> void:
	option.clear()
	option.disabled = false


func _sweep_finish_team(option: OptionButton, row: int) -> void:
	option.select(mini(row, option.item_count - 1))
	option.tooltip_text = "Team/alliance: rows sharing a number fight as allies"


func _sweep_finish_color(option: OptionButton, row: int) -> void:
	option.select(row % HOUSE_COLORS.size())
	_on_color_changed(row)


func _sweep_step_build_next_map_row() -> void:
	if _skirmish_finalize_map_index >= _skirmish_map_choice_rows.size():
		return
	var index := _skirmish_finalize_map_index
	_skirmish_finalize_map_index += 1
	_build_skirmish_map_row(index, _skirmish_map_choice_rows[index])
	if _skirmish_finalize_map_index < _skirmish_map_choice_rows.size():
		_skirmish_sweep_steps.push_front({
			"name": "map_rows",
			"run": _sweep_step_build_next_map_row,
		})


func _build_skirmish_map_row(map_index: int, choice: Dictionary) -> void:
	var map_id := String(choice["id"])
	# UNKNOWN IS NOT UNAVAILABLE HERE, AND THAT IS NOT A FAIL-OPEN. The row is a
	# LIST ENTRY, not a launch: a map with no verdict yet is drawn selectable and
	# is deep-validated the moment it is picked (`validate_skirmish_map`), which
	# is where the fail-closed refusal now lives. Drawing ~80 rows greyed until a
	# ~90 s sweep finished is what made SOLO PLAY unusable.
	var note := String(_skirmish_map_notes.get(map_id, ""))
	var available := note == ""
	var map_doc := _content_db.call("get_bundle_map", map_id) as Dictionary
	if map_doc.is_empty() and map_id == SliceIds.MAP_ID:
		map_doc = _skirmish_map_document(map_id)
	var players := int(map_doc.get("playerCount", 0))
	var row := Button.new()
	row.name = "MapRow%d" % map_index
	row.toggle_mode = true
	row.alignment = HORIZONTAL_ALIGNMENT_LEFT
	row.text = String(choice["name"]) + ("" if available else " (unavailable)")
	row.custom_minimum_size = Vector2(0, 28)
	for state in ["normal", "hover", "pressed", "disabled"]:
		var row_box := StyleBoxFlat.new()
		row_box.bg_color = Color(0.05, 0.16, 0.09, 0.72) if state != "pressed" else Color(0.16, 0.34, 0.14, 0.92)
		if state == "hover":
			row_box.bg_color = Color(0.10, 0.26, 0.11, 0.88)
		if state == "disabled":
			row_box.bg_color = Color(0.04, 0.10, 0.06, 0.55)
		row_box.border_color = Color(0.30, 0.62, 0.28, 0.85) if state == "pressed" else Color(0.18, 0.42, 0.20, 0.6)
		row_box.set_border_width_all(1)
		row_box.content_margin_left = 12.0
		row.add_theme_stylebox_override(state, row_box)
	row.add_theme_font_size_override("font_size", 16)
	row.disabled = not available
	if not available:
		row.tooltip_text = "Unavailable: %s" % note
	row.set_meta("map_id", map_id)
	row.set_meta("player_count", players)
	var players_label := Label.new()
	players_label.name = "Players"
	players_label.text = str(players) if players > 0 else "?"
	players_label.set_anchors_and_offsets_preset(Control.PRESET_CENTER_RIGHT)
	players_label.offset_left = -48
	players_label.offset_right = -16
	players_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	players_label.add_theme_font_size_override("font_size", 15)
	players_label.add_theme_color_override("font_color", Color("b7dc94"))
	players_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	row.add_child(players_label)
	row.pressed.connect(_on_map_row_pressed.bind(map_index))
	solo_flyout.map_rows_host.add_child(row)
	solo_flyout.map_rows.append({"button": row, "map_id": map_id, "players": players})


func _populate_rules_options() -> void:
	solo_flyout.initial_resources_opt.clear()
	for value in RULES_RESOURCE_VALUES:
		solo_flyout.initial_resources_opt.add_item(str(value))
		solo_flyout.initial_resources_opt.set_item_metadata(solo_flyout.initial_resources_opt.item_count - 1, value)
	solo_flyout.cp_factor_opt.clear()
	for factor in RULES_FACTOR_VALUES:
		var label := "%sX" % str(factor).trim_suffix(".0")
		solo_flyout.cp_factor_opt.add_item(label)
		solo_flyout.cp_factor_opt.set_item_metadata(solo_flyout.cp_factor_opt.item_count - 1, factor)
	solo_flyout.build_mode_opt.clear()
	solo_flyout.build_mode_opt.add_item("BFME2 Freeform")
	solo_flyout.build_mode_opt.set_item_metadata(solo_flyout.build_mode_opt.item_count - 1, false)
	solo_flyout.build_mode_opt.add_item("BFME1 Plots")
	solo_flyout.build_mode_opt.set_item_metadata(solo_flyout.build_mode_opt.item_count - 1, true)
	_select_option_by_metadata_value(solo_flyout.initial_resources_opt, RULES_DEFAULT_RESOURCES)
	_select_option_by_metadata_value(solo_flyout.cp_factor_opt, RULES_DEFAULT_FACTOR)
	_select_option_by_metadata_value(solo_flyout.build_mode_opt, RULES_DEFAULT_BUILD_PLOTS_ONLY)


func _populate_color_options() -> void:
	# Colors are populated per row by _populate_row_controls(); kept as a thin
	# entry point so the boot sequence order in _ready() stays explicit.
	_populate_row_controls()


func _populate_row_controls() -> void:
	## Fills every current player row's Army / Difficulty / Team / Color dropdowns
	## and reflects each row's controller. Re-run whenever the row set is rebuilt
	## (add/remove/map-capacity clamp) so the new rows carry valid options.
	for row in range(solo_flyout.row_army_opts.size()):
		_populate_row_army(row)
		_populate_row_difficulty(row)
		_populate_row_team(row)
		_populate_row_color(row)
		_populate_row_hero(row)
		_apply_row_controller(row)


func _populate_row_army(row: int) -> void:
	var option: OptionButton = solo_flyout.row_army_opts[row]
	option.clear()
	for faction in RETAIL_FACTIONS:
		var faction_id := String(faction["id"])
		var note := String(_skirmish_availability.get(faction_id, ""))
		option.add_item(String(faction["name"]) + (NOT_CONVERTED_SUFFIX if note != "" else ""))
		var index := option.item_count - 1
		option.set_item_metadata(index, faction_id)
		option.set_item_disabled(index, note != "")
		if note != "":
			option.set_item_tooltip(index, "Not converted: %s" % note)
	_select_first_enabled(option)


func _populate_row_difficulty(row: int) -> void:
	var option: OptionButton = solo_flyout.row_difficulty_opts[row]
	option.clear()
	for tier in RETAIL_AI_DIFFICULTIES:
		option.add_item(String(tier["name"]))
		option.set_item_metadata(option.item_count - 1, String(tier["id"]))
	_select_option_by_metadata_value(option, RETAIL_AI_DEFAULT_DIFFICULTY)


func _populate_row_team(row: int) -> void:
	## The retail Team column is the alliance grouping: rows sharing a number are
	## allied. Default assigns each row its own number (row i -> i+1) so a fresh
	## setup is free-for-all (every team mutually hostile).
	var option: OptionButton = solo_flyout.team_dropdowns[row]
	option.clear()
	option.disabled = false
	for number in range(1, solo_flyout.MAX_PLAYER_ROWS + 1):
		option.add_item(str(number))
		option.set_item_metadata(option.item_count - 1, number)
	option.select(mini(row, option.item_count - 1))
	option.tooltip_text = "Team/alliance: rows sharing a number fight as allies"


func _populate_row_color(row: int) -> void:
	var option: OptionButton = solo_flyout.color_dropdowns[row]
	option.clear()
	for entry in HOUSE_COLORS:
		option.add_item(String(entry["name"]))
		option.set_item_metadata(option.item_count - 1, entry["color"])
	option.select(row % HOUSE_COLORS.size())
	_on_color_changed(row)


func _on_army_changed() -> void:
	# A row's army decides which saved heroes that row may bring, so the picks
	# are re-asked before anything reads them.
	_refresh_hero_rows()
	_refresh_skirmish_launch_state()


func _refresh_hero_rows() -> void:
	for row in range(solo_flyout.hero_dropdowns.size()):
		_populate_row_hero(row)


func _apply_row_controller(row: int) -> void:
	var option: OptionButton = solo_flyout.row_controller_opts[row]
	var is_human := option.selected >= 0 and option.get_item_text(option.selected) == "Human"
	solo_flyout.set_row_controller_is_human(row, is_human)


func _populate_row_hero(row: int) -> void:
	## The saved Create-a-Hero heroes this row may bring, keyed by hero id.
	##
	## FILTERED THREE WAYS, all of them the player's own doing: the row must be
	## the human's (an AI never brought a hero the player made), the host rule
	## must allow custom heroes, and the hero's subclass must name this row's
	## army in its UsableFactions - a Captain of Gondor is buildable by Men,
	## Elves and Dwarves and an Orc Raider is not, so changing the army re-asks
	## the question. "-" is always offered and always first: bringing no hero is
	## the default, not a fallback.
	if row >= solo_flyout.hero_dropdowns.size():
		return
	var option: OptionButton = solo_flyout.hero_dropdowns[row]
	# The REMEMBERED pick, not the selectable one: a disabled column reports no
	# hero (that is what makes the rule bite), so reading the selection back
	# through the same door would throw the choice away the moment the rule was
	# switched off, and switching it on again would offer a blank row.
	var previous := _remembered_row_hero_id(row)
	option.clear()
	option.add_item("-")
	option.set_item_metadata(0, "")
	option.select(0)
	var is_human := _row_is_human(row)
	option.disabled = not _row_may_bring_a_hero(row)
	option.tooltip_text = (
		"The hero from MY HEROES this player brings to the match" if not option.disabled
		else (
			"Only the human player brings a created hero" if not is_human
			else "Custom Heroes are disabled for this match"
		)
	)
	# An AI row never had a pick, so it is emptied. A human row keeps its list
	# even while the rule forbids using it - greyed out, still showing what was
	# chosen - so turning the rule back on restores the choice instead of a blank.
	if not is_human:
		return
	var system := _cah_system_runtime()
	if system.is_empty():
		return
	var faction := _selected_skirmish_faction(solo_flyout.row_army_opts[row])
	for profile in CahHeroesScript.load_profiles():
		if not CahHeroesScript.validate_profile(system, profile).is_empty():
			continue
		var sub_row := CahHeroesScript.sub_class_row(
			system, int(profile.get("classIndex", -1)), int(profile.get("subClassIndex", -1))
		)
		if not CahHeroesScript.subclass_allows_faction(sub_row, faction):
			continue
		option.add_item(CahHeroesScript.sanitize_name(String(profile.get("name", ""))))
		var index := option.item_count - 1
		var hero_id := String(profile.get("heroId", ""))
		option.set_item_metadata(index, hero_id)
		if hero_id == previous:
			option.select(index)


func _remembered_row_hero_id(row: int) -> String:
	## What this row picked, whether or not the rule currently lets it be used.
	if row >= solo_flyout.hero_dropdowns.size():
		return ""
	var option: OptionButton = solo_flyout.hero_dropdowns[row]
	if option.selected < 0:
		return ""
	return String(option.get_item_metadata(option.selected))


func _row_may_bring_a_hero(row: int) -> bool:
	return _row_is_human(row) and solo_flyout.custom_heroes_toggle.button_pressed


func _selected_row_hero_id(row: int) -> String:
	if row >= solo_flyout.hero_dropdowns.size():
		return ""
	var option: OptionButton = solo_flyout.hero_dropdowns[row]
	if option.selected < 0 or option.disabled:
		return ""
	return String(option.get_item_metadata(option.selected))


func _row_hero_documents(row: int) -> Array:
	## The row's pick, as the canonical document the per-seat roster path reads.
	## EXACTLY THE ONE PICKED - never the whole saved store, which is what the
	## single-player path used to field.
	if not _row_may_bring_a_hero(row):
		return []
	var hero_id := _selected_row_hero_id(row)
	if hero_id == "":
		return []
	var profile := CahHeroesScript.load_profile(hero_id)
	if profile.is_empty():
		return []
	var document := SessionScript.canonical_hero_document(profile)
	return [document] if document != "" else []


func _selected_row_difficulty(row: int) -> String:
	var option: OptionButton = solo_flyout.row_difficulty_opts[row]
	if option.selected < 0:
		return RETAIL_AI_DEFAULT_DIFFICULTY
	return String(option.get_item_metadata(option.selected))


func _selected_row_alliance(row: int) -> int:
	var option: OptionButton = solo_flyout.team_dropdowns[row]
	if option.selected < 0:
		return row + 1
	return int(option.get_item_metadata(option.selected))


func _selected_row_color(row: int) -> Color:
	var option: OptionButton = solo_flyout.color_dropdowns[row]
	if option.selected < 0:
		return HOUSE_COLORS[row % HOUSE_COLORS.size()]["color"]
	return option.get_item_metadata(option.selected)


func _row_is_human(row: int) -> bool:
	var option: OptionButton = solo_flyout.row_controller_opts[row]
	return option.selected >= 0 and option.get_item_text(option.selected) == "Human"


func _on_rows_changed() -> void:
	_populate_row_controls()
	_refresh_start_row()
	_refresh_skirmish_launch_state()


func _on_controller_changed(row: int) -> void:
	# Exactly one local human: choosing Human on a row demotes every other row to
	# AI (radio behavior), mirroring retail skirmish where you hold a single slot.
	if _row_is_human(row):
		for other in range(solo_flyout.row_controller_opts.size()):
			if other != row and _row_is_human(other):
				solo_flyout.row_controller_opts[other].select(1)
	elif _human_row_index() == -1:
		# The human slot may never vanish; keep this row human if it was the last.
		solo_flyout.row_controller_opts[row].select(0)
	for other in range(solo_flyout.row_controller_opts.size()):
		_apply_row_controller(other)
	# The human slot moved, so the hero column moves with it: the row that just
	# became AI must stop offering a hero and the new human row must start.
	_refresh_hero_rows()
	_refresh_skirmish_launch_state()


func _human_row_index() -> int:
	for row in range(solo_flyout.row_controller_opts.size()):
		if _row_is_human(row):
			return row
	return -1


func _on_color_changed(row: int) -> void:
	var option: OptionButton = solo_flyout.color_dropdowns[row]
	if option.selected < 0:
		return
	var color: Color = option.get_item_metadata(option.selected)
	solo_flyout.color_swatches[row].color = color
	# Rows 0/1 also drive the legacy two-side color fields so an unchanged default
	# setup writes byte-identical GameState.
	if row == 0:
		_game_state.set("retail_player_color", color)
	elif row == 1:
		_game_state.set("retail_enemy_color", color)


func _refresh_start_row() -> void:
	for child in solo_flyout.start_row.get_children():
		child.queue_free()
	solo_flyout.start_buttons.clear()
	solo_flyout.start_note.visible = false
	var map_id := _selected_skirmish_map()
	if map_id == "":
		return
	var starts := _read_map_start_indices(map_id)
	if starts.is_empty():
		solo_flyout.start_note.text = "Player starts are not converted for this map"
		solo_flyout.start_note.visible = true
		return
	for start_index in starts:
		var button := Button.new()
		button.name = "Start%d" % start_index
		button.text = str(start_index)
		button.toggle_mode = true
		button.custom_minimum_size = Vector2(44, 30)
		button.tooltip_text = "Take Player_%d_Start for your side; the AI takes another" % start_index
		button.pressed.connect(_on_start_button_pressed.bind(start_index))
		solo_flyout.start_row.add_child(button)
		solo_flyout.start_buttons.append(button)
	_sync_start_button_states()


func _on_start_button_pressed(start_index: int) -> void:
	var current := int(_game_state.get("retail_player_start_index"))
	_game_state.set("retail_player_start_index", 0 if current == start_index else start_index)
	_sync_start_button_states()


func _sync_start_button_states() -> void:
	var current := int(_game_state.get("retail_player_start_index"))
	for button in solo_flyout.start_buttons:
		button.button_pressed = int(button.text) == current


func _read_map_start_indices(map_id: String) -> Array[int]:
	var indices: Array[int] = []
	var map_doc := _skirmish_map_document(map_id)
	var source_path := String(map_doc.get("_source", ""))
	if source_path == "":
		var pack_root := String(map_doc.get("_pack_root", ""))
		var map_relative := String(map_doc.get("map", ""))
		if pack_root != "" and map_relative != "":
			source_path = pack_root.path_join(map_relative)
	if source_path == "":
		return indices
	var document := _read_bounded_json(source_path.get_base_dir().path_join("waypoints.json"), SliceIds.MAP_DOCUMENT_MAX_BYTES)
	for start_name in (document.get("playerStarts", {}) as Dictionary).keys():
		var row := (document.get("playerStarts", {}) as Dictionary)[start_name] as Dictionary
		var player_index := int(row.get("playerIndex", -1))
		if player_index > 0 and not indices.has(player_index):
			indices.append(player_index)
	indices.sort()
	return indices


func _select_option_by_metadata_value(option: OptionButton, metadata_value: Variant) -> void:
	for index in range(option.item_count):
		if option.get_item_metadata(index) == metadata_value:
			option.select(index)
			return


func _select_first_available_map_row() -> void:
	for index in solo_flyout.map_rows.size():
		if not (solo_flyout.map_rows[index]["button"] as Button).disabled:
			_on_map_row_pressed(index)
			return
	_on_map_row_pressed(-1)


func _select_first_available_map_row_without_refresh() -> void:
	## Availability finalization only selects the row. Preview image decoding and
	## waypoint reads belong to page presentation and run after the busy wait,
	## not inside the sweep's measured frames.
	for index in solo_flyout.map_rows.size():
		if not (solo_flyout.map_rows[index]["button"] as Button).disabled:
			solo_flyout.set_selected_map_row(index)
			return
	solo_flyout.set_selected_map_row(-1)


func _on_map_row_pressed(index: int) -> void:
	solo_flyout.set_selected_map_row(index)
	# The selected map's authored player count bounds how many rows the setup can
	# add. Clamping down may rebuild rows (rows_changed re-populates them).
	var capacity := _selected_map_player_capacity()
	if capacity >= solo_flyout.MIN_PLAYER_ROWS:
		solo_flyout.set_max_player_count(capacity)
	_refresh_map_preview()
	_refresh_map_description()
	_refresh_start_row()
	_refresh_skirmish_launch_state()


func _selected_map_player_capacity() -> int:
	## The selected map's authored player count, bounded to the setup ceiling. The
	## number of authored player starts is the hard cap (a team needs a spawn), so
	## the smaller of playerCount and the resolvable start count wins.
	var map_id := _selected_skirmish_map()
	if map_id == "":
		return solo_flyout.MIN_PLAYER_ROWS
	var map_doc := _skirmish_map_document(map_id)
	var declared := int(map_doc.get("playerCount", 0))
	var starts := _read_map_start_indices(map_id).size()
	var capacity := declared
	if starts > 0:
		capacity = starts if declared <= 0 else mini(declared, starts)
	return maxi(solo_flyout.MIN_PLAYER_ROWS, mini(capacity, solo_flyout.MAX_PLAYER_ROWS))


func _select_first_enabled(option: OptionButton) -> void:
	for index in range(option.item_count):
		if not option.is_item_disabled(index):
			option.select(index)
			return
	option.select(-1)


func _retail_faction_availability(faction_id: String) -> String:
	## "" when the slice can mount the faction, else the fail-closed note. Men
	## always requires the host pack gate. Every faction — Men included once
	## converted runtimes exist — runs exactly the manifest resolution the
	## slice runs over the slice's own fieldable-unit classification, so the
	## menu never errors on a unit the slice would exclude nor passes one the
	## slice cannot field.
	##
	## The manifest class is compiled on demand (its own chain is ~13,300 lines and
	## nothing before this point needs it). A failure to compile it is NOT treated
	## as "faction available": it becomes the faction's refusal note, so the army
	## dropdown disables the row and states the reason, exactly as it would for a
	## faction the slice cannot field.
	##
	## ONE IMPLEMENTATION. This used to be a hand-written second copy of
	## `_compute_faction_availability` — the sweep's validator — and two copies of
	## a fail-closed rule is how a menu comes to say yes to a faction the slice
	## then refuses. It is now the same function over a freshly-read context; only
	## the caching differs (this reader is deliberately uncached, because callers
	## ask it about content they have just changed).
	var context := _skirmish_validation_context(true)
	return _compute_faction_availability(
		faction_id, context["probe"], context["manifest_script"],
		context["units"] as Dictionary, context["structures"] as Dictionary,
		context["pack_index"] as Dictionary, String(context["men_gate_error"]),
		lazy_script_failure(LAZY_SLICE), lazy_script_failure(LAZY_FACTION_MANIFEST),
		context.get("cah_system", {}) as Dictionary
	)


func _slice_probe():
	## The classification/map-resolution probe. Untyped on purpose: annotating the
	## return as `RetailVerticalSlice` would make the type resolve at COMPILE time
	## and drag the slice's whole 60k-line chain back onto the menu's compile,
	## which is the entire cost this file was restructured to shed.
	##
	## Returns null when the slice class could not be compiled. Every caller
	## already handles null by reporting a named refusal - `_compute_map_availability`
	## says map resolution is unavailable, `_compute_faction_availability` returns
	## the slice's own compile failure by name - so a broken slice script closes
	## the launch gate rather than opening it.
	if _slice_probe_instance == null:
		var script = _slice_script()
		if script == null:
			return null
		_slice_probe_instance = script.new()
	return _slice_probe_instance


func _selected_faction_pack_root() -> String:
	var member := _content_db.call("get_bundle_object", SliceIds.SOLDIER_OBJECT_ID) as Dictionary
	return String(member.get("_pack_root", ""))


func _host_slice_pack_root() -> String:
	## Same capability host the slice boots from. Must NOT use the soldier
	## document's pack root: active RotWK Men can own the fighter while the
	## BFME2 host pack alone declares files.entryMap / host surfaces.
	var host_resolution: Dictionary = PackCapabilityScript.resolve_host_slice_pack(
		_content_db.get("pack_meta") as Array
	)
	return String(host_resolution.get("root", ""))


func _skirmish_map_choices() -> Array[Dictionary]:
	## Prefer maps actually mounted in ContentDB (BFME2 + RotWK catalogs).
	## Fall back to the historical hard-coded five only when no catalog maps
	## are registered yet. Cached for one sweep so arm/build/display agree.
	if not _skirmish_map_choice_rows.is_empty():
		return _skirmish_map_choice_rows
	var rows: Array = _content_db.call("list_catalog_maps") as Array
	var choices: Array[Dictionary] = []
	# Always surface the host entry map first when present (historical Fords).
	var seen: Dictionary = {}
	var host_entry := SliceIds.MAP_ID
	if (_content_db.get("bundle_maps") as Dictionary).has(host_entry) or _host_slice_pack_root() != "":
		choices.append({"id": host_entry, "name": RETAIL_MAP_NAME})
		seen[host_entry] = true
	for row_value in rows:
		var row := row_value as Dictionary
		if row == null:
			continue
		var map_id := String(row.get("id", ""))
		if map_id == "" or seen.has(map_id):
			continue
		var display := String(row.get("name", map_id))
		if display == "":
			display = map_id
		choices.append({"id": map_id, "name": display})
		seen[map_id] = true
	if choices.is_empty():
		for fallback in RETAIL_MAP_CHOICES:
			choices.append((fallback as Dictionary).duplicate())
	_skirmish_map_choice_rows = choices
	return _skirmish_map_choice_rows


func _men_pack_gate_error() -> String:
	## The first fail-closed checks retail_vertical_slice runs for the default
	## Men manifest: soldier/horde/map bundle documents, the soldier animation
	## capability, and a mounted pack that can host the match.
	## THE LEGACY PROBE IS THE LAST RESORT, not the first. Its three documents are
	## the shapes the old bfme2-men-vslice pack shipped; a selection that provides
	## Men in the current shape carries playable-unit runtimes instead, and asking
	## it for a bundle object told the player Men was "not converted" while six
	## other factions - which never came through here - loaded from that same
	## selection. So when there is modern content mounted, Men is left to the same
	## per-faction classification every other faction gets, and only the question
	## that classification cannot answer is asked here: can anything host a match.
	if (_content_db.call("get_playable_unit_runtimes") as Dictionary).is_empty():
		var member := _content_db.call("get_bundle_object", SliceIds.SOLDIER_OBJECT_ID) as Dictionary
		var horde := _content_db.call("get_bundle_object", SliceIds.SOLDIER_HORDE_ID) as Dictionary
		var map_definition := _content_db.call("get_bundle_map", SliceIds.MAP_ID) as Dictionary
		if member.is_empty() or horde.is_empty() or map_definition.is_empty():
			return "no selected content pack provides the Men faction (run run_importer.bat)"
		var capability := _content_db.call("get_animation_capability", String(member.get("animationCapabilityId", ""))) as Dictionary
		if capability.is_empty():
			return "the selected pack's soldier animation capability is missing"
	# Ask the SAME question the slice's host resolver asks, through the SAME
	# function: a menu that passes while the slice refuses is a launch that dies
	# after the loading screen. Two copies of the walk is how they came to
	# disagree before, so there is now only one (pack_capability.gd).
	#
	# The question is the admission contract - the surfaces without which a match
	# cannot run - and nothing more. It is not "does this pack look like the pack
	# I remember": gating on the literal id refused the owner's own six-faction
	# selection, which is larger than the pack the id named, and gating on
	# presentation surfaces like menOrderHint would refuse a pack that plays
	# (measured: 351/1, a synthetic order hint instead of the retail one).
	var host_resolution: Dictionary = PackCapabilityScript.resolve_host_slice_pack(
		_content_db.get("pack_meta") as Array
	)
	if String(host_resolution.get("root", "")) != "":
		return ""
	# Fails closed and loudly: name every mounted pack and what each lacks, so
	# an unsuitable selection is a named refusal the player can act on.
	return String(host_resolution.get("error", "no mounted content pack can host a match"))


func _men_pack_gate_error_from_snapshot(
	_pack_meta_snapshot: Array,
	maps_snapshot: Dictionary,
	host_resolution: Dictionary,
	unit_snapshot: Dictionary = {}
) -> String:
	# Called on the main thread before dispatch. The returned string is the only
	# Men gate state the worker consumes; pack_meta/maps are private snapshots.
	#
	# THIS GATE USED TO ASK FOR ONE PACK BY ITS CONTENTS. It looked for the
	# gondor-fighter BUNDLE object and the Fords of Isen II bundle map, which are
	# the shapes the old bfme2-men-vslice pack shipped - so a selection that
	# provides Men perfectly well in the CURRENT shape (playable-unit runtimes,
	# which is how all seven RotWK factions arrive) was told Men was "not
	# converted" and the PLAY button was refused. Six other factions passed the
	# whole time, because only Men was ever routed through here.
	#
	# So: when the mounted content speaks the current shape at all, Men is
	# validated the way every other faction already is - classify it and read its
	# manifest's own verdict, a few lines below in _compute_faction_availability.
	# The legacy probe remains for a legacy-only selection, where it is still the
	# only evidence there is. What stays unconditional is the one thing the
	# per-faction check cannot answer: whether ANY mounted pack can host a match.
	if unit_snapshot.is_empty():
		var member := _content_db.call("get_bundle_object", SliceIds.SOLDIER_OBJECT_ID) as Dictionary
		var horde := _content_db.call("get_bundle_object", SliceIds.SOLDIER_HORDE_ID) as Dictionary
		var map_definition := maps_snapshot.get(SliceIds.MAP_ID, {}) as Dictionary
		if member.is_empty() or horde.is_empty() or map_definition.is_empty():
			return "no selected content pack provides the Men faction (run run_importer.bat)"
		var capability := _content_db.call(
			"get_animation_capability", String(member.get("animationCapabilityId", ""))
		) as Dictionary
		if capability.is_empty():
			return "the selected pack's soldier animation capability is missing"
	if String(host_resolution.get("root", "")) != "":
		return ""
	return String(host_resolution.get("error", "no mounted content pack can host a match"))


func get_retail_faction_availability() -> Dictionary:
	## The complete faction table. Public readers get every verdict, not just the
	## picked ones — the seven of them together are a fraction of one map's
	## terrain load, and a partial table is what would let a caller read "" for a
	## faction nobody classified. Memoized, so asking twice costs once.
	if not _ensure_skirmish_options():
		return {}
	_ensure_all_faction_verdicts()
	return _skirmish_availability.duplicate()


func retail_launch_error() -> String:
	## "" when a skirmish launch may proceed, else the player-facing reason it
	## is blocked. A blocked launch never falls back silently. Every row is
	## validated fail-closed: its faction must be convertible (the same per-faction
	## availability signal the slice uses), the roster must contain at least two
	## mutually-hostile alliances, and each team must claim a distinct authored
	## player start.
	##
	## VALIDATE ON PICK. This is where the ~90 s catalog-wide sweep used to be a
	## precondition. It is not one any more: the list renders instantly and THIS
	## function does the deep work, for the selected map and the faction verdicts
	## only, through the same validators the background sweep uses. Every answer
	## is memoized, so the second call — and the second launch over unchanged
	## content — is free.
	##
	## Guarded: a launch may never be judged against an unpopulated list.
	if not _ensure_skirmish_options():
		return "Skirmish availability is still loading."
	_ensure_all_faction_verdicts()
	var selected_map := _selected_skirmish_map()
	if selected_map != "":
		validate_skirmish_map(selected_map)
	return _skirmish_launch_error_from_known_verdicts()


func _skirmish_launch_error_from_known_verdicts() -> String:
	## The launch rules, applied to whatever verdicts are KNOWN. `retail_launch_error()`
	## calls it after making the relevant verdicts known; `_refresh_skirmish_launch_state()`
	## calls it directly so that merely opening the page, changing a team number or
	## clicking a map row never pays for a terrain load. One implementation, two
	## callers — the button and the launch cannot disagree about the rules.
	var host_error := _men_pack_gate_error()
	if host_error != "":
		return "The Open BFME host pack is unavailable: %s." % host_error
	var map_id := _selected_skirmish_map()
	if map_id == "":
		return "No retail map is selectable. Mount a skirmish map pack in selection.json (BFME2 or RotWK catalog) and ensure the host pack has files.entryMap for Fords."
	# THE SELECTED MAP'S OWN VERDICT. It used to be enforced only by the map row
	# being drawn disabled, which was only possible because every map had been
	# terrain-loaded before the list appeared. The rows are drawn from the catalog
	# now, so the refusal has to be stated here — `retail_launch_error()` has
	# already validated exactly this map before calling.
	var map_note := String(_skirmish_map_notes.get(map_id, ""))
	if map_note != "":
		return "%s cannot be played: %s." % [_retail_map_display_name(map_id), map_note]
	var row_count: int = solo_flyout.row_army_opts.size()
	for row in range(row_count):
		var faction_id := _selected_skirmish_faction(solo_flyout.row_army_opts[row])
		if faction_id == "":
			return "No converted faction is selectable yet. Convert a faction pack first."
		# Unknown is not a refusal HERE: `retail_launch_error()` makes every
		# faction verdict known before it asks, so an absent verdict on this path
		# only ever means "the button state is being refreshed mid-warm".
		var note := String(_skirmish_availability.get(faction_id, ""))
		if note != "":
			return "Player %d faction %s is not converted yet: %s." % [row + 1, _retail_faction_display_name(faction_id), note]
	if _human_row_index() == -1:
		return "One slot must be the local player (Human)."
	var alliances: Dictionary = {}
	for row in range(row_count):
		alliances[_selected_row_alliance(row)] = true
	if alliances.size() < 2:
		return "All players share one team; a skirmish needs at least two hostile teams."
	if _assign_start_indices(map_id, row_count).is_empty():
		return "This map provides fewer authored player starts than the %d players selected." % row_count
	return ""


func _assign_start_indices(map_id: String, row_count: int) -> Array[int]:
	## One distinct authored player start per row, or [] when the map cannot seat
	## every team. Row 0 (the human) keeps its manually chosen start when valid;
	## the rest take the remaining authored starts in ascending order.
	var starts := _read_map_start_indices(map_id)
	if starts.size() < row_count:
		return []
	var assigned: Array[int] = []
	var pool := starts.duplicate()
	var human_start := int(_game_state.get("retail_player_start_index"))
	if human_start > 0 and pool.has(human_start):
		assigned.append(human_start)
		pool.erase(human_start)
	else:
		assigned.append(int(pool.pop_front()))
	for _row in range(1, row_count):
		assigned.append(int(pool.pop_front()))
	return assigned


func _selected_skirmish_faction(option: OptionButton) -> String:
	if option.selected < 0:
		return ""
	return String(option.get_item_metadata(option.selected))


func _retail_faction_display_name(faction_id: String) -> String:
	for faction in RETAIL_FACTIONS:
		if String(faction["id"]) == faction_id:
			return String(faction["name"])
	return faction_id.capitalize()


func _retail_map_display_name(map_id: String) -> String:
	for map_choice in _skirmish_map_choices():
		if String(map_choice["id"]) == map_id:
			return String(map_choice["name"])
	for map_choice in RETAIL_MAP_CHOICES:
		if String(map_choice["id"]) == map_id:
			return String(map_choice["name"])
	if map_id.begins_with("bfme2.map."):
		return map_id.trim_prefix("bfme2.map.").capitalize()
	if map_id.begins_with("rotwk.map."):
		return map_id.trim_prefix("rotwk.map.").capitalize()
	return map_id.capitalize()


func _skirmish_map_document(map_id: String) -> Dictionary:
	## Host entry map (default Fords) comes from the capability host pack's
	## files.entryMap — same root the slice boots. Every other map resolves from
	## registered ContentDB catalog content.
	if map_id == SliceIds.MAP_ID:
		var probe = _slice_probe()
		var host_root := _host_slice_pack_root()
		if probe != null and host_root != "":
			var entry_doc: Dictionary = probe._resolve_pack_entry_map_definition(host_root, map_id)
			if not entry_doc.is_empty():
				return entry_doc
	return _content_db.call("get_bundle_map", map_id) as Dictionary


func _refresh_map_preview(_index: int = 0) -> void:
	## Shows the selected map's converted preview art from the pack that owns
	## the map document. For the Fords entry map that is the selected faction
	## pack's files.entryMap — the same document the slice boots — so a
	## supplemental map pack's variant never shadows the selected pack's art.
	## Fail closed: no synthetic stand-in is ever drawn when the art is absent.
	var map_id := _selected_skirmish_map()
	var texture: Texture2D = null
	var caption := ""
	if map_id != "":
		var map_doc := _skirmish_map_document(map_id)
		caption = String(map_doc.get("name", map_doc.get("displayName", "")))
		var preview_rel := String(map_doc.get("preview", ""))
		var pack_root := String(map_doc.get("_pack_root", ""))
		if preview_rel != "" and pack_root != "":
			var path := String(_content_db.call("resolve_asset", preview_rel, pack_root))
			if path != "" and FileAccess.file_exists(path):
				var image := Image.load_from_file(path)
				if image != null and not image.is_empty():
					texture = ImageTexture.create_from_image(image)
	solo_flyout.preview_image.texture = texture
	solo_flyout.preview_image.visible = texture != null
	solo_flyout.preview_caption.text = caption if caption != "" else _retail_map_display_name(map_id)


func _refresh_map_description() -> void:
	## Description text comes only from the map's authored document. No map in
	## the converted packs carries a description field today, so the panel
	## honestly records that instead of inventing lore.
	var map_id := _selected_skirmish_map()
	solo_flyout.description_title.text = _retail_map_display_name(map_id) if map_id != "" else ""
	var description := ""
	if map_id != "":
		var map_doc := _skirmish_map_document(map_id)
		description = String(map_doc.get("description", map_doc.get("lore", ""))).strip_edges()
	solo_flyout.description_label.text = description if description != "" else "No authored description has been converted for this map yet."


func _refresh_skirmish_launch_state(_index: int = 0) -> void:
	## SHALLOW ON PURPOSE. This runs on every page open, army change, team change
	## and map-row click, so it may not terrain-load anything: it judges the setup
	## against the verdicts already known and leaves the deep check to the press.
	if not _ensure_skirmish_options():
		solo_flyout.play_btn.disabled = true
		solo_flyout.hint_label.text = "Checking skirmish content…"
		return
	var launch_error := _skirmish_launch_error_from_known_verdicts()
	solo_flyout.play_btn.disabled = launch_error != ""
	if launch_error != "":
		solo_flyout.hint_label.text = launch_error
		return
	var pending: Array[String] = []
	var unknown := 0
	for faction in RETAIL_FACTIONS:
		var faction_id := String(faction["id"])
		if not _skirmish_availability.has(faction_id):
			unknown += 1
		elif String(_skirmish_availability[faction_id]) != "":
			pending.append(String(faction["name"]).to_upper())
	if not pending.is_empty():
		solo_flyout.hint_label.text = "NOT CONVERTED YET: %s" % ", ".join(pending)
	elif unknown > 0 or not _skirmish_map_notes.has(_selected_skirmish_map()):
		# Honest about what has and has not been checked. The alternative — an
		# unqualified "ALL FACTIONS CONVERTED" over verdicts nobody has computed —
		# would be the shell asserting something it does not know.
		solo_flyout.hint_label.text = "YOUR MAP AND ARMIES ARE CHECKED WHEN YOU PRESS PLAY"
	else:
		solo_flyout.hint_label.text = "ALL FACTIONS CONVERTED"


func _clear_wotr_battle_seam() -> void:
	_game_state.set("wotr_handoff", {})
	_game_state.set("wotr_battle_winner", -1)
	_game_state.set("wotr_battle_transport", {})
	_game_state.set("wotr_battle_report", {})


func apply_skirmish_selection() -> bool:
	## Validates the skirmish setup fail-closed and records it on GameState
	## for the retail slice. On failure GameState is left untouched and the
	## launch button re-syncs to the blocked state.
	var launch_error := retail_launch_error()
	if launch_error != "":
		_refresh_skirmish_launch_state()
		return false
	# A non-WotR launch is an explicit lifecycle boundary for every consume-once
	# strategic/tactical record.
	_clear_wotr_battle_seam()
	# A solo launch is always single-player: clear any multiplayer selection a
	# previous NETWORK visit left behind so the slice never hosts by surprise.
	_game_state.set("retail_mp_mode", "")
	var map_id := _selected_skirmish_map()
	# Legacy two-side fields: row 0 is the human, the first AI slot is the "enemy".
	# They stay authoritative for the byte-identical default launch and remain the
	# fallback whenever no N-team descriptor list is present (retail_team_setup []).
	var human_row := maxi(0, _human_row_index())
	var enemy_row := _first_non_human_row()
	_game_state.set("retail_player_faction", _selected_skirmish_faction(solo_flyout.row_army_opts[human_row]))
	_game_state.set("retail_enemy_faction", _selected_skirmish_faction(solo_flyout.row_army_opts[enemy_row]))
	_game_state.set("retail_map_id", map_id if map_id != "" else SliceIds.MAP_ID)
	_game_state.set("retail_initial_resources", _selected_rules_resources())
	_game_state.set("retail_command_point_factor", _selected_rules_factor())
	_game_state.set("retail_build_plots_only", _selected_build_plots_only())
	_game_state.set("retail_allow_ring_heroes", solo_flyout.ring_heroes_toggle.button_pressed)
	_game_state.set("retail_logic_random_seed", _single_player_logic_seed(map_id))
	# N-team descriptor list: authoritative when present. Only written for setups
	# the legacy pair cannot express (>2 rows, or a non-medium AI tier); the exact
	# legacy default clears it so the slice keeps its proven two-team path and the
	# pinned battle signature is untouched.
	if _setup_is_advanced():
		_game_state.set("retail_team_setup", _build_team_descriptors(map_id))
	else:
		_game_state.set("retail_team_setup", [])
	return true


func _first_non_human_row() -> int:
	for row in range(solo_flyout.row_army_opts.size()):
		if not _row_is_human(row):
			return row
	return mini(1, solo_flyout.row_army_opts.size() - 1)


func _setup_is_advanced() -> bool:
	## True when the setup carries something the legacy two-side fields cannot
	## represent, and therefore must be handed to the slice as a descriptor list.
	return _setup_is_advanced_rows() or _setup_carries_a_hero_decision()


func _setup_is_advanced_rows() -> bool:
	## The original two conditions: more than two rows, or an AI off medium.
	if solo_flyout.row_army_opts.size() > 2:
		return true
	for row in range(solo_flyout.row_army_opts.size()):
		if not _row_is_human(row) and _selected_row_difficulty(row) != RETAIL_AI_DEFAULT_DIFFICULTY:
			return true
	return false


func _setup_carries_a_hero_decision() -> bool:
	## WHICH HERO THE PLAYER BRINGS IS SUCH A THING, and it was not counted.
	##
	## The legacy pair carries two factions and nothing else, so a default 1v1
	## wrote no descriptor list at all - and with no list the slice falls back to
	## fielding EVERY saved hero. The dropdown and the Allow Custom Heroes toggle
	## were therefore inert in exactly the setup almost everyone plays: pick one
	## hero and get all of them, turn the rule off and still get all of them.
	##
	## A saved hero is enough to make the setup advanced, whether or not one is
	## picked, because "-" is a DECISION - it means bring none - and the legacy
	## path cannot say it. With no saved heroes there is nothing to decide and
	## nothing to field either way, so the default launch keeps its proven
	## two-team path and the pinned battle signature is untouched.
	return not CahHeroesScript.load_profiles().is_empty()


func _build_team_descriptors(map_id: String) -> Array:
	## The full N-team roster the slice hands to the sim. One descriptor per row,
	## each carrying its sim team id (skipping NEUTRAL_TEAM), faction, controller,
	## AI difficulty, alliance number, house color, and a distinct authored start.
	var row_count: int = solo_flyout.row_army_opts.size()
	var starts := _assign_start_indices(map_id, row_count)
	var descriptors: Array = []
	for row in range(row_count):
		var is_human := _row_is_human(row)
		descriptors.append({
			"team": TEAM_ID_POOL[row],
			"faction": _selected_skirmish_faction(solo_flyout.row_army_opts[row]),
			"controller": CONTROLLER_HUMAN if is_human else CONTROLLER_AI,
			"difficulty": RETAIL_AI_DEFAULT_DIFFICULTY if is_human else _selected_row_difficulty(row),
			"alliance": _selected_row_alliance(row),
			"color": _selected_row_color(row),
			"start_index": starts[row] if row < starts.size() else 0,
			"heroes": _row_hero_documents(row),
		})
	return descriptors


func _selected_rules_resources() -> int:
	var option: OptionButton = solo_flyout.initial_resources_opt
	if option.selected < 0:
		return RULES_DEFAULT_RESOURCES
	return int(option.get_item_metadata(option.selected))


func _selected_rules_factor() -> float:
	var option: OptionButton = solo_flyout.cp_factor_opt
	if option.selected < 0:
		return RULES_DEFAULT_FACTOR
	return float(option.get_item_metadata(option.selected))


func _selected_build_plots_only() -> bool:
	var option: OptionButton = solo_flyout.build_mode_opt
	if option.selected < 0:
		return RULES_DEFAULT_BUILD_PLOTS_ONLY
	return bool(option.get_item_metadata(option.selected))


func _single_player_logic_seed(map_id: String) -> int:
	## Varied by the complete visible match choice, deterministic for replaying
	## that configuration. `hash()` is deliberately avoided: its implementation
	## is not the cross-version simulation contract.
	var material := "%s|%d|%.3f|%s|%s" % [
		map_id, _selected_rules_resources(), _selected_rules_factor(),
		_selected_skirmish_faction(solo_flyout.row_army_opts[maxi(0, _human_row_index())]),
		_selected_skirmish_faction(solo_flyout.row_army_opts[_first_non_human_row()]),
	]
	var value := 0x811C9DC5
	for byte in material.to_utf8_buffer():
		value = ((value ^ int(byte)) * 16777619) & 0x7FFFFFFF
	return value


func _on_rules_changed(_index: int = 0) -> void:
	_game_state.set("retail_initial_resources", _selected_rules_resources())
	_game_state.set("retail_command_point_factor", _selected_rules_factor())
	_game_state.set("retail_build_plots_only", _selected_build_plots_only())


func _on_rules_reset() -> void:
	_select_option_by_metadata_value(solo_flyout.initial_resources_opt, RULES_DEFAULT_RESOURCES)
	_select_option_by_metadata_value(solo_flyout.cp_factor_opt, RULES_DEFAULT_FACTOR)
	_select_option_by_metadata_value(solo_flyout.build_mode_opt, RULES_DEFAULT_BUILD_PLOTS_ONLY)
	solo_flyout.ring_heroes_toggle.button_pressed = true
	_on_rules_changed()


func _selected_skirmish_map() -> String:
	if solo_flyout.selected_map_row < 0 or solo_flyout.selected_map_row >= solo_flyout.map_rows.size():
		return ""
	var row: Dictionary = solo_flyout.map_rows[solo_flyout.selected_map_row]
	if (row["button"] as Button).disabled:
		return ""
	return String(row["map_id"])


func retail_map_availability(map_id: String) -> String:
	## "" when the slice resolves and boots the map, else the honest reason it
	## is disabled. Host entry map (default Fords) uses the capability host pack
	## root — same as retail_vertical_slice boot. Other maps use ContentDB
	## registered catalog documents (BFME2 and/or RotWK).
	##
	## ONE IMPLEMENTATION, uncached. This was a hand-written second copy of
	## `_compute_map_availability` — the sweep's and the on-pick gate's validator.
	## It now calls that function over a freshly-read context, so a map can never
	## be judged one way by the launch gate and another way by this reader.
	## `validate_skirmish_map()` is the memoizing form used by the launch path;
	## this one deliberately re-resolves, because its callers mutate cooked
	## fixtures between calls and expect the answer to move with them.
	var context := _skirmish_validation_context(true)
	return _compute_map_availability(
		map_id, context["probe"], context["map_data_script"],
		String(context["host_root"]), String(context["selected_root"]),
		context["maps"] as Dictionary, lazy_script_failure(LAZY_SLICE)
	)


func _read_bounded_json(path: String, maximum_bytes: int) -> Dictionary:
	## Bounded JSON read matching the slice's pack-document guardrails.
	if maximum_bytes <= 0 or not FileAccess.file_exists(path):
		return {}
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null or file.get_length() <= 0 or file.get_length() > maximum_bytes:
		return {}
	var text := file.get_as_text()
	file.close()
	var value: Variant = JSON.parse_string(text)
	return (value as Dictionary) if typeof(value) == TYPE_DICTIONARY else {}


# --- War of the Ring ---------------------------------------------------------

## "" when War of the Ring can be entered, else the player-facing reason it
## cannot. The reason is the one the document search itself produced - it names
## both places that were searched and the command that generates a document -
## because "unavailable" alone sends nobody anywhere.
func wotr_unavailable_reason() -> String:
	return _wotr_unavailable_reason


func _locate_wotr_document() -> void:
	## Look for the living-world document in the packs the game actually mounted
	## first, then the documented workspace/environment path. NO FALLBACK MAP
	## EXISTS: when nothing is found this records the reason and War of the Ring
	## stays shut. A fabricated strategic map is indistinguishable from a real one
	## once it is on screen, which is what makes that failure mode so expensive.
	var roots: Array = []
	for meta_value in (_content_db.get("pack_meta") as Array):
		roots.append(String((meta_value as Dictionary).get("root", "")))
	roots.sort()
	var found: Dictionary = WotrSessionScript.locate_document(roots)
	if not bool(found.get("ok", false)):
		_wotr_document = {}
		_wotr_document_path = ""
		_wotr_document_source = ""
		_wotr_unavailable_reason = String(found.get("reason", "no living-world document is available"))
		return
	_wotr_document = found["document"] as Dictionary
	_wotr_document_path = String(found["path"])
	_wotr_document_source = String(found["source"])
	_wotr_unavailable_reason = ""


func _refresh_wotr_entry() -> void:
	## ONE source of truth, two surfaces. `wotr_btn` is the canonical entry the
	## rest of the code (and the round-trip runner) reads; the SOLO PLAY flyout
	## row is the surface the player clicks. Both are written from
	## `_wotr_unavailable_reason` here, in the same call, so the shell can never
	## offer a campaign the menu has already refused — or hide one it would open.
	var blocked := _wotr_unavailable_reason != ""
	if wotr_btn != null:
		wotr_btn.disabled = blocked
		wotr_btn.text = "WAR OF THE RING" if not blocked else "WAR OF THE RING (UNAVAILABLE)"
		wotr_btn.tooltip_text = _wotr_unavailable_reason
	var solo_flyout_menu = _shell_flyouts.get("solo", null)
	if solo_flyout_menu != null:
		var item := _wotr_flyout_item()
		solo_flyout_menu.set_item_state(
			"wotr", bool(item["enabled"]), String(item["tooltip"]))


## Start a campaign on the located document. Fails closed and reports: seats come
## from the document's own player templates, in sorted order, restricted to the
## ones whose faction the tactical layer can actually field, and the scenario is
## the campaign's first startable two-seat scenario.
##
## `chosen` is the GAME SETUP screen's `{scenario, seats}` when the player came
## through it, and EMPTY when they did not - `show_page("wotr")` and the return
## from a tactical battle both arrive without one.
##
## THE SEATING USED TO BE FIXED, and said so. It is now the player's, through
## retail's own GAME SETUP screen, and the fixed seating below survives as the
## fallback for the two callers that legitimately have no chooser in front of
## them. EITHER WAY THE SESSION DECIDES: `begin()` refuses a document that will
## not load, fewer than two seats, and a scenario with no ownership to apply,
## and this function reports its refusal rather than working around it.
func _start_wotr_session(chosen: Dictionary = {}) -> bool:
	# A fresh strategic campaign cannot inherit any consume-once battle seam from
	# an earlier launch, including one whose return was refused.
	_clear_wotr_battle_seam()
	# Setup always authors hero_id, including "-" as an explicit empty choice.
	# The no-argument route is resume/open and must retain the existing pick.
	if chosen.has("hero_id"):
		_wotr_selected_hero_document = ""
	var chosen_hero_id := String(chosen.get("hero_id", ""))
	if not chosen_hero_id.is_empty():
		var profile := CahHeroesScript.load_profile(chosen_hero_id)
		var system := _cah_system_runtime()
		var hero_refusals: Array[String] = []
		if profile.is_empty():
			hero_refusals.append("profile not found")
		elif system.is_empty():
			hero_refusals.append("Create-a-Hero system unavailable")
		else:
			hero_refusals = CahHeroesScript.validate_profile(system, profile)
		if not hero_refusals.is_empty():
			_wotr_unavailable_reason = "the selected created hero was refused: %s" % ", ".join(hero_refusals)
			return false
		_wotr_selected_hero_document = SessionScript.canonical_hero_document(profile)
		if _wotr_selected_hero_document.is_empty():
			_wotr_unavailable_reason = "the selected created hero could not be canonicalised"
			return false
	# Seat options are filtered by per-faction availability below.
	if not _ensure_skirmish_options():
		return false
	if _wotr_document.is_empty():
		return false
	var probe = WotrSessionScript.new()
	var probe_world = _new_wotr_world()
	if probe_world == null:
		_wotr_unavailable_reason = lazy_script_failure(LAZY_WOTR_WORLD)
		return false
	if not probe_world.load_from_dict(_wotr_document, ""):
		_wotr_unavailable_reason = "the living-world document did not load: %s" % str(probe_world.errors)
		return false
	probe.world = probe_world
	var seats: Array = chosen.get("seats", []) as Array
	var seats_were_chosen := not seats.is_empty()
	var seatable: Array[String] = []
	if seats.is_empty():
		for option in probe.seat_options(_skirmish_availability):
			if String(option["unavailable_reason"]) != "":
				continue
			seatable.append(String(option["template"]))
			seats.append({
				"template": String(option["template"]),
				"team": seats.size() + 1,
				"controller": WotrStateScript.CONTROLLER_HUMAN if seats.is_empty() else WotrStateScript.CONTROLLER_AI,
			})
			if seats.size() == 2:
				break
		# Everything past the first two, so the opponent swap below has candidates.
		for option in probe.seat_options(_skirmish_availability):
			if String(option["unavailable_reason"]) != "":
				continue
			var template := String(option["template"])
			if not seatable.has(template):
				seatable.append(template)
	if seats.size() < 2:
		_wotr_unavailable_reason = "fewer than two of the campaign's factions are converted, so no War of the Ring session can be seated"
		return false
	var scenario := String(chosen.get("scenario", ""))
	if scenario.is_empty():
		# NO CHOOSER IN FRONT OF US, so this fallback takes a scenario that needs
		# nothing choosing: `startable_scenarios(2)` without the freeform flag,
		# i.e. one whose ownership is authored. A freeform scenario needs a start
		# territory per seat and there is nobody here to pick them.
		var scenarios := probe.startable_scenarios(2)
		if scenarios.is_empty():
			_wotr_unavailable_reason = "the document's campaign carries no scenario that seats two players with authored territory"
			return false
		scenario = String(scenarios[0])
	var session = WotrSessionScript.new()
	# THE AUTO-RESOLVE BUNDLES LOAD FIRST, BEFORE `begin()`, AND THE ORDER IS NOT
	# COSMETIC.
	#
	# `begin()` builds `state.roster_units` (session line "BEFORE any army is
	# placed, because `place_army()` copies the units onto the army record") and
	# `_build_roster_units()` returns `{}` outright when `autoresolve` or
	# `autoresolve_bindings` is null. This call used to come AFTER `begin()` - down
	# in `_seat_an_opponent_that_can_fight()` - so EVERY army in the first session
	# was placed with an empty unit list regardless of faction, every seat looked
	# like it "could not fight", and the reseating below fired on a hole this file
	# had just dug itself. Loading here fixes the seats and the diagnosis at once.
	#
	# A FAILURE IS STILL NOT AN ERROR. A checkout with no auto-resolve bundles
	# starts, plays and fights tactical battles exactly as before; only
	# AUTO-RESOLVE refuses, by name, on the strategic screen's own button. So the
	# return is deliberately not checked - `session.auto_resolve_reason` carries it
	# and the screen prints it.
	session.load_auto_resolve(_wotr_pack_roots())
	# THE SETUP SCREEN'S RULES AND START TERRITORIES TRAVEL WITH THE SEATING.
	# `rules` was already being emitted and dropped here, so the RULES tab's two
	# live rows never reached the strategic state; `start_regions` is what makes
	# retail's own freeform default startable at all. Both default to empty, which
	# is exactly what the two chooser-less callers want.
	if not session.begin_evidenced(_wotr_document, probe_world.campaign_name, scenario, seats,
			chosen.get("rules", {}) as Dictionary,
			chosen.get("start_regions", PackedStringArray()) as PackedStringArray,
			_wotr_input_identity(session)):
		_wotr_unavailable_reason = "the strategic layer refused this campaign: %s" % ", ".join(Array(session.refusals))
		return false
	session.document_path = _wotr_document_path
	session.document_source = _wotr_document_source
	if not seats_were_chosen:
		session = _seat_an_opponent_that_can_fight(
			session, seats, seatable, probe_world.campaign_name, scenario, chosen)
		if session == null:
			return false
	_wotr_session = session
	return true


## ------------------------------------------------------------------------------
## THE DEFAULT OPPONENT HAS TO BE ONE THAT CAN ACTUALLY FIGHT
## ------------------------------------------------------------------------------
##
## THE STORY THIS COMMENT USED TO TELL WAS WRONG, and correcting it matters more
## than the code below does. It said "no Dwarven roster is covered by the
## auto-resolve bindings bundle this checkout ships", i.e. that seat 1 was mute
## because of a CONTENT COVERAGE HOLE. Two separate causes were being blamed on a
## missing bundle, and neither of them was one:
##
##   1. THIS FILE'S OWN ORDERING. `load_auto_resolve()` was called down here,
##      AFTER `session.begin()` had already built `state.roster_units` from a null
##      bundle. Every army in the first session - Angmar's, the Dwarves', anyone's
##      - was therefore placed fielding NOTHING, and `_seat_cannot_fight()` said
##      so accurately about a hole this function had helped dig. `begin()` is now
##      preceded by the load in `_start_wotr_session()` above.
##   2. A RETAIL DATA TYPO. `livingworldbuildableunits.inc` declares
##      `DainPlayerArmy`'s own entry as `DainPlayerArmy` - a roster row pointing
##      at itself - which shadowed the `DwarvenDain` row that carries the actual
##      units. `wotr_world.gd` now refuses self-referential roster rows and
##      records each one in `world.data_defects`, so the Dwarves field a complete
##      roster like every other seat.
##
## With both fixed, all seven seatable templates field units and this function is
## a no-op. It is KEPT, not deleted, because the question it asks is still the
## right one and is not answerable in advance: a checkout with no auto-resolve
## bundles at all, or a future document with a genuinely uncovered roster, would
## seat a mute opponent and the player would only discover it by losing a session
## to it. What it must never again do is paper over a bug in this file.
##
## The rules it keeps:
##
##   * ONLY the AI seat moves. The human's seat is the first template as before -
##     changing what the player is handed to make the opponent work would be
##     solving the wrong problem.
##   * ONLY the chooser-less path. A seating the player picked on the GAME SETUP
##     screen is theirs, and it stands even if the opponent cannot fight; the
##     screen already names `auto_resolve_unbound_templates` in its diagnosis.
##   * The swap is PRINTED, with the roster that forced it, so a reader is never
##     left wondering why the campaign seated somebody the document did not list
##     second.
func _seat_an_opponent_that_can_fight(
		seated, seats: Array, seatable: Array[String],
		campaign: String, scenario: String, chosen: Dictionary):
	var roots := _wotr_pack_roots()
	# NO LOAD HERE ANY MORE. `_start_wotr_session()` loaded the bundles before it
	# called `begin()`, which is the only order in which the rosters survive; a
	# second load at this point would refresh the bundle handles and leave
	# `state.roster_units` exactly as `begin()` already built it.
	if not _seat_cannot_fight(seated, 1):
		return seated
	var rejected := String((seats[1] as Dictionary)["template"])
	for template in seatable:
		if template == String((seats[0] as Dictionary)["template"]) or template == rejected:
			continue
		var retry: Array = [
			(seats[0] as Dictionary).duplicate(),
			{"template": template, "team": 2, "controller": WotrStateScript.CONTROLLER_AI},
		]
		var candidate = WotrSessionScript.new()
		# SAME ORDER AS `_start_wotr_session()`, and for the same reason: a candidate
		# begun before its bundles load places every army with no units and then
		# fails the very test it is being begun for.
		candidate.load_auto_resolve(roots)
		if not candidate.begin_evidenced(_wotr_document, campaign, scenario, retry,
				chosen.get("rules", {}) as Dictionary,
				chosen.get("start_regions", PackedStringArray()) as PackedStringArray,
				_wotr_input_identity(candidate)):
			continue
		candidate.document_path = _wotr_document_path
		candidate.document_source = _wotr_document_source
		if _seat_cannot_fight(candidate, 1):
			continue
		print("[wotr] default opponent: %s was seated instead of %s - none of %s's armies has auto-resolve data in the bindings bundle on this machine, so every battle it committed would refuse by name. Unbound rosters: %s" % [
			template, rejected, rejected,
			", ".join(Array(seated.auto_resolve_unbound_templates))])
		return candidate
	# NOBODY CAN FIGHT. The original seating stands rather than being replaced by
	# an equally mute one, and the fact is printed; the strategic screen's
	# diagnosis carries the same list of unbound rosters.
	print("[wotr] default opponent: NO seatable template's armies have auto-resolve data on this machine, so the campaign is seated as the document orders it and no battle can be decided from the map. Unbound rosters: %s" % ", ".join(
		Array(seated.auto_resolve_unbound_templates)))
	return seated


## Whether a seat's armies are ALL rosters the auto-resolve bindings do not cover.
## Not "some are missing" - a seat with one fieldable army can still fight, and
## reseating over a partial hole would be this file second-guessing the document.
func _seat_cannot_fight(session, seat: int) -> bool:
	if session == null or session.state == null:
		return false
	if session.autoresolve == null or session.autoresolve_bindings == null:
		# No bundle at all is a different state entirely: NOTHING can auto-resolve,
		# reseating fixes nothing, and the screen already says so on the
		# AUTO-RESOLVE button. Leave the document's own order alone.
		return false
	var unbound := Array(session.auto_resolve_unbound_templates)
	var owned := 0
	var mute := 0
	for army_id in session.state.armies.keys():
		var army := session.state.armies[army_id] as Dictionary
		if int(army.get("owner", WotrStateScript.NEUTRAL)) != seat:
			continue
		owned += 1
		if unbound.has(String(army.get("roster", ""))):
			mute += 1
	return owned > 0 and mute == owned


## The mounted pack roots, sorted - the same list every War of the Ring loader is
## handed, so the document, the map, the auto-resolve tables and the AI template
## are all searched in one order.
func _wotr_pack_roots() -> Array:
	var roots: Array = []
	for meta_value in (_content_db.get("pack_meta") as Array):
		roots.append(String((meta_value as Dictionary).get("root", "")))
	roots.sort()
	return roots


## Required identity for begin_evidenced. This only reports facts already
## available at the production seam; it does not discover a second content
## configuration or infer private paths.
func _wotr_input_identity(session) -> Dictionary:
	var selection: Dictionary = {}
	var selection_path := String(ModLoader.active_selection_path).strip_edges()
	if not selection_path.is_empty():
		selection = {"kind": "selection.json", "path": selection_path}
	else:
		var roots := _wotr_pack_roots()
		if roots.size() == 1:
			# No selection document exists on this loader route. Name the one
			# explicitly mounted immutable bundle root rather than claiming a hash
			# for a selection.json that was never read.
			selection = {"kind": "immutableBundleRoot", "root": String(roots[0])}
	var configs := {
		"autoresolve": {"status": "absent", "reason": "no auto-resolve rules bundle was loaded"},
		"autoresolve_bindings": {"status": "absent", "reason": "no auto-resolve bindings bundle was loaded"},
		"ai_template": {"status": "absent", "reason": "AI template is loaded lazily after session admission"},
		"building_catalogue": {"status": "absent", "reason": "building catalogue is loaded inside low-level session admission"},
	}
	if session != null and session.autoresolve != null:
		var path := String(session.autoresolve.source_path)
		if not path.is_empty(): configs["autoresolve"] = {"status": "present", "path": path}
	if session != null and session.autoresolve_bindings != null:
		var path := String(session.autoresolve_bindings.source_path)
		if not path.is_empty(): configs["autoresolve_bindings"] = {"status": "present", "path": path}
	return {
		"document_path": _wotr_document_path,
		"document_source": _wotr_document_source,
		"active_content_source": String(ModLoader.active_content_source),
		"selection": selection,
		"pack_meta": (_content_db.get("pack_meta") as Array).duplicate(true),
		"config_bundles": configs,
	}


## Pack map ids the tactical layer can actually boot, in sorted order. The screen
## binds region maps to these; an empty list means no battle can be fought and
## the commitment refuses by name rather than inventing a battlefield.
func wotr_available_map_ids() -> Array:
	var ids: Array = []
	for choice in RETAIL_MAP_CHOICES:
		var map_id := String(choice["id"])
		if retail_map_availability(map_id) == "":
			ids.append(map_id)
	ids.sort()
	return ids


## Open retail's GAME SETUP screen. It gets the located document, a probe
## session (world only - it never builds strategic state), the mounted pack roots
## its string and geometry bundles are searched under, and the SAME unavailable
## reason the strategic page refuses on, so the two surfaces cannot disagree
## about whether War of the Ring is open.
func _open_wotr_setup() -> bool:
	if not _ensure_wotr_setup_screen():
		return false
	var pack_roots: Array = []
	for meta_value in (_content_db.get("pack_meta") as Array):
		pack_roots.append(String((meta_value as Dictionary).get("root", "")))
	pack_roots.sort()
	var probe = null
	if _wotr_unavailable_reason == "" and not _wotr_document.is_empty():
		var probe_world = _new_wotr_world()
		if probe_world != null and probe_world.load_from_dict(_wotr_document, ""):
			probe = WotrSessionScript.new()
			probe.world = probe_world
	if not _ensure_skirmish_options():
		return false
	wotr_setup_screen.pack_faction_availability = _skirmish_availability
	wotr_setup_screen.configure(
		_wotr_document, probe, pack_roots, _wotr_unavailable_reason, _cah_system_runtime())
	for line in wotr_setup_screen.describe_load():
		print("[wotr-setup] %s" % String(line))
	return true


## PLAY on the setup screen. It reaches EXACTLY the path the fixed seating
## reached - `_start_wotr_session()` into `WotrSession.begin()` - carrying the
## scenario and seats the player chose instead of the ones this file used to
## pick. Nothing else about the chosen setup travels: colour is presentation,
## and every locked row on the RULES tab is locked precisely because there is no
## carrier for it inside the commitment.
func _on_wotr_setup_play(setup: Dictionary) -> void:
	_wotr_session = null
	if not _start_wotr_session(setup):
		_refresh_wotr_entry()
		wotr_setup_screen.show_message(
			"the session refused this setup: %s" % _wotr_unavailable_reason)
		return
	if not _open_wotr():
		wotr_setup_screen.show_message(
			"the strategic screen refused to open: %s" % _wotr_unavailable_reason)
		return
	_show_page(PAGE_WOTR)


func _open_wotr() -> bool:
	if _wotr_unavailable_reason != "":
		return false
	# Compile-on-navigate, checked. A failure here sets the unavailable reason and
	# refreshes the entry, so the route closes with a sentence rather than opening
	# a page with no screen behind it.
	if not _ensure_wotr_screen():
		return false
	if _wotr_session == null and not _start_wotr_session():
		_refresh_wotr_entry()
		return false
	# The same mounted pack roots the living-world DOCUMENT is searched for, so a
	# pack that ships retail's converted 3D map is found the same way and in the
	# same order as the one that ships the region data.
	wotr_screen.configure(
		_wotr_session, wotr_available_map_ids(), _wotr_unavailable_reason, _wotr_pack_roots())
	return true


## A battle was admitted into the strategic state. Record the handoff, project
## the COMMITMENT onto the slice's roster contract, and launch.
##
## Everything the tactical match is configured from comes out of the commitment:
## the two factions, which side is machine-driven, and the battlefield. The
## fields the slice needs that a commitment does not describe are FIXED
## CONSTANTS here, not choices - the AI tier is the sim's own default and the
## start spots are the map's authored ones in ascending order - because a per-
## session choice would be a value reaching the simulation that the strategic
## hash never saw.
## The slice's N-team roster for a committed War of the Ring battle - a PURE
## PROJECTION of the commitment plus the battlefield's own authored start spots.
## Empty when the battlefield cannot seat two sides.
##
## Everything that decides the match comes out of `configured["team_roster"]`,
## which `wotr_session.tactical_roster()` re-derived from the record inside the
## strategic hash. The remaining descriptor fields are FIXED, not chosen: the AI
## tier is the simulation's own default and the colours are the two authored slice
## team colours. A chooser for either would put a per-session value in front of
## the simulation that no hash ever saw.
func wotr_team_descriptors(configured: Dictionary) -> Array:
	var roster: Array = configured.get("team_roster", []) as Array
	var commitment := configured.get("commitment", {}) as Dictionary
	var battlefield := String(commitment.get("battlefield_map", ""))
	if roster.size() != 2 or battlefield.is_empty():
		return []
	# The human start spot is reset FIRST: `_assign_start_indices` honours a
	# previously chosen one, and a leftover skirmish choice must not decide where
	# a War of the Ring army deploys.
	_game_state.set("retail_player_start_index", 0)
	var starts := _assign_start_indices(battlefield, 2)
	if starts.is_empty():
		return []
	var descriptors: Array = []
	for index in range(2):
		var seat := roster[index] as Dictionary
		descriptors.append({
			"team": int(seat["team"]),
			"faction": String(seat["faction"]),
			"controller": CONTROLLER_AI if bool(seat["is_ai"]) else CONTROLLER_HUMAN,
			"difficulty": RETAIL_AI_DEFAULT_DIFFICULTY,
			"alliance": index + 1,
			"color": HOUSE_COLORS[index]["color"],
			"start_index": starts[index],
			"heroes": [_wotr_selected_hero_document]
				if not bool(seat["is_ai"]) and not _wotr_selected_hero_document.is_empty() else [],
		})
	return descriptors


func _on_wotr_battle_committed(configured: Dictionary) -> void:
	if _launch_in_progress:
		return
	var commitment_value: Variant = configured.get("commitment", null)
	if typeof(commitment_value) != TYPE_DICTIONARY:
		wotr_screen.show_message(
			"The tactical launch was refused: the configured commitment is malformed.")
		return
	var commitment := commitment_value as Dictionary
	if String(commitment.get("battle_type", "")) != WotrStateScript.BATTLE_TYPE_RTS:
		wotr_screen.show_message(
			"The tactical launch was refused: the admitted commitment is not an RTS battle.")
		return
	var battlefield := String(commitment.get("battlefield_map", ""))
	if battlefield.is_empty():
		wotr_screen.show_message("The commitment names no battlefield; the battle cannot be launched.")
		return
	var roster: Array = configured["team_roster"] as Array
	if roster.size() != 2:
		wotr_screen.show_message("The commitment did not authorise two sides.")
		return
	# Mandatory and transactional: mint and fully verify the receipt-bound record
	# before the first tactical GameState write. A refusal cannot leave a partial
	# launch configuration behind.
	var transport: Dictionary = _wotr_session.battle_transport(configured)
	if transport.is_empty():
		wotr_screen.show_message(
			"The tactical transport was refused: %s" % ", ".join(Array(_wotr_session.refusals)))
		return
	var descriptors := wotr_team_descriptors(configured)
	if descriptors.is_empty():
		wotr_screen.show_message(
			"%s provides fewer than two authored player starts, so this battle cannot be seated." % battlefield)
		return
	_game_state.set("retail_mp_mode", "")
	_game_state.set("retail_player_faction", String((roster[0] as Dictionary)["faction"]))
	_game_state.set("retail_enemy_faction", String((roster[1] as Dictionary)["faction"]))
	_game_state.set("retail_map_id", battlefield)
	_game_state.set("retail_initial_resources", int((configured["gameplay_rules"] as Dictionary).get("starting_resources", -1)))
	_game_state.set("retail_command_point_factor", 1.0)
	_game_state.set("retail_build_plots_only", false)
	_game_state.set("retail_team_setup", descriptors)
	_game_state.set("wotr_handoff", _wotr_session.handoff_payload().duplicate(true))
	_game_state.set("wotr_battle_transport", transport.duplicate(true))
	_game_state.set("wotr_battle_report", {})
	_game_state.set("wotr_battle_winner", -1)
	_launch_in_progress = true
	get_tree().change_scene_to_file("res://scenes/retail_loading_boot.tscn")


## Come back from a tactical battle with its result. Three outcomes, all of them
## reported: the battle decided and the map moves; the battle was left undecided
## and NOTHING is applied (a player who quit did not lose the war); or the
## handoff itself no longer makes sense, which is a refusal rather than a silent
## fresh campaign.
func _resume_wotr_after_battle() -> bool:
	var payload: Variant = _game_state.get("wotr_handoff")
	if typeof(payload) != TYPE_DICTIONARY or (payload as Dictionary).is_empty():
		return false
	var winner := int(_game_state.get("wotr_battle_winner"))
	var transport := (_game_state.get("wotr_battle_transport") as Dictionary).duplicate(true)
	var report := (_game_state.get("wotr_battle_report") as Dictionary).duplicate(true)
	# Consume immediately, before receipt verification or report validation. A
	# refusal leaves the adopted strategic transaction open, never replayable from
	# stale GameState bytes on the next menu construction.
	_clear_wotr_battle_seam()
	var session = WotrSessionScript.new()
	if not session.adopt_evidenced_handoff(payload as Dictionary):
		_wotr_unavailable_reason = "the War of the Ring session could not be resumed: %s" % ", ".join(Array(session.refusals))
		_refresh_wotr_entry()
		return false
	_wotr_session = session
	var message := ""
	var outcome_ok := false
	if not report.is_empty():
		# A present report owns this branch completely. Invalid input must never fall
		# through to the legacy boolean resolver, because doing so would turn a named
		# transport/report refusal into a strategically accepted result.
		var outcome: Dictionary = session.resolve_fought_battle(transport, report, winner)
		outcome_ok = bool(outcome.get("ok", false))
		if outcome_ok:
			message = "%s: %s %s." % [
				String(outcome["region"]),
				_wotr_seat_name(session, int(outcome["winner_player"])),
				"took the region" if bool(outcome["captured"]) else "held the region",
			]
		else:
			message = "The tactical report was refused; the battle remains: %s" % ", ".join(
				Array(outcome.get("refusals", PackedStringArray())))
	elif winner == WotrBattleScript.UNDECIDED:
		# HEAD-compatible absent-report behaviour: leaving the old tactical slice
		# undecided abandons the transaction without inventing a winner.
		session.abandon_battle()
		message = "The battle was left undecided. Nothing was applied; the region did not change hands."
	else:
		# HEAD-compatible absent-report behaviour for the current slice, which has
		# no production report writer. The richer resolver is used only when a report
		# is actually present.
		var outcome: Dictionary = session.resolve_battle(winner)
		outcome_ok = bool(outcome.get("ok", false))
		if outcome_ok:
			message = "%s: %s %s." % [
				String(outcome["region"]),
				_wotr_seat_name(session, int(outcome["winner_player"])),
				"took the region" if bool(outcome["captured"]) else "held the region",
			]
		else:
			# Outcome settlement is atomic. A refusal means the snapshot and event
			# history were rolled back and the same battle remains open for action.
			message = "The result was refused and rolled back; the battle remains: %s" % ", ".join(
				Array(outcome.get("refusals", PackedStringArray())))
	if not _open_wotr():
		return false
	# The far side of the scene change may resume opponents only after a
	# successful result reached authoritative clean Tactical. Human Retreat and a
	# rolled-back/undecided Battle stay on screen for their pending action.
	var clean_tactical: bool = (
		outcome_ok
		and session.state.phase == WotrStateScript.PHASE_TACTICAL
		and session.state.pending_battle.is_empty()
		and session.state.pending_claim.is_empty()
		and session.state.pending_retreats.is_empty()
	)
	if clean_tactical:
		wotr_screen.run_opponent_turns()
	wotr_screen.show_message(message)
	_show_page(PAGE_WOTR)
	return true


func _wotr_seat_name(session, seat: int) -> String:
	if seat < 0 or seat >= session.state.players.size():
		return "seat %d" % seat
	return String((session.state.players[seat] as Dictionary).get("template", "seat %d" % seat))


func _connect_actions() -> void:
	tutorials_btn.tooltip_text = BAR_TOOLTIPS["tutorials"]
	solo_btn.tooltip_text = BAR_TOOLTIPS["solo"]
	multiplayer_btn.tooltip_text = BAR_TOOLTIPS["multiplayer"]
	options_btn.tooltip_text = BAR_TOOLTIPS["options"]
	quit_btn.tooltip_text = BAR_TOOLTIPS["quit"]
	# MY HEROES is live: it opens the Create-a-Hero front end. The screen is
	# always reachable even when no mounted pack carries the class table,
	# because a screen that names the missing content and the command that
	# produces it tells the player far more than a greyed-out button does.
	my_heroes_btn.disabled = false
	my_heroes_btn.tooltip_text = BAR_TOOLTIPS["my_heroes"]
	my_heroes_btn.pressed.connect(_on_my_heroes_pressed)
	tutorials_btn.pressed.connect(_toggle_shell_flyout.bind("tutorials"))
	solo_btn.pressed.connect(_toggle_shell_flyout.bind("solo"))
	options_btn.pressed.connect(_toggle_shell_flyout.bind("options"))
	# MULTIPLAYER opens the NETWORK panel directly: REPLAYS and ONLINE (REF-01)
	# have no converted implementation and NETWORK is the sole live route, so a
	# one-live-row flyout would only add a click.
	multiplayer_btn.pressed.connect(func() -> void:
		_close_shell_flyouts()
		_show_page(PAGE_MULTIPLAYER))
	wotr_btn.pressed.connect(_on_wotr_pressed)
	# The WOTR screens' and the lobby's own signals are connected where those
	# panels are BUILT (`_ensure_wotr_screen()`, `_ensure_wotr_setup_screen()`,
	# `ensure_multiplayer_lobby()`), because they do not exist yet at this point -
	# they are compiled at the moment the player navigates to them.
	quit_btn.pressed.connect(func() -> void: get_tree().quit())
	multiplayer_flyout.host_requested.connect(_on_multiplayer_host)
	multiplayer_flyout.join_requested.connect(_on_multiplayer_join)
	multiplayer_flyout.back_requested.connect(func() -> void: _show_page(PAGE_MAIN))
	solo_flyout.army_changed.connect(_on_army_changed)
	solo_flyout.hero_changed.connect(func(_row: int) -> void: _refresh_skirmish_launch_state())
	solo_flyout.custom_heroes_toggle.toggled.connect(func(_on: bool) -> void:
		_refresh_hero_rows()
		_refresh_skirmish_launch_state())
	solo_flyout.color_changed.connect(_on_color_changed)
	solo_flyout.rows_changed.connect(_on_rows_changed)
	solo_flyout.controller_changed.connect(_on_controller_changed)
	solo_flyout.team_changed.connect(func(_row: int) -> void: _refresh_skirmish_launch_state())
	solo_flyout.play_pressed.connect(_on_retail)
	solo_flyout.main_menu_pressed.connect(func() -> void: _show_page(PAGE_MAIN))
	solo_flyout.stats_pressed.connect(func() -> void: _show_page(PAGE_STATS))
	solo_flyout.rules_reset_btn.pressed.connect(_on_rules_reset)
	solo_flyout.initial_resources_opt.item_selected.connect(_on_rules_changed)
	solo_flyout.cp_factor_opt.item_selected.connect(_on_rules_changed)
	solo_flyout.build_mode_opt.item_selected.connect(_on_rules_changed)
	stats_screen.back_pressed.connect(func() -> void: _show_page(PAGE_SOLO))
	developer_access_btn.pressed.connect(func() -> void: _show_page(PAGE_DEVELOPER))
	developer_back_btn.pressed.connect(func() -> void: _show_page(PAGE_MAIN))


func show_page(page: String) -> bool:
	if page not in [PAGE_MAIN, PAGE_SOLO, PAGE_WOTR, PAGE_WOTR_SETUP, PAGE_OPTIONS,
			PAGE_DEVELOPER, PAGE_STATS]:
		return false
	if page == PAGE_SOLO and not _skirmish_options_ready:
		call_deferred("_open_skirmish_page_when_ready")
		return true
	# GAME SETUP OPENS EVEN WHEN THE CAMPAIGN CANNOT START, and draws the reason.
	# The strategic page below refuses instead, because a strategic page with no
	# map is a blank Middle-earth; a setup screen with no document is a setup
	# screen carrying the sentence that says which file is missing.
	if page == PAGE_WOTR_SETUP:
		# The ONE thing that can still refuse this route is the setup screen's own
		# script failing to compile. `_open_wotr_setup()` returns false only in
		# that case, and it has already recorded the named reason.
		if not _open_wotr_setup():
			return false
		_show_page(page)
		return true
	# WAR OF THE RING REFUSES RATHER THAN OPENING EMPTY. With no living-world
	# document there is no map to show, and showing a page anyway - blank, or
	# worse, populated with something invented - is the failure this refusal
	# exists to prevent. `wotr_unavailable_reason()` carries the why.
	if page == PAGE_WOTR and not _open_wotr():
		return false
	_show_page(page)
	return true


func _open_skirmish_page_when_ready() -> void:
	await _wait_for_skirmish_options()
	_show_page(PAGE_SOLO)


## The WAR OF THE RING entry now lands on retail's GAME SETUP screen rather than
## dropping into a campaign that seated itself. The screen opens even when the
## campaign cannot start, because it is the surface that can SAY why.
func _on_wotr_pressed() -> void:
	if not _skirmish_options_ready:
		await _wait_for_skirmish_options()
	if not show_page(PAGE_WOTR_SETUP):
		status.text = "War of the Ring is unavailable: %s" % _wotr_unavailable_reason


func get_current_page() -> String:
	return current_page


func _show_page(page: String) -> void:
	current_page = page
	# The main bar stays visible under the compact NETWORK flyout, matching the
	# retail shell where flyouts open above the persistent bottom bar (REF-01).
	_set_nodes_visible(_main_page_nodes(), page == PAGE_MAIN or page == PAGE_MULTIPLAYER or page == PAGE_MP_LOBBY)
	_set_nodes_visible(_solo_page_nodes(), page == PAGE_SOLO)
	_set_nodes_visible(_wotr_page_nodes(), page == PAGE_WOTR)
	_set_nodes_visible(_wotr_setup_page_nodes(), page == PAGE_WOTR_SETUP)
	_set_nodes_visible(_multiplayer_page_nodes(), page == PAGE_MULTIPLAYER)
	_set_nodes_visible(_mp_lobby_page_nodes(), page == PAGE_MP_LOBBY)
	_set_nodes_visible(_options_page_nodes(), page == PAGE_OPTIONS)
	_set_nodes_visible(_developer_page_nodes(), page == PAGE_DEVELOPER)
	_set_nodes_visible(_stats_page_nodes(), page == PAGE_STATS)
	_set_nodes_visible(_my_heroes_page_nodes(), page == PAGE_MY_HEROES)
	_apply_shell_chrome_for_page(page)
	# Upward flyouts belong to the bar; any page change dismisses them.
	_close_shell_flyouts()
	if _nav_diamonds != null:
		_nav_diamonds.queue_redraw()
	# Developer tools remain deliberately absent from the player-facing surface;
	# F10 and show_page("developer") preserve access for proof work.
	developer_access_btn.visible = false
	match page:
		PAGE_MAIN:
			if solo_btn.visible:
				solo_btn.grab_focus()
		PAGE_SOLO:
			_refresh_skirmish_launch_state()
			_refresh_map_preview()
			_refresh_map_description()
			_refresh_start_row()
			if solo_flyout.player_army_opt.visible:
				solo_flyout.player_army_opt.grab_focus()
		PAGE_WOTR:
			# Null only if the page was reached without `_open_wotr()`, which is the
			# only route that builds the screen.
			if wotr_screen != null and wotr_screen.back_button != null and wotr_screen.back_button.visible:
				wotr_screen.back_button.grab_focus()
		PAGE_MULTIPLAYER:
			if multiplayer_flyout.host_button != null and multiplayer_flyout.host_button.visible:
				multiplayer_flyout.host_button.grab_focus()
		PAGE_MP_LOBBY:
			if multiplayer_lobby != null and multiplayer_lobby.leave_button != null and multiplayer_lobby.leave_button.visible:
				multiplayer_lobby.leave_button.grab_focus()
		PAGE_OPTIONS:
			if options_screen.visible and options_screen.window_mode_opt != null:
				options_screen.window_mode_opt.grab_focus()
		PAGE_DEVELOPER:
			if developer_back_btn.visible:
				developer_back_btn.grab_focus()
		PAGE_STATS:
			if stats_screen.back_btn.visible:
				stats_screen.back_btn.grab_focus()
		PAGE_MY_HEROES:
			if (
				my_heroes_screen != null
				and my_heroes_screen.back_button != null
				and my_heroes_screen.back_button.visible
			):
				my_heroes_screen.back_button.grab_focus()


## THE PAGES THAT ARE THE GAME RATHER THAN A PAGE OF THE SHELL.
##
## Everything else in this file is a panel that opens ON the shell: the backdrop,
## the OPEN BFME masthead, the version corner and the bottom bar are the frame the
## panel is seen inside, and hiding them would be wrong. The War of the Ring
## strategic screen is not a panel. It is a full-bleed 3D Middle-earth with HUD
## islands floating over it, it owns ESCAPE (its own pause shell), and it is where
## a player spends the session - so the shell's furniture around it is not a frame,
## it is clutter over the game. The owner's words were "there is no ... good way to
## get rid of the ui so it can get out of my way and just play the game".
##
## Listed as a constant so the runners can assert the set rather than re-deriving
## it, and so adding a page that takes the whole window is one line here.
##
## MY HEROES joins it for the same reason: retail's Create-a-Hero is a screen of
## its own with its own header and its own backdrop, and the shell's masthead
## over the top of it made it read as a dialog the menu had opened. It is handed
## the shell's own backdrop texture (`_layout_my_heroes_screen`) so taking the
## shell's `BackdropArt` down does not cost the art.
const FULL_WINDOW_PAGES := [PAGE_WOTR, PAGE_MY_HEROES]


## The shell's own furniture, which is hidden while a full-window page is up and
## restored the moment one is left. Deliberately NOT `Backdrop`: that node is a
## flat near-black ColorRect, it is the ground every surface in this game is drawn
## on, and leaving it up means a full-window page that has not painted a pixel yet
## shows black rather than whatever the compositor last had.
func _shell_chrome_nodes() -> Array[Control]:
	var nodes: Array[Control] = []
	for node_name in ["Atmosphere", "BackdropArt", "BarScrim", "Footer"]:
		if has_node(node_name):
			nodes.append(get_node(node_name) as Control)
	for node_name in ["Title", "TitleVersion", "Subtitle"]:
		if center != null and center.has_node(node_name):
			nodes.append(center.get_node(node_name) as Control)
	if _nav_diamonds != null:
		nodes.append(_nav_diamonds)
	if _shell_apt_runtime != null:
		nodes.append(_shell_apt_runtime)
	return nodes


func _apply_shell_chrome_for_page(page: String) -> void:
	var wanted := page not in FULL_WINDOW_PAGES
	for node in _shell_chrome_nodes():
		node.visible = wanted


## Public so the runners can ask the shell what it believes without reaching into
## node paths: true when the shell's own chrome is down because a page has taken
## the whole window.
func shell_chrome_is_hidden() -> bool:
	for node in _shell_chrome_nodes():
		if node.visible:
			return false
	return true


func _main_page_nodes() -> Array[Control]:
	## The six retail bar caps. `wotr_btn` is deliberately absent: it is the WAR
	## OF THE RING entry's state carrier, reached through the SOLO PLAY flyout,
	## and must never appear as a seventh cap.
	var nodes: Array[Control] = []
	for button in _bar_buttons():
		nodes.append(button)
	return nodes


func _solo_page_nodes() -> Array[Control]:
	return [solo_flyout]


func _wotr_page_nodes() -> Array[Control]:
	return [wotr_screen]


func _wotr_setup_page_nodes() -> Array[Control]:
	return [wotr_setup_screen]


func _multiplayer_page_nodes() -> Array[Control]:
	return [multiplayer_flyout]


func _mp_lobby_page_nodes() -> Array[Control]:
	return [multiplayer_lobby]


func _options_page_nodes() -> Array[Control]:
	return [options_screen]


func _stats_page_nodes() -> Array[Control]:
	return [stats_screen]


func _my_heroes_page_nodes() -> Array[Control]:
	var nodes: Array[Control] = []
	if my_heroes_screen != null:
		nodes.append(my_heroes_screen)
	return nodes


func _developer_page_nodes() -> Array[Control]:
	var nodes: Array[Control] = [developer_frame, developer_heading]
	nodes.append(developer_back_btn)
	nodes.append(status)
	return nodes


func _set_nodes_visible(nodes: Array[Control], visible_value: bool) -> void:
	for node in nodes:
		if node == null:
			continue
		node.visible = visible_value
		# Hidden main-bar controls must not keep intercepting clicks under the
		# solo flyout (that felt like the menu "spazzing" when Solo was open).
		# Do not touch BaseButton.disabled — launch/faction gates own that bit.
		# Containers use PASS so children (OptionButtons in grids) still receive
		# input when the page is shown; IGNORE only when the page is hidden.
		if visible_value:
			if node is BaseButton or node is Range:
				node.mouse_filter = Control.MOUSE_FILTER_STOP
			elif node is Container or node is Panel or node is PanelContainer:
				node.mouse_filter = Control.MOUSE_FILTER_PASS
			else:
				# Labels / pure chrome: do not steal clicks from buttons below.
				node.mouse_filter = Control.MOUSE_FILTER_IGNORE
		else:
			node.mouse_filter = Control.MOUSE_FILTER_IGNORE


func _on_options() -> void:
	_open_options(PAGE_MAIN)


## Open the shell's single OPTIONS screen and remember where to go back to.
## `return_page` is the page the player was on: the bottom bar's OPTIONS cap
## passes PAGE_MAIN, the War of the Ring pause shell passes PAGE_WOTR, and the
## strategic session is still seated behind it either way.
func _open_options(return_page: String) -> void:
	_options_return_page = return_page
	options_screen.open()
	_show_page(PAGE_OPTIONS)


## Validates and records a NETWORK selection on GameState without any scene
## change (the runner exercises this seam directly). "" mode is rejected.
func apply_multiplayer_selection(mode: String, address: String, port: int) -> bool:
	if mode != "host" and mode != "join":
		return false
	if port < multiplayer_flyout.PORT_MIN or port > multiplayer_flyout.PORT_MAX:
		return false
	if mode == "join" and not address.strip_edges().is_valid_ip_address():
		return false
	var host_error := retail_launch_error()
	if host_error != "":
		multiplayer_flyout.set_status("Cannot start: %s" % host_error, true)
		return false
	# The tier-1 network scenario is the proven lockstep path: authored slice
	# defaults (Men vs Men on Fords), no rules overrides, host=team 0.
	_game_state.set("retail_player_faction", "men")
	_game_state.set("retail_enemy_faction", "men")
	_game_state.set("retail_map_id", SliceIds.MAP_ID)
	_game_state.set("retail_initial_resources", -1)
	_game_state.set("retail_command_point_factor", 1.0)
	_game_state.set("retail_build_plots_only", false)
	_game_state.set("retail_player_start_index", 0)
	_game_state.set("retail_mp_mode", mode)
	_game_state.set("retail_mp_address", address.strip_edges() if mode == "join" else "127.0.0.1")
	_game_state.set("retail_mp_port", port)
	return true


func _on_multiplayer_host(port: int) -> void:
	_launch_multiplayer("host", "127.0.0.1", port)


func _on_multiplayer_join(address: String, port: int) -> void:
	_launch_multiplayer("join", address, port)


func _launch_multiplayer(mode: String, address: String, port: int) -> void:
	if _launch_in_progress:
		return
	# The selection seam stays: it validates fail-closed and records the
	# transport fields (mode/address/port) the slice will read. Its tier-1
	# men/men faction writes are provisional — the GAME LOBBY overwrites the
	# whole selection (retail_team_setup and friends) at launch.
	if not apply_multiplayer_selection(mode, address, port):
		return
	# COMPILE-ON-NAVIGATE, BOTH CHECKED. The lobby panel and the lockstep session
	# are the two scripts a multiplayer game needs and a menu does not. A failure
	# to compile either is reported into the NETWORK panel's status line, which is
	# the same surface a refused host/join already reports through - never a HOST
	# button that appears to work and then does nothing.
	if not ensure_multiplayer_lobby():
		multiplayer_flyout.set_status(
			"Cannot start: %s" % lazy_script_failure(LAZY_MULTIPLAYER_LOBBY), true)
		return
	var session_script = _lazy_script(LAZY_LOCKSTEP_SESSION)
	if session_script == null:
		multiplayer_flyout.set_status(
			"Cannot start: %s" % lazy_script_failure(LAZY_LOCKSTEP_SESSION), true)
		return
	var session = session_script.new()
	var session_error: Error = session.host(port) if mode == "host" else session.join(address, port)
	if session_error != OK:
		multiplayer_flyout.set_status(
			"Could not %s on %s:%d (error %d)." % ["host" if mode == "host" else "join", address, port, session_error],
			true
		)
		return
	_lobby_session = session
	if mode == "host":
		# Only a bound host advertises: the beacon promises a joinable game, so
		# it starts after host() succeeded and stops the moment the lobby ends.
		multiplayer_flyout.start_advertising(
			port,
			String(_game_state.get("retail_mp_player_name")),
			multiplayer_lobby.MAP_NAMES[0],
			session
		)
	multiplayer_flyout.set_busy(true)
	multiplayer_lobby.open(session, mode == "host",
		String(_game_state.get("retail_mp_player_name")), _cah_system_runtime())
	_show_page(PAGE_MP_LOBBY)


func _on_lobby_launch_confirmed() -> void:
	# Both peers verified the byte-identical roster and wrote GameState (incl.
	# retail_team_setup) inside the lobby; the menu only owns the scene change.
	if _launch_in_progress:
		return
	_clear_wotr_battle_seam()
	_launch_in_progress = true
	multiplayer_flyout.stop_advertising()
	multiplayer_lobby.close_lobby()
	if _lobby_session != null:
		# Graceful, frame-async drain: a hard close here provably drops the
		# host's still-un-acked lobby.launch echo and strands the guest. The
		# bounded drain never hangs the launch; UDP has no TIME_WAIT, so once
		# closed the slice's lockstep session re-binds the same port at boot.
		var session = _lobby_session
		session.begin_graceful_close()
		for _frame in range(30):
			if session.poll_graceful_close():
				break
			await get_tree().process_frame
		session.close()
	_lobby_session = null
	get_tree().change_scene_to_file("res://scenes/retail_loading_boot.tscn")


func _on_lobby_leave() -> void:
	# The lobby already closed the session (notified disconnect) before
	# emitting leave_requested.
	_lobby_session = null
	multiplayer_flyout.stop_advertising()
	multiplayer_flyout.set_busy(false)
	_show_page(PAGE_MULTIPLAYER)


func _on_retail() -> void:
	# Rapid re-presses must not double-fire: the first change frees this menu
	# and a second then errors on the freed tree.
	if _launch_in_progress:
		return
	# VALIDATE-ON-PICK'S VISIBLE HALF. Pressing PLAY is where the selected map's
	# terrain load and the faction manifest checks actually happen — a few seconds
	# once, instead of ~90 s for a catalog nobody asked about. When every needed
	# verdict is already memoized (the usual case, because the background warmer
	# has been running since the menu drew) this costs nothing and the press stays
	# synchronous; only a genuinely cold pick yields a frame to paint the notice.
	if not _skirmish_selection_is_fully_validated():
		_launch_in_progress = true
		solo_flyout.play_btn.disabled = true
		solo_flyout.hint_label.text = "CHECKING YOUR MAP AND ARMIES…"
		await get_tree().process_frame
		if not is_inside_tree():
			return
		_launch_in_progress = false
	if apply_skirmish_selection():
		_launch_in_progress = true
		# Route through the loading-boot scene: the retail loading screen shows
		# immediately, fetches the slice scene on a thread, and the slice then
		# adopts the same screen for its real per-phase progress.
		get_tree().change_scene_to_file("res://scenes/retail_loading_boot.tscn")


func _unhandled_input(event: InputEvent) -> void:
	## Clicking the backdrop dismisses an open bar flyout, matching retail where
	## the list closes as soon as the pointer commits anywhere else. Presses that
	## land on a flyout row or a bar button are consumed by those buttons and
	## never reach here.
	if not _shell_flyout_is_open():
		return
	var mouse := event as InputEventMouseButton
	if mouse == null or not mouse.pressed:
		return
	for button in _bar_buttons():
		if button.visible and button.get_global_rect().has_point(mouse.global_position):
			return
	_close_shell_flyouts()
	get_viewport().set_input_as_handled()


func _unhandled_key_input(event: InputEvent) -> void:
	if not event.pressed or event.echo:
		return
	if event.keycode == KEY_ESCAPE and _shell_flyout_is_open():
		# An open bar flyout is the innermost surface; ESC dismisses it first.
		_close_shell_flyouts()
		get_viewport().set_input_as_handled()
	elif event.keycode == KEY_ESCAPE and current_page == PAGE_MP_LOBBY and multiplayer_lobby != null:
		# Escaping the lobby is a LEAVE, never a silent page swap: the session
		# must close (notified disconnect) or the peer would wait forever.
		multiplayer_lobby._on_leave_pressed()
		get_viewport().set_input_as_handled()
	elif event.keycode == KEY_ESCAPE and current_page != PAGE_MAIN:
		_show_page(PAGE_MAIN)
		get_viewport().set_input_as_handled()
	elif event.keycode == KEY_F10:
		_show_page(PAGE_MAIN if current_page == PAGE_DEVELOPER else PAGE_DEVELOPER)
		get_viewport().set_input_as_handled()
	elif event.keycode == KEY_F11:
		# F11 IS FULLSCREEN, EVERYWHERE IN THE SHELL. It is bound here rather than on
		# any one page because the owner's complaint was that there is no way to put
		# the GAME fullscreen at all - so the binding has to work on the menu, on the
		# setup screen and on the strategic screen alike, and this node is the only
		# one all three are inside. `wotr_screen` consumes F1 and ESCAPE and nothing
		# else, so the key reaches here from every page.
		toggle_fullscreen()
		get_viewport().set_input_as_handled()


## THE FULLSCREEN TOGGLE, AND IT WRITES THROUGH THE SETTINGS STORE.
##
## Two halves, and skipping either one is how a fullscreen toggle becomes a thing
## the player has to redo every launch:
##
##   1. It APPLIES the mode through `OpenBFMEOptionsScreen.apply_display_settings`,
##      which is the one applier the options screen, the slice boot and
##      `startup_boot.gd` all already go through. There is no second code path
##      that knows how to size a window.
##   2. It PERSISTS the mode through `OpenBFMEUserSettings.save_display`, which is
##      the file `startup_boot.gd:_apply_stored_display_settings()` reads before it
##      draws a single frame. So the state survives a restart, which is what
##      "a display setting that works and persists" means.
##
## WHICH FULLSCREEN. Toggling into `borderless` rather than `fullscreen_exclusive`
## is deliberate: borderless keeps the desktop's own resolution (so the strategic
## map is composed for the monitor the player actually has), it alt-tabs without a
## mode change, and it is what F11 means in every application that binds F11. The
## EXCLUSIVE mode stays reachable, and stays a deliberate choice, on the options
## screen - the toggle returns to `windowed` from either fullscreen mode, so a
## player who chose exclusive there can still get their desktop back with one key.
##
## Returns the mode it left the window in, so a runner can assert the toggle rather
## than photographing a window.
const FULLSCREEN_TOGGLE_MODE := "borderless"


func toggle_fullscreen() -> String:
	var display: Dictionary = OpenBFMEUserSettings.load_display()
	var current := String(display.get("window_mode", "windowed"))
	var resolution := String(display.get("resolution", "1920x1080"))
	var wanted := "windowed" if current != "windowed" else FULLSCREEN_TOGGLE_MODE
	var saved: Error = OpenBFMEUserSettings.save_display(wanted, resolution)
	if saved != OK:
		# FAIL LOUDLY AND DO NOTHING. Applying a mode this run that the next run
		# will not come back to is worse than refusing: the player would learn a key
		# that works once. The reason is named, not swallowed.
		push_warning("[MainMenu] F11 could not persist the window mode (%s); the window is left as it was." % error_string(saved))
		return current
	OpenBFMEOptionsScreen.apply_display_settings(wanted, resolution)
	if options_screen != null and options_screen.has_method("reload_from_store"):
		options_screen.reload_from_store()
	print("[MainMenu] F11: window mode %s -> %s (persisted; startup_boot applies it next launch)" % [
		current, wanted])
	return wanted

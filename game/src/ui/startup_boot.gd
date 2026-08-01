extends Node
## THE GAME'S MAIN SCENE. Deliberately the smallest possible one.
##
## WHY THIS EXISTS - a measured engine-ordering fact, not a style preference.
## Godot loads the project's main scene RESOURCE before it adds a single
## autoload to the tree. `scenes/boot.tscn` carries `main_menu.gd`, whose
## preload chain reaches the whole tactical slice and the whole War of the Ring
## layer - about 30k lines of GDScript. Compiling that chain cost a MEASURED
## 2,816 ms, and every one of those milliseconds was spent before ContentDB had
## even started loading, before any script of ours could draw anything, and with
## the window showing nothing at all.
##
## Making the main scene this file instead reorders that compile behind a real
## loading surface: the window, the stored display mode and the ornate loading
## screen are all up FIRST, and only then is `scenes/boot.tscn` loaded. Nothing
## about the shell changed - it is the same scene, instantiated the same way,
## just no longer the thing the engine must finish parsing before it will show
## the player a single pixel. Measured effect: first drawn frame moved from
## ~12,470 ms to ~3,200 ms.
##
## The shell load is still SYNCHRONOUS, and `_load_shell()` documents exactly why
## it cannot be threaded today and what has to change for it to become threaded.
##
## HARD RULE FOR THIS FILE: it must not `preload` anything but the profiler.
## Every preload added here goes straight back onto the pre-first-frame path and
## undoes the fix. Runtime `load()` inside `_ready()` is fine; that runs after
## the window is up.

const BootProfile = preload("res://src/core/boot_profile.gd")

const SHELL_SCENE_PATH := "res://scenes/boot.tscn"
const SCREEN_SCENE_PATH := "res://scenes/retail_loading_screen.tscn"
const USER_SETTINGS_SCRIPT_PATH := "res://src/ui/user_settings.gd"
const OPTIONS_SCREEN_SCRIPT_PATH := "res://src/ui/options_screen.gd"

## Opening ratio, so the bar is not sitting at a dead zero on the first frame.
## The values above it are the honest phase boundaries of a synchronous load,
## not an animation: 0.5 when the shell compile starts, 1.0 when it returns.
const PROGRESS_FLOOR := 0.05

var _screen: CanvasLayer = null
var _requested := false
var _first_frame_marked := false
var _handed_off := false


func _ready() -> void:
	BootProfile.mark("startup_scene_ready")
	# Settle the window BEFORE anything slow runs. This used to be a
	# `call_deferred` out of the menu's `_ready`, which meant the window was
	# created maximized by project.godot and then snapped to the stored
	# mode/resolution twelve seconds later - the visible late resize the owner
	# reported. Applying it here costs ~20 ms and happens while the player is
	# still looking at the engine boot splash, so the window is in its final
	# state before the first frame of ours is ever drawn.
	_apply_stored_display_settings()
	BootProfile.mark("startup:display_settings")

	var packed: Resource = load(SCREEN_SCENE_PATH)
	if packed != null:
		_screen = (packed as PackedScene).instantiate()
		add_child(_screen)
		# The shared retail loading surface in its application-startup shape.
		_screen.call("configure_boot", "OPEN BFME")
		_screen.call("set_load_progress", PROGRESS_FLOOR, "Loading game shell")
	BootProfile.mark("startup:loading_surface_up")

	_report_startup_art_gaps()

	# The shell fetch deliberately does NOT start here. It starts from _process
	# once the loading surface has actually been presented (see _process), so
	# that whichever path the fetch ends up taking, the player is already looking
	# at something.


## NAMED GAPS - retail startup art that EXISTS in the extracted retail layers but
## that no converted content pack carries, so the runtime cannot reach it.
##
## Same fail-closed convention as `wotr_handoff.gd:UNSUPPORTED_BY_TACTICAL_SIM`:
## the hole is named, the reason is stated, and NOTHING IS SUBSTITUTED. The
## ornate frame this screen draws is the repository's own chrome, and the engine
## boot splash is Open BFME's own ring mark; neither is presented as retail art.
##
## Each entry names the retail source file that would satisfy it. All of them sit
## under `.private/retail-work/`, where the runtime cannot and must not read from
## - retail payloads never leave `.private`, and retail art is never committed.
## Closing these is an IMPORTER task (owner: the importer stream): convert the
## plate into the active pack's `assets/ui/...` and register it in that pack's
## `data/ui_manifest.json` under one of the ids `main_menu.gd` already looks for
## (`shellmapbackdrop` / `mainmenubackdrop` / `shellbackdrop` / `mainmenu`).
## Once a pack publishes one, the shell picks it up with no code change here.
const UNCONVERTED_RETAIL_STARTUP_ART := {
	"startup_splash_plate":
		"retail art/compiledtextures/ti/titlescreenuserinterface.jpg (1024x1024, "
		+ "BFME2 and RotWK both ship one) is the retail title/startup plate. No "
		+ "converted pack registers it, so the engine boot splash shows Open "
		+ "BFME's own icon_ring.png instead.",
	"install_load_plate":
		"retail art/compiledtextures/in/installload.jpg (1024x1024) is retail's "
		+ "own load plate. Not present in any converted pack.",
	"shell_backdrop":
		"retail art/Textures/apt_MainMenu_1.tga (1024x1024) is the retail shell "
		+ "atlas. The active pack's ui_manifest.json carries only HUD atlas crops "
		+ "(largest entry 192x192), so main_menu.gd's backdrop lookup resolves "
		+ "nothing and the procedural Atmosphere placeholder stands in.",
	"map_loadscreens":
		"77 retail per-map loadscreens exist (art/compiledtextures/{wo,go,ev,in,an,lo}/"
		+ "*loadscreen*.dds|.tga, 1024x512). None are converted, so the match "
		+ "loading screen shows no map plate.",
}


func _report_startup_art_gaps() -> void:
	## Printed, not silently held: an absent retail plate must be visible as a
	## named gap in the log rather than looking like a design choice.
	if not BootProfile.enabled():
		return
	for gap_id in UNCONVERTED_RETAIL_STARTUP_ART:
		print("[StartupBoot] NAMED GAP %s: %s" % [gap_id, UNCONVERTED_RETAIL_STARTUP_ART[gap_id]])


func _process(_delta: float) -> void:
	if not _first_frame_marked:
		_first_frame_marked = true
		# The frame the player actually sees something on. This is the number
		# the owner's "several seconds of nothing" complaint was about. _process
		# runs before drawing, so the surface is presented at the END of this
		# frame - which is why the fetch below waits for the NEXT tick.
		BootProfile.mark("first_frame")
		return
	if _handed_off or _requested:
		return
	_requested = true
	_load_shell()


func _load_shell() -> void:
	## SYNCHRONOUS, and now by CHOICE rather than by constraint. Both halves of
	## that sentence matter, so both are recorded.
	##
	## THE OLD CONSTRAINT, AND THAT IT IS GONE. A GDScript that `preload`s a
	## `.gdshader` cannot be compiled on the background loader thread: the preload
	## resolves to "Could not preload resource file" and every dependent script
	## fails with it. `src/wotr/wotr_map_view.gd` preloaded four shaders,
	## `wotr_screen.gd` depended on it and `main_menu.gd` preloaded that, so
	## `boot.tscn` was not threaded-loadable at all. Two workarounds were tried and
	## BOTH FAILED: pre-warming the shaders into the resource cache (the
	## restriction is in preload resolution, not the cache), and attempting the
	## threaded load with a synchronous fallback (WORSE than useless - the failed
	## compile is cached, so the fallback returns null too and the shell never
	## appears). That handoff has since landed: those shaders are runtime `load()`,
	## and `main_menu.gd` no longer preloads `wotr_screen.gd` at all.
	##
	## WHY IT IS STILL SYNCHRONOUS. The thing threading was meant to hide has
	## mostly stopped existing. `main_menu.gd` now preloads only what the menu
	## draws - 18 files / ~10,900 lines instead of 91 / ~90,100 - and this load is
	## a MEASURED 0.53-0.69 s, down from 4.6-4.8 s. Threading half a second of
	## compile buys a bar that moves instead of a bar that waits, at the cost of a
	## code path whose failure mode (above) is the shell never appearing. That is a
	## bad trade at this size. If this stage ever grows back past a second or so,
	## the threaded request is now genuinely available - see
	## `main_menu.gd:_warm_lazy_script()`, which already drives the loader thread
	## for the tactical slice.
	##
	## Meanwhile the compile blocks the main thread. That is a STATIONARY loading
	## screen, not a black window: the surface is already drawn and the phase text
	## says what is happening.
	_screen_progress(0.5, "Loading game shell")
	var packed: Resource = load(SHELL_SCENE_PATH)
	if packed == null:
		# FAIL CLOSED AND VISIBLY. There is no menu to fall back to; saying so on
		# the loading screen beats leaving a bar frozen forever.
		_screen_progress(1.0, "The game shell failed to load. See the log.")
		push_error("[StartupBoot] %s failed to load; the shell cannot be shown." % SHELL_SCENE_PATH)
		return
	_screen_progress(1.0, "Building menu")
	_apply_loaded_scene(packed)


func _screen_progress(ratio: float, phase: String) -> void:
	if _screen != null:
		_screen.call("set_load_progress", ratio, phase)


func _apply_stored_display_settings() -> void:
	## Static seam shared with the options screen and the slice boot, reached by
	## runtime load() so neither script joins this scene's compile cost.
	var settings_script: Resource = load(USER_SETTINGS_SCRIPT_PATH)
	var options_script: Resource = load(OPTIONS_SCREEN_SCRIPT_PATH)
	if settings_script == null or options_script == null:
		push_warning("[StartupBoot] display settings scripts are unavailable; the window keeps its project defaults.")
		return
	var display: Dictionary = settings_script.call("load_display")
	options_script.call(
		"apply_display_settings",
		String(display.get("window_mode", "windowed")),
		String(display.get("resolution", "1920x1080"))
	)


func _apply_loaded_scene(packed: Resource) -> void:
	if packed == null or _handed_off:
		return
	_handed_off = true
	var shell := (packed as PackedScene).instantiate()
	BootProfile.mark("shell_instantiated")
	# The shell must live at the scene root: `current_scene` rejects a node
	# parented to this boot node, and freeing this node must not take the
	# loading screen down while it is still fading over the shell.
	var tree := get_tree()
	tree.root.add_child(shell)
	tree.current_scene = shell
	if _screen != null:
		# reparent(), not add_child(): the screen already has this node as its
		# parent, and add_child would refuse - leaving the screen to be destroyed
		# by the queue_free() below mid-fade.
		_screen.reparent(tree.root)
		# Fade only once the shell has had a frame to lay itself out, so the
		# player never sees a blank frame between the two surfaces.
		_fade_screen_after_shell_frame()
	queue_free()


func _fade_screen_after_shell_frame() -> void:
	var screen := _screen
	_screen = null
	var tree := get_tree()
	if tree == null or screen == null:
		return
	# Two frames: one for the shell's _ready work to land, one for it to draw.
	tree.process_frame.connect(
		func() -> void:
			if not is_instance_valid(screen):
				return
			var inner_tree := screen.get_tree()
			if inner_tree == null:
				return
			inner_tree.process_frame.connect(
				func() -> void:
					if is_instance_valid(screen):
						BootProfile.mark("shell_visible")
						screen.call("fade_out_and_free"),
				CONNECT_ONE_SHOT
			),
		CONNECT_ONE_SHOT
	)

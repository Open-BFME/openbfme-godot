extends SceneTree
## STARTUP REGRESSION GUARD.
##
## The game used to spend ~12.5 s between launch and its first drawn frame, with
## a black window for all of it. The cause was structural, not incidental:
##  * Godot loads the main scene RESOURCE before it adds any autoload to the
##    tree, so pointing `run/main_scene` at `scenes/boot.tscn` put that scene's
##    entire ~30k-line preload chain (2.8 s of GDScript compilation) ahead of
##    every other startup stage AND ahead of anything being drawn;
##  * the menu ran a 4.3 s faction/map availability sweep in `_ready`, to fill a
##    flyout that is hidden on the front page;
##  * ContentDB re-walked and re-deep-copied more than it needed to.
##
## Each of those is easy to reintroduce by accident - repointing `main_scene`
## "for convenience", adding a preload to `startup_boot.gd`, or moving a call
## back into `main_menu._ready()` all silently restore a multi-second black
## window that no other test in this repository would notice.
##
## HOW THIS MEASURES. Boot timing cannot be observed from inside a `--script`
## run, because that run has no main scene and no boot ordering to speak of. So
## this launches the real game as a child process with `-- --profile-boot` and
## asserts against the BOOT_PROFILE timeline it prints.
##
## ORDERING CHECKS ARE THE POINT; the millisecond budgets are a coarse net.
## Budgets are set well above measured values so ordinary machine variance never
## fails the suite - they exist to catch a stage regressing by a multiple, not by
## a jitter. The ordering assertions below have no tolerance at all and are what
## actually pin the architecture.
##
## THE THIRD REGRESSION THIS NOW GUARDS: compiling the whole game to draw a menu.
## `main_menu.gd` used to `preload` the tactical vertical slice, the War of the
## Ring strategic screen, retail's GAME SETUP screen and the multiplayer lockstep
## session. `preload` resolves at COMPILE time, so instantiating the shell scene
## compiled 91 files / ~90,100 lines of GDScript - a measured 4.6-4.8 s - before a
## single button could be drawn, for surfaces the player had not navigated to.
## Those are now runtime `load()` at the point of navigation. Re-adding any of
## them as a `preload` puts that time straight back, so the checks below pin BOTH
## halves: the heavy scripts must not be compiled by the time the menu's _ready
## returns, and a lazy load that FAILS must produce a named refusal rather than a
## live control that opens nothing.
##
## LIVENESS: 44 checks. Raise this when you add checks; never lower it.

const EXPECTED_CHECKS := 44

## Heavy navigation scripts that must NOT be compiled to show a menu, with the id
## `main_menu.gd` registers each under.
const LAZY_IDS: Array[String] = [
	"slice", "faction_manifest", "wotr_screen", "wotr_setup_screen",
	"multiplayer_lobby", "lockstep_session",
]
const LAZY_PATHS: Array[String] = [
	"res://src/retail_slice/retail_vertical_slice.gd",
	"res://src/ui/wotr_screen.gd",
	"res://src/ui/wotr_setup_screen.gd",
	"res://src/ui/multiplayer_lobby.gd",
]
const LAZY_PATH_IDS: Array[String] = [
	"slice", "wotr_screen", "wotr_setup_screen", "multiplayer_lobby",
]

## Wall-clock ceiling for the child boot. Generous: a cold filesystem on a slow
## machine is not a regression.
const CHILD_TIMEOUT_FRAMES := 900

# --- budgets (milliseconds) -------------------------------------------------
# Measured on the development machine (shared with other work, so these are the
# slow end of the range), warm cache, headless, after the lazy-compile work:
# first frame ~4.8-5.1 s, ContentDB ~3.2-3.4 s, shell compile 0.53-0.66 s, menu
# visible 6.0-6.4 s. Budgets are roughly 2x those.
const BUDGET_FIRST_FRAME_AT_MS := 7000
const BUDGET_CONTENTDB_RELOAD_MS := 5000
## WAS 6500, against a measured 4.6-4.8 s of preload compilation. The shell scene
## no longer compiles anything the menu does not draw, so this is now a real
## ceiling rather than a formality: re-adding a heavy `preload` to main_menu.gd
## blows straight through it. NEVER raise this to make a preload fit.
const BUDGET_SHELL_COMPILE_MS := 1800
## The availability sweep is stepped one unit per idle frame with the menu already
## up, so the gap between its surrounding marks is mostly idle time and means
## nothing. These two budget what does mean something - the total work, and the
## single longest step, which is the longest frame the player could feel.
## Measured after the slice's compile moved to the background loader thread:
## total main-thread work 2.3-2.7 s across ~15 frames, worst single step
## 499-562 ms (the Men faction's roster classification). The worst-step budget is
## the one that matters - it is the longest frame a player can feel, and it is
## what regresses if the loader-thread warm is removed or a step is merged back
## into a bigger one.
const BUDGET_SKIRMISH_SWEEP_TOTAL_MS := 6000
const BUDGET_SKIRMISH_SWEEP_WORST_STEP_MS := 1800
const BUDGET_SHELL_VISIBLE_AT_MS := 12000

var _checks := 0
var _failures: Array[String] = []


func _init() -> void:
	# The surface-content section below awaits frames, so this whole entry point
	# is a coroutine; a SceneTree script's _init may await once the tree exists.
	await process_frame
	var marks := _run_child_boot()
	if marks.is_empty():
		_fail("child boot produced no BOOT_PROFILE timeline at all")
		_report()
		return

	# ---- structural ordering: the loading surface must precede the heavy work.
	_check_present(marks, "startup_scene_ready")
	_check_present(marks, "first_frame")
	_check_present(marks, "shell_instantiated")
	_check_present(marks, "menu:skirmish_options")
	_check_present(marks, "shell_visible")

	# THE central assertion. `first_frame` is the startup scene's first drawn
	# frame; `shell_instantiated` is the end of the shell's synchronous compile.
	# If someone repoints run/main_scene at scenes/boot.tscn, or adds a heavy
	# preload to startup_boot.gd, the shell compile moves ahead of the first
	# frame and this fails - which is exactly the black-window regression.
	_check_before(marks, "first_frame", "shell_instantiated",
		"the loading surface must be drawn BEFORE the shell scene is compiled")

	# The startup scene must be the main scene: its _ready has to run before the
	# shell exists at all.
	_check_before(marks, "startup_scene_ready", "shell_instantiated",
		"scenes/startup_boot.tscn must be the main scene, not scenes/boot.tscn")

	# The skirmish availability sweep must stay off the pre-first-frame path.
	_check_before(marks, "first_frame", "menu:skirmish_options",
		"the faction/map availability sweep must not run before the first frame")

	# The window settles before anything is drawn, so there is no visible resize.
	_check_before(marks, "startup:display_settings", "first_frame",
		"stored display settings must be applied before the first drawn frame")

	# THE MENU IS UP BEFORE THE SWEEP RUNS, NOT AFTER IT. The sweep used to be a
	# single one-shot on the next process_frame, which landed in the frame BEFORE
	# the menu's first drawn one - measured, the menu was not interactive until it
	# finished. It is now stepped from _process after the shell has been presented.
	_check_before(marks, "menu_first_frame", "menu:skirmish_options",
		"the menu must draw its first frame before the availability sweep runs")
	_check_before(marks, "shell_visible", "menu:skirmish_options",
		"the loading surface must hand over to the menu before the availability sweep runs")

	# THE HEAVY SCREENS ARE NOT BUILT DURING BOOT. `menu:wotr_screen_construct`,
	# `menu:wotr_setup_screen_construct` and `menu:multiplayer_lobby_construct` are
	# emitted where those panels are CONSTRUCTED, which is now the point of
	# navigation. A plain boot navigates nowhere, so none of them may appear in the
	# timeline at all. Re-adding the construction to `_ready` puts them back.
	_check_absent(marks, "menu:wotr_screen_construct",
		"the War of the Ring screen was built during boot; it belongs behind its navigation")
	_check_absent(marks, "menu:wotr_setup_screen_construct",
		"the GAME SETUP screen was built during boot; it belongs behind its navigation")
	_check_absent(marks, "menu:multiplayer_lobby_construct",
		"the GAME LOBBY was built during boot; it belongs behind a host/join")

	# ---- budgets.
	_check_at_most(marks, "first_frame", "at_ms", BUDGET_FIRST_FRAME_AT_MS,
		"time to the first drawn frame")
	_check_at_most(marks, "autoload:ContentDB.reload", "delta_ms", BUDGET_CONTENTDB_RELOAD_MS,
		"ContentDB.reload")
	_check_at_most(marks, "shell_instantiated", "delta_ms", BUDGET_SHELL_COMPILE_MS,
		"synchronous shell scene compile")
	_check_at_most(marks, "menu:skirmish_sweep_total", "delta_ms", BUDGET_SKIRMISH_SWEEP_TOTAL_MS,
		"total work done by the stepped faction/map availability sweep")
	_check_at_most(marks, "menu:skirmish_sweep_worst_step", "delta_ms",
		BUDGET_SKIRMISH_SWEEP_WORST_STEP_MS,
		"the longest single step of the availability sweep")
	_check_at_most(marks, "shell_visible", "at_ms", BUDGET_SHELL_VISIBLE_AT_MS,
		"time until the menu is up")

	await _check_loading_surface_has_content()
	await _check_heavy_screens_are_not_compiled_for_a_menu()
	_report()


func _check_heavy_screens_are_not_compiled_for_a_menu() -> void:
	## The in-process half of the preload guard. The child boot above proves the
	## heavy panels are not CONSTRUCTED during boot; this proves their scripts are
	## not COMPILED either, proves the cache actually caches, and proves that a
	## lazy load which fails refuses by name instead of leaving a dead control.
	##
	## Instantiating boot.tscn here is the same thing the engine does at startup,
	## so `_ready` has run in full by the first assertion below.
	root.size = Vector2i(1920, 1080)
	var packed: Resource = load("res://scenes/boot.tscn")
	_checks += 1
	if packed == null:
		_fail("res://scenes/boot.tscn did not load")
		return
	var menu := (packed as PackedScene).instantiate()
	root.add_child(menu)

	# 1. NOT COMPILED. Read before yielding a frame, so the stepped availability
	#    sweep (which legitimately compiles the slice) provably has not started.
	for lazy_id in LAZY_IDS:
		_checks += 1
		if bool(menu.call("lazy_script_is_compiled", lazy_id)):
			_fail("'%s' was compiled during the menu's _ready; it belongs behind its navigation" % lazy_id)

	# 2. NOT EVEN IN THE RESOURCE CACHE, unless this exact menu queued its
	#    intentional loader-thread warm. Godot enters a threaded request in the
	#    global cache before the menu collects/compiles it, so cache presence alone
	#    cannot distinguish that warm from an eager preload. The bookkeeping check
	#    keeps the exception exact: unrelated cached heavy screens still fail.
	for path_index in LAZY_PATHS.size():
		var path := LAZY_PATHS[path_index]
		_checks += 1
		var lazy_id := LAZY_PATH_IDS[path_index]
		var intentionally_warming := bool(menu.call("lazy_script_warm_was_requested", lazy_id))
		if ResourceLoader.has_cached(path) and not intentionally_warming:
			_fail("%s is already in the resource cache after a bare menu _ready - something still preloads it" % path)

	# 3. A FAILED LAZY LOAD IS NAMED, NOT SILENT. Point retail's GAME SETUP screen
	#    at a path that does not exist and take the route the WAR OF THE RING entry
	#    takes. That route is chosen deliberately: it is the one that normally
	#    opens EVEN WHEN the campaign is unavailable, so the only thing that can
	#    refuse it is the compile failure being injected here.
	#
	#    The route must refuse, the reason must NAME the file, and the WAR OF THE
	#    RING entry must carry that reason. The failure mode being guarded against
	#    is a button that still looks live and does nothing when pressed.
	menu.call("set_lazy_script_path_for_test", "wotr_setup_screen", "res://src/ui/no_such_screen.gd")
	_checks += 1
	if bool(menu.call("show_page", "wotr_setup")):
		_fail("navigating to GAME SETUP succeeded with no screen script behind it")
	var reason := String(menu.call("wotr_unavailable_reason"))
	_checks += 1
	if not reason.contains("no_such_screen.gd"):
		_fail("a failed lazy load did not name the file it could not compile (got '%s')" % reason)
	_checks += 1
	if String(menu.call("lazy_script_failure", "wotr_setup_screen")) == "":
		_fail("a failed lazy load recorded no reason at all")
	var wotr_btn := menu.get_node_or_null("Center/WarOfTheRing") as Button
	_checks += 1
	if wotr_btn == null or not wotr_btn.disabled:
		_fail("the WAR OF THE RING entry stayed pressable after its screen failed to compile")
	_checks += 1
	if wotr_btn != null and wotr_btn.tooltip_text != reason:
		_fail("the WAR OF THE RING entry does not carry the failure reason (tooltip '%s')" % wotr_btn.tooltip_text)
	# The SOLO PLAY flyout row is the surface the player actually clicks; it must
	# say the same thing the button does.
	var solo_flyout_menu = menu.call("shell_flyout", "solo")
	var wotr_row: Button = null
	if solo_flyout_menu != null:
		for row_value in solo_flyout_menu.item_buttons:
			var row := row_value as Button
			if row != null and row.text.to_lower().contains("war of the ring"):
				wotr_row = row
	_checks += 1
	if wotr_row == null or not wotr_row.disabled or wotr_row.tooltip_text != reason:
		_fail("the SOLO PLAY 'WAR OF THE RING' row did not adopt the compile failure reason")

	# 4. THE CACHE IS A CACHE, AND A RECOVERED SCRIPT WITHDRAWS ITS REFUSAL.
	#    Restore the real path, navigate twice, and assert the second navigation
	#    reuses the identical Script object - navigating to a screen must not pay
	#    for its compile again.
	menu.call("set_lazy_script_path_for_test", "wotr_setup_screen", "res://src/ui/wotr_setup_screen.gd")
	_checks += 1
	if not bool(menu.call("show_page", "wotr_setup")):
		_fail("the GAME SETUP route refused with its real script in place")
	_checks += 1
	if String(menu.call("wotr_unavailable_reason")).contains("no_such_screen.gd"):
		_fail("the compile-failure reason survived a successful compile; it is now a stale refusal")
	var first_script: Variant = menu.call("_lazy_script", "wotr_setup_screen")
	menu.call("show_page", "main")
	menu.call("show_page", "wotr_setup")
	_checks += 1
	if menu.call("_lazy_script", "wotr_setup_screen") != first_script:
		_fail("navigating to the GAME SETUP screen twice re-resolved its script; the cache is not caching")

	first_script = null
	menu.call("cleanup_for_test")
	root.remove_child(menu)
	menu.free()
	packed = null
	await process_frame


func _check_absent(marks: Array, label: String, why: String) -> void:
	_checks += 1
	if not _find(marks, label).is_empty():
		_fail("boot stage '%s' ran during a plain boot - %s" % [label, why])


func _check_loading_surface_has_content() -> void:
	## The ordering checks above prove the surface goes up FIRST. They cannot
	## prove it has anything ON it - a screen that renders an empty CanvasLayer
	## would satisfy every one of them and still leave the player looking at a
	## black window, which is the exact complaint being fixed.
	##
	## So build the real screen in its real startup shape and assert it carries
	## visible content. This is a headless structural assertion, NOT a claim that
	## it looks right: pixels still need a human. See the report.
	var packed: Resource = load("res://scenes/retail_loading_screen.tscn")
	_checks += 1
	if packed == null:
		_fail("res://scenes/retail_loading_screen.tscn did not load")
		return
	var screen: CanvasLayer = (packed as PackedScene).instantiate()
	root.add_child(screen)
	await process_frame
	screen.call("configure_boot", "OPEN BFME")
	screen.call("set_load_progress", 0.42, "Loading game shell")
	await process_frame

	# Something is actually drawn: at least one visible CanvasItem with area.
	var drawn := 0
	var stack: Array[Node] = [screen]
	while not stack.is_empty():
		var node: Node = stack.pop_back()
		for child in node.get_children():
			stack.append(child)
		var item := node as Control
		if item != null and item.is_visible_in_tree() and item.size.x > 0.0 and item.size.y > 0.0:
			drawn += 1
	_checks += 1
	if drawn < 5:
		_fail("the startup loading surface drew only %d visible sized controls; it is effectively blank" % drawn)

	# The progress readout is real and reflects what it was given.
	_checks += 1
	var ratio := float(screen.call("get_load_progress"))
	if not is_equal_approx(ratio, 0.42):
		_fail("the loading surface did not take the progress it was given (got %f)" % ratio)

	# Startup shape, not match shape: the per-player match table must be gone,
	# so the startup screen never shows an empty player roster for a match that
	# has not been chosen.
	_checks += 1
	var table := _find_first(screen, "GridContainer")
	if table != null and (table as Control).is_visible_in_tree():
		_fail("the match player table is still visible in the startup shape of the loading screen")

	screen.queue_free()
	await process_frame


func _find_first(node: Node, klass: String) -> Node:
	for child in node.get_children():
		if child.is_class(klass):
			return child
		var found := _find_first(child, klass)
		if found != null:
			return found
	return null


func _run_child_boot() -> Array:
	var executable := OS.get_executable_path()
	var project_dir := ProjectSettings.globalize_path("res://")
	var output: Array = []
	var arguments: PackedStringArray = [
		"--headless",
		"--path", project_dir,
		"--quit-after", str(CHILD_TIMEOUT_FRAMES),
		"--", "--profile-boot",
	]
	var exit_code := OS.execute(executable, arguments, output, true)
	var text := ""
	for chunk in output:
		text += String(chunk)
	if exit_code != 0:
		_fail("child boot exited with code %d" % exit_code)
	var marks: Array = []
	for line_value in text.split("\n"):
		var line := String(line_value).strip_edges()
		if not line.begins_with("BOOT_PROFILE "):
			continue
		var parsed := _parse_mark(line)
		if not parsed.is_empty():
			marks.append(parsed)
	return marks


func _parse_mark(line: String) -> Dictionary:
	## `BOOT_PROFILE stage=<label padded> delta_ms=<n> at_ms=<n>`. The label is
	## printed left-padded to a fixed width and may contain no spaces of its own,
	## so splitting on whitespace is unambiguous.
	var label := ""
	var delta := -1
	var at := -1
	for field_value in line.split(" ", false):
		var field := String(field_value)
		if field.begins_with("stage="):
			label = field.substr("stage=".length())
		elif field.begins_with("delta_ms="):
			delta = int(field.substr("delta_ms=".length()))
		elif field.begins_with("at_ms="):
			at = int(field.substr("at_ms=".length()))
	if label == "" or delta < 0 or at < 0:
		return {}
	return {"label": label, "delta_ms": delta, "at_ms": at}


func _find(marks: Array, label: String) -> Dictionary:
	for mark_value in marks:
		var mark := mark_value as Dictionary
		if String(mark.get("label", "")) == label:
			return mark
	return {}


func _check_present(marks: Array, label: String) -> void:
	_checks += 1
	if _find(marks, label).is_empty():
		_fail("boot stage '%s' never ran" % label)


func _check_before(marks: Array, earlier: String, later: String, why: String) -> void:
	_checks += 1
	var a := _find(marks, earlier)
	var b := _find(marks, later)
	if a.is_empty() or b.is_empty():
		_fail("cannot order '%s' before '%s': a stage is missing" % [earlier, later])
		return
	if int(a["at_ms"]) > int(b["at_ms"]):
		_fail("'%s' (%d ms) ran AFTER '%s' (%d ms) - %s"
			% [earlier, int(a["at_ms"]), later, int(b["at_ms"]), why])


func _check_at_most(marks: Array, label: String, field: String, budget: int, why: String) -> void:
	_checks += 1
	var mark := _find(marks, label)
	if mark.is_empty():
		_fail("cannot budget '%s': the stage never ran" % label)
		return
	var value := int(mark[field])
	if value > budget:
		_fail("%s took %d ms, over the %d ms budget (stage '%s' %s)"
			% [why, value, budget, label, field])


func _fail(message: String) -> void:
	_failures.append(message)


func _report() -> void:
	for failure in _failures:
		print("FAIL: %s" % failure)
	print("boot_startup_runner: %d checks, %d failures" % [_checks, _failures.size()])
	if _checks != EXPECTED_CHECKS:
		print("FAIL: expected %d checks, ran %d - update EXPECTED_CHECKS deliberately" % [EXPECTED_CHECKS, _checks])
		quit(1)
	quit(0 if _failures.is_empty() else 1)

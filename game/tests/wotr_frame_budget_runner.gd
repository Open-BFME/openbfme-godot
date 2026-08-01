extends SceneTree

## THE WAR OF THE RING SCREEN'S FRAME-TIME BUDGET, AND THE INVARIANT THAT BROKE IT.
##
## WHY THIS RUNNER EXISTS, IN ONE PARAGRAPH. The owner played this screen and
## reported it was "super laggy to play". Measured through
## `tests/wotr_perf_runner.gd` on the real screen mounted through the real menu,
## the idle strategic map was costing 147.29 ms per frame - 6.8 frames per second.
## None of it was the terrain, the markers, the glow, the multisampling or the HUD.
## It was the engine printing six warnings and errors, seventeen lines, EVERY
## FRAME, because `wotr_map_view.gd` set the directional light's first two
## parallel-split-shadow planes and left the third at Godot's default of 0.5 -
## which is exactly where it had put the second. Two split planes at the same
## depth make a shadow projection with a zero depth range, every entry in it comes
## out non-finite, and Godot says so, forever, at whatever the process's stderr
## happens to cost. Nothing in this project could have caught that: every existing
## runner is headless, and headless Godot does not render, so it never builds a
## shadow split and never trips the defect. The only number anyone had for this
## screen came from a probe of the map's SubViewport ON ITS OWN and said 1.01 ms.
##
## So this runner does the two things that would have caught it.
##
## 1. THE STRUCTURAL PIN, and it is the important half. The split planes must be
##    STRICTLY INCREASING. That is a property of the scene the map view builds, it
##    is exact, it does not depend on how fast this machine is or on who else is
##    using it, and it fails on the defect itself rather than on a symptom. If
##    somebody adds a fourth split, retunes the first three, or writes two of them
##    from the same expression, this reddens.
##
## 2. THE BUDGET, on real frames of the real screen. The structural pin covers the
##    defect we know about. The budget covers the ones we do not: anything that
##    makes an idle strategic map cost an order of magnitude more than it does
##    today shows up here whatever its cause. It is a WINDOWED run for the same
##    reason - a budget asserted headless would be asserting nothing.
##
## THE BUDGET IS DELIBERATELY LOOSE AND THAT IS NOT A WEAKNESS. This machine is
## shared with other agents and other builds; a budget tight enough to catch a
## 20% drift would redden on somebody else's compile. See `IDLE_BUDGET_MS` for the
## arithmetic behind the number that was chosen instead.
##
## Usage:
##   Godot_v4.7 --path game --script tests/wotr_frame_budget_runner.gd -- [--at 80,80]
## with the living-world env vars set. It needs a WINDOW - do not run it headless;
## it says so and fails rather than reporting a pass it did not earn.

const SHELL_SCENE_PATH := "res://scenes/boot.tscn"
const SHELL_SETTLE_FRAMES := 12
const WARMUP_FRAMES := 30
const SAMPLE_FRAMES := 120
const WINDOW_SIZE := Vector2i(1920, 1080)
const ONSCREEN_POSITION := Vector2i(80, 80)

## WHAT AN IDLE STRATEGIC MAP MAY COST, AND WHERE THE NUMBER COMES FROM.
##
## Measured on this machine after the fix, over several runs while other agents
## were working: idle sits at 0.87-1.30 ms per frame with vsync off. The defect
## this runner exists to prevent cost 147.29 ms - and cost ~5-6 ms even with the
## process's stderr redirected to a file, which is the CHEAPEST that failure mode
## can possibly be. So the budget has to sit above the honest measurement and
## below the cheapest form of the disaster.
##
## 4.0 ms is that gap. It is 3x the worst honest idle sample and comfortably under
## the ~5 ms floor of a per-frame warning storm, and it is a quarter of a 60 Hz
## frame, so a screen that passes this has real headroom on a slower machine than
## this one. It is NOT a claim that 4 ms is acceptable - it is the line past which
## something has gone wrong enough to be worth a red test.
const IDLE_BUDGET_MS := 4.0

## WHAT PANNING MAY COST. Panning is measured separately because it is honestly
## more expensive and pretending otherwise would mean either a budget too loose to
## mean anything or a red test for a cost that is real and understood: holding a
## pan key at the zoom ceiling runs `_pan_ground`'s pan-wall search, which is a
## six-step bisection over `zoom_ceiling()`, itself a twenty-step bisection over
## the twelve-segment cut-edge test. Measured: 6.27-6.96 ms/frame panning at the
## ceiling, 3.64 ms panning zoomed in, against 1.0 ms idle. At 60 Hz that is a
## third of the frame and it is not lag; the budget is set to catch it becoming
## one.
const PAN_BUDGET_MS := 12.0

## LIVENESS, the same rule the rest of this lane carries: the expected check count
## is asserted, so a check that aborts before it asserts reddens the run instead of
## quietly shrinking it. Raise this when you add a check; never lower it.
const EXPECTED_CHECKS := 8

## WHAT THE MAP OVERLAY'S OWN REPAINT MAY COST. See `_assert_budgets` for the
## profile this number came out of and for why it is measured apart from the frame.
const OVERLAY_BUDGET_MS := 7.0

var _menu: Node = null
var _screen: Control = null
var _shell_frames := 0
var _mounted := false
var _passed := 0
var _failed := 0

var _phase := 0
var _warmed := 0
var _samples: Array[int] = []
var _last_us := 0
var _idle_ms := 0.0
var _pan_ms := 0.0


func _initialize() -> void:
	print("== WAR OF THE RING FRAME BUDGET ==")
	var window := root
	window.size = WINDOW_SIZE
	window.title = "OpenBFME - War of the Ring frame budget"
	window.content_scale_mode = Window.CONTENT_SCALE_MODE_DISABLED
	DisplayServer.window_set_flag(DisplayServer.WINDOW_FLAG_NO_FOCUS, true)
	root.set_flag(Window.FLAG_NO_FOCUS, true)
	DisplayServer.window_set_position(_wanted_window_position())
	# THE BUDGET IS ABOUT COST, NOT ABOUT THE REFRESH RATE. With vsync on, every
	# frame cheaper than the refresh interval measures AS the refresh interval, so
	# a 1 ms screen and a 15 ms screen would both report 16.7 and the budget would
	# be asserting the monitor.
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	Engine.max_fps = 0

	var packed: Resource = load(SHELL_SCENE_PATH)
	if packed == null:
		print("  FAIL  the shell scene %s did not load" % SHELL_SCENE_PATH)
		quit(1)
		return
	_menu = (packed as PackedScene).instantiate()
	window.add_child(_menu)


func _process(_delta: float) -> bool:
	if not _mounted:
		_shell_frames += 1
		if _shell_frames < SHELL_SETTLE_FRAMES:
			return false
		_mounted = true
		if not _mount():
			_report()
			return true
		_begin_phase()
		return false

	var now := Time.get_ticks_usec()
	var wall := now - _last_us
	_last_us = now
	if _warmed < WARMUP_FRAMES:
		_warmed += 1
		return false
	_samples.append(wall)
	if _samples.size() < SAMPLE_FRAMES:
		return false

	# THE MEDIAN, NOT THE MEAN. This machine is shared, so a single frame stolen by
	# somebody else's compile is expected and must not decide a test; the median of
	# 120 frames is immune to a handful of those and still moves the instant the
	# TYPICAL frame gets more expensive, which is what "laggy" means.
	var median := _median_ms()
	if _phase == 0:
		_idle_ms = median
	else:
		_pan_ms = median
		_press_pan_key(false)
	_phase += 1
	if _phase >= 2:
		_assert_budgets()
		_report()
		return true
	_begin_phase()
	return false


func _mount() -> bool:
	# A HEADLESS RUN CANNOT MEASURE A FRAME AND MUST NOT SAY IT DID. Headless Godot
	# never renders, so every timing below would be the cost of an empty loop and
	# every one of them would pass. That is precisely the blindness that let a
	# 147 ms screen through fourteen green runners.
	if DisplayServer.get_name() == "headless":
		_fail("the_run_has_a_real_window",
			"this is a headless display server; a frame budget asserted here would assert nothing")
		return false
	_check("the_run_has_a_real_window", true, "display server %s" % DisplayServer.get_name())
	if _menu == null or not _menu.has_method("show_page"):
		_fail("the_shell_mounts", "the shell did not instantiate")
		return false
	if not bool(_menu.call("show_page", "wotr")):
		var reason := String(_menu.call("wotr_unavailable_reason")) \
			if _menu.has_method("wotr_unavailable_reason") else "<no reason given>"
		_fail("the_shell_mounts", "the shell refused War of the Ring: %s" % reason)
		return false
	_screen = _menu.get("wotr_screen") as Control
	if _screen == null:
		_fail("the_shell_mounts", "the shell owns no strategic screen")
		return false
	_check("the_shell_mounts", true, "screen rect %s" % str(Rect2(_screen.get_global_rect())))
	_check_the_shadow_splits_are_strictly_increasing()
	return true


## THE INVARIANT THE 147 ms DEFECT VIOLATED, asserted directly.
##
## Godot's parallel-split shadow map cuts the camera's depth range at
## `split_1 < split_2 < split_3` and builds one shadow projection between each
## consecutive pair of `[near, split_1, split_2, split_3, far]`. A pair at the same
## depth divides by a zero range, and the resulting non-finite projection makes the
## engine log six messages a frame forever - which is what "super laggy to play"
## turned out to be. Any two of these being equal is therefore not a matter of
## taste, it is a defect, and it is one that renders normally in every screenshot.
##
## THE FRACTIONS THEMSELVES ARE NOT PINNED, only their ORDER. Whoever retunes the
## relief should be free to move them; what they must not do is put two of them in
## the same place, or leave one at a default that happens to collide with another.
func _check_the_shadow_splits_are_strictly_increasing() -> void:
	var sun: DirectionalLight3D = null
	var map: Node = _screen.get("map3d")
	var viewport := null if map == null else map.get("viewport") as SubViewport
	if viewport != null:
		sun = viewport.get_node_or_null("MapSun") as DirectionalLight3D
	if sun == null:
		_fail("the_shadow_splits_are_strictly_increasing",
			"the map's directional light could not be found, so the invariant is unchecked")
		_fail("every_split_slice_has_a_non_zero_depth_range", "no light")
		return
	var one := sun.directional_shadow_split_1
	var two := sun.directional_shadow_split_2
	var three := sun.directional_shadow_split_3
	_check("the_shadow_splits_are_strictly_increasing",
		one > 0.0 and one < two and two < three and three < 1.0,
		"split_1=%.3f split_2=%.3f split_3=%.3f (shadows %s, mode %d)" % [
			one, two, three, "on" if sun.shadow_enabled else "OFF",
			sun.directional_shadow_mode])
	# The same property stated as what it MEANS, so the failure message names the
	# consequence rather than three numbers: no slice may be zero-deep.
	var planes: Array[float] = [0.0, one, two, three, 1.0]
	var thinnest := 1.0
	for index in range(planes.size() - 1):
		thinnest = minf(thinnest, planes[index + 1] - planes[index])
	_check("every_split_slice_has_a_non_zero_depth_range", thinnest > 0.0,
		"thinnest shadow slice spans %.3f of the depth range" % thinnest)


func _begin_phase() -> void:
	_warmed = 0
	_samples.clear()
	if _phase == 0:
		_screen.toggle_diagnostics(false)
		_screen.map3d.reset_camera()
	else:
		_screen.map3d.reset_camera()
		_press_pan_key(true)
	_last_us = Time.get_ticks_usec()


func _assert_budgets() -> void:
	_check("an_idle_strategic_map_stays_inside_its_frame_budget",
		_idle_ms <= IDLE_BUDGET_MS,
		"idle median %.2f ms/frame (%.0f fps), budget %.2f ms" % [
			_idle_ms, 1000.0 / maxf(_idle_ms, 0.0001), IDLE_BUDGET_MS])
	_check("panning_the_map_stays_inside_its_frame_budget",
		_pan_ms <= PAN_BUDGET_MS,
		"pan median %.2f ms/frame (%.0f fps), budget %.2f ms" % [
			_pan_ms, 1000.0 / maxf(_pan_ms, 0.0001), PAN_BUDGET_MS])
	# THE SHAPE OF THE COST, not only its size. Panning is legitimately dearer than
	# idling; what would be a NEW defect is idling becoming as expensive as panning,
	# because that is what a per-frame storm looks like - it does not care what the
	# player is doing. This check is the one that would have caught the original
	# defect by its signature rather than by its size.
	_check("idling_is_cheaper_than_driving_the_camera",
		_idle_ms < _pan_ms,
		"idle %.2f ms vs pan %.2f ms - an idle map that costs as much as a moving one is a per-frame storm, not a scene" % [
			_idle_ms, _pan_ms])

	# THE OVERLAY'S OWN SHARE, AND WHY IT IS PINNED SEPARATELY FROM THE FRAME.
	#
	# Round 8 opened with the pan budget drifted from 5.95 ms to about 9 ms and no
	# way to say what had taken it: the frame number above is a single scalar, and
	# on a shared machine its run-to-run spread is wider than the drift that has to
	# be caught. Profiled through `wotr_perf_runner.gd --probe 3`, the camera drive
	# turned out to cost 0.006 ms and the OVERLAY REPAINT 4.2 ms - the whole of the
	# difference between panning with the overlay shown (7.9 ms) and hidden
	# (3.9 ms). It had drifted because round 7 put all 98 of retail's authored build
	# plots on the map and hung a general's medallion on every banner, and nothing
	# measured either.
	#
	# So the overlay is measured by the view itself, on its own clock, and asserted
	# here. It is a far better regression detector than the frame time: it excludes
	# the renderer, the HUD and every other process on this machine, so the same
	# noise floor buys a tolerance several times tighter. The failure message
	# carries the SECTION BREAKDOWN, which is the thing that was missing in round 8
	# - a red test that says "the overlay went from 4.2 to 9 ms and it was the
	# plots" needs no follow-up investigation at all.
	#
	# 7.0 ms is the line, and like `IDLE_BUDGET_MS` it is not a claim that 7 ms
	# would be acceptable: it sits comfortably above the measured 4.2 and
	# comfortably below the 12 ms whole-frame budget the overlay must leave room
	# inside.
	var map: Node = _screen.get("map3d")
	var overlay_ms := -1.0 if map == null else float(map.overlay_paint_ms)
	var sections: Dictionary = {} if map == null else map.overlay_section_ms
	var keys: Array = sections.keys()
	keys.sort()
	var parts: Array[String] = []
	for key in keys:
		parts.append("%s %.2f" % [String(key), float(sections[key])])
	_check("the_overlay_repaint_stays_inside_its_share_of_the_pan_budget",
		overlay_ms >= 0.0 and overlay_ms <= OVERLAY_BUDGET_MS,
		"overlay repaint %.2f ms, budget %.2f ms [%s]" % [
			overlay_ms, OVERLAY_BUDGET_MS, ", ".join(parts)])


func _median_ms() -> float:
	var sorted := _samples.duplicate()
	sorted.sort()
	return float(sorted[sorted.size() / 2]) / 1000.0


func _press_pan_key(down: bool) -> void:
	var event := InputEventKey.new()
	event.keycode = KEY_D
	event.physical_keycode = KEY_D
	event.pressed = down
	Input.parse_input_event(event)


func _report() -> void:
	var total := _passed + _failed
	print("\nchecks run %d (expected %d), passed %d, failed %d" % [
		total, EXPECTED_CHECKS, _passed, _failed])
	if total != EXPECTED_CHECKS:
		print("LIVENESS FAILURE: %d checks ran, %d expected. A check was skipped." % [
			total, EXPECTED_CHECKS])
		quit(1)
		return
	quit(1 if _failed > 0 else 0)


func _wanted_window_position() -> Vector2i:
	var override := _argument("--at", "")
	if override.is_empty():
		return ONSCREEN_POSITION
	var parts := override.split(",", false)
	if parts.size() == 2 and parts[0].is_valid_int() and parts[1].is_valid_int():
		return Vector2i(int(parts[0]), int(parts[1]))
	return ONSCREEN_POSITION


func _argument(flag: String, fallback: String) -> String:
	var args := OS.get_cmdline_user_args()
	for index in range(args.size() - 1):
		if args[index] == flag:
			return args[index + 1]
	return fallback


func _check(what: String, condition: bool, detail: String = "") -> void:
	if condition:
		_passed += 1
		print("  PASS  %s%s" % [what, "" if detail.is_empty() else " (%s)" % detail])
	else:
		_fail(what, detail)


func _fail(what: String, detail: String) -> void:
	_failed += 1
	print("  FAIL  %s%s" % [what, "" if detail.is_empty() else " (%s)" % detail])

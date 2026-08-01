extends SceneTree

## MEASURE THE WAR OF THE RING SCREEN'S FRAME TIME IN THE REAL GAME.
##
## WHY THIS EXISTS. The project owner played War of the Ring and reported it was
## "super laggy to play". The only frame-time number anyone had ever taken for
## this screen came from a probe that rendered the map's `SubViewport` ON ITS OWN
## - no HUD, no full-window panel, no menu shell, no game loop - and reported
## 1.01 ms/frame. That number was then used to argue the map was free. It was a
## measurement of something the player never looks at.
##
## So this runner measures THE SCREEN THE PLAYER GETS. It mounts `scenes/boot.tscn`,
## navigates the shell to War of the Ring exactly as `wotr_capture_runner` does
## (`show_page("wotr")`, which seats the session and configures the screen with the
## mounted pack roots), and then samples real frames while driving the screen
## through the states the owner actually plays in: idle, hovering a region,
## panning with a held key, zoomed in, zoomed out, the tray's tabs, a build-plot
## ring, and an end turn.
##
## WHAT IT SAMPLES, AND WHY EACH ONE IS HERE
##
##   * WALL FRAME TIME, from `Time.get_ticks_usec()` between visits to `_process`.
##     This is the only number that corresponds to what "laggy" means. Everything
##     else exists to explain it.
##   * `Performance.TIME_PROCESS` / `TIME_PHYSICS_PROCESS` - the main thread's own
##     script time. If the wall time is high and this is low, it is not GDScript.
##   * `RenderingServer.viewport_get_measured_render_time_cpu/_gpu` for the ROOT
##     viewport AND for the map's `SubViewport`, SEPARATELY. This is the split
##     that the earlier probe could not make and that the whole question turns on:
##     it says how much of the frame is Middle-earth and how much is the HUD.
##   * Draw calls, primitives and objects in frame, so a regression that adds
##     passes shows up as passes rather than as an unexplained millisecond.
##
## VSYNC IS OFF BY DEFAULT AND THAT IS DELIBERATE. With vsync on, every frame that
## costs anything under the refresh interval reports as the refresh interval, so a
## screen costing 3 ms and a screen costing 15 ms are the same number and the
## measurement says nothing. `--vsync on` runs it the way the player's build runs
## for the one thing vsync CAN tell you - whether the frame is missing the refresh
## deadline at all.
##
## THE MAP IS ALSO MEASURED WITH ITS OWN VIEWPORT FROZEN (`--freeze` states below),
## because "the 3D map costs X" is only believable next to the same frame without
## it. That is an isolation probe, not a proposed setting.
##
## Usage:
##   Godot_v4.7 --path game --script tests/wotr_perf_runner.gd -- \
##       [--at 80,80] [--size 2560x1440] [--vsync on|off] [--frames 90]
## with the living-world env vars set the way `wotr_capture_runner` needs them.
##
## This runner ASSERTS NOTHING. It is an instrument. The budget assertion lives in
## `tests/wotr_frame_budget_runner.gd`, which is a test and fails loudly.

const SHELL_SCENE_PATH := "res://scenes/boot.tscn"
const SHELL_SETTLE_FRAMES := 12
## Frames discarded after a state change before sampling starts. A state change
## relayouts the HUD, re-fits the camera and forces the map's SubViewport to
## redraw; the first frames after that are the cost of the CHANGE, not of the
## state, and averaging them in is how a screen gets blamed for its own transitions.
const WARMUP_FRAMES := 24
## Frames averaged per state. At ~60-300 fps this is a fraction of a second per
## state, which is short enough that a whole sweep fits in one run and long enough
## that a single hitch cannot own the mean. The worst frame is reported separately
## for exactly that reason.
const SAMPLE_FRAMES := 90
const ONSCREEN_POSITION := Vector2i(80, 80)

var _menu: Node = null
var _screen: Control = null
var _shell_frames := 0
var _mounted := false

var _plan: Array[Dictionary] = []
var _step := 0
var _applied := false
var _warmed := 0
## Per-state samples. Wall frame time in microseconds; everything else as the
## monitor reports it.
var _wall_us: Array[int] = []
var _proc_ms: Array[float] = []
var _phys_ms: Array[float] = []
var _root_cpu: Array[float] = []
var _root_gpu: Array[float] = []
var _map_cpu: Array[float] = []
var _map_gpu: Array[float] = []
var _draw_calls: Array[float] = []
var _primitives: Array[float] = []
var _last_tick_us := 0
var _rows: Array[Dictionary] = []

## Where per-state PNGs go, or empty for a numbers-only run.
var _shot_dir := ""
var _window_size := Vector2i(2560, 1440)
var _frames := SAMPLE_FRAMES
var _vsync_on := false


func _initialize() -> void:
	_window_size = _parse_size(_argument("--size", "2560x1440"), Vector2i(2560, 1440))
	_frames = maxi(int(_argument("--frames", str(SAMPLE_FRAMES))), 10)
	_vsync_on = _argument("--vsync", "off") == "on"
	_shot_dir = _argument("--shots", "")
	if not _shot_dir.is_empty():
		DirAccess.make_dir_recursive_absolute(_shot_dir)

	var window := root
	window.size = _window_size
	window.title = "OpenBFME - War of the Ring frame-time probe"
	window.content_scale_mode = Window.CONTENT_SCALE_MODE_DISABLED
	DisplayServer.window_set_flag(DisplayServer.WINDOW_FLAG_NO_FOCUS, true)
	root.set_flag(Window.FLAG_NO_FOCUS, true)
	DisplayServer.window_set_position(_wanted_window_position())

	# See the header: with vsync on, anything cheaper than the refresh interval
	# measures as the refresh interval and the instrument reads nothing.
	DisplayServer.window_set_vsync_mode(
		DisplayServer.VSYNC_ENABLED if _vsync_on else DisplayServer.VSYNC_DISABLED)
	Engine.max_fps = 0

	var packed: Resource = load(SHELL_SCENE_PATH)
	if packed == null:
		push_error("[perf] %s failed to load; there is nothing to measure." % SHELL_SCENE_PATH)
		quit(1)
		return
	_menu = (packed as PackedScene).instantiate()
	window.add_child(_menu)

	# ISOLATION PROBES, run instead of the play sweep when `--probe 1` is passed.
	#
	# WHY A SEPARATE LIST. The play sweep answers "how expensive is each thing the
	# player does". It cannot answer "WHICH PASS is the expense", because every
	# state has all the passes in it. These states turn ONE thing off at a time
	# from the same idle framing, so each row is a subtraction against the row
	# above it. None of them is a proposed setting; they are an experiment.
	if _argument("--probe", "") == "1":
		_plan = [
			{"name": "baseline-idle", "action": "reset"},
			{"name": "no-sun-shadow", "action": "probe_shadow_off"},
			{"name": "shadow-back-on", "action": "probe_shadow_on"},
			{"name": "no-glow", "action": "probe_glow_off"},
			{"name": "glow-back-on", "action": "probe_glow_on"},
			{"name": "no-msaa", "action": "probe_msaa_off"},
			{"name": "msaa-back-on", "action": "probe_msaa_on"},
			{"name": "no-territory-meshes", "action": "probe_territory_off"},
			{"name": "territory-back-on", "action": "probe_territory_on"},
			{"name": "no-markers", "action": "probe_markers_off"},
			{"name": "markers-back-on", "action": "probe_markers_on"},
			{"name": "perspective-camera", "action": "probe_perspective"},
			{"name": "orthogonal-again", "action": "probe_orthogonal"},
			{"name": "shadow-1-split", "action": "probe_split_1"},
			{"name": "shadow-2-splits", "action": "probe_split_2"},
			{"name": "shadow-4-splits", "action": "probe_split_4"},
			{"name": "shadow-1-split-again", "action": "probe_split_1"},
		]
		print("[perf] ISOLATION PROBE MODE: each row turns one thing off; read them as subtractions.")
		return

	# THE PAN PATH, TAKEN APART, `--probe 3`. Panning was the one state left costing
	# several milliseconds after the shadow storm was fixed, and "panning is
	# expensive" is not an answer - the camera drive, the cut-edge search it runs
	# and the overlay repaint it triggers are three different things and only
	# measurement says which. `micro` times the pieces directly; the states around
	# it hold the key down with one piece disabled at a time.
	if _argument("--probe", "") == "3":
		_plan = [
			{"name": "micro-timings", "action": "micro"},
			{"name": "pan-baseline", "action": "pan_hold"},
			{"name": "pan-no-overlay", "action": "pan_no_overlay"},
			{"name": "pan-overlay-back", "action": "pan_overlay_back"},
			{"name": "pan-zoomed-in", "action": "pan_zoomed_in"},
			{"name": "idle-for-contrast", "action": "pan_release"},
		]
		print("[perf] PAN BREAKDOWN MODE.")
		return

	# THE SHADOW-QUALITY COMPARISON, `--probe 2`. Four pictures of ONE framing under
	# four shadow settings, so "the cheap one does not cost the relief" is a thing
	# somebody can look at rather than a claim. Pair it with `--shots <dir>`.
	if _argument("--probe", "") == "2":
		_plan = [
			{"name": "shadow-4-splits", "action": "probe_split_4"},
			# THE SAME FOUR SPLITS WITH `split_3` SEPARATED FROM `split_2`. The map
			# view sets `split_1` and `split_2` and leaves `split_3` at Godot's own
			# default of 0.5 - which IS `split_2`, so two consecutive split planes sit
			# at the same depth and the shadow projection built between them divides
			# by a zero range. This row is the test of that reading.
			{"name": "shadow-4-splits-separated", "action": "probe_split_4_separated"},
			{"name": "shadow-2-splits", "action": "probe_split_2"},
			{"name": "shadow-1-split", "action": "probe_split_1"},
			{"name": "shadow-off", "action": "probe_shadow_off"},
		]
		print("[perf] SHADOW COMPARISON MODE.")
		return

	# THE STATES, in the order a player moves through them. `freeze` and `thaw`
	# bracket the isolation probe described in the header.
	_plan = [
		{"name": "idle", "action": "reset"},
		{"name": "idle-map-frozen", "action": "freeze"},
		{"name": "idle-hud-hidden", "action": "hide_hud"},
		{"name": "idle-again", "action": "thaw"},
		{"name": "hover-region", "action": "hover"},
		{"name": "pan-held-key", "action": "pan_hold"},
		{"name": "after-pan", "action": "pan_release"},
		{"name": "zoomed-in", "action": "zoom_in"},
		{"name": "zoomed-out", "action": "zoom_out"},
		{"name": "orbited", "action": "orbit"},
		{"name": "tab-territory", "action": "tab_territory"},
		{"name": "tab-armies", "action": "tab_armies"},
		{"name": "tab-structures", "action": "tab_structures"},
		{"name": "build-plot-ring", "action": "plot"},
		{"name": "region-selected", "action": "stage"},
		{"name": "after-end-turn", "action": "end_turn"},
	]
	print("[perf] window %s, vsync %s, %d sampled frame(s) per state after %d warmup frame(s)" % [
		str(_window_size), "ON" if _vsync_on else "OFF", _frames, WARMUP_FRAMES])


func _process(_delta: float) -> bool:
	if not _mounted:
		_shell_frames += 1
		if _shell_frames < SHELL_SETTLE_FRAMES:
			return false
		_mount_through_the_menu()
		return false

	if _screen == null:
		return true

	if _step >= _plan.size():
		_report()
		quit(0)
		return true

	var step := _plan[_step]
	if not _applied:
		# ON STDERR ON PURPOSE. The engine's own per-frame warnings go to stderr, and
		# WHICH STATE a warning storm belongs to is the whole question - a marker on
		# stdout would sort into a different stream and answer nothing.
		printerr("[perf] STATE %s" % String(step["name"]))
		_apply(String(step["action"]))
		_applied = true
		_warmed = 0
		_clear_samples()
		_last_tick_us = Time.get_ticks_usec()
		return false

	var now := Time.get_ticks_usec()
	var wall := now - _last_tick_us
	_last_tick_us = now

	if _warmed < WARMUP_FRAMES:
		_warmed += 1
		return false

	_wall_us.append(wall)
	_proc_ms.append(Performance.get_monitor(Performance.TIME_PROCESS) * 1000.0)
	_phys_ms.append(Performance.get_monitor(Performance.TIME_PHYSICS_PROCESS) * 1000.0)
	_draw_calls.append(Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME))
	_primitives.append(Performance.get_monitor(Performance.RENDER_TOTAL_PRIMITIVES_IN_FRAME))
	var root_rid := root.get_viewport_rid()
	_root_cpu.append(RenderingServer.viewport_get_measured_render_time_cpu(root_rid))
	_root_gpu.append(RenderingServer.viewport_get_measured_render_time_gpu(root_rid))
	var map_rid := _map_viewport_rid()
	if map_rid.is_valid():
		_map_cpu.append(RenderingServer.viewport_get_measured_render_time_cpu(map_rid))
		_map_gpu.append(RenderingServer.viewport_get_measured_render_time_gpu(map_rid))

	if _wall_us.size() >= _frames:
		_rows.append(_summarise(String(step["name"])))
		# A PICTURE OF EVERY PROBE STATE, when one is asked for. A probe that trades
		# a millisecond for a worse map is not a fix, and the only way to know which
		# it is, is to look at the same framing under each setting.
		if not _shot_dir.is_empty():
			var image: Image = root.get_texture().get_image()
			var path: String = _shot_dir.path_join("%02d-%s.png" % [_step, String(step["name"])])
			if image.save_png(path) != OK:
				push_error("[perf] could not write %s" % path)
			else:
				print("[perf] wrote %s" % path)
		_step += 1
		_applied = false
	return false


func _mount_through_the_menu() -> void:
	_mounted = true
	if _menu == null or not _menu.has_method("show_page"):
		push_error("[perf] the shell did not instantiate.")
		quit(1)
		return
	if not bool(_menu.call("show_page", "wotr")):
		var reason := String(_menu.call("wotr_unavailable_reason")) \
			if _menu.has_method("wotr_unavailable_reason") else "<no reason given>"
		push_error("[perf] the shell refused to open War of the Ring: %s" % reason)
		quit(1)
		return
	_screen = _menu.get("wotr_screen") as Control
	if _screen == null:
		push_error("[perf] the shell owns no strategic screen.")
		quit(1)
		return
	# MEASUREMENT HAS TO BE TURNED ON PER VIEWPORT. Without this the render-time
	# getters return 0 and the split between map and HUD is silently unavailable -
	# which reads as "the map is free" rather than as "nothing was measured".
	RenderingServer.viewport_set_measure_render_time(root.get_viewport_rid(), true)
	var map_rid := _map_viewport_rid()
	if map_rid.is_valid():
		RenderingServer.viewport_set_measure_render_time(map_rid, true)
	else:
		push_error("[perf] the map's SubViewport could not be found; the map/HUD split will be empty.")
	print("[perf] mounted: screen rect %s in a %s window; map viewport %s" % [
		str(Rect2(_screen.get_global_rect())), str(Vector2i(root.size)),
		str(_map_viewport_size())])
	print("[perf] scene has %d node(s)" % int(Performance.get_monitor(Performance.OBJECT_NODE_COUNT)))


func _map_viewport() -> SubViewport:
	if _screen == null:
		return null
	var map: Node = _screen.get("map3d")
	if map == null:
		return null
	return map.get("viewport") as SubViewport


func _map_viewport_rid() -> RID:
	var viewport := _map_viewport()
	if viewport == null:
		return RID()
	return viewport.get_viewport_rid()


func _map_viewport_size() -> Vector2i:
	var viewport := _map_viewport()
	return Vector2i.ZERO if viewport == null else viewport.size


func _apply(action: String) -> void:
	if _screen == null or _screen.session == null:
		return
	match action:
		"reset":
			_screen.toggle_diagnostics(false)
			_screen.map3d.reset_camera()
		"freeze":
			# THE ISOLATION PROBE. Not a proposed setting - a subtraction, so the
			# map's share of the frame is stated as a difference between two frames
			# of the same screen rather than inferred from a viewport measured alone.
			var viewport := _map_viewport()
			if viewport != null:
				viewport.render_target_update_mode = SubViewport.UPDATE_DISABLED
		"thaw":
			var thawed := _map_viewport()
			if thawed != null:
				thawed.render_target_update_mode = SubViewport.UPDATE_ALWAYS
			_screen.set_hud_hidden(false)
		"hide_hud":
			_screen.set_hud_hidden(true)
		"hover":
			var staging: PackedStringArray = _screen.session.staging_regions()
			if not staging.is_empty():
				_screen._on_region_hovered(staging[0])
				_screen.refresh()
		"stage":
			var staged: PackedStringArray = _screen.session.staging_regions()
			if not staged.is_empty():
				_screen.select_region(staged[0])
		"plot":
			for region_id in _screen.session.staging_regions():
				var region: Dictionary = _screen.session.world.region(String(region_id))
				if int(region.get("building_spot_count", 0)) <= 0:
					continue
				_screen.select_region(String(region_id))
				_screen._on_plot_clicked(String(region_id), 0)
				break
		"zoom_in":
			_screen.map3d.focus_region(_screen.session.selected_region, 0.10)
		"zoom_out":
			_screen.map3d.set_orbit(0.0, -52.0)
			_screen.map3d.focus_region("", 1.3)
		"orbit":
			_screen.map3d.set_orbit(0.9, -24.0)
		"pan_hold":
			_screen.map3d.reset_camera()
			_press_pan_key(true)
		"pan_release":
			_press_pan_key(false)
		"end_turn":
			if _screen.end_turn_button != null:
				_screen.end_turn_button.emit_signal("pressed")
		"tab_territory", "tab_armies", "tab_structures":
			var key := action.substr(4)
			if _screen._tab_buttons.has(key):
				(_screen._tab_buttons[key] as Button).emit_signal("pressed")
		"probe_shadow_off", "probe_shadow_on":
			var sun := _sun()
			if sun != null:
				sun.shadow_enabled = action.ends_with("_on")
		"probe_glow_off", "probe_glow_on":
			var env := _environment()
			if env != null:
				env.glow_enabled = action.ends_with("_on")
		"probe_msaa_off", "probe_msaa_on":
			var viewport := _map_viewport()
			if viewport != null:
				viewport.msaa_3d = Viewport.MSAA_4X if action.ends_with("_on") else Viewport.MSAA_DISABLED
		"probe_territory_off", "probe_territory_on":
			var territory := _screen.map3d.get("_territory_root") as Node3D
			if territory != null:
				territory.visible = action.ends_with("_on")
		"probe_markers_off", "probe_markers_on":
			var marker_root := _screen.map3d.get("_marker_root") as Node3D
			if marker_root != null:
				marker_root.visible = action.ends_with("_on")
		"probe_perspective":
			# THE SHADOW SPLIT MATH IS THE SUSPECT AND THIS IS THE TEST OF IT. Godot
			# builds a directional light's shadow splits out of the camera's own
			# projection; the first probe run spammed
			# `_light_instance_setup_directional_shadow` errors every frame under the
			# parallel lens this screen switched to. Swapping the lens without
			# touching anything else says whether the projection is what the cost is
			# attached to.
			if _screen.map3d.camera != null:
				_screen.map3d.camera.projection = Camera3D.PROJECTION_PERSPECTIVE
				_screen.map3d.camera.fov = 12.0
		"probe_orthogonal":
			if _screen.map3d.camera != null:
				_screen.map3d.camera.projection = Camera3D.PROJECTION_ORTHOGONAL
				_screen.map3d._apply_camera()
		"micro":
			_micro()
		"pan_no_overlay":
			# The overlay is the Control that paints the markers, banners, labels and
			# plot rings. Hidden, Godot skips its `_draw` entirely, so the difference
			# between this row and the one above it IS the repaint.
			var hidden := _screen.map3d.get("overlay") as Control
			if hidden != null:
				hidden.visible = false
		"pan_overlay_back":
			var shown := _screen.map3d.get("overlay") as Control
			if shown != null:
				shown.visible = true
		"pan_zoomed_in":
			# THE SAME HELD KEY FROM A FRAMING THAT IS NOT AT THE ZOOM CEILING. The
			# pan-wall search in `_pan_ground` is documented as only binding at the
			# ceiling; the screen OPENS at the ceiling, so "the common case is cheap"
			# needs checking against a framing where the search genuinely does not run.
			_screen.map3d.focus_region("", 0.45)
		"probe_split_4_separated":
			var separated := _sun()
			if separated != null:
				separated.shadow_enabled = true
				separated.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
				separated.directional_shadow_split_1 = 0.2
				separated.directional_shadow_split_2 = 0.5
				separated.directional_shadow_split_3 = 0.8
		"probe_split_1", "probe_split_2", "probe_split_4":
			# PARALLEL SPLIT SHADOW MAPS UNDER A PARALLEL CAMERA. PSSM slices the
			# camera's frustum by DEPTH, which is what a perspective lens gives you
			# and what a parallel one does not; this is the direct test of whether the
			# split count is what the degenerate projection is coming out of.
			var lit := _sun()
			if lit != null:
				lit.shadow_enabled = true
				match action:
					"probe_split_1":
						lit.directional_shadow_mode = DirectionalLight3D.SHADOW_ORTHOGONAL
					"probe_split_2":
						lit.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_2_SPLITS
					_:
						lit.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS


## TIME THE PAN PATH'S OWN PIECES, by calling them directly a fixed number of
## times. A per-frame average cannot separate `zoom_ceiling()` from the repaint it
## shares a frame with; this can, because it calls one at a time.
func _micro() -> void:
	var map: Node = _screen.map3d
	var reps := 200
	var started := Time.get_ticks_usec()
	for _i in reps:
		map.slab_cut_edge_is_in_frame(1.0)
	var cut_us := float(Time.get_ticks_usec() - started) / float(reps)
	started = Time.get_ticks_usec()
	for _i in reps:
		map.zoom_ceiling()
	var ceiling_us := float(Time.get_ticks_usec() - started) / float(reps)
	# The whole drive, with the key genuinely held, so the pan-wall search runs the
	# way it runs in play rather than being short-circuited by a zero axis.
	_press_pan_key(true)
	started = Time.get_ticks_usec()
	for _i in reps:
		map.drive_camera(0.016)
	var drive_us := float(Time.get_ticks_usec() - started) / float(reps)
	_press_pan_key(false)
	map.reset_camera()
	print("[perf] MICRO  slab_cut_edge_is_in_frame %.3f ms/call" % (cut_us / 1000.0))
	print("[perf] MICRO  zoom_ceiling              %.3f ms/call" % (ceiling_us / 1000.0))
	print("[perf] MICRO  drive_camera (D held)     %.3f ms/call" % (drive_us / 1000.0))
	print("[perf] MICRO  -> drive_camera is %.1f zoom_ceiling(s), each %.1f cut-edge test(s)" % [
		drive_us / maxf(ceiling_us, 0.0001), ceiling_us / maxf(cut_us, 0.0001)])
	# THE OVERLAY, TAKEN APART. `--probe 3` could already isolate the overlay as a
	# whole by hiding it; it could not see inside it, and "the overlay costs 5 ms"
	# is not an answer any more than "panning is expensive" was. The view times its
	# own sections now (`wotr_map_view.gd:overlay_paint_ms`), so this prints the
	# LAST repaint's breakdown - taken after the drive above, which forced ~200 of
	# them, so it is a repaint of a moving camera rather than of a still one.
	var sections: Dictionary = map.overlay_section_ms
	var keys: Array = sections.keys()
	keys.sort()
	var parts: Array[String] = []
	for key in keys:
		parts.append("%s %.3f" % [String(key), float(sections[key])])
	print("[perf] MICRO  overlay repaint          %.3f ms  [%s]" % [
		float(map.overlay_paint_ms), ", ".join(parts)])
	print("[perf] MICRO  overlay population      %d banner(s), %d standing marker(s), %d plot region(s), %d label(s)" % [
		int(map.banners_drawn), (map._standing_markers as Array).size(),
		(map.plot_regions() as PackedStringArray).size(), int(map.labels_drawn)])
	_press_pan_key(true)


func _sun() -> DirectionalLight3D:
	var viewport := _map_viewport()
	if viewport == null:
		return null
	return viewport.get_node_or_null("MapSun") as DirectionalLight3D


func _environment() -> Environment:
	var camera: Camera3D = null if _screen == null else _screen.map3d.camera
	if camera == null:
		return null
	if camera.environment != null:
		return camera.environment
	var viewport := _map_viewport()
	return null if viewport == null else viewport.world_3d.environment


func _press_pan_key(down: bool) -> void:
	var event := InputEventKey.new()
	event.keycode = KEY_D
	event.physical_keycode = KEY_D
	event.pressed = down
	Input.parse_input_event(event)


func _clear_samples() -> void:
	_wall_us.clear()
	_proc_ms.clear()
	_phys_ms.clear()
	_root_cpu.clear()
	_root_gpu.clear()
	_map_cpu.clear()
	_map_gpu.clear()
	_draw_calls.clear()
	_primitives.clear()


func _summarise(name: String) -> Dictionary:
	return {
		"state": name,
		"ms": _mean_us_as_ms(_wall_us),
		"p95": _percentile_us_as_ms(_wall_us, 0.95),
		"worst": _max_us_as_ms(_wall_us),
		"fps": 1000.0 / maxf(_mean_us_as_ms(_wall_us), 0.0001),
		"script": _mean(_proc_ms) + _mean(_phys_ms),
		"root_cpu": _mean(_root_cpu),
		"root_gpu": _mean(_root_gpu),
		"map_cpu": _mean(_map_cpu),
		"map_gpu": _mean(_map_gpu),
		"draws": _mean(_draw_calls),
		"prims": _mean(_primitives),
	}


func _report() -> void:
	print("")
	print("[perf] ============================================================")
	print("[perf] WAR OF THE RING FRAME TIME - window %s, vsync %s, %d frames/state" % [
		str(Vector2i(root.size)), "ON" if _vsync_on else "OFF", _frames])
	print("[perf] map SubViewport %s, video memory %.1f MiB, %d node(s)" % [
		str(_map_viewport_size()),
		Performance.get_monitor(Performance.RENDER_VIDEO_MEM_USED) / 1048576.0,
		int(Performance.get_monitor(Performance.OBJECT_NODE_COUNT))])
	print("[perf] %-20s %8s %8s %8s %7s %8s %8s %8s %8s %8s %9s" % [
		"state", "ms", "p95", "worst", "fps", "script", "rootCPU", "rootGPU", "mapCPU", "mapGPU", "draws"])
	for row in _rows:
		print("[perf] %-20s %8.2f %8.2f %8.2f %7.1f %8.2f %8.2f %8.2f %8.2f %8.2f %9.0f" % [
			String(row["state"]), row["ms"], row["p95"], row["worst"], row["fps"],
			row["script"], row["root_cpu"], row["root_gpu"], row["map_cpu"], row["map_gpu"],
			row["draws"]])
	print("[perf] ============================================================")
	# THE MACHINE IS SHARED. Anything sampled here competes with whatever else is
	# running on this box, so the run states that rather than presenting a mean as
	# if it were a clean-room number.
	print("[perf] NOTE: sampled on a shared machine; compare states within a run, not runs against each other.")


func _mean(values: Array[float]) -> float:
	if values.is_empty():
		return 0.0
	var total := 0.0
	for value in values:
		total += value
	return total / float(values.size())


func _mean_us_as_ms(values: Array[int]) -> float:
	if values.is_empty():
		return 0.0
	var total := 0
	for value in values:
		total += value
	return float(total) / float(values.size()) / 1000.0


func _max_us_as_ms(values: Array[int]) -> float:
	var worst := 0
	for value in values:
		worst = maxi(worst, value)
	return float(worst) / 1000.0


func _percentile_us_as_ms(values: Array[int], fraction: float) -> float:
	if values.is_empty():
		return 0.0
	var sorted := values.duplicate()
	sorted.sort()
	var index := clampi(int(floor(fraction * float(sorted.size()))), 0, sorted.size() - 1)
	return float(sorted[index]) / 1000.0


func _parse_size(text: String, fallback: Vector2i) -> Vector2i:
	var parts := text.to_lower().split("x", false)
	if parts.size() == 2 and parts[0].is_valid_int() and parts[1].is_valid_int():
		return Vector2i(int(parts[0]), int(parts[1]))
	return fallback


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

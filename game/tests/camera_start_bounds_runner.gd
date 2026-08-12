extends SceneTree
## Camera gate for the v0.2.2 owner report (lane kimi-bug-camera):
##
##   1. "The camera starts upside down." The match-start heading was the fixed
##      SAGE-default yaw on every seat, so seats whose start spot does not sit
##      south-west of the playable center began facing off-map (measured on
##      main: Fords of Isen II default seat looks 126 deg away from the map
##      center, Withered Heath seat 1 is 155 deg off, Rhun is ~90 deg off on
##      both seats). Retail behavior per the owner: the default heading faces
##      the map from the player's own start spot - the player's base at the
##      bottom of the screen - never inverted. gamedata.ini (rotwk oracle,
##      data/ini/gamedata.ini:8587-8594) authors the camera frame this heading
##      rides on: DefaultCameraMinHeight 120.0, DefaultCameraMaxHeight 300.0,
##      DefaultCameraPitchAngle 37.5, DefaultCameraYawAngle 0.0 (the DEFAULT,
##      which the per-seat start view re-aims).
##
##   2. "I can't quite move my camera to the edge of the map." The scroll
##      clamp stopped the look-at point a full screen-bottom ground-projection
##      inset short of the authored playable bounds. Retail clamps the look-at
##      point so the playable border is reachable (owner: the map border can
##      sit mid-screen). OpenSAGE TacticalView.CalcCameraConstraints and the
##      GPL Generals ZH W3DView::calcCameraConstraints agree the constraint is
##      a rect the look-at point is clamped into, not a view-frustum margin
##      pulled inside the playable border.
##
## Orientation checks boot the real slice on 3 maps x 2 start spots and assert
## zero roll, the retail pitch/height window, and that the initial heading
## faces the playable-bounds center. Bounds checks drive _clamp_camera_focus
## directly against cooked map data on a normal map (Fords of Isen II) and a
## castle map (wor-minas-tirith, which cannot boot a full match while castle
## gameplay is blocked-named-gaps, so its clamp is proven at map-data level
## with a detached slice - the clamp is pure map-data geometry).
##
## LIVENESS: 48 checks. Raise this when you add checks; never lower it.

const EXPECTED_CHECKS := 48

const WatchdogScript := preload("res://tests/runner_watchdog.gd")
## Loaded at runtime in _run, never preloaded: a --script main loop compiles
## its top-level preloads before the autoload globals (ModLoader, ContentDB)
## are registered, and a poisoned retail_map_data.gd compile then breaks the
## slice's own MapDataScript preload for the whole process.
var MapDataScript: GDScript = null

## Retail gamedata.ini camera contract (rotwk cache/effective-assets,
## data/ini/gamedata.ini:8590-8593).
const RETAIL_CAMERA_MIN_HEIGHT_SOURCE := 120.0
const RETAIL_CAMERA_MAX_HEIGHT_SOURCE := 300.0
const RETAIL_CAMERA_PITCH_DEGREES := 37.5

## [map_id, GameState.retail_player_start_index]. Seat 0 is the authored
## default (the human takes Player_2_Start); seat 1 picks Player_1_Start.
const ORIENTATION_CASES := [
	["bfme2.map.fords-of-isen-ii", 0],
	["bfme2.map.fords-of-isen-ii", 1],
	["bfme2.map.withered-heath", 0],
	["bfme2.map.withered-heath", 1],
	["rotwk.map.wor-rhun", 0],
	["rotwk.map.wor-rhun", 1],
]
const CLAMP_MAPS := ["bfme2.map.fords-of-isen-ii", "rotwk.map.wor-minas-tirith"]

const BOOT_DEADLINE_MS := 240000
## cos(~25 deg): the home heading is computed exactly, so the initial look
## direction must land well inside this of the direction to the map center.
const HEADING_MIN_DOT := 0.9

var _checks := 0
var _failures: Array[String] = []
var _watchdog = WatchdogScript.new()


func _initialize() -> void:
	_watchdog.start(self, "CAMERA_START_BOUNDS_RUNNER", 0, 0, true)
	if OS.get_environment("OPENBFME_SLICE_FACTION").strip_edges() == "":
		OS.set_environment("OPENBFME_SLICE_FACTION", "men")
	for env_name in ["OPENBFME_MP", "OPENBFME_STARTER_ARMY", "OPENBFME_CONTROL_PORT"]:
		OS.set_environment(env_name, "")
	call_deferred("_run")


func _run() -> void:
	root.size = Vector2i(1920, 1080)
	MapDataScript = load("res://src/retail_slice/retail_map_data.gd") as GDScript
	var slice_scene: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	if slice_scene == null or MapDataScript == null:
		_check("slice_scene_loads", false, "slice scene or map data script did not load")
		return _finish()
	var game_state = root.get_node_or_null("GameState")
	var content_db = root.get_node_or_null("ContentDB")
	if game_state == null or content_db == null:
		_check("autoloads_present", false, "GameState/ContentDB missing")
		return _finish()

	for case_value in ORIENTATION_CASES:
		var case: Array = case_value
		await _run_orientation_case(slice_scene, game_state, String(case[0]), int(case[1]))
	for clamp_map in CLAMP_MAPS:
		_run_clamp_case(slice_scene, content_db, clamp_map)
	_finish()


func _run_orientation_case(slice_scene: PackedScene, game_state, map_id: String, seat: int) -> void:
	var slug := map_id.get_slice(".", 2)
	var tag := "%s_seat%d" % [slug.replace("-", "_"), seat]
	OS.set_environment("OPENBFME_SLICE_MAP", map_id)
	game_state.set("retail_player_start_index", seat)
	var slice = slice_scene.instantiate()
	root.add_child(slice)
	var deadline := Time.get_ticks_msec() + BOOT_DEADLINE_MS
	while Time.get_ticks_msec() < deadline:
		await process_frame
		if bool(slice.ready_ok) or String(slice.failure_reason) != "":
			break
	if not _check("%s_boots" % tag, bool(slice.ready_ok), String(slice.failure_reason)):
		slice.queue_free()
		await process_frame
		return

	var basis: Basis = slice.camera.global_transform.basis
	var forward := -basis.z
	var flat := Vector2(forward.x, forward.z)
	var roll_degrees := rad_to_deg(asin(clampf(basis.x.dot(Vector3.UP), -1.0, 1.0)))
	_check("%s_zero_roll" % tag, absf(roll_degrees) < 0.5, "roll=%.4f deg" % roll_degrees)
	var pitch_degrees := rad_to_deg(atan2(-forward.y, flat.length()))
	_check("%s_retail_pitch" % tag, absf(pitch_degrees - RETAIL_CAMERA_PITCH_DEGREES) < 0.5,
		"pitch=%.4f deg (gamedata.ini DefaultCameraPitchAngle=%.1f)" % [pitch_degrees, RETAIL_CAMERA_PITCH_DEGREES])
	var camera_data := (slice.environment_runtime_metadata as Dictionary).get("camera", {}) as Dictionary
	var height_source := float(camera_data.get("current_height_source", -1.0))
	_check("%s_retail_height_range" % tag,
		height_source >= RETAIL_CAMERA_MIN_HEIGHT_SOURCE - 0.001 and height_source <= RETAIL_CAMERA_MAX_HEIGHT_SOURCE + 0.001,
		"height=%.4f source units (gamedata.ini range %.0f..%.0f)" % [height_source, RETAIL_CAMERA_MIN_HEIGHT_SOURCE, RETAIL_CAMERA_MAX_HEIGHT_SOURCE])

	var bounds: Rect2 = slice.source_map_data.local_bounds
	var to_center: Vector2 = bounds.get_center() - slice.camera_focus
	var dot := -2.0
	if to_center.length_squared() > 0.0001 and flat.length_squared() > 0.0001:
		dot = flat.normalized().dot(to_center.normalized())
	_check("%s_heading_faces_map_center" % tag, dot > HEADING_MIN_DOT,
		"dot(look, focus->playable-center)=%.4f (needs > %.2f: base must start at the bottom of the screen, never inverted)" % [dot, HEADING_MIN_DOT])

	slice.cleanup_for_test()
	slice.queue_free()
	await process_frame
	await process_frame


func _run_clamp_case(slice_scene: PackedScene, content_db, map_id: String) -> void:
	var slug := map_id.get_slice(".", 2).replace("-", "_")
	var definition: Dictionary = content_db.call("get_bundle_map", map_id) as Dictionary
	var pack_root := String(definition.get("_pack_root", ""))
	var map_data = MapDataScript.new()
	if pack_root == "" or not bool(map_data.load_from_pack(pack_root, definition)):
		for side in 9:
			_check("%s_clamp_%d" % [slug, side], false,
				"map data unavailable: %s" % String(map_data.error))
		return
	# Detached slice: the clamp is pure map-data geometry and needs no boot.
	# This is also what lets the castle map (wor-minas-tirith) be proven here
	# while castle gameplay itself is blocked-named-gaps.
	var slice = slice_scene.instantiate()
	slice.source_map_data = map_data
	var bounds: Rect2 = map_data.local_bounds
	var center := bounds.get_center()
	for zoom in [1.0, 0.0]:
		slice.camera_zoom = zoom
		slice.camera_zoom_target = zoom
		var ztag := "zoomed_out" if zoom == 1.0 else "zoomed_in"
		slice.camera_focus = Vector2(bounds.position.x - 100000.0, center.y)
		slice._clamp_camera_focus()
		_check("%s_%s_look_at_reaches_left_edge" % [slug, ztag],
			is_equal_approx(slice.camera_focus.x, bounds.position.x),
			"focus.x=%.5f bounds.left=%.5f" % [slice.camera_focus.x, bounds.position.x])
		slice.camera_focus = Vector2(bounds.end.x + 100000.0, center.y)
		slice._clamp_camera_focus()
		_check("%s_%s_look_at_reaches_right_edge" % [slug, ztag],
			is_equal_approx(slice.camera_focus.x, bounds.end.x),
			"focus.x=%.5f bounds.right=%.5f" % [slice.camera_focus.x, bounds.end.x])
		slice.camera_focus = Vector2(center.x, bounds.position.y - 100000.0)
		slice._clamp_camera_focus()
		_check("%s_%s_look_at_reaches_top_edge" % [slug, ztag],
			is_equal_approx(slice.camera_focus.y, bounds.position.y),
			"focus.y=%.5f bounds.top=%.5f" % [slice.camera_focus.y, bounds.position.y])
		slice.camera_focus = Vector2(center.x, bounds.end.y + 100000.0)
		slice._clamp_camera_focus()
		_check("%s_%s_look_at_reaches_bottom_edge" % [slug, ztag],
			is_equal_approx(slice.camera_focus.y, bounds.end.y),
			"focus.y=%.5f bounds.bottom=%.5f" % [slice.camera_focus.y, bounds.end.y])
	slice.camera_focus = center
	slice._clamp_camera_focus()
	_check("%s_interior_focus_untouched" % slug, slice.camera_focus.is_equal_approx(center),
		"focus=%s center=%s (clamp must not shrink the interior)" % [str(slice.camera_focus), str(center)])
	slice.free()


func _check(name: String, ok: bool, detail: String = "") -> bool:
	_checks += 1
	_watchdog.note(name)
	if ok:
		print("CAMERA_BOUNDS PASS %s" % name)
	else:
		printerr("CAMERA_BOUNDS FAIL %s | %s" % [name, detail])
		_failures.append(name)
	return ok


func _finish() -> void:
	print("CAMERA_START_BOUNDS_RESULT passed=%d failed=%d" % [_checks - _failures.size(), _failures.size()])
	if _checks != EXPECTED_CHECKS:
		printerr("LIVENESS FAILURE: %d checks ran, %d expected." % [_checks, EXPECTED_CHECKS])
		quit(1)
		return
	quit(0 if _failures.is_empty() else 1)

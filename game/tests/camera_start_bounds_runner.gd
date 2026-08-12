extends SceneTree
## Camera gate for the v0.2.2 owner report (lane kimi-bug-camera):
##
##   1. "The camera starts upside down." The match-start heading was the fixed
##      SAGE-default yaw on every seat, so seats whose start spot does not sit
##      south-west of the playable center began facing off-map (measured on
##      main: Fords of Isen II default seat looks 126 deg away from the map
##      center, Withered Heath seat 1 is 155 deg off, Rhun is ~90 deg off on
##      both seats).
##
##      Retail truth (map bytes + gamedata.ini, two independent probes): every
##      MP map uses a GLOBAL fixed DefaultCameraYawAngle = 0.0. Nothing in
##      retail aims per-seat. The per-seat aim-at-bounds-center heading is a
##      deliberate owner-feel deviation (base at the bottom of the screen, map
##      ahead, never inverted) — not retail's rule. Named consequence:
##      screen-north no longer matches minimap-north for some seats; retail
##      keeps them aligned.
##
##      The literal "upside down"/roll failure mode was never reproduced.
##      look_at_from_position(..., Vector3.UP) forces roll structurally to
##      zero, so the six *_zero_roll checks were green on main and are
##      invariant guards, not red-first evidence. What the heading change
##      actually fixed is "opens facing off-map".
##
##      gamedata.ini (rotwk oracle, data/ini/gamedata.ini:8587-8594) authors
##      the camera frame: DefaultCameraMinHeight 120.0, DefaultCameraMaxHeight
##      300.0, DefaultCameraPitchAngle 37.5, DefaultCameraYawAngle 0.0.
##
##   2. "I can't quite move my camera to the edge of the map." The scroll
##      clamp stopped the look-at point a full screen-bottom ground-projection
##      inset short of the authored playable border. Retail clamps the look-at
##      so the border is reachable (owner: the map border can sit mid-screen).
##      The playable region is a ROTATED quad (map_outline / SAGE playable
##      rect), not its local AABB. Clamping to the AABB lets the look-at
##      leave the authored border at the corners (Fords: ~47 local units /
##      ~1775 source units past the quad, off the heightmap). This gate
##      therefore targets playable-quad edge midpoints (reachable, on-mesh),
##      forbids AABB-corner reachability on Fords, and asserts a no-overshoot
##      invariant after every clamp.
##
## Orientation checks boot the real slice on 3 maps x 2 start spots and assert
## the retail pitch/height window, the owner-feel heading (faces playable
## center), and the roll invariant. Bounds checks drive _clamp_camera_focus
## against cooked map data on a normal map (Fords of Isen II) and a castle
## map (wor-minas-tirith, which cannot boot a full match while castle
## gameplay is blocked-named-gaps, so its clamp is proven at map-data level
## with a detached slice — the clamp is pure map-data geometry).
##
## LIVENESS: 58 checks. Raise this when you add checks; never lower it.

const EXPECTED_CHECKS := 58

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
const FORDS_MAP := "bfme2.map.fords-of-isen-ii"

const BOOT_DEADLINE_MS := 240000
## cos(~25 deg): the home heading is computed exactly, so the initial look
## direction must land well inside this of the direction to the map center.
const HEADING_MIN_DOT := 0.9
## Stated no-overshoot margin, local units. ~2 source units at Fords scale
## (~0.0265 local/source). The AABB regression overshot by 47 local units
## (~1775 source) at Fords corners; this is well inside the heightmap.
const NO_OVERSHOOT_MARGIN_LOCAL := 0.05

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
	# Invariant guard, not red-first evidence: roll is structurally zero via
	# look_at_from_position(..., Vector3.UP). Green on main before any heading
	# change. Kept so a future up-vector mistake cannot ship silently.
	var roll_degrees := rad_to_deg(asin(clampf(basis.x.dot(Vector3.UP), -1.0, 1.0)))
	_check("%s_zero_roll" % tag, absf(roll_degrees) < 0.5, "roll=%.4f deg (invariant guard)" % roll_degrees)
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
	# Owner-feel deviation, not retail: retail keeps yaw 0.0 on every seat.
	_check("%s_heading_faces_map_center" % tag, dot > HEADING_MIN_DOT,
		"dot(look, focus->playable-center)=%.4f (needs > %.2f: owner-feel, base at screen bottom)" % [dot, HEADING_MIN_DOT])

	slice.cleanup_for_test()
	slice.queue_free()
	await process_frame
	await process_frame


func _run_clamp_case(slice_scene: PackedScene, content_db, map_id: String) -> void:
	var slug := map_id.get_slice(".", 2).replace("-", "_")
	var definition: Dictionary = content_db.call("get_bundle_map", map_id) as Dictionary
	var pack_root := String(definition.get("_pack_root", ""))
	var map_data = MapDataScript.new()
	# Fords: 8 edge + 1 interior + 1 sweep + 8 AABB-corner = 18.
	# Castle map: 8 edge + 1 interior + 1 sweep = 10.
	var fail_budget := 18 if map_id == FORDS_MAP else 10
	if pack_root == "" or not bool(map_data.load_from_pack(pack_root, definition)):
		for side in fail_budget:
			_check("%s_clamp_%d" % [slug, side], false,
				"map data unavailable: %s" % String(map_data.error))
		return
	# Detached slice: the clamp is pure map-data geometry and needs no boot.
	# This is also what lets the castle map (wor-minas-tirith) be proven here
	# while castle gameplay itself is blocked-named-gaps.
	var slice = slice_scene.instantiate()
	slice.source_map_data = map_data
	var outline: PackedVector2Array = map_data.map_outline
	var bounds: Rect2 = map_data.local_bounds
	var center := bounds.get_center()
	if outline.size() < 4:
		for side in fail_budget:
			_check("%s_clamp_%d" % [slug, side], false, "map_outline has %d verts, need 4" % outline.size())
		slice.free()
		return

	for zoom in [1.0, 0.0]:
		slice.camera_zoom = zoom
		slice.camera_zoom_target = zoom
		var ztag := "zoomed_out" if zoom == 1.0 else "zoomed_in"
		for edge_i in 4:
			var a: Vector2 = outline[edge_i]
			var b: Vector2 = outline[(edge_i + 1) % outline.size()]
			var mid := (a + b) * 0.5
			var outward := _outward_normal(a, b, outline)
			# Modest outward push: 16 local units is well past the border
			# (~600 source) but stays inside float32 precision. A 1e5 push
			# drifted the along-edge source coordinate by ~0.01 local.
			slice.camera_focus = mid + outward * 16.0
			slice._clamp_camera_focus()
			var focus: Vector2 = slice.camera_focus
			var on_mesh := _on_heightmap(map_data, focus)
			_check("%s_%s_look_at_reaches_quad_edge_%d" % [slug, ztag, edge_i],
				focus.is_equal_approx(mid) and on_mesh,
				"focus=%s mid=%s dist=%.5f on_mesh=%s" % [
					str(focus), str(mid), focus.distance_to(mid), str(on_mesh)
				])
			_assert_no_overshoot(focus, outline, "%s_%s_edge_%d" % [slug, ztag, edge_i])

	slice.camera_focus = center
	slice._clamp_camera_focus()
	var interior_focus: Vector2 = slice.camera_focus
	_check("%s_interior_focus_untouched" % slug, interior_focus.is_equal_approx(center),
		"focus=%s center=%s (clamp must not shrink the interior)" % [str(interior_focus), str(center)])

	var overshoot_detail := _sweep_no_overshoot(slice, map_data, outline, bounds)
	_check("%s_no_overshoot_sweep" % slug, overshoot_detail == "", overshoot_detail)

	if map_id == FORDS_MAP:
		var aabb_corners: Array[Vector2] = [
			bounds.position,
			Vector2(bounds.end.x, bounds.position.y),
			bounds.end,
			Vector2(bounds.position.x, bounds.end.y),
		]
		for zoom in [1.0, 0.0]:
			slice.camera_zoom = zoom
			slice.camera_zoom_target = zoom
			var ztag := "zoomed_out" if zoom == 1.0 else "zoomed_in"
			for corner_i in aabb_corners.size():
				var corner: Vector2 = aabb_corners[corner_i]
				slice.camera_focus = corner
				slice._clamp_camera_focus()
				var clamped: Vector2 = slice.camera_focus
				var stayed := clamped.is_equal_approx(corner)
				var inside := _inside_playable_quad(clamped, outline, NO_OVERSHOOT_MARGIN_LOCAL)
				_check("%s_%s_aabb_corner_%d_not_reachable" % [slug, ztag, corner_i],
					not stayed and inside,
					"focus=%s aabb_corner=%s stayed=%s inside_quad=%s" % [
						str(clamped), str(corner), str(stayed), str(inside)
					])
	slice.free()


func _sweep_no_overshoot(slice, map_data, outline: PackedVector2Array, bounds: Rect2) -> String:
	var center := bounds.get_center()
	var targets: Array[Vector2] = []
	for deg in [0.0, 45.0, 90.0, 135.0, 180.0, 225.0, 270.0, 315.0]:
		var rad := deg_to_rad(deg)
		targets.append(center + Vector2(cos(rad), sin(rad)) * 100000.0)
	targets.append(bounds.position)
	targets.append(Vector2(bounds.end.x, bounds.position.y))
	targets.append(bounds.end)
	targets.append(Vector2(bounds.position.x, bounds.end.y))
	targets.append(Vector2(bounds.position.x, center.y))
	targets.append(Vector2(bounds.end.x, center.y))
	targets.append(Vector2(center.x, bounds.position.y))
	targets.append(Vector2(center.x, bounds.end.y))
	targets.append(center)
	var headings := [0.0, PI * 0.25, PI * 0.5, PI, PI * 1.5]
	var sample := 0
	for zoom in [0.0, 0.5, 1.0]:
		slice.camera_zoom = zoom
		slice.camera_zoom_target = zoom
		for heading in headings:
			slice.camera_user_yaw = heading
			slice.camera_home_yaw = heading * 0.5
			for target in targets:
				sample += 1
				slice.camera_focus = target
				slice._clamp_camera_focus()
				var swept: Vector2 = slice.camera_focus
				if not _inside_playable_quad(swept, outline, NO_OVERSHOOT_MARGIN_LOCAL):
					return "sample %d zoom=%.2f heading=%.3f target=%s focus=%s outside playable quad (margin=%.3f local, ~2 source units; AABB overshoot was ~47 local)" % [
						sample, zoom, heading, str(target), str(swept), NO_OVERSHOOT_MARGIN_LOCAL
					]
				if not _on_heightmap(map_data, swept):
					return "sample %d zoom=%.2f heading=%.3f focus=%s off heightmap" % [
						sample, zoom, heading, str(swept)
					]
	slice.camera_user_yaw = 0.0
	slice.camera_home_yaw = 0.0
	return ""


func _assert_no_overshoot(focus: Vector2, outline: PackedVector2Array, tag: String) -> void:
	# Soft diagnostic only — the named no-overshoot check is the contract.
	# Printed so a red reachability failure still shows whether the result
	# also left the quad.
	if not _inside_playable_quad(focus, outline, NO_OVERSHOOT_MARGIN_LOCAL):
		print("CAMERA_BOUNDS NOTE %s overshot playable quad focus=%s" % [tag, str(focus)])


func _outward_normal(a: Vector2, b: Vector2, outline: PackedVector2Array) -> Vector2:
	var edge := b - a
	if edge.length_squared() < 0.0000001:
		return Vector2.RIGHT
	var n := Vector2(edge.y, -edge.x).normalized()
	var mid := (a + b) * 0.5
	if Geometry2D.is_point_in_polygon(mid + n * 0.1, outline):
		n = -n
	return n


func _inside_playable_quad(focus: Vector2, outline: PackedVector2Array, margin: float) -> bool:
	if outline.size() < 3:
		return false
	if Geometry2D.is_point_in_polygon(focus, outline):
		return true
	var best := INF
	for i in outline.size():
		var a: Vector2 = outline[i]
		var b: Vector2 = outline[(i + 1) % outline.size()]
		var closest := Geometry2D.get_closest_point_to_segment(focus, a, b)
		best = minf(best, focus.distance_to(closest))
	return best <= margin


func _on_heightmap(map_data, focus: Vector2) -> bool:
	var grid: Vector2 = map_data.local_to_grid_float(focus)
	return grid.x >= 0.0 and grid.x <= float(map_data.width - 1) and grid.y >= 0.0 and grid.y <= float(map_data.height - 1)


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

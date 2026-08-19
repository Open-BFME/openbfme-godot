extends SceneTree
## Lane L9: live castle fixtures present as structures without turning every
## map prop into a HUD target. Default maps are Erebor and Carn Dum; profiling
## can override the comma-separated list through
## OPENBFME_CASTLE_PRESENTATION_MAPS (used for the required Minas Tirith run).

const Watchdog := preload("res://tests/runner_watchdog.gd")
const BOOT_DEADLINE_MS := 300000
const PROFILE_FRAMES := 300

var passed := 0
var failed := 0
var _watchdog := Watchdog.new()


func _initialize() -> void:
	_watchdog.start(self, "CASTLE_FIXTURE_PRESENTATION", 900000)
	call_deferred("_run")


func _check(name: String, condition: bool, detail: String = "") -> void:
	_watchdog.note(name)
	if condition:
		passed += 1
		print("CASTLE_FIXTURE_PRESENTATION PASS %s" % name)
	else:
		failed += 1
		printerr("CASTLE_FIXTURE_PRESENTATION FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _maps() -> Array[String]:
	var configured := OS.get_environment("OPENBFME_CASTLE_PRESENTATION_MAPS").strip_edges()
	if configured == "":
		return ["rotwk.map.wor-erebor", "rotwk.map.wor-ang-carn-dum"]
	var out: Array[String] = []
	for value in configured.split(",", false):
		var map_id := String(value).strip_edges()
		if map_id != "":
			out.append(map_id)
	return out


func _run() -> void:
	for map_id in _maps():
		await _exercise_map(map_id)
	print("CASTLE_FIXTURE_PRESENTATION_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)


func _exercise_map(map_id: String) -> void:
	OS.set_environment("OPENBFME_SLICE_FACTION", "men")
	OS.set_environment("OPENBFME_SLICE_MAP", map_id)
	var scene: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	var slice = scene.instantiate() if scene != null else null
	_check("%s_scene_loads" % map_id, slice != null)
	if slice == null:
		return
	root.add_child(slice)
	var deadline := Time.get_ticks_msec() + BOOT_DEADLINE_MS
	while Time.get_ticks_msec() < deadline:
		await process_frame
		if bool(slice.ready_ok) or String(slice.failure_reason) != "":
			break
	if not bool(slice.ready_ok):
		_check("%s_slice_ready" % map_id, false, String(slice.failure_reason))
		root.remove_child(slice)
		slice.free()
		await process_frame
		return
	_check("%s_slice_ready" % map_id, true)

	var fixture_ids: Array[int] = []
	for id_value in slice.simulation.structure_ids():
		var id := int(id_value)
		if String(slice.simulation.structure(id).get("structure_kind", "")) == "castle_fixture":
			fixture_ids.append(id)
	_check("%s_has_seeded_fixtures" % map_id, not fixture_ids.is_empty(), "count=%d" % fixture_ids.size())

	if map_id.ends_with("wor-erebor"):
		_check_erebor(slice, fixture_ids)
	if map_id.ends_with("wor-ang-carn-dum"):
		_check_carn_dum(slice, fixture_ids)

	var marker_count := -1
	if slice.hud != null and slice.hud.minimap != null and slice.hud.minimap.has_method("castle_fixture_marker_count"):
		marker_count = int(slice.hud.minimap.castle_fixture_marker_count())
	var expected_markers := 0
	for id in fixture_ids:
		var row: Dictionary = slice.simulation.structure(id)
		if int(row.get("health", 0)) > 0 and String(row.get("castle_fixture_role", "")) in ["wall", "gate", "tower", "garrison"]:
			expected_markers += 1
	_check(
		"%s_minimap_fixture_markers_match_drawn_rows" % map_id,
		marker_count == expected_markers,
		"markers=%d expected=%d" % [marker_count, expected_markers]
	)

	var stable_node := _first_fixture_prop(slice, fixture_ids)
	var visibility_samples: Array[bool] = []
	if stable_node != null:
		for _frame in 30:
			visibility_samples.append(bool(stable_node.visible))
			await process_frame
	# Some cooked maps deliberately have unresolved castle bindings.  The
	# cross-map contract still samples Erebor's bound wall for all 30 frames;
	# an unbound placement has no presentation node whose visibility could
	# flicker.
	var constant := stable_node == null or not visibility_samples.is_empty()
	for value in visibility_samples:
		if value != visibility_samples[0]:
			constant = false
	_check(
		"%s_fixture_visibility_constant_for_30_unchanged_frames" % map_id,
		constant,
		"unbound" if stable_node == null else str(visibility_samples)
	)

	# Exact requested frame metric: wall-clock elapsed around 300 process_frame
	# awaits after the map is fully ready. Printed even on a red-first run.
	var profile_started := Time.get_ticks_usec()
	for _frame in PROFILE_FRAMES:
		await process_frame
	var profile_elapsed_us := Time.get_ticks_usec() - profile_started
	print("CASTLE_FIXTURE_PRESENTATION_PROFILE map=%s frames=%d total_ms=%.3f ms_per_process_frame=%.6f" % [
		map_id, PROFILE_FRAMES, float(profile_elapsed_us) / 1000.0,
		float(profile_elapsed_us) / 1000.0 / float(PROFILE_FRAMES),
	])

	root.remove_child(slice)
	slice.free()
	await process_frame


func _check_erebor(slice, fixture_ids: Array[int]) -> void:
	var gate_id := _fixture_id(slice, fixture_ids, "EreborGateDoors")
	_check("erebor_gate_fixture_found", gate_id != 0)
	var gate_selectable := false
	if gate_id != 0 and slice.has_method("castle_fixture_selectable"):
		gate_selectable = bool(slice.castle_fixture_selectable(slice.simulation.structure(gate_id)))
	_check("erebor_objectTargetable_false_gate_is_not_selectable", gate_id != 0 and not gate_selectable)
	_check(
		"erebor_objectTargetable_false_gate_has_no_pick_candidate",
		gate_id != 0 and slice._structure_pick_candidates([gate_id]).is_empty()
	)

	# EreborWall02 is destructible, bound to a real prop node, and retail authors
	# a RUBBLE state. The selected pack has no distinct rubble GLB for it, so the
	# required event consumer must hide the prop rather than leave it standing.
	var wall_id := _fixture_id(slice, fixture_ids, "EreborWall02")
	var wall: Dictionary = slice.simulation.structure(wall_id)
	var wall_node := _fixture_prop(slice, int(wall.get("source_index", -1)))
	_check("erebor_destructible_wall_prop_found", wall_id != 0 and wall_node != null and not bool(wall.get("indestructible", true)))
	if wall_id != 0 and wall_node != null:
		var old_visual := wall_node.get_child(0) if wall_node.get_child_count() > 0 else null
		slice.simulation._apply_damage(-1, wall_id, int(wall.get("maximum_health", 1)) + 1, "structure")
		slice._sync_presentation()
		var swapped := wall_node.get_child_count() > 0 and wall_node.get_child(0) != old_visual
		var hidden := not bool(wall_node.visible)
		_check("erebor_wall_death_event_hides_or_swaps_prop", hidden or swapped,
			"visible=%s swapped=%s health=%s" % [str(wall_node.visible), str(swapped), str(slice.simulation.structure(wall_id).get("health"))])


func _check_carn_dum(slice, fixture_ids: Array[int]) -> void:
	var wall_id := _fixture_id(slice, fixture_ids, "AngmarWallCarnDum")
	var selectable := false
	if wall_id != 0 and slice.has_method("castle_fixture_selectable"):
		selectable = bool(slice.castle_fixture_selectable(slice.simulation.structure(wall_id)))
	_check("carndum_selectable_wall_segment_is_selectable", wall_id != 0 and selectable,
		str(slice.simulation.structure(wall_id) if wall_id != 0 else {}))
	var wall_pick_candidates: Array = slice._structure_pick_candidates([wall_id]) if wall_id != 0 else []
	_check(
		"carndum_selectable_wall_has_pick_candidate",
		wall_pick_candidates.size() == 1 and int((wall_pick_candidates[0] as Dictionary).get("id", 0)) == wall_id,
		str(wall_pick_candidates)
	)
	var state: Dictionary = {}
	if wall_id != 0 and slice.battlefield.has_method("castle_fixture_presentation_state"):
		state = slice.battlefield.castle_fixture_presentation_state(wall_id)
	_check("carndum_wall_health_and_hover_follow_selectability",
		bool(state.get("selectable", false)) and bool(state.get("health_bar", false)) and String(state.get("tooltip", "")) != "", str(state))
	_check("carndum_wall_honors_fog_and_mp_cull_kindof",
		bool(state.get("dont_hide_if_fogged", false)) and bool(state.get("never_cull_for_mp", false)), str(state))


func _fixture_id(slice, ids: Array[int], type_name: String) -> int:
	for id in ids:
		if String(slice.simulation.structure(id).get("castle_fixture_type", "")) == type_name:
			return id
	return 0


func _fixture_prop(slice, source_index: int) -> Node3D:
	if slice.battlefield == null or slice.battlefield.retail_prop_container == null:
		return null
	for child in slice.battlefield.retail_prop_container.get_children():
		if int(child.get_meta("source_index", -1)) == source_index:
			return child as Node3D
	return null


func _first_fixture_prop(slice, ids: Array[int]) -> Node3D:
	for id in ids:
		var node := _fixture_prop(slice, int(slice.simulation.structure(id).get("source_index", -1)))
		if node != null:
			return node
	return null

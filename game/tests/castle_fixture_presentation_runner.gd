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
	var census_bound := 0
	var census_selectable_and_bound := 0
	for id in fixture_ids:
		var census_row: Dictionary = slice.simulation.structure(id)
		if _fixture_prop(slice, int(census_row.get("source_index", -1))) == null:
			continue
		census_bound += 1
		if slice.castle_fixture_selectable(census_row):
			census_selectable_and_bound += 1
	var census_marker_count := int(slice.hud.minimap.castle_fixture_marker_count()) if slice.hud != null and slice.hud.minimap != null else -1
	print("CASTLE_FIXTURE_PRESENTATION_CENSUS map=%s seeded=%d bound=%d selectable_and_bound=%d minimap=%d" % [
		map_id, fixture_ids.size(), census_bound, census_selectable_and_bound, census_marker_count,
	])

	if map_id.ends_with("wor-erebor"):
		await _check_erebor(slice, fixture_ids)
	if map_id.ends_with("wor-ang-carn-dum"):
		_check_carn_dum(slice, fixture_ids)

	var marker_count := -1
	if slice.hud != null and slice.hud.minimap != null and slice.hud.minimap.has_method("castle_fixture_marker_count"):
		marker_count = int(slice.hud.minimap.castle_fixture_marker_count())
	var expected_markers := 0
	for id in fixture_ids:
		var row: Dictionary = slice.simulation.structure(id)
		if int(row.get("health", 0)) <= 0 or _fixture_prop(slice, int(row.get("source_index", -1))) == null:
			continue
		var kind_of := _upper_tokens(row.get("castle_fixture_kind_of", []))
		if not kind_of.has("INERT") and not kind_of.has("UNATTACKABLE"):
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
	_check("erebor_selectable_kindof_gate_is_selectable", gate_id != 0 and gate_selectable)
	var gate_candidates: Array = slice._structure_pick_candidates([gate_id]) if gate_id != 0 else []
	_check("erebor_selectable_kindof_gate_has_pick_candidate",
		gate_candidates.size() == 1 and int((gate_candidates[0] as Dictionary).get("id", 0)) == gate_id,
		str(gate_candidates))
	if gate_id != 0:
		var gate_position := Vector2(slice.simulation.structure(gate_id).get("position", Vector2.ZERO))
		slice._closest_selectable_castle_fixture(gate_position)
		var cached_count: int = slice._castle_fixture_pick_candidates.size()
		slice._closest_selectable_castle_fixture(gate_position)
		_check("erebor_hover_pick_candidates_are_cached",
			not slice._castle_fixture_pick_cache_dirty and cached_count > 0
			and slice._castle_fixture_pick_candidates.size() == cached_count)

	var wall_id := _fixture_id(slice, fixture_ids, "EreborWall02")
	_check("erebor_non_selectable_kindof_wall_is_not_selectable",
		wall_id != 0 and not slice.castle_fixture_selectable(slice.simulation.structure(wall_id)))

	# EBGarrisonableTower's retail RUBBLE model is EB_tower3_D3 and the cooked
	# lifecycle binding is rubble-eb-tower3-d3.glb. Reveal one placement first so
	# the test proves a visible intact prop becomes visible rubble, then reapply
	# shroud after a deferred-free frame to reproduce the rejected implementation's
	# freed-object crash.
	var tower_id := _fixture_id(slice, fixture_ids, "EBGarrisonableTower")
	var tower: Dictionary = slice.simulation.structure(tower_id)
	var tower_node := _fixture_prop(slice, int(tower.get("source_index", -1)))
	_check("erebor_rubble_tower_prop_found", tower_id != 0 and tower_node != null and not bool(tower.get("indestructible", true)))
	if tower_id != 0 and tower_node != null:
		var fog = slice.simulation.fog_of_war()
		fog.reveal(slice.local_team, Vector2(tower.get("position", Vector2.ZERO)), 40.0, true, "l9-rubble-test")
		slice.shroud_overlay.update(true)
		var hidden_before := int(slice.shroud_overlay.apply_to_scenery())
		var intact_visual := tower_node.get_child(0) as Node3D if tower_node.get_child_count() > 0 else null
		_check("erebor_rubble_tower_visible_before_kill", intact_visual != null and intact_visual.is_visible_in_tree())
		slice.simulation._apply_damage(-1, tower_id, int(tower.get("maximum_health", 1)) + 1, "structure")
		slice._sync_presentation()
		_check("erebor_structure_event_invalidates_hover_pick_cache", slice._castle_fixture_pick_cache_dirty)
		await process_frame
		var hidden_after := int(slice.shroud_overlay.apply_to_scenery())
		_check("erebor_shroud_hidden_count_survives_rubble_swap", hidden_after == hidden_before,
			"before=%d after=%d" % [hidden_before, hidden_after])
		var state: Dictionary = slice.battlefield.castle_fixture_presentation_state(tower_id)
		_check("erebor_rubble_uses_retail_model_binding",
			String(state.get("rubble_model", "")).to_lower() == "eb_tower3_d3"
			and String(state.get("rubble_path", "")).get_file().to_lower() == "rubble-eb-tower3-d3.glb",
			str(state))
		var rubble := _fixture_rubble_visual(tower_node)
		_check("erebor_rubble_visual_is_visible_after_kill", rubble != null and rubble.is_visible_in_tree(), str(state))


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


func _upper_tokens(value: Variant) -> Array[String]:
	var out: Array[String] = []
	for token in value as Array:
		out.append(String(token).to_upper())
	return out


func _fixture_rubble_visual(node: Node) -> Node3D:
	for descendant in node.find_children("*", "Node3D", true, false):
		if bool(descendant.get_meta("castle_fixture_rubble_visual", false)):
			return descendant as Node3D
	return null

extends SceneTree

const Watchdog = preload("res://tests/runner_watchdog.gd")
const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")

const ROTWK_NEUTRAL_DIGEST := "8e192543df7d4352b663ce68dfd34bc2fddce69928f0cb0bd79b283a9082a26b"
const FORDS_LAIR_INDICES := [42, 43, 249, 250, 499, 500]

var passed := 0
var failed := 0
var watchdog = Watchdog.new()


func _initialize() -> void:
	watchdog.start(self, "SELECTED_NEUTRAL_FORDS_VISUAL_RUNNER")
	OS.set_environment("OPENBFME_STARTER_ARMY", "1")
	OS.set_environment("OPENBFME_SCENARIO_MAP_PLACEMENTS", "1")
	call_deferred("_run")


func _run() -> void:
	root.size = Vector2i(1920, 1080)
	var packed: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	_check(packed != null, "scene_parses")
	if packed == null:
		_finish()
		return
	var slice = packed.instantiate()
	root.add_child(slice)
	await process_frame
	await process_frame
	_check(bool(slice.ready_ok), "selected_slice_ready", String(slice.failure_reason))
	if not bool(slice.ready_ok):
		slice.queue_free()
		_finish()
		return

	var content_db = root.get_node_or_null("ContentDB")
	var neutral_root := String(content_db.get_neutral_pack_receipt("rotwk").get("_pack_root", ""))
	_check(neutral_root.ends_with(ROTWK_NEUTRAL_DIGEST), "exact_rotwk_neutral_pack")
	var wolf_id := -1
	for id in slice.simulation.entity_ids():
		var wolf_row := slice.simulation.entity(id) as Dictionary
		if String(wolf_row.get("scenario_source_object_id", "")) == "Wolf":
			wolf_id = id
			break
	_check(wolf_id > 0, "civilian_wolf_scenario_entity_present")
	if wolf_id > 0:
		var wolf_row := slice.simulation.entity(wolf_id) as Dictionary
		var wolf_visual = slice.scenario_unit_nodes.get(wolf_id)
		_check(int(wolf_row.get("team", -1)) == SimScript.CASTLE_CIVILIAN_TEAM, "wolf_visual_retains_civilian_owner")
		_check(
			wolf_visual is Node3D
			and wolf_visual.get_script() != null
			and String(wolf_visual.get_script().resource_path) == "res://src/retail_slice/retail_scenario_visual.gd"
			and String(wolf_visual.object_id) == "Wolf"
			and String(wolf_visual.pack_root) == neutral_root
			and int(wolf_visual.mesh_instance_count) > 0,
			"wolf_uses_exact_selected_scenario_visual",
		)
		# Hostility must not bypass descriptor-backed readiness: the scenario
		# identity branch precedes the legacy CREEP provisional exemption.
		var original_team := int(wolf_row.get("team", -1))
		wolf_row["team"] = SimScript.CREEP_TEAM
		slice.scenario_unit_nodes.erase(wolf_id)
		_check(not slice._all_battalion_retail_visuals_loaded(), "hostile_scenario_unit_without_visual_fails_readiness")
		slice.scenario_unit_nodes[wolf_id] = wolf_visual
		_check(slice._all_battalion_retail_visuals_loaded(), "hostile_scenario_unit_with_exact_visual_passes_readiness")
		wolf_row["team"] = original_team
	var observed_indices: Array[int] = []
	var cave_lair_id := -1
	for id_value in slice.structure_nodes.keys():
		var id := int(id_value)
		var row := slice.simulation.structure(id) as Dictionary
		if not row.has("scenario_lifecycle_receipt"):
			continue
		var node = slice.structure_nodes[id]
		var source_index := int(row.get("scenario_source_index", -1))
		observed_indices.append(source_index)
		_check(String(node.get_meta("pack_root", "")) == neutral_root, "structure_pack_%d" % id)
		_check(int(node.get_meta("source_index", -2)) == source_index, "structure_source_index_%d" % id)
		_check(is_equal_approx(float(node.get_meta("source_yaw", NAN)), float(row.get("yaw", INF))), "structure_yaw_receipt_%d" % id)
		_check(is_equal_approx(float(node.rotation.y), float(row.get("yaw", INF))), "structure_yaw_transform_%d" % id)
		_check(String(node.contract_error) == "" and bool(node.retail_visual_loaded), "structure_visual_loaded_%d" % id, String(node.contract_error))
		_check(String(node.current_lifecycle_phase) == "intact", "structure_intact_%d" % id)
		_check(String(node.active_body_path).begins_with("assets/models/structures/"), "structure_exact_glb_%d" % id, String(node.active_body_path))
		var legacy = slice.battlefield.bound_structure_node_by_source_index(source_index)
		_check(legacy != null and not bool(legacy.visible) and int(legacy.get_meta("replaced_by_selected_neutral_scenario", -1)) == id, "legacy_map_binding_replaced_%d" % id)
		if String(row.get("source_object_id", "")) == "CaveTrollLair":
			cave_lair_id = id
	observed_indices.sort()
	_check(observed_indices == FORDS_LAIR_INDICES, "exact_fords_structure_indices", str(observed_indices))
	_check(cave_lair_id > 0, "cave_lair_present")

	# The same selected lifecycle owns every authored damage body.
	if cave_lair_id > 0:
		var cave_node = slice.structure_nodes[cave_lair_id]
		var cave_row := (slice.simulation.structure(cave_lair_id) as Dictionary).duplicate(true)
		cave_row.health = 1000
		cave_node.sync_state(cave_row)
		_check(String(cave_node.current_lifecycle_phase) == "damaged" and String(cave_node.active_body_path).contains("/damaged-"), "selected_damage_phase")
		cave_row.health = 500
		cave_node.sync_state(cave_row)
		_check(String(cave_node.current_lifecycle_phase) == "really-damaged" and String(cave_node.active_body_path).contains("/really-damaged-"), "selected_really_damage_phase")
		cave_row.health = int(cave_row.maximum_health)
		cave_node.sync_state(cave_row)

	# Initial SpawnBehavior children receive their own converted selected-pack
	# bodies and authored idle clips; they are no longer skipped as creep art.
	slice.simulation.advance(1)
	slice._sync_presentation()
	var scenario_children := 0
	for id_value in slice.scenario_unit_nodes.keys():
		var id := int(id_value)
		var visual = slice.scenario_unit_nodes[id]
		var row := slice.simulation.entity(id) as Dictionary
		if not row.has("spawn_behavior_parent_id"):
			continue
		scenario_children += 1
		_check(String(visual.pack_root) == neutral_root, "unit_pack_%d" % id)
		_check(visual.mesh_instance_count > 0 and not visual.converted_paths.is_empty(), "unit_exact_glb_%d" % id)
		_check(String(visual.presentation_binding) == "authored-animation", "unit_authored_idle_%d" % id, String(visual.presentation_binding))
		_check(String(visual.active_animation_identifier) != "" and String(visual.active_animation_name) != "", "unit_clip_resolved_%d" % id)
	_check(scenario_children == 10, "exact_fords_spawn_visuals", str(scenario_children))
	_check(slice.scenario_visual_provisionals.is_empty(), "no_selected_presentation_provisionals", str(slice.scenario_visual_provisionals))

	# Passive props are instantiated from neutralProp.* rather than map-pack
	# guesses and retain exact source transform receipts.
	var prop_id: int = int(slice.simulation.spawn_scenario_prop("SpiderWebs01", Vector2(7, 9), "map-placement"))
	var prop_row := slice.simulation.scenario_props[prop_id] as Dictionary
	prop_row.yaw = 0.625
	prop_row.scenario_source_index = 888
	prop_row.scenario_source_position = Vector3(70, 80, 90)
	slice._sync_presentation()
	var prop_visual = slice.scenario_prop_nodes.get(prop_id)
	_check(prop_visual != null, "selected_prop_instantiated")
	if prop_visual != null:
		_check(String(prop_visual.pack_root) == neutral_root, "selected_prop_pack")
		_check(prop_visual.converted_paths == ["assets/models/neutral-props/spiderwebs01/intact-pmspiderwebs01.glb"], "selected_prop_exact_glb", str(prop_visual.converted_paths))
		_check(int(prop_visual.get_meta("source_index", -1)) == 888 and is_equal_approx(float(prop_visual.rotation.y), 0.625), "selected_prop_source_transform")
		_check(String(prop_visual.presentation_binding) == "static-converted-model", "selected_prop_static_receipt")

	# A source-authored static core presentation stays static and records why;
	# no unrelated animation is fabricated to make the gate green.
	var dragon_doc: Dictionary = content_db.get_scenario_unit_runtime("rotwk", "CINE_GrnDrgn_Flying", "script-spawn")
	var scenario_visual_script = load("res://src/retail_slice/retail_scenario_visual.gd")
	var dragon = scenario_visual_script.new()
	_check(not dragon_doc.is_empty() and dragon.configure(dragon_doc, {"position": Vector2.ZERO, "state": "idle"}, slice.source_map_data.local_transform_scale, "unit"), "static_core_visual_instantiates", String(dragon.contract_error))
	if dragon.contract_error == "":
		_check(String(dragon.presentation_binding) == "static-model" and String(dragon.active_animation_identifier) == "", "static_core_receipt_no_fabricated_clip")
	dragon.free()
	var balrog_doc: Dictionary = content_db.get_scenario_unit_runtime("rotwk", "MordorBalrog", "script-spawn")
	var balrog = scenario_visual_script.new()
	_check(not balrog_doc.is_empty() and balrog.configure(balrog_doc, {"position": Vector2.ZERO, "state": "attack"}, slice.source_map_data.local_transform_scale, "unit"), "multi_component_visual_instantiates", String(balrog.contract_error))
	if balrog.contract_error == "":
		_check(balrog.converted_paths.size() >= 2 and balrog.mesh_instance_count >= 2, "multi_component_visual_keeps_exact_parts", str(balrog.converted_paths))
		_check(String(balrog.presentation_binding) == "authored-animation" and String(balrog.active_animation_identifier) != "", "multi_component_authored_attack")
	balrog.free()

	# Rebuild-hole exposure creates a separate selected structure visual. Once
	# the authoritative rebuild consumes it, presentation removes that exact node.
	if cave_lair_id > 0:
		var owner := slice.simulation.structures[cave_lair_id] as Dictionary
		var hole_id: int = int(slice.simulation._expose_rebuild_hole(cave_lair_id, owner, 0))
		_check(hole_id > 0, "rebuild_hole_exposed")
		slice._sync_presentation()
		var hole_node = slice.structure_nodes.get(hole_id)
		_check(
			hole_node != null and String(hole_node.active_body_path).contains("cavetrolllairhole/intact-"),
			"rebuild_hole_exact_visible_glb",
			"missing" if hole_node == null else "%s :: %s" % [String(hole_node.active_body_path), String(hole_node.contract_error)]
		)
		if hole_id > 0:
			var hole := slice.simulation.structures[hole_id] as Dictionary
			var rebuild := hole.get("rebuild_hole_behavior", {}) as Dictionary
			rebuild.respawn_tick = slice.simulation.tick_index
			hole.rebuild_hole_behavior = rebuild
			slice.simulation._step_rebuild_holes()
			slice._sync_presentation()
			_check(not slice.simulation.structures.has(hole_id) and not slice.structure_nodes.has(hole_id), "rebuilt_hole_visual_removed")

	slice.cleanup_for_test()
	slice.queue_free()
	await process_frame
	_finish()


func _check(condition: bool, label: String, detail: String = "") -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("%s%s" % [label, " :: " + detail if detail != "" else ""])


func _finish() -> void:
	watchdog.stop()
	if failed == 0:
		print("SELECTED_NEUTRAL_FORDS_VISUAL_OK passed=%d" % passed)
		quit(0)
	else:
		print("SELECTED_NEUTRAL_FORDS_VISUAL_FAIL passed=%d failed=%d" % [passed, failed])
		quit(1)

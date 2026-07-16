extends SceneTree
## Legal-safe focused gate for the private Men authored building lifecycle.

const STRUCTURE_SOURCE := "res://src/retail_slice/retail_structure.gd"
const FACTORY_SOURCE := "res://src/view/asset_factory.gd"
const SIM_SOURCE := "res://src/retail_slice/retail_slice_sim.gd"
const KINDS: Array[String] = ["fortress", "farm", "barracks", "archery_range", "stable"]
const EXPECTED_MAXIMUMS := {
	"fortress": 7500,
	"farm": 2000,
	"barracks": 3000,
	"archery_range": 3000,
	"stable": 3000,
}

var passed := 0
var failed := 0
var finished := false
var structure_script
var sim_script


func _initialize() -> void:
	create_timer(30.0, true, false, true).timeout.connect(_watchdog_timeout)
	call_deferred("_run")


func _run() -> void:
	structure_script = load(STRUCTURE_SOURCE)
	sim_script = load(SIM_SOURCE)
	_check("runner_dependencies", structure_script != null and sim_script != null)
	if structure_script == null or sim_script == null:
		_finish()
		return
	_test_simulation_maximums()
	for kind in KINDS:
		_test_kind_lifecycle(kind)
	_test_bib_during_construction_opt_in()
	_test_fail_closed_contracts()
	_test_no_legacy_presentation_shortcuts()
	await process_frame
	await process_frame
	_finish()


func _test_simulation_maximums() -> void:
	_check("sim_exact_maximum_table", sim_script.STRUCTURE_MAX_HEALTH == EXPECTED_MAXIMUMS)
	var simulation = sim_script.new()
	simulation.setup({}, {"enable_base_loop": true, "spawn_initial_battalions": false})
	var seen: Dictionary = {}
	for structure_id in simulation.structure_ids():
		var row: Dictionary = simulation.structure(structure_id)
		var kind := String(row.get("structure_kind", ""))
		var expected := int(EXPECTED_MAXIMUMS.get(kind, -1))
		seen[kind] = int(seen.get(kind, 0)) + 1
		_check(
			"sim_%s_%d_uses_exact_maximum" % [kind, structure_id],
			int(row.get("maximum_health", -1)) == expected and int(row.get("health", -1)) == expected
		)
	var two_of_each := true
	for kind in KINDS:
		two_of_each = two_of_each and int(seen.get(kind, 0)) == 2
	_check("sim_has_two_exact_bases", two_of_each and simulation.structure_ids().size() == 10)


func _test_kind_lifecycle(kind: String) -> void:
	var lifecycle := _fixture_lifecycle(kind)
	var fixtures := _fixture_visuals(lifecycle, kind)
	var maximum := int(EXPECTED_MAXIMUMS[kind])
	var entity := _entity(kind, maximum, maximum, 1.0)
	var structure = structure_script.new()
	root.add_child(structure)
	structure.configure_fixture(entity, lifecycle, fixtures, 8000.0)
	_check("%s_contract_loads" % kind, structure.contract_error == "" and structure.retail_visual_loaded)
	_check("%s_starts_intact" % kind, structure.current_lifecycle_phase == "intact" and structure.active_body_path == lifecycle.paths.intact)
	_check("%s_intact_bib_visible" % kind, structure.active_bib_path == lifecycle.paths.bib)
	_check("%s_shared_scale_comes_from_intact" % kind, is_equal_approx(float(structure.shared_uniform_scale), 2.0))
	if kind == "fortress":
		_check("fortress_intact_has_explicit_no_clip", structure.active_animation_clip == "" and structure.active_animation_mode == "none")
	else:
		_check("%s_intact_idle_uses_declared_clip" % kind, structure.active_animation_clip == "%s_idle" % kind and structure.active_animation_mode == "loop")
	if kind == "fortress":
		_check("fortress_intact_door_is_closed", structure.active_door_path == lifecycle.components.door.closed.path)
	else:
		_check("%s_has_no_door" % kind, structure.active_door_path == "")

	entity = _entity(kind, maximum, 0, 0.5)
	structure.sync_state(entity)
	_check("%s_construction_precedes_zero_health" % kind, structure.current_lifecycle_phase == "construction")
	_check("%s_construction_uses_authored_body" % kind, structure.active_body_path == lifecycle.paths.construction)
	_check("%s_construction_bib_hidden_by_default" % kind, structure.active_bib_path == "")
	_check("%s_construction_clip_is_manual" % kind, structure.active_animation_clip == "%s_build" % kind and structure.active_animation_mode == "manual-progress")
	var construction_player := _first_animation_player(_visible_model_host_child(structure))
	_check(
		"%s_construction_progress_scrubs_clip" % kind,
		construction_player != null and is_equal_approx(construction_player.current_animation_position, 1.0)
	)
	var host: Node3D = structure.get_node("StructureVisual/SharedLifecycleTransform") as Node3D
	var scale_before := host.scale
	entity["construction_progress"] = 0.75
	structure.sync_state(entity)
	construction_player = _first_animation_player(_visible_model_host_child(structure))
	_check(
		"%s_construction_rescrubs_without_y_scale" % kind,
		construction_player != null
		and is_equal_approx(construction_player.current_animation_position, 1.5)
		and host.scale.is_equal_approx(scale_before)
		and is_equal_approx(host.scale.x, host.scale.y)
		and is_equal_approx(host.scale.y, host.scale.z)
	)
	if kind == "fortress":
		_check("fortress_construction_door_is_authored", structure.active_door_path == lifecycle.components.door.construction.path)

	entity = _entity(kind, maximum, int(lifecycle.damagedHealth) + 1, 1.0)
	structure.sync_state(entity)
	_check("%s_above_damaged_boundary_is_intact" % kind, structure.current_lifecycle_phase == "intact")
	entity["health"] = int(lifecycle.damagedHealth)
	structure.sync_state(entity)
	_check("%s_damaged_boundary_is_inclusive" % kind, structure.current_lifecycle_phase == "damaged" and structure.active_body_path == lifecycle.paths.damaged)
	entity["health"] = int(lifecycle.reallyDamagedHealth) + 1
	structure.sync_state(entity)
	_check("%s_above_really_damaged_boundary_stays_damaged" % kind, structure.current_lifecycle_phase == "damaged")
	entity["health"] = int(lifecycle.reallyDamagedHealth)
	structure.sync_state(entity)
	_check("%s_really_damaged_boundary_is_inclusive" % kind, structure.current_lifecycle_phase == "reallyDamaged" and structure.active_body_path == lifecycle.paths.reallyDamaged)
	_check("%s_really_damaged_plays_declared_once_clip" % kind, structure.active_animation_clip == "%s_d2" % kind and structure.active_animation_mode == "once")
	entity["health"] = 0
	structure.sync_state(entity)
	var state: Dictionary = structure.lifecycle_state()
	_check("%s_zero_health_selects_rubble" % kind, structure.current_lifecycle_phase == "rubble" and structure.active_body_path == lifecycle.paths.rubble)
	_check("%s_rubble_is_retained" % kind, bool(state.get("retainedRubble", false)) and structure.get_node("StructureVisual").visible)
	_check("%s_rubble_hides_bib" % kind, structure.active_bib_path == "")
	_check("%s_rubble_plays_declared_once_clip" % kind, structure.active_animation_clip == "%s_d3" % kind and structure.active_animation_mode == "once")
	if kind == "fortress":
		_check("fortress_rubble_door_is_authored", structure.active_door_path == lifecycle.components.door.rubble.path)
	var all_local_scales_are_one := true
	for child_value in host.get_children():
		if child_value is Node3D:
			all_local_scales_are_one = all_local_scales_are_one and (child_value as Node3D).scale.is_equal_approx(Vector3.ONE)
	_check("%s_all_phases_use_one_shared_transform" % kind, all_local_scales_are_one and host.scale.is_equal_approx(Vector3.ONE * 2.0))
	structure.free()
	_free_fixture_models(fixtures)


func _test_bib_during_construction_opt_in() -> void:
	var kind := "farm"
	var lifecycle := _fixture_lifecycle(kind)
	lifecycle["bibDuringConstruction"] = true
	var fixtures := _fixture_visuals(lifecycle, kind)
	var maximum := int(EXPECTED_MAXIMUMS[kind])
	var structure = structure_script.new()
	root.add_child(structure)
	structure.configure_fixture(_entity(kind, maximum, maximum, 0.25), lifecycle, fixtures, 8000.0)
	_check("bib_during_construction_requires_explicit_opt_in", structure.contract_error == "" and structure.active_bib_path == lifecycle.paths.bib)
	structure.free()
	_free_fixture_models(fixtures)


func _test_fail_closed_contracts() -> void:
	var kind := "barracks"
	var maximum := int(EXPECTED_MAXIMUMS[kind])
	var lifecycle := _fixture_lifecycle(kind)
	var fixtures := _fixture_visuals(lifecycle, kind)

	var malformed := lifecycle.duplicate(true)
	malformed["schema"] = "wrong.schema"
	var malformed_structure = structure_script.new()
	root.add_child(malformed_structure)
	malformed_structure.configure_fixture(_entity(kind, maximum, maximum, 1.0), malformed, fixtures)
	_check("malformed_schema_fails_closed", malformed_structure.contract_error.contains("schema") and not malformed_structure.retail_visual_loaded and not malformed_structure.get_node("StructureVisual").visible)
	malformed_structure.free()

	var missing_fixtures := fixtures.duplicate()
	missing_fixtures.erase(String(lifecycle.paths.rubble))
	var missing_structure = structure_script.new()
	root.add_child(missing_structure)
	missing_structure.configure_fixture(_entity(kind, maximum, maximum, 1.0), lifecycle, missing_fixtures)
	_check("missing_required_asset_fails_closed", missing_structure.contract_error.contains("paths.rubble") and not missing_structure.retail_visual_loaded)
	missing_structure.free()

	var invalid_fixtures := fixtures.duplicate()
	var invalid_fixture := Node.new()
	invalid_fixtures[String(lifecycle.paths.damaged)] = invalid_fixture
	var invalid_asset_structure = structure_script.new()
	root.add_child(invalid_asset_structure)
	invalid_asset_structure.configure_fixture(_entity(kind, maximum, maximum, 1.0), lifecycle, invalid_fixtures)
	_check("malformed_required_asset_fails_closed", invalid_asset_structure.contract_error.contains("paths.damaged") and not invalid_asset_structure.retail_visual_loaded)
	invalid_asset_structure.free()

	var missing_clip_fixtures := _fixture_visuals(lifecycle, kind)
	var old_intact_fixture: Node = missing_clip_fixtures[String(lifecycle.paths.intact)] as Node
	old_intact_fixture.free()
	missing_clip_fixtures[String(lifecycle.paths.intact)] = _fixture_model(4.0)
	var missing_clip_structure = structure_script.new()
	root.add_child(missing_clip_structure)
	missing_clip_structure.configure_fixture(_entity(kind, maximum, maximum, 1.0), lifecycle, missing_clip_fixtures)
	_check("declared_animation_missing_from_asset_fails_closed", missing_clip_structure.contract_error.contains("declared animation clips") and not missing_clip_structure.retail_visual_loaded)
	missing_clip_structure.free()
	_free_fixture_models(missing_clip_fixtures)

	var maximum_mismatch_structure = structure_script.new()
	root.add_child(maximum_mismatch_structure)
	maximum_mismatch_structure.configure_fixture(_entity(kind, maximum + 1, maximum + 1, 1.0), lifecycle, fixtures)
	_check("entity_maximum_mismatch_fails_closed", maximum_mismatch_structure.contract_error.contains("maximum_health") and not maximum_mismatch_structure.retail_visual_loaded)
	maximum_mismatch_structure.free()

	var fortress := _fixture_lifecycle("fortress")
	fortress["components"]["door"]["rubble"]["path"] = null
	var nullable_door_error: String = structure_script.validate_lifecycle_contract(
		fortress,
		"fortress",
		String(fortress.paths.intact),
		int(EXPECTED_MAXIMUMS.fortress)
	)
	_check("nullable_blocked_fortress_door_is_not_loadable_completion", nullable_door_error.contains("door.rubble"))

	var model_mismatch_error: String = structure_script.validate_lifecycle_contract(
		lifecycle,
		kind,
		"assets/fixtures/not-the-intact-model.glb",
		maximum
	)
	_check("presentation_model_must_be_true_intact", model_mismatch_error.contains("not the lifecycle intact"))
	invalid_fixture.free()
	_free_fixture_models(fixtures)


func _test_no_legacy_presentation_shortcuts() -> void:
	var structure_source := FileAccess.get_file_as_string(STRUCTURE_SOURCE)
	var factory_source := FileAccess.get_file_as_string(FACTORY_SOURCE)
	_check("legacy_y_scale_construction_is_removed", not structure_source.contains("_visual_root.scale.y") and not structure_source.contains("lerpf(0.12"))
	_check("procedural_structure_fallback_is_removed", not structure_source.contains("_build_procedural_structure") and not structure_source.contains("legal-safe-masonry-kit"))
	_check("explicit_model_helper_is_fail_closed", factory_source.contains("make_explicit_model_visual") and factory_source.contains("preflight_explicit_model_path") and not structure_source.contains("make_bundle_object_visual"))


func _fixture_lifecycle(kind: String) -> Dictionary:
	var maximum := int(EXPECTED_MAXIMUMS[kind])
	var prefix := "assets/fixtures/men-%s" % kind.replace("_", "-")
	var lifecycle := {
		"schema": "openbfme.building-lifecycle-presentation",
		"schemaVersion": 0,
		"maxHealth": maximum,
		"damagedHealth": int(floor(float(maximum) * 0.66)),
		"reallyDamagedHealth": int(floor(float(maximum) * 0.33)),
		"paths": {
			"construction": "%s/construction.glb" % prefix,
			"intact": "%s/intact.glb" % prefix,
			"damaged": "%s/damaged.glb" % prefix,
			"reallyDamaged": "%s/really-damaged.glb" % prefix,
			"rubble": "%s/rubble.glb" % prefix,
			"bib": "%s/bib.glb" % prefix,
		},
		"bibDuringConstruction": false,
		"clips": {
			"construction": {"names": ["%s_build" % kind], "mode": "manual-progress"},
			"intact": {"names": ["%s_idle" % kind], "mode": "loop"},
			"reallyDamaged": {"names": ["%s_d2" % kind], "mode": "once"},
			"rubble": {"names": ["%s_d3" % kind], "mode": "once"},
		},
		"unresolved": [{"code": "fixture-honest-gap", "reason": "legal-safe test metadata"}],
	}
	if kind == "fortress":
		lifecycle["components"] = {
			"door": {
				"construction": {"path": "%s/door-construction.glb" % prefix},
				"closed": {"path": "%s/door-closed.glb" % prefix},
				"rubble": {"path": "%s/door-rubble.glb" % prefix},
			}
		}
		lifecycle["clips"]["intact"] = {"names": [], "mode": "none"}
	return lifecycle


func _fixture_visuals(lifecycle: Dictionary, kind: String) -> Dictionary:
	var result: Dictionary = {}
	var paths: Dictionary = lifecycle.paths
	result[String(paths.construction)] = _fixture_model(11.0, "%s_build" % kind)
	result[String(paths.intact)] = _fixture_model(4.0, "" if kind == "fortress" else "%s_idle" % kind)
	result[String(paths.damaged)] = _fixture_model(9.0)
	result[String(paths.reallyDamaged)] = _fixture_model(2.0, "%s_d2" % kind)
	result[String(paths.rubble)] = _fixture_model(1.0, "%s_d3" % kind)
	result[String(paths.bib)] = _fixture_model(0.2)
	if kind == "fortress":
		var door: Dictionary = lifecycle.components.door
		result[String(door.construction.path)] = _fixture_model(7.0)
		result[String(door.closed.path)] = _fixture_model(6.0)
		result[String(door.rubble.path)] = _fixture_model(1.0)
	return result


func _fixture_model(height: float, animation_name: String = "") -> Node3D:
	var node := Node3D.new()
	var mesh_instance := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = Vector3(3.0, height, 3.0)
	mesh_instance.mesh = mesh
	mesh_instance.position.y = height * 0.5
	node.add_child(mesh_instance)
	if animation_name != "":
		var player := AnimationPlayer.new()
		var library := AnimationLibrary.new()
		var animation := Animation.new()
		animation.length = 2.0
		library.add_animation(animation_name, animation)
		player.add_animation_library("", library)
		node.add_child(player)
	return node


func _entity(kind: String, maximum: int, health: int, progress: float) -> Dictionary:
	return {
		"id": 100,
		"team": 0,
		"kind": "structure",
		"structure_kind": kind,
		"health": health,
		"maximum_health": maximum,
		"construction_progress": progress,
	}


func _visible_model_host_child(structure) -> Node3D:
	var host: Node3D = structure.get_node("StructureVisual/SharedLifecycleTransform") as Node3D
	for child_value in host.get_children():
		if child_value is Node3D and (child_value as Node3D).visible:
			return child_value as Node3D
	return null


func _first_animation_player(node: Node3D) -> AnimationPlayer:
	if node == null:
		return null
	var pending: Array[Node] = [node]
	while not pending.is_empty():
		var current: Node = pending.pop_back()
		if current is AnimationPlayer:
			return current as AnimationPlayer
		for child_value in current.get_children():
			if child_value is Node:
				pending.append(child_value as Node)
	return null


func _free_fixture_models(fixtures: Dictionary) -> void:
	var freed: Dictionary = {}
	for value in fixtures.values():
		if value is Node:
			var node := value as Node
			var instance_id := node.get_instance_id()
			if not freed.has(instance_id) and is_instance_valid(node):
				freed[instance_id] = true
				node.free()


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_STRUCTURE_LIFECYCLE PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_STRUCTURE_LIFECYCLE FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	if finished:
		return
	finished = true
	print("RETAIL_STRUCTURE_LIFECYCLE_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _watchdog_timeout() -> void:
	if finished:
		return
	failed += 1
	printerr("RETAIL_STRUCTURE_LIFECYCLE FAIL watchdog_timeout")
	_finish()

extends SceneTree
## Legal-safe focused runtime gate for neutral lifecycle routing and schema v1.

const STRUCTURE_IDS := {
	"CaveTrollLair": "bfme2.object.neutral-cave-troll-lair",
	"Inn": "bfme2.object.neutral-inn",
	"WargLair": "bfme2.object.neutral-warg-lair",
}
const PLACEMENT_COUNTS := {"CaveTrollLair": 2, "Inn": 2, "WargLair": 4}
const SOURCE_MODELS := {
	"CaveTrollLair": "art/w3d/nb/nbtrolllair.w3d",
	"Inn": "art/w3d/nb/nbinn_skn.w3d",
	"WargLair": "art/w3d/nb/nbwarglair.w3d",
}

var passed := 0
var failed := 0
var map_data_script
var battlefield_script
var structure_script
var content_db
var mod_loader
var fixture_pack_root := ""
var fixture_glb_relative := "assets/models/buildings/farm.glb"
var fixture_glb_path := ""
var fixture_pack_root_added := false
var routed_map_data


func _initialize() -> void:
	create_timer(30.0, true, false, true).timeout.connect(_watchdog_timeout)
	call_deferred("_run")


func _run() -> void:
	content_db = root.get_node_or_null("ContentDB")
	mod_loader = root.get_node_or_null("ModLoader")
	map_data_script = load("res://src/retail_slice/retail_map_data.gd")
	battlefield_script = load("res://src/retail_slice/retail_fords_battlefield.gd")
	structure_script = load("res://src/retail_slice/retail_structure.gd")
	_check(
		"dependencies_compile",
		content_db != null
		and mod_loader != null
		and map_data_script != null
		and battlefield_script != null
		and structure_script != null
	)
	if failed > 0:
		_finish()
		return
	_find_legal_fixture_pack()
	_check("legal_fixture_glb_is_available", fixture_pack_root != "" and fixture_glb_path != "")
	if fixture_pack_root == "":
		_finish()
		return
	_test_map_binding_routing()
	_test_lifecycle_v1_presenter()
	_test_battlefield_structure_instantiation()
	var asset_factory = load("res://src/view/asset_factory.gd")
	asset_factory.clear_mesh_cache()
	_cleanup_legal_fixture_pack()
	await process_frame
	await process_frame
	_finish()


func _find_legal_fixture_pack() -> void:
	fixture_pack_root = ProjectSettings.globalize_path("user://openbfme-neutral-lifecycle-fixture")
	fixture_glb_path = fixture_pack_root.path_join(fixture_glb_relative)
	var directory_error := DirAccess.make_dir_recursive_absolute(fixture_glb_path.get_base_dir())
	if directory_error != OK or not _write_triangle_glb(fixture_glb_path):
		fixture_pack_root = ""
		fixture_glb_path = ""
		return
	if not content_db.pack_roots.has(fixture_pack_root):
		content_db.pack_roots.append(fixture_pack_root)
		fixture_pack_root_added = true


func _write_triangle_glb(path: String) -> bool:
	var binary := PackedByteArray()
	binary.resize(44)
	var positions := [
		Vector3(-1.0, 0.0, 0.0),
		Vector3(1.0, 0.0, 0.0),
		Vector3(0.0, 2.0, 0.0),
	]
	var offset := 0
	for position in positions:
		binary.encode_float(offset, position.x)
		binary.encode_float(offset + 4, position.y)
		binary.encode_float(offset + 8, position.z)
		offset += 12
	binary.encode_u16(36, 0)
	binary.encode_u16(38, 1)
	binary.encode_u16(40, 2)
	var document := {
		"asset": {"version": "2.0", "generator": "OpenBFME legal-safe neutral lifecycle test"},
		"scene": 0,
		"scenes": [{"nodes": [0]}],
		"nodes": [{"mesh": 0}],
		"meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1}]}],
		"buffers": [{"byteLength": binary.size()}],
		"bufferViews": [
			{"buffer": 0, "byteOffset": 0, "byteLength": 36, "target": 34962},
			{"buffer": 0, "byteOffset": 36, "byteLength": 6, "target": 34963},
		],
		"accessors": [
			{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [-1.0, 0.0, 0.0], "max": [1.0, 2.0, 0.0]},
			{"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"},
		],
	}
	var json_bytes := JSON.stringify(document).to_utf8_buffer()
	while json_bytes.size() % 4 != 0:
		json_bytes.append(32)
	var total_length := 12 + 8 + json_bytes.size() + 8 + binary.size()
	var header := PackedByteArray()
	header.resize(12)
	header.encode_u32(0, 0x46546C67)
	header.encode_u32(4, 2)
	header.encode_u32(8, total_length)
	var json_header := PackedByteArray()
	json_header.resize(8)
	json_header.encode_u32(0, json_bytes.size())
	json_header.encode_u32(4, 0x4E4F534A)
	var binary_header := PackedByteArray()
	binary_header.resize(8)
	binary_header.encode_u32(0, binary.size())
	binary_header.encode_u32(4, 0x004E4942)
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return false
	file.store_buffer(header)
	file.store_buffer(json_header)
	file.store_buffer(json_bytes)
	file.store_buffer(binary_header)
	file.store_buffer(binary)
	file.close()
	return FileAccess.file_exists(path)


func _cleanup_legal_fixture_pack() -> void:
	if fixture_pack_root_added:
		content_db.pack_roots.erase(fixture_pack_root)
	DirAccess.remove_absolute(fixture_glb_path)
	DirAccess.remove_absolute(fixture_glb_path.get_base_dir())
	DirAccess.remove_absolute(fixture_pack_root.path_join("assets/models"))
	DirAccess.remove_absolute(fixture_pack_root.path_join("assets"))
	DirAccess.remove_absolute(fixture_pack_root)


func _test_map_binding_routing() -> void:
	var map_data = map_data_script.new()
	map_data.pack_root = fixture_pack_root
	var bindings := _binding_document()
	var loaded := bool(map_data._load_object_bindings(bindings, PLACEMENT_COUNTS))
	_check("structure_binding_table_loads", loaded, String(map_data.error))
	_check(
		"structure_types_are_separate_from_props",
		map_data.bound_structure_type_ids == ["CaveTrollLair", "Inn", "WargLair"]
		and map_data.bound_prop_type_ids.is_empty()
		and int(map_data.bound_structure_placement_count) == 8
		and int(map_data.bound_prop_placement_count) == 0
	)
	var placements := _normalized_placements()
	_check("structure_placements_route", bool(map_data._route_normalized_object_placements(placements)), String(map_data.error))
	_check(
		"exact_2_2_4_placements_are_lifecycle_structures",
		map_data.bound_structure_placements.size() == 8
		and _count_type(map_data.bound_structure_placements, "CaveTrollLair") == 2
		and _count_type(map_data.bound_structure_placements, "Inn") == 2
		and _count_type(map_data.bound_structure_placements, "WargLair") == 4
	)
	_check(
		"lifecycle_structures_never_enter_prop_or_marker_paths",
		map_data.bound_prop_placements.is_empty()
		and map_data.generic_prop_placements.is_empty()
		and _placements_have_explicit_structure_contract(map_data.bound_structure_placements)
	)
	routed_map_data = map_data

	var unsafe_document := bindings.duplicate(true)
	(unsafe_document.records[0] as Dictionary)["objectId"] = "bfme2.object../escaped"
	var unsafe_probe = map_data_script.new()
	unsafe_probe.pack_root = fixture_pack_root
	_check(
		"unsafe_structure_object_id_fails_closed",
		not bool(unsafe_probe._load_object_bindings(unsafe_document, PLACEMENT_COUNTS))
		and String(unsafe_probe.error).contains("unsafe or incomplete")
	)
	var missing_source_document := bindings.duplicate(true)
	(missing_source_document.records[0] as Dictionary).erase("sourceVirtualModel")
	var missing_source_probe = map_data_script.new()
	missing_source_probe.pack_root = fixture_pack_root
	_check(
		"missing_source_model_fails_closed",
		not bool(missing_source_probe._load_object_bindings(missing_source_document, PLACEMENT_COUNTS))
	)
	var disguised_prop_document := bindings.duplicate(true)
	(disguised_prop_document.records[0] as Dictionary)["classification"] = "renderable"
	var disguised_probe = map_data_script.new()
	disguised_probe.pack_root = fixture_pack_root
	_check(
		"renderable_prop_cannot_claim_structure_fields",
		not bool(disguised_probe._load_object_bindings(disguised_prop_document, PLACEMENT_COUNTS))
		and String(disguised_probe.error).contains("lifecycle structure fields")
	)


func _test_lifecycle_v1_presenter() -> void:
	var object_id := String(STRUCTURE_IDS.CaveTrollLair)
	var lifecycle := _fixture_lifecycle(object_id, "assets/fixtures/neutral-cave", 2000, true)
	var fixtures := _fixture_visuals(lifecycle)
	var structure = structure_script.new()
	root.add_child(structure)
	structure.configure_fixture(_entity(2000), lifecycle, fixtures, 8000.0)
	_check("v1_contract_loads", structure.contract_error == "" and structure.retail_visual_loaded, String(structure.contract_error))
	_check(
		"v1_starts_from_declared_intact_phase",
		String(structure.current_lifecycle_phase) == "intact"
		and String(structure.active_visual_mode) == "glb"
		and String(structure.active_body_path).ends_with("/intact.glb")
	)
	_check("v1_does_not_derive_phase_from_health", String(structure.current_lifecycle_phase) == "intact")

	_check("authoritative_damaged_phase_applies", bool(structure.set_authoritative_lifecycle_phase("damaged")))
	_check(
		"damaged_routes_exact_fx_and_audio_ids",
		String(structure.active_entering_fx) == "FX_BuildingDamaged"
		and String(structure.active_audio_event) == "BuildingLightDamageStone"
		and String(structure.declared_next_phase()) == "really-damaged"
	)
	_check("authoritative_really_damaged_phase_applies", bool(structure.set_authoritative_lifecycle_phase("really-damaged")))
	_check(
		"really_damaged_plays_declared_once_clip",
		String(structure.active_animation_clip) == "neutral_d2"
		and String(structure.active_animation_mode) == "once"
		and String(structure.active_entering_fx) == "FX_BuildingReallyDamaged"
	)
	_check("proven_cave_death_order_allows_entering_collapse", bool(structure.request_declared_lifecycle_transition()))
	_check(
		"collapse_is_simulation_driven_and_routes_sink",
		String(structure.active_animation_clip) == "neutral_d3"
		and String(structure.active_entering_fx) == "FX_StructureMediumCollapse"
		and String(structure.active_audio_event) == "BuildingSink"
		and String(structure.declared_next_phase()) == "rubble"
	)
	_check(
		"death_immediately_spawns_exact_rebuild_hole_state",
		bool(structure.rebuild_hole_present)
		and String(structure.rebuild_hole_phase) == "visible-rubble"
		and String(structure.rebuild_hole_body_path).ends_with("/rebuild-hole.glb")
		and int(structure.rebuild_hole_maximum_health) == 500
		and is_zero_approx(float(structure.rebuild_hole_health_regen_percent_per_second))
		and is_equal_approx(float(structure.rebuild_hole_fade_in_seconds), 2.0)
		and String(structure.rebuild_hole_terminal_duration) == "unbounded-until-rebuild-or-explicit-destruction"
		and structure.rebuild_hole_original_removal_frame == null
		and String(structure.rebuild_hole_original_removal_frame_status) == "blocked-on-bfme2-runtime-oracle"
	)
	_check(
		"automatic_collapse_completion_fails_closed_on_unproven_timing",
		not bool(structure.request_declared_lifecycle_transition())
		and String(structure.current_lifecycle_phase) == "collapsing"
		and String(structure.transition_error).contains("collapse completion timing")
	)
	_check("authoritative_rubble_phase_applies", bool(structure.set_authoritative_lifecycle_phase("rubble")))
	var rubble_state: Dictionary = structure.lifecycle_state()
	_check(
		"authored_rubble_no_render_has_no_fake_body",
		String(structure.active_visual_mode) == "no-render"
		and String(structure.active_body_path) == ""
		and String(structure.retail_mesh_path) == ""
		and not bool(rubble_state.get("retainedRubble", true))
		and bool(rubble_state.get("rebuildHolePresent", false))
	)
	_check(
		"automatic_original_removal_fails_closed_on_unknown_frame",
		not bool(structure.request_declared_lifecycle_transition())
		and String(structure.current_lifecycle_phase) == "rubble"
		and String(structure.transition_error).contains("original-object removal timing")
	)
	_check("authoritative_post_rubble_phase_applies", bool(structure.set_authoritative_lifecycle_phase("post-rubble")))
	_check(
		"post_rubble_keeps_no_render_and_exact_particle_route",
		String(structure.active_visual_mode) == "no-render"
		and structure.active_particle_system_ids == ["SmokeBuildingMediumRubble"]
		and structure.declared_next_phase() == null
	)
	_check("authoritative_rebuild_completion_can_remove_persistent_hole", bool(structure.set_authoritative_rebuild_hole_present(false)) and not bool(structure.rebuild_hole_present))
	_check("authoritative_hole_restore_uses_same_exact_state", bool(structure.set_authoritative_rebuild_hole_present(true)) and bool(structure.rebuild_hole_present))
	_check("authoritative_explicit_destruction_can_remove_hole", bool(structure.set_authoritative_rebuild_hole_present(false)) and not bool(structure.rebuild_hole_present))
	_check("authoritative_post_collapse_phase_applies", bool(structure.set_authoritative_lifecycle_phase("post-collapse")))
	_check(
		"post_collapse_is_an_explicit_terminal_no_render_phase",
		String(structure.active_visual_mode) == "no-render"
		and structure.active_particle_system_ids == ["SmokeBuildingMediumRubble"]
		and structure.declared_next_phase() == null
	)
	_check("construction_phase_is_authoritative", bool(structure.set_authoritative_lifecycle_phase("construction", 0.25)))
	var construction_player := _first_animation_player(_visible_body(structure))
	_check(
		"construction_progress_scrubs_declared_clip",
		String(structure.active_animation_clip) == "neutral_build"
		and String(structure.active_animation_mode) == "manual-progress"
		and construction_player != null
		and is_equal_approx(construction_player.current_animation_position, 0.5)
	)
	structure.free()
	_free_fixture_visuals(fixtures)

	var blocked := lifecycle.duplicate(true)
	blocked.simulationFacts.maximumHealth = null
	var blocked_error: String = structure_script.validate_lifecycle_contract(blocked, "CaveTrollLair", String((lifecycle.phases[1] as Dictionary).visual.glb), 2000, object_id)
	_check("unresolved_health_facts_fail_closed", blocked_error.contains("exact integer") or blocked_error.contains("blocked"), blocked_error)
	var pending_effects := lifecycle.duplicate(true)
	pending_effects.effects.definitionTranslationStatus = "exact-particle-plan-cross-selection-pending"
	var effects_error: String = structure_script.validate_lifecycle_contract(pending_effects, "CaveTrollLair", String((lifecycle.phases[1] as Dictionary).visual.glb), 2000, object_id)
	_check("pending_particle_cross_selection_does_not_block_intact_structure", effects_error == "", effects_error)
	var missing_object_id := lifecycle.duplicate(true)
	missing_object_id.erase("objectId")
	var object_id_error: String = structure_script.validate_lifecycle_contract(missing_object_id, "CaveTrollLair", String((lifecycle.phases[1] as Dictionary).visual.glb), 2000, object_id)
	_check("v1_object_id_is_required", object_id_error.contains("objectId"), object_id_error)
	var wrong_next := lifecycle.duplicate(true)
	(wrong_next.phases[4] as Dictionary)["nextPhase"] = "post-rubble"
	var next_error: String = structure_script.validate_lifecycle_contract(wrong_next, "CaveTrollLair", String((lifecycle.phases[1] as Dictionary).visual.glb), 2000, object_id)
	_check("wrong_phase_graph_fails_closed", next_error.contains("wrong explicit next phase"), next_error)

	var inn_lifecycle := _fixture_lifecycle(String(STRUCTURE_IDS.Inn), "assets/fixtures/neutral-inn", 3000, true)
	var inn_fixtures := _fixture_visuals(inn_lifecycle)
	var inn_structure = structure_script.new()
	root.add_child(inn_structure)
	inn_structure.configure_fixture(
		{
			"id": 2,
			"team": 0,
			"structure_kind": "Inn",
			"maximum_health": 3000,
			"health": 3000,
			"construction_progress": 1.0,
		},
		inn_lifecycle,
		inn_fixtures,
		8000.0
	)
	_check(
		"inn_intact_instantiates_despite_unresolved_death_timing",
		String(inn_structure.contract_error) == ""
		and String(inn_structure.current_lifecycle_phase) == "intact"
		and not bool(inn_structure.rebuild_hole_present)
	)
	_check("inn_authoritative_really_damaged_phase_applies", bool(inn_structure.set_authoritative_lifecycle_phase("really-damaged")))
	_check(
		"inn_automatic_death_transition_fails_on_unproven_reachability",
		not bool(inn_structure.request_declared_lifecycle_transition())
		and String(inn_structure.current_lifecycle_phase) == "really-damaged"
		and String(inn_structure.transition_error).contains("reachability/order")
	)
	_check("inn_oracle_authoritative_collapsing_phase_can_present", bool(inn_structure.set_authoritative_lifecycle_phase("collapsing")))
	_check(
		"inn_automatic_collapse_completion_remains_blocked",
		not bool(inn_structure.request_declared_lifecycle_transition())
		and String(inn_structure.current_lifecycle_phase) == "collapsing"
		and String(inn_structure.transition_error).contains("collapse completion timing")
	)
	_check("inn_oracle_authoritative_rubble_phase_can_present", bool(inn_structure.set_authoritative_lifecycle_phase("rubble")))
	_check(
		"inn_uses_authored_visible_rubble_without_rebuild_hole",
		String(inn_structure.active_visual_mode) == "glb"
		and String(inn_structure.active_body_path).ends_with("/rubble.glb")
		and not bool(inn_structure.rebuild_hole_present)
	)
	_check(
		"inn_automatic_post_rubble_transition_remains_blocked",
		not bool(inn_structure.request_declared_lifecycle_transition())
		and String(inn_structure.current_lifecycle_phase) == "rubble"
		and String(inn_structure.transition_error).contains("post-rubble transition timing")
	)
	inn_structure.free()
	_free_fixture_visuals(inn_fixtures)


func _test_battlefield_structure_instantiation() -> void:
	if routed_map_data == null:
		_check("battlefield_routing_prerequisite", false)
		return
	var previous: Dictionary = {}
	for type_name in STRUCTURE_IDS:
		var object_id := String(STRUCTURE_IDS[type_name])
		previous[object_id] = {
			"present": content_db.bundle_objects.has(object_id),
			"value": content_db.bundle_objects.get(object_id, {}).duplicate(true),
		}
		var maximum := 3000 if type_name == "Inn" else 2000
		content_db.bundle_objects[object_id] = {
			"id": object_id,
			"kind": "structure",
			"_pack_root": fixture_pack_root,
			"presentation": {
				"model": fixture_glb_relative,
				"heightMillimeters": 7000,
				"buildingLifecycle": _fixture_lifecycle(object_id, fixture_glb_relative.get_base_dir(), maximum, false),
			},
		}
	var battlefield = battlefield_script.new()
	root.add_child(battlefield)
	var built := bool(battlefield._build_bound_retail_structures(routed_map_data))
	_check("battlefield_builds_lifecycle_structure_channel", built, String(battlefield.error))
	_check(
		"battlefield_instantiates_exactly_eight_structures",
		int(battlefield.bound_retail_structure_count) == 8
		and battlefield.retail_structure_container != null
		and battlefield.retail_structure_container.get_child_count() == 8
	)
	_check(
		"battlefield_preserves_structure_type_scoreboard",
		battlefield.bound_retail_structure_type_ids == ["CaveTrollLair", "Inn", "WargLair"]
	)
	_check(
		"battlefield_structures_preserve_source_transform_and_identity",
		_scene_structures_match(battlefield.retail_structure_container, routed_map_data.bound_structure_placements)
	)
	_check(
		"battlefield_structures_start_intact_at_full_health",
		_all_structures_start_intact(battlefield.retail_structure_container)
	)
	_check(
		"battlefield_does_not_duplicate_structures_as_props",
		int(battlefield.bound_retail_prop_count) == 0
		and battlefield.retail_prop_container == null
		and routed_map_data.bound_prop_placements.is_empty()
	)
	battlefield.free()
	for object_id_value in previous:
		var object_id := String(object_id_value)
		var row: Dictionary = previous[object_id]
		if bool(row.present):
			content_db.bundle_objects[object_id] = row.value
		else:
			content_db.bundle_objects.erase(object_id)


func _binding_document() -> Dictionary:
	var records: Array[Dictionary] = []
	for type_name in ["CaveTrollLair", "Inn", "WargLair"]:
		records.append({
			"typeName": type_name,
			"status": "bound",
			"classification": "lifecycle-structure",
			"matchMethod": "exact-type-name",
			"placementCount": int(PLACEMENT_COUNTS[type_name]),
			"sourceVirtualModel": String(SOURCE_MODELS[type_name]),
			"glb": fixture_glb_relative,
			"objectId": String(STRUCTURE_IDS[type_name]),
		})
	return {
		"schema": "openbfme.sage-object-bindings",
		"schemaVersion": 0,
		"matchPolicy": "explicit-exact-type-name-only",
		"records": records,
		"summary": {
			"typeCount": 3,
			"placementCount": 8,
			"boundTypeCount": 3,
			"boundPlacementCount": 8,
			"logicalTypeCount": 0,
			"logicalPlacementCount": 0,
			"resolvedTypeCount": 3,
			"resolvedPlacementCount": 8,
			"unresolvedTypeCount": 0,
			"unresolvedPlacementCount": 0,
			"resolutionStatus": "complete",
		},
	}


func _normalized_placements() -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	var source_index := 100
	for type_name in ["CaveTrollLair", "Inn", "WargLair"]:
		for local_index in range(int(PLACEMENT_COUNTS[type_name])):
			var position := Vector3(float(source_index), 1.5 + local_index, float(-source_index))
			var yaw := float(source_index) * 0.01
			result.append({
				"source_type": type_name,
				"source_index": source_index,
				"source_position": position * 10.0,
				"position": position,
				"source_yaw": yaw,
				"yaw": yaw,
				"scale": Vector3.ONE,
			})
			source_index += 1
	return result


func _fixture_lifecycle(object_id: String, prefix: String, maximum: int, unique_paths: bool) -> Dictionary:
	var is_inn := object_id == String(STRUCTURE_IDS.Inn)
	var is_warg := object_id == String(STRUCTURE_IDS.WargLair)
	var placement_count := 4 if is_warg else 2
	var damaged_threshold := 2000 if is_inn else 1000
	var really_damaged_threshold := 1000 if is_inn else 500
	var paths := {
		"construction": _fixture_path(prefix, "construction", unique_paths),
		"intact": _fixture_path(prefix, "intact", unique_paths),
		"damaged": _fixture_path(prefix, "damaged", unique_paths),
		"really-damaged": _fixture_path(prefix, "really-damaged", unique_paths),
		"collapsing": _fixture_path(prefix, "collapse", unique_paths),
		"rubble": _fixture_path(prefix, "rubble", unique_paths),
		"post-rubble": _fixture_path(prefix, "post-rubble", unique_paths),
		"rebuild-hole": _fixture_path(prefix, "rebuild-hole", unique_paths),
	}
	var collapse := {
		"animationFrameCount": 101 if is_inn else 91,
		"animationFrameRate": 30,
		"animationVirtualPath": "art/w3d/nb/neutral_d3.w3d",
		"exactTotalTimingStatus": (
			"no-StructureCollapseUpdate-and-bfme2-KeepObjectDie-default-not-proven"
			if is_inn
			else "blocked-on-bfme2-runtime-oracle"
		),
		"module": null if is_inn else "StructureCollapseUpdate",
	}
	var post_rubble := {
		"terminalDuration": (
			"unbounded-under-KeepObjectDie-until-rebuild-or-explicit-destruction"
			if is_inn
			else "unbounded-until-rebuild-or-explicit-destruction"
		),
	}
	if is_inn:
		post_rubble["exactPostRubbleTransitionTimingStatus"] = "blocked-on-bfme2-KeepObjectDie-runtime-oracle"
	var rebuild_hole: Variant = null
	if not is_inn:
		rebuild_hole = {
			"exactOriginalRemovalFrame": null,
			"exactOriginalRemovalFrameStatus": "blocked-on-bfme2-runtime-oracle",
			"healthRegenPercentPerSecond": 0.0,
			"maximumHealth": 500.0,
			"originalObjectDestroyWhenCollapseDone": true,
			"sourceDefinitionVirtualPath": "data/ini/object/neutral/holes.ini",
			"sourceTypeName": "WargLairHole" if is_warg else "CaveTrollLairHole",
			"spawnTiming": "death-behavior-immediate",
			"states": [
				{
					"fadeInSeconds": 2.0,
					"phase": "visible-rubble",
					"sourceConditionSets": [[]],
					"transitionAuthority": "deterministic-simulation",
					"visual": {"mode": "glb", "glb": paths["rebuild-hole"]},
				}
			],
			"terminalDuration": "unbounded-until-rebuild-or-explicit-destruction",
		}
	var lifecycle := {
		"objectId": object_id,
		"schemaVersion": 1,
		"initialPhase": "intact",
		"phases": [
			_phase("construction", [["AWAITING_CONSTRUCTION"], ["ACTIVELY_BEING_CONSTRUCTED", "PARTIALLY_CONSTRUCTED"]], "glb", paths.construction, "neutral_build", "manual-progress", "intact"),
			_phase("intact", [[]], "glb", paths.intact, null, "none", "damaged"),
			_phase("damaged", [["DAMAGED"]], "glb", paths.damaged, null, "none", "really-damaged"),
			_phase("really-damaged", [["REALLYDAMAGED"]], "glb", paths["really-damaged"], "neutral_d2", "once", "collapsing"),
			_phase("collapsing", [["COLLAPSING"]], "glb", paths.collapsing, "neutral_d3", "once", "rubble"),
			_phase("rubble", [["RUBBLE"]], "glb" if is_inn else "no-render", paths.rubble if is_inn else "None", null, "none", "post-rubble"),
			_phase("post-rubble", [["POST_RUBBLE"]], "glb" if is_inn else "no-render", paths["post-rubble"] if is_inn else "NONE", null, "none", null),
			_phase("post-collapse", [["POST_COLLAPSE"]], "glb" if is_inn else "no-render", paths.rubble if is_inn else "None", null, "none", null),
		],
		"bib": {
			"sourceConditions": [],
			"visual": {"mode": "glb", "glb": _fixture_path(prefix, "bib", unique_paths)},
			"duringConstruction": true,
		},
		"audioEvents": {
			"ambient": null,
			"collapse": "BuildingSink",
			"constructionSelect": null,
			"damaged": "BuildingLightDamageStone",
			"reallyDamaged": "BuildingHeavyDamageStone",
			"select": "CreepBuildingGenericSelect",
		},
		"effects": {
			"enteringStateFx": {
				"damaged": "FX_BuildingDamaged",
				"really-damaged": "FX_BuildingReallyDamaged",
				"collapsing": "FX_StructureMediumCollapse",
			},
			"collapseUpdateFx": {} if is_inn else {"initial": "FX_StructureMediumCollapse", "almost-final": "FX_StructureAlmostCollapse"},
			"particleAttachments": [
				{"sourceConditions": ["POST_RUBBLE"], "bone": "NONE", "particleSystemId": "SmokeBuildingMediumRubble", "options": []},
				{"sourceConditions": ["POST_COLLAPSE"], "bone": "NONE", "particleSystemId": "SmokeBuildingMediumRubble", "options": []},
			],
			"definitionTranslationStatus": "exact-particle-plan-cross-selection-pending",
		},
		"simulationFacts": {
			"maximumHealth": maximum,
			"damageStateRule": {
				"damagedThreshold": damaged_threshold,
				"reallyDamagedThreshold": really_damaged_threshold,
			},
			"initialHealth": {
				"derivedHitPoints": maximum,
				"mapAuthoredPercent": 100,
				"status": "proven-full-health-map-placement",
			},
			"construction": {
				"animation": "Neutral_A.Neutral_A",
				"animationMode": "MANUAL",
				"buildTimeSeconds": 45 if is_inn else 30,
			},
			"collapse": collapse,
			"postRubble": post_rubble,
			"captureInitialState": {
				"initialHealthPercent": 100,
				"initialPhase": "intact",
				"mapPlacementCount": placement_count,
				"owner": "PlyrNeutral/teamPlyrNeutral" if is_inn else "PlyrCreeps/teamPlyrCreeps",
			},
		},
		"rebuildHole": rebuild_hole,
	}
	return lifecycle


func _fixture_path(prefix: String, role: String, unique_paths: bool) -> String:
	return "%s/%s.glb" % [prefix, role] if unique_paths else "%s/farm.glb" % prefix


func _phase(
	phase: String,
	conditions: Array,
	visual_mode: String,
	visual_value: String,
	clip: Variant,
	animation_mode: String,
	next_phase: Variant
) -> Dictionary:
	var visual := {"mode": visual_mode}
	if visual_mode == "glb":
		visual["glb"] = visual_value
	else:
		visual["sourceIdentifier"] = visual_value
	return {
		"phase": phase,
		"sourceConditionSets": conditions,
		"visual": visual,
		"animation": {"clip": clip, "mode": animation_mode},
		"nextPhase": next_phase,
		"transitionAuthority": "deterministic-simulation",
	}


func _fixture_visuals(lifecycle: Dictionary) -> Dictionary:
	var clips_by_path: Dictionary = {}
	for phase_value in lifecycle.phases:
		var phase: Dictionary = phase_value
		var visual: Dictionary = phase.visual
		if String(visual.mode) != "glb":
			continue
		var path := String(visual.glb)
		if not clips_by_path.has(path):
			clips_by_path[path] = []
		var clip: Variant = phase.animation.clip
		if typeof(clip) == TYPE_STRING:
			(clips_by_path[path] as Array).append(String(clip))
	var bib_path := String(lifecycle.bib.visual.glb)
	if not clips_by_path.has(bib_path):
		clips_by_path[bib_path] = []
	if typeof(lifecycle.get("rebuildHole")) == TYPE_DICTIONARY:
		var rebuild: Dictionary = lifecycle.rebuildHole
		var rebuild_state: Dictionary = rebuild.states[0]
		var rebuild_path := String(rebuild_state.visual.glb)
		if not clips_by_path.has(rebuild_path):
			clips_by_path[rebuild_path] = []
	var result: Dictionary = {}
	for path_value in clips_by_path:
		var path := String(path_value)
		result[path] = _fixture_model(clips_by_path[path] as Array)
	return result


func _fixture_model(clips: Array) -> Node3D:
	var root_node := Node3D.new()
	var mesh_instance := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = Vector3(4.0, 4.0, 4.0)
	mesh_instance.mesh = mesh
	mesh_instance.position.y = 2.0
	root_node.add_child(mesh_instance)
	if not clips.is_empty():
		var player := AnimationPlayer.new()
		var library := AnimationLibrary.new()
		for clip_value in clips:
			var animation := Animation.new()
			animation.length = 2.0
			library.add_animation(String(clip_value), animation)
		player.add_animation_library("", library)
		root_node.add_child(player)
	return root_node


func _entity(maximum: int) -> Dictionary:
	return {
		"id": 1,
		"team": 0,
		"structure_kind": "CaveTrollLair",
		"maximum_health": maximum,
		"health": maximum,
		"construction_progress": 1.0,
	}


func _count_type(placements: Array[Dictionary], type_name: String) -> int:
	var count := 0
	for placement in placements:
		if String(placement.get("source_type", "")) == type_name:
			count += 1
	return count


func _placements_have_explicit_structure_contract(placements: Array[Dictionary]) -> bool:
	for placement in placements:
		var type_name := String(placement.get("source_type", ""))
		if (
			String(placement.get("classification", "")) != "lifecycle-structure"
			or String(placement.get("object_id", "")) != String(STRUCTURE_IDS.get(type_name, ""))
			or String(placement.get("source_virtual_model", "")) != String(SOURCE_MODELS.get(type_name, ""))
		):
			return false
	return placements.size() == 8


func _scene_structures_match(container: Node3D, placements: Array[Dictionary]) -> bool:
	if container == null:
		return false
	var by_index: Dictionary = {}
	for placement in placements:
		by_index[int(placement.source_index)] = placement
	for child_value in container.get_children():
		var child := child_value as Node3D
		var source_index := int(child.get_meta("source_index", -1))
		if child == null or child.get_script() != structure_script or not by_index.has(source_index):
			return false
		var placement: Dictionary = by_index[source_index]
		var expected := Transform3D(Basis(Vector3.UP, float(placement.yaw)).scaled(Vector3(placement.scale)), Vector3(placement.position))
		if (
			not child.transform.origin.is_equal_approx(expected.origin)
			or not child.transform.basis.is_equal_approx(expected.basis)
			or String(child.get_meta("content_object_id", "")) != String(placement.object_id)
			or String(child.get_meta("source_virtual_model", "")) != String(placement.source_virtual_model)
		):
			return false
	return container.get_child_count() == placements.size()


func _all_structures_start_intact(container: Node3D) -> bool:
	if container == null or container.get_child_count() != 8:
		return false
	for child in container.get_children():
		if String(child.current_lifecycle_phase) != "intact" or not is_equal_approx(float(child.health_ratio), 1.0) or String(child.contract_error) != "":
			return false
	return true


func _visible_body(structure: Node3D) -> Node3D:
	var host := structure.get_node_or_null("StructureVisual/SharedLifecycleTransform")
	if host == null:
		return null
	for child_value in host.get_children():
		if child_value is Node3D and (child_value as Node3D).visible and String((child_value as Node3D).name).begins_with("Body_"):
			return child_value as Node3D
	return null


func _first_animation_player(node: Node) -> AnimationPlayer:
	if node == null:
		return null
	if node is AnimationPlayer:
		return node as AnimationPlayer
	for child in node.get_children():
		var found := _first_animation_player(child)
		if found != null:
			return found
	return null


func _free_fixture_visuals(fixtures: Dictionary) -> void:
	for value in fixtures.values():
		if value is Node:
			(value as Node).free()


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_NEUTRAL_LIFECYCLE PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_NEUTRAL_LIFECYCLE FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _watchdog_timeout() -> void:
	printerr("RETAIL_NEUTRAL_LIFECYCLE FAIL watchdog_timeout")
	quit(1)


func _finish() -> void:
	print("RETAIL_NEUTRAL_LIFECYCLE_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)

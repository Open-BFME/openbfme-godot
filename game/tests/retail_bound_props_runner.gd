extends SceneTree
## Focused private-pack gate for exact bound Fords prop consumption.

const MAP_ID := "bfme2.map.fords-of-isen-ii"
const SOLDIER_OBJECT_ID := "bfme2.object.gondor-fighter"
const EXPECTED_BOUND_TYPE := "PTGrass15"
const EXPECTED_BOUND_PLACEMENTS := 31
const EXPECTED_RENDERABLE_TYPE_COUNT := 55
const EXPECTED_RENDERABLE_PLACEMENTS := 1249
const EXPECTED_LIFECYCLE_STRUCTURE_TYPES: Array[String] = ["CaveTrollLair", "Inn", "WargLair"]
const EXPECTED_LIFECYCLE_STRUCTURE_PLACEMENTS := 8
const EXPECTED_PARTICLE_OWNED_TYPES: Array[String] = ["WtrRiplsSmall", "WtrflHaze"]

var passed := 0
var failed := 0
var _mod_loader
var _content_db
var _map_data_script
var _battlefield_script
var _asset_factory_script


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_mod_loader = root.get_node_or_null("ModLoader")
	_content_db = root.get_node_or_null("ContentDB")
	_check("content_autoloads_available", _mod_loader != null and _content_db != null)
	if _mod_loader == null or _content_db == null:
		_finish()
		return
	_map_data_script = load("res://src/retail_slice/retail_map_data.gd")
	_battlefield_script = load("res://src/retail_slice/retail_fords_battlefield.gd")
	_asset_factory_script = load("res://src/view/asset_factory.gd")
	_check("retail_prop_scripts_compile", _map_data_script != null and _battlefield_script != null and _asset_factory_script != null)
	if _map_data_script == null or _battlefield_script == null or _asset_factory_script == null:
		_finish()
		return
	if not _content_db.bundle_objects.has(SOLDIER_OBJECT_ID):
		_content_db.reload()
	var member_definition: Dictionary = _content_db.get_bundle_object(SOLDIER_OBJECT_ID)
	var map_definition: Dictionary = _content_db.get_bundle_map(MAP_ID)
	var member_pack_root := String(member_definition.get("_pack_root", ""))
	var pack_root := String(map_definition.get("_pack_root", ""))
	_check("private_split_roots_selected", pack_root != "" and member_pack_root != "" and not pack_root.begins_with("res://") and not member_pack_root.begins_with("res://"), "map=%s member=%s" % [pack_root, member_pack_root])
	if pack_root == "" or map_definition.is_empty():
		_finish()
		return

	var map_data = _map_data_script.new()
	_check("bound_prop_map_loads", bool(map_data.load_from_pack(pack_root, map_definition)), String(map_data.error))
	if not map_data.ready:
		_finish()
		return
	_check("binding_manifest_is_exact_partial_table", int(map_data.object_binding_record_count) == 86 and String(map_data.object_binding_resolution_status) == "partial", "%d/%s" % [map_data.object_binding_record_count, map_data.object_binding_resolution_status])
	_check("renderable_prop_closure_is_exact", map_data.bound_prop_type_ids.size() == EXPECTED_RENDERABLE_TYPE_COUNT and int(map_data.bound_prop_placement_count) == EXPECTED_RENDERABLE_PLACEMENTS and map_data.bound_prop_placements.size() == EXPECTED_RENDERABLE_PLACEMENTS, "%d/%d/%d" % [map_data.bound_prop_type_ids.size(), map_data.bound_prop_placement_count, map_data.bound_prop_placements.size()])
	_check("lifecycle_structure_split_is_exact", map_data.bound_structure_type_ids == EXPECTED_LIFECYCLE_STRUCTURE_TYPES and int(map_data.bound_structure_placement_count) == EXPECTED_LIFECYCLE_STRUCTURE_PLACEMENTS and map_data.bound_structure_placements.size() == EXPECTED_LIFECYCLE_STRUCTURE_PLACEMENTS, "%s/%d/%d" % [str(map_data.bound_structure_type_ids), map_data.bound_structure_placement_count, map_data.bound_structure_placements.size()])
	_check("logical_types_remain_non_rendered", map_data.logical_prop_type_ids.size() == 26 and int(map_data.logical_prop_placement_count) == 114, "%d/%d" % [map_data.logical_prop_type_ids.size(), map_data.logical_prop_placement_count])
	_check("particle_owned_scoreboard_is_explicit", map_data.unresolved_prop_type_ids == EXPECTED_PARTICLE_OWNED_TYPES and int(map_data.unresolved_prop_placement_count) == 13, "%s/%d" % [str(map_data.unresolved_prop_type_ids), map_data.unresolved_prop_placement_count])
	_check("particle_owned_objects_never_become_fake_markers", map_data.generic_prop_placements.is_empty(), str(map_data.generic_prop_placements.size()))
	_check("bound_type_never_enters_marker_preview", _placements_exclude_types(map_data.generic_prop_placements, map_data.bound_prop_type_ids))

	var objects_document: Dictionary = _mod_loader._read_json(map_data.map_root.path_join(String(map_definition.get("objects", "")))) as Dictionary
	var bindings_document: Dictionary = _mod_loader._read_json(map_data.object_bindings_path) as Dictionary
	var source_by_index := _source_objects_by_index(objects_document)
	var source_type_counts := _source_type_counts(objects_document)
	_check("private_source_objects_available_for_transform_oracle", source_by_index.size() == 1526 and int(source_type_counts.get(EXPECTED_BOUND_TYPE, 0)) == EXPECTED_BOUND_PLACEMENTS, "%d/%d" % [source_by_index.size(), int(source_type_counts.get(EXPECTED_BOUND_TYPE, 0))])
	_check("bound_type_ids_match_manifest", map_data.bound_prop_type_ids == _binding_type_ids(bindings_document, "bound", "renderable"))
	_check("lifecycle_structure_ids_match_manifest", map_data.bound_structure_type_ids == _binding_type_ids(bindings_document, "bound", "lifecycle-structure"))
	_check("unresolved_type_ids_match_manifest", map_data.unresolved_prop_type_ids == _binding_type_ids(bindings_document, "unresolved"))
	_check("bound_placements_preserve_source_facts", _bound_placement_facts_match(map_data, source_by_index))
	_check("bound_paths_are_pack_contained_glbs", _bound_paths_are_contained(map_data, pack_root))

	var battlefield = _battlefield_script.new()
	battlefield.name = "FocusedBoundPropBattlefield"
	root.add_child(battlefield)
	_check("battlefield_accepts_valid_bound_props", bool(battlefield.configure(map_data)), String(battlefield.error))
	_check("battlefield_instantiates_exact_renderable_prop_closure", int(battlefield.bound_retail_prop_count) == EXPECTED_RENDERABLE_PLACEMENTS and battlefield.retail_prop_container != null and battlefield.retail_prop_container.get_child_count() == EXPECTED_RENDERABLE_PLACEMENTS, str(battlefield.bound_retail_prop_count))
	_check("battlefield_exposes_exact_bound_types", battlefield.bound_retail_prop_type_ids == map_data.bound_prop_type_ids, str(battlefield.bound_retail_prop_type_ids))
	_check("bound_props_have_real_retail_mesh_instances", int(battlefield.bound_retail_mesh_instance_count) >= EXPECTED_RENDERABLE_PLACEMENTS and _all_bound_nodes_have_meshes(battlefield.retail_prop_container), str(battlefield.bound_retail_mesh_instance_count))
	_check("bound_scene_nodes_preserve_source_transforms", _bound_scene_transforms_match(battlefield.retail_prop_container, map_data.bound_prop_placements, pack_root, map_data.local_transform_scale))
	_check("battlefield_exposes_particle_owned_scoreboard", int(battlefield.unresolved_prop_placement_count) == 13 and battlefield.unresolved_prop_type_ids == EXPECTED_PARTICLE_OWNED_TYPES)
	var vegetation_marker = battlefield.find_child("SourceVegetationPlacementMarkers", true, false)
	var rock_marker = battlefield.find_child("SourceRockPlacementMarkers", true, false)
	_check("particle_owned_objects_have_no_substitute_geometry", int(battlefield.generic_prop_count) == 0 and vegetation_marker == null and rock_marker == null, str(battlefield.generic_prop_count))

	var missing_map_definition: Dictionary = map_definition.duplicate(true)
	missing_map_definition["objectBindings"] = "missing-object-bindings.json"
	var missing_document_probe = _map_data_script.new()
	_check("missing_binding_document_rejected", not bool(missing_document_probe.load_from_pack(pack_root, missing_map_definition)) and String(missing_document_probe.error).contains("object bindings"), String(missing_document_probe.error))

	var missing_glb_document: Dictionary = bindings_document.duplicate(true)
	_set_bound_record_field(missing_glb_document, "glb", "assets/models/props/openbfme-missing-bound-prop.glb")
	var missing_glb_probe = _binding_probe(map_data, missing_glb_document, source_type_counts)
	_check("missing_declared_bound_glb_rejected", not bool(missing_glb_probe["ok"]) and String(missing_glb_probe["data"].error).contains("missing or invalid"), String(missing_glb_probe["data"].error))

	var escaped_glb_document: Dictionary = bindings_document.duplicate(true)
	_set_bound_record_field(escaped_glb_document, "glb", "../escaped-bound-prop.glb")
	var escaped_glb_probe = _binding_probe(map_data, escaped_glb_document, source_type_counts)
	_check("escaped_declared_bound_glb_rejected", not bool(escaped_glb_probe["ok"]) and String(escaped_glb_probe["data"].error).contains("escaped"), String(escaped_glb_probe["data"].error))

	var corrupt_binding_document: Dictionary = bindings_document.duplicate(true)
	_set_bound_record_field(corrupt_binding_document, "matchMethod", "none")
	var corrupt_binding_probe = _binding_probe(map_data, corrupt_binding_document, source_type_counts)
	_check("corrupt_bound_record_rejected", not bool(corrupt_binding_probe["ok"]) and String(corrupt_binding_probe["data"].error).contains("exact renderable"), String(corrupt_binding_probe["data"].error))

	var original_glb_path := String((map_data.bound_prop_placements[0] as Dictionary).get("glb_path", ""))
	(map_data.bound_prop_placements[0] as Dictionary)["glb_path"] = map_data.map_root.path_join("missing-runtime-bound-prop.glb")
	var broken_battlefield = _battlefield_script.new()
	broken_battlefield.name = "MissingBoundPropBattlefield"
	root.add_child(broken_battlefield)
	_check("runtime_missing_glb_fails_closed", not bool(broken_battlefield.configure(map_data)) and String(broken_battlefield.error).contains("unsafe or incomplete"), String(broken_battlefield.error))
	_check("runtime_failure_never_substitutes_markers", int(broken_battlefield.bound_retail_prop_count) == 0 and int(broken_battlefield.generic_prop_count) == 0 and broken_battlefield.retail_prop_container == null and broken_battlefield.get_child_count() == 0)
	(map_data.bound_prop_placements[0] as Dictionary)["glb_path"] = original_glb_path

	print("RETAIL_BOUND_PROPS_METRICS bound_types=%s bound_placements=%d unresolved_types=%d unresolved_placements=%d markers=%d meshes=%d" % [
		",".join(map_data.bound_prop_type_ids),
		map_data.bound_prop_placement_count,
		map_data.unresolved_prop_type_ids.size(),
		map_data.unresolved_prop_placement_count,
		battlefield.generic_prop_count,
		battlefield.bound_retail_mesh_instance_count,
	])
	battlefield.queue_free()
	broken_battlefield.queue_free()
	_asset_factory_script.clear_mesh_cache()
	await process_frame
	await process_frame
	_finish()


func _binding_probe(reference_map_data, binding_document: Dictionary, source_type_counts: Dictionary) -> Dictionary:
	var probe = _map_data_script.new()
	probe.pack_root = reference_map_data.pack_root
	probe.map_root = reference_map_data.map_root
	probe.object_count = reference_map_data.object_count
	return {"ok": probe._load_object_bindings(binding_document, source_type_counts), "data": probe}


func _set_bound_record_field(document: Dictionary, key: String, value: Variant) -> void:
	var records: Array = document.get("records", [])
	for index in range(records.size()):
		var record: Dictionary = records[index]
		if String(record.get("status", "")) == "bound":
			record[key] = value
			records[index] = record
			break
	document["records"] = records


func _source_objects_by_index(document: Dictionary) -> Dictionary:
	var result: Dictionary = {}
	for value in Array(document.get("objects", [])):
		if typeof(value) == TYPE_DICTIONARY:
			var row: Dictionary = value
			result[int(row.get("index", -1))] = row
	return result


func _source_type_counts(document: Dictionary) -> Dictionary:
	var result: Dictionary = {}
	for value in Array(document.get("objects", [])):
		if typeof(value) != TYPE_DICTIONARY:
			continue
		var row := value as Dictionary
		if int(row.get("roadType", -1)) != 0:
			continue
		var type_name := String(row.get("typeName", ""))
		result[type_name] = int(result.get(type_name, 0)) + 1
	return result


func _binding_type_ids(document: Dictionary, status: String, classification: String = "") -> Array[String]:
	var result: Array[String] = []
	for value in Array(document.get("records", [])):
		if typeof(value) == TYPE_DICTIONARY and String((value as Dictionary).get("status", "")) == status and (classification == "" or String((value as Dictionary).get("classification", "")) == classification):
			result.append(String((value as Dictionary).get("typeName", "")))
	result.sort()
	return result


func _bound_placement_facts_match(map_data, source_by_index: Dictionary) -> bool:
	var seen: Dictionary = {}
	for placement_value in map_data.bound_prop_placements:
		var placement: Dictionary = placement_value
		var source_index := int(placement.get("source_index", -1))
		if seen.has(source_index) or not source_by_index.has(source_index):
			return false
		var source: Dictionary = source_by_index[source_index]
		var source_position := _vector3(Array(source.get("godotPosition", [])))
		var source_yaw := float(source.get("godotYawRadians", NAN))
		if (
			String(source.get("typeName", "")) != String(placement.get("source_type", ""))
			or Vector3(placement.get("source_position", Vector3.INF)) != source_position
			or not Vector3(placement.get("position", Vector3.INF)).is_equal_approx(map_data.source_to_local(source_position))
			or not is_equal_approx(float(placement.get("source_yaw", NAN)), source_yaw)
			or not is_equal_approx(float(placement.get("yaw", NAN)), source_yaw)
			or Vector3(placement.get("scale", Vector3.ZERO)) != Vector3.ONE
		):
			return false
		seen[source_index] = true
	return seen.size() == EXPECTED_RENDERABLE_PLACEMENTS


func _bound_paths_are_contained(map_data, pack_root: String) -> bool:
	for placement_value in map_data.bound_prop_placements:
		var path := String((placement_value as Dictionary).get("glb_path", ""))
		if path.get_extension().to_lower() != "glb" or not _mod_loader.path_is_within(pack_root, path) or not FileAccess.file_exists(path):
			return false
	return true


func _bound_scene_transforms_match(container: Node3D, placements: Array[Dictionary], pack_root: String, local_transform_scale: float) -> bool:
	if container == null:
		return false
	var by_index: Dictionary = {}
	for placement in placements:
		by_index[int(placement.get("source_index", -1))] = placement
	for child_value in container.get_children():
		var child := child_value as Node3D
		if child == null or String(child.get_meta("presentation", "")) != "retail-bound-glb":
			return false
		var source_index := int(child.get_meta("source_index", -1))
		if not by_index.has(source_index):
			return false
		var placement: Dictionary = by_index[source_index]
		var expected := Transform3D(
			Basis(Vector3.UP, float(placement.get("yaw", NAN))).scaled(Vector3(placement.get("scale", Vector3.ZERO)) * local_transform_scale),
			Vector3(placement.get("position", Vector3.INF))
		)
		var path := String(child.get_meta("glb_path", ""))
		if not child.transform.origin.is_equal_approx(expected.origin) or not child.transform.basis.is_equal_approx(expected.basis) or not _mod_loader.path_is_within(pack_root, path) or _mesh_instance_count(child) <= 0:
			return false
	return container.get_child_count() == placements.size()


func _all_bound_nodes_have_meshes(container: Node3D) -> bool:
	if container == null:
		return false
	for child in container.get_children():
		if _mesh_instance_count(child) <= 0:
			return false
	return container.get_child_count() > 0


func _mesh_instance_count(node: Node) -> int:
	if node == null:
		return 0
	var result := 1 if node is MeshInstance3D and (node as MeshInstance3D).mesh != null else 0
	for child in node.get_children():
		result += _mesh_instance_count(child)
	return result


func _placements_exclude_types(placements: Array[Dictionary], excluded: Array[String]) -> bool:
	for placement in placements:
		if String(placement.get("binding_status", "")) != "unresolved" or excluded.has(String(placement.get("source_type", ""))):
			return false
	return true


func _is_unresolved_marker(node: Node, bound_types: Array[String]) -> bool:
	if node == null or String(node.get_meta("presentation", "")) != "unresolved-marker" or bool(node.get_meta("retail_bound", true)):
		return false
	var disclosure := String(node.get_meta("disclosure", ""))
	var source_types: Array = node.get_meta("source_type_ids", [])
	for type_name in bound_types:
		if source_types.has(type_name):
			return false
	return disclosure.contains("not retail art")


func _vector3(values: Array) -> Vector3:
	if values.size() != 3:
		return Vector3.INF
	return Vector3(float(values[0]), float(values[1]), float(values[2]))


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_BOUND_PROPS PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_BOUND_PROPS FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	print("RETAIL_BOUND_PROPS_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)

## Focused private-pack gate for exact SAGE road/object separation.

extends SceneTree

const MAP_ID := "bfme2.map.fords-of-isen-ii"
const SOLDIER_OBJECT_ID := "bfme2.object.gondor-fighter"
const EXPECTED_ROAD_IDS: Array[String] = [
	"Footprints",
	"FtPrintDrkGr02",
	"FtPrintGrass02",
	"FtprintsDrk",
	"FtprintsDrk02",
]

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var content_db = root.get_node_or_null("ContentDB")
	var mod_loader = root.get_node_or_null("ModLoader")
	var map_data_script = load("res://src/retail_slice/retail_map_data.gd")
	_check("road_runner_dependencies", content_db != null and mod_loader != null and map_data_script != null)
	if content_db == null or mod_loader == null or map_data_script == null:
		_finish()
		return
	if not content_db.bundle_objects.has(SOLDIER_OBJECT_ID):
		content_db.reload()
	var member_definition: Dictionary = content_db.get_bundle_object(SOLDIER_OBJECT_ID)
	var map_definition: Dictionary = content_db.get_bundle_map(MAP_ID)
	var soldier_pack_root := String(member_definition.get("_pack_root", ""))
	var pack_root := String(map_definition.get("_pack_root", ""))
	_check("private_split_roots_are_selected", pack_root != "" and soldier_pack_root != "" and not pack_root.begins_with("res://") and not soldier_pack_root.begins_with("res://"), "map=%s soldier=%s" % [pack_root, soldier_pack_root])
	if pack_root == "" or map_definition.is_empty():
		_finish()
		return
	var map_source := String(map_definition.get("_source", ""))
	var declared_roads := String(map_definition.get("roads", ""))
	var declared_roads_path := map_source.get_base_dir().path_join(declared_roads)
	_check("map_declares_contained_roads_document", map_source != "" and declared_roads == "roads.json" and mod_loader.path_is_within(pack_root, map_source) and mod_loader.path_is_within(pack_root, declared_roads_path) and FileAccess.file_exists(declared_roads_path), "pack=%s source=%s roads=%s" % [pack_root, map_source, declared_roads_path])

	var map_data = map_data_script.new()
	_check("exact_road_map_loads", bool(map_data.load_from_pack(pack_root, map_definition)), String(map_data.error))
	if not map_data.ready:
		_finish()
		return
	_check("road_ids_exact", map_data.road_type_ids == EXPECTED_ROAD_IDS, str(map_data.road_type_ids))
	_check("road_counts_exact", int(map_data.road_type_count) == 5 and int(map_data.road_control_point_count) == 142 and int(map_data.road_segment_count) == 71 and int(map_data.road_unresolved_control_point_count) == 0, "%d/%d/%d/%d" % [map_data.road_type_count, map_data.road_control_point_count, map_data.road_segment_count, map_data.road_unresolved_control_point_count])
	_check("object_scoreboard_excludes_roads", int(map_data.object_count) == 1526 and int(map_data.nonroad_object_count) == 1384 and int(map_data.bound_prop_placement_count) + int(map_data.bound_structure_placement_count) + int(map_data.logical_prop_placement_count) + int(map_data.unresolved_prop_placement_count) == 1384)
	_check("road_controls_never_become_prop_candidates", _placements_exclude_road_ids(map_data.bound_prop_placements, map_data.road_type_ids) and _placements_exclude_road_ids(map_data.generic_prop_placements, map_data.road_type_ids))

	var objects_document_value: Variant = mod_loader._read_json(map_data.map_root.path_join(String(map_definition.get("objects", ""))))
	var roads_document_value: Variant = mod_loader._read_json(map_data.roads_path)
	_check("road_documents_are_dictionaries", typeof(objects_document_value) == TYPE_DICTIONARY and typeof(roads_document_value) == TYPE_DICTIONARY)
	if typeof(objects_document_value) != TYPE_DICTIONARY or typeof(roads_document_value) != TYPE_DICTIONARY:
		_finish()
		return
	var objects_document := objects_document_value as Dictionary
	var roads_document := roads_document_value as Dictionary
	var source_roads := _source_roads(objects_document)
	_check("roads_schema_exact", String(roads_document.get("schema", "")) == "openbfme.sage-roads" and int(roads_document.get("schemaVersion", -1)) == 0 and String(roads_document.get("pairingPolicy", "")) == "source-order-exact-wire-2-then-4-same-road-id" and String(roads_document.get("curveReconstruction", "")) == "not-attempted")
	_check("roads_summary_exact", _summary_is_exact(roads_document.get("summary", {}) as Dictionary))
	_check("segment_endpoints_preserve_source_objects", _segment_endpoints_match(roads_document, source_roads))

	var missing_definition := map_definition.duplicate(true)
	missing_definition["roads"] = "missing-roads.json"
	var missing_probe = map_data_script.new()
	_check("missing_roads_fail_closed", not bool(missing_probe.load_from_pack(pack_root, missing_definition)) and String(missing_probe.error).contains("roads"), String(missing_probe.error))

	var malformed_document := roads_document.duplicate(true)
	malformed_document["schema"] = "openbfme.invalid-roads"
	var malformed_probe = map_data_script.new()
	_check("malformed_roads_fail_closed", not bool(malformed_probe._load_roads(malformed_document, source_roads, malformed_document.get("summary", {}) as Dictionary)) and String(malformed_probe.error).contains("contract"), String(malformed_probe.error))

	var unpaired_document := roads_document.duplicate(true)
	var unpaired_points: Array = unpaired_document.get("controlPoints", [])
	var unpaired_sources := source_roads.duplicate(true)
	var second_point := (unpaired_points[1] as Dictionary).duplicate(true)
	second_point["wireType"] = 2
	second_point["role"] = "segment-start"
	unpaired_points[1] = second_point
	unpaired_document["controlPoints"] = unpaired_points
	var second_source_index := int(second_point.get("sourceIndex", -1))
	var second_source := (unpaired_sources[second_source_index] as Dictionary).duplicate(true)
	second_source["wire_type"] = 2
	unpaired_sources[second_source_index] = second_source
	var unpaired_probe = map_data_script.new()
	_check("unpaired_road_controls_fail_closed", not bool(unpaired_probe._load_roads(unpaired_document, unpaired_sources, unpaired_document.get("summary", {}) as Dictionary)) and String(unpaired_probe.error).contains("unpaired"), String(unpaired_probe.error))

	print("RETAIL_ROAD_METRICS road_types=%d control_points=%d segments=%d nonroad_objects=%d" % [
		map_data.road_type_count,
		map_data.road_control_point_count,
		map_data.road_segment_count,
		map_data.nonroad_object_count,
	])
	_finish()


func _source_roads(document: Dictionary) -> Dictionary:
	var result: Dictionary = {}
	for value in Array(document.get("objects", [])):
		if typeof(value) != TYPE_DICTIONARY:
			continue
		var row := value as Dictionary
		var road_type := int(row.get("roadType", 0))
		if road_type == 0:
			continue
		result[int(row.get("index", -1))] = {
			"road_id": String(row.get("typeName", "")),
			"wire_type": road_type,
			"sage_position": _vector3(row.get("sagePosition", [])),
			"godot_position": _vector3(row.get("godotPosition", [])),
		}
	return result


func _summary_is_exact(summary: Dictionary) -> bool:
	return (
		String(summary.get("status", "")) == "exact-paired-control-points"
		and int(summary.get("roadIdCount", -1)) == 5
		and int(summary.get("controlPointCount", -1)) == 142
		and int(summary.get("pairedControlPointCount", -1)) == 142
		and int(summary.get("unresolvedControlPointCount", -1)) == 0
		and int(summary.get("segmentCount", -1)) == 71
		and int(summary.get("unresolvedDiagnosticCount", -1)) == 0
	)


func _segment_endpoints_match(document: Dictionary, source_roads: Dictionary) -> bool:
	var segments: Array = document.get("segments", [])
	if segments.size() != 71:
		return false
	for expected_index in range(segments.size()):
		var segment := segments[expected_index] as Dictionary
		var start_index := int(segment.get("startSourceIndex", -1))
		var end_index := int(segment.get("endSourceIndex", -1))
		if not source_roads.has(start_index) or not source_roads.has(end_index):
			return false
		var start: Dictionary = source_roads[start_index]
		var finish: Dictionary = source_roads[end_index]
		if (
			int(segment.get("index", -1)) != expected_index
			or int(start.get("wire_type", -1)) != 2
			or int(finish.get("wire_type", -1)) != 4
			or String(segment.get("roadId", "")) != String(start.get("road_id", ""))
			or String(segment.get("roadId", "")) != String(finish.get("road_id", ""))
			or _vector3(segment.get("sageStart", [])) != Vector3(start.get("sage_position", Vector3.INF))
			or _vector3(segment.get("sageEnd", [])) != Vector3(finish.get("sage_position", Vector3.INF))
			or _vector3(segment.get("godotStart", [])) != Vector3(start.get("godot_position", Vector3.INF))
			or _vector3(segment.get("godotEnd", [])) != Vector3(finish.get("godot_position", Vector3.INF))
		):
			return false
	return true


func _placements_exclude_road_ids(placements: Array[Dictionary], road_ids: Array[String]) -> bool:
	for placement in placements:
		if road_ids.has(String(placement.get("source_type", ""))):
			return false
	return true


func _vector3(value: Variant) -> Vector3:
	var values: Array = value as Array if typeof(value) == TYPE_ARRAY else []
	if values.size() != 3:
		return Vector3.INF
	return Vector3(float(values[0]), float(values[1]), float(values[2]))


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_ROAD PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_ROAD FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	print("RETAIL_ROAD_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)

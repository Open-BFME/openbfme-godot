## Focused private-pack gate for provenance-closed, topology-aware Fords roads.

extends SceneTree

const EXPECTED_ROAD_IDS: Array[String] = [
	"Footprints",
	"FtPrintDrkGr02",
	"FtPrintGrass02",
	"FtprintsDrk",
	"FtprintsDrk02",
]
const EXPECTED_CLOSURE_SHA256 := "45f557b171a18739e268c626e7be0f2aecadba4a0a527d809fdc7b3a1076fdc2"

var passed := 0
var failed := 0
var finished := false


func _initialize() -> void:
	create_timer(45.0, true, false, true).timeout.connect(_watchdog_timeout)
	call_deferred("_run")


func _run() -> void:
	var mod_loader = root.get_node_or_null("ModLoader")
	var map_data_script = load("res://src/retail_slice/retail_map_data.gd")
	var battlefield_script = load("res://src/retail_slice/retail_fords_battlefield.gd")
	var material_builder_script = load("res://src/retail_slice/retail_road_material_builder.gd")
	_check("road_visual_runner_dependencies", mod_loader != null and map_data_script != null and battlefield_script != null and material_builder_script != null)
	if mod_loader == null or map_data_script == null or battlefield_script == null or material_builder_script == null:
		_finish()
		return
	var pack_root := OS.get_environment("OPENBFME_ROAD_PACK").replace("\\", "/").trim_suffix("/")
	_check("explicit_private_road_pack_is_required", pack_root != "" and not pack_root.begins_with("res://") and DirAccess.dir_exists_absolute(pack_root), pack_root)
	if pack_root == "" or not DirAccess.dir_exists_absolute(pack_root):
		_finish()
		return
	var pack_value: Variant = mod_loader._read_json(pack_root.path_join("pack.json"))
	_check("explicit_pack_document_is_valid", typeof(pack_value) == TYPE_DICTIONARY)
	if typeof(pack_value) != TYPE_DICTIONARY:
		_finish()
		return
	var pack := pack_value as Dictionary
	var entry_map := String((pack.get("files", {}) as Dictionary).get("entryMap", ""))
	var map_path: String = mod_loader.resolve_pack_path(pack_root, entry_map)
	var map_value: Variant = mod_loader._read_json(map_path)
	_check("explicit_pack_entry_map_is_contained", entry_map == "maps/fords-of-isen-ii/map.json" and map_path != "" and mod_loader.path_is_within(pack_root, map_path) and typeof(map_value) == TYPE_DICTIONARY, map_path)
	if typeof(map_value) != TYPE_DICTIONARY:
		_finish()
		return
	var map_definition := (map_value as Dictionary).duplicate(true)
	map_definition["_source"] = map_path
	map_definition["_pack_root"] = pack_root
	var map_data = map_data_script.new()
	_check("provenance_closed_road_map_loads", bool(map_data.load_from_pack(pack_root, map_definition)), String(map_data.error))
	if not map_data.ready:
		_finish()
		return

	_check("road_material_ids_exact", map_data.road_type_ids == EXPECTED_ROAD_IDS and int(map_data.road_material_count) == 5, str(map_data.road_type_ids))
	_check("road_material_source_aggregate_exact", String(map_data.road_source_report_aggregate_sha256) == EXPECTED_CLOSURE_SHA256, String(map_data.road_source_report_aggregate_sha256))
	_check("road_material_authored_widths_exact", _authored_widths_exact(map_data.road_material_catalog))
	_check("road_pngs_are_contained_decoded_and_provenance_hashed", _catalog_matches_provenance(map_data, mod_loader))
	_check("road_topology_source_counts_exact", int(map_data.road_segment_count) == 71 and int(map_data.road_unique_endpoint_count) == 120 and int(map_data.road_shared_node_count) == 21, "%d/%d/%d" % [map_data.road_segment_count, map_data.road_unique_endpoint_count, map_data.road_shared_node_count])
	_check("road_topology_curve_evidence_exact", int(map_data.road_curve_candidate_node_count) == 18 and int(map_data.road_crossing_candidate_node_count) == 0 and int(map_data.road_modifier_flags_or) == 0, "%d/%d/%d" % [map_data.road_curve_candidate_node_count, map_data.road_crossing_candidate_node_count, map_data.road_modifier_flags_or])

	var battlefield = battlefield_script.new()
	root.add_child(battlefield)
	_check("topology_aware_retail_roads_build_in_isolation", bool(battlefield._build_roads(map_data)), String(battlefield.error))
	if not battlefield.road_material_source_driven or battlefield.road_container == null:
		battlefield.queue_free()
		await process_frame
		_finish()
		return
	_check("retail_road_material_groups_exact", int(battlefield.road_material_count) == 5 and int(battlefield.road_mesh_instance_count) == 5 and battlefield.road_container != null and battlefield.road_container.get_child_count() == 5)
	_check("retail_road_topology_runtime_exact", int(battlefield.road_source_edge_count) == 71 and int(battlefield.road_unique_endpoint_count) == 120 and int(battlefield.road_shared_node_count) == 21)
	_check("eleven_broad_curves_and_seven_exact_fallbacks", int(battlefield.road_curve_candidate_node_count) == 18 and int(battlefield.road_generated_broad_curve_count) == 11 and int(battlefield.road_straight_fallback_node_count) == 7, "%d/%d/%d" % [battlefield.road_curve_candidate_node_count, battlefield.road_generated_broad_curve_count, battlefield.road_straight_fallback_node_count])
	_check("curve_edge_pairs_and_formula_evidence_exact", _curve_evidence_exact(battlefield))
	_check("broad_curve_is_split_into_atlas_strips", int(battlefield.road_curve_strip_count) >= 3 and int(battlefield.road_render_strip_count) == 71 + int(battlefield.road_curve_strip_count), "%d/%d" % [battlefield.road_curve_strip_count, battlefield.road_render_strip_count])
	_check("adaptive_mesh_counts_are_source_derived", int(battlefield.road_vertex_count) > 284 and int(battlefield.road_triangle_count) == int(battlefield.road_vertex_count) - 2 * int(battlefield.road_render_strip_count), "vertices=%d triangles=%d strips=%d" % [battlefield.road_vertex_count, battlefield.road_triangle_count, battlefield.road_render_strip_count])
	_check("road_vertices_are_terrain_draped", _all_road_vertices_clear_terrain(battlefield.road_container, map_data))
	_check("road_materials_preserve_alpha_depth_and_anisotropy", _materials_are_exact(battlefield.road_container))

	var missing_definition := map_definition.duplicate(true)
	missing_definition["roadMaterials"] = "missing-road-materials.json"
	var missing_probe = map_data_script.new()
	_check(
		"missing_safe_road_materials_fall_back_to_geometry",
		bool(missing_probe.load_from_pack(pack_root, missing_definition))
			and int(missing_probe.road_segment_count) == int(map_data.road_segment_count)
			and int(missing_probe.road_material_count) == 0
			and String(missing_probe.road_materials_path) == "",
		String(missing_probe.error)
	)
	var escaped_definition := map_definition.duplicate(true)
	escaped_definition["roadMaterials"] = "../road-materials.json"
	var escaped_probe = map_data_script.new()
	_check("escaped_road_materials_fail_closed", not bool(escaped_probe.load_from_pack(pack_root, escaped_definition)) and String(escaped_probe.error).contains("road materials"), String(escaped_probe.error))
	var road_manifest_value: Variant = mod_loader._read_json(map_data.road_materials_path)
	_check("road_material_manifest_is_dictionary", typeof(road_manifest_value) == TYPE_DICTIONARY)
	if typeof(road_manifest_value) == TYPE_DICTIONARY:
		var road_manifest := road_manifest_value as Dictionary
		var invalid_schema := road_manifest.duplicate(true)
		invalid_schema["schema"] = "openbfme.invalid-road-materials"
		_check("invalid_road_material_schema_fails_closed", not _probe_material_document(map_data_script, map_data, invalid_schema))
		var invalid_width := road_manifest.duplicate(true)
		var invalid_width_rows: Array = invalid_width.get("roads", [])
		var invalid_width_row := (invalid_width_rows[0] as Dictionary).duplicate(true)
		invalid_width_row["RoadWidth"] = "0"
		invalid_width_rows[0] = invalid_width_row
		invalid_width["roads"] = invalid_width_rows
		_check("nonpositive_road_width_fails_closed", not _probe_material_document(map_data_script, map_data, invalid_width))
		var invalid_id := road_manifest.duplicate(true)
		var invalid_id_rows: Array = invalid_id.get("roads", [])
		var invalid_id_row := (invalid_id_rows[0] as Dictionary).duplicate(true)
		invalid_id_row["id"] = "FootprintGuess"
		invalid_id_rows[0] = invalid_id_row
		invalid_id["roads"] = invalid_id_rows
		_check("inexact_road_id_fails_closed", not _probe_material_document(map_data_script, map_data, invalid_id))
		var escaped_png := road_manifest.duplicate(true)
		var escaped_png_rows: Array = escaped_png.get("roads", [])
		var escaped_png_row := (escaped_png_rows[0] as Dictionary).duplicate(true)
		escaped_png_row["texturePng"] = "../escaped.png"
		escaped_png_rows[0] = escaped_png_row
		escaped_png["roads"] = escaped_png_rows
		_check("escaped_road_png_fails_closed", not _probe_material_document(map_data_script, map_data, escaped_png))

	print("RETAIL_ROAD_VISUAL_METRICS materials=%d source_edges=%d endpoints=%d shared=%d curve_candidates=%d broad_curves=%d curve_strips=%d vertices=%d triangles=%d meshes=%d" % [
		battlefield.road_material_count,
		battlefield.road_source_edge_count,
		battlefield.road_unique_endpoint_count,
		battlefield.road_shared_node_count,
		battlefield.road_curve_candidate_node_count,
		battlefield.road_generated_broad_curve_count,
		battlefield.road_curve_strip_count,
		battlefield.road_vertex_count,
		battlefield.road_triangle_count,
		battlefield.road_mesh_instance_count,
	])
	battlefield.queue_free()
	await process_frame
	_finish()


func _authored_widths_exact(catalog: Array[Dictionary]) -> bool:
	if catalog.size() != 5:
		return false
	for row in catalog:
		if String(row.get("road_width_authored", "")) != "52" or String(row.get("road_width_in_texture_authored", "")) != "0.95" or not is_equal_approx(float(row.get("road_width", 0.0)), 52.0) or not is_equal_approx(float(row.get("road_width_in_texture", 0.0)), 0.95):
			return false
	return true


func _curve_evidence_exact(battlefield) -> bool:
	var expected_generated := [
		[2, 3], [7, 8], [11, 12], [14, 15], [19, 20], [21, 22],
		[24, 25], [27, 28], [34, 35], [41, 42], [50, 51],
	]
	var expected_fallback := [[0, 1], [28, 29], [32, 33], [46, 47], [62, 63], [66, 67], [69, 70]]
	var generated: Array = []
	for row in battlefield.road_generated_curve_evidence:
		generated.append(Array(row.get("edge_indices", [])))
		if String(row.get("reason", "")) != "generated-broad-curve" or float(row.get("curve_angle_radians", 0.0)) < battlefield.ROAD_MIN_CURVE_ANGLE or float(row.get("start_tangent_source", 0.0)) - float(row.get("overlap_source", 0.0)) > float(row.get("start_edge_length_source", 0.0)) + 0.0001 or float(row.get("end_tangent_source", 0.0)) - float(row.get("overlap_source", 0.0)) > float(row.get("end_edge_length_source", 0.0)) + 0.0001 or int(row.get("strip_count", 0)) <= 0:
			return false
	var fallback: Array = []
	var tangent_fallbacks := 0
	for row in battlefield.road_curve_fallback_evidence:
		fallback.append(Array(row.get("edge_indices", [])))
		var reason := String(row.get("reason", ""))
		if reason == "below-minimum-curve-angle":
			if float(row.get("curve_angle_radians", INF)) >= battlefield.ROAD_MIN_CURVE_ANGLE:
				return false
		elif reason == "tangent-exceeds-edge":
			tangent_fallbacks += 1
			var start_exceeds := float(row.get("start_tangent_source", 0.0)) - float(row.get("overlap_source", 0.0)) > float(row.get("start_edge_length_source", 0.0))
			var end_exceeds := float(row.get("end_tangent_source", 0.0)) - float(row.get("overlap_source", 0.0)) > float(row.get("end_edge_length_source", 0.0))
			if not start_exceeds and not end_exceeds:
				return false
		else:
			return false
	generated.sort_custom(func(a: Array, b: Array) -> bool: return int(a[0]) < int(b[0]) or (int(a[0]) == int(b[0]) and int(a[1]) < int(b[1])))
	fallback.sort_custom(func(a: Array, b: Array) -> bool: return int(a[0]) < int(b[0]) or (int(a[0]) == int(b[0]) and int(a[1]) < int(b[1])))
	expected_generated.sort_custom(func(a: Array, b: Array) -> bool: return int(a[0]) < int(b[0]) or (int(a[0]) == int(b[0]) and int(a[1]) < int(b[1])))
	expected_fallback.sort_custom(func(a: Array, b: Array) -> bool: return int(a[0]) < int(b[0]) or (int(a[0]) == int(b[0]) and int(a[1]) < int(b[1])))
	return generated == expected_generated and fallback == expected_fallback and tangent_fallbacks == 1


func _catalog_matches_provenance(map_data, mod_loader) -> bool:
	var manifest_value: Variant = mod_loader._read_json(map_data.provenance_manifest_path)
	if typeof(manifest_value) != TYPE_DICTIONARY:
		return false
	var by_path: Dictionary = {}
	for value in Array((manifest_value as Dictionary).get("bundle_files", [])):
		if typeof(value) == TYPE_DICTIONARY:
			var row := value as Dictionary
			by_path[String(row.get("path", ""))] = row
	for row in map_data.road_material_catalog:
		var relative := String(row.get("texture_relative", ""))
		var path := String(row.get("texture_path", ""))
		if not by_path.has(relative) or not mod_loader.path_is_within(map_data.pack_root, path) or not mod_loader.path_is_within(map_data.map_root, path):
			return false
		var provenance: Dictionary = by_path[relative]
		if String(provenance.get("sha256", "")) != String(row.get("texture_sha256", "")) or int(provenance.get("size", -1)) != int(row.get("texture_byte_length", -1)):
			return false
		var bytes := FileAccess.get_file_as_bytes(path)
		var image := Image.new()
		if bytes.is_empty() or image.load_png_from_buffer(bytes) != OK or image.is_empty():
			return false
	return true


func _materials_are_exact(container: Node3D) -> bool:
	for child in container.get_children():
		var mesh_instance := child as MeshInstance3D
		if mesh_instance == null or mesh_instance.mesh == null:
			return false
		var material := mesh_instance.mesh.surface_get_material(0) as StandardMaterial3D
		if material == null or material.albedo_texture == null or material.transparency != BaseMaterial3D.TRANSPARENCY_ALPHA or material.depth_draw_mode != BaseMaterial3D.DEPTH_DRAW_DISABLED or material.no_depth_test or material.cull_mode != BaseMaterial3D.CULL_DISABLED or material.texture_filter != BaseMaterial3D.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC or not material.texture_repeat:
			return false
	return true


func _all_road_vertices_clear_terrain(container: Node3D, map_data) -> bool:
	for child in container.get_children():
		var mesh_instance := child as MeshInstance3D
		if mesh_instance == null or mesh_instance.mesh == null:
			return false
		var arrays: Array = mesh_instance.mesh.surface_get_arrays(0)
		var vertices := arrays[Mesh.ARRAY_VERTEX] as PackedVector3Array
		for vertex in vertices:
			if vertex.y + 0.000001 < map_data.local_ground_height(Vector2(vertex.x, vertex.z)):
				return false
	return true


func _probe_material_document(map_data_script, source, document: Dictionary) -> bool:
	var probe = map_data_script.new()
	probe.pack_root = source.pack_root
	probe.map_root = source.map_root
	probe.road_type_count = source.road_type_count
	probe.road_type_ids.assign(source.road_type_ids)
	probe.road_materials_path = source.road_materials_path
	return bool(probe._load_road_materials(document))


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_ROAD_VISUAL PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_ROAD_VISUAL FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	if finished:
		return
	finished = true
	print("RETAIL_ROAD_VISUAL_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _watchdog_timeout() -> void:
	if finished:
		return
	failed += 1
	printerr("RETAIL_ROAD_VISUAL FAIL watchdog_timeout")
	_finish()

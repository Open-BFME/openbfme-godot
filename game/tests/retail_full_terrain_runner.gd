extends SceneTree
## Focused gate for the full-resolution, non-placeholder Fords battlefield.

const MAP_ID := "bfme2.map.fords-of-isen-ii"
const EXPECTED_WIDTH := 415
const EXPECTED_HEIGHT := 353
const EXPECTED_VERTICES := 146_495
const EXPECTED_QUADS := 145_728
const EXPECTED_TRIANGLES := 291_456
const EXPECTED_IMPASSABLE := 18_325

var passed := 0
var failed := 0
var finished := false


func _initialize() -> void:
	create_timer(60.0, true, false, true).timeout.connect(_watchdog_timeout)
	call_deferred("_run")


func _run() -> void:
	var content_db = root.get_node_or_null("ContentDB")
	var mod_loader = root.get_node_or_null("ModLoader")
	var map_data_script = load("res://src/retail_slice/retail_map_data.gd")
	var battlefield_script = load("res://src/retail_slice/retail_fords_battlefield.gd")
	_check("terrain_runner_dependencies", content_db != null and mod_loader != null and map_data_script != null and battlefield_script != null)
	if content_db == null or mod_loader == null or map_data_script == null or battlefield_script == null:
		_finish()
		return

	var external_root := OS.get_environment("OPENBFME_CONTENT").replace("\\", "/").trim_suffix("/")
	var selection_path := external_root.path_join("selection.json") if external_root != "" else ""
	var selected_pack_root: String = mod_loader.selected_user_pack_root(external_root, selection_path) if selection_path != "" else ""
	var selected_pack_value: Variant = mod_loader._read_json(selected_pack_root.path_join("pack.json")) if selected_pack_root != "" else null
	var selected_entry_map := String(((selected_pack_value as Dictionary).get("files", {}) as Dictionary).get("entryMap", "")) if typeof(selected_pack_value) == TYPE_DICTIONARY else ""
	var selected_map_path: String = mod_loader.resolve_pack_path(selected_pack_root, selected_entry_map) if selected_pack_root != "" else ""
	var selected_map_value: Variant = mod_loader._read_json(selected_map_path) if selected_map_path != "" else null
	var selected_map_definition: Dictionary = (selected_map_value as Dictionary).duplicate(true) if typeof(selected_map_value) == TYPE_DICTIONARY else {}
	_check("currently_selected_private_map_pack", selected_pack_root != "" and not selected_pack_root.begins_with("res://") and DirAccess.dir_exists_absolute(selected_pack_root) and selected_map_path != "" and mod_loader.path_is_within(selected_pack_root, selected_map_path), selected_pack_root)
	# Host identity is "the selected pack is a converted retail pack that
	# provides Men", not "its id is the single-faction literal": composed packs
	# are id'd `bfme2-<a>-<b>-…-vslice` and declare their factions in pack.json
	# `factionImportCoverage`.
	_check(
		"selected_pack_hosts_men_vertical_slice",
		typeof(selected_pack_value) == TYPE_DICTIONARY
			and bool(mod_loader.call("pack_is_retail_import", selected_pack_root))
			and bool(mod_loader.call("pack_provides_faction", selected_pack_root, "men"))
			and String(selected_map_definition.get("id", "")) == MAP_ID,
		selected_pack_root
	)
	if selected_map_definition.is_empty() or selected_pack_root == "" or typeof(selected_pack_value) != TYPE_DICTIONARY:
		_finish()
		return

	# Before the composed main pack is selected, integration can point this
	# runner at the already-audited road overlay. Its terrain contract must be
	# byte-identical at the document level to the selected pack. Normal handoff
	# runs omit the override and exercise the selected pack directly.
	var explicit_runtime_root := OS.get_environment("OPENBFME_TERRAIN_PACK").replace("\\", "/").trim_suffix("/")
	var pack_root := explicit_runtime_root if explicit_runtime_root != "" else selected_pack_root
	var pack_value: Variant = mod_loader._read_json(pack_root.path_join("pack.json"))
	var entry_map := String(((pack_value as Dictionary).get("files", {}) as Dictionary).get("entryMap", "")) if typeof(pack_value) == TYPE_DICTIONARY else ""
	var map_path: String = mod_loader.resolve_pack_path(pack_root, entry_map) if pack_root != "" else ""
	var map_value: Variant = mod_loader._read_json(map_path) if map_path != "" else null
	var map_definition: Dictionary = (map_value as Dictionary).duplicate(true) if typeof(map_value) == TYPE_DICTIONARY else {}
	if not map_definition.is_empty():
		map_definition["_source"] = map_path
		map_definition["_pack_root"] = pack_root
	_check("runtime_pack_is_selected_or_explicit_private_fixture", pack_root == selected_pack_root or (explicit_runtime_root != "" and not pack_root.begins_with("res://") and DirAccess.dir_exists_absolute(pack_root)), pack_root)
	_check("explicit_fixture_preserves_selected_terrain_contract", pack_root == selected_pack_root or _terrain_contract_documents_match(selected_map_path, selected_map_definition, map_path, map_definition, mod_loader))
	if map_definition.is_empty() or typeof(pack_value) != TYPE_DICTIONARY:
		_finish()
		return

	var map_data = map_data_script.new()
	_check("runtime_fords_map_loads", bool(map_data.load_from_pack(pack_root, map_definition)), String(map_data.error))
	if not map_data.ready:
		_finish()
		return
	_check("source_grid_dimensions_exact", int(map_data.width) == EXPECTED_WIDTH and int(map_data.height) == EXPECTED_HEIGHT)
	_check("source_height_and_passability_payloads_exact", map_data.height_samples.size() == EXPECTED_VERTICES * 2 and int(map_data.impassable_count) == EXPECTED_IMPASSABLE)

	var battlefield = battlefield_script.new()
	battlefield.name = "FocusedExactTerrainBattlefield"
	root.add_child(battlefield)
	var build_started_ms := Time.get_ticks_msec()
	var configured := bool(battlefield.configure(map_data))
	var build_elapsed_ms := Time.get_ticks_msec() - build_started_ms
	_check("full_battlefield_builds_from_runtime_pack", configured, String(battlefield.error))
	_check("exact_battlefield_build_time_is_practical", build_elapsed_ms <= 5000, "%d ms" % build_elapsed_ms)
	if not configured or battlefield.terrain_mesh_instance == null or battlefield.terrain_mesh_instance.mesh == null:
		battlefield.queue_free()
		await process_frame
		_finish()
		return

	var terrain_mesh: ArrayMesh = battlefield.terrain_mesh_instance.mesh as ArrayMesh
	var arrays: Array = terrain_mesh.surface_get_arrays(0) if terrain_mesh != null and terrain_mesh.get_surface_count() == 1 else []
	var vertices := arrays[Mesh.ARRAY_VERTEX] as PackedVector3Array if arrays.size() == Mesh.ARRAY_MAX else PackedVector3Array()
	var normals := arrays[Mesh.ARRAY_NORMAL] as PackedVector3Array if arrays.size() == Mesh.ARRAY_MAX else PackedVector3Array()
	var uvs := arrays[Mesh.ARRAY_TEX_UV] as PackedVector2Array if arrays.size() == Mesh.ARRAY_MAX else PackedVector2Array()
	var colors := arrays[Mesh.ARRAY_COLOR] as PackedColorArray if arrays.size() == Mesh.ARRAY_MAX else PackedColorArray()
	var indices := arrays[Mesh.ARRAY_INDEX] as PackedInt32Array if arrays.size() == Mesh.ARRAY_MAX else PackedInt32Array()
	_check("every_source_sample_is_a_mesh_vertex", bool(battlefield.terrain_exact_grid_ready) and int(battlefield.terrain_vertex_count) == EXPECTED_VERTICES and vertices.size() == EXPECTED_VERTICES)
	_check("every_source_quad_is_two_triangles", int(battlefield.terrain_triangle_count) == EXPECTED_TRIANGLES and indices.size() == EXPECTED_TRIANGLES * 3 and int(battlefield.terrain_mesh_instance.get_meta("source_quad_count", -1)) == EXPECTED_QUADS)
	_check("full_grid_attributes_have_exact_cardinality", normals.size() == EXPECTED_VERTICES and uvs.size() == EXPECTED_VERTICES and colors.size() == EXPECTED_VERTICES)
	_check("source_sample_positions_and_uvs_match", _representative_grid_samples_match(vertices, uvs, map_data))
	_check("source_corner_heights_match", _source_corner_heights_match(map_data))
	_check("source_quad_diagonal_and_winding_match", _representative_quad_indices_match(indices, EXPECTED_WIDTH, EXPECTED_HEIGHT))
	_check("source_normals_are_finite_and_upward", _representative_normals_are_upward(normals, EXPECTED_WIDTH, EXPECTED_HEIGHT))
	_check("full_passability_popcount_is_preserved", int(battlefield.impassable_vertex_count) == EXPECTED_IMPASSABLE and int(battlefield.impassable_vertex_count) == int(map_data.impassable_count))

	var material := terrain_mesh.surface_get_material(0) as ShaderMaterial
	var texture_array := material.get_shader_parameter("terrain_textures") as Texture2DArray if material != null else null
	var tile_data := material.get_shader_parameter("terrain_tile_data") as Texture2D if material != null else null
	var blend_data := material.get_shader_parameter("terrain_blend_data") as Texture2D if material != null else null
	_check("retail_terrain_material_is_bound", bool(battlefield.terrain_material_source_driven) and material == battlefield.terrain_material and String(material.get_meta("source", "")) == "cooked-retail-terrain-materials-and-sage-blends" if material != null else false)
	_check("retail_texture_array_covers_catalog", texture_array != null and texture_array.get_layers() == int(map_data.terrain_texture_count) and int(battlefield.terrain_texture_array_size) == int(map_data.terrain_texture_count))
	_check("retail_material_data_covers_full_grid", tile_data != null and blend_data != null and tile_data.get_width() == EXPECTED_WIDTH and tile_data.get_height() == EXPECTED_HEIGHT and blend_data.get_width() == EXPECTED_WIDTH and blend_data.get_height() == EXPECTED_HEIGHT and int(battlefield.terrain_tile_data_cell_count) == EXPECTED_VERTICES)

	_check("ford_gate_facts_are_metadata_only", int(battlefield.ford_marker_count) == 3 and battlefield.ford_gate_diagnostics.size() == 3 and int(battlefield.get_meta("source_ford_gate_count", -1)) == 3 and _visible_named_placeholder_count(battlefield, "SourceFord_") == 0)
	_check("unresolved_prop_scoreboard_is_preserved", battlefield.unresolved_prop_type_ids == map_data.unresolved_prop_type_ids and int(battlefield.unresolved_prop_placement_count) == int(map_data.unresolved_prop_placement_count) and int(battlefield.get_meta("source_unresolved_prop_placement_count", -1)) == int(map_data.unresolved_prop_placement_count))
	_check("unresolved_prop_preview_is_metadata_only", int(battlefield.generic_prop_count) == map_data.generic_prop_placements.size() and int(battlefield.get_meta("source_unresolved_prop_sample_count", -1)) == int(battlefield.generic_prop_count) and _diagnostic_sample_count(battlefield) == int(battlefield.generic_prop_count) and _visible_unresolved_placeholder_count(battlefield) == 0)
	_check("no_generic_primitive_placeholder_meshes", _generic_placeholder_primitive_count(battlefield) == 0)

	_check("road_integration_remains_source_driven", bool(battlefield.road_material_source_driven) and int(battlefield.road_source_edge_count) == 71 and int(battlefield.road_material_count) == 5 and int(battlefield.road_mesh_instance_count) == 5)
	_check("road_curve_and_mesh_counts_are_preserved", int(battlefield.road_generated_broad_curve_count) == 11 and int(battlefield.road_straight_fallback_node_count) == 7 and int(battlefield.road_render_strip_count) == 95 and int(battlefield.road_vertex_count) == 534 and int(battlefield.road_triangle_count) == 344)

	var oversized_probe = battlefield_script.new()
	var original_width := int(map_data.width)
	var original_height := int(map_data.height)
	map_data.width = 1000
	map_data.height = 1000
	_check("oversized_exact_grid_fails_closed", not bool(oversized_probe._build_terrain(map_data)) and String(oversized_probe.error).contains("runtime bound") and oversized_probe.get_child_count() == 0, String(oversized_probe.error))
	map_data.width = original_width
	map_data.height = original_height
	oversized_probe.free()

	var inconsistent_probe = battlefield_script.new()
	var original_heightmap_bytes := int(map_data.heightmap_bytes)
	map_data.heightmap_bytes = original_heightmap_bytes - 2
	_check("inconsistent_exact_grid_fails_closed", not bool(inconsistent_probe._build_terrain(map_data)) and String(inconsistent_probe.error).contains("height samples") and inconsistent_probe.get_child_count() == 0, String(inconsistent_probe.error))
	map_data.heightmap_bytes = original_heightmap_bytes
	inconsistent_probe.free()

	print("RETAIL_FULL_TERRAIN_METRICS width=%d height=%d vertices=%d quads=%d triangles=%d impassable=%d build_ms=%d roads=%d unresolved=%d" % [
		map_data.width,
		map_data.height,
		battlefield.terrain_vertex_count,
		EXPECTED_QUADS,
		battlefield.terrain_triangle_count,
		battlefield.impassable_vertex_count,
		build_elapsed_ms,
		battlefield.road_source_edge_count,
		battlefield.unresolved_prop_placement_count,
	])
	battlefield.queue_free()
	var asset_factory = load("res://src/view/asset_factory.gd")
	if asset_factory != null:
		asset_factory.clear_mesh_cache()
	await process_frame
	await process_frame
	_finish()


func _terrain_contract_documents_match(selected_map_path: String, selected_definition: Dictionary, runtime_map_path: String, runtime_definition: Dictionary, mod_loader) -> bool:
	if selected_map_path == "" or runtime_map_path == "":
		return false
	var selected_root := selected_map_path.get_base_dir()
	var runtime_root := runtime_map_path.get_base_dir()
	var selected_terrain: Variant = mod_loader._read_json(selected_root.path_join(String(selected_definition.get("terrain", ""))))
	var runtime_terrain: Variant = mod_loader._read_json(runtime_root.path_join(String(runtime_definition.get("terrain", ""))))
	var selected_materials: Variant = mod_loader._read_json(selected_root.path_join(String(selected_definition.get("terrainMaterials", ""))))
	var runtime_materials: Variant = mod_loader._read_json(runtime_root.path_join(String(runtime_definition.get("terrainMaterials", ""))))
	return (
		typeof(selected_terrain) == TYPE_DICTIONARY
		and typeof(runtime_terrain) == TYPE_DICTIONARY
		and selected_terrain == runtime_terrain
		and typeof(selected_materials) == TYPE_DICTIONARY
		and typeof(runtime_materials) == TYPE_DICTIONARY
		and selected_materials == runtime_materials
	)


func _representative_grid_samples_match(vertices: PackedVector3Array, uvs: PackedVector2Array, map_data) -> bool:
	for cell_value in [Vector2i(0, 0), Vector2i(414, 0), Vector2i(0, 352), Vector2i(414, 352), Vector2i(20, 20), Vector2i(208, 142), Vector2i(200, 176)]:
		var cell := Vector2i(cell_value)
		var index: int = cell.y * EXPECTED_WIDTH + cell.x
		if index < 0 or index >= vertices.size() or not vertices[index].is_equal_approx(map_data.terrain_local_at(cell.x, cell.y)) or uvs[index] != Vector2(cell.x, cell.y):
			return false
	return true


func _source_corner_heights_match(map_data) -> bool:
	return (
		int(map_data.height_raw_at(0, 0)) == 7680
		and int(map_data.height_raw_at(414, 0)) == 7895
		and int(map_data.height_raw_at(0, 352)) == 7680
		and int(map_data.height_raw_at(414, 352)) == 7652
		and int(map_data.height_raw_at(208, 142)) == 7168
		and int(map_data.height_raw_at(200, 176)) == 7542
	)


func _representative_quad_indices_match(indices: PackedInt32Array, width: int, height: int) -> bool:
	if indices.size() != (width - 1) * (height - 1) * 6:
		return false
	for cell_value in [Vector2i(0, 0), Vector2i(207, 176), Vector2i(width - 2, height - 2)]:
		var cell := Vector2i(cell_value)
		var quad_index: int = cell.y * (width - 1) + cell.x
		var offset: int = quad_index * 6
		var top_left: int = cell.y * width + cell.x
		var expected := PackedInt32Array([
			top_left,
			top_left + 1,
			top_left + width,
			top_left + width,
			top_left + 1,
			top_left + width + 1,
		])
		for index in range(6):
			if indices[offset + index] != expected[index]:
				return false
	return true


func _representative_normals_are_upward(normals: PackedVector3Array, width: int, height: int) -> bool:
	for cell_value in [Vector2i(0, 0), Vector2i(width - 1, 0), Vector2i(0, height - 1), Vector2i(width - 1, height - 1), Vector2i(208, 142), Vector2i(200, 176)]:
		var cell := Vector2i(cell_value)
		var normal := normals[cell.y * width + cell.x]
		if not normal.is_finite() or normal.y <= 0.0 or not is_equal_approx(normal.length(), 1.0):
			return false
	return true


func _visible_named_placeholder_count(node: Node, name_prefix: String) -> int:
	var result := 0
	var stack: Array[Node] = [node]
	while not stack.is_empty():
		var current: Node = stack.pop_back()
		if current is GeometryInstance3D and String(current.name).begins_with(name_prefix):
			result += 1
		for child in current.get_children():
			stack.append(child)
	return result


func _visible_unresolved_placeholder_count(node: Node) -> int:
	var result := 0
	var stack: Array[Node] = [node]
	while not stack.is_empty():
		var current: Node = stack.pop_back()
		if current is GeometryInstance3D and String(current.get_meta("presentation", "")) == "unresolved-marker":
			result += 1
		for child in current.get_children():
			stack.append(child)
	return result


func _generic_placeholder_primitive_count(node: Node) -> int:
	var result := 0
	var stack: Array[Node] = [node]
	while not stack.is_empty():
		var current: Node = stack.pop_back()
		if current is MeshInstance3D:
			var mesh := (current as MeshInstance3D).mesh
			if mesh is CylinderMesh or mesh is SphereMesh or mesh is BoxMesh and String(current.name).begins_with("SourceFord_"):
				result += 1
		elif current is MultiMeshInstance3D:
			var multimesh := (current as MultiMeshInstance3D).multimesh
			if multimesh != null and (multimesh.mesh is CylinderMesh or multimesh.mesh is SphereMesh):
				result += 1
		for child in current.get_children():
			stack.append(child)
	return result


func _diagnostic_sample_count(battlefield: Node) -> int:
	var result := 0
	for node_name in ["SourceVegetationPlacementMarkers", "SourceRockPlacementMarkers"]:
		var diagnostic := battlefield.find_child(node_name, true, false)
		if diagnostic != null:
			if diagnostic is GeometryInstance3D or not bool(diagnostic.get_meta("diagnostic_only", false)):
				return -1
			result += int(diagnostic.get_meta("source_placement_count", -1))
	return result


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_FULL_TERRAIN PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_FULL_TERRAIN FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	if finished:
		return
	finished = true
	print("RETAIL_FULL_TERRAIN_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _watchdog_timeout() -> void:
	if finished:
		return
	failed += 1
	printerr("RETAIL_FULL_TERRAIN FAIL watchdog_timeout")
	_finish()

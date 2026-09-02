class_name NativeTerrain
extends Node3D
## Retail map presentation over the native core's read-only map-v1 height grid.

const PlainTerrainScript := preload("res://src/map/map_terrain_mesh.gd")
const FORDS_AFTERNOON_TERRAIN_LIGHTS: Array[Dictionary] = [
	{
		"ambient": Vector3(0.04313725605607033, 0.03529411926865578, 0.04313725605607033),
		"color": Vector3(0.6235294342041016, 0.49803921580314636, 0.45098039507865906),
		"direction": Vector3(0.2649596631526947, 0.4240240752696991, -0.8660253882408142),
	},
	{
		"ambient": Vector3.ZERO,
		"color": Vector3(0.2235294133424759, 0.3333333432674408, 0.29019609093666077),
		"direction": Vector3(0.5065234899520874, -0.6036512851715088, -0.6156614422798157),
	},
	{
		"ambient": Vector3(0.04313725605607033, 0.03529411926865578, 0.04313725605607033),
		"color": Vector3(0.6235294342041016, 0.49803921580314636, 0.45098039507865906),
		"direction": Vector3(0.2649596631526947, 0.4240240752696991, -0.8660253882408142),
	},
]

var error := ""
var presentation_path := "unconfigured"
var cooked_map_path := ""
var pack_root := ""
var texture_layer_count := 0
var primary_blend_cell_count := 0
var three_way_blend_cell_count := 0
var cliff_cell_count := 0
var water_polygon_count := 0
var water_triangle_count := 0
var water_focus_world := Vector3.ZERO
var terrain_light_count := 0
var map_data
var terrain_mesh: MeshInstance3D
var water_mesh: MeshInstance3D
var weather_fx
var _terrain_material: ShaderMaterial
var _plain_fallback


func configure(map_v1, content_root: String = "") -> bool:
	_clear_generated()
	if map_v1 == null or map_v1.grid_width < 2 or map_v1.grid_height < 2:
		return _fail("native terrain requires a loaded map-v1 document")
	var root := content_root.strip_edges()
	if root.is_empty():
		root = OS.get_environment("OPENBFME_CONTENT").strip_edges()
	var resolved := resolve_cooked_map(root, map_v1.source)
	if resolved.is_empty():
		_plain_fallback = PlainTerrainScript.new()
		_plain_fallback.name = "PlainMapV1Fallback"
		add_child(_plain_fallback)
		if not _plain_fallback.build(map_v1):
			return _fail("plain map-v1 fallback could not be built")
		presentation_path = "plain-map-v1-fallback"
		print("NATIVE_TERRAIN_PATH path=%s cooked=<missing>" % presentation_path)
		return true
	pack_root = String(resolved.get("pack_root", ""))
	cooked_map_path = String(resolved.get("map_path", ""))
	var definition := (resolved.get("definition", {}) as Dictionary).duplicate(true)
	definition["_source"] = cooked_map_path
	definition["_pack_root"] = pack_root
	var map_data_script = load("res://src/retail_slice/retail_map_data.gd")
	if map_data_script == null:
		return _fail("retail map data module did not parse")
	map_data = map_data_script.new()
	if not map_data.load_from_pack(pack_root, definition):
		return _fail("cooked map refused: %s" % map_data.error)
	if map_data.width != map_v1.grid_width or map_data.height != map_v1.grid_height:
		return _fail("cooked/map-v1 grid mismatch cooked=%dx%d map-v1=%dx%d" % [
			map_data.width, map_data.height, map_v1.grid_width, map_v1.grid_height
		])
	if not _build_textured_height_grid(map_v1):
		return false
	if not _build_water(map_v1):
		return false
	weather_fx = load("res://src/retail_slice/retail_weather_fx.gd").new()
	weather_fx.name = "RetailWeatherFx"
	add_child(weather_fx)
	var weather_error: String = weather_fx.configure(map_data.map_weather_name, 1.0)
	if not weather_error.is_empty():
		return _fail(weather_error)
	presentation_path = "cooked-retail-map"
	print("NATIVE_TERRAIN_PATH path=%s cooked=%s textures=%d water=%d" % [
		presentation_path, cooked_map_path, texture_layer_count, water_polygon_count
	])
	return true


func set_passability_overlay(enabled: bool) -> void:
	if _terrain_material != null:
		_terrain_material.set_shader_parameter("passability_debug_enabled", enabled)
	if _plain_fallback != null:
		_plain_fallback.set_passability_overlay(enabled)


func set_camera_anchor(world_position: Vector3) -> void:
	if weather_fx != null:
		weather_fx.set_camera_anchor(world_position)


static func resolve_cooked_map(content_root: String, source: Dictionary) -> Dictionary:
	if content_root.is_empty() or not DirAccess.dir_exists_absolute(content_root):
		return {}
	var selection := _read_json(content_root.path_join("selection.json"))
	var references: Array[String] = []
	var active := String(selection.get("activePack", ""))
	if not active.is_empty():
		references.append(active)
	for value in selection.get("supplementalPacks", []) as Array:
		references.append(String(value))
	var wanted_sha := String(source.get("sha256", "")).to_lower()
	var wanted_slug := String(source.get("path", "")).to_lower().replace("\\", "/")
	for reference in references:
		if not _safe_relative(reference):
			continue
		var candidate_root := content_root.path_join(reference)
		var pack := _read_json(candidate_root.path_join("pack.json"))
		var files := pack.get("files", {}) as Dictionary
		var catalog_relative := String(files.get("mapCatalog", ""))
		if catalog_relative.is_empty() or not _safe_relative(catalog_relative):
			continue
		var catalog := _read_json(candidate_root.path_join(catalog_relative))
		for row_value in catalog.get("maps", []) as Array:
			if not (row_value is Dictionary):
				continue
			var row := row_value as Dictionary
			var relative := String(row.get("map", ""))
			if relative.is_empty() or not _safe_relative(relative):
				continue
			var map_path := candidate_root.path_join(relative)
			var definition := _read_json(map_path)
			var identity := definition.get("source", {}) as Dictionary
			var sha_matches := not wanted_sha.is_empty() and String(identity.get("sha256", "")).to_lower() == wanted_sha
			var slug_matches := wanted_slug.contains("fords of isen ii") and String(definition.get("id", "")).to_lower().contains("fords-of-isen-ii")
			if sha_matches or slug_matches:
				return {"pack_root": candidate_root, "map_path": map_path, "definition": definition}
	return {}


func _build_textured_height_grid(map_v1) -> bool:
	var width: int = map_v1.grid_width
	var height: int = map_v1.grid_height
	var vertices := PackedVector3Array()
	var normals := PackedVector3Array()
	var uvs := PackedVector2Array()
	var colors := PackedColorArray()
	var indices := PackedInt32Array()
	vertices.resize(width * height)
	normals.resize(width * height)
	uvs.resize(width * height)
	colors.resize(width * height)
	for y in height:
		for x in width:
			var slot := y * width + x
			vertices[slot] = Vector3(float(x * map_v1.cell_size), map_v1.height_at_grid(x, y), float(y * map_v1.cell_size))
			uvs[slot] = Vector2(float(x), float(y))
			colors[slot] = Color(1.0, 0.0, 0.0, 1.0) if map_v1.is_impassable_at(x, y) else Color(0.0, 0.0, 0.0, 1.0)
	for y in height:
		for x in width:
			var slot := y * width + x
			var left: float = map_v1.height_at_grid(x - 1, y)
			var right: float = map_v1.height_at_grid(x + 1, y)
			var down: float = map_v1.height_at_grid(x, y - 1)
			var up: float = map_v1.height_at_grid(x, y + 1)
			normals[slot] = Vector3(left - right, float(map_v1.cell_size) * 2.0, down - up).normalized()
	indices.resize((width - 1) * (height - 1) * 6)
	var cursor := 0
	for y in height - 1:
		for x in width - 1:
			var top_left := y * width + x
			indices[cursor] = top_left
			indices[cursor + 1] = top_left + 1
			indices[cursor + 2] = top_left + width
			indices[cursor + 3] = top_left + width
			indices[cursor + 4] = top_left + 1
			indices[cursor + 5] = top_left + width + 1
			cursor += 6
	var arrays: Array = []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices
	arrays[Mesh.ARRAY_NORMAL] = normals
	arrays[Mesh.ARRAY_TEX_UV] = uvs
	arrays[Mesh.ARRAY_COLOR] = colors
	arrays[Mesh.ARRAY_INDEX] = indices
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	var builder = load("res://src/retail_slice/retail_terrain_material_builder.gd").new()
	_terrain_material = builder.build(map_data)
	if _terrain_material == null:
		return _fail("retail terrain material refused: %s" % builder.error)
	_apply_source_terrain_lighting()
	mesh.surface_set_material(0, _terrain_material)
	terrain_mesh = MeshInstance3D.new()
	terrain_mesh.name = "MapV1RetailTerrain"
	terrain_mesh.mesh = mesh
	terrain_mesh.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
	terrain_mesh.set_meta("source", "map-v1-height+cooked-retail-tile-blend-cliff-layers")
	add_child(terrain_mesh)
	texture_layer_count = int(builder.texture_array_size)
	primary_blend_cell_count = map_data.terrain_nonzero_blend_cell_count
	three_way_blend_cell_count = map_data.terrain_nonzero_three_way_blend_cell_count
	cliff_cell_count = map_data.terrain_nonzero_cliff_cell_count
	return texture_layer_count > 0


func _apply_source_terrain_lighting() -> void:
	# RetailTerrainMaterialBuilder deliberately starts fail-dark. The placeholder
	# scene binds this exact source-authored Fords afternoon rig after building;
	# the native adapter must do the same or every valid texture samples black.
	var ambient := Vector3.ZERO
	for row in FORDS_AFTERNOON_TERRAIN_LIGHTS:
		ambient += row.ambient as Vector3
	_terrain_material.set_shader_parameter("sage_ambient_color", ambient)
	for index in FORDS_AFTERNOON_TERRAIN_LIGHTS.size():
		var row := FORDS_AFTERNOON_TERRAIN_LIGHTS[index]
		_terrain_material.set_shader_parameter("sage_light_%d_color" % index, row.color as Vector3)
		_terrain_material.set_shader_parameter(
			"sage_light_%d_direction" % index,
			_sage_direction_to_local(row.direction as Vector3)
		)
	_terrain_material.set_meta("sage_ambient_color", ambient)
	_terrain_material.set_meta("sage_lighting_model", "opensage-terrain-three-light-lambert")
	terrain_light_count = FORDS_AFTERNOON_TERRAIN_LIGHTS.size()


func _sage_direction_to_local(source_direction: Vector3) -> Vector3:
	var godot_horizontal := Vector2(source_direction.x, -source_direction.y)
	var local_direction := Vector3(
		godot_horizontal.dot(map_data.local_axis_x),
		source_direction.z,
		godot_horizontal.dot(map_data.local_axis_z)
	)
	return local_direction.normalized() if local_direction.length_squared() > 0.0 else Vector3.DOWN


func _build_water(map_v1) -> bool:
	water_polygon_count = map_data.standing_water_polygons.size() + map_data.river_strips.size()
	if water_polygon_count == 0:
		return true
	var vertices := PackedVector3Array()
	var normals := PackedVector3Array()
	var colors := PackedColorArray()
	var indices := PackedInt32Array()
	for polygon_value in map_data.standing_water_polygons:
		var polygon := polygon_value as PackedVector3Array
		var flat := PackedVector2Array()
		for point in polygon:
			var native_point := _cooked_point_to_map_v1(point, map_v1)
			flat.append(Vector2(native_point.x, native_point.z))
		var triangles := Geometry2D.triangulate_polygon(flat)
		var base := vertices.size()
		for point in polygon:
			vertices.append(_cooked_point_to_map_v1(point, map_v1) + Vector3.UP * 0.025)
			normals.append(Vector3.UP)
			colors.append(Color.WHITE)
		for triangle in triangles:
			indices.append(base + int(triangle))
	for river in map_data.river_strips:
		var sections := river.get("sections", []) as Array
		for section_index in maxi(0, sections.size() - 1):
			var first := sections[section_index] as PackedVector3Array
			var second := sections[section_index + 1] as PackedVector3Array
			var base := vertices.size()
			for point in [first[0], first[1], second[0], second[1]]:
				vertices.append(_cooked_point_to_map_v1(point as Vector3, map_v1) + Vector3.UP * 0.03)
				normals.append(Vector3.UP)
				colors.append(Color.WHITE)
			indices.append_array(PackedInt32Array([base, base + 2, base + 1, base + 1, base + 2, base + 3]))
	if vertices.is_empty() or indices.is_empty():
		return _fail("cooked map declares water but produced no triangles")
	var arrays: Array = []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices
	arrays[Mesh.ARRAY_NORMAL] = normals
	arrays[Mesh.ARRAY_COLOR] = colors
	arrays[Mesh.ARRAY_INDEX] = indices
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	var water_builder = load("res://src/retail_slice/retail_water_surface.gd").new()
	var water_row: Dictionary = {}
	if not map_data.standing_water_materials.is_empty():
		water_row = map_data.standing_water_materials[0]
	elif not map_data.river_strips.is_empty():
		water_row = map_data.river_strips[0]
	var material: ShaderMaterial = water_builder.build_material(map_data.active_time_of_day, pack_root, water_row)
	if material == null:
		return _fail("retail water material refused: %s" % water_builder.error)
	mesh.surface_set_material(0, material)
	water_mesh = MeshInstance3D.new()
	water_mesh.name = "CookedRetailWater"
	water_mesh.mesh = mesh
	water_mesh.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(water_mesh)
	water_triangle_count = indices.size() / 3
	for point in vertices:
		water_focus_world += point
	water_focus_world /= float(vertices.size())
	return true


func _cooked_point_to_map_v1(point: Vector3, map_v1) -> Vector3:
	# RetailMapData presents geometry in its player-start-centred local frame.
	# The native host and map-v1 use the raw height-grid frame (origin at grid
	# 0,0), so convert horizontal coordinates back through the cooked grid and
	# undo the local elevation scale before sharing the geometry with snapshots.
	var grid: Vector2 = map_data.local_to_grid_float(Vector2(point.x, point.z))
	var source_height: float = (
		point.y / map_data.local_transform_scale + map_data.reference_elevation
		if map_data.local_transform_scale > 0.0
		else point.y
	)
	return Vector3(grid.x * float(map_v1.cell_size), source_height, grid.y * float(map_v1.cell_size))


static func _read_json(path: String) -> Dictionary:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return {}
	var parsed: Variant = JSON.parse_string(file.get_as_text())
	file.close()
	return parsed as Dictionary if parsed is Dictionary else {}


static func _safe_relative(value: String) -> bool:
	var path := value.replace("\\", "/")
	return not path.is_empty() and not path.begins_with("/") and not path.contains(":") and not path.split("/").has("..")


func _clear_generated() -> void:
	error = ""
	presentation_path = "unconfigured"
	cooked_map_path = ""
	pack_root = ""
	texture_layer_count = 0
	primary_blend_cell_count = 0
	three_way_blend_cell_count = 0
	cliff_cell_count = 0
	water_polygon_count = 0
	water_triangle_count = 0
	water_focus_world = Vector3.ZERO
	terrain_light_count = 0
	for child in get_children():
		remove_child(child)
		child.queue_free()
	terrain_mesh = null
	water_mesh = null
	weather_fx = null
	_plain_fallback = null


func _fail(message: String) -> bool:
	error = message
	push_error("[NativeTerrain] %s" % message)
	return false

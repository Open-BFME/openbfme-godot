class_name NativeMapTerrainMesh
extends Node3D
## One indexed terrain mesh. Vertex alpha carries the passability debug mask.

var _mesh_instance: MeshInstance3D
var _material: ShaderMaterial
var _overlay_enabled := false


func build(document) -> bool:
	if document == null or document.grid_width < 2 or document.grid_height < 2:
		push_error("[NativeMapTerrainMesh] refused an empty map document")
		return false
	var vertices := PackedVector3Array()
	var normals := PackedVector3Array()
	var colors := PackedColorArray()
	var indices := PackedInt32Array()
	var cell_count: int = document.grid_width * document.grid_height
	vertices.resize(cell_count)
	normals.resize(cell_count)
	colors.resize(cell_count)
	var minimum_height := INF
	var maximum_height := -INF
	for y in document.grid_height:
		for x in document.grid_width:
			var slot: int = y * document.grid_width + x
			var elevation: float = document.height_at_grid(x, y)
			minimum_height = minf(minimum_height, elevation)
			maximum_height = maxf(maximum_height, elevation)
			vertices[slot] = Vector3(float(x * document.cell_size), elevation, float(y * document.cell_size))
	for y in document.grid_height:
		for x in document.grid_width:
			var slot: int = y * document.grid_width + x
			var left: float = document.height_at_grid(x - 1, y)
			var right: float = document.height_at_grid(x + 1, y)
			var down: float = document.height_at_grid(x, y - 1)
			var up: float = document.height_at_grid(x, y + 1)
			var normal := Vector3(left - right, float(document.cell_size) * 2.0, down - up).normalized()
			normals[slot] = normal
			var height_mix := inverse_lerp(minimum_height, maximum_height, vertices[slot].y)
			var slope_mix := clampf(1.0 - normal.y, 0.0, 1.0)
			var low := Color(0.16, 0.29, 0.12)
			var high := Color(0.38, 0.35, 0.22)
			var cliff := Color(0.28, 0.25, 0.21)
			var terrain_color := low.lerp(high, height_mix * 0.55).lerp(cliff, slope_mix * 2.2)
			terrain_color.a = 1.0 if document.is_impassable_at(x, y) else 0.0
			colors[slot] = terrain_color
	indices.resize((document.grid_width - 1) * (document.grid_height - 1) * 6)
	var cursor := 0
	for y in document.grid_height - 1:
		for x in document.grid_width - 1:
			var a: int = y * document.grid_width + x
			var b: int = a + 1
			var c: int = a + document.grid_width
			var d: int = c + 1
			indices[cursor] = a
			indices[cursor + 1] = c
			indices[cursor + 2] = b
			indices[cursor + 3] = b
			indices[cursor + 4] = c
			indices[cursor + 5] = d
			cursor += 6
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices
	arrays[Mesh.ARRAY_NORMAL] = normals
	arrays[Mesh.ARRAY_COLOR] = colors
	arrays[Mesh.ARRAY_INDEX] = indices
	var terrain_mesh := ArrayMesh.new()
	terrain_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	_material = ShaderMaterial.new()
	var shader := Shader.new()
	shader.code = "shader_type spatial; render_mode unshaded, cull_disabled; uniform bool show_passability = false; varying vec4 terrain_color; void vertex(){ terrain_color = COLOR; } void fragment(){ vec3 base = terrain_color.rgb; if (show_passability && terrain_color.a > 0.5) { base = mix(base, vec3(0.95, 0.08, 0.03), 0.72); } ALBEDO = base; }"
	_material.shader = shader
	terrain_mesh.surface_set_material(0, _material)
	_mesh_instance = MeshInstance3D.new()
	_mesh_instance.name = "MapTerrain"
	_mesh_instance.mesh = terrain_mesh
	add_child(_mesh_instance)
	print("SIM_HOST_MAP_TERRAIN vertices=%d triangles=%d height_min=%.3f height_max=%.3f" % [vertices.size(), indices.size() / 3, minimum_height, maximum_height])
	return true


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and (event as InputEventKey).pressed and not (event as InputEventKey).echo:
		if (event as InputEventKey).keycode == KEY_P:
			set_passability_overlay(not _overlay_enabled)
			get_viewport().set_input_as_handled()


func set_passability_overlay(enabled: bool) -> void:
	_overlay_enabled = enabled
	if _material != null:
		_material.set_shader_parameter("show_passability", enabled)
	print("SIM_HOST_MAP_PASSABILITY_OVERLAY enabled=%s" % enabled)


func mesh_instance() -> MeshInstance3D:
	return _mesh_instance

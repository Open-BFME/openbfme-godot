class_name RetailFordsBattlefield
extends Node3D
## Bounded presentation of the importer's cooked Fords facts.
##
## Terrain uses a decimated source height grid with passability vertex colors.
## Water uses the cooked standing-area and river-strip geometry. Object models
## are unresolved, so a deterministic subset appears only as generic markers.

const TERRAIN_STEP := 8
const MAX_TERRAIN_VERTICES := 4096
const MAX_WATER_TRIANGLES := 4096

var source_driven := false
var terrain_vertex_count := 0
var terrain_triangle_count := 0
var impassable_vertex_count := 0
var water_surface_count := 0
var water_triangle_count := 0
var ford_marker_count := 0
var generic_prop_count := 0
var terrain_mesh_instance: MeshInstance3D
var water_mesh_instance: MeshInstance3D


func configure(map_data: RetailMapData) -> bool:
	_clear_generated()
	if map_data == null or not map_data.ready:
		return false
	if not _build_terrain(map_data):
		return false
	if not _build_water(map_data):
		return false
	_build_ford_markers(map_data)
	_build_generic_props(map_data)
	source_driven = terrain_vertex_count > 0 and water_surface_count > 0 and ford_marker_count == 3
	return source_driven


func _build_terrain(map_data: RetailMapData) -> bool:
	var xs := _sample_axis(map_data.width, TERRAIN_STEP)
	var ys := _sample_axis(map_data.height, TERRAIN_STEP)
	if xs.size() * ys.size() > MAX_TERRAIN_VERTICES:
		return false

	var vertices := PackedVector3Array()
	var normals := PackedVector3Array()
	var uvs := PackedVector2Array()
	var colors := PackedColorArray()
	var indices := PackedInt32Array()
	for y in ys:
		for x in xs:
			vertices.append(map_data.terrain_local_at(x, y))
			normals.append(Vector3.ZERO)
			uvs.append(Vector2(float(x) / float(maxi(1, map_data.width - 1)), float(y) / float(maxi(1, map_data.height - 1))))
			var blocked := map_data.is_impassable_at(x, y)
			if blocked:
				impassable_vertex_count += 1
				colors.append(Color("716a58"))
			else:
				var variation := float((x * 17 + y * 31) % 9) * 0.008
				colors.append(Color(0.38 + variation, 0.53 + variation, 0.27 + variation, 1.0))

	var columns := xs.size()
	for row in range(ys.size() - 1):
		for column in range(xs.size() - 1):
			var top_left := row * columns + column
			var top_right := top_left + 1
			var bottom_left := top_left + columns
			var bottom_right := bottom_left + 1
			indices.append_array(PackedInt32Array([
				top_left, bottom_left, top_right,
				top_right, bottom_left, bottom_right,
			]))
	normals = _calculate_normals(vertices, indices)

	var arrays: Array = []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices
	arrays[Mesh.ARRAY_NORMAL] = normals
	arrays[Mesh.ARRAY_TEX_UV] = uvs
	arrays[Mesh.ARRAY_COLOR] = colors
	arrays[Mesh.ARRAY_INDEX] = indices
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	var material := ShaderMaterial.new()
	var shader := Shader.new()
	shader.code = """
shader_type spatial;
render_mode cull_disabled, diffuse_burley;
varying float world_height;
float terrain_noise(vec2 p) {
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}
void vertex() {
	world_height = VERTEX.y;
}
void fragment() {
	float broad = terrain_noise(floor(UV * 96.0)) * 0.12;
	float fine = terrain_noise(floor(UV * 310.0)) * 0.055;
	float highland = smoothstep(2.0, 10.0, world_height);
	vec3 grass = COLOR.rgb * (0.98 + broad + fine);
	vec3 soil = vec3(0.38, 0.31, 0.20) * (0.94 + broad);
	vec3 rock = vec3(0.44, 0.46, 0.42) * (0.92 + fine);
	float blocked = step(0.34, COLOR.r);
	ALBEDO = mix(mix(grass, rock, highland * 0.38), soil, blocked * 0.55);
	ROUGHNESS = 0.93;
	SPECULAR = 0.18;
}
"""
	material.shader = shader
	mesh.surface_set_material(0, material)
	terrain_mesh_instance = MeshInstance3D.new()
	terrain_mesh_instance.name = "SourceHeightAndPassabilityTerrain"
	terrain_mesh_instance.mesh = mesh
	terrain_mesh_instance.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
	terrain_mesh_instance.set_meta("source", "cooked-heightmap-and-passability")
	add_child(terrain_mesh_instance)
	terrain_vertex_count = vertices.size()
	terrain_triangle_count = indices.size() / 3
	return terrain_vertex_count > 0 and terrain_triangle_count > 0 and impassable_vertex_count > 0


func _build_water(map_data: RetailMapData) -> bool:
	var vertices := PackedVector3Array()
	var normals := PackedVector3Array()
	var colors := PackedColorArray()
	var indices := PackedInt32Array()
	for polygon_value in map_data.standing_water_polygons:
		var polygon: PackedVector3Array = polygon_value
		var polygon_2d := PackedVector2Array()
		for point in polygon:
			polygon_2d.append(Vector2(point.x, point.z))
		var triangles := Geometry2D.triangulate_polygon(polygon_2d)
		if triangles.is_empty():
			continue
		var base := vertices.size()
		for point in polygon:
			vertices.append(point + Vector3.UP * 0.025)
			normals.append(Vector3.UP)
			colors.append(Color(0.06, 0.25, 0.32, 0.82))
		for triangle_index in triangles:
			indices.append(base + int(triangle_index))
		water_surface_count += 1

	for river in map_data.river_strips:
		var sections: Array = river.get("sections", [])
		if sections.size() < 2:
			continue
		for section_index in range(sections.size() - 1):
			var first: PackedVector3Array = sections[section_index]
			var second: PackedVector3Array = sections[section_index + 1]
			if indices.size() / 3 + 2 > MAX_WATER_TRIANGLES:
				return false
			var base := vertices.size()
			for point in [first[0], first[1], second[0], second[1]]:
				vertices.append((point as Vector3) + Vector3.UP * 0.03)
				normals.append(Vector3.UP)
				colors.append(Color(0.08, 0.30, 0.39, 0.80))
			indices.append_array(PackedInt32Array([
				base, base + 2, base + 1,
				base + 1, base + 2, base + 3,
			]))
		water_surface_count += 1
	if vertices.is_empty() or indices.is_empty() or indices.size() / 3 > MAX_WATER_TRIANGLES:
		return false

	var arrays: Array = []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices
	arrays[Mesh.ARRAY_NORMAL] = normals
	arrays[Mesh.ARRAY_COLOR] = colors
	arrays[Mesh.ARRAY_INDEX] = indices
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	var material := ShaderMaterial.new()
	var shader := Shader.new()
	shader.code = """
shader_type spatial;
render_mode blend_mix, depth_prepass_alpha, cull_disabled;
varying vec3 local_position;
void vertex() {
	local_position = VERTEX;
}
void fragment() {
	float ripple = sin((local_position.x + local_position.z) * 0.42 + TIME * 1.6) * 0.035;
	ALBEDO = COLOR.rgb + vec3(ripple * 0.25, ripple * 0.65, ripple);
	METALLIC = 0.12;
	ROUGHNESS = 0.18;
	SPECULAR = 0.78;
	ALPHA = 0.80;
}
"""
	material.shader = shader
	mesh.surface_set_material(0, material)
	water_mesh_instance = MeshInstance3D.new()
	water_mesh_instance.name = "SourceCookedWaterGeometry"
	water_mesh_instance.mesh = mesh
	water_mesh_instance.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	water_mesh_instance.set_meta("source", "cooked-standing-areas-and-river-strips")
	add_child(water_mesh_instance)
	water_triangle_count = indices.size() / 3
	return true


func _build_ford_markers(map_data: RetailMapData) -> void:
	for gate in map_data.ford_gates:
		var edge_a := Vector2(gate.get("edge_a", Vector2.ZERO))
		var edge_b := Vector2(gate.get("edge_b", Vector2.ZERO))
		var delta := edge_b - edge_a
		if delta.length() <= 0.1:
			continue
		var marker := MeshInstance3D.new()
		marker.name = "SourceFord_%s" % String(gate.get("name", "unknown"))
		var mesh := BoxMesh.new()
		mesh.size = Vector3(delta.length(), 0.12, 2.2)
		marker.mesh = mesh
		var center := (edge_a + edge_b) * 0.5
		marker.position = Vector3(center.x, float(gate.get("elevation", 0.0)) + 0.16, center.y)
		marker.rotation.y = -atan2(delta.y, delta.x)
		var material := StandardMaterial3D.new()
		material.albedo_color = Color("b5a16b")
		material.roughness = 1.0
		marker.material_override = material
		marker.set_meta("source_river_id", int(gate.get("source_river_id", -1)))
		marker.set_meta("source_section_index", int(gate.get("source_section_index", -1)))
		add_child(marker)
		ford_marker_count += 1


func _build_generic_props(map_data: RetailMapData) -> void:
	var vegetation: Array[Dictionary] = []
	var rocks: Array[Dictionary] = []
	for placement in map_data.generic_prop_placements:
		if String(placement.get("kind", "")) == "vegetation":
			vegetation.append(placement)
		else:
			rocks.append(placement)
	_add_prop_multimesh("SourceVegetationPlacementMarkers", vegetation, true)
	_add_prop_multimesh("SourceRockPlacementMarkers", rocks, false)
	generic_prop_count = vegetation.size() + rocks.size()


func _add_prop_multimesh(node_name: String, placements: Array[Dictionary], vegetation: bool) -> void:
	if placements.is_empty():
		return
	var mesh: PrimitiveMesh
	if vegetation:
		var cylinder := CylinderMesh.new()
		cylinder.top_radius = 0.08
		cylinder.bottom_radius = 0.86
		cylinder.height = 3.6
		cylinder.radial_segments = 8
		mesh = cylinder
	else:
		var sphere := SphereMesh.new()
		sphere.radius = 0.72
		sphere.height = 1.15
		sphere.radial_segments = 8
		sphere.rings = 4
		mesh = sphere
	var material := StandardMaterial3D.new()
	material.albedo_color = Color("355a32") if vegetation else Color("66645f")
	material.roughness = 1.0
	mesh.material = material
	var multimesh := MultiMesh.new()
	multimesh.transform_format = MultiMesh.TRANSFORM_3D
	multimesh.mesh = mesh
	multimesh.instance_count = placements.size()
	for index in range(placements.size()):
		var placement := placements[index]
		var position := Vector3(placement.get("position", Vector3.ZERO))
		var source_index := int(placement.get("source_index", index))
		var scale_variation := 0.78 + float(abs(source_index) % 7) * 0.055
		var scale := Vector3(scale_variation, scale_variation, scale_variation)
		if not vegetation:
			scale = Vector3(1.25, 0.65, 1.0) * scale_variation
		var basis := Basis(Vector3.UP, float(placement.get("yaw", 0.0))).scaled(scale)
		var elevation_offset := 2.5 * scale_variation if vegetation else 0.45 * scale_variation
		multimesh.set_instance_transform(index, Transform3D(basis, position + Vector3.UP * elevation_offset))
	var instance := MultiMeshInstance3D.new()
	instance.name = node_name
	instance.multimesh = multimesh
	instance.set_meta("disclosure", "generic markers; retail object models unresolved")
	add_child(instance)
	if vegetation:
		_add_vegetation_trunks(placements)


func _add_vegetation_trunks(placements: Array[Dictionary]) -> void:
	var trunk_mesh := CylinderMesh.new()
	trunk_mesh.top_radius = 0.15
	trunk_mesh.bottom_radius = 0.22
	trunk_mesh.height = 2.3
	trunk_mesh.radial_segments = 7
	var material := StandardMaterial3D.new()
	material.albedo_color = Color("4c3927")
	material.roughness = 1.0
	trunk_mesh.material = material
	var multimesh := MultiMesh.new()
	multimesh.transform_format = MultiMesh.TRANSFORM_3D
	multimesh.mesh = trunk_mesh
	multimesh.instance_count = placements.size()
	for index in range(placements.size()):
		var placement := placements[index]
		var position := Vector3(placement.get("position", Vector3.ZERO))
		var source_index := int(placement.get("source_index", index))
		var scale_variation := 0.78 + float(abs(source_index) % 7) * 0.055
		var basis := Basis(Vector3.UP, float(placement.get("yaw", 0.0))).scaled(Vector3.ONE * scale_variation)
		multimesh.set_instance_transform(index, Transform3D(basis, position + Vector3.UP * 1.15 * scale_variation))
	var instance := MultiMeshInstance3D.new()
	instance.name = "SourceVegetationTrunks"
	instance.multimesh = multimesh
	instance.set_meta("disclosure", "generic markers; retail object models unresolved")
	add_child(instance)


func _calculate_normals(vertices: PackedVector3Array, indices: PackedInt32Array) -> PackedVector3Array:
	var result := PackedVector3Array()
	result.resize(vertices.size())
	for index in range(0, indices.size(), 3):
		var a := int(indices[index])
		var b := int(indices[index + 1])
		var c := int(indices[index + 2])
		var normal := (vertices[b] - vertices[a]).cross(vertices[c] - vertices[a]).normalized()
		result[a] = result[a] + normal
		result[b] = result[b] + normal
		result[c] = result[c] + normal
	for index in range(result.size()):
		result[index] = result[index].normalized() if result[index].length_squared() > 0.000001 else Vector3.UP
	return result


func _sample_axis(size: int, step: int) -> Array[int]:
	var result: Array[int] = []
	var index := 0
	while index < size:
		result.append(index)
		index += step
	if result.is_empty() or result[-1] != size - 1:
		result.append(size - 1)
	return result


func _clear_generated() -> void:
	for child in get_children():
		child.queue_free()
	source_driven = false
	terrain_vertex_count = 0
	terrain_triangle_count = 0
	impassable_vertex_count = 0
	water_surface_count = 0
	water_triangle_count = 0
	ford_marker_count = 0
	generic_prop_count = 0

class_name RetailOrderIndicator
extends Node3D
## Presentation-only rendering of one authoritative battalion route.
## The simulation owns every point; this node only turns the remaining route into
## a readable BFME-style ground ribbon and destination banner.

var route_points: Array[Vector2] = []
var destination := Vector2.ZERO
var showing_order := false
var _line: MeshInstance3D
var _flag_root: Node3D
var _line_material: StandardMaterial3D


func _ready() -> void:
	_build_visuals()
	clear_order()


func sync_from_entity(entity: Dictionary, selected: bool, height_sampler: Callable) -> void:
	if not selected or int(entity.get("health", 0)) <= 0:
		clear_order()
		return
	var values: Variant = entity.get("route", [])
	if typeof(values) != TYPE_ARRAY or (values as Array).is_empty():
		clear_order()
		return
	route_points.clear()
	for value in values as Array:
		if typeof(value) == TYPE_VECTOR2:
			route_points.append(Vector2(value))
	if route_points.is_empty():
		clear_order()
		return
	destination = Vector2(entity.get("destination", route_points.back()))
	var points: Array[Vector2] = [Vector2(entity.get("position", Vector2.ZERO))]
	points.append_array(route_points)
	_rebuild_line(points, height_sampler)
	var final_height := float(height_sampler.call(destination)) if height_sampler.is_valid() else 0.35
	_flag_root.position = Vector3(destination.x, final_height + 0.04, destination.y)
	_line.visible = true
	_flag_root.visible = true
	showing_order = true


func clear_order() -> void:
	route_points.clear()
	showing_order = false
	if _line != null:
		_line.visible = false
	if _flag_root != null:
		_flag_root.visible = false


func _build_visuals() -> void:
	_line = MeshInstance3D.new()
	_line.name = "OrderRibbon"
	_line.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_line_material = StandardMaterial3D.new()
	_line_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	_line_material.albedo_color = Color(0.42, 0.78, 1.0, 0.9)
	_line_material.emission_enabled = true
	_line_material.emission = Color(0.12, 0.42, 0.82)
	_line_material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	_line_material.no_depth_test = true
	add_child(_line)

	_flag_root = Node3D.new()
	_flag_root.name = "DestinationFlag"
	add_child(_flag_root)
	var pole := MeshInstance3D.new()
	var pole_mesh := CylinderMesh.new()
	pole_mesh.top_radius = 0.045
	pole_mesh.bottom_radius = 0.055
	pole_mesh.height = 1.65
	pole.mesh = pole_mesh
	pole.position.y = 0.825
	var metal := StandardMaterial3D.new()
	metal.albedo_color = Color("d8c48b")
	metal.metallic = 0.65
	metal.roughness = 0.3
	pole.material_override = metal
	_flag_root.add_child(pole)

	var banner := MeshInstance3D.new()
	banner.name = "Banner"
	banner.mesh = _make_banner_mesh()
	banner.position = Vector3(0.34, 1.38, 0.0)
	var cloth := StandardMaterial3D.new()
	cloth.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	cloth.albedo_color = Color(0.12, 0.42, 0.78, 0.95)
	cloth.emission_enabled = true
	cloth.emission = Color(0.04, 0.14, 0.32)
	cloth.cull_mode = BaseMaterial3D.CULL_DISABLED
	cloth.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	banner.material_override = cloth
	_flag_root.add_child(banner)

	var target_ring := MeshInstance3D.new()
	target_ring.name = "TargetRing"
	var ring := TorusMesh.new()
	ring.inner_radius = 0.58
	ring.outer_radius = 0.68
	ring.rings = 28
	ring.ring_segments = 7
	target_ring.mesh = ring
	target_ring.position.y = 0.025
	target_ring.material_override = _line_material
	_flag_root.add_child(target_ring)


func _make_banner_mesh() -> ArrayMesh:
	var vertices := PackedVector3Array([
		Vector3(-0.34, 0.34, 0.0),
		Vector3(0.34, 0.34, 0.0),
		Vector3(0.34, -0.17, 0.0),
		Vector3(-0.34, -0.34, 0.0),
	])
	var arrays: Array = []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices
	arrays[Mesh.ARRAY_INDEX] = PackedInt32Array([0, 1, 2, 0, 2, 3])
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	return mesh


func _rebuild_line(points: Array[Vector2], height_sampler: Callable) -> void:
	var mesh := ImmediateMesh.new()
	mesh.surface_begin(Mesh.PRIMITIVE_LINE_STRIP, _line_material)
	for point in points:
		var height := float(height_sampler.call(point)) if height_sampler.is_valid() else 0.35
		mesh.surface_add_vertex(Vector3(point.x, height + 0.16, point.y))
	mesh.surface_end()
	_line.mesh = mesh

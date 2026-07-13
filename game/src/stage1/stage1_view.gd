class_name Stage1View
extends Node3D
## Interpolated primitive presentation. No gameplay truth lives here.

var _grid_size: Vector2i = Vector2i(48, 32)
var _scale: int = 1000
var _configured: bool = false
var _melee_meshes: Array[MultiMeshInstance3D] = []
var _ranged_meshes: Array[MultiMeshInstance3D] = []
var _projectile_mesh: MultiMeshInstance3D
var _fortress_views: Dictionary = {}
var _selection_views: Dictionary = {}

func configure(snapshot: Dictionary) -> void:
	if _configured:
		return
	_grid_size = snapshot.get("grid_size", Vector2i(48, 32))
	_scale = int(snapshot.get("scale", 1000))
	_build_ground(snapshot.get("blocked", []), snapshot.get("fortresses", []))
	_build_multimeshes()
	_configured = true

func render_snapshot(snapshot: Dictionary, alpha: float, selected_ids: Array[int]) -> void:
	if not _configured:
		configure(snapshot)
	var melee_positions: Array = [[], []]
	var ranged_positions: Array = [[], []]
	var horde_positions: Dictionary = {}
	for horde: Dictionary in snapshot.get("hordes", []):
		var team := int(horde.team)
		if team < 0 or team > 1:
			continue
		var anchor := _interpolated_fixed(horde.previous_anchor, horde.anchor, alpha)
		horde_positions[int(horde.id)] = fixed_to_world(anchor, 0.04)
		for member: Dictionary in horde.members:
			if int(member.health) <= 0:
				continue
			var position := _interpolated_fixed(member.previous_position, member.position, alpha)
			if bool(member.ranged):
				ranged_positions[team].append(position)
			else:
				melee_positions[team].append(position)
	for team in 2:
		_write_instances(_melee_meshes[team].multimesh, melee_positions[team], false)
		_write_instances(_ranged_meshes[team].multimesh, ranged_positions[team], true)
	_sync_fortresses(snapshot.get("fortresses", []))
	_sync_projectiles(snapshot.get("projectiles", []), alpha)
	_sync_selection(selected_ids, horde_positions)

func horde_world_positions(snapshot: Dictionary, alive_only: bool = true) -> Dictionary:
	var result: Dictionary = {}
	for horde: Dictionary in snapshot.get("hordes", []):
		if alive_only and int(horde.alive) <= 0:
			continue
		result[int(horde.id)] = fixed_to_world(horde.anchor, 0.0)
	return result

func fortress_world_positions(snapshot: Dictionary, alive_only: bool = true) -> Dictionary:
	var result: Dictionary = {}
	for fortress: Dictionary in snapshot.get("fortresses", []):
		if alive_only and int(fortress.health) <= 0:
			continue
		result[int(fortress.id)] = fixed_to_world(fortress.position, 0.0)
	return result

func fixed_to_world(position: Vector2i, height: float = 0.0) -> Vector3:
	return Vector3(
		float(position.x) / float(_scale) - float(_grid_size.x) * 0.5,
		height,
		float(position.y) / float(_scale) - float(_grid_size.y) * 0.5
	)

func world_to_fixed(position: Vector2) -> Vector2i:
	return Vector2i(
		int(round((position.x + float(_grid_size.x) * 0.5) * float(_scale))),
		int(round((position.y + float(_grid_size.y) * 0.5) * float(_scale)))
	)

func _interpolated_fixed(previous: Vector2i, current: Vector2i, alpha: float) -> Vector2i:
	return Vector2i(
		int(round(lerpf(float(previous.x), float(current.x), alpha))),
		int(round(lerpf(float(previous.y), float(current.y), alpha)))
	)

func _build_ground(blocked_cells: Array, fortress_rows: Array) -> void:
	var ground := MeshInstance3D.new()
	ground.name = "LegalSafeArenaGround"
	var ground_mesh := BoxMesh.new()
	ground_mesh.size = Vector3(float(_grid_size.x), 0.18, float(_grid_size.y))
	ground.mesh = ground_mesh
	ground.position.y = -0.11
	var ground_material := StandardMaterial3D.new()
	ground_material.albedo_color = Color(0.21, 0.30, 0.20)
	ground_material.roughness = 0.96
	ground.material_override = ground_material
	add_child(ground)

	var grid_lines := ImmediateMesh.new()
	var line_material := StandardMaterial3D.new()
	line_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	line_material.albedo_color = Color(0.62, 0.72, 0.55, 0.17)
	line_material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	grid_lines.surface_begin(Mesh.PRIMITIVE_LINES, line_material)
	for x in range(_grid_size.x + 1):
		var wx := float(x) - float(_grid_size.x) * 0.5
		grid_lines.surface_add_vertex(Vector3(wx, 0.005, -float(_grid_size.y) * 0.5))
		grid_lines.surface_add_vertex(Vector3(wx, 0.005, float(_grid_size.y) * 0.5))
	for y in range(_grid_size.y + 1):
		var wz := float(y) - float(_grid_size.y) * 0.5
		grid_lines.surface_add_vertex(Vector3(-float(_grid_size.x) * 0.5, 0.005, wz))
		grid_lines.surface_add_vertex(Vector3(float(_grid_size.x) * 0.5, 0.005, wz))
	grid_lines.surface_end()
	var grid_node := MeshInstance3D.new()
	grid_node.name = "AuthoritativeGrid"
	grid_node.mesh = grid_lines
	add_child(grid_node)

	var blocker_instance := MultiMeshInstance3D.new()
	blocker_instance.name = "StaticBlockers"
	var blocker_mesh := BoxMesh.new()
	blocker_mesh.size = Vector3(0.88, 1.25, 0.88)
	var blocker_material := StandardMaterial3D.new()
	blocker_material.albedo_color = Color(0.28, 0.29, 0.31)
	blocker_material.roughness = 0.88
	blocker_mesh.material = blocker_material
	var blocker_multi := MultiMesh.new()
	blocker_multi.transform_format = MultiMesh.TRANSFORM_3D
	blocker_multi.mesh = blocker_mesh
	blocker_multi.instance_count = blocked_cells.size()
	for i in blocked_cells.size():
		var cell: Vector2i = blocked_cells[i]
		var position := fixed_to_world(Stage1Grid.cell_center(cell), 0.58)
		var basis := Basis().rotated(Vector3.UP, float((cell.x * 17 + cell.y * 31) % 9) * 0.07)
		blocker_multi.set_instance_transform(i, Transform3D(basis, position))
	blocker_instance.multimesh = blocker_multi
	add_child(blocker_instance)

	var crossing := MeshInstance3D.new()
	crossing.name = "CentralCrossing"
	var crossing_mesh := BoxMesh.new()
	crossing_mesh.size = Vector3(1.2, 0.09, 9.0)
	crossing.mesh = crossing_mesh
	crossing.position = fixed_to_world(Stage1Grid.cell_center(Vector2i(_grid_size.x / 2, _grid_size.y / 2)), 0.015)
	var crossing_material := StandardMaterial3D.new()
	crossing_material.albedo_color = Color(0.54, 0.43, 0.27)
	crossing_material.roughness = 0.9
	crossing.material_override = crossing_material
	add_child(crossing)

	for fortress: Dictionary in fortress_rows:
		_add_base_pad_fixed(fortress.position, Color(0.12, 0.42, 0.92, 0.72) if int(fortress.team) == 0 else Color(0.92, 0.18, 0.14, 0.72))

func _add_base_pad_fixed(position: Vector2i, color: Color) -> void:
	var pad := MeshInstance3D.new()
	var mesh := CylinderMesh.new()
	mesh.top_radius = 3.0
	mesh.bottom_radius = 3.0
	mesh.height = 0.07
	pad.mesh = mesh
	pad.position = fixed_to_world(position, 0.0)
	var material := StandardMaterial3D.new()
	material.albedo_color = color
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	material.roughness = 0.82
	pad.material_override = material
	add_child(pad)

func _build_multimeshes() -> void:
	for team in 2:
		_melee_meshes.append(_make_member_multimesh(team, false))
		_ranged_meshes.append(_make_member_multimesh(team, true))
	_projectile_mesh = MultiMeshInstance3D.new()
	_projectile_mesh.name = "Projectiles"
	var sphere := SphereMesh.new()
	sphere.radius = 0.09
	sphere.height = 0.18
	var projectile_material := StandardMaterial3D.new()
	projectile_material.albedo_color = Color(1.0, 0.82, 0.24)
	projectile_material.emission_enabled = true
	projectile_material.emission = Color(1.0, 0.46, 0.08)
	sphere.material = projectile_material
	var multi := MultiMesh.new()
	multi.transform_format = MultiMesh.TRANSFORM_3D
	multi.mesh = sphere
	_projectile_mesh.multimesh = multi
	add_child(_projectile_mesh)

func _make_member_multimesh(team: int, ranged: bool) -> MultiMeshInstance3D:
	var node := MultiMeshInstance3D.new()
	node.name = "%s_%s" % ["Blue" if team == 0 else "Red", "Ranged" if ranged else "Melee"]
	var mesh: PrimitiveMesh
	if ranged:
		var sphere := SphereMesh.new()
		sphere.radius = 0.22
		sphere.height = 0.44
		mesh = sphere
	else:
		var capsule := CapsuleMesh.new()
		capsule.radius = 0.18
		capsule.height = 0.72
		mesh = capsule
	var material := StandardMaterial3D.new()
	material.albedo_color = Color(0.12, 0.48, 1.0) if team == 0 else Color(0.95, 0.16, 0.12)
	material.roughness = 0.64
	if ranged:
		material.metallic = 0.25
	mesh.material = material
	var multi := MultiMesh.new()
	multi.transform_format = MultiMesh.TRANSFORM_3D
	multi.mesh = mesh
	node.multimesh = multi
	add_child(node)
	return node

func _write_instances(multi: MultiMesh, positions: Array, ranged: bool) -> void:
	multi.instance_count = positions.size()
	for i in positions.size():
		var world := fixed_to_world(positions[i], 0.22 if ranged else 0.36)
		multi.set_instance_transform(i, Transform3D(Basis(), world))

func _sync_projectiles(rows: Array, alpha: float) -> void:
	_projectile_mesh.multimesh.instance_count = rows.size()
	for i in rows.size():
		var projectile: Dictionary = rows[i]
		var position := _interpolated_fixed(projectile.previous_position, projectile.position, alpha)
		_projectile_mesh.multimesh.set_instance_transform(i, Transform3D(Basis(), fixed_to_world(position, 0.75)))

func _sync_fortresses(rows: Array) -> void:
	var live: Dictionary = {}
	for row: Dictionary in rows:
		var id := int(row.id)
		live[id] = true
		var root: Node3D = _fortress_views.get(id)
		if root == null:
			root = _make_fortress(int(row.team))
			root.name = "Fortress_%d" % id
			add_child(root)
			_fortress_views[id] = root
		root.position = fixed_to_world(row.position, 0.0)
		root.visible = int(row.health) > 0
		var label: Label3D = root.get_node("Label")
		label.text = "%s FORTRESS\n%d / %d" % ["BLUE" if int(row.team) == 0 else "RED", int(row.health), int(row.max_health)]
	for id in _fortress_views.keys():
		if not live.has(id):
			(_fortress_views[id] as Node).queue_free()
			_fortress_views.erase(id)

func _make_fortress(team: int) -> Node3D:
	var root := Node3D.new()
	var keep := MeshInstance3D.new()
	var keep_mesh := BoxMesh.new()
	keep_mesh.size = Vector3(2.8, 2.1, 2.8)
	keep.mesh = keep_mesh
	keep.position.y = 1.05
	var material := StandardMaterial3D.new()
	material.albedo_color = Color(0.14, 0.34, 0.72) if team == 0 else Color(0.66, 0.14, 0.12)
	material.roughness = 0.82
	keep.material_override = material
	root.add_child(keep)
	for offset in [Vector2(-1.35, -1.35), Vector2(1.35, -1.35), Vector2(-1.35, 1.35), Vector2(1.35, 1.35)]:
		var tower := MeshInstance3D.new()
		var tower_mesh := CylinderMesh.new()
		tower_mesh.top_radius = 0.48
		tower_mesh.bottom_radius = 0.58
		tower_mesh.height = 2.8
		tower.mesh = tower_mesh
		tower.position = Vector3(offset.x, 1.4, offset.y)
		tower.material_override = material
		root.add_child(tower)
	var label := Label3D.new()
	label.name = "Label"
	label.position = Vector3(0.0, 3.3, 0.0)
	label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	label.font_size = 44
	label.pixel_size = 0.018
	label.outline_size = 8
	root.add_child(label)
	return root

func _sync_selection(selected_ids: Array[int], horde_positions: Dictionary) -> void:
	var keep: Dictionary = {}
	for id in selected_ids:
		if not horde_positions.has(id):
			continue
		keep[id] = true
		var ring: MeshInstance3D = _selection_views.get(id)
		if ring == null:
			ring = _make_selection_ring()
			add_child(ring)
			_selection_views[id] = ring
		ring.position = horde_positions[id]
	for id in _selection_views.keys():
		if not keep.has(id):
			(_selection_views[id] as Node).queue_free()
			_selection_views.erase(id)

func _make_selection_ring() -> MeshInstance3D:
	var ring := MeshInstance3D.new()
	var mesh := CylinderMesh.new()
	mesh.top_radius = 1.85
	mesh.bottom_radius = 1.85
	mesh.height = 0.035
	ring.mesh = mesh
	var material := StandardMaterial3D.new()
	material.albedo_color = Color(0.25, 0.95, 1.0, 0.34)
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	ring.material_override = material
	return ring

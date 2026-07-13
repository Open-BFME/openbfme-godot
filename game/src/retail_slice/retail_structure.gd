class_name RetailStructure
extends Node3D
## Runtime presentation for authoritative retail-slice structures. Imported
## bundle models are preferred; the repository-authored masonry kit is an
## honest fallback while the private building dependency closure is incomplete.

var entity_id := 0
var team := 0
var structure_kind := ""
var selected := false
var health_ratio := 1.0
var construction_ratio := 1.0
var presentation_mode := "unconfigured"
var retail_visual_loaded := false
var pick_radius := 4.0
var _selection_ring: MeshInstance3D
var _health_fill: MeshInstance3D
var _visual_root: Node3D


func configure(entity: Dictionary, bundle_object_id: String = "") -> void:
	entity_id = int(entity.get("id", 0))
	team = int(entity.get("team", 0))
	structure_kind = String(entity.get("structure_kind", entity.get("kind", "fortress")))
	name = "RetailStructure_%d_%s" % [entity_id, structure_kind]
	pick_radius = 7.0 if structure_kind == "fortress" else 4.5
	_build_visual(bundle_object_id)
	_build_markers()
	sync_state(entity)


func sync_state(entity: Dictionary) -> void:
	var maximum := maxi(1, int(entity.get("maximum_health", 1)))
	health_ratio = clampf(float(entity.get("health", maximum)) / float(maximum), 0.0, 1.0)
	construction_ratio = clampf(float(entity.get("construction_progress", 1.0)), 0.0, 1.0)
	if _visual_root != null:
		_visual_root.scale.y = lerpf(0.12, 1.0, construction_ratio)
		_visual_root.visible = health_ratio > 0.0
	if _selection_ring != null:
		_selection_ring.visible = selected and health_ratio > 0.0
	if _health_fill != null:
		_health_fill.scale.x = maxf(0.001, health_ratio)
		_health_fill.position.x = (health_ratio - 1.0) * 1.65


func set_selected(value: bool) -> void:
	selected = value
	if _selection_ring != null:
		_selection_ring.visible = selected and health_ratio > 0.0


func _build_visual(bundle_object_id: String) -> void:
	_visual_root = Node3D.new()
	_visual_root.name = "StructureVisual"
	add_child(_visual_root)
	if bundle_object_id != "" and ContentDB.bundle_objects.has(bundle_object_id):
		var asset_factory = load("res://src/view/asset_factory.gd")
		var imported: Node3D = asset_factory.make_bundle_object_visual(bundle_object_id, team)
		if imported != null and bool(imported.get_meta("authored", false)):
			_visual_root.add_child(imported)
			retail_visual_loaded = true
			presentation_mode = "private-imported"
			return
	presentation_mode = "legal-safe-masonry-kit"
	_build_procedural_structure()


func _build_procedural_structure() -> void:
	var stone := _material(Color("7d8589"), 0.05, 0.82)
	var dark_stone := _material(Color("454d54"), 0.12, 0.76)
	var roof := _material(Color("284962") if team == 0 else Color("6b2d2b"), 0.35, 0.48)
	match structure_kind:
		"fortress":
			_add_box(Vector3(8.8, 2.5, 7.4), Vector3(0, 1.25, 0), stone)
			_add_box(Vector3(4.6, 3.1, 3.8), Vector3(0, 3.25, 0), dark_stone)
			for corner in [Vector2(-3.8, -3.0), Vector2(3.8, -3.0), Vector2(-3.8, 3.0), Vector2(3.8, 3.0)]:
				_add_tower(Vector3(corner.x, 2.4, corner.y), stone, roof)
			_add_box(Vector3(2.2, 2.5, 0.35), Vector3(0, 1.15, -3.72), roof)
		"barracks":
			_add_box(Vector3(7.0, 2.3, 4.8), Vector3(0, 1.15, 0), stone)
			_add_roof(Vector3(7.5, 1.8, 5.35), Vector3(0, 3.12, 0), roof)
			_add_box(Vector3(1.4, 2.0, 0.3), Vector3(0, 1.0, -2.52), dark_stone)
		"farm":
			_add_box(Vector3(4.8, 1.7, 3.8), Vector3(0, 0.85, 0), stone)
			_add_roof(Vector3(5.3, 1.5, 4.25), Vector3(0, 2.35, 0), roof)
		"archery_range":
			_add_box(Vector3(6.6, 2.1, 4.5), Vector3(0, 1.05, 0), stone)
			_add_roof(Vector3(7.0, 1.5, 4.9), Vector3(0, 2.8, 0), roof)
			for x in [-2.1, 0.0, 2.1]:
				_add_box(Vector3(0.15, 2.3, 0.15), Vector3(x, 1.15, -2.5), dark_stone)
		"stable":
			_add_box(Vector3(7.6, 2.0, 5.2), Vector3(0, 1.0, 0), stone)
			_add_roof(Vector3(8.1, 1.6, 5.7), Vector3(0, 2.7, 0), roof)
			_add_box(Vector3(2.4, 1.8, 0.25), Vector3(0, 0.9, -2.72), dark_stone)
		_:
			_add_box(Vector3(5.0, 2.0, 5.0), Vector3(0, 1.0, 0), stone)


func _build_markers() -> void:
	_selection_ring = MeshInstance3D.new()
	_selection_ring.name = "SelectionRing"
	var ring := TorusMesh.new()
	ring.inner_radius = pick_radius
	ring.outer_radius = pick_radius + 0.22
	ring.rings = 36
	ring.ring_segments = 8
	_selection_ring.mesh = ring
	_selection_ring.position.y = 0.08
	_selection_ring.material_override = _emissive(Color("67e48b"))
	_selection_ring.visible = false
	add_child(_selection_ring)

	var health_back := MeshInstance3D.new()
	health_back.name = "HealthBack"
	var back_mesh := BoxMesh.new()
	back_mesh.size = Vector3(3.6, 0.16, 0.13)
	health_back.mesh = back_mesh
	health_back.position = Vector3(0, 7.4 if structure_kind == "fortress" else 4.8, 0)
	health_back.material_override = _emissive(Color("131a1e"))
	add_child(health_back)
	_health_fill = MeshInstance3D.new()
	_health_fill.name = "HealthFill"
	_health_fill.mesh = back_mesh.duplicate()
	_health_fill.position = health_back.position + Vector3(0, 0.02, -0.01)
	_health_fill.material_override = _emissive(Color("5bd765") if team == 0 else Color("df5a4f"))
	add_child(_health_fill)


func _add_tower(at: Vector3, stone: Material, roof: Material) -> void:
	var body := MeshInstance3D.new()
	var cylinder := CylinderMesh.new()
	cylinder.top_radius = 1.08
	cylinder.bottom_radius = 1.22
	cylinder.height = 4.8
	cylinder.radial_segments = 8
	body.mesh = cylinder
	body.position = at
	body.material_override = stone
	_visual_root.add_child(body)
	var cap := MeshInstance3D.new()
	var cone := CylinderMesh.new()
	cone.top_radius = 0.05
	cone.bottom_radius = 1.38
	cone.height = 1.7
	cone.radial_segments = 8
	cap.mesh = cone
	cap.position = at + Vector3(0, 3.15, 0)
	cap.material_override = roof
	_visual_root.add_child(cap)


func _add_box(size: Vector3, at: Vector3, material: Material) -> void:
	var node := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = size
	node.mesh = mesh
	node.position = at
	node.material_override = material
	_visual_root.add_child(node)


func _add_roof(size: Vector3, at: Vector3, material: Material) -> void:
	var node := MeshInstance3D.new()
	var mesh := PrismMesh.new()
	mesh.size = size
	node.mesh = mesh
	node.position = at
	node.rotation_degrees.y = 90.0
	node.material_override = material
	_visual_root.add_child(node)


func _material(color: Color, metallic: float, roughness: float) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.albedo_color = color
	material.metallic = metallic
	material.roughness = roughness
	return material


func _emissive(color: Color) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_color = color
	material.emission_enabled = true
	material.emission = color * 0.35
	return material

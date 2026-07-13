class_name Stage2PlacementGhost
extends Node3D
## Presentation-only placement preview. Authority always revalidates the command.

var _mesh: MeshInstance3D
var _material: StandardMaterial3D
var _footprint: Vector2i = Vector2i.ONE
var _valid: bool = false

func _ready() -> void:
	_mesh = MeshInstance3D.new()
	_mesh.name = "Footprint"
	var box := BoxMesh.new()
	box.size = Vector3(1.0, 0.18, 1.0)
	_mesh.mesh = box
	_material = StandardMaterial3D.new()
	_material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	_material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	_mesh.material_override = _material
	add_child(_mesh)
	visible = false
	_apply_style()

func arm(footprint: Vector2i) -> void:
	_footprint = footprint
	_mesh.scale = Vector3(float(footprint.x), 1.0, float(footprint.y))
	visible = true

func disarm() -> void:
	visible = false

func update_preview(world_position: Vector3, is_valid: bool) -> void:
	position = Vector3(world_position.x, 0.08, world_position.z)
	_valid = is_valid
	_apply_style()

func is_valid_preview() -> bool:
	return _valid

func footprint() -> Vector2i:
	return _footprint

func _apply_style() -> void:
	if _material == null:
		return
	_material.albedo_color = Color(0.18, 0.95, 0.42, 0.42) if _valid else Color(1.0, 0.18, 0.16, 0.46)
	_material.emission_enabled = true
	_material.emission = Color(0.06, 0.42, 0.12) if _valid else Color(0.48, 0.03, 0.02)

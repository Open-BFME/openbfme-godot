extends Node3D
## Lean presentation for M3-expansion buildings whose pack coverage is
## per-phase GLBs (data/m3/model-census.json) rather than the full declared
## bundle-object lifecycle the original five structures use. Shows the best
## converted model for the current lifecycle phase, honors construction
## progress, and mirrors RetailStructure's selection ring. No procedural
## geometry: a phase without a converted model falls back to the intact model
## (recorded, never invented).
##
## No class_name (headless --script runners skip global class registration).

const AssetFactory = preload("res://src/view/asset_factory.gd")

var entity_id := 0
var structure_kind := ""
var team := 0
var pick_radius := 4.0
var selected := false
var health_ratio := 1.0

var _models: Dictionary = {}
var _pack_root := ""
var _active_phase := ""
var _visual: Node3D = null
var _selection_ring: MeshInstance3D = null
var _scale := 1.0


## models: {"intact": <abs glb>, optional "construction"/"damaged"/
## "really-damaged"/"rubble"}. All paths were resolved from the selected pack.
func configure(entity: Dictionary, models: Dictionary, pack_root: String, transform_scale: float) -> void:
	entity_id = int(entity.get("id", 0))
	structure_kind = String(entity.get("structure_kind", ""))
	team = int(entity.get("team", 0))
	_models = models.duplicate()
	_pack_root = pack_root
	_scale = transform_scale
	name = "SimpleStructure_%d_%s" % [entity_id, structure_kind]
	_build_selection_ring()
	sync_state(entity)


func set_selected(value: bool) -> void:
	selected = value
	if _selection_ring != null:
		_selection_ring.visible = selected and health_ratio > 0.0


func sync_state(entity: Dictionary) -> void:
	var maximum := maxi(1, int(entity.get("maximum_health", 1)))
	health_ratio = clampf(float(int(entity.get("health", 0))) / float(maximum), 0.0, 1.0)
	var progress := clampf(float(entity.get("construction_progress", 1.0)), 0.0, 1.0)
	var phase := _phase_for(progress)
	if phase != _active_phase:
		_active_phase = phase
		_swap_visual(phase)
	if _visual != null and phase == "construction" and not _models.has("construction"):
		# Buildup affordance when no authored construction model was converted:
		# the intact body rises with progress. Presentation-only.
		_visual.scale = Vector3(1.0, maxf(0.15, progress), 1.0)
	elif _visual != null:
		_visual.scale = Vector3.ONE
	visible = health_ratio > 0.0 or _models.has("rubble")
	if _selection_ring != null:
		_selection_ring.visible = selected and health_ratio > 0.0


func _phase_for(progress: float) -> String:
	if progress < 1.0:
		return "construction"
	if health_ratio <= 0.0:
		return "rubble" if _models.has("rubble") else "intact"
	if health_ratio <= 0.33 and _models.has("really-damaged"):
		return "really-damaged"
	if health_ratio <= 0.66 and _models.has("damaged"):
		return "damaged"
	return "intact"


func _swap_visual(phase: String) -> void:
	if _visual != null:
		_visual.queue_free()
		_visual = null
	var model_path := String(_models.get(phase, _models.get("intact", "")))
	if model_path == "":
		return
	var visual := AssetFactory.make_explicit_model_visual(model_path, team, "", _pack_root)
	if visual == null:
		return
	visual.scale = Vector3.ONE
	for child in visual.get_children():
		if child is Node3D:
			(child as Node3D).scale = Vector3.ONE * _scale
	add_child(visual)
	_visual = visual


func _build_selection_ring() -> void:
	_selection_ring = MeshInstance3D.new()
	_selection_ring.name = "SelectionRing"
	var ring := TorusMesh.new()
	ring.inner_radius = pick_radius
	ring.outer_radius = pick_radius + 0.22
	ring.rings = 36
	ring.ring_segments = 8
	_selection_ring.mesh = ring
	_selection_ring.position.y = 0.08
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_color = Color("67e48b")
	_selection_ring.material_override = material
	_selection_ring.visible = false
	add_child(_selection_ring)

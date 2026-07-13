class_name Stage2View
extends Stage1View
## Stage 1 snapshot presentation plus legal-safe construction and rally primitives.

var _building_views: Dictionary = {}
var _rally_views: Dictionary = {}
var _building_selection: MeshInstance3D
var _selected_building_id: int = 0

func render_snapshot(snapshot: Dictionary, alpha: float, selected_ids: Array[int]) -> void:
	super.render_snapshot(snapshot, alpha, selected_ids)
	_sync_buildings(snapshot.get("buildings", []))
	_update_building_selection()

func set_selected_building(building_id: int) -> void:
	_selected_building_id = building_id
	_update_building_selection()

func _sync_buildings(rows: Array) -> void:
	var live: Dictionary = {}
	for row: Dictionary in rows:
		var id := int(row.get("id", 0))
		if id <= 0:
			continue
		live[id] = true
		var root: Node3D = _building_views.get(id)
		if root == null:
			root = _make_building(int(row.get("role", 2)), int(row.get("team", 0)))
			root.name = "Building_%d" % id
			add_child(root)
			_building_views[id] = root
		root.position = fixed_to_world(row.get("position", Vector2i.ZERO), 0.0)
		root.visible = int(row.get("health", 0)) > 0
		var construction_ticks := maxi(1, int(row.get("construction_ticks", 1)))
		var progress := clampf(float(row.get("progress_ticks", 0)) / float(construction_ticks), 0.08, 1.0)
		var body := root.get_node("Body") as MeshInstance3D
		body.scale.y = progress
		body.position.y = progress * 0.75
		var label := root.get_node("Label") as Label3D
		var status := "READY" if bool(row.get("complete", false)) else "BUILD %d%%" % int(progress * 100.0)
		var efficiency := int(row.get("efficiency_permille", 1000))
		var efficiency_text := "  %d%%" % (efficiency / 10) if int(row.get("role", 2)) == 1 else ""
		label.text = "%s%s\n%d / %d" % [status, efficiency_text, int(row.get("health", 0)), int(row.get("max_health", 0))]
		_sync_rally(id, row)
	for id in _building_views.keys():
		if not live.has(id):
			(_building_views[id] as Node).queue_free()
			_building_views.erase(id)
			if _rally_views.has(id):
				(_rally_views[id] as Node).queue_free()
				_rally_views.erase(id)

func _update_building_selection() -> void:
	if _building_selection == null:
		_building_selection = MeshInstance3D.new()
		_building_selection.name = "SelectedBuildingRing"
		var mesh := TorusMesh.new()
		mesh.inner_radius = 1.48
		mesh.outer_radius = 1.64
		mesh.rings = 24
		mesh.ring_segments = 8
		_building_selection.mesh = mesh
		var material := StandardMaterial3D.new()
		material.albedo_color = Color(0.20, 0.92, 1.0, 0.92)
		material.emission_enabled = true
		material.emission = Color(0.05, 0.44, 0.62)
		material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		_building_selection.material_override = material
		add_child(_building_selection)
	var selected: Node3D = _building_views.get(_selected_building_id)
	_building_selection.visible = selected != null and selected.visible
	if _building_selection.visible:
		_building_selection.position = selected.position + Vector3(0.0, 0.10, 0.0)

func _make_building(role: int, team: int) -> Node3D:
	var root := Node3D.new()
	var body := MeshInstance3D.new()
	body.name = "Body"
	var mesh: PrimitiveMesh
	if role == 1:
		var cylinder := CylinderMesh.new()
		cylinder.top_radius = 1.1
		cylinder.bottom_radius = 1.35
		cylinder.height = 1.5
		mesh = cylinder
	else:
		var box := BoxMesh.new()
		box.size = Vector3(2.4, 1.5, 2.0)
		mesh = box
	body.mesh = mesh
	var material := StandardMaterial3D.new()
	material.albedo_color = Color(0.18, 0.48, 0.92) if team == 0 else Color(0.86, 0.22, 0.17)
	material.roughness = 0.78
	body.material_override = material
	root.add_child(body)
	var label := Label3D.new()
	label.name = "Label"
	label.position = Vector3(0.0, 2.25, 0.0)
	label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	label.font_size = 30
	label.pixel_size = 0.015
	label.outline_size = 7
	root.add_child(label)
	return root

func _sync_rally(building_id: int, row: Dictionary) -> void:
	var has_rally := bool(row.get("has_rally", false))
	if not has_rally:
		if _rally_views.has(building_id):
			(_rally_views[building_id] as Node).queue_free()
			_rally_views.erase(building_id)
		return
	var marker: MeshInstance3D = _rally_views.get(building_id)
	if marker == null:
		marker = MeshInstance3D.new()
		marker.name = "Rally_%d" % building_id
		var mesh := CylinderMesh.new()
		mesh.top_radius = 0.55
		mesh.bottom_radius = 0.55
		mesh.height = 0.035
		marker.mesh = mesh
		var material := StandardMaterial3D.new()
		material.albedo_color = Color(1.0, 0.82, 0.16, 0.72)
		material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		marker.material_override = material
		add_child(marker)
		_rally_views[building_id] = marker
	marker.position = fixed_to_world(row.get("rally", Vector2i.ZERO), 0.05)

class_name RetailOrderIndicator
extends Node3D
## Presentation-only rendering of one authoritative battalion route.
## The simulation owns every point; this node only turns the remaining route into
## a readable BFME-style ground ribbon and destination banner.

const AssetFactory = preload("res://src/view/asset_factory.gd")
const RETAIL_CONTRACT_PATH := "effects/men-order-hint.json"
const RETAIL_CONTRACT_SCHEMA := "openbfme.men-order-hint"
const RETAIL_MODEL_PATH := "assets/models/system/scmovehint.glb"

var route_points: Array[Vector2] = []
var destination := Vector2.ZERO
var showing_order := false
var private_parity_mode_active := false
var retail_contract_ready := false
var retail_model_path := ""
var _pack_root := ""
var _source_unit_scale := 0.0
var _line: MeshInstance3D
var _flag_root: Node3D
var _retail_hint: Node3D
var _line_material: StandardMaterial3D


func configure(pack_root: String, source_unit_scale: float) -> void:
	_pack_root = pack_root
	_source_unit_scale = source_unit_scale


func _ready() -> void:
	private_parity_mode_active = _private_retail_pack_selected(_pack_root)
	_build_visuals()
	clear_order()


func sync_from_entity(entity: Dictionary, selected: bool, height_sampler: Callable) -> void:
	if private_parity_mode_active:
		_sync_retail_hint(entity, selected, height_sampler)
		return
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
	if _retail_hint != null:
		_retail_hint.visible = false


func _build_visuals() -> void:
	# Private parity uses the exact MoveHint -> SCMoveHint source chain. The
	# repository-authored ribbon/flag below remains public fallback only.
	if private_parity_mode_active:
		_build_retail_hint()
		return
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


func synthetic_overlay_node_count() -> int:
	var result := 0
	if _line != null and is_instance_valid(_line):
		result += 1
	if _flag_root != null and is_instance_valid(_flag_root):
		result += 1
		result += _flag_root.get_child_count()
	return result


func retail_visual_node_count() -> int:
	return 1 if _retail_hint != null and is_instance_valid(_retail_hint) else 0


func _private_retail_pack_selected(pack_root: String = "") -> bool:
	## Retail order hints require a converted retail pack, which is a property of
	## the pack's import provenance — not of its id. Composed multi-faction packs
	## carry id `bfme2-<a>-<b>-…-vslice`, so the historical literal match here
	## dropped the retail hint for every cross-faction match.
	if pack_root != "":
		return ModLoader.pack_is_retail_import(pack_root)
	var definition: Dictionary = ContentDB.get_bundle_object("bfme2.object.gondor-fighter")
	return ModLoader.pack_is_retail_import(String(definition.get("_pack_root", "")))


func _build_retail_hint() -> void:
	retail_contract_ready = false
	if _pack_root == "" or not is_finite(_source_unit_scale) or _source_unit_scale <= 0.0:
		return
	var contract_path := ModLoader.resolve_pack_path(_pack_root, RETAIL_CONTRACT_PATH)
	if not ModLoader.path_is_within(_pack_root, contract_path) or not FileAccess.file_exists(contract_path):
		return
	var value: Variant = ModLoader._read_json(contract_path)
	if typeof(value) != TYPE_DICTIONARY:
		return
	var contract := value as Dictionary
	if (
		String(contract.get("schema", "")) != RETAIL_CONTRACT_SCHEMA
		or int(contract.get("schemaVersion", -1)) != 0
		or String(contract.get("objectName", "")) != "MoveHint"
		or String(contract.get("gameDataName", "")) != "SCMoveHint"
		or String(contract.get("model", "")) != RETAIL_MODEL_PATH
		or String(contract.get("modelScalePolicy", "")) != "source-map-local-transform-scale"
	):
		return
	retail_model_path = ModLoader.resolve_pack_path(_pack_root, RETAIL_MODEL_PATH)
	if not ModLoader.path_is_within(_pack_root, retail_model_path):
		return
	if AssetFactory.preflight_explicit_model_path(retail_model_path, _pack_root) != "":
		return
	_retail_hint = AssetFactory.make_explicit_model_visual(retail_model_path, 0)
	if _retail_hint == null:
		return
	_retail_hint.name = "RetailSCMoveHint"
	_retail_hint.scale = Vector3.ONE * _source_unit_scale
	_retail_hint.visible = false
	_disable_retail_hint_shadows(_retail_hint)
	add_child(_retail_hint)
	retail_contract_ready = true


func _disable_retail_hint_shadows(node: Node) -> void:
	if node is GeometryInstance3D:
		(node as GeometryInstance3D).cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	for child in node.get_children():
		_disable_retail_hint_shadows(child)


func _sync_retail_hint(entity: Dictionary, selected: bool, height_sampler: Callable) -> void:
	if not retail_contract_ready or not selected or int(entity.get("health", 0)) <= 0:
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
	var final_height := float(height_sampler.call(destination)) if height_sampler.is_valid() else 0.35
	_retail_hint.position = Vector3(destination.x, final_height + 0.02, destination.y)
	_retail_hint.visible = true
	showing_order = true


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

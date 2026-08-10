class_name RetailFordsBattlefield
extends Node3D
## Bounded presentation of the importer's cooked Fords facts.
##
## Terrain uses every cooked source-height sample with passability vertex colors.
## Water uses the cooked standing-area and river-strip geometry. Exact bound
## object types mount converted retail GLBs; unresolved types and ford gates are
## retained as non-rendered diagnostics until exact retail presentation exists.

# Bounds cover the largest cooked map in the five-map pack (Mount Doom at
# 600x600) while staying below the 1000x1000 grid the fail-closed probes feed.
const MAX_EXACT_TERRAIN_VERTICES := 750_000
const MAX_EXACT_TERRAIN_TRIANGLES := 1_500_000
const MAX_WATER_TRIANGLES := 4096
const ROAD_SAMPLE_STEP_SOURCE := 10.0
const ROAD_HEIGHT_BIAS_SOURCE := 0.1
const ROAD_HEIGHT_DELTA_SOURCE := 0.001
const ROAD_MIN_CURVE_ANGLE := 0.47
const ROAD_BROAD_CURVE_RADIUS := 3.3
const ROAD_OVERLAP_LENGTH := 0.015
const ROAD_CURVE_SEGMENT_ANGLE := PI / 6.0
const ROAD_CURVE_OVERLAP_ANGLE := deg_to_rad(2.0)
const TerrainMaterialBuilderScript = preload("res://src/retail_slice/retail_terrain_material_builder.gd")
const RoadMaterialBuilderScript = preload("res://src/retail_slice/retail_road_material_builder.gd")
const AssetFactoryScript = preload("res://src/view/asset_factory.gd")
const RetailStructureScript = preload("res://src/retail_slice/retail_structure.gd")
const RetailAnimatedPropControllerScript = preload("res://src/retail_slice/retail_animated_prop_controller.gd")
const RetailParticleControllerScript = preload("res://src/retail_slice/retail_particle_controller.gd")
const MAIN_CAMERA_EXCLUDED_PROP_TYPES := {
	# FarmTemplate's retail INI declares DefaultModelConditionState Model=None.
	# GBFarm_WB is only selected by ModelConditionState=WORLD_BUILDER, so the
	# converted editor marker must remain present for provenance but invisible
	# during normal play.
	"FarmTemplate": "default-model-none-world-builder-only",
	# Retail KindOf is SKYBOX INERT CAN_CAST_REFLECTIONS. The exact reflection
	# camera/pass contract is unresolved, so displaying this as world geometry
	# would be a known classification error.
	"WaterReflectionSkydome_GapOfRohan": "water-reflection-source-render-pass-unresolved",
}

var source_driven := false
var error := ""
var terrain_exact_grid_ready := false
var terrain_vertex_count := 0
var terrain_triangle_count := 0
var impassable_vertex_count := 0
var terrain_material_source_driven := false
var terrain_texture_array_size := 0
var terrain_texture_array_dimension := 0
var terrain_tile_data_cell_count := 0
var terrain_primary_blend_cell_count := 0
var terrain_three_way_blend_cell_count := 0
var terrain_cliff_cell_count := 0
var passability_debug_enabled := false
var water_surface_count := 0
var water_triangle_count := 0
var road_material_source_driven := false
var road_material_count := 0
var road_source_edge_count := 0
var road_unique_endpoint_count := 0
var road_shared_node_count := 0
var road_curve_candidate_node_count := 0
var road_generated_broad_curve_count := 0
var road_straight_fallback_node_count := 0
var road_generated_curve_evidence: Array[Dictionary] = []
var road_curve_fallback_evidence: Array[Dictionary] = []
var road_curve_strip_count := 0
var road_render_strip_count := 0
var road_vertex_count := 0
var road_triangle_count := 0
var road_mesh_instance_count := 0
var road_container: Node3D
var road_materials: Dictionary = {}
# Compatibility name used by the slice controller. This now counts validated
# source ford gates; no visible ford-marker geometry is created.
var ford_marker_count := 0
var generic_prop_count := 0
var ford_gate_diagnostics: Array[Dictionary] = []
var unresolved_prop_diagnostics: Dictionary = {}
var bound_retail_prop_count := 0
var bound_retail_mesh_instance_count := 0
var bound_retail_prop_type_ids: Array[String] = []
var bound_retail_structure_count := 0
var bound_retail_structure_type_ids: Array[String] = []
var animated_prop_contract_active := false
var animated_prop_controlled_placement_count := 0
var animated_prop_controlled_type_ids: Array[String] = []
var animated_prop_parity_ready := false
var animated_prop_diagnostics: Array[Dictionary] = []
var particle_contract_active := false
var particle_contract_ready := false
var particle_presentation_ready := false
var particle_parity_ready := false
var particle_validated_definition_count := 0
var particle_validated_texture_count := 0
var particle_provisional_selection_count := 0
var particle_unresolved_family_selection_count := 0
var particle_emitter_count := 0
var particle_diagnostics: Array[Dictionary] = []
var observability_enabled := false
var structure_damage_route_log: Array[Dictionary] = []
var last_structure_damage_route: Dictionary = {}
var unresolved_prop_placement_count := 0
var unresolved_prop_type_ids: Array[String] = []
var retail_prop_container: Node3D
var retail_structure_container: Node3D
var animated_prop_controller: RetailAnimatedPropController
var particle_controller: Node3D
var terrain_mesh_instance: MeshInstance3D
var water_mesh_instance: MeshInstance3D
var terrain_material: ShaderMaterial
var terrain_texture_array: Texture2DArray
var terrain_tile_data_texture: Texture2D
var terrain_blend_data_texture: Texture2D
var terrain_cliff_info_texture: Texture2D


func configure(map_data: RetailMapData) -> bool:
	var profile_init := OS.get_environment("OPENBFME_PROFILE_INIT") == "1"
	var profile_last_ms := Time.get_ticks_msec()
	_clear_generated()
	if map_data == null or not map_data.ready:
		return _fail_configuration("retail map data is unavailable")
	if not _build_bound_retail_props(map_data):
		return false
	if profile_init:
		print("RETAIL_BATTLEFIELD_PHASE name=bound_props delta_ms=%d" % (Time.get_ticks_msec() - profile_last_ms))
		profile_last_ms = Time.get_ticks_msec()
	if not _configure_animated_prop_runtime(map_data):
		return _abort_configuration(error if error != "" else "animated retail prop runtime could not be configured")
	if profile_init:
		print("RETAIL_BATTLEFIELD_PHASE name=animated_props delta_ms=%d" % (Time.get_ticks_msec() - profile_last_ms))
		profile_last_ms = Time.get_ticks_msec()
	if not _configure_particle_runtime(map_data):
		return _abort_configuration(error if error != "" else "retail particle runtime could not be configured")
	if profile_init:
		print("RETAIL_BATTLEFIELD_PHASE name=particles delta_ms=%d" % (Time.get_ticks_msec() - profile_last_ms))
		profile_last_ms = Time.get_ticks_msec()
	if not _build_bound_retail_structures(map_data):
		return _abort_configuration(error if error != "" else "bound retail structures could not be built")
	if profile_init:
		print("RETAIL_BATTLEFIELD_PHASE name=bound_structures delta_ms=%d" % (Time.get_ticks_msec() - profile_last_ms))
		profile_last_ms = Time.get_ticks_msec()
	if not _build_terrain(map_data):
		return _abort_configuration(error if error != "" else "source terrain could not be built")
	if profile_init:
		print("RETAIL_BATTLEFIELD_PHASE name=terrain delta_ms=%d" % (Time.get_ticks_msec() - profile_last_ms))
		profile_last_ms = Time.get_ticks_msec()
	if not _build_roads(map_data):
		return _abort_configuration(error if error != "" else "source roads could not be built")
	if profile_init:
		print("RETAIL_BATTLEFIELD_PHASE name=roads delta_ms=%d" % (Time.get_ticks_msec() - profile_last_ms))
		profile_last_ms = Time.get_ticks_msec()
	if not _build_water(map_data):
		return _abort_configuration("source water could not be built")
	if profile_init:
		print("RETAIL_BATTLEFIELD_PHASE name=water delta_ms=%d" % (Time.get_ticks_msec() - profile_last_ms))
	if not _record_ford_gate_diagnostics(map_data):
		return _abort_configuration(error if error != "" else "source ford-gate diagnostics are incomplete")
	if not _record_unresolved_prop_diagnostics(map_data):
		return _abort_configuration(error if error != "" else "source unresolved-prop diagnostics are incomplete")
	# Road and water layers are per-map optional: Fords of Isen II authors both,
	# other cooked maps may have no road network and (in principle) no water.
	# Material-backed roads remain exact. Geometry without a material catalog is
	# an intentional incomplete presentation: it boots with an empty render layer.
	# Road material completeness is `road_material_source_driven` only — never
	# treat geometry-only as textured-road parity. Overall `source_driven` means
	# the battlefield is boot-ready from source layers (terrain exact, roads
	# exact|vacuous|geometry-only empty, water/prop diagnostics consistent).
	var roads_exact := (
		road_material_source_driven
		and road_material_count == map_data.road_material_count
		and road_source_edge_count == map_data.road_segment_count
		and road_unique_endpoint_count == map_data.road_unique_endpoint_count
		and road_shared_node_count == map_data.road_shared_node_count
		and road_curve_candidate_node_count == map_data.road_curve_candidate_node_count
		and road_generated_broad_curve_count > 0
		and road_generated_broad_curve_count + road_straight_fallback_node_count == map_data.road_curve_candidate_node_count
		and road_vertex_count > 0
		and road_triangle_count > 0
		and road_mesh_instance_count == map_data.road_material_count
	)
	var roads_vacuous := (
		map_data.road_type_count == 0
		and road_material_count == 0
		and road_source_edge_count == 0
		and road_vertex_count == 0
		and road_triangle_count == 0
		and road_mesh_instance_count == 0
	)
	var roads_geometry_only := (
		map_data.road_type_count > 0
		and map_data.road_segment_count > 0
		and map_data.road_material_count == 0
		and not road_material_source_driven
		and road_material_count == 0
		and road_source_edge_count == 0
		and road_vertex_count == 0
		and road_triangle_count == 0
		and road_mesh_instance_count == 0
	)
	var map_declares_water := map_data.standing_water_count + map_data.river_count > 0
	source_driven = (
		terrain_material_source_driven
		and terrain_exact_grid_ready
		and (roads_exact or roads_vacuous or roads_geometry_only)
		and terrain_vertex_count == map_data.width * map_data.height
		and terrain_triangle_count == (map_data.width - 1) * (map_data.height - 1) * 2
		and impassable_vertex_count == map_data.impassable_count
		and (not map_declares_water or water_surface_count > 0)
		and ford_marker_count == map_data.ford_gates.size()
		and generic_prop_count == map_data.generic_prop_placements.size()
		and unresolved_prop_placement_count == map_data.unresolved_prop_placement_count
		and unresolved_prop_type_ids == map_data.unresolved_prop_type_ids
		and bound_retail_prop_count == map_data.bound_prop_placement_count
		and bound_retail_prop_type_ids == map_data.bound_prop_type_ids
		and bound_retail_structure_count == map_data.bound_structure_placement_count
		and bound_retail_structure_type_ids == map_data.bound_structure_type_ids
		and (
			not animated_prop_contract_active
			or (
				animated_prop_controller != null
				and animated_prop_controller.contract_ready
				and animated_prop_parity_ready
				and animated_prop_controlled_placement_count == 26
			)
		)
		and (
			not particle_contract_active
			or (
				particle_controller != null
				and particle_contract_ready
				and particle_presentation_ready
				and particle_validated_definition_count == 35
				and particle_validated_texture_count == 15
				and particle_provisional_selection_count == 1
				and particle_unresolved_family_selection_count == 10
				and particle_emitter_count == 7
			)
		)
	)
	if animated_prop_contract_active and not animated_prop_parity_ready and error == "":
		error = "animated retail prop runtime retained exact geometry but failed animation parity"
	return source_driven


func _build_terrain(map_data: RetailMapData) -> bool:
	var contract_error := _exact_terrain_contract_error(map_data)
	if contract_error != "":
		return _fail_configuration(contract_error)
	var width := int(map_data.width)
	var height := int(map_data.height)
	var vertex_count := width * height
	var triangle_count := (width - 1) * (height - 1) * 2

	var vertices := PackedVector3Array()
	var normals := PackedVector3Array()
	var uvs := PackedVector2Array()
	var colors := PackedColorArray()
	var indices := PackedInt32Array()
	vertices.resize(vertex_count)
	normals.resize(vertex_count)
	uvs.resize(vertex_count)
	colors.resize(vertex_count)
	indices.resize(triangle_count * 3)
	for y in range(height):
		var row_offset := y * width
		for x in range(width):
			var vertex_index := row_offset + x
			var position := map_data.terrain_local_at(x, y)
			if not _finite_vector3(position):
				return _fail_configuration("exact terrain contains a non-finite source vertex")
			vertices[vertex_index] = position
			# Integer grid UVs are the authored SAGE terrain-cell coordinate system.
			uvs[vertex_index] = Vector2(float(x), float(y))
			var blocked := map_data.is_impassable_at(x, y)
			if blocked:
				impassable_vertex_count += 1
				colors[vertex_index] = Color(1.0, 0.0, 0.0, 1.0)
			else:
				colors[vertex_index] = Color(0.0, 0.0, 0.0, 1.0)
	if impassable_vertex_count != map_data.impassable_count:
		return _fail_configuration("exact terrain passability count changed during mesh construction")

	var write_index := 0
	for row in range(height - 1):
		for column in range(width - 1):
			var top_left := row * width + column
			var top_right := top_left + 1
			var bottom_left := top_left + width
			var bottom_right := bottom_left + 1
			# Same source diagonal for every heightfield quad, wound upward after
			# SAGE's horizontal plane is transformed into Godot X/Z.
			indices[write_index] = top_left
			indices[write_index + 1] = top_right
			indices[write_index + 2] = bottom_left
			indices[write_index + 3] = bottom_left
			indices[write_index + 4] = top_right
			indices[write_index + 5] = bottom_right
			write_index += 6
	if write_index != indices.size():
		return _fail_configuration("exact terrain quad coverage is internally inconsistent")
	normals = _calculate_source_grid_normals(vertices, width, height)
	if normals.size() != vertex_count:
		return _fail_configuration("exact terrain normals do not cover the source grid")

	var arrays: Array = []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices
	arrays[Mesh.ARRAY_NORMAL] = normals
	arrays[Mesh.ARRAY_TEX_UV] = uvs
	arrays[Mesh.ARRAY_COLOR] = colors
	arrays[Mesh.ARRAY_INDEX] = indices
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	var material_builder = TerrainMaterialBuilderScript.new()
	terrain_material = material_builder.build(map_data)
	if terrain_material == null:
		error = String(material_builder.error)
		return false
	terrain_material.set_shader_parameter("passability_debug_enabled", passability_debug_enabled)
	terrain_texture_array = material_builder.texture_array
	terrain_tile_data_texture = material_builder.tile_data_texture
	terrain_blend_data_texture = material_builder.blend_data_texture
	terrain_cliff_info_texture = material_builder.cliff_info_texture
	terrain_texture_array_size = int(material_builder.texture_array_size)
	terrain_texture_array_dimension = int(material_builder.texture_array_dimension)
	terrain_tile_data_cell_count = int(material_builder.tile_data_cell_count)
	terrain_primary_blend_cell_count = map_data.terrain_nonzero_blend_cell_count
	terrain_three_way_blend_cell_count = map_data.terrain_nonzero_three_way_blend_cell_count
	terrain_cliff_cell_count = map_data.terrain_nonzero_cliff_cell_count
	terrain_material_source_driven = (
		terrain_texture_array != null
		and terrain_texture_array_size == map_data.terrain_texture_count
		and terrain_tile_data_texture != null
		and terrain_blend_data_texture != null
		and terrain_cliff_info_texture != null
		and terrain_tile_data_cell_count == map_data.width * map_data.height
	)
	if not terrain_material_source_driven:
		error = "retail terrain material resources are incomplete"
		return false
	mesh.surface_set_material(0, terrain_material)
	terrain_mesh_instance = MeshInstance3D.new()
	terrain_mesh_instance.name = "SourceHeightAndPassabilityTerrain"
	terrain_mesh_instance.mesh = mesh
	terrain_mesh_instance.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_ON
	terrain_mesh_instance.set_meta("source", "cooked-heightmap-retail-materials-blends-cliffs-and-passability")
	terrain_mesh_instance.set_meta("grid_width", width)
	terrain_mesh_instance.set_meta("grid_height", height)
	terrain_mesh_instance.set_meta("source_quad_count", (width - 1) * (height - 1))
	terrain_mesh_instance.set_meta("source_exact", true)
	terrain_mesh_instance.set_meta("normal_policy", "source-grid-four-surrounding-quad-average")
	terrain_mesh_instance.set_meta("terrain_texture_layers", terrain_texture_array_size)
	terrain_mesh_instance.set_meta("primary_blend_cells", terrain_primary_blend_cell_count)
	terrain_mesh_instance.set_meta("three_way_blend_cells", terrain_three_way_blend_cell_count)
	terrain_mesh_instance.set_meta("cliff_cells", terrain_cliff_cell_count)
	add_child(terrain_mesh_instance)
	terrain_vertex_count = vertices.size()
	terrain_triangle_count = indices.size() / 3
	terrain_exact_grid_ready = (
		terrain_vertex_count == vertex_count
		and terrain_triangle_count == triangle_count
		and impassable_vertex_count == map_data.impassable_count
	)
	return terrain_exact_grid_ready


func set_passability_debug(enabled: bool) -> void:
	passability_debug_enabled = enabled
	if terrain_material != null:
		terrain_material.set_shader_parameter("passability_debug_enabled", enabled)


func _build_roads(map_data: RetailMapData) -> bool:
	if map_data.road_type_count == 0:
		# Maps whose cook emitted no road network are valid: nothing is built
		# and the source_driven gate below treats the road layer as vacuous.
		if map_data.road_segment_count != 0 or map_data.road_material_count != 0 or map_data.road_control_point_count != 0:
			return _fail_configuration("roadless source map retained nonzero road facts")
		return true
	if map_data.road_segment_count <= 0:
		return _fail_configuration("Fords road topology contains unsupported or incomplete source facts")
	if map_data.road_material_count == 0:
		# Match RetailMapData's geometry-only boot contract: retain exact source
		# geometry as diagnostics, but build no meshes and invent no road textures.
		return true
	if (
		map_data.road_material_count != map_data.road_type_count
		or map_data.road_modifier_flags_or != 0
		or map_data.road_crossing_candidate_node_count != 0
	):
		return _fail_configuration("Fords road topology contains unsupported or incomplete source facts")
	var material_builder = RoadMaterialBuilderScript.new()
	road_materials = material_builder.build(map_data)
	if road_materials.is_empty():
		return _fail_configuration(String(material_builder.error))
	road_material_count = int(material_builder.texture_count)
	road_material_source_driven = road_material_count == map_data.road_material_count
	if not road_material_source_driven:
		return _fail_configuration("retail road material resources are incomplete")

	var topology := _road_topology(map_data)
	var edges: Array[Dictionary] = topology.get("edges", [])
	var node_edges: Dictionary = topology.get("node_edges", {})
	if edges.size() != map_data.road_segment_count or node_edges.size() != map_data.road_unique_endpoint_count:
		return _fail_configuration("road topology does not exactly cover the cooked source edges")
	_align_road_orientation(edges, node_edges)
	var material_rows := _road_material_rows(map_data)
	if material_rows.size() != map_data.road_material_count:
		return _fail_configuration("road material lookup is incomplete")
	var curve_strips := _insert_broad_road_curves(edges, node_edges, material_rows, map_data)
	if error != "":
		return false
	road_source_edge_count = edges.size()
	road_unique_endpoint_count = node_edges.size()
	road_shared_node_count = map_data.road_shared_node_count
	road_curve_candidate_node_count = map_data.road_curve_candidate_node_count
	road_straight_fallback_node_count = road_curve_candidate_node_count - road_generated_broad_curve_count
	if road_generated_broad_curve_count <= 0 or road_generated_broad_curve_count + road_straight_fallback_node_count != road_curve_candidate_node_count:
		return _fail_configuration("Fords road curve topology is internally incomplete (%d generated / %d fallback)" % [road_generated_broad_curve_count, road_straight_fallback_node_count])

	var geometry_by_id: Dictionary = {}
	for road_id in map_data.road_type_ids:
		geometry_by_id[road_id] = {
			"vertices": [],
			"normals": [],
			"uvs": [],
			"indices": [],
		}
	for edge in edges:
		var road_id := String(edge.get("road_id", ""))
		if not geometry_by_id.has(road_id) or not material_rows.has(road_id):
			return _fail_configuration("road edge has no exact material group")
		var row: Dictionary = material_rows[road_id]
		var half_width := float(row.get("road_width", 0.0)) * float(row.get("road_width_in_texture", 0.0)) * map_data.local_transform_scale * 0.5
		var start_to_top := _straight_road_border(edge, true, half_width, edges, node_edges)
		var end_to_top := _straight_road_border(edge, false, half_width, edges, node_edges)
		if start_to_top == Vector2.INF or end_to_top == Vector2.INF:
			return _fail_configuration("straight road seam could not be derived from topology")
		var uv_data := {
			"mode": "straight",
			"road_width": float(row.get("road_width", 0.0)) * map_data.local_transform_scale,
			"width_in_texture": float(row.get("road_width_in_texture", 0.0)),
		}
		if not _append_road_strip(geometry_by_id[road_id], map_data, Vector2(edge.get("start", Vector2.INF)), Vector2(edge.get("end", Vector2.INF)), start_to_top, end_to_top, uv_data):
			return _fail_configuration("straight road strip could not be terrain draped")
		road_render_strip_count += 1
	for strip in curve_strips:
		var road_id := String(strip.get("road_id", ""))
		if not geometry_by_id.has(road_id):
			return _fail_configuration("curve strip has no exact material group")
		if not _append_road_strip(
			geometry_by_id[road_id],
			map_data,
			Vector2(strip.get("start", Vector2.INF)),
			Vector2(strip.get("end", Vector2.INF)),
			Vector2(strip.get("start_to_top", Vector2.INF)),
			Vector2(strip.get("end_to_top", Vector2.INF)),
			strip.get("uv", {}) as Dictionary
		):
			return _fail_configuration("broad-curve road strip could not be terrain draped")
		road_curve_strip_count += 1
		road_render_strip_count += 1

	var container := Node3D.new()
	container.name = "SourceRetailRoads"
	container.set_meta("presentation", "retail-sage-road-topology")
	container.set_meta("source_edges", road_source_edge_count)
	container.set_meta("shared_nodes", road_shared_node_count)
	container.set_meta("curve_candidates", road_curve_candidate_node_count)
	container.set_meta("generated_broad_curves", road_generated_broad_curve_count)
	for road_id in map_data.road_type_ids:
		var geometry: Dictionary = geometry_by_id[road_id]
		var vertices := PackedVector3Array(geometry.get("vertices", []))
		var normals := PackedVector3Array(geometry.get("normals", []))
		var uvs := PackedVector2Array(geometry.get("uvs", []))
		var indices := PackedInt32Array(geometry.get("indices", []))
		if vertices.is_empty() or vertices.size() != normals.size() or vertices.size() != uvs.size() or indices.is_empty() or indices.size() % 3 != 0:
			container.free()
			return _fail_configuration("road material group produced incomplete adaptive geometry")
		var arrays: Array = []
		arrays.resize(Mesh.ARRAY_MAX)
		arrays[Mesh.ARRAY_VERTEX] = vertices
		arrays[Mesh.ARRAY_NORMAL] = normals
		arrays[Mesh.ARRAY_TEX_UV] = uvs
		arrays[Mesh.ARRAY_INDEX] = indices
		var mesh := ArrayMesh.new()
		mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
		mesh.surface_set_material(0, road_materials[road_id])
		var instance := MeshInstance3D.new()
		instance.name = "RetailRoad_%s" % road_id
		instance.mesh = mesh
		instance.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		instance.set_meta("source", "cooked-road-topology-open-sage-derived-meshing")
		instance.set_meta("road_id", road_id)
		instance.set_meta("source_edge_count", _road_edge_count(edges, road_id))
		container.add_child(instance)
		road_vertex_count += vertices.size()
		road_triangle_count += indices.size() / 3
		road_mesh_instance_count += 1
	if container.get_child_count() != map_data.road_material_count:
		container.free()
		return _fail_configuration("road mesh groups do not exactly cover the material inventory")
	add_child(container)
	road_container = container
	return road_vertex_count > 0 and road_triangle_count > 0


func _road_topology(map_data: RetailMapData) -> Dictionary:
	var edges: Array[Dictionary] = []
	var node_edges: Dictionary = {}
	for segment in map_data.road_segments:
		var source_start := Vector3(segment.get("godot_start", Vector3.INF))
		var source_end := Vector3(segment.get("godot_end", Vector3.INF))
		var local_start_3 := map_data.source_to_local(source_start)
		var local_end_3 := map_data.source_to_local(source_end)
		var edge := {
			"index": int(segment.get("index", -1)),
			"road_id": String(segment.get("road_id", "")),
			"source_start": source_start,
			"source_end": source_end,
			"start": Vector2(local_start_3.x, local_start_3.z),
			"end": Vector2(local_end_3.x, local_end_3.z),
			"start_type": int(segment.get("start_wire_type", -1)),
			"end_type": int(segment.get("end_wire_type", -1)),
			"aligned_like": -1,
		}
		edges.append(edge)
		for node in [source_start, source_end]:
			if not node_edges.has(node):
				node_edges[node] = []
			var incident: Array = node_edges[node]
			incident.append(edges.size() - 1)
	return {"edges": edges, "node_edges": node_edges}


func _align_road_orientation(edges: Array[Dictionary], node_edges: Dictionary) -> void:
	for edge_index in range(edges.size()):
		var edge: Dictionary = edges[edge_index]
		if int(edge.get("aligned_like", -1)) >= 0:
			continue
		edge["aligned_like"] = edge_index
		_walk_road_alignment(edge_index, Vector3(edge.get("source_start", Vector3.INF)), edges, node_edges)
		_walk_road_alignment(edge_index, Vector3(edge.get("source_end", Vector3.INF)), edges, node_edges)


func _walk_road_alignment(first_edge_index: int, first_node: Vector3, edges: Array[Dictionary], node_edges: Dictionary) -> void:
	var current_edge_index := first_edge_index
	var current_node := first_node
	while true:
		var next_edge_index := _next_alignment_edge(current_edge_index, current_node, edges, node_edges)
		if next_edge_index < 0:
			return
		var current: Dictionary = edges[current_edge_index]
		var next: Dictionary = edges[next_edge_index]
		next["aligned_like"] = int(current.get("aligned_like", current_edge_index))
		if Vector3(next.get("source_start", Vector3.INF)) == Vector3(current.get("source_start", Vector3.INF)) or Vector3(next.get("source_end", Vector3.INF)) == Vector3(current.get("source_end", Vector3.INF)):
			_swap_road_edge(next)
		current_edge_index = next_edge_index
		current = edges[current_edge_index]
		current_node = Vector3(current.get("source_end", Vector3.INF)) if current_node == Vector3(current.get("source_start", Vector3.INF)) else Vector3(current.get("source_start", Vector3.INF))


func _next_alignment_edge(edge_index: int, node: Vector3, edges: Array[Dictionary], node_edges: Dictionary) -> int:
	var incident: Array = node_edges.get(node, [])
	if incident.size() <= 1:
		return -1
	if int(incident[0]) == edge_index and int(edges[int(incident[1])].get("aligned_like", -1)) < 0:
		return int(incident[1])
	if int(incident[1]) == edge_index and int(edges[int(incident[0])].get("aligned_like", -1)) < 0:
		return int(incident[0])
	return -1


func _swap_road_edge(edge: Dictionary) -> void:
	var source: Vector3 = edge["source_start"]
	edge["source_start"] = edge["source_end"]
	edge["source_end"] = source
	var position: Vector2 = edge["start"]
	edge["start"] = edge["end"]
	edge["end"] = position
	var wire_type: int = edge["start_type"]
	edge["start_type"] = edge["end_type"]
	edge["end_type"] = wire_type


func _road_material_rows(map_data: RetailMapData) -> Dictionary:
	var result: Dictionary = {}
	for row in map_data.road_material_catalog:
		var road_id := String(row.get("id", ""))
		if road_id == "" or result.has(road_id):
			return {}
		result[road_id] = row
	return result


func _insert_broad_road_curves(edges: Array[Dictionary], node_edges: Dictionary, material_rows: Dictionary, map_data: RetailMapData) -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	road_generated_broad_curve_count = 0
	road_generated_curve_evidence.clear()
	road_curve_fallback_evidence.clear()
	for node_value in node_edges.keys():
		var node := Vector3(node_value)
		var per_road: Dictionary = {}
		for edge_index_value in Array(node_edges[node]):
			var edge_index := int(edge_index_value)
			var road_id := String(edges[edge_index].get("road_id", ""))
			if not per_road.has(road_id):
				per_road[road_id] = []
			var same_road: Array = per_road[road_id]
			same_road.append(edge_index)
		for road_id_value in per_road.keys():
			var road_id := String(road_id_value)
			var incident: Array = per_road[road_id]
			if incident.size() != 2:
				continue
			var records: Array[Dictionary] = []
			for edge_index_value in incident:
				var edge_index := int(edge_index_value)
				var edge: Dictionary = edges[edge_index]
				var node_local := Vector2(edge.get("start", Vector2.INF)) if Vector3(edge.get("source_start", Vector3.INF)) == node else Vector2(edge.get("end", Vector2.INF))
				var other_local := Vector2(edge.get("end", Vector2.INF)) if Vector3(edge.get("source_start", Vector3.INF)) == node else Vector2(edge.get("start", Vector2.INF))
				var out_direction := (other_local - node_local).normalized()
				if out_direction == Vector2.ZERO:
					_fail_configuration("zero-length edge reached road curve topology")
					return []
				var source_angle := fposmod(atan2(-out_direction.y, out_direction.x), TAU)
				records.append({"edge_index": edge_index, "out": out_direction, "angle": source_angle, "node_local": node_local})
			records.sort_custom(func(a: Dictionary, b: Dictionary) -> bool: return float(a["angle"]) < float(b["angle"]))
			var first_gap := TAU + float(records[0]["angle"]) - float(records[1]["angle"])
			var second_gap := float(records[1]["angle"]) - float(records[0]["angle"])
			var start_record: Dictionary = records[0] if first_gap <= second_gap else records[1]
			var end_record: Dictionary = records[1] if first_gap <= second_gap else records[0]
			var inner_angle := minf(first_gap, second_gap)
			var curve_angle := PI - inner_angle
			var edge_pair: Array[int] = [int(incident[0]), int(incident[1])]
			edge_pair.sort()
			var base_evidence := {
				"road_id": road_id,
				"source_node": node,
				"edge_indices": edge_pair,
				"inner_angle_radians": inner_angle,
				"curve_angle_radians": curve_angle,
			}
			if curve_angle < ROAD_MIN_CURVE_ANGLE:
				var below_minimum := base_evidence.duplicate(true)
				below_minimum["reason"] = "below-minimum-curve-angle"
				road_curve_fallback_evidence.append(below_minimum)
				continue
			if not material_rows.has(road_id):
				_fail_configuration("curve candidate has no exact road material")
				return []
			var row: Dictionary = material_rows[road_id]
			var half_width := float(row.get("road_width", 0.0)) * float(row.get("road_width_in_texture", 0.0)) * map_data.local_transform_scale * 0.5
			var radius := ROAD_BROAD_CURVE_RADIUS * half_width
			var sine := sin(inner_angle * 0.5)
			var to_center_direction := (Vector2(start_record["out"]) + Vector2(end_record["out"])).normalized()
			if absf(sine) <= 0.000001 or to_center_direction == Vector2.ZERO:
				var degenerate := base_evidence.duplicate(true)
				degenerate["reason"] = "degenerate-curve-center"
				road_curve_fallback_evidence.append(degenerate)
				continue
			var node_local := Vector2(start_record["node_local"])
			var to_center := to_center_direction * (radius / sine)
			var center := node_local + to_center
			var start_distance := to_center.dot(Vector2(start_record["out"]))
			var end_distance := to_center.dot(Vector2(end_record["out"]))
			var start_edge: Dictionary = edges[int(start_record["edge_index"])]
			var end_edge: Dictionary = edges[int(end_record["edge_index"])]
			var overlap_distance := 2.0 * ROAD_CURVE_OVERLAP_ANGLE * radius
			var start_edge_length := Vector2(start_edge["end"]).distance_to(Vector2(start_edge["start"]))
			var end_edge_length := Vector2(end_edge["end"]).distance_to(Vector2(end_edge["start"]))
			if pow(start_distance - overlap_distance, 2.0) > pow(start_edge_length, 2.0) or pow(end_distance - overlap_distance, 2.0) > pow(end_edge_length, 2.0):
				var too_short := base_evidence.duplicate(true)
				too_short["reason"] = "tangent-exceeds-edge"
				too_short["start_tangent_source"] = start_distance / map_data.local_transform_scale
				too_short["end_tangent_source"] = end_distance / map_data.local_transform_scale
				too_short["overlap_source"] = overlap_distance / map_data.local_transform_scale
				too_short["start_edge_length_source"] = start_edge_length / map_data.local_transform_scale
				too_short["end_edge_length_source"] = end_edge_length / map_data.local_transform_scale
				road_curve_fallback_evidence.append(too_short)
				continue
			var start_tangent := node_local + Vector2(start_record["out"]) * start_distance
			var end_tangent := node_local + Vector2(end_record["out"]) * end_distance
			var additional_radius := (radius + half_width) * (1.0 - cos(ROAD_CURVE_SEGMENT_ANGLE * 0.5)) / cos(ROAD_CURVE_SEGMENT_ANGLE * 0.5)
			var remaining_angle := curve_angle
			var overlap_rotation_angle := 0.0
			var previous_end_point := start_tangent
			var generated: Array[Dictionary] = []
			while remaining_angle > 0.000001:
				var center_left := _rotate_road_point(previous_end_point, center, -overlap_rotation_angle)
				var up_direction := (center - center_left).normalized()
				var top_left := center_left + up_direction * half_width
				var bottom_left := center_left - up_direction * (half_width + additional_radius)
				var top_right := _rotate_road_point(top_left, center, -ROAD_CURVE_SEGMENT_ANGLE)
				var bottom_right := _rotate_road_point(bottom_left, center, -ROAD_CURVE_SEGMENT_ANGLE)
				var right_span := bottom_right - top_right
				var current_end_point := top_right + right_span.normalized() * half_width
				generated.append({
					"road_id": road_id,
					"start": (top_left + bottom_left) * 0.5,
					"end": (top_right + bottom_right) * 0.5,
					"start_to_top": (top_left - bottom_left) * 0.5,
					"end_to_top": (top_right - bottom_right) * 0.5,
					"uv": _broad_curve_uv(float(row.get("road_width_in_texture", 0.0))),
				})
				previous_end_point = current_end_point
				remaining_angle -= ROAD_CURVE_SEGMENT_ANGLE + overlap_rotation_angle
				overlap_rotation_angle = -maxf(ROAD_CURVE_OVERLAP_ANGLE, ROAD_CURVE_SEGMENT_ANGLE - remaining_angle)
			if generated.is_empty():
				var empty_curve := base_evidence.duplicate(true)
				empty_curve["reason"] = "empty-curve-strip"
				road_curve_fallback_evidence.append(empty_curve)
				continue
			var straight_overlap := 2.0 * ROAD_OVERLAP_LENGTH * float(row.get("road_width", 0.0)) * map_data.local_transform_scale
			_set_road_edge_endpoint(start_edge, node, start_tangent - Vector2(start_record["out"]) * straight_overlap, Vector2(generated[0]["start_to_top"]))
			_set_road_edge_endpoint(end_edge, node, end_tangent - Vector2(end_record["out"]) * straight_overlap, Vector2(generated[-1]["end_to_top"]))
			result.append_array(generated)
			road_generated_broad_curve_count += 1
			var generated_evidence := base_evidence.duplicate(true)
			generated_evidence["reason"] = "generated-broad-curve"
			generated_evidence["start_tangent_source"] = start_distance / map_data.local_transform_scale
			generated_evidence["end_tangent_source"] = end_distance / map_data.local_transform_scale
			generated_evidence["overlap_source"] = overlap_distance / map_data.local_transform_scale
			generated_evidence["start_edge_length_source"] = start_edge_length / map_data.local_transform_scale
			generated_evidence["end_edge_length_source"] = end_edge_length / map_data.local_transform_scale
			generated_evidence["strip_count"] = generated.size()
			road_generated_curve_evidence.append(generated_evidence)
	return result


func _set_road_edge_endpoint(edge: Dictionary, node: Vector3, position: Vector2, border: Vector2) -> void:
	var at_start := Vector3(edge.get("source_start", Vector3.INF)) == node
	edge["start" if at_start else "end"] = position
	var direction := (Vector2(edge.get("end", Vector2.ZERO)) - Vector2(edge.get("start", Vector2.ZERO))).normalized()
	var own_normal := Vector2(-direction.y, direction.x)
	var aligned_border := border if border.dot(own_normal) >= 0.0 else -border
	edge["start_border_override" if at_start else "end_border_override"] = aligned_border


func _straight_road_border(edge: Dictionary, at_start: bool, half_width: float, edges: Array[Dictionary], node_edges: Dictionary) -> Vector2:
	var override_key := "start_border_override" if at_start else "end_border_override"
	if edge.has(override_key):
		return Vector2(edge[override_key])
	var start := Vector2(edge.get("start", Vector2.INF))
	var end := Vector2(edge.get("end", Vector2.INF))
	var direction := (end - start).normalized()
	if direction == Vector2.ZERO:
		return Vector2.INF
	var own_normal := Vector2(-direction.y, direction.x)
	var node := Vector3(edge.get("source_start" if at_start else "source_end", Vector3.INF))
	var same_road_neighbors: Array[Dictionary] = []
	for other_index_value in Array(node_edges.get(node, [])):
		var other: Dictionary = edges[int(other_index_value)]
		if other != edge and String(other.get("road_id", "")) == String(edge.get("road_id", "")):
			same_road_neighbors.append(other)
	if same_road_neighbors.size() != 1:
		return own_normal * half_width
	var other: Dictionary = same_road_neighbors[0]
	var other_direction := (Vector2(other.get("end", Vector2.ZERO)) - Vector2(other.get("start", Vector2.ZERO))).normalized()
	var other_normal := Vector2(-other_direction.y, other_direction.x)
	var miter := (own_normal + other_normal).normalized()
	if miter == Vector2.ZERO:
		return own_normal * half_width
	var cosine := own_normal.dot(miter)
	if absf(cosine) <= 0.0001:
		return own_normal * half_width
	return miter * (half_width / cosine)


func _append_road_strip(geometry: Dictionary, map_data: RetailMapData, start: Vector2, end: Vector2, start_to_top: Vector2, end_to_top: Vector2, uv_data: Dictionary) -> bool:
	if start == Vector2.INF or end == Vector2.INF or start_to_top == Vector2.INF or end_to_top == Vector2.INF or start.distance_squared_to(end) <= 0.0000001:
		return false
	var sample_step := ROAD_SAMPLE_STEP_SOURCE * map_data.local_transform_scale
	var start_ground := map_data.local_ground_height(start)
	var end_ground := map_data.local_ground_height(end)
	var initial_distance := Vector3(start.x, start_ground, start.y).distance_to(Vector3(end.x, end_ground, end.y))
	var section_count := maxi(1, int(initial_distance / sample_step))
	var candidates: Array[Dictionary] = []
	for section in range(section_count + 1):
		var progress := float(section) / float(section_count)
		var center := start.lerp(end, progress)
		var to_top := start_to_top.lerp(end_to_top, progress)
		var cross_section := _road_cross_section(map_data, center, to_top, sample_step)
		cross_section["progress"] = progress
		candidates.append(cross_section)
	var useful: Array[Dictionary] = [candidates[0]]
	var height_threshold := ROAD_HEIGHT_DELTA_SOURCE * map_data.local_transform_scale
	for index in range(1, section_count):
		var interpolated_height := (float(candidates[index - 1]["height"]) + float(candidates[index + 1]["height"])) * 0.5
		if absf(interpolated_height - float(candidates[index]["height"])) > height_threshold:
			useful.append(candidates[index])
	useful.append(candidates[section_count])
	if useful.size() < 2:
		return false
	var center_direction := (end - start).normalized()
	var direction_normal := Vector3(-center_direction.y, 0.0, center_direction.x)
	var distance_along := 0.0
	var distances: Array[float] = [0.0]
	for index in range(1, useful.size()):
		distance_along += Vector3(useful[index]["center"]).distance_to(Vector3(useful[index - 1]["center"]))
		distances.append(distance_along)
	var base_index := Array(geometry["vertices"]).size()
	for index in range(useful.size()):
		var previous_direction := Vector3.ZERO if index == 0 else (Vector3(useful[index]["center"]) - Vector3(useful[index - 1]["center"])).normalized()
		var next_direction := Vector3.ZERO if index == useful.size() - 1 else (Vector3(useful[index + 1]["center"]) - Vector3(useful[index]["center"])).normalized()
		var average_direction := (previous_direction + next_direction).normalized()
		var normal := direction_normal.cross(average_direction).normalized()
		if normal.y < 0.0:
			normal = -normal
		var progress := float(useful[index]["progress"])
		var uv_pair := _road_uv_pair(uv_data, progress, distances[index], start_to_top, end_to_top, center_direction)
		if uv_pair.is_empty():
			return false
		var vertices: Array = geometry["vertices"]
		var normals: Array = geometry["normals"]
		var uvs: Array = geometry["uvs"]
		vertices.append(Vector3(useful[index]["bottom"]))
		vertices.append(Vector3(useful[index]["top"]))
		normals.append(normal)
		normals.append(normal)
		uvs.append(Vector2(uv_pair["bottom"]))
		uvs.append(Vector2(uv_pair["top"]))
	var indices: Array = geometry["indices"]
	for index in range(useful.size() - 1):
		var offset := base_index + index * 2
		indices.append_array([offset, offset + 1, offset + 3, offset, offset + 2, offset + 3])
	return true


func _road_cross_section(map_data: RetailMapData, center: Vector2, to_top: Vector2, sample_step: float) -> Dictionary:
	var sections := maxi(1, int(to_top.length() / sample_step))
	var maximum_height := map_data.local_ground_height(center)
	for index in range(1, sections + 1):
		var scaled := to_top * (float(index) / float(sections))
		maximum_height = maxf(maximum_height, map_data.local_ground_height(center + scaled))
		maximum_height = maxf(maximum_height, map_data.local_ground_height(center - scaled))
	maximum_height += ROAD_HEIGHT_BIAS_SOURCE * map_data.local_transform_scale
	var top := center + to_top
	var bottom := center - to_top
	return {
		"height": maximum_height,
		"center": Vector3(center.x, maximum_height, center.y),
		"top": Vector3(top.x, maximum_height, top.y),
		"bottom": Vector3(bottom.x, maximum_height, bottom.y),
	}


func _road_uv_pair(uv_data: Dictionary, progress: float, distance_along: float, start_to_top: Vector2, end_to_top: Vector2, direction: Vector2) -> Dictionary:
	if String(uv_data.get("mode", "")) == "straight":
		var texture_length := float(uv_data.get("road_width", 0.0)) * 4.0
		var width_in_texture := float(uv_data.get("width_in_texture", 0.0))
		if texture_length <= 0.0 or width_in_texture <= 0.0:
			return {}
		var center_v := 0.166
		var half_v := 0.125 * width_in_texture
		var start_offset := start_to_top.dot(direction) / texture_length
		var end_offset := end_to_top.dot(direction) / texture_length
		var top_offset := lerpf(start_offset, end_offset, progress)
		var u := distance_along / texture_length
		return {
			"top": Vector2(u + top_offset, center_v + half_v),
			"bottom": Vector2(u - top_offset, center_v - half_v),
		}
	if String(uv_data.get("mode", "")) == "broad_curve":
		return {
			"top": Vector2(uv_data.get("top_left", Vector2.ZERO)).lerp(Vector2(uv_data.get("top_right", Vector2.ZERO)), progress),
			"bottom": Vector2(uv_data.get("bottom_left", Vector2.ZERO)).lerp(Vector2(uv_data.get("bottom_right", Vector2.ZERO)), progress),
		}
	return {}


func _broad_curve_uv(width_in_texture: float) -> Dictionary:
	var half_width := 0.125 * width_in_texture
	var radius := 3.5 * half_width
	var cosine := cos(ROAD_CURVE_SEGMENT_ANGLE * 0.5)
	var additional_radius := (radius + half_width) * (1.0 - cosine) / cosine
	var center := Vector2(0.0, 0.498 - radius)
	var top_left := Vector2(0.0, 0.498 - half_width)
	var bottom_left := Vector2(0.0, 0.498 + half_width + additional_radius)
	return {
		"mode": "broad_curve",
		"top_left": top_left,
		"top_right": _rotate_road_point(top_left, center, -ROAD_CURVE_SEGMENT_ANGLE),
		"bottom_left": bottom_left,
		"bottom_right": _rotate_road_point(bottom_left, center, -ROAD_CURVE_SEGMENT_ANGLE),
	}


func _rotate_road_point(point: Vector2, center: Vector2, angle: float) -> Vector2:
	return center + (point - center).rotated(angle)


func _road_edge_count(edges: Array[Dictionary], road_id: String) -> int:
	var result := 0
	for edge in edges:
		if String(edge.get("road_id", "")) == road_id:
			result += 1
	return result


func _build_water(map_data: RetailMapData) -> bool:
	var vertices := PackedVector3Array()
	var normals := PackedVector3Array()
	var colors := PackedColorArray()
	var indices := PackedInt32Array()
	if map_data.standing_water_polygons.is_empty() and map_data.river_strips.is_empty():
		# Maps without authored water are valid; the surface layer stays empty.
		return true
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


func _record_ford_gate_diagnostics(map_data: RetailMapData) -> bool:
	ford_gate_diagnostics.clear()
	for gate_value in map_data.ford_gates:
		var gate: Dictionary = gate_value
		var name := String(gate.get("name", ""))
		var edge_a := Vector2(gate.get("edge_a", Vector2.INF))
		var edge_b := Vector2(gate.get("edge_b", Vector2.INF))
		var elevation := float(gate.get("elevation", NAN))
		var source_river_id := int(gate.get("source_river_id", -1))
		var source_section_index := int(gate.get("source_section_index", -1))
		if (
			name == ""
			or not _finite_vector2(edge_a)
			or not _finite_vector2(edge_b)
			or not _finite_number(elevation)
			or source_river_id < 0
			or source_section_index < 0
			or edge_a.distance_to(edge_b) <= 0.1
		):
			return _fail_configuration("source ford-gate diagnostic is invalid")
		ford_gate_diagnostics.append({
			"name": name,
			"edge_a": edge_a,
			"edge_b": edge_b,
			"elevation": elevation,
			"source_river_id": source_river_id,
			"source_section_index": source_section_index,
		})
	ford_marker_count = ford_gate_diagnostics.size()
	set_meta("source_ford_gate_count", ford_marker_count)
	set_meta("source_ford_gates", ford_gate_diagnostics.duplicate(true))
	set_meta("source_ford_gate_presentation", "non-rendered-diagnostic")
	return ford_marker_count == map_data.ford_gates.size()


func _build_bound_retail_props(map_data: RetailMapData) -> bool:
	bound_retail_prop_type_ids.assign(map_data.bound_prop_type_ids)
	unresolved_prop_type_ids.assign(map_data.unresolved_prop_type_ids)
	unresolved_prop_placement_count = map_data.unresolved_prop_placement_count
	if map_data.bound_prop_placements.size() != map_data.bound_prop_placement_count:
		return _fail_configuration("bound retail placement count disagrees with map data")
	var container := Node3D.new()
	container.name = "SourceBoundRetailProps"
	container.set_meta("presentation", "retail-bound-glb")
	container.set_meta("source_type_ids", bound_retail_prop_type_ids.duplicate())
	var seen_source_indices: Dictionary = {}
	var staged_mesh_count := 0
	# Warm the shared mesh cache with one threaded parse per unique bound GLB;
	# the placement loop below then loads via ordinary cache hits. Only paths
	# that already pass this function's containment/existence checks are queued.
	var bound_glb_paths: Array = []
	for placement_value in map_data.bound_prop_placements:
		var bound_path := String((placement_value as Dictionary).get("glb_path", ""))
		if (
			bound_path != ""
			and bound_path.get_extension().to_lower() == "glb"
			and _path_is_within_mounted_pack(bound_path)
			and FileAccess.file_exists(bound_path)
		):
			bound_glb_paths.append(bound_path)
	AssetFactoryScript.preload_models_threaded(bound_glb_paths)
	for placement_value in map_data.bound_prop_placements:
		var placement: Dictionary = placement_value
		var source_type := String(placement.get("source_type", ""))
		var source_index := int(placement.get("source_index", -1))
		var glb_path := String(placement.get("glb_path", ""))
		var position := Vector3(placement.get("position", Vector3.INF))
		var yaw := float(placement.get("yaw", NAN))
		var scale := Vector3(placement.get("scale", Vector3.INF))
		if (
			String(placement.get("binding_status", "")) != "bound"
			or source_type == ""
			or not bound_retail_prop_type_ids.has(source_type)
			or source_index < 0
			or seen_source_indices.has(source_index)
			or glb_path == ""
			or glb_path.get_extension().to_lower() != "glb"
			or not _path_is_within_mounted_pack(glb_path)
			or not FileAccess.file_exists(glb_path)
			or not _finite_vector3(position)
			or not _finite_number(yaw)
			or not _finite_positive_vector3(scale)
		):
			container.free()
			return _fail_configuration("bound retail placement is unsafe or incomplete")
		var visual := AssetFactoryScript._try_load_model(glb_path)
		var mesh_count := _mesh_instance_count(visual)
		if visual == null or mesh_count <= 0:
			if visual != null:
				visual.free()
			container.free()
			return _fail_configuration("bound retail GLB could not instantiate a mesh")
		var placement_root := Node3D.new()
		placement_root.name = "RetailProp_%04d_%s" % [source_index, source_type]
		# The placement scale is authored in SAGE model units; the GLB retains
		# those units while the cooked battlefield uses its validated local scale.
		var local_scale := scale * map_data.local_transform_scale
		placement_root.transform = Transform3D(Basis(Vector3.UP, yaw).scaled(local_scale), position)
		placement_root.set_meta("presentation", "retail-bound-glb")
		placement_root.set_meta("source_type", source_type)
		placement_root.set_meta("source_index", source_index)
		placement_root.set_meta("source_position", placement.get("source_position", Vector3.INF))
		placement_root.set_meta("source_yaw", float(placement.get("source_yaw", yaw)))
		placement_root.set_meta("source_scale", scale)
		placement_root.set_meta("local_scale", local_scale)
		placement_root.set_meta("glb_path", glb_path)
		placement_root.set_meta("mesh_instance_count", mesh_count)
		placement_root.add_child(visual)
		if MAIN_CAMERA_EXCLUDED_PROP_TYPES.has(source_type):
			placement_root.visible = false
			placement_root.set_meta("main_camera_excluded", true)
			placement_root.set_meta("main_camera_exclusion_reason", String(MAIN_CAMERA_EXCLUDED_PROP_TYPES[source_type]))
		container.add_child(placement_root)
		seen_source_indices[source_index] = true
		staged_mesh_count += mesh_count
	# Maps whose bindings resolve nothing (the five-maps pack today) stage an
	# empty container; the mesh-count gate only applies when the map data
	# declares bound placements.
	if container.get_child_count() != map_data.bound_prop_placement_count or (map_data.bound_prop_placement_count > 0 and staged_mesh_count <= 0):
		container.free()
		return _fail_configuration("bound retail GLB placement staging was incomplete")
	add_child(container)
	retail_prop_container = container
	bound_retail_prop_count = container.get_child_count()
	bound_retail_mesh_instance_count = staged_mesh_count
	return true


func _configure_animated_prop_runtime(map_data: RetailMapData) -> bool:
	var controller = RetailAnimatedPropControllerScript.new()
	if not controller.configure_from_pack(map_data.pack_root, map_data.bound_prop_placements, retail_prop_container):
		error = "animated prop runtime contract failed: %s" % String(controller.error)
		controller.free()
		return false
	if not bool(controller.contract_declared):
		controller.free()
		return true
	controller.name = "RetailAnimatedPropController"
	controller.parity_failure.connect(_on_animated_prop_parity_failure)
	add_child(controller)
	animated_prop_controller = controller
	animated_prop_contract_active = true
	animated_prop_parity_ready = bool(controller.parity_ready)
	animated_prop_controlled_placement_count = int(controller.controlled_placement_count)
	animated_prop_controlled_type_ids.assign(controller.controlled_type_ids)
	animated_prop_diagnostics.assign(controller.diagnostics)
	set_meta("animated_prop_contract_active", true)
	set_meta("animated_prop_controlled_placements", animated_prop_controlled_placement_count)
	set_meta("animated_prop_controlled_types", animated_prop_controlled_type_ids.duplicate())
	set_meta("animated_prop_actual_animation_sets_inspected", bool(controller.actual_animation_sets_inspected))
	set_meta("animated_prop_parity_ready", animated_prop_parity_ready)
	set_meta("animated_prop_diagnostics", animated_prop_diagnostics.duplicate(true))
	if not animated_prop_parity_ready and not animated_prop_diagnostics.is_empty():
		error = "animated prop parity failed: %s" % JSON.stringify(animated_prop_diagnostics[0])
	return true


func _on_animated_prop_parity_failure(diagnostic: Dictionary) -> void:
	animated_prop_parity_ready = false
	if not animated_prop_diagnostics.has(diagnostic):
		animated_prop_diagnostics.append(diagnostic)
	set_meta("animated_prop_parity_ready", false)
	set_meta("animated_prop_diagnostics", animated_prop_diagnostics.duplicate(true))
	if error == "":
		error = "animated retail prop runtime retained exact geometry but failed animation parity"


func _configure_particle_runtime(map_data: RetailMapData) -> bool:
	var controller = RetailParticleControllerScript.new()
	if not controller.configure_from_pack(
		map_data.pack_root,
		map_data.map_root,
		map_data.local_transform_scale,
		Callable(map_data, "source_to_local"),
		self
	):
		error = "particle runtime contract failed: %s" % String(controller.error)
		controller.free()
		return false
	if not bool(controller.contract_declared):
		controller.free()
		return true
	controller.name = "RetailParticleController"
	add_child(controller)
	particle_controller = controller
	particle_contract_active = true
	particle_contract_ready = bool(controller.contract_ready)
	particle_presentation_ready = bool(controller.presentation_ready)
	particle_parity_ready = bool(controller.parity_ready)
	particle_validated_definition_count = int(controller.validated_definition_count)
	particle_validated_texture_count = int(controller.validated_texture_count)
	particle_provisional_selection_count = int(controller.provisional_runtime_selection_count)
	particle_unresolved_family_selection_count = int(controller.unresolved_family_selection_count)
	particle_emitter_count = int(controller.instantiated_emitter_count)
	particle_diagnostics.assign(controller.diagnostics)
	set_meta("particle_contract_active", particle_contract_active)
	set_meta("particle_contract_ready", particle_contract_ready)
	set_meta("particle_presentation_ready", particle_presentation_ready)
	set_meta("particle_parity_ready", particle_parity_ready)
	set_meta("particle_validated_definition_count", particle_validated_definition_count)
	set_meta("particle_validated_texture_count", particle_validated_texture_count)
	set_meta("particle_provisional_selection_count", particle_provisional_selection_count)
	set_meta("particle_unresolved_family_selection_count", particle_unresolved_family_selection_count)
	set_meta("particle_emitter_count", particle_emitter_count)
	set_meta("particle_diagnostics", particle_diagnostics.duplicate(true))
	return true


func route_structure_damage_effects(request: Dictionary) -> Dictionary:
	## RetailStructure has already checked every identifier against the selected
	## pack. This battlefield seam retains the request and rejects presentation
	## while the exact dynamic emitter/family selection remains unresolved.
	var structure_id := int(request.get("entityId", 0))
	var phase := String(request.get("phase", ""))
	var fx_id := String(request.get("enteringFx", ""))
	var particles_value: Variant = request.get("particleSystemIds", [])
	var blockers_value: Variant = request.get("blockers", [])
	if structure_id <= 0 or phase == "" or typeof(particles_value) != TYPE_ARRAY or typeof(blockers_value) != TYPE_ARRAY:
		return _record_structure_damage_route({
			"ok": false,
			"reason": "invalid-structure-damage-route-request",
			"entityId": structure_id,
			"phase": phase,
		})
	var particles: Array[String] = []
	for value in particles_value as Array:
		if typeof(value) != TYPE_STRING or String(value) == "":
			return _record_structure_damage_route({
				"ok": false,
				"reason": "invalid-structure-particle-identifier",
				"entityId": structure_id,
				"phase": phase,
			})
		particles.append(String(value))
	if fx_id == "" and particles.is_empty():
		return _record_structure_damage_route({
			"ok": true,
			"entityId": structure_id,
			"phase": phase,
			"enteringFx": "",
			"particleSystemIds": [],
			"status": "no-visual-effect-route-declared",
		})
	if not particle_contract_active or not particle_contract_ready:
		return _record_structure_damage_route({
			"ok": false,
			"reason": "selected-pack-particle-runtime-contract-unavailable",
			"entityId": structure_id,
			"phase": phase,
			"enteringFx": fx_id,
			"particleSystemIds": particles,
		})
	var blockers: Array = blockers_value as Array
	return _record_structure_damage_route({
		"ok": false,
		"reason": (
			"declared-structure-effect-runtime-blocked"
			if not blockers.is_empty()
			else "source-identified-dynamic-emitter-translation-unimplemented"
		),
		"entityId": structure_id,
		"phase": phase,
		"enteringFx": fx_id,
		"particleSystemIds": particles,
		"blockers": blockers.duplicate(true),
	})


func _record_structure_damage_route(result: Dictionary) -> Dictionary:
	last_structure_damage_route = result.duplicate(true)
	set_meta("last_structure_damage_route", last_structure_damage_route.duplicate(true))
	if observability_enabled:
		structure_damage_route_log.append(last_structure_damage_route.duplicate(true))
		if structure_damage_route_log.size() > 128:
			structure_damage_route_log = structure_damage_route_log.slice(64)
		set_meta("structure_damage_route_log", structure_damage_route_log.duplicate(true))
	else:
		structure_damage_route_log.clear()
		if has_meta("structure_damage_route_log"):
			remove_meta("structure_damage_route_log")
	return result


func _build_bound_retail_structures(map_data: RetailMapData) -> bool:
	bound_retail_structure_type_ids.assign(map_data.bound_structure_type_ids)
	if map_data.bound_structure_placements.size() != map_data.bound_structure_placement_count:
		return _fail_configuration("bound retail structure count disagrees with map data")
	if map_data.bound_structure_placement_count == 0:
		return bound_retail_structure_type_ids.is_empty()
	var container := Node3D.new()
	container.name = "SourceBoundRetailStructures"
	container.set_meta("presentation", "retail-bound-lifecycle-structures")
	container.set_meta("source_type_ids", bound_retail_structure_type_ids.duplicate())
	var seen_source_indices: Dictionary = {}
	# Warm the shared mesh cache with one threaded parse per unique bound GLB;
	# lifecycle configure below then loads via ordinary cache hits. Only paths
	# that already pass this function's containment/existence checks are queued.
	var bound_structure_glb_paths: Array = []
	for placement_value in map_data.bound_structure_placements:
		var bound_structure_path := String((placement_value as Dictionary).get("glb_path", ""))
		if (
			bound_structure_path != ""
			and bound_structure_path.get_extension().to_lower() == "glb"
			and _path_is_within_mounted_pack(bound_structure_path)
			and FileAccess.file_exists(bound_structure_path)
		):
			bound_structure_glb_paths.append(bound_structure_path)
	AssetFactoryScript.preload_models_threaded(bound_structure_glb_paths)
	for placement_value in map_data.bound_structure_placements:
		var placement: Dictionary = placement_value
		var source_type := String(placement.get("source_type", ""))
		var source_index := int(placement.get("source_index", -1))
		var source_virtual_model := String(placement.get("source_virtual_model", ""))
		var object_id := String(placement.get("object_id", ""))
		var glb_relative := String(placement.get("glb_relative", ""))
		var glb_path := String(placement.get("glb_path", ""))
		var position := Vector3(placement.get("position", Vector3.INF))
		var yaw := float(placement.get("yaw", NAN))
		var scale := Vector3(placement.get("scale", Vector3.INF))
		if (
			String(placement.get("binding_status", "")) != "bound"
			or String(placement.get("classification", "")) != "lifecycle-structure"
			or source_type == ""
			or not bound_retail_structure_type_ids.has(source_type)
			or source_index < 0
			or seen_source_indices.has(source_index)
			or source_virtual_model == ""
			or object_id == ""
			or glb_relative == ""
			or glb_path == ""
			or glb_path.get_extension().to_lower() != "glb"
			or not _path_is_within_mounted_pack(glb_path)
			or not FileAccess.file_exists(glb_path)
			or not _finite_vector3(position)
			or not _finite_number(yaw)
			or not _finite_positive_vector3(scale)
		):
			container.free()
			return _fail_configuration("bound lifecycle structure placement is unsafe or incomplete")

		var definition: Dictionary = ContentDB.get_bundle_object(object_id)
		var definition_pack_root := String(definition.get("_pack_root", ""))
		# Host faction pack + maps supplement is normal; accept any mounted non-res root.
		if (
			definition.is_empty()
			or String(definition.get("kind", "")) != "structure"
			or not _is_mounted_pack_root(definition_pack_root)
		):
			container.free()
			return _fail_configuration("bound lifecycle structure object is not registered by the selected map pack")
		if typeof(definition.get("presentation")) != TYPE_DICTIONARY:
			container.free()
			return _fail_configuration("bound lifecycle structure presentation is missing")
		var presentation: Dictionary = definition["presentation"]
		if String(presentation.get("model", "")).replace("\\", "/") != glb_relative.replace("\\", "/") or typeof(presentation.get("buildingLifecycle")) != TYPE_DICTIONARY:
			container.free()
			return _fail_configuration("bound lifecycle structure object disagrees with its map binding")
		var lifecycle: Dictionary = presentation["buildingLifecycle"]
		var maximum_health := RetailStructureScript.maximum_health_for_lifecycle(lifecycle)
		if maximum_health <= 0:
			container.free()
			return _fail_configuration("bound lifecycle structure has unresolved exact maximum health")
		var lifecycle_error: String = RetailStructureScript.validate_lifecycle_contract(
			lifecycle,
			source_type,
			glb_relative,
			maximum_health,
			object_id
		)
		if lifecycle_error != "":
			container.free()
			return _fail_configuration("bound lifecycle structure contract failed: %s" % lifecycle_error)

		var structure = RetailStructureScript.new()
		structure.transform = Transform3D(Basis(Vector3.UP, yaw).scaled(scale), position)
		structure.configure(
			{
				"id": source_index,
				"team": 0,
				"structure_kind": source_type,
				"maximum_health": maximum_health,
				"health": maximum_health,
				"construction_progress": 1.0,
			},
			object_id,
			map_data.local_transform_scale
		)
		if (
			String(structure.contract_error) != ""
			or not bool(structure.retail_visual_loaded)
			or String(structure.current_lifecycle_phase) != String(lifecycle.get("initialPhase", "intact"))
		):
			var structure_error := String(structure.contract_error)
			structure.free()
			container.free()
			return _fail_configuration("bound lifecycle structure could not instantiate: %s" % structure_error)
		structure.name = "RetailStructure_%04d_%s" % [source_index, source_type]
		structure.set_meta("presentation", "retail-bound-lifecycle-structure")
		structure.set_meta("source_type", source_type)
		structure.set_meta("source_index", source_index)
		structure.set_meta("source_position", placement.get("source_position", Vector3.INF))
		structure.set_meta("source_yaw", float(placement.get("source_yaw", yaw)))
		structure.set_meta("source_scale", scale)
		structure.set_meta("source_virtual_model", source_virtual_model)
		structure.set_meta("glb_path", glb_path)
		structure.set_meta("content_object_id", object_id)
		container.add_child(structure)
		seen_source_indices[source_index] = true
	if container.get_child_count() != map_data.bound_structure_placement_count:
		container.free()
		return _fail_configuration("bound lifecycle structure staging was incomplete")
	add_child(container)
	retail_structure_container = container
	bound_retail_structure_count = container.get_child_count()
	return true


func bound_structure_node_by_source_index(source_index: int) -> Node:
	## Lookup for sim-backed creep lairs: the bound lifecycle visual staged for
	## an authored placement, keyed by its cooked source index.
	if retail_structure_container == null:
		return null
	for child in retail_structure_container.get_children():
		if child.has_meta("source_index") and int(child.get_meta("source_index")) == source_index:
			return child
	return null


func _record_unresolved_prop_diagnostics(map_data: RetailMapData) -> bool:
	var vegetation: Array[Dictionary] = []
	var rocks: Array[Dictionary] = []
	for placement_value in map_data.generic_prop_placements:
		var placement: Dictionary = placement_value
		var kind := String(placement.get("kind", ""))
		if String(placement.get("binding_status", "")) != "unresolved" or not unresolved_prop_type_ids.has(String(placement.get("source_type", ""))):
			return _fail_configuration("unresolved-prop preview contains a resolved or unknown source type")
		if kind == "vegetation":
			vegetation.append(placement)
		elif kind == "rock":
			rocks.append(placement)
		else:
			return _fail_configuration("unresolved-prop preview contains an unsupported diagnostic kind")
	_add_unresolved_prop_diagnostic("SourceVegetationPlacementMarkers", vegetation)
	_add_unresolved_prop_diagnostic("SourceRockPlacementMarkers", rocks)
	generic_prop_count = vegetation.size() + rocks.size()
	unresolved_prop_diagnostics = {
		"total_type_ids": unresolved_prop_type_ids.duplicate(),
		"total_placement_count": unresolved_prop_placement_count,
		"sampled_placement_count": generic_prop_count,
		"sampled_vegetation_count": vegetation.size(),
		"sampled_rock_count": rocks.size(),
		"presentation": "non-rendered-diagnostic",
	}
	set_meta("source_unresolved_prop_type_ids", unresolved_prop_type_ids.duplicate())
	set_meta("source_unresolved_prop_placement_count", unresolved_prop_placement_count)
	set_meta("source_unresolved_prop_sample_count", generic_prop_count)
	set_meta("source_unresolved_prop_presentation", "non-rendered-diagnostic")
	return generic_prop_count == map_data.generic_prop_placements.size()


func _add_unresolved_prop_diagnostic(node_name: String, placements: Array[Dictionary]) -> void:
	if placements.is_empty():
		return
	# Keep the historical node names for diagnostic consumers, but deliberately
	# attach no Mesh, MultiMesh, material, or other renderable child.
	var diagnostic := Node.new()
	diagnostic.name = node_name
	diagnostic.set_meta("presentation", "unresolved-marker")
	diagnostic.set_meta("diagnostic_only", true)
	diagnostic.set_meta("retail_bound", false)
	diagnostic.set_meta("source_type_ids", _source_type_ids(placements))
	diagnostic.set_meta("source_placement_count", placements.size())
	diagnostic.set_meta("disclosure", "non-rendered diagnostic for unresolved source type; not retail art")
	add_child(diagnostic)


func _source_type_ids(placements: Array[Dictionary]) -> Array[String]:
	var result: Array[String] = []
	for placement in placements:
		var source_type := String(placement.get("source_type", ""))
		if source_type != "" and not result.has(source_type):
			result.append(source_type)
	result.sort()
	return result


func _mesh_instance_count(node: Node) -> int:
	if node == null:
		return 0
	var result := 1 if node is MeshInstance3D and (node as MeshInstance3D).mesh != null else 0
	for child in node.get_children():
		result += _mesh_instance_count(child)
	return result


func _path_is_within_mounted_pack(path: String) -> bool:
	## GLB bindings may resolve into a maps supplement pack while map.json lives
	## under the host faction pack. Accept any currently mounted non-res root.
	## Reuse the cached mounted set: list_pack_roots() re-scans and re-sorts every
	## pack (and prints) per call, and this runs once per bound map GLB - an
	## asset-heavy map turned it into thousands of full rescans and froze loading.
	if path == "":
		return false
	for root_value in ContentDB.pack_roots:
		var root := String(root_value)
		if root == "" or root.begins_with("res://"):
			continue
		if ModLoader.path_is_within(root, path):
			return true
	return false


func _is_mounted_pack_root(candidate_root: String) -> bool:
	if candidate_root == "" or candidate_root.begins_with("res://"):
		return false
	var normalized := candidate_root.replace("\\", "/").trim_suffix("/").to_lower()
	for root_value in ContentDB.pack_roots:
		var root := String(root_value)
		if root == "" or root.begins_with("res://"):
			continue
		if root.replace("\\", "/").trim_suffix("/").to_lower() == normalized:
			return true
	return false


func _finite_number(value: float) -> bool:
	return not is_nan(value) and not is_inf(value)


func _finite_vector3(value: Vector3) -> bool:
	return _finite_number(value.x) and _finite_number(value.y) and _finite_number(value.z)


func _finite_vector2(value: Vector2) -> bool:
	return _finite_number(value.x) and _finite_number(value.y)


func _finite_positive_vector3(value: Vector3) -> bool:
	return _finite_vector3(value) and value.x > 0.0 and value.y > 0.0 and value.z > 0.0


func _fail_configuration(message: String) -> bool:
	source_driven = false
	if error == "":
		error = message
	return false


func _abort_configuration(message: String) -> bool:
	var reason := message
	_clear_generated()
	error = reason
	return false


func _exact_terrain_contract_error(map_data: RetailMapData) -> String:
	if map_data == null or not map_data.ready:
		return "exact terrain requires validated retail map data"
	var width := int(map_data.width)
	var height := int(map_data.height)
	if width <= 1 or height <= 1:
		return "exact terrain dimensions cannot form source quads"
	var vertex_count := width * height
	var triangle_count := (width - 1) * (height - 1) * 2
	if vertex_count <= 0 or vertex_count > MAX_EXACT_TERRAIN_VERTICES:
		return "exact terrain vertex count exceeds the private-slice runtime bound"
	if triangle_count <= 0 or triangle_count > MAX_EXACT_TERRAIN_TRIANGLES:
		return "exact terrain triangle count exceeds the private-slice runtime bound"
	if map_data.heightmap_bytes != vertex_count * 2 or map_data.height_samples.size() != vertex_count * 2:
		return "exact terrain height samples do not cover the declared grid"
	var expected_passability_stride := (width + 7) / 8
	if map_data.passability_row_stride != expected_passability_stride or map_data.passability_bytes != expected_passability_stride * height or map_data.passability_bits.size() != expected_passability_stride * height:
		return "exact terrain passability bits do not cover the declared grid"
	if (
		map_data.terrain_tile_indices.size() != vertex_count
		or map_data.terrain_blend_cells.size() != vertex_count
		or map_data.terrain_three_way_blend_cells.size() != vertex_count
		or map_data.terrain_cliff_cells.size() != vertex_count
	):
		return "exact terrain material grids do not cover the declared height samples"
	if (
		not _finite_number(map_data.horizontal_scale)
		or map_data.horizontal_scale <= 0.0
		or not _finite_number(map_data.vertical_scale)
		or map_data.vertical_scale <= 0.0
		or not _finite_number(map_data.local_transform_scale)
		or map_data.local_transform_scale <= 0.0
	):
		return "exact terrain transform scale is invalid"
	if map_data.impassable_count < 0 or map_data.impassable_count > vertex_count or map_data.computed_impassable_count != map_data.impassable_count:
		return "exact terrain passability metadata is inconsistent"
	return ""


func _calculate_source_grid_normals(vertices: PackedVector3Array, width: int, height: int) -> PackedVector3Array:
	if width <= 1 or height <= 1 or vertices.size() != width * height:
		return PackedVector3Array()
	var quad_width := width - 1
	var quad_height := height - 1
	var quad_normals := PackedVector3Array()
	quad_normals.resize(quad_width * quad_height)
	for y in range(quad_height):
		for x in range(quad_width):
			var top_left := y * width + x
			var top_right := top_left + 1
			var bottom_left := top_left + width
			var bottom_right := bottom_left + 1
			var normal_a := (vertices[top_right] - vertices[top_left]).cross(vertices[bottom_left] - vertices[top_left]).normalized()
			var normal_b := (vertices[bottom_left] - vertices[bottom_right]).cross(vertices[top_right] - vertices[bottom_right]).normalized()
			var quad_normal := (normal_a + normal_b) * 0.5
			quad_normals[y * quad_width + x] = quad_normal if quad_normal.length_squared() > 0.000001 else Vector3.UP

	# SAGE smooths a vertex with the four surrounding quad normals and uses an
	# upward normal for each missing boundary quad. The uniform local transform
	# preserves those normals after converting the horizontal plane to Godot X/Z.
	var result := PackedVector3Array()
	result.resize(vertices.size())
	for y in range(height):
		for x in range(width):
			var sum := Vector3.ZERO
			for quad_y in range(y - 1, y + 1):
				for quad_x in range(x - 1, x + 1):
					if quad_x < 0 or quad_y < 0 or quad_x >= quad_width or quad_y >= quad_height:
						sum += Vector3.UP
					else:
						sum += quad_normals[quad_y * quad_width + quad_x]
			result[y * width + x] = sum.normalized() if sum.length_squared() > 0.000001 else Vector3.UP
	return result


func _clear_generated() -> void:
	for child in get_children():
		child.queue_free()
	source_driven = false
	error = ""
	terrain_exact_grid_ready = false
	terrain_vertex_count = 0
	terrain_triangle_count = 0
	impassable_vertex_count = 0
	terrain_material_source_driven = false
	terrain_texture_array_size = 0
	terrain_texture_array_dimension = 0
	terrain_tile_data_cell_count = 0
	terrain_primary_blend_cell_count = 0
	terrain_three_way_blend_cell_count = 0
	terrain_cliff_cell_count = 0
	terrain_mesh_instance = null
	terrain_material = null
	terrain_texture_array = null
	terrain_tile_data_texture = null
	terrain_blend_data_texture = null
	terrain_cliff_info_texture = null
	water_surface_count = 0
	water_triangle_count = 0
	road_material_source_driven = false
	road_material_count = 0
	road_source_edge_count = 0
	road_unique_endpoint_count = 0
	road_shared_node_count = 0
	road_curve_candidate_node_count = 0
	road_generated_broad_curve_count = 0
	road_straight_fallback_node_count = 0
	road_generated_curve_evidence.clear()
	road_curve_fallback_evidence.clear()
	road_curve_strip_count = 0
	road_render_strip_count = 0
	road_vertex_count = 0
	road_triangle_count = 0
	road_mesh_instance_count = 0
	road_container = null
	road_materials.clear()
	ford_marker_count = 0
	generic_prop_count = 0
	ford_gate_diagnostics.clear()
	unresolved_prop_diagnostics.clear()
	for metadata_key in [
		"source_ford_gate_count",
		"source_ford_gates",
		"source_ford_gate_presentation",
		"source_unresolved_prop_type_ids",
		"source_unresolved_prop_placement_count",
		"source_unresolved_prop_sample_count",
		"source_unresolved_prop_presentation",
		"animated_prop_contract_active",
		"animated_prop_controlled_placements",
		"animated_prop_controlled_types",
		"animated_prop_actual_animation_sets_inspected",
		"animated_prop_parity_ready",
		"animated_prop_diagnostics",
		"particle_contract_active",
		"particle_contract_ready",
		"particle_presentation_ready",
		"particle_parity_ready",
		"particle_validated_definition_count",
		"particle_validated_texture_count",
		"particle_provisional_selection_count",
		"particle_unresolved_family_selection_count",
		"particle_emitter_count",
		"particle_diagnostics",
		"last_structure_damage_route",
		"structure_damage_route_log",
	]:
		if has_meta(metadata_key):
			remove_meta(metadata_key)
	bound_retail_prop_count = 0
	bound_retail_mesh_instance_count = 0
	bound_retail_prop_type_ids.clear()
	bound_retail_structure_count = 0
	bound_retail_structure_type_ids.clear()
	animated_prop_contract_active = false
	animated_prop_controlled_placement_count = 0
	animated_prop_controlled_type_ids.clear()
	animated_prop_parity_ready = false
	animated_prop_diagnostics.clear()
	animated_prop_controller = null
	particle_contract_active = false
	particle_contract_ready = false
	particle_presentation_ready = false
	particle_parity_ready = false
	particle_validated_definition_count = 0
	particle_validated_texture_count = 0
	particle_provisional_selection_count = 0
	particle_unresolved_family_selection_count = 0
	particle_emitter_count = 0
	particle_diagnostics.clear()
	structure_damage_route_log.clear()
	last_structure_damage_route.clear()
	particle_controller = null
	unresolved_prop_placement_count = 0
	unresolved_prop_type_ids.clear()
	retail_prop_container = null
	retail_structure_container = null
	water_mesh_instance = null

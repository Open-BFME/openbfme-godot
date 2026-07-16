extends SceneTree
## Deterministic behavior/asset gate for the playable private retail slice.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
# Capture-measured dock geometry (bfme2-ref-120s.png); mirrors
# retail_hud.gd RETAIL_RADAR_CENTER / RETAIL_DISH_CENTER.
const EXPECTED_RADAR_CENTER := Vector2(225.0, 198.0)
const EXPECTED_DISH_CENTER := Vector2(587.0, 219.0)
const ARCHER_PROJECTILE_CONTROLLER_PATH := "res://src/retail_slice/retail_archer_projectile_controller.gd"
# This is a deadlock/watchdog bound, not a frame-time optimization gate. The
# vertical-slice DoD currently prioritizes source-correct gameplay and assets.
const INITIALIZATION_WATCHDOG_MS := 10000

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	root.size = Vector2i(1920, 1080)
	_run_archer_projectile_contract_fixture()
	var packed: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	_check("scene_parses", packed != null)
	if packed == null:
		_finish()
		return
	var slice = packed.instantiate()
	root.add_child(slice)
	await process_frame
	await process_frame

	_check("slice_ready", bool(slice.ready_ok), String(slice.failure_reason))
	var initialization_total_ms := int(slice.initialization_metrics_ms.get("ready_complete", -1))
	_check("initialization_completes_before_watchdog", initialization_total_ms >= 0 and initialization_total_ms <= INITIALIZATION_WATCHDOG_MS, "%d ms" % initialization_total_ms)
	_check("external_private_pack", String(slice.selected_pack_root).contains("bfme2-men-vslice") and not String(slice.selected_pack_root).begins_with("res://"), String(slice.selected_pack_root))
	_check("terrain_is_source_driven", bool(slice.source_driven_terrain))
	_check("three_ford_crossings", int(slice.crossing_count) == 3, str(slice.crossing_count))
	_check("imported_map_preview", bool(slice.map_preview_loaded))
	_check("imported_map_art", bool(slice.map_art_loaded))
	_check("cooked_source_map_mounted", slice.source_map_data != null and bool(slice.source_map_data.ready), String(slice.source_map_data.error if slice.source_map_data != null else "missing"))
	if slice.source_map_data != null:
		_check("source_heightmap_exact", int(slice.source_map_data.width) == 415 and int(slice.source_map_data.height) == 353 and int(slice.source_map_data.heightmap_bytes) == 292990)
		_check("source_passability_exact", int(slice.source_map_data.impassable_count) == 18325 and int(slice.source_map_data.passability_bytes) == 18356)
		_check("source_terrain_symbols_exact", int(slice.source_map_data.terrain_texture_count) == 66)
		var terrain_cell_count := int(slice.source_map_data.width) * int(slice.source_map_data.height)
		_check("source_terrain_cell_arrays_loaded", slice.source_map_data.terrain_tile_indices.size() == terrain_cell_count and slice.source_map_data.terrain_blend_cells.size() == terrain_cell_count and slice.source_map_data.terrain_three_way_blend_cells.size() == terrain_cell_count and slice.source_map_data.terrain_cliff_cells.size() == terrain_cell_count, str(terrain_cell_count))
		_check("source_terrain_description_tables_loaded", slice.source_map_data.terrain_blend_descriptions.size() == 16904 and slice.source_map_data.terrain_cliff_mappings.size() == 577, "blend=%d cliff=%d" % [slice.source_map_data.terrain_blend_descriptions.size(), slice.source_map_data.terrain_cliff_mappings.size()])
		_check("source_terrain_nonzero_layers_recomputed", int(slice.source_map_data.terrain_nonzero_blend_cell_count) == 51394 and int(slice.source_map_data.terrain_nonzero_three_way_blend_cell_count) == 9408 and int(slice.source_map_data.terrain_nonzero_cliff_cell_count) == 64, "blend=%d three=%d cliff=%d" % [slice.source_map_data.terrain_nonzero_blend_cell_count, slice.source_map_data.terrain_nonzero_three_way_blend_cell_count, slice.source_map_data.terrain_nonzero_cliff_cell_count])
		_check("source_terrain_material_catalog_loaded", slice.source_map_data.terrain_material_catalog.size() == 66 and int(slice.source_map_data.terrain_material_catalog[0].get("table_index", -1)) == 0 and String(slice.source_map_data.terrain_material_catalog[0].get("symbol", "")) == "GrassIsengard06" and int(slice.source_map_data.terrain_texture_array_dimension) == 512, str(slice.source_map_data.terrain_texture_array_dimension))
		_check("representative_terrain_tile_samples_exact", _source_terrain_tile_samples_match(slice.source_map_data))
		_check("source_objects_exact", int(slice.source_map_data.object_count) == 1526 and int(slice.source_map_data.nonroad_object_count) == 1384)
		_check("source_roads_are_exact_separate_pairs", int(slice.source_map_data.road_type_count) == 5 and int(slice.source_map_data.road_control_point_count) == 142 and int(slice.source_map_data.road_segment_count) == 71 and int(slice.source_map_data.road_unresolved_control_point_count) == 0 and slice.source_map_data.road_type_ids == ["Footprints", "FtPrintDrkGr02", "FtPrintGrass02", "FtprintsDrk", "FtprintsDrk02"], str(slice.source_map_data.road_type_ids))
		_check("source_road_material_closure_exact", int(slice.source_map_data.road_material_count) == 5 and slice.source_map_data.road_material_catalog.size() == 5 and String(slice.source_map_data.road_source_report_aggregate_sha256) == "45f557b171a18739e268c626e7be0f2aecadba4a0a527d809fdc7b3a1076fdc2")
		_check("source_road_topology_exact", int(slice.source_map_data.road_unique_endpoint_count) == 120 and int(slice.source_map_data.road_shared_node_count) == 21 and int(slice.source_map_data.road_curve_candidate_node_count) == 18 and int(slice.source_map_data.road_crossing_candidate_node_count) == 0 and int(slice.source_map_data.road_modifier_flags_or) == 0, "%d/%d/%d" % [slice.source_map_data.road_unique_endpoint_count, slice.source_map_data.road_shared_node_count, slice.source_map_data.road_curve_candidate_node_count])
		_check("source_waypoints_and_starts_exact", int(slice.source_map_data.waypoint_count) == 14 and int(slice.source_map_data.player_start_count) == 2)
		_check("source_water_geometry_exact", int(slice.source_map_data.standing_water_count) == 4 and int(slice.source_map_data.river_count) == 8)
		_check("source_binary_not_packaged", not bool(slice.source_map_data.source_binary_packaged))
		var player_one := Vector3(slice.source_map_data.local_player_starts.get("Player_1_Start", Vector3.ZERO))
		var player_two := Vector3(slice.source_map_data.local_player_starts.get("Player_2_Start", Vector3.ZERO))
		_check("source_start_transform_exact", player_one.is_equal_approx(Vector3(38.0, 0.0, 0.0)) and player_two.is_equal_approx(Vector3(-38.0, 0.0, 0.0)), "%s / %s" % [str(player_one), str(player_two)])
		_check("source_ford_names_drive_gates", _gate_names(slice.source_map_data.ford_gates) == ["ford1", "ford2", "ford3"], str(_gate_names(slice.source_map_data.ford_gates)))
		_check("source_ford_ids_exact", _gate_source_ids(slice.source_map_data.ford_gates) == [38, 43, 46], str(_gate_source_ids(slice.source_map_data.ford_gates)))
		_check(
			"source_unbound_generic_props_exact",
			slice.source_map_data.generic_prop_placements.size() == 0
			and int(slice.source_map_data.unresolved_prop_placement_count) == 13,
			"generic=%d unresolved=%d" % [slice.source_map_data.generic_prop_placements.size(), slice.source_map_data.unresolved_prop_placement_count]
		)
		_check("terrain_semantic_ranges_recomputed", int(slice.source_map_data.raw_elevation_min) == 5888 and int(slice.source_map_data.raw_elevation_max) == 11117 and int(slice.source_map_data.computed_raw_elevation_min) == 5888 and int(slice.source_map_data.computed_raw_elevation_max) == 11117)
		_check("passability_full_popcount_recomputed", int(slice.source_map_data.computed_impassable_count) == 18325 and int(slice.source_map_data.computed_impassable_count) == int(slice.source_map_data.impassable_count), str(slice.source_map_data.computed_impassable_count))
		_check("cooked_binary_digests_exact", String(slice.source_map_data.heightmap_sha256).to_upper() == "449D7B4BADA8549B5ED3EC8E908186922D05256D6F728B433018A6F381EDA7FB" and String(slice.source_map_data.passability_sha256).to_upper() == "11E911C6BA50A0D8DCF7FC3A71242B013B5DFDCE1169AE86C939D2DDD5E654B9")
		_check("representative_height_samples_exact", _source_height_samples_match(slice.source_map_data))
		_check("declared_playable_inset_exact", int(slice.source_map_data.border_width) == 20 and Vector2(slice.source_map_data.playable_world_extent).is_equal_approx(Vector2(3750.0, 3130.0)) and Vector2i(slice.source_map_data.playable_grid_min) == Vector2i(20, 20) and Vector2i(slice.source_map_data.playable_grid_max) == Vector2i(395, 333))
		if slice.source_map_data.map_outline.size() >= 3:
			var playable_source_min: Vector2 = slice.source_map_data.local_to_source_horizontal(slice.source_map_data.map_outline[0])
			var playable_source_max: Vector2 = slice.source_map_data.local_to_source_horizontal(slice.source_map_data.map_outline[2])
			_check("playable_polygon_uses_declared_border", playable_source_min.is_equal_approx(Vector2(200.0, -200.0)) and playable_source_max.is_equal_approx(Vector2(3950.0, -3330.0)), "%s / %s" % [str(playable_source_min), str(playable_source_max)])
		else:
			# Empty outline means map configure() failed earlier; report it as a
			# failed check instead of crashing _run (a crash here left the
			# headless runner alive forever with no result line).
			_check("playable_polygon_uses_declared_border", false, "map_outline empty (map load failed)")
		_check("bounded_navigation_built_once", bool(slice.source_map_data.navigation_ready) and int(slice.source_map_data.navigation_build_count) == 1 and int(slice.source_map_data.navigation_walkable_count) > 80000 and int(slice.source_map_data.navigation_water_blocked_count) > 0 and int(slice.source_map_data.navigation_ford_corridor_count) > 0, "walkable=%d water_blocked=%d corridors=%d builds=%d" % [slice.source_map_data.navigation_walkable_count, slice.source_map_data.navigation_water_blocked_count, slice.source_map_data.navigation_ford_corridor_count, slice.source_map_data.navigation_build_count])
		_check("reviewed_ford2_cell_stays_blocked", slice.source_map_data.is_impassable_at(208, 142) and not slice.source_map_data.is_navigation_walkable(Vector2i(208, 142)))
	if not bool(slice.ready_ok) or slice.source_map_data == null or not bool(slice.source_map_data.ready) or slice.simulation == null:
		slice.queue_free()
		await process_frame
		_finish()
		return
	_check_retail_unit_rules(slice)
	_check("simulation_uses_source_map_configuration", bool(slice.simulation.source_map_configured))
	var player_centroid := (Vector2(slice.simulation.entity(1)["position"]) + Vector2(slice.simulation.entity(2)["position"])) * 0.5
	var enemy_centroid := (Vector2(slice.simulation.entity(101)["position"]) + Vector2(slice.simulation.entity(102)["position"])) * 0.5
	var source_player_two := Vector3(slice.source_map_data.local_player_starts["Player_2_Start"])
	var source_player_one := Vector3(slice.source_map_data.local_player_starts["Player_1_Start"])
	_check("battalion_spawns_derive_from_source_starts", player_centroid.is_equal_approx(Vector2(source_player_two.x, source_player_two.z)) and enemy_centroid.is_equal_approx(Vector2(source_player_one.x, source_player_one.z)), "%s / %s" % [str(player_centroid), str(enemy_centroid)])
	_check("source_battlefield_built", slice.battlefield != null and bool(slice.battlefield.source_driven))
	if slice.battlefield != null:
		_check("source_height_mesh_uses_every_exact_sample_and_quad", bool(slice.battlefield.terrain_exact_grid_ready) and int(slice.battlefield.terrain_vertex_count) == 146495 and int(slice.battlefield.terrain_triangle_count) == 291456, "vertices=%d triangles=%d" % [slice.battlefield.terrain_vertex_count, slice.battlefield.terrain_triangle_count])
		_check("source_passability_colors_cover_full_terrain", int(slice.battlefield.impassable_vertex_count) == 18325 and int(slice.battlefield.impassable_vertex_count) == int(slice.source_map_data.impassable_count), str(slice.battlefield.impassable_vertex_count))
		var retail_terrain_material: ShaderMaterial = slice.battlefield.terrain_mesh_instance.get_active_material(0) as ShaderMaterial if slice.battlefield.terrain_mesh_instance != null else null
		var bound_texture_array: Texture2DArray = retail_terrain_material.get_shader_parameter("terrain_textures") as Texture2DArray if retail_terrain_material != null else null
		var bound_tile_data: Texture2D = retail_terrain_material.get_shader_parameter("terrain_tile_data") as Texture2D if retail_terrain_material != null else null
		var bound_blend_data: Texture2D = retail_terrain_material.get_shader_parameter("terrain_blend_data") as Texture2D if retail_terrain_material != null else null
		var bound_cliff_info: Texture2D = retail_terrain_material.get_shader_parameter("terrain_cliff_info") as Texture2D if retail_terrain_material != null else null
		_check("retail_terrain_material_is_source_driven", bool(slice.battlefield.terrain_material_source_driven) and retail_terrain_material != null and String(retail_terrain_material.get_meta("source", "")) == "cooked-retail-terrain-materials-and-sage-blends" and bool(retail_terrain_material.get_shader_parameter("sage_lighting_enabled")) and Vector3(retail_terrain_material.get_shader_parameter("sage_ambient_color")).is_equal_approx(Vector3(22.0 / 255.0, 18.0 / 255.0, 22.0 / 255.0)) and Vector3(retail_terrain_material.get_meta("sage_ambient_color", Vector3.INF)).is_equal_approx(Vector3(22.0 / 255.0, 18.0 / 255.0, 22.0 / 255.0)) and String(retail_terrain_material.get_meta("sage_lighting_model", "")) == "opensage-terrain-three-light-lambert")
		_check("retail_terrain_texture_array_has_66_layers", bound_texture_array != null and bound_texture_array.get_layers() == 66 and bound_texture_array.get_width() == 256 and bound_texture_array.get_height() == 256, "layers=%d size=%dx%d" % [bound_texture_array.get_layers() if bound_texture_array != null else -1, bound_texture_array.get_width() if bound_texture_array != null else -1, bound_texture_array.get_height() if bound_texture_array != null else -1])
		_check("retail_terrain_tile_data_bound", bound_tile_data != null and bound_tile_data.get_width() == 415 and bound_tile_data.get_height() == 353 and int(slice.battlefield.terrain_tile_data_cell_count) == 146495)
		_check("retail_terrain_blend_and_cliff_data_bound", bound_blend_data != null and bound_blend_data.get_width() == 415 and bound_blend_data.get_height() == 353 and bound_cliff_info != null and bound_cliff_info.get_width() == 577 and bound_cliff_info.get_height() == 2)
		_check("retail_terrain_primary_three_way_and_cliffs_active", int(slice.battlefield.terrain_primary_blend_cell_count) == 51394 and int(slice.battlefield.terrain_three_way_blend_cell_count) == 9408 and int(slice.battlefield.terrain_cliff_cell_count) == 64)
		_check("retail_roads_are_provenance_textured_and_grouped", bool(slice.battlefield.road_material_source_driven) and int(slice.battlefield.road_material_count) == 5 and int(slice.battlefield.road_mesh_instance_count) == 5 and slice.battlefield.road_container != null and slice.battlefield.road_container.get_child_count() == 5)
		_check("retail_road_topology_and_curve_formula_exact", int(slice.battlefield.road_source_edge_count) == 71 and int(slice.battlefield.road_unique_endpoint_count) == 120 and int(slice.battlefield.road_shared_node_count) == 21 and int(slice.battlefield.road_curve_candidate_node_count) == 18 and int(slice.battlefield.road_generated_broad_curve_count) == 11 and int(slice.battlefield.road_straight_fallback_node_count) == 7 and slice.battlefield.road_generated_curve_evidence.size() == 11 and slice.battlefield.road_curve_fallback_evidence.size() == 7)
		_check("retail_road_mesh_is_adaptive_and_terrain_draped", int(slice.battlefield.road_curve_strip_count) == 24 and int(slice.battlefield.road_render_strip_count) == 95 and int(slice.battlefield.road_vertex_count) == 534 and int(slice.battlefield.road_triangle_count) == 344, "strips=%d vertices=%d triangles=%d" % [slice.battlefield.road_render_strip_count, slice.battlefield.road_vertex_count, slice.battlefield.road_triangle_count])
		_check("source_water_mesh_built", int(slice.battlefield.water_surface_count) == 12 and int(slice.battlefield.water_triangle_count) > 50, "surfaces=%d triangles=%d" % [slice.battlefield.water_surface_count, slice.battlefield.water_triangle_count])
		_check("source_ford_gates_are_nonrendered_diagnostics", int(slice.battlefield.ford_marker_count) == 3 and int(slice.battlefield.get_meta("source_ford_gate_count", -1)) == 3 and slice.battlefield.ford_gate_diagnostics.size() == 3 and _visible_source_placeholder_count(slice.battlefield, "SourceFord_") == 0)
		_check("unresolved_props_are_nonrendered_diagnostics", int(slice.battlefield.generic_prop_count) == slice.source_map_data.generic_prop_placements.size() and int(slice.battlefield.get_meta("source_unresolved_prop_placement_count", -1)) == int(slice.source_map_data.unresolved_prop_placement_count) and int(slice.battlefield.get_meta("source_unresolved_prop_sample_count", -1)) == int(slice.battlefield.generic_prop_count) and _unresolved_diagnostic_sample_count(slice.battlefield) == int(slice.battlefield.generic_prop_count) and _visible_unresolved_placeholder_count(slice.battlefield) == 0, str(slice.battlefield.unresolved_prop_diagnostics))
		_check("world_builder_only_farm_templates_are_hidden_in_play", _bound_prop_type_visibility_matches(slice.battlefield, "FarmTemplate", 16, false, "default-model-none-world-builder-only"))
	_check("initial_battalions_include_retail_defined_builders", slice.battalion_nodes.size() == slice.simulation.initial_battalion_count() and slice.battalion_nodes.size() == 7, str(slice.battalion_nodes.size()))
	_check("ten_home_structures", slice.structure_nodes.size() == 10 and slice.simulation.structure_ids(0).size() == 5 and slice.simulation.structure_ids(1).size() == 5, "%d/%d" % [slice.structure_nodes.size(), slice.simulation.structure_ids().size()])
	var expected_unit_models := {
		"bfme2.object.gondor-fighter": {"model": "gondor-fighter.glb", "members": 15},
		"bfme2.object.gondor-archer": {"model": "gondor-archer.glb", "members": 15},
		"bfme2.object.gondor-tower-guard": {"model": "gondor-tower-guard.glb", "members": 15},
		"bfme2.object.gondor-knight": {"model": "gondor-knight.glb", "members": 10},
	}
	for object_id in expected_unit_models:
		var typed_battalion = _battalion_for_object_id(slice, object_id)
		var expected_model := String(expected_unit_models[object_id]["model"])
		var expected_members := int(expected_unit_models[object_id]["members"])
		_check(
			"%s_mounts_retail_glb" % object_id.replace("bfme2.object.", "").replace("-", "_"),
			typed_battalion != null
			and String(typed_battalion.object_id) == object_id
			and String(typed_battalion.retail_model_filename) == expected_model
			and int(typed_battalion.member_count) == expected_members
			and int(typed_battalion.retail_visual_count) == expected_members,
			"model=%s members=%d retail=%d" % [
				String(typed_battalion.retail_model_filename) if typed_battalion != null else "missing",
				int(typed_battalion.member_count) if typed_battalion != null else -1,
				int(typed_battalion.retail_visual_count) if typed_battalion != null else -1,
			]
		)
	for structure_id in slice.simulation.structure_ids():
		var structure_row: Dictionary = slice.simulation.structure(structure_id)
		var structure_kind := String(structure_row.get("structure_kind", ""))
		var structure_node = slice.structure_nodes.get(structure_id)
		var expected_structure_suffix := "assets/models/structures/men-%s/intact.glb" % structure_kind.replace("_", "-")
		var expected_bib := "assets/models/structures/men-%s/bib.glb" % structure_kind.replace("_", "-")
		var expected_door := "assets/models/structures/men-fortress/door-closed.glb" if structure_kind == "fortress" else ""
		var lifecycle_state: Dictionary = structure_node.lifecycle_state() if structure_node != null else {}
		_check(
			"structure_%d_starts_exact_private_lifecycle" % structure_id,
			structure_node != null
			and bool(structure_node.retail_visual_loaded)
			and String(structure_node.presentation_mode) == "private-imported-lifecycle"
			and String(structure_node.current_lifecycle_phase) == "intact"
			and String(structure_node.active_body_path).replace("\\", "/") == expected_structure_suffix
			and String(structure_node.active_bib_path).replace("\\", "/") == expected_bib
			and String(structure_node.active_door_path).replace("\\", "/") == expected_door
			and String(structure_node.contract_error) == ""
			and String(structure_node.retail_mesh_path).replace("\\", "/").ends_with(expected_structure_suffix)
			and lifecycle_state == structure_node.get_meta("building_lifecycle_state", {}),
			"kind=%s mode=%s phase=%s body=%s bib=%s door=%s error=%s" % [
				structure_kind,
				String(structure_node.presentation_mode) if structure_node != null else "missing",
				String(lifecycle_state.get("phase", "missing")),
				String(lifecycle_state.get("activeBodyPath", "missing")),
				String(lifecycle_state.get("activeBibPath", "missing")),
				String(lifecycle_state.get("activeDoorPath", "missing")),
				String(lifecycle_state.get("contractError", "missing")),
			]
		)
	_check("home_layout_uses_source_navigation", _home_layout_walkable(slice))
	_check("hud_uses_declared_headless_viewport", slice.hud.position.is_equal_approx(Vector2.ZERO) and slice.hud.size.is_equal_approx(Vector2(root.size)), "hud=%s viewport=%s" % [str(slice.hud.size), str(root.size)])
	root.size = Vector2i(1600, 900)
	await process_frame
	await process_frame
	var logical_viewport_size := slice.get_viewport().get_visible_rect().size
	_check(
		"hud_tracks_logical_viewport_under_window_resize",
		slice.hud.position.is_equal_approx(Vector2.ZERO)
		and slice.hud.size.is_equal_approx(logical_viewport_size)
		and is_equal_approx(slice.hud.anchor_right, 1.0)
		and is_equal_approx(slice.hud.anchor_bottom, 1.0),
		"hud=%s logical=%s window=%s" % [str(slice.hud.size), str(logical_viewport_size), str(root.size)]
	)
	root.size = Vector2i(1920, 1080)
	await process_frame
	await process_frame
	var palantir: Control = slice.find_child("PalantirDock", true, false) as Control
	var palantir_frame: Control = slice.find_child("OrnamentalFrame", true, false) as Control
	var command_panel: Control = slice.find_child("CommandPanel", true, false) as Control
	var command_grid: Control = slice.find_child("CommandGrid", true, false) as Control
	var group_strip: Control = slice.find_child("ControlGroupStrip", true, false) as Control
	_check(
		"palantir_is_bottom_left",
		palantir != null
			and palantir.global_position.x >= -1.0
			and palantir.global_position.x <= 12.0
			and palantir.global_position.y > 650.0,
		str(palantir.global_position if palantir != null else Vector2(-1, -1))
	)
	_check("palantir_radar_draws_above_opaque_frame", palantir_frame != null and slice.minimap.get_parent() == palantir_frame.get_parent() and slice.minimap.get_index() > palantir_frame.get_index())
	_check(
		"command_panel_attaches_to_palantir",
		command_panel != null
			and palantir != null
			and command_panel.global_position.x >= palantir.global_position.x + 280.0
			and command_panel.global_position.x + command_panel.size.x <= palantir.global_position.x + palantir.size.x + 1.0
	)
	# Radar and dish centers are the capture-measured dock coordinates
	# (bfme2-ref-120s.png): radar (225, 198), palantir dish (587, 219).
	_check(
		"minimap_is_centered_in_retail_left_circle",
		palantir != null
			and (slice.minimap.global_position + slice.minimap.size * 0.5).distance_to(palantir.global_position + EXPECTED_RADAR_CENTER) < 1.0
	)
	_check(
		"command_panel_is_centered_in_retail_right_circle",
		palantir != null
			and command_panel != null
			and (command_panel.global_position + EXPECTED_DISH_CENTER - Vector2(360.0, 0.0)).distance_to(palantir.global_position + EXPECTED_DISH_CENTER) < 1.0
			and command_grid != null
			and _sockets_ring_dish_center(command_grid, palantir.global_position + EXPECTED_DISH_CENTER)
	)
	_check("control_group_strip_has_nine_slots", group_strip != null and slice.hud.group_buttons.size() == 9)
	_check("retail_shadow_decal_equivalence_present", _retail_shadow_decals_present(slice))
	_check("outcome_layer_exists", slice.hud.outcome_layer != null and not slice.hud.outcome_layer.visible)
	_check("audio_controls_exist_in_pause", slice.hud.music_slider != null and slice.hud.voice_slider != null and slice.hud.mute_toggle != null)
	_check("source_mapping_not_preview_texture", String(slice.minimap.mapping_mode) == "source-derived-local-transform" and bool(slice.minimap.source_geometry_loaded) and not bool(slice.minimap.uses_source_preview_as_background))
	_check("snappy_radar_zoom_contract", is_equal_approx(float(slice.minimap.zoom_response_seconds), 0.09) and float(slice.minimap.radar_zoom_target) == 1.0)
	var player_fortress_position := Vector2(slice.simulation.structure(slice.simulation.fortress_id(0)).get("position", Vector2.INF))
	var camera_inset: float = minf(float(slice._camera_ground_constraint_inset()), minf(slice.source_map_data.local_bounds.size.x, slice.source_map_data.local_bounds.size.y) * 0.5 - 0.001)
	var camera_minimum: Vector2 = slice.source_map_data.local_bounds.position + Vector2(camera_inset, camera_inset)
	var camera_maximum: Vector2 = slice.source_map_data.local_bounds.end - Vector2(camera_inset, camera_inset)
	var expected_camera_focus: Vector2 = Vector2(
		clampf(player_fortress_position.x, camera_minimum.x, camera_maximum.x),
		clampf(player_fortress_position.y, camera_minimum.y, camera_maximum.y)
	)
	_check(
		"camera_starts_from_source_fortress_with_exact_constraint",
		slice.camera_focus.is_equal_approx(expected_camera_focus)
		and is_equal_approx(float(slice.camera_zoom), 1.0)
		and is_equal_approx(float(slice.camera_zoom_target), 1.0),
		"actual=%s expected=%s zoom=%.6f" % [str(slice.camera_focus), str(expected_camera_focus), float(slice.camera_zoom_target)]
	)
	_check("equipment_proof_loaded", bool(slice.equipment_proof_loaded))

	for id in [1, 2, 101, 102]:
		var battalion = slice.battalion_nodes.get(id)
		_check("battalion_%d_exists" % id, battalion != null)
		if battalion == null:
			continue
		_check("battalion_%d_has_15_retail_glbs" % id, int(battalion.member_count) == 15 and int(battalion.retail_visual_count) == 15, "members=%d retail=%d" % [battalion.member_count, battalion.retail_visual_count])
		_check("battalion_%d_has_15_rigs" % id, int(battalion.rigged_member_count) == 15, str(battalion.rigged_member_count))
		_check("battalion_%d_has_animation_players" % id, int(battalion.animation_player_count) >= 15, str(battalion.animation_player_count))
		var mounted_members: Array = battalion.member_visuals.values()
		_check(
			"battalion_%d_meshes_face_authoritative_forward" % id,
			mounted_members.size() == 15
				and mounted_members.all(func(member: Node3D) -> bool: return is_equal_approx(member.rotation.y, PI * 0.5))
		)
	for battalion_value in slice.battalion_nodes.values():
		var parity_battalion = battalion_value
		_check(
			"private_battalion_%d_has_no_synthetic_overlays" % int(parity_battalion.entity_id),
			bool(parity_battalion.private_parity_mode_active)
			and int(parity_battalion.synthetic_overlay_node_count()) == 0
			and not bool(parity_battalion.markers_visible()),
			"count=%d" % int(parity_battalion.synthetic_overlay_node_count())
		)
	var archer_battalion = _battalion_for_object_id(slice, "bfme2.object.gondor-archer")
	if archer_battalion != null and bool(archer_battalion.combat_visual_source_closure_present):
		_check(
			"gondor_archer_exact_projectile_and_impact_resources",
			int(archer_battalion.exact_projectile_node_count) > 0
			and int(archer_battalion.exact_impact_effect_node_count) > 0
			and String(archer_battalion.combat_visual_contract_error) == ""
			and archer_battalion.archer_projectile_controller != null
			and bool(archer_battalion.archer_projectile_controller.contract_ready)
			and not bool(archer_battalion.archer_projectile_controller.parity_ready)
			and int(archer_battalion.archer_projectile_controller.active_projectile_node_count) == 0
			and int(archer_battalion.archer_projectile_controller.active_impact_node_count) == 0
		)
	else:
		_check(
			"gondor_archer_projectile_impact_closure_blocker_is_explicit",
			archer_battalion != null
			and String(archer_battalion.combat_visual_contract_error).contains("GoodFactionArrow/GondorArcherArrow")
			and String(archer_battalion.combat_visual_contract_error).contains("EXArrowStreak01")
			and String(archer_battalion.combat_visual_contract_error).contains("FX_GoodArrowHit")
			and String(archer_battalion.combat_visual_contract_error).contains("ImpactArrow"),
			String(archer_battalion.combat_visual_contract_error if archer_battalion != null else "missing archer battalion")
		)

	var exemplar = slice.battalion_nodes.get(1)
	var enemy_exemplar = slice.battalion_nodes.get(101)
	if exemplar != null:
		_check("idle_clip_mapping", String(exemplar.clip_for_state("idle")) == "gumanmocap_idlb", String(exemplar.clip_for_state("idle")))
		_check("run_clip_mapping", String(exemplar.clip_for_state("run")) == "gumanmocap_runb", String(exemplar.clip_for_state("run")))
		_check("attack_clip_mapping", String(exemplar.clip_for_state("attack")) == "gumanmocap_atka", String(exemplar.clip_for_state("attack")))
		_check("death_clip_mapping", String(exemplar.clip_for_state("death")) == "gumanmocap_dieb", String(exemplar.clip_for_state("death")))
		_check("idle_variants_cover_source_set", exemplar.variant_clips_for_state("idle").size() == 9, str(exemplar.variant_clips_for_state("idle")))
		_check("run_variants_cover_source_set", exemplar.variant_clips_for_state("run").size() == 2, str(exemplar.variant_clips_for_state("run")))
		_check("death_variants_cover_source_set", exemplar.variant_clips_for_state("death").size() == 4, str(exemplar.variant_clips_for_state("death")))
		_check("idle_phase_variation_deterministic", int(exemplar.phase_variation_count("idle")) == 15, str(exemplar.phase_variation_count("idle")))
		_check("idle_variants_active", exemplar.active_clip_variants().size() == 9, str(exemplar.active_clip_variants()))
		_check("weapon_and_shield_semantics_proven", bool(exemplar.equipment_contract_ready) and exemplar.equipment_contract.has("right_hand_weapon") and exemplar.equipment_contract.has("left_hand_shield"))
		_check("attack_declares_weapon_timing", bool(exemplar.attack_uses_weapon_timing) and String(exemplar.clip_modes.get("attack", "")) == "once")
		_check("all_animation_tracks_resolved", int(exemplar.unresolved_animation_track_count) == 0)

	var blue_materials := _member_textured_materials(exemplar)
	var red_materials := _member_textured_materials(enemy_exemplar)
	_check("invented_team_tint_is_suppressed_in_private_parity", exemplar != null and enemy_exemplar != null and int(exemplar.team_tinted_surface_count) == 0 and int(enemy_exemplar.team_tinted_surface_count) == 0 and String(exemplar.team_color_status).contains("awaiting-exact-house-color"), "blue=%s red=%s status=%s" % [str(exemplar.team_tinted_surface_count if exemplar != null else -1), str(enemy_exemplar.team_tinted_surface_count if enemy_exemplar != null else -1), String(exemplar.team_color_status if exemplar != null else "missing")])
	_check("retail_textures_survive_without_invented_tint", blue_materials.size() == 15 and red_materials.size() == 15 and (blue_materials[0] as StandardMaterial3D).albedo_texture != null and (red_materials[0] as StandardMaterial3D).albedo_texture != null, "blue=%d red=%d" % [blue_materials.size(), red_materials.size()])
	if not blue_materials.is_empty() and not red_materials.is_empty():
		var blue_color := (blue_materials[0] as StandardMaterial3D).albedo_color
		var red_color := (red_materials[0] as StandardMaterial3D).albedo_color
		var color_distance := absf(blue_color.r - red_color.r) + absf(blue_color.g - red_color.g) + absf(blue_color.b - red_color.b)
		_check("private_retail_surface_colors_remain_source_neutral", color_distance < 0.0001, "%s vs %s" % [str(blue_color), str(red_color)])
		_check("private_retail_overlays_use_source_contracts", int(exemplar.member_overlay_node_count()) == 0 and int(enemy_exemplar.member_overlay_node_count()) == 0 and int(exemplar.source_selection_decal_count()) == 1 and int(enemy_exemplar.source_selection_decal_count()) == 1 and String(exemplar.member_overlay_status).contains("source-health-canvas-and-source-selection-merge-decal-bound") and String(exemplar.member_overlay_status).contains("oracle-color-throb-pending"), "blue=%s decals=%d red=%s decals=%d" % [String(exemplar.member_overlay_status), int(exemplar.source_selection_decal_count()), String(enemy_exemplar.member_overlay_status), int(enemy_exemplar.source_selection_decal_count())])

	_check("adaptive_music_closure", slice.audio_system != null and slice.audio_system.has_complete_audio_closure())
	_check("strict_four_unit_roster_audio_closure", slice.audio_system != null and slice.audio_system.has_complete_roster_audio_closure() and slice.audio_system.readiness_diagnostics().is_empty(), str(slice.audio_system.readiness_diagnostics() if slice.audio_system != null else ["missing_audio_system"]))
	_check("defeat_music_closure", slice.audio_system != null and slice.audio_system.music_streams.has("defeat"))
	_check("select_voice_closure", slice.audio_system != null and slice.audio_system.count_voice_kind("select") == 10, str(slice.audio_system.count_voice_kind("select") if slice.audio_system != null else -1))
	_check("soldier_attack_voice_closure", slice.audio_system != null and slice.audio_system.count_voice_kind("attack") == 6, str(slice.audio_system.count_voice_kind("attack") if slice.audio_system != null else -1))
	_check("starts_explore_music", slice.audio_system != null and String(slice.audio_system.current_music_state) == "explore", String(slice.audio_system.current_music_state if slice.audio_system != null else "missing"))
	var source_crossing_route: Dictionary = slice.source_map_data.query_route(
		Vector2(slice.simulation.entity(1)["position"]),
		Vector2(slice.simulation.entity(101)["position"])
	)
	var source_crossing_cells: Array[Vector2i] = []
	source_crossing_cells.assign(source_crossing_route.get("cells", []))
	_check("cross_river_route_is_bounded_astar", bool(source_crossing_route.get("valid", false)) and source_crossing_cells.size() > 2 and source_crossing_cells.size() <= slice.source_map_data.MAX_ROUTE_CELLS, "cells=%d reason=%s" % [source_crossing_cells.size(), String(source_crossing_route.get("reason", ""))])
	_check("cross_river_route_uses_nearest_valid_named_ford", String(source_crossing_route.get("ford_name", "")) == "ford2", String(source_crossing_route.get("ford_name", "")))
	_check("cross_river_route_respects_source_cells", _route_respects_source_navigation(slice.source_map_data, source_crossing_cells))
	_check("cross_river_route_avoids_non_ford_water", _route_water_only_in_named_fords(slice.source_map_data, source_crossing_cells))
	_check("cross_river_route_avoids_blocked_ford2_cell", not source_crossing_cells.has(Vector2i(208, 142)))
	for ford_name in ["ford1", "ford2", "ford3"]:
		var probe: Dictionary = slice.source_map_data.query_ford_probe(ford_name)
		var probe_cells: Array[Vector2i] = []
		probe_cells.assign(probe.get("cells", []))
		_check("named_%s_corridor_crosses_opposite_banks" % ford_name, bool(probe.get("valid", false)) and String(probe.get("ford_name", "")) == ford_name and _route_respects_source_navigation(slice.source_map_data, probe_cells) and _route_water_only_in_named_fords(slice.source_map_data, probe_cells) and _ford_probe_crosses_opposite_banks(slice.source_map_data, probe, ford_name), "selected=%s cells=%d" % [String(probe.get("ford_name", "")), probe_cells.size()])

	# Player interaction: selection and movement must immediately drive the real
	# imported battalion's runtime state and run clip.
	_check("select_player_battalion", bool(slice.test_select(1)))
	_check("selection_visible_in_sim", slice.simulation.selected_ids == [1], str(slice.simulation.selected_ids))
	var start_position := Vector2(slice.simulation.entity(1)["position"])
	var outside_destination: Vector2 = slice.source_map_data.grid_to_local_horizontal(Vector2i(19, 20))
	_check("outside_playable_move_rejected", int(slice.test_move(outside_destination)) == 0 and String(slice.simulation.last_route_rejection) == "outside-playable-area")
	_check("outside_rejection_does_not_move", Vector2(slice.simulation.entity(1)["position"]).is_equal_approx(start_position))
	var blocked_destination: Vector2 = slice.source_map_data.grid_to_local_horizontal(Vector2i(208, 142))
	_check("source_blocked_move_rejected", int(slice.test_move(blocked_destination)) == 0 and String(slice.simulation.last_route_rejection) == "blocked-destination")
	_check("move_order_accepted", int(slice.test_move(Vector2(-22.0, -23.0))) == 1)
	var same_side_cells: Array[Vector2i] = []
	same_side_cells.assign(slice.simulation.entity(1).get("route_cells", []))
	_check("same_side_route_respects_source_cells", _route_respects_source_navigation(slice.source_map_data, same_side_cells) and _route_water_only_in_named_fords(slice.source_map_data, same_side_cells), str(same_side_cells.size()))
	slice.step_for_test(1)
	_check("move_changes_position", Vector2(slice.simulation.entity(1)["position"]).distance_to(start_position) > 0.1)
	_check("move_uses_run_state", String(slice.simulation.entity(1)["state"]) == "run" and String(exemplar.current_clip) == "gumanmocap_runb", "%s/%s" % [slice.simulation.entity(1)["state"], exemplar.current_clip])
	_check("run_variants_active", exemplar.active_clip_variants().size() == 2, str(exemplar.active_clip_variants()))
	var order_indicator = slice.order_indicators.get(1)
	_check(
		"private_route_uses_exact_retail_move_hint_without_synthetic_flag",
		order_indicator != null
		and bool(order_indicator.private_parity_mode_active)
		and bool(order_indicator.retail_contract_ready)
		and bool(order_indicator.showing_order)
		and not order_indicator.route_points.is_empty()
		and int(order_indicator.retail_visual_node_count()) == 1
		and int(order_indicator.synthetic_overlay_node_count()) == 0
	)
	var retail_order_ids: Array[int] = [1]
	_check(
		"retail_stop_command_clears_route_and_target",
		int(slice.simulation.issue_stop(retail_order_ids)) == 1
			and (slice.simulation.entity(1).get("route", []) as Array).is_empty()
			and int(slice.simulation.entity(1).get("target_id", -1)) == 0
			and String(slice.simulation.entity(1).get("state", "")) == "idle"
	)
	_check(
		"retail_attack_move_command_arms_source_vision_acquisition",
		int(slice.simulation.issue_attack_move(retail_order_ids, Vector2(-20.0, -22.0))) == 1
			and bool(slice.simulation.entity(1).get("attack_move", false))
			and Vector2(slice.simulation.entity(1).get("attack_move_destination", Vector2.ZERO)).is_equal_approx(Vector2(-20.0, -22.0))
	)
	var assigned_ids: Array[int] = [2, 1]
	var assigned_group: Dictionary = slice.simulation.assign_control_group(1, assigned_ids)
	_check("retail_control_group_assigns_sorted", bool(assigned_group.get("ok", false)) and Array(assigned_group.get("entity_ids", [])) == [1, 2])
	_check("retail_control_group_recall", slice.simulation.recall_control_group(1) == [1, 2])

	# Each Men producer exposes only its declared roster. Per-unit gameplay keys
	# drive price, build time, command points, and the completed entity identity.
	var roster_rules: Dictionary = slice.gameplay_rules.duplicate(true)
	roster_rules.merge({
		"starting_resources": 5000,
		"command_point_cap": 1000,
		"soldier_cost": 201,
		"soldier_build_ticks": 2,
		"soldier_command_points": 11,
		"tower_guard_cost": 402,
		"tower_guard_build_ticks": 4,
		"tower_guard_command_points": 12,
		"archer_cost": 203,
		"archer_build_ticks": 3,
		"archer_command_points": 13,
		"knight_cost": 554,
		"knight_build_ticks": 5,
		"knight_command_points": 14,
		"ai_queue_interval_ticks": 15,
		"ai_attack_delay_ticks": 45,
	}, true)
	var roster_sim = SimScript.new()
	roster_sim.setup(slice.source_map_data.simulation_configuration(), roster_rules)
	roster_sim.ai_enabled = false
	var roster_barracks: int = roster_sim.producer_id(0, "barracks")
	var roster_archery: int = roster_sim.producer_id(0, "archery_range")
	var roster_stable: int = roster_sim.producer_id(0, "stable")
	_check("barracks_declares_soldier_and_tower_guard", Array(roster_sim.structure(roster_barracks).get("production", [])) == [SimScript.SOLDIER_HORDE_ID, SimScript.TOWER_GUARD_OBJECT_ID])
	_check("archery_range_declares_archer", Array(roster_sim.structure(roster_archery).get("production", [])) == [SimScript.ARCHER_OBJECT_ID])
	_check("stable_declares_knight", Array(roster_sim.structure(roster_stable).get("production", [])) == [SimScript.KNIGHT_OBJECT_ID])
	var wrong_producer_cases: Array[Dictionary] = [
		{"label": "soldier_from_archery", "producer": roster_archery, "unit_type": SimScript.SOLDIER_HORDE_ID},
		{"label": "tower_guard_from_stable", "producer": roster_stable, "unit_type": SimScript.TOWER_GUARD_OBJECT_ID},
		{"label": "archer_from_barracks", "producer": roster_barracks, "unit_type": SimScript.ARCHER_OBJECT_ID},
		{"label": "knight_from_barracks", "producer": roster_barracks, "unit_type": SimScript.KNIGHT_OBJECT_ID},
	]
	for wrong_case in wrong_producer_cases:
		var rejected: Dictionary = roster_sim.queue_unit(0, int(wrong_case["producer"]), String(wrong_case["unit_type"]))
		_check("wrong_producer_rejects_%s" % String(wrong_case["label"]), not bool(rejected.get("ok", true)) and String(rejected.get("reason", "")) == "unsupported-unit")
	var production_cases: Array[Dictionary] = [
		{"label": "soldier", "producer": roster_barracks, "unit_type": SimScript.SOLDIER_HORDE_ID, "object_id": SimScript.SOLDIER_OBJECT_ID, "name": "Gondor Soldiers", "cost": 201, "ticks": 2, "cp": 11},
		{"label": "tower_guard", "producer": roster_barracks, "unit_type": SimScript.TOWER_GUARD_OBJECT_ID, "object_id": SimScript.TOWER_GUARD_OBJECT_ID, "name": "Tower Guard", "cost": 402, "ticks": 4, "cp": 12},
		{"label": "archer", "producer": roster_archery, "unit_type": SimScript.ARCHER_OBJECT_ID, "object_id": SimScript.ARCHER_OBJECT_ID, "name": "Gondor Archers", "cost": 203, "ticks": 3, "cp": 13},
		{"label": "knight", "producer": roster_stable, "unit_type": SimScript.KNIGHT_OBJECT_ID, "object_id": SimScript.KNIGHT_OBJECT_ID, "name": "Gondor Knights", "cost": 554, "ticks": 5, "cp": 14},
	]
	for production_case in production_cases:
		var queued_case: Dictionary = roster_sim.queue_unit(0, int(production_case["producer"]), String(production_case["unit_type"]))
		var queued_item: Dictionary = queued_case.get("item", {})
		_check("%s_queues_from_correct_producer" % String(production_case["label"]), bool(queued_case.get("ok", false)))
		_check(
			"%s_uses_per_unit_rules" % String(production_case["label"]),
			int(queued_item.get("cost", -1)) == int(production_case["cost"])
			and int(queued_item.get("command_points", -1)) == int(production_case["cp"])
			and int(queued_item.get("complete_tick", -1)) == (6 if String(production_case["label"]) == "tower_guard" else int(production_case["ticks"])),
			str(queued_item)
		)
	roster_sim.advance(5)
	_check("barracks_second_item_waits_for_first", _entity_for_unit_type(roster_sim, 0, SimScript.TOWER_GUARD_OBJECT_ID).is_empty())
	roster_sim.advance(1)
	for production_case in production_cases:
		var completed: Dictionary = _entity_for_unit_type(roster_sim, 0, String(production_case["unit_type"]))
		_check(
			"%s_completes_with_typed_identity" % String(production_case["label"]),
			not completed.is_empty()
			and String(completed.get("unit_type", "")) == String(production_case["unit_type"])
			and String(completed.get("object_id", "")) == String(production_case["object_id"])
			and String(completed.get("name", "")) == String(production_case["name"])
			and int(completed.get("command_points", -1)) == int(production_case["cp"]),
			str(completed)
		)
	var ai_roster_sim = SimScript.new()
	ai_roster_sim.setup(slice.source_map_data.simulation_configuration(), roster_rules)
	ai_roster_sim.advance(15)
	var ai_barracks: int = ai_roster_sim.producer_id(1, "barracks")
	var ai_archery: int = ai_roster_sim.producer_id(1, "archery_range")
	var ai_stable: int = ai_roster_sim.producer_id(1, "stable")
	_check("ai_uses_barracks_roster_deterministically", _queued_unit_types(ai_roster_sim, ai_barracks) == [SimScript.SOLDIER_HORDE_ID, SimScript.TOWER_GUARD_OBJECT_ID])
	_check("ai_uses_archery_range_deterministically", _queued_unit_types(ai_roster_sim, ai_archery) == [SimScript.ARCHER_OBJECT_ID])
	_check("ai_uses_stable_deterministically", _queued_unit_types(ai_roster_sim, ai_stable) == [SimScript.KNIGHT_OBJECT_ID])
	var ai_reached_source_vision := false
	for _tick in range(1500):
		if _team_has_target(ai_roster_sim, SimScript.ENEMY_TEAM):
			ai_reached_source_vision = true
			break
		ai_roster_sim.tick()
	_check("four_unit_ai_preserves_attack_loop", ai_reached_source_vision)

	# Barracks production is authoritative and dynamically creates a 15-member
	# retail battalion presentation on the exact completion tick.
	var barracks_id: int = slice.simulation.producer_id(0, "barracks")
	var queued: Dictionary = slice.simulation.queue_unit(0, barracks_id)
	_check("player_barracks_queues_soldiers", bool(queued.get("ok", false)))
	slice.step_for_test(int(slice.gameplay_rules["soldier_build_ticks"]) - 1)
	_check("production_not_early_in_scene", not slice.simulation.entities.has(10))
	slice.step_for_test(1)
	_check("produced_battalion_gets_runtime_presentation", slice.simulation.entities.has(10) and slice.battalion_nodes.has(10) and int(slice.battalion_nodes[10].member_count) == 15)
	var produced_battalion = slice.battalion_nodes.get(10)
	_check("produced_battalion_reuses_validated_capability", produced_battalion != null and bool(produced_battalion.equipment_contract_ready) and produced_battalion.equipment_contract.has("right_hand_weapon") and produced_battalion.equipment_contract.has("left_hand_shield") and int(produced_battalion.unresolved_animation_track_count) == 0)
	_check("produced_battalion_mounts_soldier_retail_glb", produced_battalion != null and String(produced_battalion.object_id) == "bfme2.object.gondor-fighter" and String(produced_battalion.retail_model_filename) == "gondor-fighter.glb" and int(produced_battalion.retail_visual_count) == 15)

	# Run a complete battle through public commands, capturing actual animation
	# presentation states at attack/death and deterministic audio transitions.
	slice.reset_match()
	slice.simulation.ai_enabled = false
	_check("battle_select_one", bool(slice.test_select(1)))
	_check("battle_multi_select_two", bool(slice.simulation.toggle_selection(2)))
	_check("attack_order_one", int(slice.test_attack(101)) == 2)
	var saw_attack := _advance_until(slice, func(): return _any_state(slice.simulation, 0, "attack"), 900)
	_check("attack_state_reached", saw_attack)
	if saw_attack:
		slice._sync_presentation()
		var attacking_id := _first_state_id(slice.simulation, 0, "attack")
		var attacking_node = slice.battalion_nodes.get(attacking_id)
		# Multi-unit roster: Soldier uses gumanmocap_atka, Archer guarcher_atk*, etc.
		var attack_clip := String(attacking_node.current_clip) if attacking_node != null else ""
		var attack_allowed := false
		if attacking_node != null:
			var allowed_attack: Array = attacking_node.variant_clips_for_state("attack")
			if allowed_attack.is_empty():
				allowed_attack = [String(attacking_node.clip_for_state("attack"))]
			attack_allowed = allowed_attack.has(attack_clip) and attack_clip != ""
		_check("attack_drives_imported_clip", attack_allowed, attack_clip)
	var first_defeated := _advance_until(slice, func(): return int(slice.simulation.entity(101)["health"]) == 0, 240)
	_check("first_enemy_defeated", first_defeated)
	if first_defeated:
		slice._sync_presentation()
		var defeated_node = slice.battalion_nodes.get(101)
		_check("death_drives_imported_clip", defeated_node != null and String(defeated_node.current_state) == "death" and String(defeated_node.current_clip) == "gumanmocap_dieb", "%s/%s" % [defeated_node.current_state, defeated_node.current_clip] if defeated_node != null else "missing")
		_check("death_variants_active", defeated_node != null and defeated_node.active_clip_variants().size() == 4, str(defeated_node.active_clip_variants() if defeated_node != null else []))
		_check("defeated_markers_hidden", defeated_node != null and not bool(defeated_node.markers_visible()))
	_check("attack_order_two", int(slice.test_attack(102)) >= 1)
	var second_defeated := _advance_until(slice, func(): return int(slice.simulation.entity(102)["health"]) == 0, 900)
	_check("second_enemy_defeated", second_defeated)
	var enemy_fortress: int = slice.simulation.fortress_id(1)
	_check("enemy_fortress_attack_order", enemy_fortress != 0 and int(slice.test_attack(enemy_fortress)) >= 1)
	var battle_finished := _advance_until(slice, func(): return int(slice.simulation.winner) != -1, 14000)
	_check("battle_reaches_victory", battle_finished and int(slice.simulation.winner) == 0, "winner=%d" % int(slice.simulation.winner))
	slice._sync_presentation()
	_check("battle_music_intent", _event_kind_present(slice.simulation.events, "music.battle"))
	_check("select_voice_intent", _event_kind_present(slice.simulation.events, "voice.select"))
	_check("attack_voice_intent", _event_kind_present(slice.simulation.events, "voice.attack"))
	_check("victory_music_active", String(slice.audio_system.current_music_state) == "victory", String(slice.audio_system.current_music_state))
	_check("victory_splash_visible", bool(slice.hud.outcome_layer.visible) and String(slice.hud.outcome_title.text) == "VICTORY")

	var first_signature := String(slice.simulation.state_signature())
	var replay := SimScript.new()
	var replay_signature := _run_reference_battle(replay, slice.source_map_data.simulation_configuration(), slice.gameplay_rules)
	_check("deterministic_replay_signature", first_signature == replay_signature, "%s != %s" % [first_signature, replay_signature])
	print("RETAIL_SLICE_SIGNATURE %s" % first_signature)

	# Let the deterministic enemy play a complete unassisted match. A player loss
	# must have its own simulation intent and activate the imported defeat track.
	slice.reset_match()
	var defeat_finished := _advance_until(slice, func(): return int(slice.simulation.winner) != -1, 36000)
	slice._sync_presentation()
	_check("battle_reaches_defeat", defeat_finished and int(slice.simulation.winner) == SimScript.ENEMY_TEAM, "winner=%d tick=%d state=%s" % [int(slice.simulation.winner), int(slice.simulation.tick_index), _compact_combat_state(slice.simulation)])
	_check("defeat_event_intent", _event_kind_present(slice.simulation.events, "match.defeat") and _event_kind_present(slice.simulation.events, "music.defeat"))
	_check("defeat_music_active", String(slice.audio_system.current_music_state) == "defeat", String(slice.audio_system.current_music_state))
	_check("defeat_splash_visible", bool(slice.hud.outcome_layer.visible) and String(slice.hud.outcome_title.text) == "DEFEAT")
	var defeat_signature := String(slice.simulation.state_signature())
	var defeat_replay := SimScript.new()
	var defeat_replay_signature := _run_reference_defeat(defeat_replay, slice.source_map_data.simulation_configuration(), slice.gameplay_rules)
	_check("deterministic_defeat_signature", defeat_signature == defeat_replay_signature, "%s != %s" % [defeat_signature, defeat_replay_signature])

	slice.reset_match()
	var paused_before := bool(slice.simulation_paused)
	slice.toggle_escape_menu()
	_check("escape_menu_pauses", not paused_before and bool(slice.simulation_paused) and bool(slice.pause_panel.visible))
	slice.toggle_escape_menu()
	_check("escape_menu_resumes", not bool(slice.simulation_paused) and not bool(slice.pause_panel.visible))
	_check("route_queries_reuse_cached_navigation", int(slice.source_map_data.navigation_build_count) == 1 and int(slice.source_map_data.route_query_count) > 10, "builds=%d queries=%d" % [slice.source_map_data.navigation_build_count, slice.source_map_data.route_query_count])
	print("RETAIL_NAV_METRICS walkable=%d water_blocked=%d ford_corridor=%d route_queries=%d" % [slice.source_map_data.navigation_walkable_count, slice.source_map_data.navigation_water_blocked_count, slice.source_map_data.navigation_ford_corridor_count, slice.source_map_data.route_query_count])

	var asset_factory = load("res://src/view/asset_factory.gd")
	_check("mesh_cache_is_bounded", asset_factory.mesh_cache_size() > 0 and asset_factory.mesh_cache_size() <= asset_factory.MAX_MESH_CACHE_ENTRIES, str(asset_factory.mesh_cache_size()))
	slice.cleanup_for_test()
	_check("mesh_cache_clears_on_slice_cleanup", asset_factory.mesh_cache_size() == 0, str(asset_factory.mesh_cache_size()))
	slice.queue_free()
	replay = null
	defeat_replay = null
	await process_frame
	await process_frame
	_finish()


func _advance_until(slice, predicate: Callable, maximum_ticks: int) -> bool:
	for _index in range(maximum_ticks):
		if predicate.call():
			return true
		slice.simulation.tick()
	return bool(predicate.call())


func _run_archer_projectile_contract_fixture() -> void:
	var controller_script: GDScript = load(ARCHER_PROJECTILE_CONTROLLER_PATH)
	_check("archer_projectile_controller_script_loads", controller_script != null)
	if controller_script == null:
		return
	var controller = controller_script.new()
	root.add_child(controller)
	var fixture := _archer_projectile_contract_fixture()
	_check(
		"archer_projectile_exact_contract_fixture_validates",
		controller.validate_contract_shape(fixture)
		and bool(controller.contract_declared)
		and not bool(controller.contract_ready)
		and not bool(controller.presentation_assets_ready)
		and not bool(controller.parity_ready)
		and int(controller.active_projectile_node_count) == 0
		and int(controller.active_impact_node_count) == 0,
		String(controller.error)
	)
	var changed := fixture.duplicate(true)
	(changed["projectilePresentation"] as Dictionary)["length"] = 14
	_check(
		"archer_projectile_contract_fixture_fails_closed_on_streak_drift",
		not controller.validate_contract_shape(changed)
		and String(controller.error).contains("W3DStreakDraw")
		and int(controller.active_projectile_node_count) == 0
		and int(controller.active_impact_node_count) == 0,
		String(controller.error)
	)
	controller.queue_free()


func _archer_projectile_contract_fixture() -> Dictionary:
	var fire_tokens: Array[String] = []
	for code in range("a".unicode_at(0), "z".unicode_at(0) + 1):
		fire_tokens.append("GUArche_weapo1" + String.chr(code))
	for code in range("a".unicode_at(0), "f".unicode_at(0) + 1):
		fire_tokens.append("GUArche_weapo2" + String.chr(code))
	var impact_tokens: Array[String] = []
	for group in [["flesh1", "v"], ["wood1", "x"], ["dirt1", "p"], ["gener1", "f"]]:
		for code in range("a".unicode_at(0), String(group[1]).unicode_at(0) + 1):
			impact_tokens.append("WIArrow_%s%s" % [String(group[0]), String.chr(code)])
	var mappings: Array[Dictionary] = []
	for damage_fx_set_id in ["NormalDamageFX", "GwaihirDamageFX", "FellBeastDamageFX", "MumakilDamageFX"]:
		mappings.append({
			"damageFxSetId": damage_fx_set_id,
			"damageFxType": "GOOD_ARROW_PIERCE",
			"majorFxListId": "FX_GoodArrowHit",
			"source": _archer_source_fixture("DamageFX", damage_fx_set_id, "data/ini/damagefx.ini"),
		})
	return {
		"schema": "openbfme.archer-projectile-binding",
		"schemaVersion": 0,
		"unitObjectId": "GondorArcher",
		"projectilePresentation": {
			"kind": "W3DStreakDraw",
			"length": 15,
			"width": 2,
			"numSegments": 1,
			"additive": false,
			"model": null,
			"color": {"r": 255, "g": 255, "b": 255},
			"texture": "assets/textures/combat/archer/exarrowstreak01.png",
			"snowTexture": "assets/textures/combat/archer/exarrowstreak_snow.png",
			"trajectory": {
				"kind": "BezierProjectileBehavior",
				"firstHeight": 9,
				"secondHeight": 9,
				"firstPercentIndent": 20,
				"secondPercentIndent": 90,
				"curveFlattenMinDist": 100.0,
				"flightPathAdjustDistPerSecond": 50,
				"groundHitFxListId": "FX_GondorArrowDeath",
			},
			"reskinSource": {
				"kind": "ObjectReskin",
				"name": "GondorArcherArrow",
				"parent": "GoodFactionArrow",
				"sourceVirtualPath": "data/ini/object/goodfaction/goodfactionsubobjects.ini",
				"sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
				"byteLength": 1,
				"startLine": 1,
				"endLine": 1,
			},
			"source": _archer_source_fixture("Object", "GoodFactionArrow", "data/ini/object/goodfaction/goodfactionsubobjects.ini"),
		},
		"weapon": {
			"weaponTemplateId": "GondorArcherBow",
			"warheadTemplateId": "GondorArcherBowWarhead",
			"projectileTemplateId": "GondorArcherArrow",
			"inheritedProjectileObjectId": "GoodFactionArrow",
			"fireFxListId": "FX_RohanArcherBowWeapon",
			"hitPercentage": 100,
			"scatterRadius": 16.0,
			"speed": {"minimum": 241, "nominal": 321, "maximum": 481, "scaleWithRange": true},
			"source": _archer_source_fixture("Weapon", "GondorArcherBow", "data/ini/weapon.ini"),
		},
		"damage": {
			"damageType": "PIERCE",
			"damageFxType": "GOOD_ARROW_PIERCE",
			"majorFxMappings": mappings,
			"source": _archer_source_fixture("Weapon", "GondorArcherBowWarhead", "data/ini/weapon.ini"),
		},
		"impactPresentation": {
			"fxListId": "FX_GoodArrowHit",
			"attachedModelId": "g_arrow",
			"glb": "assets/models/combat/g-arrow.glb",
			"soundEventId": "ImpactArrow",
			"source": _archer_source_fixture("FXList", "FX_GoodArrowHit", "data/ini/fxlist.ini"),
		},
		"audioEvents": [
			_archer_audio_event_fixture("ArcherWeapon", fire_tokens),
			_archer_audio_event_fixture("ImpactArrow", impact_tokens),
		],
		"unresolvedEngineSemantics": [
			"Projectile spawn time must come from the authoritative Gondor Archer attack event; the INI closure does not prove the Godot animation event frame.",
			"GOOD_ARROW_PIERCE resolves through the struck object's DamageFX set; the four authored mappings are preserved and must not be treated as one global unconditional hit effect.",
			"The retail source does not specify deterministic random-pool selection seeds for ArcherWeapon or ImpactArrow.",
			"g_arrow is an impact AttachedModel with a W3D animation header; its attachment orientation, lifetime, and playback policy require an original-engine runtime oracle.",
			"FX_GondorArrowDeath is the ground-hit branch and is named here but lies outside the requested target-hit closure.",
		],
	}


func _archer_audio_event_fixture(event_id: String, tokens: Array[String]) -> Dictionary:
	var paths: Array[String] = []
	for token in tokens:
		paths.append("data/audio/sounds/%s.wav" % token.to_lower())
	return {
		"eventId": event_id,
		"settings": {
			"Control": "interrupt",
			"Limit": "2" if event_id == "ArcherWeapon" else "3",
			"PitchShift": "-2 2" if event_id == "ArcherWeapon" else "-5 5",
			"Priority": "normal" if event_id == "ArcherWeapon" else "low",
			"SubmixSlider": "SoundFX",
			"Type": "world shrouded everyone",
			"Volume": "60" if event_id == "ArcherWeapon" else "45",
			"VolumeShift": "-10" if event_id == "ArcherWeapon" else "-20",
		},
		"soundTokens": tokens,
		"sourceVirtualPaths": paths,
		"source": _archer_source_fixture("AudioEvent", event_id, "data/ini/soundeffects.ini"),
	}


func _archer_source_fixture(kind: String, source_name: String, path: String) -> Dictionary:
	return {
		"kind": kind,
		"name": source_name,
		"sourceVirtualPath": path,
		"sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
		"byteLength": 1,
		"startLine": 1,
		"endLine": 1,
	}


func _run_reference_battle(simulation, map_configuration: Dictionary, gameplay_rules: Dictionary) -> String:
	simulation.setup(map_configuration, gameplay_rules)
	simulation.ai_enabled = false
	simulation.select_only(1)
	simulation.toggle_selection(2)
	simulation.issue_attack(simulation.selected_ids.duplicate(), 101)
	for _index in range(900):
		if int(simulation.entity(101)["health"]) == 0:
			break
		simulation.tick()
	simulation.issue_attack(simulation.selected_ids.duplicate(), 102)
	for _index in range(900):
		if int(simulation.entity(102)["health"]) == 0:
			break
		simulation.tick()
	simulation.issue_attack(simulation.selected_ids.duplicate(), simulation.fortress_id(1))
	for _index in range(14000):
		if int(simulation.winner) != -1:
			break
		simulation.tick()
	return simulation.state_signature()


func _run_reference_defeat(simulation, map_configuration: Dictionary, gameplay_rules: Dictionary) -> String:
	simulation.setup(map_configuration, gameplay_rules)
	for _index in range(36000):
		if int(simulation.winner) != -1:
			break
		simulation.tick()
	return simulation.state_signature()


func _home_layout_walkable(slice) -> bool:
	if slice.source_map_data == null:
		return false
	for id in slice.simulation.structure_ids():
		var position := Vector2(slice.simulation.structure(id).get("position", Vector2.INF))
		var cell: Vector2i = slice.source_map_data.local_to_grid_cell(position)
		if not slice.source_map_data.is_grid_inside_playable(cell) or not slice.source_map_data.is_navigation_walkable(cell):
			return false
	return true


func _battalion_for_object_id(slice, object_id: String):
	for id in slice.battalion_nodes.keys():
		var battalion = slice.battalion_nodes[id]
		if String(battalion.object_id) == object_id:
			return battalion
	return null


func _entity_for_unit_type(simulation, team: int, unit_type: String) -> Dictionary:
	for id in simulation.entity_ids():
		var row: Dictionary = simulation.entity(id)
		if int(row.get("team", -1)) == team and String(row.get("unit_type", "")) == unit_type and int(id) >= (10 if team == 0 else 110):
			return row
	return {}


func _queued_unit_types(simulation, producer: int) -> Array[String]:
	var result: Array[String] = []
	for item_value in Array(simulation.structure(producer).get("queue", [])):
		if typeof(item_value) == TYPE_DICTIONARY:
			result.append(String((item_value as Dictionary).get("unit_type", "")))
	return result


func _team_has_target(simulation, team: int) -> bool:
	for id in simulation.living_ids(team):
		if int(simulation.entity(id).get("target_id", 0)) != 0:
			return true
	return false


func _member_textured_materials(battalion) -> Array[StandardMaterial3D]:
	var result: Array[StandardMaterial3D] = []
	if battalion == null:
		return result
	for child in battalion.get_children():
		if not child.has_meta("content_object_id"):
			continue
		var material := _first_textured_material(child)
		if material != null:
			result.append(material)
	return result


func _first_textured_material(node: Node) -> StandardMaterial3D:
	var stack: Array[Node] = [node]
	while not stack.is_empty():
		var current: Node = stack.pop_back()
		if current is MeshInstance3D:
			var instance := current as MeshInstance3D
			if instance.mesh != null:
				for surface in range(instance.mesh.get_surface_count()):
					var material: Material = instance.get_surface_override_material(surface)
					if material == null:
						material = instance.mesh.surface_get_material(surface)
					if material is StandardMaterial3D and (material as StandardMaterial3D).albedo_texture != null:
						return material as StandardMaterial3D
		for child in current.get_children():
			stack.append(child)
	return null


func _any_state(simulation, team: int, state: String) -> bool:
	return _first_state_id(simulation, team, state) != 0


func _first_state_id(simulation, team: int, state: String) -> int:
	for id in simulation.entity_ids():
		var entity: Dictionary = simulation.entity(id)
		if int(entity["team"]) == team and String(entity["state"]) == state:
			return id
	return 0


func _event_kind_present(events: Array[Dictionary], kind: String) -> bool:
	for event in events:
		if String(event.get("kind", "")) == kind:
			return true
	return false


func _gate_names(gates: Array[Dictionary]) -> Array[String]:
	var result: Array[String] = []
	for gate in gates:
		result.append(String(gate.get("name", "")))
	return result


func _gate_source_ids(gates: Array[Dictionary]) -> Array[int]:
	var result: Array[int] = []
	for gate in gates:
		result.append(int(gate.get("source_river_id", -1)))
	return result


func _source_height_samples_match(map_data) -> bool:
	return (
		int(map_data.height_raw_at(20, 20)) == 10246
		and int(map_data.height_raw_at(70, 237)) == 7680
		and int(map_data.height_raw_at(303, 69)) == 7680
		and int(map_data.height_raw_at(208, 142)) == 7168
		and int(map_data.height_raw_at(200, 176)) == 7542
		and int(map_data.height_raw_at(0, 0)) == 7680
		and int(map_data.height_raw_at(414, 352)) == 7652
	)


func _source_terrain_tile_samples_match(map_data) -> bool:
	return (
		int(map_data.terrain_tile_index_at(20, 20)) == 1656
		and int(map_data.terrain_base_texture_index_at(20, 20)) == 26
		and int(map_data.terrain_tile_index_at(70, 237)) == 46
		and int(map_data.terrain_base_texture_index_at(70, 237)) == 0
		and int(map_data.terrain_tile_index_at(303, 69)) == 47
		and int(map_data.terrain_base_texture_index_at(303, 69)) == 0
		and int(map_data.terrain_tile_index_at(208, 142)) == 3376
		and int(map_data.terrain_base_texture_index_at(208, 142)) == 52
		and int(map_data.terrain_tile_index_at(200, 176)) == 3008
		and int(map_data.terrain_base_texture_index_at(200, 176)) == 47
	)


func _route_respects_source_navigation(map_data, cells: Array[Vector2i]) -> bool:
	if cells.is_empty() or cells.size() > 1024:
		return false
	for cell in cells:
		if not map_data.is_grid_inside_playable(cell) or map_data.is_impassable_at(cell.x, cell.y) or not map_data.is_navigation_walkable(cell):
			return false
	return true


func _route_water_only_in_named_fords(map_data, cells: Array[Vector2i]) -> bool:
	for cell in cells:
		if map_data.is_water_cell(cell) and not map_data.is_ford_corridor_cell(cell):
			return false
	return true


func _ford_probe_crosses_opposite_banks(map_data, probe: Dictionary, ford_name: String) -> bool:
	var bank_a := Vector2i(probe.get("probe_bank_a", Vector2i(-1, -1)))
	var bank_b := Vector2i(probe.get("probe_bank_b", Vector2i(-1, -1)))
	if not map_data.is_navigation_walkable(bank_a) or not map_data.is_navigation_walkable(bank_b) or map_data.is_water_cell(bank_a) or map_data.is_water_cell(bank_b):
		return false
	var edge_a := Vector2(probe.get("probe_edge_a", Vector2.ZERO))
	var edge_b := Vector2(probe.get("probe_edge_b", Vector2.ZERO))
	var direction := edge_a.direction_to(edge_b)
	var midpoint := (edge_a + edge_b) * 0.5
	var local_bank_a: Vector2 = map_data.grid_to_local_horizontal(bank_a)
	var local_bank_b: Vector2 = map_data.grid_to_local_horizontal(bank_b)
	if (local_bank_a - midpoint).dot(direction) >= 0.0 or (local_bank_b - midpoint).dot(direction) <= 0.0:
		return false
	var cells: Array[Vector2i] = []
	cells.assign(probe.get("cells", []))
	for cell in cells:
		if map_data.is_water_cell(cell) and map_data.is_named_ford_corridor_cell(cell, ford_name):
			return true
	return false


func _bound_prop_type_visibility_matches(battlefield: Node, source_type: String, expected_count: int, expected_visible: bool, expected_reason: String) -> bool:
	var container: Node3D = battlefield.get("retail_prop_container") as Node3D
	if container == null:
		return false
	var matched_count := 0
	for child in container.get_children():
		var placement := child as Node3D
		if placement == null or String(placement.get_meta("source_type", "")) != source_type:
			continue
		matched_count += 1
		if placement.visible != expected_visible:
			return false
		if not expected_visible and (
			not bool(placement.get_meta("main_camera_excluded", false))
			or String(placement.get_meta("main_camera_exclusion_reason", "")) != expected_reason
		):
			return false
	return matched_count == expected_count


func _unresolved_diagnostic_sample_count(battlefield: Node) -> int:
	var result := 0
	for node_name in ["SourceVegetationPlacementMarkers", "SourceRockPlacementMarkers"]:
		var diagnostic := battlefield.find_child(node_name, true, false)
		if diagnostic != null:
			if not bool(diagnostic.get_meta("diagnostic_only", false)):
				return -1
			result += int(diagnostic.get_meta("source_placement_count", -1))
	return result


func _visible_source_placeholder_count(node: Node, name_prefix: String) -> int:
	var result := 0
	var stack: Array[Node] = [node]
	while not stack.is_empty():
		var current: Node = stack.pop_back()
		if current is GeometryInstance3D and String(current.name).begins_with(name_prefix):
			result += 1
		for child in current.get_children():
			stack.append(child)
	return result


func _visible_unresolved_placeholder_count(node: Node) -> int:
	var result := 0
	var stack: Array[Node] = [node]
	while not stack.is_empty():
		var current: Node = stack.pop_back()
		if current is GeometryInstance3D and String(current.get_meta("presentation", "")) == "unresolved-marker":
			result += 1
		for child in current.get_children():
			stack.append(child)
	return result


func _compact_combat_state(simulation) -> String:
	var rows: Array[String] = []
	for id in simulation.entity_ids():
		var row: Dictionary = simulation.entity(id)
		rows.append("e%d:t%d:%s:target%d:hp%d:p%s" % [id, int(row.get("team", -1)), String(row.get("state", "")), int(row.get("target_id", 0)), int(row.get("health", 0)), str(row.get("position", Vector2.ZERO))])
	for id in [simulation.fortress_id(0), simulation.fortress_id(1)]:
		var row: Dictionary = simulation.structure(id)
		rows.append("s%d:t%d:hp%d:p%s" % [id, int(row.get("team", -1)), int(row.get("health", 0)), str(row.get("position", Vector2.ZERO))])
	return "|".join(rows)


func _check_retail_unit_rules(slice: Node) -> void:
	# This is the focused retail movement/range/timing contract. Values are
	# deliberately exact BFME II 1.06 values, not tolerance bands.
	var content_db = root.get_node_or_null("ContentDB")
	_check("retail_unit_rules_content_db_loaded", content_db != null)
	if content_db == null:
		return
	var source_one: Vector3 = slice.source_map_data.player_starts["Player_1_Start"]
	var source_two: Vector3 = slice.source_map_data.player_starts["Player_2_Start"]
	var source_separation := Vector2(source_one.x, source_one.z).distance_to(Vector2(source_two.x, source_two.z))
	var scale: float = slice.source_map_data.LOCAL_START_SEPARATION / source_separation
	_check("retail_unit_world_scale_exact", float(slice.source_map_data.local_transform_scale) == scale, str(slice.source_map_data.local_transform_scale))
	var expected := {
		"bfme2.object.gondor-fighter": {"entity": 1, "speed": 50.0, "range": 11.5, "delay": 1000.0, "pre": 500.0, "firing": 1000.0, "damage": 40, "members": 15, "horde": "GondorFighterHorde", "weapon": "GondorSword", "first_slot_z": 20.0},
		"bfme2.object.gondor-archer": {"entity": 2, "speed": 47.0, "range": 300.0, "delay": 0.0, "pre": 200.0, "firing": 0.0, "damage": 25, "members": 15, "horde": "GondorArcherHorde", "weapon": "GondorArcherBow", "first_slot_z": 20.0},
		"bfme2.object.gondor-tower-guard": {"entity": 102, "speed": 37.0, "range": 35.0, "delay": 1000.0, "pre": 500.0, "firing": 1000.0, "damage": 50, "members": 15, "horde": "GondorTowerShieldGuardHorde", "weapon": "GondorTowerShieldGuardSword", "first_slot_z": 20.0},
		"bfme2.object.gondor-knight": {"entity": 103, "speed": 80.0, "range": 11.5, "delay": 1000.0, "pre": 500.0, "firing": 1000.0, "damage": 35, "members": 10, "horde": "GondorKnightHorde", "weapon": "GondorCavalrySword", "first_slot_z": 15.0},
	}
	var distinct_speeds := {}
	var distinct_ranges := {}
	for object_id in expected:
		var values: Dictionary = expected[object_id]
		var packed: Dictionary = content_db.get_retail_unit_rules(object_id)
		var horde: Dictionary = packed.get("horde", {})
		var member: Dictionary = packed.get("member", {})
		var locomotor_set: Dictionary = horde.get("locomotorSet", {})
		var weapon: Dictionary = member.get("weapon", {})
		var speed_field: Dictionary = locomotor_set.get("speed", {})
		var range_field: Dictionary = weapon.get("attackRange", {})
		var delay_field: Dictionary = weapon.get("delayBetweenShotsMs", {})
		var speed_source: Dictionary = speed_field.get("source", {})
		var range_source: Dictionary = range_field.get("source", {})
		var row: Dictionary = slice.simulation.entity(int(values["entity"]))
		var unit_rule: Dictionary = slice.gameplay_rules.get("unit_rules", {}).get(object_id, {})
		var provenance: Dictionary = row.get("retail_rule_provenance", {})
		var battalion = _battalion_for_object_id(slice, object_id)
		var expected_slot := Vector3(0.0, 0.0, float(values["first_slot_z"]) * scale)
		_check(
			"%s_pack_rules_exact" % object_id,
			float(speed_field.get("value", -1.0)) == float(values["speed"])
			and float(range_field.get("value", -1.0)) == float(values["range"])
			and float(delay_field.get("value", -1.0)) == float(values["delay"])
			and float(weapon.get("preAttackDelayMs", {}).get("value", -1.0)) == float(values["pre"])
			and float(weapon.get("firingDurationMs", {}).get("value", -1.0)) == float(values["firing"])
			and int(weapon.get("damage", {}).get("value", -1)) == int(values["damage"])
			and int(horde.get("formation", {}).get("memberCount", -1)) == int(values["members"]),
			str(packed)
		)
		_check(
			"%s_pack_provenance_exact" % object_id,
			String(speed_source.get("ini", "")) == "data/ini/object/goodfaction/hordes/men/menhordes.ini"
			and String(speed_source.get("scopeName", "")) == String(values["horde"])
			and String(range_source.get("ini", "")) == "data/ini/weapon.ini"
			and String(range_source.get("scopeName", "")) == String(values["weapon"])
			and String(delay_field.get("source", {}).get("scopeName", "")) == String(values["weapon"]),
			"speed=%s range=%s" % [str(speed_source), str(range_source)]
		)
		_check(
			"%s_sim_row_exact" % object_id,
			float(row.get("speed_source", -1.0)) == float(values["speed"])
			and float(row.get("speed", -1.0)) == float(values["speed"]) * scale
			and float(row.get("attack_range_source", -1.0)) == float(values["range"])
			and float(row.get("attack_range", -1.0)) == float(values["range"]) * scale
			and float(row.get("delay_between_shots_ms", -1.0)) == float(values["delay"])
			and float(row.get("pre_attack_delay_ms", -1.0)) == float(values["pre"])
			and float(row.get("firing_duration_ms", -1.0)) == float(values["firing"])
			and int(row.get("member_damage", -1)) == int(values["damage"])
			and int(row.get("member_count", -1)) == int(values["members"])
			and not provenance.is_empty(),
			str(row)
		)
		_check(
			"%s_formation_spacing_exact" % object_id,
			Array(unit_rule.get("formation_positions", [])).size() == int(values["members"])
			and battalion != null
			and Vector3(battalion.member_formation_slot(0)) == expected_slot,
			str(battalion.member_formation_slot(0) if battalion != null else Vector3.INF)
		)
		distinct_speeds[float(values["speed"])] = true
		distinct_ranges[float(values["range"])] = true
	_check("four_retail_speeds_are_distinct", distinct_speeds.size() == 4, str(distinct_speeds.keys()))
	# Retail defines the same STANDARD_MELEE_ATTACK_RANGE (11.5) for Soldier
	# and Knight; the exact per-type records therefore contain three values.
	_check("retail_attack_ranges_are_per_type_exact", distinct_ranges.size() == 3 and float(expected["bfme2.object.gondor-fighter"]["range"]) == float(expected["bfme2.object.gondor-knight"]["range"]), str(distinct_ranges.keys()))
	_check("archer_range_is_much_greater_than_melee", 300.0 * scale > 20.0 * (11.5 * scale))


func _retail_shadow_decals_present(slice) -> bool:
	# Approved shadow equivalence: every battalion member and every structure
	# carries the retail shadow-color blob decal (retail draws shadows via
	# decals with shadow mapping disabled).
	var source_shadow_color := Color(0.0, 0.0, 0.0, 64.0 / 255.0)
	var battalion_decals := 0
	for battalion_value in slice.battalion_nodes.values():
		for decal in (battalion_value as Node).find_children("RetailShadowDecal", "Decal", true, false):
			if (decal as Decal).modulate == source_shadow_color:
				battalion_decals += 1
	var structure_decals := 0
	for structure_value in slice.structure_nodes.values():
		for decal in (structure_value as Node).find_children("RetailShadowDecal", "Decal", true, false):
			if (decal as Decal).modulate == source_shadow_color:
				structure_decals += 1
	return battalion_decals > 0 and structure_decals > 0


func _sockets_ring_dish_center(command_grid: Control, dish_center_global: Vector2) -> bool:
	# The six empty command sockets ride the palantir dish rim; their
	# capture-measured centers sit 95..135 dock px from the dish center.
	var socket_count := 0
	for child in command_grid.get_children():
		var socket := child as TextureRect
		if socket == null or not String(socket.name).begins_with("RetailEmptySocket"):
			continue
		socket_count += 1
		var distance := (socket.global_position + socket.size * 0.5).distance_to(dish_center_global)
		if distance < 95.0 or distance > 135.0:
			return false
	return socket_count == 6


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_SLICE PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_SLICE FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	print("RETAIL_SLICE_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)

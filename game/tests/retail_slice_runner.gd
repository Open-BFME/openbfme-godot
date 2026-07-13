extends SceneTree
## Deterministic behavior/asset gate for the playable private retail slice.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	root.size = Vector2i(1920, 1080)
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
	_check("initialization_is_bounded", initialization_total_ms >= 0 and initialization_total_ms <= 5000, "%d ms" % initialization_total_ms)
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
		_check("source_objects_exact", int(slice.source_map_data.object_count) == 1526)
		_check("source_waypoints_and_starts_exact", int(slice.source_map_data.waypoint_count) == 14 and int(slice.source_map_data.player_start_count) == 2)
		_check("source_water_geometry_exact", int(slice.source_map_data.standing_water_count) == 4 and int(slice.source_map_data.river_count) == 8)
		_check("source_binary_not_packaged", not bool(slice.source_map_data.source_binary_packaged))
		var player_one := Vector3(slice.source_map_data.local_player_starts.get("Player_1_Start", Vector3.ZERO))
		var player_two := Vector3(slice.source_map_data.local_player_starts.get("Player_2_Start", Vector3.ZERO))
		_check("source_start_transform_exact", player_one.is_equal_approx(Vector3(38.0, 0.0, 0.0)) and player_two.is_equal_approx(Vector3(-38.0, 0.0, 0.0)), "%s / %s" % [str(player_one), str(player_two)])
		_check("source_ford_names_drive_gates", _gate_names(slice.source_map_data.ford_gates) == ["ford1", "ford2", "ford3"], str(_gate_names(slice.source_map_data.ford_gates)))
		_check("source_ford_ids_exact", _gate_source_ids(slice.source_map_data.ford_gates) == [38, 43, 46], str(_gate_source_ids(slice.source_map_data.ford_gates)))
		_check("source_object_markers_bounded", slice.source_map_data.generic_prop_placements.size() == 72, str(slice.source_map_data.generic_prop_placements.size()))
		_check("terrain_semantic_ranges_recomputed", int(slice.source_map_data.raw_elevation_min) == 5888 and int(slice.source_map_data.raw_elevation_max) == 11117 and int(slice.source_map_data.computed_raw_elevation_min) == 5888 and int(slice.source_map_data.computed_raw_elevation_max) == 11117)
		_check("passability_full_popcount_recomputed", int(slice.source_map_data.computed_impassable_count) == 18325 and int(slice.source_map_data.computed_impassable_count) == int(slice.source_map_data.impassable_count), str(slice.source_map_data.computed_impassable_count))
		_check("cooked_binary_digests_exact", String(slice.source_map_data.heightmap_sha256).to_upper() == "449D7B4BADA8549B5ED3EC8E908186922D05256D6F728B433018A6F381EDA7FB" and String(slice.source_map_data.passability_sha256).to_upper() == "11E911C6BA50A0D8DCF7FC3A71242B013B5DFDCE1169AE86C939D2DDD5E654B9")
		_check("representative_height_samples_exact", _source_height_samples_match(slice.source_map_data))
		_check("declared_playable_inset_exact", int(slice.source_map_data.border_width) == 20 and Vector2(slice.source_map_data.playable_world_extent).is_equal_approx(Vector2(3750.0, 3130.0)) and Vector2i(slice.source_map_data.playable_grid_min) == Vector2i(20, 20) and Vector2i(slice.source_map_data.playable_grid_max) == Vector2i(395, 333))
		var playable_source_min: Vector2 = slice.source_map_data.local_to_source_horizontal(slice.source_map_data.map_outline[0])
		var playable_source_max: Vector2 = slice.source_map_data.local_to_source_horizontal(slice.source_map_data.map_outline[2])
		_check("playable_polygon_uses_declared_border", playable_source_min.is_equal_approx(Vector2(200.0, -200.0)) and playable_source_max.is_equal_approx(Vector2(3950.0, -3330.0)), "%s / %s" % [str(playable_source_min), str(playable_source_max)])
		_check("bounded_navigation_built_once", bool(slice.source_map_data.navigation_ready) and int(slice.source_map_data.navigation_build_count) == 1 and int(slice.source_map_data.navigation_walkable_count) > 80000 and int(slice.source_map_data.navigation_water_blocked_count) > 0 and int(slice.source_map_data.navigation_ford_corridor_count) > 0, "walkable=%d water_blocked=%d corridors=%d builds=%d" % [slice.source_map_data.navigation_walkable_count, slice.source_map_data.navigation_water_blocked_count, slice.source_map_data.navigation_ford_corridor_count, slice.source_map_data.navigation_build_count])
		_check("reviewed_ford2_cell_stays_blocked", slice.source_map_data.is_impassable_at(208, 142) and not slice.source_map_data.is_navigation_walkable(Vector2i(208, 142)))
	_check("simulation_uses_source_map_configuration", bool(slice.simulation.source_map_configured))
	var player_centroid := (Vector2(slice.simulation.entity(1)["position"]) + Vector2(slice.simulation.entity(2)["position"])) * 0.5
	var enemy_centroid := (Vector2(slice.simulation.entity(101)["position"]) + Vector2(slice.simulation.entity(102)["position"])) * 0.5
	var source_player_two := Vector3(slice.source_map_data.local_player_starts["Player_2_Start"])
	var source_player_one := Vector3(slice.source_map_data.local_player_starts["Player_1_Start"])
	_check("battalion_spawns_derive_from_source_starts", player_centroid.is_equal_approx(Vector2(source_player_two.x, source_player_two.z)) and enemy_centroid.is_equal_approx(Vector2(source_player_one.x, source_player_one.z)), "%s / %s" % [str(player_centroid), str(enemy_centroid)])
	_check("source_battlefield_built", slice.battlefield != null and bool(slice.battlefield.source_driven))
	if slice.battlefield != null:
		_check("source_height_mesh_bounded", int(slice.battlefield.terrain_vertex_count) == 2385 and int(slice.battlefield.terrain_triangle_count) == 4576, "vertices=%d triangles=%d" % [slice.battlefield.terrain_vertex_count, slice.battlefield.terrain_triangle_count])
		_check("source_passability_colors_terrain", int(slice.battlefield.impassable_vertex_count) > 0 and int(slice.battlefield.impassable_vertex_count) < int(slice.battlefield.terrain_vertex_count), str(slice.battlefield.impassable_vertex_count))
		_check("source_water_mesh_built", int(slice.battlefield.water_surface_count) == 12 and int(slice.battlefield.water_triangle_count) > 50, "surfaces=%d triangles=%d" % [slice.battlefield.water_surface_count, slice.battlefield.water_triangle_count])
		_check("source_ford_markers_built", int(slice.battlefield.ford_marker_count) == 3)
		_check("generic_props_are_bounded_markers", int(slice.battlefield.generic_prop_count) == 72 and slice.battlefield.find_child("SourceVegetationPlacementMarkers", true, false) != null and slice.battlefield.find_child("SourceRockPlacementMarkers", true, false) != null, str(slice.battlefield.generic_prop_count))
	_check("four_battalions", slice.battalion_nodes.size() == 4, str(slice.battalion_nodes.size()))
	_check("ten_home_structures", slice.structure_nodes.size() == 10 and slice.simulation.structure_ids(0).size() == 5 and slice.simulation.structure_ids(1).size() == 5, "%d/%d" % [slice.structure_nodes.size(), slice.simulation.structure_ids().size()])
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
	var group_strip: Control = slice.find_child("ControlGroupStrip", true, false) as Control
	_check("palantir_is_bottom_left", palantir != null and palantir.global_position.x >= 12.0 and palantir.global_position.y > 650.0, str(palantir.global_position if palantir != null else Vector2(-1, -1)))
	_check("palantir_radar_draws_above_opaque_frame", palantir_frame != null and slice.minimap.get_parent() == palantir_frame.get_parent() and slice.minimap.get_index() > palantir_frame.get_index())
	_check("command_panel_attaches_to_palantir", command_panel != null and palantir != null and command_panel.global_position.x >= palantir.global_position.x + 360.0)
	_check("control_group_strip_has_nine_slots", group_strip != null and slice.hud.group_buttons.size() == 9)
	_check("outcome_layer_exists", slice.hud.outcome_layer != null and not slice.hud.outcome_layer.visible)
	_check("audio_controls_exist_in_pause", slice.hud.music_slider != null and slice.hud.voice_slider != null and slice.hud.mute_toggle != null)
	_check("source_mapping_not_preview_texture", String(slice.minimap.mapping_mode) == "source-derived-local-transform" and bool(slice.minimap.source_geometry_loaded) and not bool(slice.minimap.uses_source_preview_as_background))
	_check("snappy_radar_zoom_contract", is_equal_approx(float(slice.minimap.zoom_response_seconds), 0.09) and float(slice.minimap.radar_zoom_target) == 1.0)
	var player_fortress_position := Vector2(slice.simulation.structure(slice.simulation.fortress_id(0)).get("position", Vector2.INF))
	_check("camera_starts_inward_from_player_fortress", slice.camera_focus.distance_to(player_fortress_position) >= 16.0 and slice.camera_focus.distance_to(player_fortress_position) <= 19.0 and is_equal_approx(float(slice.camera_zoom_target), 0.18))
	_check("equipment_proof_loaded", bool(slice.equipment_proof_loaded))

	for id in [1, 2, 101, 102]:
		var battalion = slice.battalion_nodes.get(id)
		_check("battalion_%d_exists" % id, battalion != null)
		if battalion == null:
			continue
		_check("battalion_%d_has_15_retail_glbs" % id, int(battalion.member_count) == 15 and int(battalion.retail_visual_count) == 15, "members=%d retail=%d" % [battalion.member_count, battalion.retail_visual_count])
		_check("battalion_%d_has_15_rigs" % id, int(battalion.rigged_member_count) == 15, str(battalion.rigged_member_count))
		_check("battalion_%d_has_animation_players" % id, int(battalion.animation_player_count) >= 15, str(battalion.animation_player_count))

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
	_check("team_tint_covers_glb_surfaces", exemplar != null and enemy_exemplar != null and int(exemplar.team_tinted_surface_count) == 45 and int(enemy_exemplar.team_tinted_surface_count) == 45, "blue=%s red=%s" % [str(exemplar.team_tinted_surface_count if exemplar != null else -1), str(enemy_exemplar.team_tinted_surface_count if enemy_exemplar != null else -1)])
	_check("team_tint_preserves_textures", blue_materials.size() == 15 and red_materials.size() == 15 and (blue_materials[0] as StandardMaterial3D).albedo_texture != null and (red_materials[0] as StandardMaterial3D).albedo_texture != null, "blue=%d red=%d" % [blue_materials.size(), red_materials.size()])
	if not blue_materials.is_empty() and not red_materials.is_empty():
		var blue_color := (blue_materials[0] as StandardMaterial3D).albedo_color
		var red_color := (red_materials[0] as StandardMaterial3D).albedo_color
		var color_distance := absf(blue_color.r - red_color.r) + absf(blue_color.g - red_color.g) + absf(blue_color.b - red_color.b)
		_check("team_surface_colors_are_distinct", color_distance > 0.2, "%s vs %s" % [str(blue_color), str(red_color)])
		_check("team_material_resources_are_private", blue_materials[0] != red_materials[0] and (blue_materials.size() < 2 or blue_materials[0] != blue_materials[1]))

	_check("adaptive_music_closure", slice.audio_system != null and slice.audio_system.has_complete_audio_closure())
	_check("defeat_music_closure", slice.audio_system != null and slice.audio_system.music_streams.has("defeat"))
	_check("select_voice_closure", slice.audio_system != null and slice.audio_system.count_voice_kind("select") == 10, str(slice.audio_system.count_voice_kind("select") if slice.audio_system != null else -1))
	_check("attack_voice_closure", slice.audio_system != null and slice.audio_system.count_voice_kind("attack") == 15, str(slice.audio_system.count_voice_kind("attack") if slice.audio_system != null else -1))
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
	_check("selected_route_draws_order_line_and_flag", order_indicator != null and bool(order_indicator.showing_order) and not order_indicator.route_points.is_empty())
	var assigned_ids: Array[int] = [2, 1]
	var assigned_group: Dictionary = slice.simulation.assign_control_group(1, assigned_ids)
	_check("retail_control_group_assigns_sorted", bool(assigned_group.get("ok", false)) and Array(assigned_group.get("entity_ids", [])) == [1, 2])
	_check("retail_control_group_recall", slice.simulation.recall_control_group(1) == [1, 2])

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

	# Run a complete battle through public commands, capturing actual animation
	# presentation states at attack/death and deterministic audio transitions.
	slice.reset_match()
	slice.simulation.ai_enabled = false
	_check("battle_select_one", bool(slice.test_select(1)))
	_check("battle_multi_select_two", bool(slice.simulation.toggle_selection(2)))
	_check("attack_order_one", int(slice.test_attack(101)) == 2)
	var saw_attack := _advance_until(slice, func(): return _any_state(slice.simulation, 0, "attack"), 400)
	_check("attack_state_reached", saw_attack)
	if saw_attack:
		slice._sync_presentation()
		var attacking_id := _first_state_id(slice.simulation, 0, "attack")
		var attacking_node = slice.battalion_nodes.get(attacking_id)
		_check("attack_drives_imported_clip", attacking_node != null and String(attacking_node.current_clip) == "gumanmocap_atka", String(attacking_node.current_clip if attacking_node != null else "missing"))
	var first_defeated := _advance_until(slice, func(): return int(slice.simulation.entity(101)["health"]) == 0, 120)
	_check("first_enemy_defeated", first_defeated)
	if first_defeated:
		slice._sync_presentation()
		var defeated_node = slice.battalion_nodes.get(101)
		_check("death_drives_imported_clip", defeated_node != null and String(defeated_node.current_state) == "death" and String(defeated_node.current_clip) == "gumanmocap_dieb", "%s/%s" % [defeated_node.current_state, defeated_node.current_clip] if defeated_node != null else "missing")
		_check("death_variants_active", defeated_node != null and defeated_node.active_clip_variants().size() == 4, str(defeated_node.active_clip_variants() if defeated_node != null else []))
		_check("defeated_markers_hidden", defeated_node != null and not bool(defeated_node.markers_visible()))
	_check("attack_order_two", int(slice.test_attack(102)) >= 1)
	var second_defeated := _advance_until(slice, func(): return int(slice.simulation.entity(102)["health"]) == 0, 600)
	_check("second_enemy_defeated", second_defeated)
	var enemy_fortress: int = slice.simulation.fortress_id(1)
	_check("enemy_fortress_attack_order", enemy_fortress != 0 and int(slice.test_attack(enemy_fortress)) >= 1)
	var battle_finished := _advance_until(slice, func(): return int(slice.simulation.winner) != -1, 1600)
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
	var defeat_finished := _advance_until(slice, func(): return int(slice.simulation.winner) != -1, 2400)
	slice._sync_presentation()
	_check("battle_reaches_defeat", defeat_finished and int(slice.simulation.winner) == SimScript.ENEMY_TEAM, "winner=%d tick=%d" % [int(slice.simulation.winner), int(slice.simulation.tick_index)])
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


func _run_reference_battle(simulation, map_configuration: Dictionary, gameplay_rules: Dictionary) -> String:
	simulation.setup(map_configuration, gameplay_rules)
	simulation.ai_enabled = false
	simulation.select_only(1)
	simulation.toggle_selection(2)
	simulation.issue_attack(simulation.selected_ids.duplicate(), 101)
	for _index in range(520):
		if int(simulation.entity(101)["health"]) == 0:
			break
		simulation.tick()
	simulation.issue_attack(simulation.selected_ids.duplicate(), 102)
	for _index in range(620):
		if int(simulation.entity(102)["health"]) == 0:
			break
		simulation.tick()
	simulation.issue_attack(simulation.selected_ids.duplicate(), simulation.fortress_id(1))
	for _index in range(1600):
		if int(simulation.winner) != -1:
			break
		simulation.tick()
	return simulation.state_signature()


func _run_reference_defeat(simulation, map_configuration: Dictionary, gameplay_rules: Dictionary) -> String:
	simulation.setup(map_configuration, gameplay_rules)
	for _index in range(2400):
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

extends SceneTree
## Headless integration proof for the real playable Stage 2 scene.

var passed: int = 0
var failed: int = 0

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var packed := load("res://scenes/stage2_arena.tscn") as PackedScene
	_check("real_stage2_scene_loads", packed != null)
	if packed == null:
		_finish()
		return
	var arena := packed.instantiate()
	arena.process_mode = Node.PROCESS_MODE_DISABLED
	root.add_child(arena)
	current_scene = arena
	var world := arena.get("world") as Stage1World
	var view := arena.get_node_or_null("Stage1View") as Stage2View
	var hud := arena.get_node_or_null("Stage1Hud") as Stage2Hud
	var ghost := arena.get_node_or_null("PlacementGhost") as Stage2PlacementGhost
	_check("stage2_world_ready_without_scripted_commands", world != null and world.stage2_enabled and world.stage2.commands.is_empty())
	_check("stage2_starts_with_both_combat_sides", world != null and world.hordes.size() == 2 and world.fortresses.size() == 2)
	_check("stage2_scene_nodes_ready", view != null and hud != null and ghost != null)
	if world == null or view == null or hud == null or ghost == null:
		arena.queue_free()
		await process_frame
		_finish()
		return

	_test_catalog_and_ghost(arena, world, hud, ghost)
	_test_placement_construction_and_hud(arena, world, view, hud)
	_test_production_population_rally_and_victory(arena, world, view, hud)

	var prior_arena: WeakRef = weakref(arena)
	arena.call("_return_to_menu")
	await process_frame
	await process_frame
	await process_frame
	_check("menu_return_loads_boot_scene", current_scene != null and current_scene.scene_file_path == "res://scenes/boot.tscn")
	_check("menu_return_releases_stage2_scene", prior_arena.get_ref() == null)
	if current_scene != null:
		current_scene.queue_free()
		current_scene = null
	await process_frame
	await process_frame
	_finish()

func _test_catalog_and_ghost(arena: Node, world: Stage1World, hud: Stage2Hud, ghost: Stage2PlacementGhost) -> void:
	var buildable := 0
	for definition: Dictionary in world.building_definitions():
		if int(definition.get("build_menu_slot", -1)) >= 0:
			buildable += 1
	_check("bundle_catalog_has_three_buildings", buildable == 3)
	_check("bundle_catalog_has_two_battalions", world.blueprint_definitions().size() == 2)
	_check("hud_renders_bundle_build_buttons", hud.build_row.get_child_count() == 3)
	var first_button := hud.build_row.get_child(0) as Button
	first_button.emit_signal("pressed")
	_check("bound_build_button_preserves_type_code", int(arena.get("placement_type_code")) == 2)
	_check("placement_ghost_uses_bundle_footprint", ghost.visible and ghost.footprint() == Vector2i(3, 3))
	ghost.update_preview(Vector3.ZERO, true)
	_check("placement_ghost_has_green_valid_state", ghost.is_valid_preview())
	ghost.update_preview(Vector3.ZERO, false)
	_check("placement_ghost_has_red_invalid_state", not ghost.is_valid_preview())

func _test_placement_construction_and_hud(arena: Node, world: Stage1World, view: Stage2View, hud: Stage2Hud) -> void:
	var farm_position := Stage1Grid.cell_center(Vector2i(17, 23))
	var before_resources := world.get_economy(Stage1Types.Team.BLUE).resources
	var placed := bool(arena.call("request_place_at", 2, farm_position))
	_check("authoritative_snapped_placement_succeeds", placed and farm_position.x % Stage1Grid.CELL_SIZE == Stage1Grid.CELL_SIZE / 2 and farm_position.y % Stage1Grid.CELL_SIZE == Stage1Grid.CELL_SIZE / 2)
	_check("placement_spends_bundle_cost_once", world.get_economy(Stage1Types.Team.BLUE).resources == before_resources - 300)
	var overlap_rejected := not bool(arena.call("request_place_at", 2, farm_position))
	_check("authoritative_overlap_is_rejected", overlap_rejected and world.stage2.buildings.size() == 1)
	_check("overlap_feedback_is_explicit", hud.stage2_hint.text.begins_with("Invalid placement"))
	var building_views: Dictionary = view.get("_building_views")
	var farm_view := building_views.get(200) as Node3D
	var farm_body := farm_view.get_node("Body") as MeshInstance3D if farm_view != null else null
	var farm_label := farm_view.get_node("Label") as Label3D if farm_view != null else null
	_check("construction_primitive_is_presented", farm_view != null and farm_body != null and farm_body.scale.y < 1.0)
	_check("construction_label_reports_progress", farm_label != null and farm_label.text.contains("BUILD"))
	world.advance(60)
	arena.call("_refresh_stage2_presentation")
	var farm := world.get_building(200)
	_check("construction_completes_in_bundle_ticks", farm != null and farm.completed and farm.progress_ticks == 60)
	_check("construction_health_ramp_reaches_max", farm != null and farm.health == farm.maximum_health)
	_check("completed_primitive_and_hud_are_readable", farm_body != null and is_equal_approx(farm_body.scale.y, 1.0) and farm_label.text.contains("READY") and hud.economy_label.text.contains("Supplies"))
	arena.call("_select_building", 200)
	_check("resource_building_does_not_offer_rally", hud.train_row.get_child_count() == 1 and not _row_has_button(hud.train_row, "Set Rally"))

func _test_production_population_rally_and_victory(arena: Node, world: Stage1World, view: Stage2View, hud: Stage2Hud) -> void:
	var producer_position := Stage1Grid.cell_center(Vector2i(17, 38))
	_check("production_building_placement_succeeds", bool(arena.call("request_place_at", 3, producer_position)))
	world.advance(90)
	arena.call("_refresh_stage2_presentation")
	var producer := world.get_building(201)
	_check("producer_constructs_before_training", producer != null and producer.completed)
	arena.call("_select_building", 201)
	var blocked_rally := Stage1Grid.cell_center(Vector2i(32, 20))
	_check("blocked_rally_target_has_feedback", not bool(arena.call("request_rally_at", blocked_rally)) and hud.stage2_hint.text.contains("Invalid rally target"))
	var rally := Stage1Grid.cell_center(Vector2i(28, 30))
	_check("walkable_rally_target_is_accepted", bool(arena.call("request_rally_at", rally)) and producer.has_rally_point and producer.rally_point == rally)
	var rally_views: Dictionary = view.get("_rally_views")
	_check("rally_marker_is_presented", rally_views.has(201))
	var train_button := hud.train_row.get_child(1) as Button
	train_button.emit_signal("pressed")
	_check("bound_train_button_queues_correct_blueprint", producer.jobs.size() == 1 and producer.jobs[0].type_code == 100)
	var queued_all := true
	for _index in 4:
		queued_all = queued_all and bool(arena.call("request_train", 100))
	_check("all_five_training_slots_are_playable", queued_all and producer.jobs.size() == 5)
	var economy := world.get_economy(Stage1Types.Team.BLUE)
	_check("queued_population_is_reserved_and_shown", world.stage2.population_reserved(Stage1Types.Team.BLUE) == 5 and hud.economy_label.text.contains("1 + 5 queued / 6"))
	_check("full_queue_rejection_has_feedback", not bool(arena.call("request_train", 100)) and hud.stage2_hint.text.contains("queue full"))
	world.advance(90)
	arena.call("_refresh_stage2_presentation")
	_check("completed_job_moves_reserved_to_used", world.stage2.population_used(world, Stage1Types.Team.BLUE) == 2 and world.stage2.population_reserved(Stage1Types.Team.BLUE) == 4)
	# Wait for the next deterministic farm payout as the second queued job
	# completes, so the rejection source is population rather than currency.
	world.advance(90)
	arena.call("_refresh_stage2_presentation")
	_check("population_cap_rejection_has_feedback", not bool(arena.call("request_train", 100)) and hud.stage2_hint.text.contains("Population cap"))
	world.advance(270)
	arena.call("_refresh_stage2_presentation")
	_check("five_battalions_spawn_from_queue", world.alive_horde_ids(Stage1Types.Team.BLUE).size() == 6)
	_check("population_reaches_exact_cap", world.stage2.population_used(world, Stage1Types.Team.BLUE) == 6 and world.stage2.population_reserved(Stage1Types.Team.BLUE) == 0 and economy.population_cap == 6)
	var first_trained := world.get_horde(102)
	_check("trained_battalion_moves_toward_rally", first_trained != null and first_trained.anchor.distance_squared_to(producer_position) > Stage1Grid.CELL_SIZE * Stage1Grid.CELL_SIZE)
	var attackers: Array[int] = world.alive_horde_ids(Stage1Types.Team.BLUE)
	world.order_attack(attackers, 2)
	var remaining_ticks := 2400
	while world.winner == Stage1Types.Team.NONE and remaining_ticks > 0:
		world.tick()
		remaining_ticks -= 1
	arena.call("_refresh_stage2_presentation")
	_check("trained_force_has_playable_victory_path", world.winner == Stage1Types.Team.BLUE, "ticks_used=%d" % (2400 - remaining_ticks))

func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("PASS %s%s" % [name, " " + detail if detail != "" else ""])
	else:
		failed += 1
		print("FAIL %s%s" % [name, " " + detail if detail != "" else ""])

func _row_has_button(row: Node, text_value: String) -> bool:
	for child: Node in row.get_children():
		if child is Button and (child as Button).text == text_value:
			return true
	return false

func _finish() -> void:
	if failed == 0:
		print("STAGE2_VISUAL_PROOF PASS assertions=%d" % passed)
		quit(0)
	else:
		print("STAGE2_VISUAL_PROOF FAIL assertions=%d failed=%d" % [passed + failed, failed])
		quit(1)

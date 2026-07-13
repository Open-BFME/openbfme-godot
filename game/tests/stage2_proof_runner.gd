extends SceneTree
## godot --headless --path game -s res://tests/stage2_proof_runner.gd

const Stage1BundleScript = preload("res://src/stage1_sim/stage1_bundle.gd")

var passed: int = 0
var failed: int = 0
var bundle: Dictionary

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	bundle = Stage1BundleScript.load_bundle()
	_test_contract_and_api()
	_test_transactional_placement()
	_test_construction_damage()
	_test_farm_efficiency_income()
	_test_fifo_population()
	_test_destroyed_producer()
	_test_deterministic_spawn_rally()
	_test_bundle_scenario()
	if failed == 0:
		print("STAGE2_GODOT_PROOF PASS assertions=%d" % passed)
		quit(0)
	else:
		print("STAGE2_GODOT_PROOF FAIL assertions=%d failed=%d" % [passed + failed, failed])
		quit(1)

func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("PASS %s%s" % [name, " " + detail if detail != "" else ""])
	else:
		failed += 1
		print("FAIL %s%s" % [name, " " + detail if detail != "" else ""])

func _world() -> Stage1World:
	var world := Stage1World.new(32, 32)
	world.setup_empty(false)
	assert(world.enable_stage2(bundle.economy) == "")
	return world

func _center(x: int, y: int) -> Vector2i:
	return Stage1Grid.cell_center(Vector2i(x, y))

func _test_contract_and_api() -> void:
	_check("stage2_bundle_contract", Stage1BundleScript.verify_stage2_contract(bundle) == "")
	var world := _world()
	_check("stage2_enabled_api", world.stage2_enabled and world.get_economy(Stage1Types.Team.BLUE).resources == 1600)
	_check("stage2_catalog_api", world.building_definitions().size() == 4 and world.blueprint_definitions().size() == 2)
	var snapshot := world.snapshot()
	_check("stage2_snapshot_shape", snapshot.economies.size() == 2 and snapshot.buildings.is_empty())
	var invalid_economy: Dictionary = bundle.economy.duplicate(true)
	var invalid_buildings: Array = invalid_economy.get("buildings", [])
	for raw_building: Variant in invalid_buildings:
		var building: Dictionary = raw_building
		if int(building.get("typeCode", 0)) == 2:
			building["constructionTicks"] = 0
	var invalid_world := Stage1World.new(32, 32)
	invalid_world.setup_empty(false)
	_check("buildable_zero_construction_rejected", invalid_world.enable_stage2(invalid_economy) == "building_construction_ticks_2")
	_check("fortress_zero_construction_accepted", world.stage2.get_definition(1).construction_ticks == 0)

func _test_transactional_placement() -> void:
	var world := _world()
	var blue := world.get_economy(Stage1Types.Team.BLUE)
	world.add_fortress(Stage1Types.Team.BLUE, _center(4, 4))
	var initial_hash := world.state_hash()
	var off_center := world.try_place_building(Stage1Types.Team.BLUE, 2, Vector2i(10000, 10000))
	_check("off_center_placement_rejected", off_center == null and world.state_hash() == initial_hash and blue.resources == 1600)
	world.grid.set_blocked(Vector2i(10, 10))
	var static_blocked := world.try_place_building(Stage1Types.Team.BLUE, 2, _center(10, 10))
	_check("static_blocked_placement_transactional", static_blocked == null and blue.resources == 1600 and world.stage2.next_building_id == 200)
	world.grid.set_blocked(Vector2i(10, 10), false)
	var before_fortress_overlap := world.state_hash()
	var blocked_before_fortress_overlap := world.grid.blocked_cells()
	var fortress_edge_overlap := world.try_place_building(Stage1Types.Team.BLUE, 2, _center(6, 4))
	_check("fortress_full_footprint_edge_overlap_rejected", fortress_edge_overlap == null and not world.can_place_building(Stage1Types.Team.BLUE, 2, _center(6, 4)))
	_check("fortress_overlap_rejection_transactional", world.state_hash() == before_fortress_overlap and blue.resources == 1600 and world.stage2.next_building_id == 200 and world.stage2.buildings.is_empty() and world.grid.blocked_cells() == blocked_before_fortress_overlap)
	var building := world.try_place_building(Stage1Types.Team.BLUE, 2, _center(10, 10))
	_check("placement_debits_and_blocks", building != null and building.id == 200 and building.health == 1 and blue.resources == 1300 and world.grid.is_blocked(Vector2i(9, 9)) and world.grid.is_blocked(Vector2i(11, 11)))
	var before_building_overlap := world.state_hash()
	var overlap := world.try_place_building(Stage1Types.Team.BLUE, 2, _center(12, 10))
	_check("overlap_rejected_transactionally", overlap == null and blue.resources == 1300 and world.stage2.next_building_id == 201 and world.state_hash() == before_building_overlap)

func _test_construction_damage() -> void:
	var world := _world()
	var building := world.try_place_building(Stage1Types.Team.BLUE, 2, _center(10, 10))
	world.advance(10)
	_check("construction_ramp_exact_tick_10", building.progress_ticks == 10 and building.construction_health_cap == 200 and building.health == 200)
	world.damage_building(building.id, 50)
	world.advance(10)
	_check("construction_damage_deficit_persists", building.construction_health_cap == 400 and building.health == 350)
	world.advance(40)
	_check("construction_completes_without_healing_damage", building.completed and building.health == 1150 and building.construction_health_cap == 1200 and building.next_income_tick == 149)
	world.damage_building(building.id, 2000)
	_check("destroyed_building_unblocks", building.destroyed and building.health == 0 and not world.grid.is_blocked(Vector2i(10, 10)) and world.validate_state() == "")

func _test_farm_efficiency_income() -> void:
	var world := _world()
	var blue := world.get_economy(Stage1Types.Team.BLUE)
	var first := world.try_place_building(Stage1Types.Team.BLUE, 2, _center(10, 10))
	var second := world.try_place_building(Stage1Types.Team.BLUE, 2, _center(18, 10))
	world.advance(60)
	_check("clustered_farms_complete", first.completed and second.completed and first.next_income_tick == 149 and second.next_income_tick == 149)
	_check("farm_efficiency_neighbor_penalty", world.stage2.farm_efficiency(first) == 800 and world.stage2.farm_efficiency(second) == 800)
	var after_costs := blue.resources
	world.advance(89)
	_check("farm_income_not_early", blue.resources == after_costs and world.tick_index == 149)
	world.tick()
	_check("farm_income_integer_floor", blue.resources == after_costs + 160 and blue.total_earned == 160)
	world.damage_building(second.id, 2000)
	world.advance(90)
	_check("destroyed_neighbor_restores_efficiency", blue.resources == after_costs + 260 and world.stage2.farm_efficiency(first) == 1000 and blue.total_earned == 260)

func _test_fifo_population() -> void:
	var world := _world()
	var blue := world.get_economy(Stage1Types.Team.BLUE)
	var producer := world.try_place_building(Stage1Types.Team.BLUE, 3, _center(10, 10))
	world.advance(90)
	var queued := true
	for _index in 5:
		queued = queued and world.try_train(producer.id, 100)
	_check("train_queue_immediate_debit_reserve", queued and blue.resources == 200 and world.stage2.population_reserved(Stage1Types.Team.BLUE) == 5 and world.stage2.population_used(world, Stage1Types.Team.BLUE) == 0)
	_check("train_queue_and_population_cap_reject_sixth", not world.try_train(producer.id, 100) and producer.jobs.size() == 5 and blue.resources == 200)
	world.advance(90)
	_check("production_fifo_spawns_head", world.hordes.size() == 1 and world.hordes[0].id == 100 and producer.jobs.size() == 4 and producer.jobs[0].id == 2)
	_check("production_transfers_reserved_population", world.stage2.population_used(world, Stage1Types.Team.BLUE) == 1 and world.stage2.population_reserved(Stage1Types.Team.BLUE) == 4 and world.validate_state() == "")

func _test_destroyed_producer() -> void:
	var world := _world()
	var producer := world.try_place_building(Stage1Types.Team.BLUE, 3, _center(10, 10))
	world.advance(90)
	world.try_train(producer.id, 100)
	world.try_train(producer.id, 100)
	var resources := world.get_economy(Stage1Types.Team.BLUE).resources
	world.damage_building(producer.id, 4000)
	_check("destroyed_producer_releases_queue", producer.destroyed and producer.jobs.is_empty() and world.stage2.population_reserved(Stage1Types.Team.BLUE) == 0)
	_check("destroyed_producer_no_refund_and_unblocks", world.get_economy(Stage1Types.Team.BLUE).resources == resources and not world.grid.is_blocked(Vector2i(10, 10)) and world.validate_state() == "")

func _test_deterministic_spawn_rally() -> void:
	var first := _run_rally_case()
	var second := _run_rally_case()
	var horde: Stage1Types.Horde = first.hordes[0]
	_check("spawn_search_and_rally_fallback", horde.id == 100 and horde.destination == _center(20, 19) and horde.order == Stage1Types.OrderKind.MOVE)
	_check("spawn_rally_replay_hash", first.state_hash() == second.state_hash(), "hash=%s" % first.state_hash_text())

func _run_rally_case() -> Stage1World:
	var world := _world()
	world.grid.set_blocked(Vector2i(20, 20))
	var producer := world.try_place_building(Stage1Types.Team.BLUE, 3, _center(10, 10))
	world.advance(90)
	world.try_set_rally(producer.id, _center(20, 20))
	world.try_train(producer.id, 100)
	world.advance(90)
	return world

func _test_bundle_scenario() -> void:
	var world := Stage1BundleScript.build_stage2_world(bundle)
	var commands: Array[Dictionary] = []
	for value in bundle.stage2_map.scenario.commands:
		commands.append(value)
	commands.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		if int(a.executeTick) != int(b.executeTick):
			return int(a.executeTick) < int(b.executeTick)
		return int(a.sequence) < int(b.sequence)
	)
	var command_index := 0
	var ticks := int(bundle.stage2_map.scenario.simulationTicks)
	var scheduling_ok := true
	while world.tick_index < ticks:
		while command_index < commands.size() and int(commands[command_index].executeTick) == world.tick_index:
			scheduling_ok = scheduling_ok and Stage1BundleScript.schedule_stage2_command(world, commands[command_index]) == ""
			command_index += 1
		world.tick()
	var blue := world.get_economy(Stage1Types.Team.BLUE)
	var red := world.get_economy(Stage1Types.Team.RED)
	var completed := 0
	for building in world.stage2.buildings:
		if building.completed and not building.destroyed:
			completed += 1
	var living := world.living_member_count(Stage1Types.Team.BLUE) + world.living_member_count(Stage1Types.Team.RED)
	var valid := world.validate_state() == ""
	var passed_bundle := scheduling_ok and command_index == commands.size() and world.stage2.buildings.size() == 3 and completed == 3 and world.hordes.size() == 7 and living == 105 and blue.resources == 1000 and red.resources == 0 and blue.total_earned == 1600 and red.total_earned == 0 and world.stage2.population_used(world, Stage1Types.Team.BLUE) == 6 and world.stage2.population_reserved(Stage1Types.Team.BLUE) == 0 and world.winner == Stage1Types.Team.BLUE and world.fortress_for(Stage1Types.Team.RED).health == 0 and valid and world.state_hash_text() == "2D9E7B79"
	_check("bundle_stage2_cross_language_outcome", passed_bundle, "hash=%s" % world.state_hash_text())
	print("BUNDLE_STAGE2_GODOT ticks=%d buildings=%d completed_buildings=%d hordes=%d living=%d blue_resources=%d red_resources=%d blue_total_earned=%d red_total_earned=%d blue_population_used=%d blue_population_reserved=%d winner=%d red_fortress_hp=%d state_valid=%d failure=%s hash=%s status=%s" % [ticks, world.stage2.buildings.size(), completed, world.hordes.size(), living, blue.resources, red.resources, blue.total_earned, red.total_earned, world.stage2.population_used(world, Stage1Types.Team.BLUE), world.stage2.population_reserved(Stage1Types.Team.BLUE), world.winner, world.fortress_for(Stage1Types.Team.RED).health, int(valid), world.validate_state() if not valid else "none", world.state_hash_text(), "PASS" if passed_bundle else "FAIL"])

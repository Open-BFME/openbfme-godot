extends SceneTree
## godot --headless --path game -s res://tests/stage1_proof_runner.gd

const Stage1BundleScript = preload("res://src/stage1_sim/stage1_bundle.gd")

var passed: int = 0
var failed: int = 0

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	_test_grid_detour()
	_test_orders()
	_test_exact_destination_and_replan()
	_test_order_safety_and_contacts()
	_test_twenty_v_twenty()
	_test_mirrored_combat()
	_test_projectiles()
	_test_fortress_victory()
	_test_repeat_hash()
	_test_render_rate_independence()
	_test_fifty_horde_budget()
	_test_bundle_contract()
	if failed == 0:
		print("STAGE1_GODOT_PROOF PASS assertions=%d" % passed)
		quit(0)
	else:
		print("STAGE1_GODOT_PROOF FAIL assertions=%d failed=%d" % [passed + failed, failed])
		quit(1)

func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("PASS %s%s" % [name, " " + detail if detail != "" else ""])
	else:
		failed += 1
		print("FAIL %s%s" % [name, " " + detail if detail != "" else ""])

func _test_grid_detour() -> void:
	var world := Stage1World.new()
	world.setup_empty(true)
	var start := Stage1Grid.cell_center(Vector2i(8, 8))
	var destination := Stage1Grid.cell_center(Vector2i(40, 8))
	var path := world.grid.find_path(start, destination)
	var uses_crossing := false
	var all_walkable := not path.is_empty()
	for cell in path:
		uses_crossing = uses_crossing or (cell.x >= 23 and cell.x <= 24 and cell.y >= 12 and cell.y <= 19)
		all_walkable = all_walkable and not world.grid.is_blocked(cell)
	_check("grid_path_exists", not path.is_empty(), "cells=%d" % path.size())
	_check("grid_path_uses_declared_crossing", uses_crossing)
	_check("grid_path_never_enters_blocker", all_walkable)
	var horde := world.add_horde(Stage1Types.Team.BLUE, start, 15, 5)
	world.order_move([horde.id], destination)
	var maximum_y := horde.anchor.y
	for _i in 220:
		world.tick()
		maximum_y = maxi(maximum_y, horde.anchor.y)
	_check("horde_detours_around_obstacle", maximum_y >= 12000, "max_y=%d" % maximum_y)
	_check("horde_reaches_far_side", horde.anchor.x >= 39500, "x=%d" % horde.anchor.x)
	_check("horde_owns_single_path", not horde.path.is_empty() and horde.path_revision == 1)

func _test_orders() -> void:
	var world := Stage1World.new()
	world.setup_empty(false)
	var horde := world.add_horde(Stage1Types.Team.BLUE, Stage1Grid.cell_center(Vector2i(4, 4)), 15, 5)
	var start := horde.anchor
	world.order_move([horde.id], Stage1Grid.cell_center(Vector2i(12, 4)))
	world.advance(12)
	_check("move_command_applies", horde.order == Stage1Types.OrderKind.MOVE and horde.anchor.x > start.x)
	world.order_stop([horde.id])
	world.advance(3)
	var stopped := horde.anchor
	world.advance(8)
	_check("stop_command_holds", horde.order == Stage1Types.OrderKind.STOP and horde.anchor == stopped)
	world.order_attack_move([horde.id], Stage1Grid.cell_center(Vector2i(20, 4)))
	world.advance(3)
	_check("attack_move_command_applies", horde.order == Stage1Types.OrderKind.ATTACK_MOVE)
	var enemy := world.add_horde(Stage1Types.Team.RED, Stage1Grid.cell_center(Vector2i(18, 4)), 15, 5)
	world.order_attack([horde.id], enemy.id)
	world.advance(3)
	_check("attack_target_command_applies", horde.order == Stage1Types.OrderKind.ATTACK_TARGET and horde.target_id == enemy.id)

func _test_exact_destination_and_replan() -> void:
	var world := Stage1World.new(20, 12)
	world.setup_empty(false)
	var horde := world.add_horde(Stage1Types.Team.BLUE, Stage1Grid.cell_center(Vector2i(2, 5)), 15)
	var same_cell_destination := horde.anchor + Vector2i(123, 77)
	world.order_move([horde.id], same_cell_destination)
	world.advance(8)
	_check("move_reaches_exact_subcell_destination", horde.anchor == same_cell_destination, "anchor=%s" % horde.anchor)
	var far_destination := Stage1Grid.cell_center(Vector2i(16, 5)) + Vector2i(137, -91)
	world.order_move([horde.id], far_destination)
	world.advance(3)
	var original_revision := horde.path_revision
	world.grid.set_blocked(Vector2i(8, 5))
	world.advance(120)
	_check("blocked_shared_path_replans", horde.path_revision > original_revision, "revision=%d" % horde.path_revision)
	_check("replanned_path_reaches_exact_destination", horde.anchor == far_destination, "anchor=%s" % horde.anchor)

func _test_order_safety_and_contacts() -> void:
	var world := Stage1World.new(16, 12)
	world.setup_empty(false)
	var blue := world.add_horde(Stage1Types.Team.BLUE, Stage1Grid.cell_center(Vector2i(5, 6)), 15, 5)
	var ally := world.add_horde(Stage1Types.Team.BLUE, Stage1Grid.cell_center(Vector2i(7, 6)), 15, 5)
	world.order_attack([blue.id], ally.id)
	world.advance(3)
	_check("friendly_attack_target_rejected", blue.order == Stage1Types.OrderKind.STOP and ally.members[0].health == ally.members[0].max_health)
	var red := world.add_horde(Stage1Types.Team.RED, Stage1Grid.cell_center(Vector2i(6, 6)), 1, 0)
	world.order_attack([blue.id], red.id)
	world.advance(3)
	var contacts := 0
	for member in blue.members:
		if not member.ranged and member.target_id == red.members[0].id:
			contacts += 1
	_check("melee_contact_slots_bounded", contacts > 0 and contacts <= 4, "contacts=%d" % contacts)

func _test_twenty_v_twenty() -> void:
	var world := Stage1World.new(32, 20)
	world.setup_empty(false)
	world.add_fortress(Stage1Types.Team.BLUE, Stage1Grid.cell_center(Vector2i(2, 10)))
	world.add_fortress(Stage1Types.Team.RED, Stage1Grid.cell_center(Vector2i(29, 10)))
	var blue_a := world.add_horde(Stage1Types.Team.BLUE, Stage1Grid.cell_center(Vector2i(10, 8)), 10, 5)
	var blue_b := world.add_horde(Stage1Types.Team.BLUE, Stage1Grid.cell_center(Vector2i(10, 12)), 10, 5)
	var red_a := world.add_horde(Stage1Types.Team.RED, Stage1Grid.cell_center(Vector2i(21, 8)), 10, 5)
	var red_b := world.add_horde(Stage1Types.Team.RED, Stage1Grid.cell_center(Vector2i(21, 12)), 10, 5)
	world.order_attack_move([blue_a.id, blue_b.id], Stage1Grid.cell_center(Vector2i(28, 10)))
	world.order_attack_move([red_a.id, red_b.id], Stage1Grid.cell_center(Vector2i(3, 10)))
	var projectile_seen := false
	for _i in 800:
		world.tick()
		projectile_seen = projectile_seen or not world.projectiles.is_empty()
		if world.living_member_count(Stage1Types.Team.BLUE) == 0 or world.living_member_count(Stage1Types.Team.RED) == 0:
			break
	var blue_alive := world.living_member_count(Stage1Types.Team.BLUE)
	var red_alive := world.living_member_count(Stage1Types.Team.RED)
	_check("twenty_v_twenty_resolves_casualties", blue_alive < 20 or red_alive < 20, "blue=%d red=%d" % [blue_alive, red_alive])
	_check("twenty_v_twenty_state_valid", world.validate_state() == "", world.validate_state())
	_check("twenty_v_twenty_ranged_path_active", projectile_seen)

func _test_mirrored_combat() -> void:
	var first := _create_mirror_world(false)
	var second := _create_mirror_world(true)
	first.advance(600)
	second.advance(600)
	var first_blue := first.living_member_count(Stage1Types.Team.BLUE)
	var first_red := first.living_member_count(Stage1Types.Team.RED)
	var second_blue := second.living_member_count(Stage1Types.Team.BLUE)
	var second_red := second.living_member_count(Stage1Types.Team.RED)
	_check("combat_mirrors_team_creation_order", first_blue == second_red and first_red == second_blue, "a=%d:%d b=%d:%d" % [first_blue, first_red, second_blue, second_red])

func _create_mirror_world(reverse_teams: bool) -> Stage1World:
	var world := Stage1World.new(24, 16)
	world.setup_empty(false)
	var left_team := Stage1Types.Team.RED if reverse_teams else Stage1Types.Team.BLUE
	var right_team := Stage1Types.Team.BLUE if reverse_teams else Stage1Types.Team.RED
	var left := world.add_horde(left_team, Stage1Grid.cell_center(Vector2i(6, 8)), 20, 5)
	var right := world.add_horde(right_team, Stage1Grid.cell_center(Vector2i(18, 8)), 20, 5)
	world.order_attack_move([left.id], right.anchor)
	world.order_attack_move([right.id], left.anchor)
	return world

func _test_projectiles() -> void:
	var world := Stage1World.new(24, 16)
	world.setup_empty(false)
	var blue := world.add_horde(Stage1Types.Team.BLUE, Stage1Grid.cell_center(Vector2i(8, 8)), 5, 1)
	var red := world.add_horde(Stage1Types.Team.RED, Stage1Grid.cell_center(Vector2i(12, 8)), 5, 0)
	world.order_attack([blue.id], red.id)
	var seen := false
	var start_health := red.members[0].health
	for _i in 80:
		world.tick()
		seen = seen or not world.projectiles.is_empty()
	_check("projectile_spawns", seen)
	_check("projectile_delivers_damage", red.members[0].health < start_health or not red.members[0].is_alive())

func _test_fortress_victory() -> void:
	var world := Stage1World.new(24, 16)
	world.setup_empty(false)
	world.add_fortress(Stage1Types.Team.BLUE, Stage1Grid.cell_center(Vector2i(2, 8)), 1200)
	var red_fort := world.add_fortress(Stage1Types.Team.RED, Stage1Grid.cell_center(Vector2i(18, 8)), 600)
	var blue := world.add_horde(Stage1Types.Team.BLUE, Stage1Grid.cell_center(Vector2i(14, 8)), 15, 5)
	world.order_attack([blue.id], red_fort.id)
	for _i in 600:
		world.tick()
		if world.winner != Stage1Types.Team.NONE:
			break
	_check("fortress_can_be_destroyed", not red_fort.is_alive(), "hp=%d" % red_fort.health)
	_check("fortress_destruction_declares_victory", world.winner == Stage1Types.Team.BLUE)

func _test_repeat_hash() -> void:
	var first := Stage1World.new()
	var second := Stage1World.new()
	first.setup_demo(4, 15)
	second.setup_demo(4, 15)
	first.advance(360)
	second.advance(360)
	_check("repeat_run_hash_equal", first.state_hash() == second.state_hash(), "%s" % first.state_hash_text())
	_check("repeat_run_state_valid", first.validate_state() == "" and second.validate_state() == "")

func _test_render_rate_independence() -> void:
	var expected_hash := -1
	var rates := [60, 120, 144, 240]
	var all_equal := true
	for rate in rates:
		var world := Stage1World.new()
		world.setup_demo(3, 15)
		var accumulator := 0
		for _frame in rate * 12:
			accumulator += Stage1World.TICKS_PER_SECOND
			while accumulator >= rate:
				world.tick()
				accumulator -= rate
		if expected_hash < 0:
			expected_hash = world.state_hash()
		else:
			all_equal = all_equal and world.state_hash() == expected_hash
	_check("render_rate_independent_hash", all_equal, "hash=%08X" % expected_hash)

func _test_fifty_horde_budget() -> void:
	var world := Stage1World.new(64, 40)
	world.setup_empty(false)
	var blue_ids: Array[int] = []
	var red_ids: Array[int] = []
	for i in 25:
		var y := 2 + i % 36
		blue_ids.append(world.add_horde(Stage1Types.Team.BLUE, Stage1Grid.cell_center(Vector2i(5 + i % 4, y)), 15, 5).id)
		red_ids.append(world.add_horde(Stage1Types.Team.RED, Stage1Grid.cell_center(Vector2i(58 - i % 4, 39 - y)), 15, 5).id)
	world.order_move(blue_ids, Stage1Grid.cell_center(Vector2i(46, 20)))
	world.order_move(red_ids, Stage1Grid.cell_center(Vector2i(17, 20)))
	var start_usec := Time.get_ticks_usec()
	world.advance(180)
	var elapsed_usec := maxi(1, Time.get_ticks_usec() - start_usec)
	var ticks_per_second := 180.0 * 1000000.0 / float(elapsed_usec)
	_check("fifty_hordes_meet_30_tick_budget", ticks_per_second >= 30.0, "ticks_per_second=%.1f" % ticks_per_second)
	_check("fifty_hordes_750_members", world.living_member_count(Stage1Types.Team.BLUE) + world.living_member_count(Stage1Types.Team.RED) == 750)
	_check("fifty_hordes_state_valid", world.validate_state() == "", world.validate_state())

func _test_bundle_contract() -> void:
	var bundle: Dictionary = Stage1BundleScript.load_bundle()
	var contract_failure: String = Stage1BundleScript.verify_contract(bundle)
	_check("bundle_contract_matches_gdscript", contract_failure == "", contract_failure)
	if contract_failure != "":
		return
	var world: Stage1World = Stage1BundleScript.build_world(bundle)
	var map: Dictionary = bundle.map
	var ticks := int(map.scenario.simulationTicks)
	world.advance(ticks)
	var valid: bool = world.validate_state() == ""
	_check("bundle_scenario_state_valid", valid, world.validate_state())
	_check("bundle_scenario_entities", world.hordes.size() == 2 and world.hordes[0].members.size() + world.hordes[1].members.size() == 30)
	var red_fortress := world.fortress_for(Stage1Types.Team.RED)
	_check("bundle_scenario_blue_victory", world.winner == Stage1Types.Team.BLUE and red_fortress != null and red_fortress.health == 0)
	print("BUNDLE_GODOT ticks=%d hordes=%d members=30 living=%d winner=%d blue_fortress_hp=%d red_fortress_hp=%d state_valid=%d hash=%s status=%s" % [
		ticks,
		world.hordes.size(),
		world.living_member_count(Stage1Types.Team.BLUE) + world.living_member_count(Stage1Types.Team.RED),
		world.winner,
		world.fortress_for(Stage1Types.Team.BLUE).health,
		red_fortress.health,
		1 if valid else 0,
		world.state_hash_text(),
		"PASS" if valid else "FAIL",
	])

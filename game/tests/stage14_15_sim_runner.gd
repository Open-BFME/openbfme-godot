extends SceneTree
## Focused production-loop gate; legal-safe fallback coordinates only.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")

var passed := 0
var failed := 0


func _initialize() -> void:
	var world = SimScript.new()
	var rules := _rules()
	world.setup({}, rules)
	world.ai_enabled = false
	_check(world.base_loop_enabled, "base_loop_enabled")
	_check(world.structure_ids().size() == 10, "five_structures_per_team")
	for team in [0, 1]:
		var kinds: Array[String] = []
		for id in world.structure_ids(team):
			kinds.append(String(world.structure(id).get("structure_kind", "")))
		kinds.sort()
		var expected: Array[String] = ["archery_range", "barracks", "farm", "fortress", "stable"]
		_check(kinds == expected, "team_%d_structure_roster" % team)
	_check(world.fortress_id(0) != 0 and world.fortress_id(1) != 0, "both_home_fortresses")
	_check(world.producer_id(0) != 0 and world.producer_id(1) != 0, "both_barracks")
	_check(world.resources_for_team(0) == 1200 and world.resources_for_team(1) == 1200, "symmetric_starting_resources")

	var player_barracks: int = world.producer_id(0)
	var enemy_barracks: int = world.producer_id(1)
	var wrong_owner: Dictionary = world.queue_unit(0, enemy_barracks)
	_check(not bool(wrong_owner.get("ok", true)) and String(wrong_owner.get("reason", "")) == "wrong-owner", "queue_rejects_wrong_owner")
	var queued: Dictionary = world.queue_unit(0, player_barracks)
	_check(bool(queued.get("ok", false)), "queue_accepts_supported_soldier")
	_check(world.resources_for_team(0) == 1000, "queue_charges_cost_once")
	var capped: Dictionary = world.queue_unit(0, player_barracks)
	_check(not bool(capped.get("ok", true)) and String(capped.get("reason", "")) == "command-point-cap", "queue_respects_command_point_cap")
	_check(world.resources_for_team(0) == 1000, "rejected_queue_does_not_charge")
	world.advance(199)
	_check(not world.entities.has(10), "production_not_early")
	world.advance(1)
	_check(world.entities.has(10), "production_completes_exact_tick")
	_check(int(world.entity(10).get("member_count", 0)) == 15, "produced_horde_has_fifteen_members")
	_check(world.command_points_for_team(0) == 180, "production_commits_command_points")
	_check(world.state_snapshot().has("structures") and world.state_snapshot().has("resources"), "snapshot_covers_base_authority")

	var first_signature: String = world.state_signature()
	var replay = SimScript.new()
	replay.setup({}, rules)
	replay.ai_enabled = false
	replay.queue_unit(0, replay.producer_id(0))
	replay.advance(200)
	_check(replay.state_signature() == first_signature, "production_replay_is_deterministic")

	var combat = SimScript.new()
	combat.setup({}, rules)
	combat.ai_enabled = false
	var fortress: int = combat.fortress_id(1)
	var fortress_row: Dictionary = combat.structure(fortress)
	var attacker: Dictionary = combat.entity(1)
	attacker["position"] = Vector2(fortress_row["position"]) + Vector2(8.0, 0.0)
	attacker["destination"] = attacker["position"]
	var starting_health: int = int(fortress_row["health"])
	_check(combat.issue_attack([1], fortress) == 1, "structure_attack_order_accepted")
	combat.advance(4)
	_check(int(fortress_row["health"]) == starting_health, "no_damage_before_source_windup")
	combat.advance(1)
	_check(int(fortress_row["health"]) == starting_health - 600, "impact_on_exact_windup_tick")
	_check(int(attacker.get("attack_sequence", 0)) == 1, "attack_cycle_token_emitted_once")
	combat._apply_structure_damage(1, fortress, 999999)
	combat.tick()
	_check(combat.winner == 0, "enemy_fortress_destruction_wins")
	_check(_has_event(combat.events, "match.victory"), "victory_event_emitted")

	var defeat = SimScript.new()
	defeat.setup({}, rules)
	defeat.ai_enabled = false
	defeat._apply_structure_damage(101, defeat.fortress_id(0), 999999)
	defeat.tick()
	_check(defeat.winner == 1, "player_fortress_destruction_loses")
	_check(_has_event(defeat.events, "match.defeat"), "defeat_event_emitted")

	var ai_world = SimScript.new()
	var ai_rules := rules.duplicate(true)
	ai_rules["ai_queue_interval_ticks"] = 15
	ai_rules["ai_attack_delay_ticks"] = 45
	ai_world.setup({}, ai_rules)
	ai_world.advance(15)
	_check(_has_event(ai_world.events, "production.queued"), "ai_uses_shared_queue_contract")
	_check(ai_world.resources_for_team(1) == 1000, "ai_pays_same_unit_cost")
	_check(not _team_has_target(ai_world, 1), "ai_preparation_window_blocks_early_wave")
	ai_world.advance(30)
	_check(_team_has_target(ai_world, 1), "ai_wave_starts_on_declared_delay")

	world.setup({}, rules)
	_check(not world.entities.has(10) and world.structure_ids().size() == 10, "reset_removes_dynamic_entities")
	print("STAGE 14/15 SIM TESTS: %d passed, %d failed" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _rules() -> Dictionary:
	return {
		"enable_base_loop": true,
		"starting_resources": 1200,
		"command_point_cap": 200,
		"member_health": 200,
		"member_count": 15,
		"battalion_damage": 600,
		"speed_world_per_second": 5.5,
		"attack_period_ticks": 10,
		"pre_attack_ticks": 5,
		"soldier_cost": 200,
		"soldier_build_ticks": 200,
		"soldier_command_points": 60,
		"farm_income": 25,
		"farm_payout_ticks": 50,
		"maximum_queue": 5,
		"ai_queue_interval_ticks": 60,
	}


func _has_event(events: Array[Dictionary], kind: String) -> bool:
	for event in events:
		if String(event.get("kind", "")) == kind:
			return true
	return false


func _team_has_target(world, team: int) -> bool:
	for id in world.living_ids(team):
		if int(world.entity(id).get("target_id", 0)) != 0:
			return true
	return false


func _check(condition: bool, label: String) -> void:
	if condition:
		passed += 1
		print("PASS ", label)
	else:
		failed += 1
		push_error("FAIL %s" % label)

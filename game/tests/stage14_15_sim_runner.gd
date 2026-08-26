extends SceneTree
## Focused production-loop gate; legal-safe fallback coordinates only.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const WatchdogScript = preload("res://tests/runner_watchdog.gd")

var passed := 0
var failed := 0
var _watchdog := WatchdogScript.new()


func _initialize() -> void:
	# Q80/Q81 lesson: a mid-run script error aborts _initialize before quit(),
	# leaving the process alive forever (767 CPU-seconds observed). The
	# watchdog turns that into a loud bounded failure.
	_watchdog.start(self, "STAGE_14_15_RUNNER")
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
	# Q80: with configured unit rules the sim runs the retail MEMBER combat
	# model (the legacy battalion single-hit path was the scalar-only mode).
	# The attacker stands inside its 1.15 attack range; the 600-damage slash
	# volley staggers across the firing window (ticks 6-9) and lands through
	# the fortress's authored 20% slash armor = 120 total — the same contract
	# retail_member_combat_runner pins. The old assertions (raw 600 in one
	# synchronized hit, no armor) were the superseded invented model.
	attacker["position"] = Vector2(fortress_row["position"]) + Vector2(1.0, 0.0)
	attacker["destination"] = attacker["position"]
	var starting_health: int = int(fortress_row["health"])
	_check(combat.issue_attack([1], fortress) == 1, "structure_attack_order_accepted")
	combat.advance(5)
	_check(int(fortress_row["health"]) == starting_health, "no_damage_before_source_windup")
	combat.advance(4)
	_check(int(fortress_row["health"]) == starting_health - 120, "volley_lands_through_fortress_armor_by_cycle_end")
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
	# This roster fields exactly 3 enemy combat battalions (ids 101-103); the
	# default wave size of 4 could never muster, so the declared-delay wave
	# contract is asserted at this fixture's full-muster size.
	ai_rules["ai_wave_size"] = 3
	ai_world.setup({}, ai_rules)
	ai_world.advance(15)
	_check(_has_event(ai_world.events, "production.queued"), "ai_uses_shared_queue_contract")
	_check(ai_world.resources_for_team(1) == 1000, "ai_pays_same_unit_cost")
	_check(not _team_wave_started(ai_world, 1), "ai_preparation_window_blocks_early_wave")
	ai_world.advance(30)
	# "Wave starts" = battalions are MARKED into the wave and marching. Target
	# acquisition is vision-gated now (strategic advance gains no combat lock
	# through unexplored distance), so target_id is no longer the wave signal.
	_check(_team_wave_started(ai_world, 1), "ai_wave_starts_on_declared_delay")

	world.setup({}, rules)
	_check(not world.entities.has(10) and world.structure_ids().size() == 10, "reset_removes_dynamic_entities")
	print("STAGE 14/15 SIM TESTS: %d passed, %d failed" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _rules() -> Dictionary:
	# Q80: the manifest's spawn roster demands configured unit rules (the
	# coherence validator refuses the old scalar-only legacy mode). Encode
	# this fixture's historical scalars into explicit per-unit rules so the
	# same behavior is configured the loud way.
	return {"faction_manifest": preload("res://src/retail_slice/retail_faction_manifest.gd").default_manifest(),
		# No BUILDER rule: the historical fixture never fielded a porter (its
		# roster entry requires a unit rule), and giving it one makes the AI
		# open with farm construction, shifting the queue/wave tick contracts.
		"unit_rules": {
			SimScript.SOLDIER_OBJECT_ID: _unit_rule(SimScript.SOLDIER_HORDE_ID, false),
			SimScript.ARCHER_OBJECT_ID: _unit_rule(SimScript.ARCHER_OBJECT_ID, false),
			SimScript.TOWER_GUARD_OBJECT_ID: _unit_rule(SimScript.TOWER_GUARD_OBJECT_ID, false),
			SimScript.KNIGHT_OBJECT_ID: _unit_rule(SimScript.KNIGHT_OBJECT_ID, false),
		},
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


func _unit_rule(horde_id: String, is_builder: bool) -> Dictionary:
	# Movement/range/production fields only. The member-combat timing fields
	# (delay_between_shots_ms / firing windows / member_damage) are omitted on
	# purpose: their presence flips the sim into the member-level combat
	# model, and this gate's contracts are the legacy battalion-level timings
	# driven by the fixture scalars.
	return {
		"horde_id": horde_id,
		"speed": 0.55,
		"speed_source": 5.5,
		"acceleration": 1.0,
		"acceleration_source": 10.0,
		"turn_rate_degrees_per_second": 180.0,
		"braking": 1.0,
		"braking_source": 10.0,
		"attack_range": 1.15,
		"attack_range_source": 11.5,
		"minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0,
		"vision_range": 40.0,
		"vision_range_source": 400.0,
		"delay_between_shots_ms": 1000.0,
		"pre_attack_delay_ms": 500.0,
		"firing_duration_ms": 500.0,
		"attack_period_ticks": 10,
		"pre_attack_ticks": 5,
		"firing_duration_ticks": 5,
		"member_damage": 40,
		"member_health": 200,
		"member_count": 15,
		"formation_positions": _formation_positions(15),
		"provenance": {},
		"is_builder": is_builder,
	}


func _formation_positions(member_count: int) -> Array[Vector3]:
	# One slot per member — a single shared slot leaves members without a
	# formation seat and their weapons never release.
	var positions: Array[Vector3] = []
	for index in range(member_count):
		positions.append(Vector3(float(index), 0.0, 0.0))
	return positions


func _has_event(events: Array[Dictionary], kind: String) -> bool:
	for event in events:
		if String(event.get("kind", "")) == kind:
			return true
	return false


func _team_wave_started(world, team: int) -> bool:
	for id in world.living_ids(team):
		if bool(world.entity(id).get("ai_in_wave", false)):
			return true
	return false


func _check(condition: bool, label: String) -> void:
	if condition:
		passed += 1
		print("PASS ", label)
	else:
		failed += 1
		push_error("FAIL %s" % label)

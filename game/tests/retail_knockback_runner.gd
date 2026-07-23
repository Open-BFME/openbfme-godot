extends SceneTree

## Knockback / trample / flyer lane proof harness.
##
## Deterministic fixture sims (lockstep-runner style: harness rules injected
## before the setup({}, {}) contract) exercise the knockback core, the cavalry
## trample knockdown upgrade, the data-driven hero blast shockwave (synthetic
## ability rule — no compiled Men ability authors knockback magnitudes yet),
## and tier-1 flyers (ground-navigation immunity, melee untouchability).

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")

var passed := 0
var failed := 0


class WaterBandRouteProvider extends RefCounted:
	## Minimal deterministic ground navigation stub: a vertical water band at
	## x in [5, 10) that ground routes refuse to cross (no ford authored).
	const WATER_MIN_X := 5.0
	const WATER_MAX_X := 10.0

	func is_local_inside_navigation(_position: Vector2) -> bool:
		return true

	func local_to_grid_cell(position: Vector2) -> Vector2i:
		return Vector2i(int(floor(position.x)), int(floor(position.y)))

	func is_navigation_walkable(cell: Vector2i) -> bool:
		return cell.x < int(WATER_MIN_X) or cell.x >= int(WATER_MAX_X)

	func query_route(from: Vector2, to: Vector2) -> Dictionary:
		var from_side := from.x < WATER_MIN_X
		var to_side := to.x < WATER_MAX_X and to.x >= WATER_MIN_X
		if to_side:
			return {"valid": false, "reason": "destination-in-water", "points": [], "cells": [], "ford_name": ""}
		if from_side != (to.x < WATER_MIN_X):
			return {"valid": false, "reason": "water-blocks-route", "points": [], "cells": [], "ford_name": ""}
		var points: Array[Vector2] = []
		points.append(to)
		return {"valid": true, "reason": "", "points": points, "cells": [], "ford_name": ""}


func _initialize() -> void:
	call_deferred("_run")


func _harness_rules() -> Dictionary:
	return {
		"enable_base_loop": true,
		"starting_resources": 10000,
		"ai_attack_delay_ticks": 4000,
		"unit_rules": {
			SimScript.SOLDIER_OBJECT_ID: _unit_rule({}),
			SimScript.ARCHER_OBJECT_ID: _unit_rule({}),
			SimScript.TOWER_GUARD_OBJECT_ID: _unit_rule({}),
			SimScript.KNIGHT_OBJECT_ID: _unit_rule({}),
			SimScript.BUILDER_OBJECT_ID: _unit_rule({"is_builder": true}),
			"test.soldier": _unit_rule({}),
			"test.cavalry": _unit_rule({
				"category": "cavalry",
				"speed": 2.0, "speed_source": 20.0,
				"acceleration": 2.0, "acceleration_source": 20.0,
				"braking": 2.0, "braking_source": 20.0,
				"member_damage": 20,
			}),
			"test.flyer": _unit_rule({
				"is_flyer": true,
				"speed": 2.0, "speed_source": 20.0,
				"acceleration": 2.0, "acceleration_source": 20.0,
				"braking": 2.0, "braking_source": 20.0,
			}),
			"test.archer": _unit_rule({
				"attack_range": 30.0, "attack_range_source": 300.0,
			}),
			"test.hero": _unit_rule({"category": "hero"}),
		},
	}


func _unit_rule(overrides: Dictionary) -> Dictionary:
	var rule := {
		"horde_id": SimScript.SOLDIER_HORDE_ID,
		"speed": 1.0,
		"speed_source": 10.0,
		"acceleration": 1.0,
		"acceleration_source": 10.0,
		"turn_rate_degrees_per_second": 180.0,
		"braking": 1.0,
		"braking_source": 10.0,
		"attack_range": 1.15,
		"attack_range_source": 11.5,
		"minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0,
		"vision_range": 12.0,
		"vision_range_source": 120.0,
		"delay_between_shots_ms": 600.0,
		"pre_attack_delay_ms": 200.0,
		"firing_duration_ms": 200.0,
		"attack_period_ticks": 10,
		"pre_attack_ticks": 2,
		"firing_duration_ticks": 2,
		"member_damage": 10,
		"member_health": 200,
		"member_count": 1,
		"formation_positions": [Vector3.ZERO],
		"provenance": {},
		"is_builder": false,
	}
	for key in overrides.keys():
		rule[key] = overrides[key]
	return rule


func _blast_rule() -> Dictionary:
	## Synthetic weapon-blast ability carrying the knockback rule keys.
	## No compiled Men ability authors knockback magnitudes today (the
	## converter does not extract MetaImpactNugget shockwave data yet), so the
	## mechanic is proven behind the rule keys with this fixture; importer-side
	## extraction of retail magnitudes is recorded follow-up.
	return {
		"ability_id": "test.blast",
		"slot": 1,
		"special_power_id": "TestSyntheticBlast",
		"targeting": "point",
		"cooldown_ticks": 50,
		"required_level": 1,
		"level_gate_resolved": true,
		"castable": true,
		"availability_reason": "",
		"limitations": [],
		"effect": {
			"kind": "weapon-blast",
			"damage": 60,
			"damage_radius": 10.0,
			"range": 50.0,
			"knockback_radius": 8.0,
			"knockback_strength": 3.0,
		},
	}


func _make_sim(with_water := false):
	var sim = SimScript.new()
	sim._rules = _harness_rules()
	sim.setup({}, {})
	sim.ai_enabled = false
	for structure_id in sim.structure_ids():
		sim.structures.erase(structure_id)
	for entity_id in sim.entity_ids():
		sim.entities.erase(entity_id)
	sim.expansion_pads.clear()
	sim._ai_production_plan.clear()
	sim._ai_build_order_index = SimScript.AI_BUILD_ORDER.size()
	sim._enemy_ai_construction_attempted = true
	sim._enemy_ai_construction_resolved = true
	if with_water:
		sim.route_provider = WaterBandRouteProvider.new()
	sim._unit_ability_rules["test.hero-horde"] = [_blast_rule()]
	return sim


func _spawn(sim, id: int, team: int, at: Vector2, object_id: String, unit_type: String) -> Dictionary:
	sim._add_battalion(id, team, at, "T%d" % id, object_id, unit_type)
	return sim.entities.get(id, {})


func _ids(values: Array) -> Array[int]:
	var output: Array[int] = []
	output.assign(values)
	return output


func _events_of_kind(sim, kind: String) -> Array[Dictionary]:
	var output: Array[Dictionary] = []
	for event_value in sim.events:
		if String((event_value as Dictionary).get("kind", "")) == kind:
			output.append(event_value as Dictionary)
	return output


func _command(tick: int, seq: int, type: String, args: Dictionary, team: int = 0) -> Dictionary:
	return {"tick": tick, "team": team, "seq": seq, "type": type, "args": args}


func _run() -> void:
	_scenario_trample_and_knockdown_lifecycle()
	_scenario_blast_radial_knockback()
	_scenario_water_and_flyers()
	_scenario_melee_vs_flyer()
	_scenario_twin_determinism()
	_scenario_hash_sensitivity()
	print("RETAIL_KNOCKBACK_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _scenario_trample_and_knockdown_lifecycle() -> void:
	var sim = _make_sim()
	var cavalry := _spawn(sim, 11, SimScript.PLAYER_TEAM, Vector2(-20.0, 0.0), "test.cavalry", "test.cavalry-horde")
	var victim := _spawn(sim, 101, SimScript.ENEMY_TEAM, Vector2(-12.0, 0.0), "test.soldier", "test.soldier-horde")
	sim.issue_move(_ids([11]), Vector2(0.0, 0.0))
	var knock_tick := -1
	var previous_position := Vector2(victim["position"])
	for _tick in range(120):
		previous_position = Vector2(victim["position"])
		sim.tick()
		if bool(victim.get("knocked_down", false)):
			knock_tick = sim.tick_index
			break
	var knock_events := _events_of_kind(sim, "combat.knockback")
	var trample_events := _events_of_kind(sim, "combat.trample")
	var displaced := false
	var downed := false
	if knock_tick > 0 and not knock_events.is_empty():
		var event := knock_events[0]
		var center := Vector2(float((event["center"] as Array)[0]), float((event["center"] as Array)[1]))
		var landed := Vector2(float((event["landed"] as Array)[0]), float((event["landed"] as Array)[1]))
		var moved := previous_position.distance_to(Vector2(victim["position"]))
		displaced = (
			moved >= 1.0
			and landed.distance_to(Vector2(victim["position"])) < 0.01
			and landed.distance_to(center) > previous_position.distance_to(center) - 0.001
		)
		downed = int(victim.get("knockdown_ticks", 0)) > 0 and int(victim.get("health", 0)) < 200
	_check(
		"trample_displaces_and_downs_victim",
		knock_tick > 0 and not trample_events.is_empty() and displaced and downed,
		"knock_tick=%d trample_events=%d displaced=%s downed=%s" % [knock_tick, trample_events.size(), displaced, downed]
	)
	if knock_tick <= 0:
		_check("downed_victim_skips_acting", false, "no knockdown observed")
		_check("downed_victim_blocks_move_orders", false, "no knockdown observed")
		_check("victim_stands_up_and_accepts_orders", false, "no knockdown observed")
		_check("snapshot_round_trip_mid_knockdown", false, "no knockdown observed")
		return
	# Freeze further tramples so the lifecycle window stays clean.
	cavalry["trample_cooldown"] = 1000000
	# Snapshot taken mid-knockdown; the restored twin must track the original
	# hash-for-hash through the stand-up and beyond.
	var bytes: PackedByteArray = sim.snapshot()
	var restored = _make_sim()
	var snapshot_ok: bool = restored.restore(bytes) and restored.state_hash() == sim.state_hash()
	var down_position := Vector2(victim["position"])
	var acted_while_down := false
	var blocked_while_down := false
	for step in range(40):
		if step == 2 and int(victim.get("knockdown_ticks", 0)) > 0:
			blocked_while_down = sim.issue_move(_ids([101]), Vector2(-12.0, 4.0), "order.move", SimScript.ENEMY_TEAM) == 0
		sim.tick()
		restored.tick()
		snapshot_ok = snapshot_ok and restored.state_hash() == sim.state_hash()
		if int(victim.get("knockdown_ticks", 0)) > 0:
			if Vector2(victim["position"]).distance_to(down_position) > 0.001 or String(victim.get("state", "")) != "knocked_down":
				acted_while_down = true
	var stood_up: bool = not bool(victim.get("knocked_down", false)) \
		and int(victim.get("knockdown_ticks", 0)) == 0 \
		and not _events_of_kind(sim, "combat.stand_up").is_empty()
	var accepts_after: bool = sim.issue_move(_ids([101]), Vector2(-12.0, 4.0), "order.move", SimScript.ENEMY_TEAM) == 1
	_check("downed_victim_skips_acting", not acted_while_down)
	_check("downed_victim_blocks_move_orders", blocked_while_down)
	_check("victim_stands_up_and_accepts_orders", stood_up and accepts_after, "stood_up=%s accepts=%s" % [stood_up, accepts_after])
	_check("snapshot_round_trip_mid_knockdown", snapshot_ok)


func _scenario_blast_radial_knockback() -> void:
	var sim = _make_sim()
	_spawn(sim, 21, SimScript.PLAYER_TEAM, Vector2(0.0, 0.0), "test.hero", "test.hero-horde")
	var ally := _spawn(sim, 22, SimScript.PLAYER_TEAM, Vector2(3.0, 0.0), "test.soldier", "test.soldier-horde")
	var east := _spawn(sim, 111, SimScript.ENEMY_TEAM, Vector2(6.0, 0.0), "test.soldier", "test.soldier-horde")
	var north := _spawn(sim, 112, SimScript.ENEMY_TEAM, Vector2(0.0, 6.0), "test.soldier", "test.soldier-horde")
	var flyer := _spawn(sim, 113, SimScript.ENEMY_TEAM, Vector2(-6.0, 0.0), "test.flyer", "test.flyer-horde")
	var result: Dictionary = sim.cast_ability(21, "test.blast", Vector2(0.0, 0.0))
	var east_thrown: bool = Vector2(east["position"]).is_equal_approx(Vector2(9.0, 0.0)) \
		and bool(east.get("knocked_down", false)) and int(east.get("health", 0)) == 140
	var north_thrown: bool = Vector2(north["position"]).is_equal_approx(Vector2(0.0, 9.0)) \
		and bool(north.get("knocked_down", false)) and int(north.get("health", 0)) == 140
	_check(
		"blast_knockback_radial_enemies",
		bool(result.get("ok", false)) and east_thrown and north_thrown and _events_of_kind(sim, "combat.knockback").size() == 2,
		"result=%s east=%s north=%s" % [result, east["position"], north["position"]]
	)
	var ally_untouched: bool = Vector2(ally["position"]).is_equal_approx(Vector2(3.0, 0.0)) \
		and not bool(ally.get("knocked_down", false)) and int(ally.get("health", 0)) == 200
	var flyer_immune: bool = Vector2(flyer["position"]).is_equal_approx(Vector2(-6.0, 0.0)) \
		and not bool(flyer.get("knocked_down", false)) and int(flyer.get("knockdown_ticks", 0)) == 0 \
		and int(flyer.get("health", 0)) == 140
	_check("blast_spares_allies_and_flyers", ally_untouched and flyer_immune, "ally=%s flyer=%s" % [ally_untouched, flyer_immune])


func _scenario_water_and_flyers() -> void:
	var sim = _make_sim(true)
	var walker := _spawn(sim, 31, SimScript.PLAYER_TEAM, Vector2(0.0, 0.0), "test.soldier", "test.soldier-horde")
	var flyer := _spawn(sim, 32, SimScript.PLAYER_TEAM, Vector2(0.0, -6.0), "test.flyer", "test.flyer-horde")
	var walker_accepted: int = sim.issue_move(_ids([31]), Vector2(20.0, 0.0))
	var flyer_accepted: int = sim.issue_move(_ids([32]), Vector2(20.0, -6.0))
	for _tick in range(250):
		sim.tick()
	var walker_stayed: bool = Vector2(walker["position"]).is_equal_approx(Vector2(0.0, 0.0))
	var flyer_crossed: bool = Vector2(flyer["position"]).x > 15.0
	_check("ground_unit_refuses_water_crossing", walker_accepted == 0 and walker_stayed, "accepted=%d position=%s" % [walker_accepted, walker["position"]])
	_check("flyer_crosses_water", flyer_accepted == 1 and flyer_crossed, "accepted=%d position=%s" % [flyer_accepted, flyer["position"]])


func _scenario_melee_vs_flyer() -> void:
	var sim = _make_sim()
	var melee := _spawn(sim, 51, SimScript.PLAYER_TEAM, Vector2(0.0, 0.0), "test.soldier", "test.soldier-horde")
	var flyer := _spawn(sim, 151, SimScript.ENEMY_TEAM, Vector2(2.0, 0.0), "test.flyer", "test.flyer-horde")
	_spawn(sim, 52, SimScript.PLAYER_TEAM, Vector2(0.0, 40.0), "test.archer", "test.archer-horde")
	var melee_accepted: int = sim.issue_attack(_ids([51]), 151)
	for _tick in range(30):
		sim.tick()
	var flyer_unhurt: bool = int(flyer.get("health", 0)) == 200
	var melee_never_engaged: bool = int(melee.get("target_id", 0)) == 0
	_check(
		"melee_cannot_engage_flyer",
		melee_accepted == 0 and flyer_unhurt and melee_never_engaged,
		"accepted=%d flyer_health=%d melee_target=%d" % [melee_accepted, int(flyer.get("health", 0)), int(melee.get("target_id", 0))]
	)
	var archer_accepted: int = sim.issue_attack(_ids([52]), 151)
	for _tick in range(200):
		sim.tick()
	_check(
		"archer_hits_flyer",
		archer_accepted == 1 and int(flyer.get("health", 0)) < 200,
		"accepted=%d flyer_health=%d" % [archer_accepted, int(flyer.get("health", 0))]
	)


func _make_battle_sim():
	var sim = _make_sim(true)
	_spawn(sim, 41, SimScript.PLAYER_TEAM, Vector2(-30.0, 0.0), "test.cavalry", "test.cavalry-horde")
	_spawn(sim, 42, SimScript.PLAYER_TEAM, Vector2(-26.0, 4.0), "test.soldier", "test.soldier-horde")
	_spawn(sim, 43, SimScript.PLAYER_TEAM, Vector2(-26.0, -4.0), "test.archer", "test.archer-horde")
	_spawn(sim, 44, SimScript.PLAYER_TEAM, Vector2(-30.0, 8.0), "test.hero", "test.hero-horde")
	_spawn(sim, 45, SimScript.PLAYER_TEAM, Vector2(-2.0, -20.0), "test.flyer", "test.flyer-horde")
	_spawn(sim, 141, SimScript.ENEMY_TEAM, Vector2(-18.0, 0.0), "test.soldier", "test.soldier-horde")
	_spawn(sim, 142, SimScript.ENEMY_TEAM, Vector2(-18.0, 8.0), "test.soldier", "test.soldier-horde")
	_spawn(sim, 143, SimScript.ENEMY_TEAM, Vector2(-18.0, -4.0), "test.flyer", "test.flyer-horde")
	return sim


func _battle_commands() -> Array[Dictionary]:
	return [
		_command(1, 1, "issue_set_stance", {"ids": [141], "stance": "HoldGround"}, SimScript.ENEMY_TEAM),
		_command(2, 2, "issue_move", {"ids": [41], "destination": Vector2(-5.0, -4.0)}),
		_command(3, 3, "issue_move", {"ids": [45], "destination": Vector2(20.0, -20.0)}),
		_command(5, 4, "cast_ability", {"hero_id": 44, "ability_id": "test.blast", "target_point": Vector2(-18.0, 6.0)}),
		_command(8, 5, "issue_attack", {"ids": [43], "target_id": 143}),
	]


func _scenario_twin_determinism() -> void:
	var sim_a = _make_battle_sim()
	var sim_b = _make_battle_sim()
	var submissions_ok := true
	for cmd in _battle_commands():
		submissions_ok = sim_a.submit_command(cmd) and submissions_ok
		submissions_ok = sim_b.submit_command(cmd) and submissions_ok
	var twin_ok := submissions_ok
	var first_divergence := -1
	for expected_tick in range(1, 801):
		sim_a.tick()
		sim_b.tick()
		if sim_a.state_hash() != sim_b.state_hash() and first_divergence < 0:
			first_divergence = expected_tick
			twin_ok = false
	var knockbacks := _events_of_kind(sim_a, "combat.knockback").size()
	var tramples := _events_of_kind(sim_a, "combat.trample").size()
	var stand_ups := _events_of_kind(sim_a, "combat.stand_up").size()
	var flyer_crossed: bool = Vector2((sim_a.entities[45] as Dictionary)["position"]).x > 15.0
	var mechanics_fired: bool = knockbacks >= 2 and tramples >= 1 and stand_ups >= 1 and flyer_crossed
	_check(
		"twin_run_800_tick_hash_equality_with_mechanics",
		twin_ok and mechanics_fired,
		"first_divergence=%d knockbacks=%d tramples=%d stand_ups=%d flyer_crossed=%s" % [first_divergence, knockbacks, tramples, stand_ups, flyer_crossed]
	)


func _scenario_hash_sensitivity() -> void:
	var sim_a = _make_battle_sim()
	var sim_b = _make_battle_sim()
	var baseline_equal: bool = sim_a.state_hash() == sim_b.state_hash()
	var row: Dictionary = sim_b.entities[141]
	row["knockdown_ticks"] = 7
	row["knocked_down"] = true
	_check(
		"hash_sensitive_to_knockdown_ticks",
		baseline_equal and sim_a.state_hash() != sim_b.state_hash(),
		"baseline_equal=%s" % baseline_equal
	)


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_KNOCKBACK PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_KNOCKBACK FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])

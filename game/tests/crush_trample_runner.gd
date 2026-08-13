extends SceneTree
## Authored crush / trample. Retail knights (knightsofdolamroth.ini:494-502)
## crush when CrusherLevel > CrushableLevel and speed >= MinCrushVelocityPercent
## of max. Damage is the CrushWeapon nugget, not member_damage * 0.5.
##
##   DolAmrothLancerCrush = GONDOR_KNIGHTSOFDOL_CRUSH_DAMAGE = 250
##     (weapon.ini:293-311, gamedata.ini:1892)
##   KnightCrush = KNIGHT_CRUSH_DAMAGE = 80 (gamedata.ini:7956)
##   MinCrushVelocityPercent = 40
##
## Cavalry horde locomotors author TurnTime=1000 (360 deg/s) and
## MaxTurnWithoutReform=100 (locomotor.ini:837-847). Infantry stay 2000 / 45.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const AdapterScript = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")

const EXPECTED_CHECKS := 15
const EPSILON := 0.0001

## Authored crush damage. The invented TRAMPLE_DAMAGE_FACTOR=0.5 path yields
## member_damage(20) * member_count(1) * 0.5 = 10, which must not pass.
const DOL_AMROTH_CRUSH_DAMAGE := 250
const KNIGHT_CRUSH_DAMAGE := 80

var passed := 0
var failed := 0

const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "CRUSH_TRAMPLE_RUNNER")
	call_deferred("_run")


func _run() -> void:
	_test_dol_amroth_crush_is_authored_250()
	_test_knight_crush_is_authored_80()
	_test_crush_gates()
	_test_adapter_copies_crush_and_turn_fields()
	_test_simulation_rule_preserves_resolved_crush()
	_test_cavalry_row_receives_authored_turn()
	_finish()


func _harness_rules() -> Dictionary:
	return {
		"enable_base_loop": true,
		"starting_resources": 10000,
		"ai_attack_delay_ticks": 4000,
		"unit_rules": {
			SimScript.SOLDIER_OBJECT_ID: _unit_rule({
				"crushable_level": 0,
			}),
			SimScript.ARCHER_OBJECT_ID: _unit_rule({}),
			SimScript.TOWER_GUARD_OBJECT_ID: _unit_rule({}),
			SimScript.KNIGHT_OBJECT_ID: _unit_rule({
				"category": "cavalry",
				"speed": 2.0, "speed_source": 20.0,
				"acceleration": 2.0, "acceleration_source": 20.0,
				"braking": 2.0, "braking_source": 20.0,
				"member_damage": 20,
				"turn_rate_degrees_per_second": 360.0,
				"max_turn_without_reform_degrees": 100.0,
				"crusher_level": 2,
				"crushable_level": 1,
				"crush_weapon_id": "DolAmrothLancerCrush",
				"crush_damage": DOL_AMROTH_CRUSH_DAMAGE,
				"min_crush_velocity_percent": 40,
				"crush_deceleration_percent": 20,
				"crush_knockback": 10.0,
			}),
			"test.soldier": _unit_rule({
				"crushable_level": 0,
			}),
			"test.cavalry": _unit_rule({
				"category": "cavalry",
				"speed": 2.0, "speed_source": 20.0,
				"acceleration": 2.0, "acceleration_source": 20.0,
				"braking": 2.0, "braking_source": 20.0,
				"member_damage": 20,
				"turn_rate_degrees_per_second": 360.0,
				"max_turn_without_reform_degrees": 100.0,
				"crusher_level": 2,
				"crushable_level": 1,
				"crush_weapon_id": "DolAmrothLancerCrush",
				"crush_damage": DOL_AMROTH_CRUSH_DAMAGE,
				"min_crush_velocity_percent": 40,
				"crush_deceleration_percent": 20,
				"crush_knockback": 10.0,
			}),
			"test.knight-crush": _unit_rule({
				"category": "cavalry",
				"speed": 2.0, "speed_source": 20.0,
				"acceleration": 2.0, "acceleration_source": 20.0,
				"braking": 2.0, "braking_source": 20.0,
				"member_damage": 20,
				"crusher_level": 2,
				"crushable_level": 1,
				"crush_weapon_id": "KnightCrush",
				"crush_damage": KNIGHT_CRUSH_DAMAGE,
				"min_crush_velocity_percent": 40,
				"crush_deceleration_percent": 20,
				"crush_knockback": 10.0,
			}),
			"test.uncrushable": _unit_rule({
				"crushable_level": 2,
			}),
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
		"member_health": 400,
		"member_count": 1,
		"formation_positions": [Vector3.ZERO],
		"provenance": {},
		"is_builder": false,
	}
	for key in overrides.keys():
		rule[key] = overrides[key]
	return rule


func _make_sim():
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
	sim.force_ai_construction_complete()
	return sim


func _spawn(sim, id: int, team: int, at: Vector2, object_id: String, unit_type: String) -> Dictionary:
	sim._add_battalion(id, team, at, "T%d" % id, object_id, unit_type)
	return sim.entities.get(id, {})


func _events_of_kind(sim, kind: String) -> Array[Dictionary]:
	var output: Array[Dictionary] = []
	for event_value in sim.events:
		if String((event_value as Dictionary).get("kind", "")) == kind:
			output.append(event_value as Dictionary)
	return output


func _charge_and_crush(sim, cavalry: Dictionary, _victim: Dictionary) -> void:
	cavalry["current_speed"] = float(cavalry.get("speed", 0.0))
	cavalry["trample_cooldown"] = 0
	sim._try_cavalry_trample(cavalry)


func _test_dol_amroth_crush_is_authored_250() -> void:
	var sim = _make_sim()
	var cavalry := _spawn(sim, 11, SimScript.PLAYER_TEAM, Vector2(0.0, 0.0), "test.cavalry", "test.cavalry-horde")
	var victim := _spawn(sim, 101, SimScript.ENEMY_TEAM, Vector2(1.0, 0.0), "test.soldier", "test.soldier-horde")
	var health_before := int(victim.get("health", 0))
	_charge_and_crush(sim, cavalry, victim)
	var dealt := health_before - int(victim.get("health", 0))
	var crush_events := _events_of_kind(sim, "combat.crush")
	var invented := int(round(float(cavalry.get("member_damage", 20)) * float(cavalry.get("member_count", 1)) * 0.5))
	_check(
		"dol_amroth_crush_damage_is_250",
		dealt == DOL_AMROTH_CRUSH_DAMAGE,
		"dealt=%d invented_half_factor=%d health %d->%d" % [dealt, invented, health_before, int(victim.get("health", -1))]
	)
	_check(
		"crush_does_not_use_invented_half_factor",
		dealt != invented and dealt == DOL_AMROTH_CRUSH_DAMAGE,
		"dealt=%d invented=%d" % [dealt, invented]
	)
	_check(
		"crush_emits_combat_crush",
		crush_events.size() >= 1 and int(crush_events[0].get("amount", crush_events[0].get("data", {}).get("amount", 0))) == DOL_AMROTH_CRUSH_DAMAGE,
		"events=%s" % crush_events
	)


func _test_knight_crush_is_authored_80() -> void:
	var sim = _make_sim()
	var cavalry := _spawn(sim, 12, SimScript.PLAYER_TEAM, Vector2(0.0, 0.0), "test.knight-crush", "test.knight-crush-horde")
	var victim := _spawn(sim, 102, SimScript.ENEMY_TEAM, Vector2(1.0, 0.0), "test.soldier", "test.soldier-horde")
	var health_before := int(victim.get("health", 0))
	_charge_and_crush(sim, cavalry, victim)
	var dealt := health_before - int(victim.get("health", 0))
	_check(
		"knight_crush_damage_is_80",
		dealt == KNIGHT_CRUSH_DAMAGE,
		"dealt=%d health %d->%d" % [dealt, health_before, int(victim.get("health", -1))]
	)


func _test_crush_gates() -> void:
	var sim = _make_sim()
	var cavalry := _spawn(sim, 13, SimScript.PLAYER_TEAM, Vector2(0.0, 0.0), "test.cavalry", "test.cavalry-horde")
	var uncrushable := _spawn(sim, 103, SimScript.ENEMY_TEAM, Vector2(1.0, 0.0), "test.uncrushable", "test.uncrushable-horde")
	var health_before := int(uncrushable.get("health", 0))
	_charge_and_crush(sim, cavalry, uncrushable)
	_check(
		"crush_requires_crusher_level_greater",
		int(uncrushable.get("health", 0)) == health_before,
		"health %d->%d" % [health_before, int(uncrushable.get("health", -1))]
	)

	var sim_slow = _make_sim()
	var slow := _spawn(sim_slow, 14, SimScript.PLAYER_TEAM, Vector2(0.0, 0.0), "test.cavalry", "test.cavalry-horde")
	var victim := _spawn(sim_slow, 104, SimScript.ENEMY_TEAM, Vector2(1.0, 0.0), "test.soldier", "test.soldier-horde")
	slow["current_speed"] = float(slow.get("speed", 2.0)) * 0.3
	slow["trample_cooldown"] = 0
	var slow_before := int(victim.get("health", 0))
	sim_slow._try_cavalry_trample(slow)
	_check(
		"crush_requires_min_velocity_percent",
		int(victim.get("health", 0)) == slow_before,
		"speed=%.2f health %d->%d" % [float(slow.get("current_speed", 0.0)), slow_before, int(victim.get("health", -1))]
	)

	var sim_foot = _make_sim()
	var foot := _spawn(sim_foot, 15, SimScript.PLAYER_TEAM, Vector2(0.0, 0.0), "test.soldier", "test.soldier-horde")
	var foot_victim := _spawn(sim_foot, 105, SimScript.ENEMY_TEAM, Vector2(1.0, 0.0), "test.soldier", "test.soldier-horde")
	foot["current_speed"] = 2.0
	foot["trample_cooldown"] = 0
	var foot_before := int(foot_victim.get("health", 0))
	sim_foot._try_cavalry_trample(foot)
	_check(
		"infantry_does_not_crush",
		int(foot_victim.get("health", 0)) == foot_before,
		"health %d->%d" % [foot_before, int(foot_victim.get("health", -1))]
	)


func _simulation_document(movement: Dictionary, crush: Dictionary) -> Dictionary:
	var simulation := {
		"unit_type": "bfme2.object.test-cavalry-horde",
		"source_object_id": "GondorKnightsofDolAmroth",
		"category": "cavalry",
		"member_count": 5,
		"member_health": 400,
		"speed_source": 90.0,
		"vision_range_source": 175.0,
		"movement": {
			"acceleration": 30.0,
			"braking": 30.0,
			"turnRateDegreesPerSecond": 360.0,
		},
		"combat": {
			"attackRange": 20.0,
			"delayBetweenShotsMs": 1000.0,
			"preAttackDelayMs": 200.0,
			"firingDurationMs": 200.0,
			"damage": 80,
			"weaponId": "DolAmrothKnightLance",
		},
		"formation": {
			"positions": [
				{"x": 0.0, "y": 0.0},
				{"x": 20.0, "y": 0.0},
				{"x": 0.0, "y": 20.0},
				{"x": 20.0, "y": 20.0},
				{"x": 10.0, "y": 10.0},
			],
		},
	}
	for key in movement.keys():
		(simulation["movement"] as Dictionary)[key] = movement[key]
	for key in crush.keys():
		simulation[key] = crush[key]
	return simulation


func _test_adapter_copies_crush_and_turn_fields() -> void:
	var rule: Dictionary = AdapterScript.normalized_unit_rule(
		_simulation_document(
			{"maxTurnWithoutReformDegrees": 100.0},
			{
				"crusher_level": 2,
				"crushable_level": 1,
				"crush_weapon_id": "DolAmrothLancerCrush",
				"crush_damage": DOL_AMROTH_CRUSH_DAMAGE,
				"min_crush_velocity_percent": 40,
				"crush_deceleration_percent": 20,
				"crush_knockback": 10.0,
			}
		),
		0.1
	)
	_check(
		"adapter_copies_crush_damage_250",
		not rule.is_empty()
			and int(rule.get("crush_damage", 0)) == DOL_AMROTH_CRUSH_DAMAGE
			and int(rule.get("crusher_level", 0)) == 2
			and String(rule.get("crush_weapon_id", "")) == "DolAmrothLancerCrush",
		str(rule)
	)
	_check(
		"adapter_copies_cavalry_turn_fields",
		not rule.is_empty()
			and is_equal_approx(float(rule.get("turn_rate_degrees_per_second", 0.0)), 360.0)
			and is_equal_approx(float(rule.get("max_turn_without_reform_degrees", 0.0)), 100.0),
		"turn=%s reform=%s" % [rule.get("turn_rate_degrees_per_second"), rule.get("max_turn_without_reform_degrees")]
	)

	var infantry: Dictionary = AdapterScript.normalized_unit_rule(
		{
			"unit_type": "bfme2.object.gondor-fighter-horde",
			"source_object_id": "GondorFighterHorde",
			"category": "infantry",
			"member_count": 1,
			"member_health": 200,
			"speed_source": 50.0,
			"vision_range_source": 175.0,
			"movement": {
				"acceleration": 20.0,
				"braking": 20.0,
				"turnRateDegreesPerSecond": 180.0,
				"maxTurnWithoutReformDegrees": 45.0,
			},
			"combat": {
				"attackRange": 11.5,
				"delayBetweenShotsMs": 600.0,
				"preAttackDelayMs": 200.0,
				"firingDurationMs": 200.0,
				"damage": 10,
			},
			"formation": {"positions": [{"x": 0.0, "y": 0.0}]},
		},
		0.1
	)
	_check(
		"adapter_infantry_keeps_45_and_180",
		not infantry.is_empty()
			and is_equal_approx(float(infantry.get("turn_rate_degrees_per_second", 0.0)), 180.0)
			and is_equal_approx(float(infantry.get("max_turn_without_reform_degrees", 0.0)), 45.0),
		"turn=%s reform=%s" % [infantry.get("turn_rate_degrees_per_second"), infantry.get("max_turn_without_reform_degrees")]
	)


func _test_simulation_rule_preserves_resolved_crush() -> void:
	## THE PIPELINE BUG. simulation_rule used to flatten resolved.crush onto
	## an intermediate row and then drop it, so normalized_unit_rule never
	## saw 250 even after a recook.
	var document := {
		"objectId": "GondorKnightsofDolAmroth",
		"category": "cavalry",
		"registration": {
			"simulation": {
				"status": "ready",
				"resolved": {
					"displayNameId": "OBJECT:GondorKnightsofDol",
					"buildCost": 800,
					"buildTimeSeconds": 30.0,
					"commandPoints": 20,
					"memberCount": 1,
					"memberHealth": 400,
					"speed": 90.0,
					"visionRange": 175.0,
					"combat": {
						"attackRange": 20.0,
						"delayBetweenShotsMs": 1000.0,
						"preAttackDelayMs": 200.0,
						"firingDurationMs": 200.0,
						"damage": 80,
					},
					"movement": {
						"acceleration": 30.0,
						"braking": 30.0,
						"turnRateDegreesPerSecond": 360.0,
						"maxTurnWithoutReformDegrees": 100.0,
					},
					"formation": {"positions": [{"x": 0.0, "y": 0.0}]},
					"crush": {
						"crusherLevel": 2,
						"crushableLevel": 1,
						"crushWeaponId": "DolAmrothLancerCrush",
						"crushDamage": DOL_AMROTH_CRUSH_DAMAGE,
						"minCrushVelocityPercent": 40,
						"crushDecelerationPercent": 20,
						"crushKnockback": 10.0,
					},
				},
			},
		},
	}
	var simulation: Dictionary = AdapterScript.simulation_rule(document, false)
	_check(
		"simulation_rule_keeps_resolved_crush_damage",
		not simulation.is_empty()
			and int(simulation.get("crush_damage", 0)) == DOL_AMROTH_CRUSH_DAMAGE
			and int(simulation.get("crusher_level", 0)) == 2
			and String(simulation.get("crush_weapon_id", "")) == "DolAmrothLancerCrush",
		"crush_damage=%d crusher=%d empty=%s" % [
			int(simulation.get("crush_damage", 0)),
			int(simulation.get("crusher_level", 0)),
			str(simulation.is_empty()),
		]
	)
	var rule: Dictionary = AdapterScript.normalized_unit_rule(simulation, 0.1)
	_check(
		"normalized_unit_rule_keeps_simulation_crush",
		not rule.is_empty()
			and int(rule.get("crush_damage", 0)) == DOL_AMROTH_CRUSH_DAMAGE
			and int(rule.get("crusher_level", 0)) == 2,
		"crush_damage=%d empty=%s" % [int(rule.get("crush_damage", 0)), str(rule.is_empty())]
	)


func _test_cavalry_row_receives_authored_turn() -> void:
	var sim = _make_sim()
	var horse := _spawn(sim, 21, SimScript.PLAYER_TEAM, Vector2.ZERO, "test.cavalry", "test.cavalry-horde")
	var foot := _spawn(sim, 22, SimScript.PLAYER_TEAM, Vector2(0.0, 10.0), "test.soldier", "test.soldier-horde")
	_check(
		"cavalry_row_receives_turn_time_1000",
		is_equal_approx(float(horse.get("turn_rate_degrees_per_second", 0.0)), 360.0),
		str(horse.get("turn_rate_degrees_per_second"))
	)
	_check(
		"cavalry_row_receives_max_turn_without_reform_100",
		is_equal_approx(float(horse.get("max_turn_without_reform_degrees", 0.0)), 100.0)
			and is_equal_approx(sim._retail_reform_threshold_degrees(horse), 100.0),
		"row=%s threshold=%s" % [horse.get("max_turn_without_reform_degrees"), sim._retail_reform_threshold_degrees(horse)]
	)
	_check(
		"infantry_row_stays_45_and_180",
		is_equal_approx(float(foot.get("turn_rate_degrees_per_second", 0.0)), 180.0)
			and is_equal_approx(sim._retail_reform_threshold_degrees(foot), 45.0),
		"turn=%s threshold=%s" % [foot.get("turn_rate_degrees_per_second"), sim._retail_reform_threshold_degrees(foot)]
	)


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("CRUSH_TRAMPLE PASS %s" % name)
	else:
		failed += 1
		printerr("CRUSH_TRAMPLE FAIL %s%s" % [name, "" if detail == "" else " (%s)" % detail])


func _finish() -> void:
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		printerr(
			"CRUSH_TRAMPLE FAIL expected_checks (passed=%d failed=%d expected=%d)"
			% [passed, failed - 1, EXPECTED_CHECKS]
		)
	print("CRUSH_TRAMPLE_RESULT passed=%d failed=%d" % [passed, failed])
	_runner_watchdog.stop()
	quit(0 if failed == 0 else 1)

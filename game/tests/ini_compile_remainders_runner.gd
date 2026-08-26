extends SceneTree
## Adapter + sim consume of remaining CORE INI fields.
## Compile is proven in importer/tests/test_ini_compile_remainders.py.
## These checks drive shipped adapter/sim functions; absent stays absent.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const AdapterScript = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")

const EXPECTED_CHECKS := 19
const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
var _watchdog := RunnerWatchdogScript.new()
var passed := 0
var failed := 0


func _initialize() -> void:
	_watchdog.start(self, "INI_COMPILE_REMAINDERS")
	call_deferred("_run")


func _run() -> void:
	_test_adapter_copies_authored_fields()
	_test_adapter_absent_fields_stay_absent()
	_test_sim_flanking_bonus()
	_test_sim_crush_revenge()
	_test_sim_stance_modifiers()
	_finish()


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("INI_REMAINDER PASS %s" % label)
	else:
		failed += 1
		printerr("INI_REMAINDER FAIL %s%s" % [label, "" if detail == "" else " (%s)" % detail])


func _bare_sim():
	var sim = SimScript.new()
	var filler := _unit_rule({})
	sim._rules = {"faction_manifest": _q80_harness_manifest(), 
		"enable_base_loop": true,
		"starting_resources": 10000,
		"ai_attack_delay_ticks": 4000,
		"unit_rules": {
			SimScript.SOLDIER_OBJECT_ID: filler,
			SimScript.ARCHER_OBJECT_ID: filler,
			SimScript.TOWER_GUARD_OBJECT_ID: filler,
			SimScript.KNIGHT_OBJECT_ID: filler,
		},
	}
	sim.setup({}, {})
	sim.ai_enabled = false
	return sim


func _unit_rule(overrides: Dictionary) -> Dictionary:
	var rule := {
		"horde_id": SimScript.SOLDIER_HORDE_ID,
		"speed": 2.0,
		"speed_source": 20.0,
		"acceleration": 20.0,
		"acceleration_source": 200.0,
		"turn_rate_degrees_per_second": 180.0,
		"braking": 20.0,
		"braking_source": 200.0,
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
		"member_damage": 20,
		"member_health": 400,
		"member_count": 1,
		"formation_positions": [Vector3.ZERO],
		"provenance": {},
		"category": "infantry",
		"is_builder": false,
	}
	for key in overrides.keys():
		rule[key] = overrides[key]
	return rule


func _simulation_base() -> Dictionary:
	return {
		"unit_type": "bfme2.object.remainder-infantry-horde",
		"source_object_id": "RemainderInfantryHorde",
		"category": "infantry",
		"member_count": 1,
		"member_health": 200,
		"speed_source": 55.0,
		"vision_range_source": 175.0,
		"movement": {
			"acceleration": 50.0,
			"braking": 50.0,
			"turnRateDegreesPerSecond": 180.0,
			"maxTurnWithoutReformDegrees": 45.0,
			"waitForFormation": true,
		},
		"combat": {
			"attackRange": 11.5,
			"delayBetweenShotsMs": 600.0,
			"preAttackDelayMs": 200.0,
			"firingDurationMs": 200.0,
			"damage": 40,
			"flankingBonus": 50.0,
		},
		"formation": {"positions": [{"x": 0.0, "y": 0.0}]},
		"crush_revenge_weapon_id": "BasicInfantryCrushRevenge",
		"crush_revenge_damage": 10,
		"kind_of": ["SELECTABLE", "INFANTRY", "HORDE"],
		"stances": {
			"template": "FighterHorde",
			"default": "Battle",
			"cycleOrder": ["HoldGround", "Battle", "Aggressive"],
			"states": {
				"HoldGround": {
					"damageMultiplier": 0.85,
					"incomingDamageMultiplier": 0.75,
					"visionMultiplier": 0.1,
					"speedMultiplier": 1.0,
				},
				"Battle": {
					"damageMultiplier": 1.0,
					"incomingDamageMultiplier": 1.0,
					"visionMultiplier": 1.0,
					"speedMultiplier": 1.0,
				},
				"Aggressive": {
					"damageMultiplier": 1.25,
					"incomingDamageMultiplier": 1.15,
					"visionMultiplier": 2.0,
					"speedMultiplier": 1.0,
				},
			},
		},
	}


func _test_adapter_copies_authored_fields() -> void:
	var rule: Dictionary = AdapterScript.normalized_unit_rule(_simulation_base(), 0.1)
	_check(
		"adapter_copies_flanking_bonus",
		not rule.is_empty() and is_equal_approx(float(rule.get("flanking_bonus", 0.0)), 50.0),
		str(rule.get("flanking_bonus"))
	)
	_check(
		"adapter_copies_wait_for_formation",
		not rule.is_empty() and bool(rule.get("wait_for_formation", false)),
		str(rule.get("wait_for_formation"))
	)
	_check(
		"adapter_copies_crush_revenge",
		not rule.is_empty()
			and String(rule.get("crush_revenge_weapon_id", "")) == "BasicInfantryCrushRevenge"
			and int(rule.get("crush_revenge_damage", 0)) == 10,
		str(rule)
	)
	_check(
		"adapter_copies_kind_of",
		not rule.is_empty()
			and (rule.get("kind_of", []) as Array).has("HORDE")
			and (rule.get("kind_of", []) as Array).has("INFANTRY"),
		str(rule.get("kind_of"))
	)
	_check(
		"adapter_copies_stances",
		not rule.is_empty()
			and String((rule.get("stances", {}) as Dictionary).get("template", "")) == "FighterHorde",
		str(rule.get("stances"))
	)
	var document := {
		"objectId": "RemainderInfantryHorde",
		"category": "infantry",
		"kindOf": {
			"container": ["SELECTABLE", "HORDE", "MELEE_HORDE"],
			"primaryMember": ["SELECTABLE", "INFANTRY"],
		},
		"registration": {
			"simulation": {
				"status": "ready",
				"resolved": {
					"displayNameId": "OBJECT:RemainderInfantry",
					"buildCost": 80,
					"buildTimeSeconds": 15.0,
					"commandPoints": 10,
					"memberCount": 1,
					"memberHealth": 200,
					"speed": 55.0,
					"visionRange": 175.0,
					"combat": {
						"attackRange": 20.0,
						"delayBetweenShotsMs": 1000.0,
						"preAttackDelayMs": 200.0,
						"firingDurationMs": 200.0,
						"damage": 40,
						"flankingBonus": 50.0,
					},
					"movement": {
						"acceleration": 50.0,
						"braking": 50.0,
						"turnRateDegreesPerSecond": 180.0,
						"waitForFormation": true,
					},
					"formation": {"positions": [{"x": 0.0, "y": 0.0}]},
					"crush": {
						"crushRevengeWeaponId": "BasicInfantryCrushRevenge",
						"crushRevengeDamage": 10,
					},
					"stances": {
						"template": "FighterHorde",
						"default": "Battle",
						"states": {},
					},
				},
			},
			"kindOf": {
				"container": ["SELECTABLE", "HORDE", "MELEE_HORDE"],
				"primaryMember": ["SELECTABLE", "INFANTRY"],
			},
		},
	}
	var simulation: Dictionary = AdapterScript.simulation_rule(document, false)
	_check(
		"simulation_rule_copies_kind_of_and_revenge",
		not simulation.is_empty()
			and (simulation.get("kind_of", []) as Array).has("HORDE")
			and int(simulation.get("crush_revenge_damage", 0)) == 10
			and String((simulation.get("stances", {}) as Dictionary).get("template", "")) == "FighterHorde",
		str(simulation)
	)


func _test_adapter_absent_fields_stay_absent() -> void:
	var bare := {
		"unit_type": "bfme2.object.remainder-solo",
		"source_object_id": "RemainderSoloMember",
		"category": "infantry",
		"member_count": 1,
		"member_health": 200,
		"speed_source": 40.0,
		"vision_range_source": 175.0,
		"movement": {
			"acceleration": 20.0,
			"braking": 20.0,
			"turnRateDegreesPerSecond": 720.0,
		},
		"combat": {
			"attackRange": 11.5,
			"delayBetweenShotsMs": 600.0,
			"preAttackDelayMs": 200.0,
			"firingDurationMs": 200.0,
			"damage": 10,
		},
		"formation": {"positions": [{"x": 0.0, "y": 0.0}]},
	}
	var rule: Dictionary = AdapterScript.normalized_unit_rule(bare, 0.1)
	_check(
		"absent_flanking_bonus_not_invented",
		not rule.is_empty() and not rule.has("flanking_bonus"),
		str(rule.keys())
	)
	_check(
		"absent_wait_for_formation_not_invented",
		not rule.is_empty() and not rule.has("wait_for_formation"),
		str(rule.keys())
	)
	_check(
		"absent_crush_revenge_not_invented",
		not rule.is_empty()
			and not rule.has("crush_revenge_damage")
			and not rule.has("crush_revenge_weapon_id"),
		str(rule.keys())
	)
	_check(
		"absent_kind_of_not_invented",
		not rule.is_empty() and not rule.has("kind_of"),
		str(rule.keys())
	)


func _test_sim_flanking_bonus() -> void:
	var sim = _bare_sim()
	var attacker_rule := _unit_rule({"flanking_bonus": 50.0, "member_damage": 40, "member_health": 10000})
	var victim_rule := _unit_rule({"member_health": 10000, "member_damage": 1})
	sim._rules["unit_rules"] = {"test.hit": attacker_rule, "test.victim": victim_rule}
	sim._add_battalion(11, 0, Vector2.ZERO, "V", "test.victim", "test.victim-horde", -1, victim_rule)
	sim._add_battalion(12, 1, Vector2(-4.0, 0.0), "R", "test.hit", "test.hit-horde", -1, attacker_rule)
	sim._add_battalion(13, 1, Vector2(4.0, 0.0), "F", "test.hit", "test.hit-horde", -1, attacker_rule)
	var victim: Dictionary = sim.entities[11]
	victim["facing"] = Vector2.RIGHT
	var rear: Dictionary = sim.entities[12]
	var front: Dictionary = sim.entities[13]
	_check(
		"entity_copies_flanking_bonus",
		is_equal_approx(float(rear.get("flanking_bonus", 0.0)), 50.0),
		str(rear.get("flanking_bonus"))
	)
	var rear_mult := sim._flanking_outgoing_multiplier(rear, victim)
	var front_mult := sim._flanking_outgoing_multiplier(front, victim)
	_check(
		"rear_is_flanking_hit",
		sim._is_flanking_hit(12, victim) and not sim._is_flanking_hit(13, victim),
		"rear=%s front=%s" % [str(sim._is_flanking_hit(12, victim)), str(sim._is_flanking_hit(13, victim))]
	)
	_check(
		"flanking_bonus_adds_fifty_percent",
		is_equal_approx(rear_mult, 1.5) and is_equal_approx(front_mult, 1.0),
		"rear=%s front=%s" % [str(rear_mult), str(front_mult)]
	)
	var no_bonus = _bare_sim()
	var plain := _unit_rule({"member_damage": 40, "member_health": 10000})
	no_bonus._rules["unit_rules"] = {"test.hit": plain, "test.victim": victim_rule}
	no_bonus._add_battalion(21, 0, Vector2.ZERO, "V", "test.victim", "test.victim-horde", -1, victim_rule)
	no_bonus._add_battalion(22, 1, Vector2(-4.0, 0.0), "R", "test.hit", "test.hit-horde", -1, plain)
	var plain_row: Dictionary = no_bonus.entities[22]
	_check(
		"absent_flanking_bonus_not_on_entity",
		not plain_row.has("flanking_bonus"),
		str(plain_row.keys())
	)


func _test_sim_crush_revenge() -> void:
	var sim = _bare_sim()
	var cav_rule := _unit_rule({
		"category": "cavalry",
		"crush_damage": 250,
		"crusher_level": 2,
		"member_health": 4000,
		"member_damage": 20,
	})
	var foot_rule := _unit_rule({
		"crushable_level": 0,
		"crush_revenge_weapon_id": "BasicInfantryCrushRevenge",
		"crush_revenge_damage": 10,
		"member_health": 4000,
	})
	sim._rules["unit_rules"] = {"test.cav": cav_rule, "test.foot": foot_rule}
	sim._add_battalion(31, 0, Vector2.ZERO, "C", "test.cav", "test.cav-horde", -1, cav_rule)
	sim._add_battalion(32, 1, Vector2(1.0, 0.0), "F", "test.foot", "test.foot-horde", -1, foot_rule)
	var cav: Dictionary = sim.entities[31]
	var foot: Dictionary = sim.entities[32]
	cav["current_speed"] = float(cav.get("speed", 0.0))
	cav["trample_cooldown"] = 0
	var cav_before := int(cav.get("health", 0))
	var foot_before := int(foot.get("health", 0))
	sim._try_cavalry_trample(cav)
	var foot_dealt := foot_before - int(foot.get("health", 0))
	var cav_dealt := cav_before - int(cav.get("health", 0))
	_check(
		"crush_revenge_reflects_authored_10",
		foot_dealt == 250 and cav_dealt == 10,
		"foot_dealt=%d cav_dealt=%d" % [foot_dealt, cav_dealt]
	)
	var closed = _bare_sim()
	var named_only := _unit_rule({
		"crushable_level": 0,
		"crush_revenge_weapon_id": "BasicInfantryCrushRevenge",
		"member_health": 4000,
	})
	closed._rules["unit_rules"] = {"test.cav": cav_rule, "test.foot": named_only}
	closed._add_battalion(41, 0, Vector2.ZERO, "C", "test.cav", "test.cav-horde", -1, cav_rule)
	closed._add_battalion(42, 1, Vector2(1.0, 0.0), "F", "test.foot", "test.foot-horde", -1, named_only)
	var closed_cav: Dictionary = closed.entities[41]
	closed_cav["current_speed"] = float(closed_cav.get("speed", 0.0))
	closed_cav["trample_cooldown"] = 0
	var closed_before := int(closed_cav.get("health", 0))
	closed._try_cavalry_trample(closed_cav)
	_check(
		"crush_revenge_fail_closed_without_damage",
		int(closed_cav.get("health", 0)) == closed_before,
		"health=%d before=%d" % [int(closed_cav.get("health", 0)), closed_before]
	)


func _test_sim_stance_modifiers() -> void:
	var sim = _bare_sim()
	var stances := {
		"template": "FighterHorde",
		"default": "Battle",
		"states": {
			"HoldGround": {"damageMultiplier": 0.85, "incomingDamageMultiplier": 0.75, "visionMultiplier": 0.1, "speedMultiplier": 1.0},
			"Battle": {"damageMultiplier": 1.0, "incomingDamageMultiplier": 1.0, "visionMultiplier": 1.0, "speedMultiplier": 1.0},
			"Aggressive": {"damageMultiplier": 1.25, "incomingDamageMultiplier": 1.15, "visionMultiplier": 2.0, "speedMultiplier": 1.0},
		},
	}
	var rule := _unit_rule({"stances": stances})
	sim._rules["unit_rules"] = {"test.stance": rule}
	sim._add_battalion(51, 0, Vector2.ZERO, "S", "test.stance", "test.stance-horde", -1, rule)
	var row: Dictionary = sim.entities[51]
	var battle: Dictionary = sim._stance_state(row, "Battle")
	var hold: Dictionary = sim._stance_state(row, "HoldGround")
	var aggressive: Dictionary = sim._stance_state(row, "Aggressive")
	_check(
		"compiled_hold_ground_uses_085_075",
		is_equal_approx(float(hold.get("damageMultiplier", 0.0)), 0.85)
			and is_equal_approx(float(hold.get("incomingDamageMultiplier", 0.0)), 0.75),
		str(hold)
	)
	_check(
		"compiled_aggressive_uses_125_115",
		is_equal_approx(float(aggressive.get("damageMultiplier", 0.0)), 1.25)
			and is_equal_approx(float(aggressive.get("incomingDamageMultiplier", 0.0)), 1.15),
		str(aggressive)
	)
	var empty_sim = _bare_sim()
	var empty_rule := _unit_rule({})
	empty_sim._rules["unit_rules"] = {"test.empty": empty_rule}
	empty_sim._add_battalion(52, 0, Vector2.ZERO, "E", "test.empty", "test.empty-horde", -1, empty_rule)
	var empty_state: Dictionary = empty_sim._stance_state(empty_sim.entities[52], "HoldGround")
	_check(
		"absent_stance_contract_stays_1",
		is_equal_approx(float(empty_state.get("damageMultiplier", 0.0)), 1.0)
			and is_equal_approx(float(empty_state.get("incomingDamageMultiplier", 0.0)), 1.0)
			and is_equal_approx(float(battle.get("damageMultiplier", 0.0)), 1.0),
		str(empty_state)
	)


func _finish() -> void:
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		printerr(
			"INI_REMAINDER FAIL expected_checks (passed=%d failed=%d expected=%d)"
			% [passed, failed - 1, EXPECTED_CHECKS]
		)
	print("INI_COMPILE_REMAINDERS_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)


static func _q80_harness_manifest() -> Dictionary:
	# Q80: labeled SYNTHETIC default_manifest() with an EMPTY roster - this
	# harness hand-places rows / lacks rules for the default gondor roster.
	var manifest: Dictionary = preload("res://src/retail_slice/retail_faction_manifest.gd").default_manifest()
	manifest["spawn_roster"] = []
	return manifest

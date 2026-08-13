extends SceneTree
## One compiled rule, the sim reads it, no invented stand-in when present.
##
## Retail: armor.ini:762 SoldierArmor FlankedPenalty = 50%.
## Live pack gondorfighterhorde.json already ships table.flankedPenalty.
## Crush / PreAttackType / TurnTime gates: invented stand-in only when the
## authored field is absent.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const AdapterScript = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")

const EXPECTED_CHECKS := 12
const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
var _watchdog := RunnerWatchdogScript.new()
var passed := 0
var failed := 0


func _initialize() -> void:
	_watchdog.start(self, "AUTHORED_FIELD_CONSUMPTION")
	call_deferred("_run")


func _run() -> void:
	_test_flanked_penalty_from_live_soldier_armor()
	_test_crush_authored_beats_half_factor()
	_test_pre_attack_authored_beats_coast_proxy()
	_test_facing_snap_only_without_turn_source()
	_finish()


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("AUTHORED_FIELD PASS %s" % label)
	else:
		failed += 1
		printerr("AUTHORED_FIELD FAIL %s%s" % [label, "" if detail == "" else " (%s)" % detail])


func _bare_sim():
	var sim = SimScript.new()
	var filler := _unit_rule({})
	sim._rules = {
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


func _live_fighter_armor_table() -> Dictionary:
	var content_db = root.get_node_or_null("ContentDB")
	if content_db == null or not content_db.has_method("get_playable_unit_runtime"):
		return {}
	var document: Variant = content_db.get_playable_unit_runtime("GondorFighterHorde")
	if typeof(document) != TYPE_DICTIONARY:
		document = content_db.get_playable_unit_runtime("gondorfighterhorde")
	if typeof(document) != TYPE_DICTIONARY:
		return {}
	var registration: Dictionary = (document as Dictionary).get("registration", {}) as Dictionary
	var gameplay: Dictionary = registration.get("gameplay", {}) as Dictionary
	var simulation: Dictionary = gameplay.get("simulation", {}) as Dictionary
	var resolved: Dictionary = simulation.get("resolved", {}) as Dictionary
	var armor: Dictionary = resolved.get("armor", {}) as Dictionary
	var table: Variant = armor.get("table", {})
	return table as Dictionary if typeof(table) == TYPE_DICTIONARY else {}


func _test_flanked_penalty_from_live_soldier_armor() -> void:
	var table := _live_fighter_armor_table()
	var authored: Variant = table.get("flankedPenalty", {})
	var authored_percent := 0.0
	if typeof(authored) == TYPE_DICTIONARY:
		authored_percent = float((authored as Dictionary).get("percent", 0.0))
	_check(
		"live_soldier_armor_ships_flanked_penalty",
		authored_percent > 0.0,
		"table_keys=%s flanked=%s" % [str(table.keys()), str(authored)]
	)
	var sim = _bare_sim()
	var compiled: Dictionary = sim._compiled_armor_table(table)
	var expected := authored_percent / 100.0
	_check(
		"compiled_armor_table_reads_live_flanked_penalty",
		is_equal_approx(float(compiled.get("flanked_penalty", 0.0)), expected) and expected > 0.0,
		"compiled=%s expected=%s" % [str(compiled.get("flanked_penalty")), str(expected)]
	)
	var empty_compiled: Dictionary = sim._compiled_armor_table({
		"default": {"percent": 100.0},
		"scalars": {},
		"damageScalar": {"percent": 100.0},
	})
	_check(
		"absent_flanked_penalty_is_not_invented",
		not empty_compiled.has("flanked_penalty"),
		str(empty_compiled)
	)
	var victim_id := 21
	var front_id := 22
	var rear_id := 23
	sim._rules["unit_rules"] = {
		"test.victim": _unit_rule({"member_health": 10000, "object_id": "test.victim"}),
		"test.hit": _unit_rule({}),
	}
	sim._add_battalion(victim_id, 0, Vector2.ZERO, "V", "test.victim", "test.victim-horde", -1, _unit_rule({}))
	sim._add_battalion(front_id, 1, Vector2(4.0, 0.0), "F", "test.hit", "test.hit-horde", -1, _unit_rule({}))
	sim._add_battalion(rear_id, 1, Vector2(-4.0, 0.0), "R", "test.hit", "test.hit-horde", -1, _unit_rule({}))
	var victim: Dictionary = sim.entities[victim_id]
	victim["facing"] = Vector2.RIGHT
	victim["object_id"] = "test.victim"
	sim._unit_armor["test.victim"] = compiled
	var front := sim._incoming_damage_factor(front_id, victim, "battalion", "SLASH", [])
	var rear := sim._incoming_damage_factor(rear_id, victim, "battalion", "SLASH", [])
	_check(
		"front_hit_does_not_apply_flanked_penalty",
		front > 0.0 and rear > front,
		"front=%s rear=%s" % [str(front), str(rear)]
	)
	_check(
		"rear_hit_uses_authored_flanked_penalty",
		expected > 0.0 and is_equal_approx(rear, front * (1.0 + expected)),
		"rear=%s front=%s expected_mult=%s" % [str(rear), str(front), str(1.0 + expected)]
	)
func _test_crush_authored_beats_half_factor() -> void:
	var sim = _bare_sim()
	var authored_rule := _unit_rule({
		"category": "cavalry",
		"crush_damage": 250,
		"crusher_level": 2,
		"crushable_level": 1,
		"min_crush_velocity_percent": 40,
		"member_damage": 20,
		"member_count": 1,
	})
	var victim_rule := _unit_rule({"crushable_level": 0, "member_health": 4000})
	sim._rules["unit_rules"] = {"test.cav": authored_rule, "test.foot": victim_rule}
	sim._add_battalion(31, 0, Vector2.ZERO, "C", "test.cav", "test.cav-horde", -1, authored_rule)
	sim._add_battalion(32, 1, Vector2(1.0, 0.0), "F", "test.foot", "test.foot-horde", -1, victim_rule)
	var cav: Dictionary = sim.entities[31]
	var foot: Dictionary = sim.entities[32]
	cav["current_speed"] = float(cav.get("speed", 0.0))
	cav["trample_cooldown"] = 0
	var before := int(foot.get("health", 0))
	sim._try_cavalry_trample(cav)
	var dealt := before - int(foot.get("health", 0))
	var invented := int(round(20.0 * 1.0 * SimScript.TRAMPLE_DAMAGE_FACTOR))
	_check(
		"authored_crush_is_not_the_half_factor_pulse",
		dealt == 250 and dealt != invented,
		"dealt=%d invented=%d" % [dealt, invented]
	)
	var legacy = _bare_sim()
	var cav_rule := _unit_rule({"category": "cavalry", "member_damage": 20, "member_count": 1})
	legacy._rules["unit_rules"] = {"test.cav": cav_rule, "test.foot": victim_rule}
	legacy._add_battalion(41, 0, Vector2.ZERO, "C", "test.cav", "test.cav-horde", -1, cav_rule)
	legacy._add_battalion(42, 1, Vector2(1.0, 0.0), "F", "test.foot", "test.foot-horde", -1, victim_rule)
	var legacy_cav: Dictionary = legacy.entities[41]
	var legacy_foot: Dictionary = legacy.entities[42]
	legacy_cav["current_speed"] = float(legacy_cav.get("speed", 0.0))
	legacy_cav["trample_cooldown"] = 0
	var legacy_before := int(legacy_foot.get("health", 0))
	legacy._try_cavalry_trample(legacy_cav)
	var legacy_dealt := legacy_before - int(legacy_foot.get("health", 0))
	_check(
		"absent_crush_damage_keeps_legacy_half_factor",
		legacy_dealt == invented,
		"dealt=%d invented=%d" % [legacy_dealt, invented]
	)



func _test_pre_attack_authored_beats_coast_proxy() -> void:
	var coast_shape := {
		"clipSize": 1,
		"delayBetweenShotsMs": 0.0,
		"continuousFireCoastExpression": "GONDOR_ARCHER_BOW_RELOADTIME_MAX",
	}
	var authored := coast_shape.duplicate()
	authored["preAttackType"] = "PER_SHOT"
	var authored_resolved: Dictionary = AdapterScript._resolved_pre_attack_type(authored)
	_check(
		"authored_pre_attack_type_wins_over_coast_proxy",
		String(authored_resolved.get("type", "")) == "PER_SHOT"
			and String(authored_resolved.get("source", "")) == "preAttackType",
		str(authored_resolved)
	)
	var proxy: Dictionary = AdapterScript._resolved_pre_attack_type(coast_shape)
	_check(
		"coast_proxy_only_when_pre_attack_type_absent",
		String(proxy.get("type", "")) == "PER_POSITION"
			and String(proxy.get("source", "")).contains("proxy"),
		str(proxy)
	)


func _test_facing_snap_only_without_turn_source() -> void:
	var sim = _bare_sim()
	var snap_rule := _unit_rule({"turn_rate_degrees_per_second": 180.0})
	var honor_rule := _unit_rule({
		"turn_rate_degrees_per_second": 90.0,
		"turn_rate_source": "locomotor",
	})
	sim._rules["unit_rules"] = {"test.snap": snap_rule, "test.honor": honor_rule}
	sim._add_battalion(51, 0, Vector2.ZERO, "S", "test.snap", "test.snap-horde", -1, snap_rule)
	sim._add_battalion(52, 0, Vector2(0.0, 8.0), "H", "test.honor", "test.honor-horde", -1, honor_rule)
	var snap: Dictionary = sim.entities[51]
	snap["facing"] = Vector2.RIGHT
	snap["route"] = [Vector2(0.0, 6.0)]
	snap["current_speed"] = float(snap.get("speed", 2.0))
	sim._step_route(snap)
	var snap_facing := Vector2(snap.get("facing", Vector2.ZERO))
	_check(
		"no_turn_source_still_snaps_facing",
		snap_facing.dot(Vector2(0.0, 1.0)) > 0.99,
		"facing=%s" % str(snap_facing)
	)
	var honor: Dictionary = sim.entities[52]
	honor["facing"] = Vector2.RIGHT
	honor["route"] = [Vector2(0.0, 14.0)]
	honor["current_speed"] = float(honor.get("speed", 2.0))
	sim._step_route(honor)
	var honor_facing := Vector2(honor.get("facing", Vector2.ZERO)).normalized()
	_check(
		"turn_source_does_not_snap_facing",
		honor_facing.dot(Vector2.RIGHT) > 0.5 and honor_facing.dot(Vector2(0.0, 1.0)) < 0.99,
		"facing=%s" % str(honor_facing)
	)
	var adapted: Dictionary = AdapterScript.normalized_unit_rule(
		{
			"unit_type": "bfme2.object.test-horde",
			"source_object_id": "Test",
			"category": "infantry",
			"member_count": 1,
			"member_health": 200,
			"speed_source": 50.0,
			"vision_range_source": 175.0,
			"movement": {
				"acceleration": 20.0,
				"braking": 20.0,
				"turnRateDegreesPerSecond": 240.0,
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
		"adapter_marks_locomotor_turn_rate_source",
		String(adapted.get("turn_rate_source", "")) == "locomotor"
			and is_equal_approx(float(adapted.get("turn_rate_degrees_per_second", 0.0)), 240.0),
		str(adapted.get("turn_rate_source"))
	)
func _finish() -> void:
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		printerr(
			"AUTHORED_FIELD FAIL expected_checks (passed=%d failed=%d expected=%d)"
			% [passed, failed - 1, EXPECTED_CHECKS]
		)
	print("AUTHORED_FIELD_CONSUMPTION_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)

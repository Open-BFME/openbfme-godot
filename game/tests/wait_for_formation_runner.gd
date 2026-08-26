extends SceneTree
## Authored WaitForFormation coheres a group order without the global
## retail_formation_movement flag. Pin fixtures that do not author the field
## stay absent-unless-set.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")

const EXPECTED_CHECKS := 5
const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
var _watchdog := RunnerWatchdogScript.new()
var passed := 0
var failed := 0


func _initialize() -> void:
	_watchdog.start(self, "WAIT_FOR_FORMATION")
	call_deferred("_run")


func _run() -> void:
	_test_authored_group_shares_slowest()
	_test_absent_field_adds_no_cap()
	_test_mixed_group_caps_only_waiters()
	_finish()


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("WAIT_FOR_FORMATION PASS %s" % label)
	else:
		failed += 1
		printerr("WAIT_FOR_FORMATION FAIL %s%s" % [label, "" if detail == "" else " (%s)" % detail])


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
	sim.retail_formation_movement = false
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


func _test_authored_group_shares_slowest() -> void:
	var sim = _bare_sim()
	var fast := _unit_rule({"speed": 20.0, "wait_for_formation": true})
	var slow := _unit_rule({"speed": 10.0, "wait_for_formation": true})
	sim._rules["unit_rules"] = {"test.fast": fast, "test.slow": slow}
	sim._add_battalion(11, 0, Vector2.ZERO, "F", "test.fast", "test.fast-horde", -1, fast)
	sim._add_battalion(12, 0, Vector2(2.0, 0.0), "S", "test.slow", "test.slow-horde", -1, slow)
	var accepted := sim.issue_move([11, 12], Vector2(40.0, 0.0))
	var fast_row: Dictionary = sim.entities[11]
	var slow_row: Dictionary = sim.entities[12]
	_check("issue_move_accepts_both", accepted == 2, "accepted=%d" % accepted)
	_check(
		"authored_waiters_share_slowest_cap",
		is_equal_approx(float(fast_row.get("group_speed_cap", -1.0)), 10.0)
			and is_equal_approx(float(slow_row.get("group_speed_cap", -1.0)), 10.0),
		"fast=%s slow=%s" % [str(fast_row.get("group_speed_cap")), str(slow_row.get("group_speed_cap"))]
	)


func _test_absent_field_adds_no_cap() -> void:
	var sim = _bare_sim()
	var a := _unit_rule({"speed": 20.0})
	var b := _unit_rule({"speed": 10.0})
	sim._rules["unit_rules"] = {"test.a": a, "test.b": b}
	sim._add_battalion(21, 0, Vector2.ZERO, "A", "test.a", "test.a-horde", -1, a)
	sim._add_battalion(22, 0, Vector2(2.0, 0.0), "B", "test.b", "test.b-horde", -1, b)
	sim.issue_move([21, 22], Vector2(40.0, 0.0))
	_check(
		"absent_wait_for_formation_adds_no_cap",
		not (sim.entities[21] as Dictionary).has("group_speed_cap")
			and not (sim.entities[22] as Dictionary).has("group_speed_cap")
	)


func _test_mixed_group_caps_only_waiters() -> void:
	var sim = _bare_sim()
	var waiter := _unit_rule({"speed": 20.0, "wait_for_formation": true})
	var pin := _unit_rule({"speed": 8.0})
	sim._rules["unit_rules"] = {"test.wait": waiter, "test.pin": pin}
	sim._add_battalion(31, 0, Vector2.ZERO, "W", "test.wait", "test.wait-horde", -1, waiter)
	sim._add_battalion(32, 0, Vector2(2.0, 0.0), "P", "test.pin", "test.pin-horde", -1, pin)
	sim.issue_move([31, 32], Vector2(40.0, 0.0))
	_check(
		"mixed_group_caps_waiter_at_slowest",
		is_equal_approx(float((sim.entities[31] as Dictionary).get("group_speed_cap", -1.0)), 8.0),
		str((sim.entities[31] as Dictionary).get("group_speed_cap"))
	)
	_check(
		"mixed_group_leaves_pin_row_without_cap_key",
		not (sim.entities[32] as Dictionary).has("group_speed_cap")
	)


func _finish() -> void:
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		printerr("WAIT_FOR_FORMATION FAIL expected_checks passed=%d failed=%d expected=%d" % [passed, failed - 1, EXPECTED_CHECKS])
	print("WAIT_FOR_FORMATION_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)


static func _q80_harness_manifest() -> Dictionary:
	# Q80: labeled SYNTHETIC default_manifest() with an EMPTY roster - this
	# harness hand-places rows / lacks rules for the default gondor roster.
	var manifest: Dictionary = preload("res://src/retail_slice/retail_faction_manifest.gd").default_manifest()
	manifest["spawn_roster"] = []
	return manifest

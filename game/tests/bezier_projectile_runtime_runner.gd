extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var rock := _rock_document()
	var web := _web_document()
	var landing := _common_landing_unit_document()
	var sim := Sim.new()
	sim.setup({}, {
		"game": "bfme2",
		"spawn_initial_battalions": false,
		"scenario_prop_runtimes": {rock.objectId: rock, web.objectId: web},
		"scenario_unit_runtimes": {landing.objectId: landing},
	})
	sim.ai_enabled = false
	sim.base_loop_enabled = false

	var rock_id := sim.spawn_scenario_prop("RockBigTroll", Vector2.ZERO, "map-placement")
	_check(rock_id > 0 and sim.scenario_props.has(rock_id), "RockBigTroll remains an admitted passive prop")
	var passive := sim.scenario_props[rock_id] as Dictionary
	_check(not passive.has("bezier_projectile") and Vector2(passive.position) == Vector2.ZERO, "map placement does not implicitly activate projectile behavior")
	var passive_hash := sim.state_hash()
	var launch: Dictionary = sim.launch_scenario_bezier_projectile(rock_id, Vector2(100, 0), 4)
	_check(bool(launch.get("ok", false)), "explicit authored projectile launch is accepted")
	_check(String(launch.get("progressAuthority", "")) == "external-authored-projectile-flight", "launch preserves external flight progress authority")
	_check(sim.state_hash() != passive_hash, "active projectile state enters authoritative hash")

	_advance_fixture(sim, 2)
	var airborne := sim.scenario_props[rock_id] as Dictionary
	_check(Vector2(airborne.position).is_equal_approx(Vector2(60.875, 0.0)), "half-flight samples exact authored cubic indent controls")
	_check(is_equal_approx(float(airborne.get("projectile_height_source", -1.0)), 3.0), "half-flight samples exact authored cubic heights")
	_check(String((airborne.get("bezier_projectile", {}) as Dictionary).get("status", "")) == "airborne", "trajectory remains airborne before authored duration")

	var mid_hash := sim.state_hash()
	var snapshot := sim.snapshot()
	var restored := Sim.new()
	restored.setup({}, {"spawn_initial_battalions": false})
	_check(restored.restore(snapshot) and restored.state_hash() == mid_hash, "active Bezier flight snapshot/hash round-trips")
	_advance_fixture(restored, 2)
	var arrived := restored.scenario_props[rock_id] as Dictionary
	_check(Vector2(arrived.position).is_equal_approx(Vector2(100, 0)) and is_zero_approx(float(arrived.get("projectile_height_source", -1.0))), "authored duration reaches exact target")
	_check(String((arrived.get("bezier_projectile", {}) as Dictionary).get("status", "")) == "arrival-deferred", "unproven impact semantics stop at an explicit deferred boundary")
	_check(restored.scenario_props.has(rock_id) and not arrived.has("health") and not arrived.has("team"), "arrival invents no kill damage removal body or ownership")
	var arrival_state := (arrived.get("bezier_projectile", {}) as Dictionary).duplicate(true)
	_advance_fixture(restored, 2)
	var after_arrival := restored.scenario_props[rock_id] as Dictionary
	_check((after_arrival.get("bezier_projectile", {}) as Dictionary) == arrival_state and Vector2(after_arrival.position) == Vector2(100, 0), "arrival-deferred projectile executes exactly once and remains stable")

	var web_id := sim.spawn_scenario_prop("SpiderWebs01", Vector2(5, 5), "map-placement")
	var before_web := (sim.scenario_props[web_id] as Dictionary).duplicate(true)
	var rejected: Dictionary = sim.launch_scenario_bezier_projectile(web_id, Vector2(20, 20), 3)
	_check(not bool(rejected.get("ok", false)) and String(rejected.get("reason", "")) == "bezier-contract-unavailable", "prop without authored Bezier contract fails closed")
	_check((sim.scenario_props[web_id] as Dictionary) == before_web, "rejected launch mutates no passive prop state")

	var landing_id := 80001
	sim.entities[landing_id] = {
		"id": landing_id, "team": Sim.PLAYER_TEAM, "position": Vector2.ZERO,
		"destination": Vector2.ZERO, "health": 100.0, "maximum_health": 100.0,
		"member_maximum_health": 100.0, "member_health": [100.0],
		"category": "infantry", "kind_of": ["INFANTRY"], "state": "idle",
		"target_id": 0, "target_kind": "battalion", "route": [], "route_cells": [],
		"current_speed": 0.0, "speed": 0.0, "attack_cooldown": 0,
		"attack_move": false, "order_kind": "", "auto_acquire_enabled": false,
		"timed_modifiers": {}, "completed_upgrades": [], "command_points": 0,
		"scenario_source_object_id": "KnockbackLandingFixture",
		"scenario_spawn_surface": "map-placement", "unit_type": "KnockbackLandingFixture",
	}
	var missing_bounce := sim.launch_scenario_bezier_projectile(landing_id, Vector2(100, 0), 4)
	_check(not bool(missing_bounce.get("ok", false)) and String(missing_bounce.get("reason", "")) == "authored-bounce-flight-duration-missing", "common landing requires external authored bounce duration")
	var common_launch := sim.launch_scenario_bezier_projectile(landing_id, Vector2(100, 0), 4, 2)
	_check(bool(common_launch.get("ok", false)) and String(common_launch.get("runtimeStatus", "")) == "executable", "common landing row activates executable arrival")
	_advance_fixture(sim, 4)
	_check(sim.entities.has(landing_id), "first impact begins authored bounce before terminal removal")
	_advance_fixture(sim, 2)
	_check(not sim.entities.has(landing_id), "DieOnImpact removes common landing carrier after final bounce")
	_check(sim.scenario_bezier_presentation_requests.size() == 2, "bounce and terminal impact emit exactly two ordered FX requests")
	if sim.scenario_bezier_presentation_requests.size() == 2:
		_check(String((sim.scenario_bezier_presentation_requests[0] as Dictionary).get("fxListId", "")) == "FX_ThrownRockBounceHit", "first impact routes authored bounce FX")
		_check(String((sim.scenario_bezier_presentation_requests[1] as Dictionary).get("fxListId", "")) == "FX_ThrownRockGroundHit", "terminal impact routes authored ground-hit FX")
	var terminal_hash := sim.state_hash()
	var terminal_restored := Sim.new()
	terminal_restored.setup({}, {"spawn_initial_battalions": false})
	_check(terminal_restored.restore(sim.snapshot()) and terminal_restored.state_hash() == terminal_hash, "terminal removal and presentation receipts snapshot/hash round-trip")

	_finish()


func _advance_fixture(sim: RetailSliceSim, ticks: int) -> void:
	## A projectile-only fixture has no opposing armies, so ordinary victory
	## resolution closes the match after one tick. Keep the acceptance match open
	## without changing the product's post-victory freeze behavior.
	for _index in ticks:
		sim.winner = -1
		sim.tick()
	sim.winner = -1


func _rock_document() -> Dictionary:
	var document := _web_document()
	document.objectId = "RockBigTroll"
	document.moduleContracts = [_bezier_contract()]
	return document


func _web_document() -> Dictionary:
	return {
		"schema": "openbfme.neutral-prop-descriptor",
		"objectId": "SpiderWebs01",
		"production": [],
		"scenarioAdmission": {
			"kind": "authored-passive-prop",
			"surfaces": ["map-placement", "script-spawn", "object-creation-list"],
			"buildCommandExposed": false,
		},
		"kindOf": {"effective": ["IMMOBILE", "INERT"]},
		"moduleContracts": [],
		"geometry": null,
		"geometryContactPoints": [],
		"publicBones": [],
		"presentation": {"fixture": "prop-visual"},
	}


func _common_landing_unit_document() -> Dictionary:
	return {
		"objectId": "KnockbackLandingFixture", "category": "monster",
		"registration": {
			"production": [],
			"composition": {"containerObjectId": "KnockbackLandingFixture", "primaryMemberObjectId": "KnockbackLandingFixture"},
			"scenarioAdmission": {"kind": "authored-non-buildable", "role": "creature", "surfaces": ["map-placement"], "buildCommandExposed": false, "evidence": "no-authored-unit-build-route", "sourceIni": "data/ini/object/fixture.ini", "line": 1},
			"presentation": {"fixture": "knockback-visual"},
			"simulation": {"status": "ready", "missing": [], "resolved": {
				"displayNameId": {"value": "Knockback Fixture"}, "buildCost": {"value": 0},
				"buildTimeSeconds": {"value": 1.0}, "commandPoints": {"value": 1},
				"memberCount": {"value": 1}, "memberHealth": {"value": 100},
				"speed": {"value": 90.0}, "visionRange": {"value": 200.0},
				"scenarioOnly": {"value": true, "disposition": "explicit-scenario-admission"},
				"movement": {"acceleration": {"value": 90.0}, "braking": {"value": 90.0}, "turnRateDegreesPerSecond": {"value": 360.0}},
				"formation": {"positions": [{"x": 0.0, "y": 0.0}]},
				"combat": {"attackRange": {"value": 20.0}, "delayBetweenShotsMs": {"value": 1500.0}, "preAttackDelayMs": {"value": 500.0}, "firingDurationMs": {"value": 500.0}, "damage": {"value": 1}},
				"moduleContracts": [_common_landing_contract()],
			}},
		},
	}


func _bezier_contract() -> Dictionary:
	var source := "data/ini/object/nature/props.ini"
	return {
		"module": "BezierProjectileBehavior",
		"fields": {
			"FirstHeight": {"authored": "8", "value": 8, "sourceIni": source, "line": 379},
			"SecondHeight": {"authored": "0", "value": 0, "sourceIni": source, "line": 380},
			"FirstPercentIndent": {"authored": "43%", "percent": 43.0, "ratio": 0.43, "sourceIni": source, "line": 381},
			"SecondPercentIndent": {"authored": "86%", "percent": 86.0, "ratio": 0.86, "sourceIni": source, "line": 382},
			"DetonateCallsKill": {"authored": "Yes", "value": true, "sourceIni": source, "line": 383},
			"PreLandingStateTime": {"authored": "1000", "value": 1000, "sourceIni": source, "line": 384},
		},
		"runtimeStatus": "deferred",
		"extraction": "typed",
		"carrier": "Behavior",
		"sourceIni": source,
		"line": 378,
		"tag": "ModuleTag_03",
		"effectGraph": {
			"kind": "bezier-projectile",
			"trajectory": {
				"kind": "cubic-bezier-envelope", "runtimeStatus": "executable",
				"firstHeight": 8.0, "secondHeight": 0.0,
				"firstIndentRatio": 0.43, "secondIndentRatio": 0.86,
				"progressAuthority": "external-authored-projectile-flight",
			},
			"executionEligibility": {
				"runtimeStatus": "deferred",
				"blockers": ["impact", "kill", "prelanding"],
			},
		},
	}


func _common_landing_contract() -> Dictionary:
	var source := "data/ini/object/fixture.ini"
	var field := func(authored: Variant, value: Variant, line: int) -> Dictionary:
		return {"authored": str(authored), "value": value, "sourceIni": source, "line": line}
	var percent := func(authored: String, ratio: float, line: int) -> Dictionary:
		return {"authored": authored, "percent": ratio * 100.0, "ratio": ratio, "sourceIni": source, "line": line}
	return {
		"module": "BezierProjectileBehavior", "runtimeStatus": "executable",
		"extraction": "typed", "carrier": "Behavior", "sourceIni": source,
		"line": 10, "tag": "ModuleTag_Landing",
		"fields": {
			"FirstHeight": field.call("24", 24, 11), "SecondHeight": field.call("24", 24, 12),
			"FirstPercentIndent": percent.call("30%", 0.3, 13), "SecondPercentIndent": percent.call("70%", 0.7, 14),
			"TumbleRandomly": field.call("No", false, 15), "CrushStyle": field.call("Yes", true, 16),
			"DieOnImpact": field.call("Yes", true, 17), "BounceCount": field.call("1", 1, 18),
			"BounceDistance": field.call("40", 40, 19), "BounceFirstHeight": field.call("24", 24, 20),
			"BounceSecondHeight": field.call("24", 24, 21),
			"BounceFirstPercentIndent": percent.call("20%", 0.2, 22),
			"BounceSecondPercentIndent": percent.call("80%", 0.8, 23),
			"GroundHitFX": field.call("FX_ThrownRockGroundHit", "FX_ThrownRockGroundHit", 24),
			"GroundBounceFX": field.call("FX_ThrownRockBounceHit", "FX_ThrownRockBounceHit", 25),
		},
		"effectGraph": {
			"kind": "bezier-projectile",
			"trajectory": {"kind": "cubic-bezier-envelope", "runtimeStatus": "executable", "firstHeight": 24.0, "secondHeight": 24.0, "firstIndentRatio": 0.3, "secondIndentRatio": 0.7, "progressAuthority": "external-authored-projectile-flight"},
			"executionEligibility": {"runtimeStatus": "executable", "blockers": []},
			"arrival": {"kind": "authored-ground-impact-bounce", "runtimeStatus": "executable", "crushStyle": true, "dieOnImpact": true, "tumbleRandomly": false, "bounceCount": 1, "bounceDistance": 40.0, "bounceFirstHeight": 24.0, "bounceSecondHeight": 24.0, "bounceFirstIndentRatio": 0.2, "bounceSecondIndentRatio": 0.8, "groundHitFxId": "FX_ThrownRockGroundHit", "groundBounceFxId": "FX_ThrownRockBounceHit", "terminalPolicy": "remove-on-final-impact"},
		},
	}


func _check(condition: bool, label: String) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("BEZIER_PROJECTILE_RUNTIME_FAIL: %s" % label)


func _finish() -> void:
	print("BEZIER_PROJECTILE_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)

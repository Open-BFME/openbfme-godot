extends SceneTree

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const PROFILE_ENV := "OPENBFME_RANGER_PROFILE"

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var contract := _load_ranger_contract()
	if contract.is_empty():
		_finish()
		return
	_test_no_contract_preserves_m2()
	_test_malformed_contract_fails_closed(contract)
	var first := _run_complete_flow(contract)
	var second := _run_complete_flow(contract)
	_check(
		"same_command_stream_is_deterministic",
		String(first.get("signature", "")) != ""
			and String(first.get("signature", "")) == String(second.get("signature", "")),
		str({"first": first.get("signature", ""), "second": second.get("signature", "")})
	)
	_finish()


func _test_no_contract_preserves_m2() -> void:
	var sim = SimScript.new()
	sim.setup({}, _gameplay_rules({}))
	sim.ai_enabled = false
	var archery := sim.producer_id(SimScript.PLAYER_TEAM, "archery_range")
	_check("missing_contract_is_not_an_error", sim.configuration_error == "")
	_check(
		"missing_contract_preserves_m2_archery_roster",
		Array(sim.structure(archery).get("production", [])) == [SimScript.ARCHER_OBJECT_ID]
	)


func _test_malformed_contract_fails_closed(contract: Dictionary) -> void:
	var malformed := contract.duplicate(true)
	malformed["schemaVersion"] = 99
	var sim = SimScript.new()
	sim.setup({}, _gameplay_rules(malformed))
	sim.ai_enabled = false
	var archery := sim.producer_id(SimScript.PLAYER_TEAM, "archery_range")
	_check("malformed_contract_reports_configuration_error", sim.configuration_error != "")
	_check(
		"malformed_contract_does_not_add_ranger",
		not Array(sim.structure(archery).get("production", [])).has(SimScript.RANGER_HORDE_ID)
	)
	malformed = contract.duplicate(true)
	(malformed["prerequisite"] as Dictionary)["options"] = "CANCELABLE"
	sim = SimScript.new()
	sim.setup({}, _gameplay_rules(malformed))
	archery = sim.producer_id(SimScript.PLAYER_TEAM, "archery_range")
	_check(
		"malformed_nested_contract_fails_closed",
		sim.configuration_error != ""
			and not Array(sim.structure(archery).get("production", [])).has(SimScript.RANGER_HORDE_ID)
	)
	malformed = contract.duplicate(true)
	(malformed["prerequisite"] as Dictionary)["toCommandSet"] = ""
	sim = SimScript.new()
	sim.setup({}, _gameplay_rules(malformed))
	archery = sim.producer_id(SimScript.PLAYER_TEAM, "archery_range")
	_check(
		"malformed_command_set_transition_fails_closed",
		sim.configuration_error != ""
			and not Array(sim.structure(archery).get("production", [])).has(SimScript.RANGER_HORDE_ID)
	)


func _run_complete_flow(contract: Dictionary) -> Dictionary:
	var sim = SimScript.new()
	sim.setup({}, _gameplay_rules(contract))
	sim.ai_enabled = false
	_check("valid_contract_has_no_configuration_error", sim.configuration_error == "", sim.configuration_error)
	var archery := sim.producer_id(SimScript.PLAYER_TEAM, "archery_range")
	var building: Dictionary = sim.structure(archery)
	_check(
		"ranger_command_is_visible_at_level_one",
		int(building.get("level", 0)) == 1
			and Array(building.get("production", [])).has(SimScript.RANGER_HORDE_ID)
	)
	var locked: Dictionary = sim.queue_unit(SimScript.PLAYER_TEAM, archery, SimScript.RANGER_HORDE_ID)
	_check(
		"ranger_is_locked_before_level_two",
		not bool(locked.get("ok", true))
			and String(locked.get("reason", "")) == "missing-upgrade"
			and String(locked.get("required_upgrade", "")) == "Upgrade_GondorArcheryRangeLevel2"
	)
	var resources_before := sim.resources_for_team(SimScript.PLAYER_TEAM)
	var queued: Dictionary = sim.queue_structure_upgrade(
		SimScript.PLAYER_TEAM,
		archery,
		"Upgrade_GondorArcheryRangeLevel2"
	)
	var upgrade_item: Dictionary = queued.get("item", {}) as Dictionary
	_check(
		"level_two_upgrade_uses_source_cost_and_duration",
		bool(queued.get("ok", false))
			and int(upgrade_item.get("cost", -1)) == 500
			and int(upgrade_item.get("duration_ticks", -1)) == 300
			and sim.resources_for_team(SimScript.PLAYER_TEAM) == resources_before - 500,
		str(queued)
	)
	sim.advance(299)
	_check("level_two_upgrade_does_not_complete_early", int(sim.structure(archery).get("level", 0)) == 1)
	sim.advance(1)
	building = sim.structure(archery)
	_check(
		"level_two_upgrade_completes_exactly",
		int(building.get("level", 0)) == 2
			and Array(building.get("completed_upgrades", [])).has("Upgrade_GondorArcheryRangeLevel2")
			and String(building.get("command_set", "")) == "GondorArcheryCommandSetLevel2"
			and _has_event(sim.events, "upgrade.completed", archery)
	)
	var ranger_resources_before := sim.resources_for_team(SimScript.PLAYER_TEAM)
	var ranger_queue: Dictionary = sim.queue_unit(SimScript.PLAYER_TEAM, archery, SimScript.RANGER_HORDE_ID)
	var ranger_item: Dictionary = ranger_queue.get("item", {}) as Dictionary
	_check(
		"level_two_accepts_source_ranger_production",
		bool(ranger_queue.get("ok", false))
			and int(ranger_item.get("cost", -1)) == 600
			and int(ranger_item.get("duration_ticks", -1)) == 300
			and int(ranger_item.get("command_points", -1)) == 70
			and sim.resources_for_team(SimScript.PLAYER_TEAM) == ranger_resources_before - 600,
		str(ranger_queue)
	)
	sim.advance(299)
	_check("ranger_does_not_complete_early", not sim.entities.has(10))
	sim.advance(1)
	var ranger: Dictionary = sim.entity(10)
	_check(
		"ranger_completes_with_typed_identity_and_members",
		String(ranger.get("object_id", "")) == SimScript.RANGER_OBJECT_ID
			and String(ranger.get("unit_type", "")) == SimScript.RANGER_HORDE_ID
			and String(ranger.get("horde_id", "")) == "GondorRangerHorde"
			and int(ranger.get("member_count", 0)) == 10
			and int(ranger.get("member_maximum_health", 0)) == 300
			and int(ranger.get("command_points", 0)) == 70
			and _has_event(sim.events, "production.complete", archery),
		str(ranger)
	)
	return {"signature": sim.state_signature()}


func _gameplay_rules(contract: Dictionary) -> Dictionary:
	var unit_rules := {
		SimScript.SOLDIER_OBJECT_ID: _unit_rule(SimScript.SOLDIER_HORDE_ID, 15, 200, 40, 1.15),
		SimScript.ARCHER_OBJECT_ID: _unit_rule(SimScript.ARCHER_OBJECT_ID, 15, 200, 40, 30.0),
		SimScript.TOWER_GUARD_OBJECT_ID: _unit_rule(SimScript.TOWER_GUARD_OBJECT_ID, 15, 200, 40, 3.5),
		SimScript.KNIGHT_OBJECT_ID: _unit_rule(SimScript.KNIGHT_OBJECT_ID, 15, 200, 40, 1.15),
	}
	var rules := {
		"enable_base_loop": true,
		"spawn_initial_battalions": false,
		"starting_resources": 5000,
		"command_point_cap": 1000,
		"unit_rules": unit_rules,
		"farm_payout_ticks": 50,
		"farm_income": 25,
		"maximum_queue": 5,
	}
	if not contract.is_empty():
		var member: Dictionary = ((contract.get("unitRule", {}) as Dictionary).get("member", {}) as Dictionary)
		var health := int((member.get("health", {}) as Dictionary).get("value", 0))
		rules["ranger_runtime"] = contract.duplicate(true)
		rules["ranger_unit_rule"] = _unit_rule("GondorRangerHorde", 10, health, 45, 40.0)
	return rules


func _unit_rule(horde_id: String, member_count: int, member_health: int, member_damage: int, attack_range: float) -> Dictionary:
	var positions: Array[Vector3] = []
	for index in range(member_count):
		positions.append(Vector3(float(index), 0.0, 0.0))
	return {
		"horde_id": horde_id,
		"speed": 1.0,
		"speed_source": 10.0,
		"acceleration": 1.0,
		"acceleration_source": 10.0,
		"turn_rate_degrees_per_second": 180.0,
		"braking": 1.0,
		"braking_source": 10.0,
		"attack_range": attack_range,
		"attack_range_source": attack_range * 10.0,
		"minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0,
		"vision_range": 47.0,
		"vision_range_source": 470.0,
		"delay_between_shots_ms": 600.0,
		"pre_attack_delay_ms": 200.0,
		"firing_duration_ms": 200.0,
		"attack_period_ticks": 10,
		"pre_attack_ticks": 2,
		"firing_duration_ticks": 2,
		"member_damage": member_damage,
		"member_count": member_count,
		"member_health": member_health,
		"formation_positions": positions,
		"provenance": {},
	}


func _load_ranger_contract() -> Dictionary:
	var path := OS.get_environment(PROFILE_ENV)
	if path == "":
		path = ProjectSettings.globalize_path("res://../.private/scratch/jobs/m3-ranger-source-correct-conversion/candidate-a.json")
	if not FileAccess.file_exists(path):
		_fail("source_profile_exists", path)
		return {}
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		_fail("source_profile_opens", path)
		return {}
	var value: Variant = JSON.parse_string(file.get_as_text())
	if typeof(value) != TYPE_DICTIONARY:
		_fail("source_profile_is_json_object", path)
		return {}
	var runtime: Variant = (value as Dictionary).get("runtime_data")
	if typeof(runtime) != TYPE_DICTIONARY:
		_fail("source_profile_has_runtime_data", path)
		return {}
	var contract: Variant = (runtime as Dictionary).get("data/m3/ranger-runtime.json")
	if typeof(contract) != TYPE_DICTIONARY:
		_fail("source_profile_has_ranger_contract", path)
		return {}
	return (contract as Dictionary).duplicate(true)


func _has_event(events: Array, kind: String, entity_id: int) -> bool:
	for value in events:
		if typeof(value) != TYPE_DICTIONARY:
			continue
		var event := value as Dictionary
		if String(event.get("kind", "")) == kind and int(event.get("entity_id", 0)) == entity_id:
			return true
	return false


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_ARCHERY_RANGE_LEVEL2 PASS %s" % name)
	else:
		_fail(name, detail)


func _fail(name: String, detail: String = "") -> void:
	failed += 1
	printerr("RETAIL_ARCHERY_RANGE_LEVEL2 FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	print("RETAIL_ARCHERY_RANGE_LEVEL2_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)

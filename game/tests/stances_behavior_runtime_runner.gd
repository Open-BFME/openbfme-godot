extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED_CHECKS := 17
var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var sim := _make_sim()
	sim._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = [_contract()]
	_spawn(sim, 1)
	var row := sim.entities[1] as Dictionary
	_check("typed_stance_contract_attaches", row.has("stances_behavior"))
	_check("battle_is_neutral_default", String(row.get("stance", "")) == "Battle" and not (row.get("timed_modifiers", {}) as Dictionary).has("stance"))
	var aggressive := sim.set_entity_stance(1, "Aggressive")
	_check("command_api_accepts_authored_stance", bool(aggressive.get("ok", false)))
	_check("aggressive_damage_modifier_applies", is_equal_approx(sim._timed_modifier_product(row, "DAMAGE_MULT"), 1.25))
	_check("aggressive_armor_penalty_applies", is_equal_approx(sim._ability_incoming_multiplier(row), 1.10))
	_check("stance_category_is_recorded", String(((row.get("timed_modifiers", {}) as Dictionary).get("stance", {}) as Dictionary).get("category", "")) == "STANCE")
	var hold := sim.set_entity_stance(1, "hold ground")
	_check("normalized_hold_ground_switches", bool(hold.get("ok", false)) and String(row.get("stance", "")) == "HoldGround")
	_check("switch_replaces_prior_stance_effects", is_equal_approx(sim._timed_modifier_product(row, "DAMAGE_MULT"), 0.85) and is_equal_approx(sim._ability_incoming_multiplier(row), 0.75))
	var snapshot := sim.snapshot()
	var state_hash := sim.state_hash()
	var restored := _make_sim()
	_check("stance_snapshot_restores", restored.restore(snapshot))
	_check("stance_hash_round_trips", restored.state_hash() == state_hash)
	_check("restored_modifier_effect_remains_live", is_equal_approx(restored._timed_modifier_product(restored.entities[1] as Dictionary, "DAMAGE_MULT"), 0.85))
	var toggled := sim.toggle_entity_stance(1)
	_check("toggle_uses_retail_three_stance_order", bool(toggled.get("ok", false)) and String(row.get("stance", "")) == "Battle")
	_check("battle_removes_stance_category_only", not (row.get("timed_modifiers", {}) as Dictionary).has("stance"))
	_check("unknown_stance_fails_closed", String(sim.set_entity_stance(1, "Berserk").get("reason", "")).begins_with("stance-not-available"))

	var unresolved := _make_sim(false)
	unresolved._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = [_contract()]
	_spawn(unresolved, 2)
	var receipt := ((unresolved.entities[2] as Dictionary).get("stances_behavior", {}) as Dictionary).get("unsupported_semantics", []) as Array
	_check("unresolved_template_is_receipted", receipt.has("unresolved_stance_modifier_template:FighterHorde"))
	_check("unresolved_template_refuses_switch", String(unresolved.set_entity_stance(2, "Aggressive").get("reason", "")).begins_with("unresolved_stance_modifier_template"))

	var opaque := _make_sim()
	var opaque_contract := _contract()
	opaque_contract["extraction"] = "opaque"
	opaque._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = [opaque_contract]
	_spawn(opaque, 3)
	_check("opaque_contract_fails_closed", String(opaque.set_entity_stance(3, "Aggressive").get("reason", "")) == "typed-stances-contract-missing")

	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("STANCES_BEHAVIOR_RUNTIME_FAIL liveness ran=%d expected=%d" % [ran, EXPECTED_CHECKS])
	print("STANCES_BEHAVIOR_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _contract() -> Dictionary:
	return {"module": "StancesBehavior", "extraction": "typed", "runtimeStatus": "deferred", "tag": "ModuleTag_Stances", "line": 10, "fields": {"StanceTemplate": {"value": "FighterHorde", "authored": "FighterHorde"}}}


func _make_sim(with_modifiers: bool = true) -> RetailSliceSim:
	var unit_rules := {}
	for object_id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]:
		unit_rules[object_id] = _unit_rule().duplicate(true)
	var rules := {"unit_rules": unit_rules}
	if with_modifiers:
		rules["attribute_modifier_rules"] = {
			"FighterHordeStanceAggressive": {"category": "STANCE", "effects": [{"kind": "ARMOR", "value": -0.10}, {"kind": "DAMAGE_MULT", "value": 1.25}, {"kind": "VISION", "value": 2.0}]},
			"FighterHordeStanceHoldGround": {"category": "STANCE", "effects": [{"kind": "ARMOR", "value": 0.25}, {"kind": "DAMAGE_MULT", "value": 0.85}, {"kind": "VISION", "value": 0.10}]},
		}
	var sim: RetailSliceSim = Sim.new()
	sim.setup({}, rules)
	sim.ai_enabled = false
	sim.base_loop_enabled = false
	sim.entities.clear()
	sim.structures.clear()
	return sim


func _spawn(sim: RetailSliceSim, id: int) -> void:
	sim._add_battalion(id, Sim.PLAYER_TEAM, Vector2.ZERO, "Fixture", Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, 0, _unit_rule())


func _unit_rule() -> Dictionary:
	return {"horde_id": Sim.SOLDIER_HORDE_ID, "category": "infantry", "speed": 1.0, "speed_source": 10.0, "acceleration": 1.0, "acceleration_source": 10.0, "turn_rate_degrees_per_second": 180.0, "braking": 1.0, "braking_source": 10.0, "attack_range": 0.1, "attack_range_source": 1.0, "minimum_attack_range": 0.0, "minimum_attack_range_source": 0.0, "vision_range": 1.0, "vision_range_source": 10.0, "delay_between_shots_ms": 1000.0, "pre_attack_delay_ms": 0.0, "firing_duration_ms": 0.0, "attack_period_ticks": 10, "pre_attack_ticks": 0, "firing_duration_ticks": 0, "member_damage": 1, "member_health": 100, "member_count": 1, "formation_positions": [Vector3.ZERO], "provenance": {}}


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("STANCES_BEHAVIOR_RUNTIME_FAIL %s" % label)

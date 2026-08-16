extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED := 25
var passed := 0
var failed := 0

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var sim := _sim([_contract()])
	var row := sim.entities[1] as Dictionary
	var policy := row.get("flammable_update", {}) as Dictionary
	_check("typed_contract_attaches", not policy.is_empty())
	_check("literal_duration_is_three_ticks", int(policy.get("aflame_duration_ticks", -1)) == 3)
	_check("presentation_fx_preserved", ((policy.get("fire_fx", []) as Array)[0] as Dictionary).get("fx") == "FX_TestFire")
	_check("presentation_sound_preserved", String(policy.get("burning_sound_id", "")) == "GenericFireMediumLoop")
	var first := sim.record_flame_damage(1, 4.0)
	_check("below_limit_does_not_ignite", bool(first.get("ok", false)) and not bool(first.get("ignited", true)))
	var second := sim.record_flame_damage(1, 6.0)
	_check("cumulative_limit_ignites", bool(second.get("ignited", false)))
	_check("aflame_status_set", sim.entity_has_object_status(1, "AFLAME"))
	_check("ignition_event_once", _event_count(sim, "module.flammable_ignited") == 1)
	var before := int(row.get("health", 0))
	sim.tick_index = 1
	sim._step_flammable_updates()
	_check("authored_pulse_applies", int(row.get("health", 0)) == before - 3)
	_check("pulse_event_emitted", _event_count(sim, "module.flammable_damage") == 1)
	_check("burned_delay_sets_status", sim.entity_has_object_status(1, "BURNED"))
	var snapshot := sim.snapshot()
	var digest := sim.state_hash()
	var restored := _sim([])
	_check("snapshot_restores", restored.restore(snapshot))
	_check("snapshot_hash_round_trips", restored.state_hash() == digest)
	_check("aflame_timer_restores", int(((restored.entities[1] as Dictionary).get("flammable_update", {}) as Dictionary).get("aflame_until_tick", -1)) == 3)
	sim.tick_index = 3
	sim._step_flammable_updates()
	_check("duration_extinguishes", not bool((row.get("flammable_update", {}) as Dictionary).get("aflame", true)))
	_check("aflame_status_clears", not sim.entity_has_object_status(1, "AFLAME"))
	_check("burned_status_persists", sim.entity_has_object_status(1, "BURNED"))
	var contained := _sim([_contract()])
	contained.entity_container[1] = 99
	_check("contained_burn_respects_false", String(contained.record_flame_damage(1, 20.0).get("reason", "")) == "contained-burning-disabled")
	var unresolved_contract := _contract()
	(unresolved_contract["fields"] as Dictionary)["FlameDamageLimit"] = {"expression": "TEST_LIMIT", "define": "TEST_LIMIT", "sourceIni": "fixture.ini", "line": 1}
	var unresolved := _sim([unresolved_contract])
	_check("define_threshold_fails_closed", String(unresolved.record_flame_damage(1, 20.0).get("reason", "")) == "unresolved-flammable-field")
	_check("define_receipt_preserved", ((unresolved.entities[1] as Dictionary).get("flammable_update", {}) as Dictionary).get("unsupported_semantics", []).has("unresolved-expression:FlameDamageLimit"))
	var opaque := _sim([_contract("opaque")])
	_check("opaque_contract_fails_closed", not (opaque.entities[1] as Dictionary).has("flammable_update"))
	var malformed_contract := _contract()
	(malformed_contract["fields"] as Dictionary)["Invented"] = {"value": 1}
	var malformed := _sim([malformed_contract])
	_check("unknown_field_fails_closed", not (malformed.entities[1] as Dictionary).has("flammable_update"))
	var negative_contract := _contract()
	(negative_contract["fields"] as Dictionary)["AflameDuration"] = {"milliseconds": -1}
	var negative := _sim([negative_contract])
	_check("negative_literal_fails_closed", not (negative.entities[1] as Dictionary).has("flammable_update"))
	var integrated := _sim([_contract()])
	integrated._apply_damage(-1, 1, 10, "battalion", "NORMAL", "FLAME")
	_check("flame_damage_path_integrates", bool(((integrated.entities[1] as Dictionary).get("flammable_update", {}) as Dictionary).get("aflame", false)))
	_check("internal_pulse_does_not_reignite", _event_count(sim, "module.flammable_ignited") == 1)
	if passed + failed != EXPECTED:
		failed += 1
		printerr("FLAMMABLE_UPDATE_RUNTIME_FAIL liveness")
	print("FLAMMABLE_UPDATE_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)

func _contract(extraction: String = "typed") -> Dictionary:
	return {"module": "FlammableUpdate", "runtimeStatus": "deferred", "extraction": extraction, "tag": "ModuleTag_Fire", "sourceIni": "data/ini/object/fixture.ini", "line": 10, "fields": {"AflameDuration": {"milliseconds": 300}, "AflameDamageDelay": {"milliseconds": 100}, "FlameDamageExpiration": {"milliseconds": 200}, "BurnedDelay": {"milliseconds": 100}, "AflameDamageAmount": {"value": 3}, "FlameDamageLimit": {"value": 10}, "BurnContained": {"value": false}, "SetBurnedStatus": {"value": true}, "DamageType": {"value": "FORCE"}, "FireFXList": [{"fx": "FX_TestFire", "bone": "Fire01", "sourceIni": "fixture.ini", "line": 2}], "BurningSoundName": {"value": "GenericFireMediumLoop"}}}

func _sim(contracts: Array) -> RetailSliceSim:
	var rule := {"horde_id": Sim.SOLDIER_HORDE_ID, "category": "infantry", "speed": 1.0, "speed_source": 10.0, "acceleration": 1.0, "acceleration_source": 10.0, "turn_rate_degrees_per_second": 180.0, "braking": 1.0, "braking_source": 10.0, "attack_range": 0.1, "attack_range_source": 1.0, "minimum_attack_range": 0.0, "minimum_attack_range_source": 0.0, "vision_range": 1.0, "vision_range_source": 10.0, "delay_between_shots_ms": 1000.0, "pre_attack_delay_ms": 0.0, "firing_duration_ms": 0.0, "attack_period_ticks": 10, "pre_attack_ticks": 0, "firing_duration_ticks": 0, "member_damage": 1, "member_health": 100, "member_count": 1, "formation_positions": [Vector3.ZERO], "provenance": {}}
	var rules := {}
	for id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]: rules[id] = rule
	var sim: RetailSliceSim = Sim.new()
	sim.setup({}, {"unit_rules": rules})
	sim.ai_enabled = false
	sim.base_loop_enabled = false
	sim.entities.clear()
	sim.structures.clear()
	sim._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = contracts
	sim._add_battalion(1, Sim.PLAYER_TEAM, Vector2.ZERO, "Fixture", Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, 0, rule)
	return sim

func _event_count(sim: RetailSliceSim, kind: String) -> int:
	var count := 0
	for value in sim.events:
		if String((value as Dictionary).get("kind", "")) == kind: count += 1
	return count

func _check(name: String, condition: bool) -> void:
	if condition: passed += 1
	else:
		failed += 1
		push_error("FLAMMABLE_UPDATE_RUNTIME_FAIL " + name)

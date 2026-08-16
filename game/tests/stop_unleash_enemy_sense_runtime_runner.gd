extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const EXPECTED := 42
var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var sim := _sim()
	var actor := sim.entities[1] as Dictionary
	sim._attach_stop_special_power_contract(actor, _stop_contract())
	_check("StopSpecialPower_typed_graph_attaches", not (actor.get("stop_special_powers", []) as Array).is_empty())
	_check("stop_templates_are_exact", (actor.get("stop_special_powers", [])[0] as Dictionary).get("special_power_template", "") == "SpecialAbilityStop" and (actor.get("stop_special_powers", [])[0] as Dictionary).get("stop_power_template", "") == "SpecialAbilitySiegeDeploy")
	actor["activate_module_channel"] = {"special_power_template_id":"SpecialAbilitySiegeDeploy","ability_id":"Deploy","current_target_id":2}
	actor["state"] = "ability"; actor["order_kind"] = "attack"; actor["route"] = [Vector2(4, 0)]; actor["target_id"] = 2; actor["target_kind"] = "battalion"
	var stopped: Dictionary = sim.activate_stop_special_power(1, "SpecialAbilityStop", 0)
	_check("stop_activation_succeeds", bool(stopped.get("ok", false)))
	_check("matching_channel_is_cancelled", not actor.has("activate_module_channel"))
	_check("stop_interrupts_current_order", String(actor.get("order_kind", "x")) == "" and (actor.get("route", []) as Array).is_empty() and int(actor.get("target_id", -1)) == 0 and String(actor.get("state", "")) == "idle")
	_check("stop_emits_exact_target_template", _event_payload(sim, "ability.special_power_stopped", "stop_power_template") == "SpecialAbilitySiegeDeploy")
	_check("stop_wrong_owner_fails_closed", String(sim.activate_stop_special_power(1, "SpecialAbilityStop", 1).get("reason", "")) == "wrong-owner")
	_check("stop_without_active_target_fails_closed", String(sim.activate_stop_special_power(1, "SpecialAbilityStop", 0).get("reason", "")) == "stop-power-not-active")
	actor["activate_module_channel"] = {"special_power_template_id":"SpecialAbilityOther","ability_id":"Other"}
	_check("stop_does_not_cancel_other_template", not bool(sim.activate_stop_special_power(1, "SpecialAbilityStop", 0).get("ok", false)) and actor.has("activate_module_channel"))
	var bad_stop := _stop_contract(); (bad_stop["effectGraph"] as Dictionary)["stopPowerTemplateId"] = "SpecialAbilityOther"; var bad_stop_row := {}; sim._attach_stop_special_power_contract(bad_stop_row, bad_stop)
	_check("stop_graph_field_mismatch_fails_closed", not bad_stop_row.has("stop_special_powers"))
	var opaque_stop := _stop_contract(); opaque_stop["extraction"] = "opaque"; var opaque_stop_row := {}; sim._attach_stop_special_power_contract(opaque_stop_row, opaque_stop)
	_check("opaque_stop_fails_closed", not opaque_stop_row.has("stop_special_powers"))

	var sentry := sim.structures[50] as Dictionary
	sim._attach_unleash_special_power_contract(sentry, _unleash_contract())
	_check("UnleashSpecialPower_typed_graph_attaches", not (sentry.get("unleash_special_powers", []) as Array).is_empty())
	_check("unleash_release_binding_is_exact", String((sentry.get("unleash_special_powers", [])[0] as Dictionary).get("spawned_object_id", "")) == "FixtureSlave" and (sentry.get("unleash_special_powers", [])[0] as Dictionary).get("creation_gate_upgrade_ids", []) == ["Upgrade_HasFixtureSlave"])
	var unleashed: Dictionary = sim.activate_unleash_special_power(50, "SpecialAbilityUnleash", 0)
	_check("instant_unleash_succeeds", bool(unleashed.get("ok", false)) and int(unleashed.get("slave_id", 0)) == 2)
	var slave := sim.entities[2] as Dictionary
	_check("unleash_releases_slave_from_master", int((slave.get("slaved_update", {}) as Dictionary).get("master_id", -1)) == 0)
	_check("unleash_restores_command_admission", not bool(slave.get("unselectable", true)) and not bool(slave.get("ignores_select_all", true)))
	_check("unleash_preserves_watcher_upgrades_until_death", (sentry.get("completed_upgrades", []) as Array).has("Upgrade_HasFixtureSlave") and not (sentry.get("completed_upgrades", []) as Array).has("Upgrade_FixtureSlaveAvailable"))
	_check("released_slave_cannot_be_unleashed_twice", String(sim.activate_unleash_special_power(50, "SpecialAbilityUnleash", 0).get("reason", "")) == "owned-slave-not-found")
	_check("unleash_event_names_slave", int(_event_payload(sim, "ability.slave_unleashed", "slave_id")) == 2)
	var unsupported := _unleash_contract(); ((unsupported["fields"] as Dictionary)["AwardXPForTriggering"] as Dictionary)["value"] = 5; (unsupported["effectGraph"] as Dictionary)["awardXpForTriggering"] = 5; var unsupported_row := {"id":60,"team":0,"health":100}; sim._attach_unleash_special_power_contract(unsupported_row, unsupported); sim.structures[60] = unsupported_row
	_check("nonzero_unleash_xp_is_receipted", ((unsupported_row.get("unleash_special_powers", [])[0] as Dictionary).get("unsupported_semantics", []) as Array).has("nonzero-award-xp"))
	_check("unsupported_unleash_does_not_execute", String(sim.activate_unleash_special_power(60, "SpecialAbilityUnleash", 0).get("reason", "")) == "unsupported-semantics")
	var malformed_unleash := _unleash_contract(); (malformed_unleash["effectGraph"] as Dictionary)["targetMode"] = "LOCATION"; var malformed_unleash_row := {}; sim._attach_unleash_special_power_contract(malformed_unleash_row, malformed_unleash)
	_check("unleash_wrong_target_mode_fails_closed", not malformed_unleash_row.has("unleash_special_powers"))

	var sense_owner := sim.entities[3] as Dictionary
	sim._attach_special_enemy_sense_contract(sense_owner, _sense_contract())
	var sense := sense_owner.get("special_enemy_sense", {}) as Dictionary
	_check("SpecialEnemySenseUpdate_typed_graph_attaches", not sense.is_empty())
	_check("sense_range_and_timer_are_exact", float(sense.get("scan_range_source", 0.0)) == 200.0 and int(sense.get("scan_interval_ticks", 0)) == 20)
	sim._step_special_enemy_sense_updates()
	_check("sense_matches_hostile_kind_within_radius", bool(sense_owner.get("special_enemy_sense_active", false)) and sense_owner.get("special_enemy_sensed_ids", []) == [4])
	_check("sense_rejects_friendly_and_wrong_kind", not (sense_owner.get("special_enemy_sensed_ids", []) as Array).has(5) and not (sense_owner.get("special_enemy_sensed_ids", []) as Array).has(6))
	_check("sense_transition_event_emitted", bool(_event_payload(sim, "module.special_enemy_sense_changed", "active")))
	(sim.entities[4] as Dictionary)["position"] = Vector2(40, 0)
	sim.tick_index = 19; sim._step_special_enemy_sense_updates()
	_check("sense_waits_for_exact_scan_cadence", bool(sense_owner.get("special_enemy_sense_active", false)))
	sim.tick_index = 20; sim._step_special_enemy_sense_updates()
	_check("sense_clears_on_next_scan", not bool(sense_owner.get("special_enemy_sense_active", true)) and (sense_owner.get("special_enemy_sensed_ids", []) as Array).is_empty())
	(sim.entities[7] as Dictionary)["position"] = Vector2(10, 0); sim.tick_index = 40; sim._step_special_enemy_sense_updates()
	_check("sense_matches_exact_object_identifier", sense_owner.get("special_enemy_sensed_ids", []) == [7])
	var malformed_sense := _sense_contract(); (malformed_sense["effectGraph"] as Dictionary)["scanRange"] = 999; var malformed_sense_row := {}; sim._attach_special_enemy_sense_contract(malformed_sense_row, malformed_sense)
	_check("sense_graph_field_mismatch_fails_closed", not malformed_sense_row.has("special_enemy_sense"))
	var unknown_filter := _sense_contract(); ((unknown_filter["fields"] as Dictionary)["SpecialEnemyFilter"] as Dictionary)["value"] = ["ANY", "ORC"]; (unknown_filter["effectGraph"] as Dictionary)["specialEnemyFilter"] = ["ANY", "ORC"]; var unknown_row := {}; sim._attach_special_enemy_sense_contract(unknown_row, unknown_filter)
	_check("unprefixed_sense_leaf_is_receipted", ((unknown_row.get("special_enemy_sense", {}) as Dictionary).get("unsupported_semantics", []) as Array).has("unsupported-filter-token:ORC"))
	var projected := Adapter.ability_rules(_ability_document())
	_check("descriptor_adapter_accepts_both_stable_graphs", projected.size() == 2)
	_check("descriptor_adapter_preserves_effect_kinds", String((projected[0] as Dictionary).get("effect", {}).get("kind", "")) == "stop-special-power" and String((projected[1] as Dictionary).get("effect", {}).get("kind", "")) == "unleash-special-power")
	var bad_stop_document := _ability_document(); ((((bad_stop_document["registration"] as Dictionary)["abilities"] as Array)[0] as Dictionary)["effect"] as Dictionary)["linkedModule"] = {}
	_check("descriptor_adapter_rejects_malformed_stop_graph", Adapter.ability_rules(bad_stop_document).is_empty())
	var bad_unleash_document := _ability_document(); ((((bad_unleash_document["registration"] as Dictionary)["abilities"] as Array)[1] as Dictionary)["effect"] as Dictionary)["creationGateUpgradeIds"] = []
	_check("descriptor_adapter_rejects_malformed_unleash_graph", Adapter.ability_rules(bad_unleash_document).is_empty())
	actor["unit_type"] = "FixtureStop"; actor["level"] = 1; actor["ability_states"] = {}; actor["activate_module_channel"] = {"special_power_template_id":"SpecialAbilitySiegeDeploy","ability_id":"Deploy"}
	sim._unit_ability_rules["FixtureStop"] = [projected[0]]
	var cast_stop := sim.cast_ability(1, "Command_Stop", Vector2.ZERO, 0)
	_check("descriptor_stop_uses_authoritative_cast_path", bool(cast_stop.get("ok", false)) and not actor.has("activate_module_channel"))
	_check("descriptor_stop_arms_authored_cooldown", int(((actor.get("ability_states", {}) as Dictionary).get("Command_Stop", {}) as Dictionary).get("cooldown_ready_tick", 0)) == sim.tick_index + 10)
	var unleash_owner := sentry.duplicate(true); unleash_owner["id"] = 8; unleash_owner["unit_type"] = "FixtureUnleash"; unleash_owner["level"] = 1; unleash_owner["ability_states"] = {}; sim.entities[8] = unleash_owner; sim._attach_unleash_special_power_contract(unleash_owner, _unleash_contract())
	(slave.get("slaved_update", {}) as Dictionary)["master_id"] = 8; slave["unselectable"] = true; slave["ignores_select_all"] = true
	sim._unit_ability_rules["FixtureUnleash"] = [projected[1]]
	var cast_unleash := sim.cast_ability(8, "Command_Unleash", Vector2.ZERO, 0)
	_check("descriptor_unleash_uses_authoritative_cast_path", bool(cast_unleash.get("ok", false)) and int(cast_unleash.get("slave_id", 0)) == 2)
	_check("descriptor_unleash_arms_authored_cooldown", int(((unleash_owner.get("ability_states", {}) as Dictionary).get("Command_Unleash", {}) as Dictionary).get("cooldown_ready_tick", 0)) == sim.tick_index + 10)
	var digest := sim.state_hash(); var snapshot := sim.snapshot(); var restored := _sim()
	_check("snapshot_restores", restored.restore(snapshot))
	_check("typed_special_state_hash_round_trips", restored.state_hash() == digest)
	if passed + failed != EXPECTED:
		failed += 1; printerr("STOP_UNLEASH_ENEMY_SENSE_RUNTIME_FAIL liveness")
	print("STOP_UNLEASH_ENEMY_SENSE_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _stop_contract() -> Dictionary:
	return {"module":"StopSpecialPower","runtimeStatus":"deferred","extraction":"typed","tag":"ModuleTag_Stop","sourceIni":"fixture/siege.ini","line":50,"fields":{"SpecialPowerTemplate":{"value":"SpecialAbilityStop"},"StopPowerTemplate":{"value":"SpecialAbilitySiegeDeploy"}},"effectGraph":{"kind":"stop-special-power","specialPowerTemplateId":"SpecialAbilityStop","stopPowerTemplateId":"SpecialAbilitySiegeDeploy","targetMode":"SELF","interruptsCurrentOrder":true,"linkedModule":{"kind":"SiegeDeploySpecialPower","tag":"ModuleTag_Deploy","sourceIni":"fixture/siege.ini","line":20},"sourceIni":"fixture/siege.ini","line":50}}


func _unleash_contract() -> Dictionary:
	return {"module":"UnleashSpecialPower","runtimeStatus":"deferred","extraction":"typed","tag":"ModuleTag_Unleash","sourceIni":"fixture/wargsentry.ini","line":100,"fields":{"SpecialPowerTemplate":{"value":"SpecialAbilityUnleash"},"UnpackTime":{"milliseconds":0},"AwardXPForTriggering":{"value":0},"Instant":{"value":true}},"effectGraph":{"kind":"unleash-special-power","specialPowerTemplateId":"SpecialAbilityUnleash","timingMs":{"UnpackTime":0},"awardXpForTriggering":0,"instant":true,"targetMode":"SELF_OWNED_SLAVE","spawnedObjectId":"FixtureSlave","creationGateUpgradeIds":["Upgrade_HasFixtureSlave"],"slaveWatcher":{"removeUpgradeId":"Upgrade_HasFixtureSlave","grantUpgradeId":"Upgrade_FixtureSlaveAvailable","sourceIni":"fixture/wargsentry.ini","line":80},"sourceIni":"fixture/wargsentry.ini","line":100}}


func _sense_contract() -> Dictionary:
	return {"module":"SpecialEnemySenseUpdate","runtimeStatus":"deferred","extraction":"typed","tag":"ModuleTag_StingSeesOrcs","sourceIni":"fixture/frodo.ini","line":200,"fields":{"SpecialEnemyFilter":{"value":["ANY","+ORC","+URUK","+MordorShelob"]},"ScanRange":{"value":200},"ScanInterval":{"milliseconds":2000}},"effectGraph":{"kind":"special-enemy-sense","specialEnemyFilter":["ANY","+ORC","+URUK","+MordorShelob"],"scanRange":200,"scanIntervalMs":2000,"targetMode":"PERIODIC_ENEMY_RADIUS_SCAN","sourceIni":"fixture/frodo.ini","line":200}}


func _ability_document() -> Dictionary:
	var stop_effect := (_stop_contract().get("effectGraph", {}) as Dictionary).duplicate(true)
	var unleash_effect := (_unleash_contract().get("effectGraph", {}) as Dictionary).duplicate(true)
	return {"registration":{"stringBindings":{},"abilities":[
		{"id":"Command_Stop","slot":1,"specialPowerId":"SpecialAbilityStop","targeting":"self","cooldownMs":1000,"button":{"iconIds":[],"labelIds":[],"tooltipIds":[],"options":[]},"effect":stop_effect,"implementation":{"status":"implemented","reason":"","limitations":[]},"levelGate":{},"specialPowerContract":{}},
		{"id":"Command_Unleash","slot":2,"specialPowerId":"SpecialAbilityUnleash","targeting":"self","cooldownMs":1000,"button":{"iconIds":[],"labelIds":[],"tooltipIds":[],"options":[]},"effect":unleash_effect,"implementation":{"status":"implemented","reason":"","limitations":[]},"levelGate":{},"specialPowerContract":{}},
	]}}


func _sim() -> RetailSliceSim:
	var sim: RetailSliceSim = Sim.new()
	var unit_rule := {"horde_id":Sim.SOLDIER_HORDE_ID,"category":"infantry","speed":1.0,"speed_source":10.0,"acceleration":1.0,"acceleration_source":10.0,"turn_rate_degrees_per_second":180.0,"braking":1.0,"braking_source":10.0,"attack_range":0.1,"attack_range_source":1.0,"minimum_attack_range":0.0,"minimum_attack_range_source":0.0,"vision_range":1.0,"vision_range_source":10.0,"delay_between_shots_ms":1000.0,"pre_attack_delay_ms":0.0,"firing_duration_ms":0.0,"attack_period_ticks":10,"pre_attack_ticks":0,"firing_duration_ticks":0,"member_damage":1,"member_health":100,"member_count":1,"formation_positions":[Vector3.ZERO],"provenance":{}}
	var unit_rules := {}; for object_id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]: unit_rules[object_id] = unit_rule
	sim.setup({}, {"unit_rules":unit_rules,"source_unit_scale":0.1}); sim.ai_enabled=false; sim.base_loop_enabled=false; sim.entities.clear(); sim.structures.clear(); sim.events.clear()
	sim.entities[1] = {"id":1,"team":0,"health":100,"position":Vector2.ZERO,"destination":Vector2.ZERO,"route":[],"order_kind":"","state":"idle","target_id":0,"target_kind":"","kind_of":["SIEGEENGINE"]}
	sim.entities[2] = {"id":2,"team":0,"health":100,"position":Vector2(2,0),"source_object_id":"FixtureSlave","object_id":"FixtureSlave","unit_type":"FixtureSlave","kind_of":["MONSTER"],"slaved_update":{"master_id":50},"unselectable":true,"ignores_select_all":true}
	sim.entities[3] = {"id":3,"team":0,"health":100,"position":Vector2.ZERO,"kind_of":["HERO"]}
	sim.entities[4] = {"id":4,"team":1,"health":100,"position":Vector2(10,0),"kind_of":["ORC"]}
	sim.entities[5] = {"id":5,"team":0,"health":100,"position":Vector2(10,0),"kind_of":["ORC"]}
	sim.entities[6] = {"id":6,"team":1,"health":100,"position":Vector2(10,0),"kind_of":["INFANTRY"]}
	sim.entities[7] = {"id":7,"team":1,"health":100,"position":Vector2(40,0),"source_object_id":"MordorShelob","object_id":"MordorShelob","kind_of":["HERO"]}
	sim.structures[50] = {"id":50,"team":0,"health":100,"position":Vector2.ZERO,"completed_upgrades":["Upgrade_HasFixtureSlave"],"object_creation_upgrades":[{"thing_to_spawn":"FixtureSlave","triggers":["Upgrade_HasFixtureSlave"],"spawned_ids":[2]}]}
	return sim


func _event_payload(sim: RetailSliceSim, kind: String, key: String) -> Variant:
	for index in range(sim.events.size() - 1, -1, -1):
		var event := sim.events[index] as Dictionary
		if String(event.get("kind", "")) == kind:
			return event.get(key)
	return null


func _check(name: String, condition: bool) -> void:
	if condition: passed += 1
	else: failed += 1; push_error("STOP_UNLEASH_ENEMY_SENSE_RUNTIME_FAIL " + name)

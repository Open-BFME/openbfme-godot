extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED := 38
var passed := 0
var failed := 0

func _initialize() -> void: call_deferred("_run")

func _run() -> void:
	var sim := _sim(); var hidden := sim.entities[1] as Dictionary
	sim._attach_invisibility_update_contract(hidden, _contract(true, false, false))
	var policy := hidden.get("invisibility_update", {}) as Dictionary
	_check("InvisibilityUpdate_typed_contract_attaches", not policy.is_empty())
	_check("type_and_period_are_exact", String(policy.get("invisibility_type", "")) == "STEALTH" and int(policy.get("update_ticks", 0)) == 2)
	_check("required_and_forbidden_upgrades_preserved", policy.get("required_upgrades", []) == ["Upgrade_Cloak"] and policy.get("forbidden_upgrades", []) == ["Upgrade_MapMode"])
	sim._step_invisibility_updates(); _check("missing_required_upgrade_blocks", not sim._stealth_active(hidden))
	hidden["completed_upgrades"] = ["Upgrade_Cloak"]; _advance(sim, 2); _check("required_upgrade_enables_on_exact_cadence", sim._stealth_active(hidden))
	_check("enter_fx_emitted", _event_payload(sim, "module.invisibility_changed", "fx_id") == "FX_CloakOn")
	hidden["completed_upgrades"] = ["Upgrade_Cloak", "Upgrade_MapMode"]; _advance(sim, 2); _check("forbidden_upgrade_reveals", not sim._stealth_active(hidden))
	hidden["completed_upgrades"] = ["Upgrade_Cloak"]; _advance(sim, 2); _check("untoggle_forbidden_upgrade_requires_reenable", not sim._stealth_active(hidden))
	sim.set_invisibility_update_active(1, true); _advance(sim, 2); _check("cloak_reenters_after_explicit_reenable", sim._stealth_active(hidden))
	hidden["destination"] = Vector2(5, 0); _advance(sim, 2); _check("moving_forbidden_condition_reveals", not sim._stealth_active(hidden))
	_check("untoggle_option_disables_after_leaving_stealth", not bool(policy.get("enabled", true)))
	hidden["destination"] = hidden["position"]; _check("command_api_reenables", bool(sim.set_invisibility_update_active(1, true).get("ok", false))); _advance(sim, 2); _check("reenabled_cloak_returns", sim._stealth_active(hidden))
	sim._break_stealth(hidden, "TAKING_DAMAGE"); _check("damage_breaks_authored_condition", not sim._stealth_active(hidden)); _advance(sim, 2); _check("untoggled_damage_break_stays_off", not sim._stealth_active(hidden))
	sim.set_invisibility_update_active(1, true); hidden["weapon_conditions"] = ["CLOSE_RANGE"]; _advance(sim, 2); _check("forbidden_weapon_condition_blocks", not sim._stealth_active(hidden)); hidden["weapon_conditions"] = []; sim.set_invisibility_update_active(1, true); _advance(sim, 2)
	hidden["model_conditions"] = ["IS_FIRING_WEAPON"]; _advance(sim, 2); _check("hint_detectable_condition_is_exposed", bool(hidden.get("invisibility_hint_detectable", false)))
	_check("hint_is_not_conflated_with_forbidden_condition", sim._stealth_active(hidden)); hidden["model_conditions"] = []
	for isolate_id in [3,4,5]: (sim.entities[isolate_id] as Dictionary)["team"] = 1; sim._spatial_sync(sim.entities[isolate_id] as Dictionary)
	var near_attacker := sim.entities[2] as Dictionary; near_attacker["position"] = Vector2(1.5, 0); sim._spatial_sync(near_attacker); sim._spatial_sync(hidden)
	_check("detection_range_reveals_to_near_attacker", sim._spatial_nearest_hostile(near_attacker, 1, near_attacker["position"], 10.0, Sim.SPATIAL_FILTER_STEALTH) == 1)
	near_attacker["position"] = Vector2(5, 0); sim._spatial_sync(near_attacker); _check("outside_detection_range_remains_hidden", sim._spatial_nearest_hostile(near_attacker, 1, near_attacker["position"], 10.0, Sim.SPATIAL_FILTER_STEALTH) == 0)
	for isolate_id in [3,4,5]: (sim.entities[isolate_id] as Dictionary)["team"] = 0; sim._spatial_sync(sim.entities[isolate_id] as Dictionary)
	var broadcaster := sim.entities[3] as Dictionary; sim._attach_invisibility_update_contract(broadcaster, _contract(true, true, false)); broadcaster["completed_upgrades"] = ["Upgrade_Cloak"]; _advance(sim, 2)
	_check("broadcast_cloaks_matching_ally", sim._stealth_active(sim.entities[4] as Dictionary))
	_check("broadcast_filter_excludes_wrong_kind", not sim._stealth_active(sim.entities[5] as Dictionary))
	(sim.entities[4] as Dictionary)["position"] = Vector2(20, 0); sim._spatial_sync(sim.entities[4] as Dictionary); _advance(sim, 2); _check("leaving_broadcast_range_revokes_source", not sim._stealth_active(sim.entities[4] as Dictionary))
	var digest := sim.state_hash(); var snapshot := sim.snapshot(); var restored := _sim(); _check("snapshot_restores", restored.restore(snapshot)); _check("snapshot_hash_round_trips", restored.state_hash() == digest)
	_check("policy_schedule_restores", int(((restored.entities[1] as Dictionary).get("invisibility_update", {}) as Dictionary).get("next_update_tick", -1)) == int(policy.get("next_update_tick", -2)))
	var opaque := _contract(true, false, false); opaque["extraction"] = "opaque"; var opaque_row := {}; sim._attach_invisibility_update_contract(opaque_row, opaque); _check("opaque_contract_fails_closed", not opaque_row.has("invisibility_update"))
	var malformed := _contract(true, false, false); (malformed["fields"] as Dictionary).erase("InvisibilityNugget"); var malformed_row := {}; sim._attach_invisibility_update_contract(malformed_row, malformed); _check("missing_nugget_fails_closed", not malformed_row.has("invisibility_update"))
	var unresolved := _contract(true, true, true); var unresolved_row := {"id":9,"team":0,"health":100}; sim._attach_invisibility_update_contract(unresolved_row, unresolved); var unresolved_policy := unresolved_row.get("invisibility_update", {}) as Dictionary
	_check("symbolic_range_is_inert", float(unresolved_policy.get("broadcast_range_source", 0.0)) < 0.0)
	_check("symbolic_filter_gap_receipted", (unresolved_policy.get("unsupported_semantics", []) as Array).has("unresolved_broadcast_filter:ELVEN_MIST_OBJECT_FILTER"))
	_check("symbolic_range_gap_receipted", (unresolved_policy.get("unsupported_semantics", []) as Array).has("unresolved_invisibility_define:BroadcastRange:ENSHROUDING_MIST_EFFECT_RADIUS"))
	var tree_contract := _contract(true, false, false); var tree_nugget := (((tree_contract["fields"] as Dictionary)["InvisibilityNugget"] as Array)[0] as Dictionary); (tree_nugget["ForbiddenConditions"] as Dictionary)["value"] = ["AWAY_FROM_TREES", "MOVING"]; var tree_row := {"id":10,"team":0,"health":100,"completed_upgrades":["Upgrade_Cloak"]}; sim._attach_invisibility_update_contract(tree_row, tree_contract); var tree_policy := tree_row.get("invisibility_update", {}) as Dictionary
	_check("tree_geometry_gap_receipted", (tree_policy.get("unsupported_semantics", []) as Array).has("environment-condition-unresolved:AWAY_FROM_TREES"))
	_check("unresolved_tree_geometry_fails_closed", not bool(tree_policy.get("self_executable", true)))
	_check("voice_roles_preserved", String(policy.get("voice_move_role", "")) == "VoiceMoveToTrees" and String(policy.get("voice_enter_role", "")) == "VoiceEnterStateMoveToTrees")
	var support := sim._spellbook_field_ping_support([{"leaf":_ping_leaf()}], {})
	_check("descriptor_invisibility_ping_unlocks", bool(support.get("ok", false)) and not ((support.get("effect", {}) as Dictionary).get("invisibility_updates", []) as Array).is_empty())
	sim.set_invisibility_update_active(3, false)
	(sim.entities[4] as Dictionary)["position"] = Vector2(2, 0); sim._spatial_sync(sim.entities[4] as Dictionary)
	var effect := support.get("effect", {}) as Dictionary; sim._cast_spellbook_field_ping(0, "Mist", effect, Vector2.ZERO); sim._step_field_pings()
	_check("field_ping_broadcast_cloaks_ally", sim._stealth_active(sim.entities[4] as Dictionary))
	_advance(sim, int(effect.get("lifetime_ticks", 1)), true); _check("field_ping_expiry_revokes_broadcast", not sim._stealth_active(sim.entities[4] as Dictionary))
	_check("typed_provenance_preserved", String(policy.get("tag", "")) == "ModuleTag_Cloak" and int(policy.get("line", 0)) == 100)
	if passed + failed != EXPECTED: failed += 1; printerr("INVISIBILITY_UPDATE_RUNTIME_FAIL liveness")
	print("INVISIBILITY_UPDATE_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed]); quit(0 if failed == 0 else 1)

func _contract(starts: bool, broadcast: bool, symbolic: bool) -> Dictionary:
	var nugget := {"sourceIni":"fixture/invisibility.ini","line":110,"InvisibilityType":{"value":"STEALTH"},"ForbiddenConditions":{"value":["MOVING","FIRING_ANY","TAKING_DAMAGE","USING_ABILITY"]},"ForbiddenWeaponConditions":{"value":["CLOSE_RANGE","CONTESTING_BUILDING"]},"HintDetectableConditions":{"value":["IS_FIRING_WEAPON"]},"Options":{"value":["UNTOGGLE_HIDDEN_WHEN_LEAVING_STEALTH"]},"DetectionRange":{"expression":"20","value":20},"BecomeStealthedFX":{"value":"FX_CloakOn"},"ExitStealthFX":{"value":"FX_CloakOff"}}
	var fields := {"StartsActive":{"value":starts},"UpdatePeriod":{"milliseconds":200},"RequiredUpgrades":{"value":["Upgrade_Cloak"]},"ForbiddenUpgrades":{"value":["Upgrade_MapMode"]},"UnitSpecificSoundNameToUseAsVoiceMoveToStealthyArea":{"value":"VoiceMoveToTrees"},"UnitSpecificSoundNameToUseAsVoiceEnterStateMoveToStealthyArea":{"value":"VoiceEnterStateMoveToTrees"},"InvisibilityNugget":[nugget]}
	if broadcast: fields.merge({"Broadcast":{"value":true},"BroadcastRange":{"expression":"ENSHROUDING_MIST_EFFECT_RADIUS"} if symbolic else {"expression":"30","value":30},"BroadcastObjectFilter":{"value":["ELVEN_MIST_OBJECT_FILTER"] if symbolic else ["ANY","+INFANTRY","-STRUCTURE","ALLIES"]}})
	return {"module":"InvisibilityUpdate","runtimeStatus":"deferred","extraction":"typed","tag":"ModuleTag_Cloak","sourceIni":"fixture/invisibility.ini","line":100,"fields":fields}

func _ping_leaf() -> Dictionary:
	return {"id":"EnshroudingMistPing","kindOf":["IMMOBILE","UNATTACKABLE"],"lifetime":{"maxMs":500},"visionRange":1.0,"auras":[],"invisibilityUpdates":[{"broadcast":"Yes","broadcastObjectFilter":"ANY +INFANTRY ALLIES","broadcastRange":30,"detectionRange":20.0,"invisibilityType":"CAMOUFLAGE","startsActive":"Yes","updatePeriodMs":100}]}

func _sim() -> RetailSliceSim:
	var rule := {"horde_id":Sim.SOLDIER_HORDE_ID,"category":"infantry","speed":1.0,"speed_source":10.0,"acceleration":1.0,"acceleration_source":10.0,"turn_rate_degrees_per_second":180.0,"braking":1.0,"braking_source":10.0,"attack_range":0.1,"attack_range_source":1.0,"minimum_attack_range":0.0,"minimum_attack_range_source":0.0,"vision_range":10.0,"vision_range_source":100.0,"delay_between_shots_ms":1000.0,"pre_attack_delay_ms":0.0,"firing_duration_ms":0.0,"attack_period_ticks":10,"pre_attack_ticks":0,"firing_duration_ticks":0,"member_damage":1,"member_health":100,"member_count":1,"formation_positions":[Vector3.ZERO],"provenance":{}}
	var rules := {}; for object_id in [Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,Sim.ARCHER_OBJECT_ID,Sim.TOWER_GUARD_OBJECT_ID,Sim.KNIGHT_OBJECT_ID]: rules[object_id]=rule
	var sim: RetailSliceSim=Sim.new();sim.setup({}, {"unit_rules":rules,"source_unit_scale":0.1});sim.ai_enabled=false;sim.base_loop_enabled=false;sim.entities.clear();sim.structures.clear()
	sim.entities[1]={"id":1,"team":0,"health":100,"position":Vector2.ZERO,"destination":Vector2.ZERO,"state":"idle","category":"infantry","kind_of":["INFANTRY"],"completed_upgrades":[]}
	sim.entities[2]={"id":2,"team":1,"health":100,"position":Vector2(5,0),"destination":Vector2(5,0),"state":"idle","category":"infantry","kind_of":["INFANTRY"]}
	sim.entities[3]={"id":3,"team":0,"health":100,"position":Vector2.ZERO,"destination":Vector2.ZERO,"state":"idle","category":"hero","kind_of":["HERO"],"completed_upgrades":[]}
	sim.entities[4]={"id":4,"team":0,"health":100,"position":Vector2(2,0),"destination":Vector2(2,0),"state":"idle","category":"infantry","kind_of":["INFANTRY"]}
	sim.entities[5]={"id":5,"team":0,"health":100,"position":Vector2(2,0),"destination":Vector2(2,0),"state":"idle","category":"structure","kind_of":["STRUCTURE"]}
	for id in sim.entity_ids():sim._spatial_sync(sim.entities[id] as Dictionary)
	return sim

func _event_payload(sim: RetailSliceSim, kind: String, key: String) -> Variant:
	for event in sim.events: if String((event as Dictionary).get("kind", "")) == kind: return (event as Dictionary).get(key)
	return null

func _advance(sim: RetailSliceSim, ticks: int, field_pings: bool = false) -> void:
	for unused in ticks:
		sim.tick_index += 1
		sim._step_invisibility_updates()
		if field_pings: sim._step_field_pings()

func _check(name:String,condition:bool)->void:
	if condition:passed+=1
	else:failed+=1;push_error("INVISIBILITY_UPDATE_RUNTIME_FAIL "+name)

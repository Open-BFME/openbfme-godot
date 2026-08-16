extends SceneTree

# Executable receipt: WeaponModeSpecialPowerUpdate typed contract and its
# compiler-emitted weapon-mode-special-power graph reach RetailSliceSim.

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const EXPECTED := 30
var passed := 0
var failed := 0


func _initialize() -> void:
	create_timer(30.0).timeout.connect(_watchdog)
	call_deferred("_run")


func _watchdog() -> void:
	push_error("WEAPON_MODE_SPECIAL_POWER_RUNTIME_FAIL watchdog exceeded 30 seconds")
	quit(1)


func _run() -> void:
	var sim := _sim()
	_spawn(sim, 1, 0)
	var row := sim.entities[1] as Dictionary
	sim._attach_weapon_mode_special_power_contract(row, _contract(true))
	var policies := row.get("weapon_mode_special_powers", []) as Array
	_check("typed_contract_attaches_once", policies.size() == 1)
	var policy := _policy(row)
	_check("typed_identity_and_duration", String(policy.get("special_power_template", "")) == "SpecialAbilityFixtureMode" and int(policy.get("duration_ticks", 0)) == 3)
	_check("authored_starts_paused", bool(policy.get("starts_paused", false)) and bool(policy.get("paused", false)))
	_check("ordered_flag_resolves_profile", policy.get("weapon_set_flags", []) == ["WEAPONSET_TOGGLE_1"] and String(policy.get("mode", "")) == "weaponset_toggle_1")
	_check("resolved_modifier_is_bound", String(policy.get("modifier_name", "")) == "ModifierFixtureMode" and (policy.get("unsupported_semantics", []) as Array).is_empty())
	sim._attach_weapon_mode_special_power_contract(row, _contract(true))
	_check("contract_attach_dedupes", (row.get("weapon_mode_special_powers", []) as Array).size() == 1)
	var paused := sim.activate_weapon_mode_special_power(1, "SpecialAbilityFixtureMode", 0)
	_check("paused_activation_fails_closed", String(paused.get("reason", "")) == "special-power-paused")
	_check("wrong_owner_fails_closed", String(sim.activate_weapon_mode_special_power(1, "SpecialAbilityFixtureMode", 1).get("reason", "")) == "wrong-owner")
	_check("command_api_unpauses", bool(sim.set_weapon_mode_special_power_paused(1, "SpecialAbilityFixtureMode", false).get("ok", false)))
	(row.get("member_attack_start_ticks", []) as Array)[0] = 9
	(row.get("member_attack_hit_ticks", []) as Array)[0] = 10
	var activated := sim.activate_weapon_mode_special_power(1, "SpecialAbilityFixtureMode", 0)
	_check("activation_succeeds", bool(activated.get("ok", false)) and int(activated.get("expires_tick", -1)) == 3)
	_check("weapon_profile_applies", String(row.get("active_weapon_mode", "")) == "weaponset_toggle_1" and int(row.get("member_damage", 0)) == 30)
	_check("mode_switch_clears_attack_schedule", int((row.get("member_attack_start_ticks", []) as Array)[0]) == -1 and int((row.get("member_attack_hit_ticks", []) as Array)[0]) == -1)
	_check("modifier_applies", is_equal_approx(sim._ability_outgoing_multiplier(row), 1.5))
	_check("start_event_emitted", _has_event(sim, "ability.weapon_mode_started"))
	sim.tick_index = 2; sim._step_weapon_mode_special_powers(row)
	_check("duration_does_not_expire_early", String(row.get("active_weapon_mode", "")) == "weaponset_toggle_1" and bool(_policy(row).get("active", false)))
	var snap := sim.snapshot(); var hash := sim.state_hash(); var restored := _sim()
	_check("snapshot_restores_active_mode", restored.restore(snap) and String((restored.entities[1] as Dictionary).get("active_weapon_mode", "")) == "weaponset_toggle_1")
	_check("active_mode_hash_round_trips", restored.state_hash() == hash)
	sim.tick_index = 3; sim._step_weapon_mode_special_powers(row)
	restored.tick_index = 3; restored._step_weapon_mode_special_powers(restored.entities[1] as Dictionary)
	_check("exact_expiry_restores_prior_mode", String(row.get("active_weapon_mode", "")) == "default" and int(row.get("member_damage", 0)) == 10)
	_check("expiry_removes_modifier", is_equal_approx(sim._ability_outgoing_multiplier(row), 1.0) and not row.has("timed_modifiers"))
	_check("restored_expiry_is_identical", restored.state_hash() == sim.state_hash())
	_check("finish_event_emitted", _has_event(sim, "ability.weapon_mode_finished"))

	var secondary := _sim(); _spawn(secondary, 1, 0); var secondary_row := secondary.entities[1] as Dictionary
	secondary._attach_weapon_mode_special_power_contract(secondary_row, _contract(false, [], "SECONDARY"))
	_check("secondary_slot_resolves_profile", String(_policy(secondary_row).get("mode", "")) == "knife")
	_check("secondary_slot_activates", bool(secondary.activate_weapon_mode_special_power(1, "SpecialAbilityFixtureMode", 0).get("ok", false)) and String(secondary_row.get("active_weapon_mode", "")) == "knife")

	var unresolved := _sim(); _spawn(unresolved, 1, 0); var unresolved_row := unresolved.entities[1] as Dictionary
	var define_contract := _contract(false); (define_contract.get("fields", {}) as Dictionary)["Duration"] = {"define":"MISSING_DURATION","sourceIni":"fixture.ini","line":4}
	unresolved._attach_weapon_mode_special_power_contract(unresolved_row, define_contract)
	_check("unresolved_define_receipted", String(unresolved.activate_weapon_mode_special_power(1, "SpecialAbilityFixtureMode", 0).get("reason", "")).begins_with("unresolved_duration_define:"))

	var graph := _graph(true)
	_check("adapter_accepts_compiler_graph", Adapter.ability_rules(_adapter_doc(graph)).size() == 1)
	var malformed := graph.duplicate(true); malformed["lockWeaponSlot"] = "PRIMARY"
	_check("adapter_rejects_bad_slot", Adapter.ability_rules(_adapter_doc(malformed)).is_empty())
	var no_effect := graph.duplicate(true); no_effect.erase("weaponSetFlags"); no_effect.erase("attributeModifier")
	_check("adapter_rejects_effectless_graph", Adapter.ability_rules(_adapter_doc(no_effect)).is_empty())

	var cast_sim := _sim(); cast_sim._unit_ability_rules[Sim.SOLDIER_HORDE_ID] = cast_sim._scaled_ability_rules([_ability(graph)], 0.1); _spawn(cast_sim, 1, 0); cast_sim._attach_hero_ability_state(cast_sim.entities[1] as Dictionary)
	var cast_row := cast_sim.entities[1] as Dictionary
	var first_cast := cast_sim.cast_ability(1, "Command_FixtureMode", Vector2.ZERO, 0)
	_check("descriptor_pause_does_not_arm_cooldown", String(first_cast.get("reason", "")) == "special-power-paused" and int((((cast_row.get("ability_states", {}) as Dictionary).get("Command_FixtureMode", {}) as Dictionary).get("cooldown_ready_tick", -1))) == 0)
	cast_sim.set_weapon_mode_special_power_paused(1, "SpecialAbilityFixtureMode", false)
	var cast := cast_sim.cast_ability(1, "Command_FixtureMode", Vector2.ZERO, 0)
	_check("cast_path_activates_and_arms_cooldown", bool(cast.get("ok", false)) and String(cast_row.get("active_weapon_mode", "")) == "weaponset_toggle_1" and int((((cast_row.get("ability_states", {}) as Dictionary).get("Command_FixtureMode", {}) as Dictionary).get("cooldown_ready_tick", 0))) == 10)
	var unsupported_graph := _graph(false); ((unsupported_graph.get("attributeModifier", {}) as Dictionary)["unsupportedModifiers"] as Array).append("INVENTED_KIND")
	var unsupported_sim := _sim(); unsupported_sim._unit_ability_rules[Sim.SOLDIER_HORDE_ID] = unsupported_sim._scaled_ability_rules([_ability(unsupported_graph)], 0.1); _spawn(unsupported_sim, 1, 0); unsupported_sim._attach_hero_ability_state(unsupported_sim.entities[1] as Dictionary)
	_check("unsupported_modifier_fails_closed", String(unsupported_sim.cast_ability(1, "Command_FixtureMode", Vector2.ZERO, 0).get("reason", "")) == "unsupported_modifier_kind:INVENTED_KIND")
	finish()


func _sim() -> RetailSliceSim:
	var rules := {}; for object_id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]: rules[object_id] = _rule()
	var sim: RetailSliceSim = Sim.new()
	sim.setup({}, {"unit_rules":rules, "source_map_transform_scale":0.1, "attribute_modifier_rules":{"ModifierFixtureMode":{"category":"HERO_MODE", "effects":[{"kind":"DAMAGE_MULT", "value":1.5, "application":"multiplicative"}]}}})
	sim.ai_enabled = false; sim.base_loop_enabled = false; sim.entities.clear(); sim.structures.clear()
	return sim


func _spawn(sim: RetailSliceSim, id: int, team: int) -> void:
	sim._add_battalion(id, team, Vector2.ZERO, "Fixture", Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, 0, _rule())


func _rule() -> Dictionary:
	var base := {"name":"default","weapon_slot":"primary","attack_range":1.0,"attack_range_source":10.0,"minimum_attack_range":0.0,"minimum_attack_range_source":0.0,"delay_between_shots_ms":1000.0,"pre_attack_delay_ms":0.0,"firing_duration_ms":0.0,"attack_period_ticks":10,"pre_attack_ticks":0,"firing_duration_ticks":0,"member_damage":10}
	var toggled := base.duplicate(true); toggled["name"]="weaponset_toggle_1"; toggled["member_damage"]=30
	var knife := base.duplicate(true); knife["name"]="knife"; knife["weapon_slot"]="secondary"; knife["member_damage"]=40
	return {"horde_id":Sim.SOLDIER_HORDE_ID,"category":"hero","speed":1.0,"speed_source":10.0,"acceleration":1.0,"acceleration_source":10.0,"turn_rate_degrees_per_second":180.0,"braking":1.0,"braking_source":10.0,"attack_range":1.0,"attack_range_source":10.0,"minimum_attack_range":0.0,"minimum_attack_range_source":0.0,"vision_range":20.0,"vision_range_source":200.0,"delay_between_shots_ms":1000.0,"pre_attack_delay_ms":0.0,"firing_duration_ms":0.0,"attack_period_ticks":10,"pre_attack_ticks":0,"firing_duration_ticks":0,"member_damage":10,"member_health":100,"member_count":1,"formation_positions":[Vector3.ZERO],"default_weapon_mode":"default","weapon_modes":{"default":base,"weaponset_toggle_1":toggled,"knife":knife},"provenance":{}}


func _contract(starts_paused: bool, flags: Array = ["WEAPONSET_TOGGLE_1"], lock_slot: String = "") -> Dictionary:
	var fields := {"SpecialPowerTemplate":{"value":"SpecialAbilityFixtureMode","sourceIni":"fixture.ini","line":2},"Duration":{"milliseconds":300,"sourceIni":"fixture.ini","line":3},"StartsPaused":{"value":starts_paused,"sourceIni":"fixture.ini","line":4},"AttributeModifier":{"value":"ModifierFixtureMode","sourceIni":"fixture.ini","line":5}}
	if not flags.is_empty(): fields["WeaponSetFlags"]={"value":flags.duplicate(),"sourceIni":"fixture.ini","line":6}
	if lock_slot != "": fields["LockWeaponSlot"]={"value":lock_slot,"sourceIni":"fixture.ini","line":7}
	return {"module":"WeaponModeSpecialPowerUpdate","runtimeStatus":"deferred","extraction":"typed","carrier":"unit","tag":"ModuleTag_FixtureMode","sourceIni":"fixture.ini","line":1,"fields":fields}


func _graph(starts_paused: bool) -> Dictionary:
	return {"kind":"weapon-mode-special-power","specialPowerTemplateId":"SpecialAbilityFixtureMode","durationMs":300,"startsPaused":starts_paused,"weaponSetFlags":["WEAPONSET_TOGGLE_1"],"attributeModifier":{"id":"ModifierFixtureMode","category":"HERO_MODE","modifiers":[{"kind":"DAMAGE_MULT","value":1.5,"application":"multiplicative"}],"unsupportedModifiers":[],"sourceIni":"modifier.ini","line":1},"sourceIni":"specialpower.ini","line":1}


func _ability(graph: Dictionary) -> Dictionary:
	return {"ability_id":"Command_FixtureMode","special_power_id":"SpecialAbilityFixtureMode","targeting":"self","cooldown_ticks":10,"required_level":1,"level_gate_resolved":true,"castable":true,"effect":graph,"special_power_contract":{}}


func _adapter_doc(graph: Dictionary) -> Dictionary:
	return {"registration":{"abilities":[{"id":"Command_FixtureMode","slot":1,"targeting":"self","specialPowerId":"SpecialAbilityFixtureMode","cooldownMs":1000,"button":{},"effect":graph,"implementation":{"status":"implemented","reason":"","limitations":[]},"levelGate":{}}]}}


func _policy(row: Dictionary) -> Dictionary:
	var values := row.get("weapon_mode_special_powers", []) as Array
	return values[0] as Dictionary if not values.is_empty() else {}


func _has_event(sim: RetailSliceSim, kind: String) -> bool:
	return sim.events.any(func(event: Dictionary) -> bool: return String(event.get("kind", "")) == kind)


func _check(label: String, condition: bool) -> void:
	if condition: passed += 1
	else: failed += 1; push_error("WEAPON_MODE_SPECIAL_POWER_RUNTIME_FAIL " + label)


func finish() -> void:
	var ran := passed + failed
	if ran != EXPECTED: failed += 1; push_error("WEAPON_MODE_SPECIAL_POWER_RUNTIME_FAIL liveness ran=%d expected=%d" % [ran, EXPECTED])
	print("WEAPON_MODE_SPECIAL_POWER_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)

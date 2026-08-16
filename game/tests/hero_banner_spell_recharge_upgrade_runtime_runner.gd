extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED := 32
var passed := 0
var failed := 0

func _initialize() -> void: call_deferred("_run")

func _run() -> void:
	var sim := _sim()
	var tower := sim.structures[50] as Dictionary
	var signal_fire := sim.structures[60] as Dictionary
	var banner_policy := tower.get("allow_banner_spawn_upgrade", {}) as Dictionary
	var recharge_policy := signal_fire.get("spell_recharge_modifier_upgrade", {}) as Dictionary
	_check("AllowBannerSpawnUpgrade_typed_contract_attaches", not banner_policy.is_empty())
	_check("banner_trigger_is_exact", banner_policy.get("triggered_by", []) == ["Upgrade_MenFortressHouseOfHealing"])
	_check("banner_gate_starts_inactive", not sim.structure_allows_banner_spawn(50))
	(tower.get("completed_upgrades", []) as Array).append("Upgrade_MenFortressHouseOfHealing")
	_check("banner_gate_activates_from_local_upgrade", sim.structure_allows_banner_spawn(50))
	(tower.get("completed_upgrades", []) as Array).erase("Upgrade_MenFortressHouseOfHealing")
	_check("banner_gate_deactivates_when_upgrade_removed", not sim.structure_allows_banner_spawn(50))
	_check("SpellRechargeModifierUpgrade_typed_contract_attaches", not recharge_policy.is_empty())
	_check("ordered_percentages_are_exact", recharge_policy.get("percentages", []) == [-25, -35, -50])
	_check("starts_active_is_exact", bool(recharge_policy.get("starts_active", false)))
	_check("label_is_presentation_receipt", (recharge_policy.get("unsupported_semantics", []) as Array).has("presentation-label:GUI:SpellBookRefreshReduction"))
	_check("in_flight_rescale_gap_is_explicit", (recharge_policy.get("unsupported_semantics", []) as Array).has("in-flight-cooldown-rescale-unresolved"))
	_check("one_signal_fire_uses_first_percentage", sim.spell_recharge_ticks_for_team(0, 100) == 75)
	var cast_sim := _sim(); cast_sim._team_spellbooks[0] = {"ready":true,"powers":{"FixtureHeal":{"castable":true,"reload_ticks":100,"science_id":"Science_Fixture","effect":{"kind":"heal","radius_source":100.0,"amount":0.5,"as_percent":true,"affects":"ANY"}}}}; cast_sim.purchased_powers[0] = ["FixtureHeal"]; cast_sim._power_cooldown_until[0] = {}; var wounded := cast_sim.entities[1] as Dictionary; wounded["health"] = 50; wounded["member_health"] = [50]
	_check("cast_path_accepts_recharge_modified_power", bool(cast_sim.cast_power(0, "FixtureHeal", Vector2.ZERO).get("ok", false)))
	_check("cast_path_arms_modified_cooldown", int((cast_sim._power_cooldown_until[0] as Dictionary).get("FixtureHeal", 0)) - cast_sim.tick_index == 75)
	_check("cooldown_state_reports_modified_total", int(cast_sim.power_cooldown_state(0, "FixtureHeal").get("total_ticks", 0)) == 75)
	var signal2 := signal_fire.duplicate(true); signal2["id"] = 61; sim.structures[61] = signal2
	_check("two_signal_fires_use_second_percentage", sim.spell_recharge_ticks_for_team(0, 100) == 65)
	var signal3 := signal_fire.duplicate(true); signal3["id"] = 62; sim.structures[62] = signal3
	_check("three_signal_fires_use_third_percentage", sim.spell_recharge_ticks_for_team(0, 100) == 50)
	var signal4 := signal_fire.duplicate(true); signal4["id"] = 63; sim.structures[63] = signal4
	_check("extra_signal_fires_clamp_to_last_authored_percentage", sim.spell_recharge_ticks_for_team(0, 100) == 50)
	(signal2 as Dictionary)["team"] = 1; (signal3 as Dictionary)["health"] = 0; (signal4 as Dictionary)["spell_recharge_modifier_upgrade"] = (recharge_policy.duplicate(true)); ((signal4 as Dictionary)["spell_recharge_modifier_upgrade"] as Dictionary)["active"] = false
	_check("ownership_death_and_active_state_filter_instances", sim.spell_recharge_ticks_for_team(0, 100) == 75)
	_check("signed_recharge_rounding_is_deterministic", sim.spell_recharge_ticks_for_team(0, 7) == 5)
	var digest := sim.state_hash(); var snapshot := sim.snapshot(); var restored := _sim()
	_check("snapshot_restores", restored.restore(snapshot))
	_check("snapshot_hash_round_trips", restored.state_hash() == digest)
	_check("banner_upgrade_removal_restores", not restored.structure_allows_banner_spawn(50))
	_check("recharge_instance_state_restores", restored.spell_recharge_ticks_for_team(0, 100) == 75)
	var horde := restored.entities[1] as Dictionary; horde["level"] = 2; restored.entity_container[1] = 50; restored.containment[50] = [1]
	restored._refresh_banner_carrier_state(horde)
	_check("contained_horde_banner_is_blocked_without_upgrade", int(horde.get("banner_entity_id", 0)) == 0)
	(restored.structures[50] as Dictionary)["completed_upgrades"] = ["Upgrade_MenFortressHouseOfHealing"]
	restored._refresh_banner_carrier_state(horde)
	_check("contained_horde_banner_spawns_after_upgrade", int(horde.get("banner_entity_id", 0)) != 0)
	var opaque := _banner_contract(); opaque["extraction"] = "opaque"; var opaque_row := {"id": 70}; sim._attach_allow_banner_spawn_upgrade_contract(opaque_row, opaque)
	_check("opaque_banner_contract_fails_closed", not opaque_row.has("allow_banner_spawn_upgrade"))
	var malformed := _recharge_contract(); (malformed["fields"] as Dictionary)["Percentage"] = []; var malformed_row := {"id": 71}; sim._attach_spell_recharge_modifier_upgrade_contract(malformed_row, malformed)
	_check("missing_percentage_fails_closed", not malformed_row.has("spell_recharge_modifier_upgrade"))
	var inactive := _recharge_contract(); ((inactive["fields"] as Dictionary)["StartsActive"] as Dictionary)["value"] = false; var inactive_row := {"id": 72, "team": 0, "health": 100}; sim._attach_spell_recharge_modifier_upgrade_contract(inactive_row, inactive); sim.structures[72] = inactive_row
	_check("starts_inactive_contract_does_not_modify", sim.spell_recharge_ticks_for_team(0, 100) == 75)
	var hero_row := {"id": 80}; sim._attach_buildable_hero_list_upgrade_contract(hero_row, _hero_contract())
	var hero_policy := hero_row.get("buildable_hero_list_upgrade", {}) as Dictionary
	_check("BuildableHeroListUpgrade_typed_contract_is_preserved", hero_policy.get("triggered_by", []) == ["Upgrade_RingHero"])
	_check("system_spellbook_binding_gap_is_explicit", (hero_policy.get("unsupported_semantics", []) as Array).has("system-spellbook-object-not-instantiated"))
	var bad_hero := _hero_contract(); (bad_hero["fields"] as Dictionary).erase("TriggeredBy"); var bad_hero_row := {}; sim._attach_buildable_hero_list_upgrade_contract(bad_hero_row, bad_hero)
	_check("missing_hero_trigger_fails_closed", not bad_hero_row.has("buildable_hero_list_upgrade"))
	_check("module_provenance_preserved", String(recharge_policy.get("tag", "")) == "ModuleTag_MakeSpellsRefreshFaster" and int(recharge_policy.get("line", 0)) == 258)
	if passed + failed != EXPECTED: failed += 1; printerr("HERO_BANNER_RECHARGE_UPGRADE_RUNTIME_FAIL liveness")
	print("HERO_BANNER_RECHARGE_UPGRADE_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed]); quit(0 if failed == 0 else 1)

func _banner_contract() -> Dictionary:
	return {"module":"AllowBannerSpawnUpgrade","runtimeStatus":"deferred","extraction":"typed","tag":"ModuleTag_AllowRespawning","sourceIni":"fixture/garrisontower.ini","line":321,"fields":{"TriggeredBy":{"authored":"Upgrade_MenFortressHouseOfHealing","value":["Upgrade_MenFortressHouseOfHealing"],"sourceIni":"fixture/garrisontower.ini","line":322}}}

func _recharge_contract() -> Dictionary:
	return {"module":"SpellRechargeModifierUpgrade","runtimeStatus":"deferred","extraction":"typed","tag":"ModuleTag_MakeSpellsRefreshFaster","sourceIni":"fixture/signalfire.ini","line":258,"fields":{"LabelForPalantirString":{"value":"GUI:SpellBookRefreshReduction"},"StartsActive":{"value":true},"Percentage":[{"authored":"-25%","value":-25},{"authored":"-35%","value":-35},{"authored":"-50%","value":-50}]}}

func _hero_contract() -> Dictionary:
	return {"module":"BuildableHeroListUpgrade","runtimeStatus":"deferred","extraction":"typed","tag":"BitsUpgrade","sourceIni":"fixture/system.ini","line":743,"fields":{"TriggeredBy":{"value":["Upgrade_RingHero"]}}}

func _sim() -> RetailSliceSim:
	var unit_rule := {"horde_id":Sim.SOLDIER_HORDE_ID,"category":"infantry","speed":1.0,"speed_source":10.0,"acceleration":1.0,"acceleration_source":10.0,"turn_rate_degrees_per_second":180.0,"braking":1.0,"braking_source":10.0,"attack_range":0.1,"attack_range_source":1.0,"minimum_attack_range":0.0,"minimum_attack_range_source":0.0,"vision_range":1.0,"vision_range_source":10.0,"delay_between_shots_ms":1000.0,"pre_attack_delay_ms":0.0,"firing_duration_ms":0.0,"attack_period_ticks":10,"pre_attack_ticks":0,"firing_duration_ticks":0,"member_damage":1,"member_health":100,"member_count":1,"formation_positions":[Vector3.ZERO],"provenance":{}}
	var unit_rules := {}; for object_id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]: unit_rules[object_id] = unit_rule
	var sim: RetailSliceSim = Sim.new(); sim.setup({}, {"unit_rules": unit_rules}); sim.ai_enabled = false; sim.base_loop_enabled = false; sim.entities.clear(); sim.structures.clear()
	sim.register_structure_module_contracts("Tower", [_banner_contract()]); sim.register_structure_module_contracts("SignalFire", [_recharge_contract()])
	sim.structures[50] = {"id":50,"team":0,"source_object_id":"Tower","health":100,"maximum_health":100,"completed_upgrades":[]}; sim._attach_structure_module_contracts(sim.structures[50] as Dictionary)
	sim.structures[60] = {"id":60,"team":0,"source_object_id":"SignalFire","health":100,"maximum_health":100,"completed_upgrades":[]}; sim._attach_structure_module_contracts(sim.structures[60] as Dictionary)
	sim._unit_banner_carriers["FixtureHorde"] = {"object_id":"FixtureBanner","banner_max_health":50,"min_level":2,"offset_source":Vector2.ZERO}
	sim.entities[1] = {"id":1,"team":0,"unit_type":"FixtureHorde","object_id":"FixtureHorde","health":100,"maximum_health":100,"member_health":[100],"member_maximum_health":100,"level":1,"position":Vector2.ZERO,"facing":Vector2.RIGHT}
	return sim

func _check(name: String, condition: bool) -> void:
	if condition: passed += 1
	else: failed += 1; push_error("HERO_BANNER_RECHARGE_UPGRADE_RUNTIME_FAIL " + name)

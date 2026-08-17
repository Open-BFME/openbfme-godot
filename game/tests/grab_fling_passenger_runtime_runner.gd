extends SceneTree

# Executable receipts: GrabPassengerSpecialPower and
# FlingPassengerSpecialAbilityUpdate descriptor graphs.
const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const EXPECTED := 29
var passed := 0
var failed := 0


func _initialize() -> void:
	create_timer(30.0).timeout.connect(func() -> void: push_error("GRAB_FLING_RUNTIME_FAIL watchdog"); quit(1))
	call_deferred("_run")


func _run() -> void:
	var graph := _grab_graph()
	_check("adapter_accepts_expanded_grab_graph", Adapter.ability_rules(_doc(graph)).size() == 1)
	var malformed := graph.duplicate(true); malformed.erase("containment")
	_check("adapter_rejects_grab_without_containment", Adapter.ability_rules(_doc(malformed)).is_empty())
	var bad_fling := _fling_graph(); bad_fling.erase("landingWarhead")
	_check("adapter_rejects_unpaired_fling_trajectory", Adapter.ability_rules(_doc(bad_fling)).is_empty())

	var sim := _sim()
	sim._unit_ability_rules[Sim.SOLDIER_HORDE_ID] = sim._scaled_ability_rules([_ability(graph)], 0.1)
	_spawn(sim, 1, 0, Vector2.ZERO, "MountainTroll", ["MONSTER"])
	_spawn(sim, 2, Sim.NEUTRAL_TEAM, Vector2(0.5, 0), "TreeOak", ["CLUB", "TREE", "SELECTABLE"])
	_spawn(sim, 3, Sim.NEUTRAL_TEAM, Vector2(0.4, 0), "OrcClub", ["CLUB", "ORC"])
	_spawn(sim, 4, 1, Vector2.ZERO, "EnemyInfantry", ["INFANTRY"])
	sim._attach_hero_ability_state(sim.entities[1] as Dictionary)
	(sim.entities[1] as Dictionary)["health"] = 50; (sim.entities[1] as Dictionary)["maximum_health"] = 100
	var scaled := ((sim._unit_ability_rules[Sim.SOLDIER_HORDE_ID] as Array)[0] as Dictionary).get("effect", {}) as Dictionary
	_check("grab_range_scales", is_equal_approx(float(scaled.get("range", 0.0)), 0.8))
	_check("grab_timers_scale_individually", ((scaled.get("acquire", {}) as Dictionary).get("timing_ticks", {}) as Dictionary) == {"UnpackTime":1,"PreparationTime":1,"PersistentPrepTime":2,"PackTime":1})
	_check("nested_fling_graph_scales", (((scaled.get("releaseAbilities", []) as Array)[0] as Dictionary).get("timing_ticks", {}) as Dictionary) == {"UnpackTime":2,"PackTime":1})
	var cast := sim.cast_ability(1, "Command_Grab", Vector2(0.5, 0), 0)
	_check("grab_cast_schedules", bool(cast.get("ok", false)) and int(cast.get("target_id", 0)) == 2)
	var channel := (sim.entities[1] as Dictionary).get("grab_passenger_channel", {}) as Dictionary
	_check("grab_uses_authored_animation_trigger", int(channel.get("trigger_tick", -1)) == 4)
	_check("grab_finish_preserves_all_authored_phases", int(channel.get("finish_tick", -1)) == 10)
	_check("update_module_starts_attack_targets_passenger", int((sim.entities[1] as Dictionary).get("target_id", 0)) == 2)
	sim.tick_index = 3; sim._step_grab_passenger(sim.entities[1] as Dictionary)
	_check("grab_does_not_trigger_early", not sim.entity_container.has(2))
	var before := sim.snapshot(); var before_hash := sim.state_hash(); var restored := _sim()
	_check("grab_channel_snapshot_hash_restores", restored.restore(before) and restored.state_hash() == before_hash)
	sim.tick_index = 4; sim._step_grab_passenger(sim.entities[1] as Dictionary)
	restored.tick_index = 4; restored._step_grab_passenger(restored.entities[1] as Dictionary)
	_check("tree_enters_entity_container", int(sim.entity_container.get(2, -1)) == 1 and sim.passenger_count(1) == 1)
	_check("contained_status_is_applied", bool(((sim.entities[2] as Dictionary).get("object_status", {}) as Dictionary).get("UNSELECTABLE", false)))
	_check("orc_is_rejected_by_manual_filter", not sim.entity_container.has(3))
	_check("weapon_set_and_state_receipts_bind", (sim.entities[1] as Dictionary).get("grab_weapon_set_types", []) == ["CLUB"] and (sim.entities[1] as Dictionary).get("grab_weapon_state_types", []) == ["CLUB"])
	_check("authored_grab_heal_percent_applies", int((sim.entities[1] as Dictionary).get("health", 0)) == 70)
	_check("grab_trigger_snapshot_path_identical", sim.state_hash() == restored.state_hash())
	sim.tick_index = 10; sim._step_grab_passenger(sim.entities[1] as Dictionary)
	_check("grab_channel_finishes_exactly", not (sim.entities[1] as Dictionary).has("grab_passenger_channel"))
	_check("occupied_grab_capacity_refuses", String(sim._apply_ability_grab_passenger(sim.entities[1] as Dictionary, "Command_Grab", scaled, Vector2(0.5,0)).get("reason", "")) == "capacity-full")

	sim.tick_index = 10
	var release := sim.release_grabbed_passenger(1)
	_check("nested_release_schedules_fling", bool(release.get("ok", false)))
	(sim.entities[1] as Dictionary)["order_sequence"] = 99
	sim.tick_index = 11; sim._step_fling_passenger(sim.entities[1] as Dictionary)
	_check("must_finish_ignores_order_interrupt", (sim.entities[1] as Dictionary).has("fling_passenger_channel"))
	var fling_snapshot := sim.snapshot(); var fling_hash := sim.state_hash(); var fling_restored := _sim()
	_check("fling_channel_snapshot_hash_restores", fling_restored.restore(fling_snapshot) and fling_restored.state_hash() == fling_hash)
	sim.tick_index = 12; sim._step_fling_passenger(sim.entities[1] as Dictionary)
	fling_restored.tick_index = 12; fling_restored._step_fling_passenger(fling_restored.entities[1] as Dictionary)
	_check("fling_consumes_contained_tree_identity", not sim.entities.has(2) and not sim.entity_container.has(2))
	_check("fling_materializes_deterministic_physics_object", sim.physics_objects.size() == 1 and sim.state_hash() == fling_restored.state_hash())
	var physics_id := int(sim.physics_objects.keys()[0])
	var physics := sim.physics_objects[physics_id] as Dictionary
	_check("fling_preserves_velocity_and_warhead", is_equal_approx(float(physics.get("vertical_velocity_source", -1.0)), 10.0) and String((physics.get("landing_warhead", {}) as Dictionary).get("id", "")) == "TreeLandingWarhead")
	for step in range(13, 20):
		sim.tick_index = step; sim._step_physics_objects()
		if not sim.physics_objects.has(physics_id): break
	_check("landing_force_kill_filter_executes", int((sim.entities[4] as Dictionary).get("health", 1)) <= 0)
	_check("landing_warhead_receipt_emits", _has_event(sim, "ability.fling_landed"))

	var death_sim := _sim(); death_sim._unit_ability_rules[Sim.SOLDIER_HORDE_ID]=death_sim._scaled_ability_rules([_ability(graph)],0.1); _spawn(death_sim,1,0,Vector2.ZERO,"MountainTroll",["MONSTER"]); _spawn(death_sim,2,Sim.NEUTRAL_TEAM,Vector2(0.5,0),"TreeOak",["CLUB","TREE","SELECTABLE"]); death_sim._attach_hero_ability_state(death_sim.entities[1] as Dictionary); death_sim.cast_ability(1,"Command_Grab",Vector2(0.5,0),0); death_sim.tick_index=4; death_sim._step_grab_passenger(death_sim.entities[1] as Dictionary); (death_sim.entities[1] as Dictionary)["health"]=0; death_sim._step_grab_passenger(death_sim.entities[1] as Dictionary)
	_check("carrier_death_ejects_grabbed_passenger", not death_sim.entity_container.has(2) and String((death_sim.entities[2] as Dictionary).get("state", "")) == "idle")
	_finish()


func _sim() -> RetailSliceSim:
	var sim: RetailSliceSim = Sim.new()
	var rules := {}
	for object_id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]: rules[object_id] = _fixture_rule("Baseline", ["INFANTRY"])
	sim.setup({}, {"unit_rules": rules, "faction_manifest": {"structure_armor": _fixture_structure_armor()}, "source_map_transform_scale": 0.1, "physics_gravity_source_per_second_squared": 100.0})
	sim.ai_enabled=false; sim.base_loop_enabled=false; sim.entities.clear(); sim.structures.clear()
	return sim


func _fixture_structure_armor() -> Dictionary:
	var armor := {}
	for kind_value in Sim.STRUCTURE_KINDS:
		armor[String(kind_value)] = {
			"set_id": "FixtureArmor",
			"damage_scalar": 1.0,
			"scalars": {"default": 1.0},
		}
	return armor


func _spawn(sim: RetailSliceSim, id: int, team: int, point: Vector2, source_id: String, kinds: Array) -> void:
	var rule := _fixture_rule(source_id, kinds)
	sim._rules["unit_rules"][Sim.SOLDIER_OBJECT_ID]=rule; sim._add_battalion(id,team,point,source_id,Sim.SOLDIER_OBJECT_ID,Sim.SOLDIER_HORDE_ID,0,rule)
	(sim.entities[id] as Dictionary)["kind_of"] = kinds.duplicate(); (sim.entities[id] as Dictionary)["source_object_id"] = source_id


func _fixture_rule(source_id: String, kinds: Array) -> Dictionary:
	return {"source_object_id":source_id,"horde_id":Sim.SOLDIER_HORDE_ID,"category":"infantry","kind_of":kinds,"speed":1.0,"speed_source":10.0,"acceleration":1.0,"acceleration_source":10.0,"turn_rate_degrees_per_second":180.0,"braking":1.0,"braking_source":10.0,"attack_range":1.0,"attack_range_source":10.0,"minimum_attack_range":0.0,"minimum_attack_range_source":0.0,"vision_range":20.0,"vision_range_source":200.0,"delay_between_shots_ms":1000.0,"pre_attack_delay_ms":0.0,"firing_duration_ms":0.0,"attack_period_ticks":10,"pre_attack_ticks":0,"firing_duration_ticks":0,"member_damage":10,"member_health":100,"member_count":1,"formation_positions":[Vector3.ZERO],"provenance":{}}


func _grab_graph() -> Dictionary:
	return {"kind":"grab-passenger","specialPowerTemplateId":"SpecialAbilityGrabPassenger","updateModuleStartsAttack":true,"allowTree":true,"initiateFxId":"FX_Grab","acquire":{"startAbilityRange":8,"timingMs":{"UnpackTime":100,"PreparationTime":100,"PersistentPrepTime":200,"PackTime":100},"animation":{"state":"EATING","durationMs":500,"triggerTimeMs":200},"rejectedConditions":["WEAPON_TOGGLE"],"healGainPercent":20,"awardXp":0,"sourceIni":"fixture.ini","line":2},"containment":{"slots":1,"passengerFilter":"ANY +CLUB +ORC","manualPickUpFilter":"ANY +CLUB -ORC","objectStatusOfContained":["UNSELECTABLE"],"allowEnemiesInside":true,"allowNeutralInside":true,"allowAlliesInside":true,"weaponSetTypes":["CLUB"],"weaponStateTypes":["CLUB"],"sourceIni":"fixture.ini","line":10},"targetAdmission":{"passengerFilter":"ANY +CLUB -ORC","treeKindOf":"CLUB","treeObjectIds":["TreeOak"],"sourceIni":"naturetrees.ini"},"releaseAbilities":[_fling_graph()],"sourceIni":"fixture.ini","line":1}


func _fling_graph() -> Dictionary:
	return {"kind":"fling-passenger","specialPowerTemplateId":"SpecialAbilityFling","mustFinishAbility":true,"timingMs":{"UnpackTime":200,"PackTime":100},"velocity":{"x":0.0,"y":0.0,"z":10.0},"customAnimation":{"state":"THROW","durationMs":300},"landingWarhead":{"id":"TreeLandingWarhead","radius":20,"damageType":"CRUSH","deathType":"CRUSHED","specialObjectFilter":"NONE +INFANTRY -HERO","forceKillObjectFilter":"NONE +INFANTRY -HERO","sourceIni":"weapon.ini","line":1},"sourceIni":"fixture.ini","line":20}


func _ability(graph: Dictionary) -> Dictionary:
	return {"ability_id":"Command_Grab","special_power_id":"SpecialAbilityGrabPassenger","targeting":"enemy-object","cooldown_ticks":20,"required_level":1,"level_gate_resolved":true,"castable":true,"effect":graph,"special_power_contract":{}}


func _doc(graph: Dictionary) -> Dictionary:
	return {"registration":{"abilities":[{"id":"Command_Grab","slot":1,"targeting":"enemy-object","specialPowerId":"SpecialAbilityGrabPassenger","cooldownMs":2000,"button":{},"effect":graph,"implementation":{"status":"implemented","reason":"","limitations":[]},"levelGate":{}}]}}


func _has_event(sim: RetailSliceSim, kind: String) -> bool:
	for value in sim.events:
		if String((value as Dictionary).get("kind", "")) == kind: return true
	return false


func _check(label: String, condition: bool) -> void:
	if condition: passed += 1
	else: failed += 1; push_error("GRAB_FLING_RUNTIME_FAIL " + label)


func _finish() -> void:
	if passed + failed != EXPECTED: failed += 1; push_error("GRAB_FLING_RUNTIME_FAIL count")
	print("GRAB_FLING_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed]); quit(0 if failed == 0 else 1)

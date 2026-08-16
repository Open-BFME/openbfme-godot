extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED_CHECKS := 25

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_run_aura_checks()
	_run_lifetime_checks()
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("ATTRIBUTE_AURA_LIFETIME_RUNTIME_FAIL liveness ran=%d expected=%d" % [ran, EXPECTED_CHECKS])
	print("ATTRIBUTE_AURA_LIFETIME_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _run_aura_checks() -> void:
	var sim := _make_sim()
	sim._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = [_aura_contract(false, false, false)]
	_spawn(sim, 1, Sim.PLAYER_TEAM, Vector2.ZERO, "hero", ["HERO"])
	_spawn(sim, 2, Sim.PLAYER_TEAM, Vector2(3, 0), "infantry", ["INFANTRY"])
	_spawn(sim, 3, Sim.PLAYER_TEAM, Vector2(30, 0), "infantry", ["INFANTRY"])
	_spawn(sim, 90, Sim.ENEMY_TEAM, Vector2(3, 0), "infantry", ["INFANTRY"])
	var source := sim.entities[1] as Dictionary
	source["object_status"] = {"TAINT": true}
	(sim.entities[2] as Dictionary)["timed_modifiers"] = {"old-leadership": {"category": "LEADERSHIP", "modifiers": [{"kind": "DAMAGE_MULT", "value": 9.0}], "expires_tick": 999}}
	sim.tick()
	_check("starts_inactive_aura_sleeps_without_trigger", not _has_typed_aura(sim.entities[2] as Dictionary))
	source["completed_upgrades"] = ["Upgrade_Leadership"]
	sim.advance(2)
	_check("refresh_schedule_waits_for_authored_delay", not _has_typed_aura(sim.entities[2] as Dictionary))
	sim.tick()
	var ally := sim.entities[2] as Dictionary
	_check("triggered_aura_applies_resolved_modifier", _has_typed_aura(ally) and is_equal_approx(sim._timed_modifier_product(ally, "DAMAGE_MULT"), 1.5))
	_check("anti_category_removes_existing_category", not (ally.get("timed_modifiers", {}) as Dictionary).has("old-leadership"))
	_check("object_filter_rejects_hero_self", not _has_typed_aura(source))
	_check("range_rejects_distant_ally", not _has_typed_aura(sim.entities[3] as Dictionary))
	_check("target_enemy_false_rejects_enemy", not _has_typed_aura(sim.entities[90] as Dictionary))
	_check("category_replacement_key_is_deterministic", (ally.get("timed_modifiers", {}) as Dictionary).has("typed-aura-category:BUFF"))
	var aura_snapshot := sim.snapshot()
	var aura_hash := sim.state_hash()
	var aura_restored := _make_sim()
	_check("aura_state_snapshot_restores", aura_restored.restore(aura_snapshot))
	_check("aura_state_hash_round_trips", aura_restored.state_hash() == aura_hash)
	sim.advance(3)
	aura_restored.advance(3)
	_check("restored_aura_schedule_continues_deterministically", aura_restored.state_hash() == sim.state_hash())
	source["completed_upgrades"] = ["Upgrade_Leadership", "Upgrade_Disabled"]
	sim.advance(6)
	_check("conflict_gate_stops_refresh_and_modifier_expires", not _has_typed_aura(sim.entities[2] as Dictionary))

	var enemies := _make_sim()
	enemies._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = [_aura_contract(true, true, true)]
	_spawn(enemies, 10, Sim.PLAYER_TEAM, Vector2.ZERO, "hero", ["HERO"])
	_spawn(enemies, 12, Sim.PLAYER_TEAM, Vector2(100, 0), "infantry", ["INFANTRY"])
	_spawn(enemies, 11, Sim.ENEMY_TEAM, Vector2(2, 0), "infantry", ["INFANTRY"])
	(enemies.entities[10] as Dictionary)["object_status"] = {"TAINT": true}
	(enemies.entities[10] as Dictionary)["health"] = 0
	(enemies.entities[10] as Dictionary)["level"] = 1
	# AffectContainedOnly requires actual containment state, independently of a
	# transport module; this is the authoritative membership store.
	enemies.containment[700] = [11]
	enemies.entity_container[11] = 700
	enemies.tick()
	_check("run_while_dead_and_target_enemy_are_consumed", _has_typed_aura(enemies.entities[11] as Dictionary))
	(enemies.entities[10] as Dictionary)["level"] = 2
	enemies.advance(12)
	_check("max_active_rank_stops_higher_rank_refresh", not _has_typed_aura(enemies.entities[11] as Dictionary))

	var unresolved := _make_sim(false)
	unresolved._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = [_aura_contract(true, false, false)]
	_spawn(unresolved, 20, Sim.PLAYER_TEAM, Vector2.ZERO, "hero", ["HERO"])
	_spawn(unresolved, 21, Sim.PLAYER_TEAM, Vector2.ONE, "infantry", ["INFANTRY"])
	(unresolved.entities[20] as Dictionary)["object_status"] = {"TAINT": true}
	unresolved.tick()
	var receipt := (((unresolved.entities[20] as Dictionary).get("attribute_modifier_auras", []) as Array)[0] as Dictionary).get("unsupported_semantics", []) as Array
	_check("unresolved_modifier_fails_closed_with_receipt", not _has_typed_aura(unresolved.entities[21] as Dictionary) and receipt.has("unresolved_modifier_list:FixtureAura"))


func _run_lifetime_checks() -> void:
	var sim := _make_sim()
	_spawn(sim, 40, Sim.PLAYER_TEAM, Vector2.ZERO, "infantry", ["INFANTRY"])
	_spawn(sim, 41, Sim.ENEMY_TEAM, Vector2(50, 0), "infantry", ["INFANTRY"])
	sim.register_structure_module_contracts("FixtureTimedObject", [_lifetime_contract(true, 100, 300)])
	_add_timed_structure(sim, 800)
	sim.tick()
	var lifetime := (sim.structures[800] as Dictionary).get("lifetime_update", {}) as Dictionary
	_check("wait_for_wakeup_does_not_arm_at_attach", int(lifetime.get("expire_tick", -1)) == -1)
	sim.advance(3)
	_check("sleeping_lifetime_does_not_expire", int((sim.structures[800] as Dictionary).get("health", 0)) == 10)
	var wake := sim.wake_lifetime(800, "structure")
	_check("wake_arms_lifetime_once", bool(wake.get("ok", false)))
	lifetime = (sim.structures[800] as Dictionary).get("lifetime_update", {}) as Dictionary
	_check("bounded_lifetime_selection_is_inclusive", int(lifetime.get("selected_duration_ms", -1)) >= 100 and int(lifetime.get("selected_duration_ms", -1)) <= 300)
	var snapshot := sim.snapshot()
	var state_hash := sim.state_hash()
	var restored := _make_sim()
	_check("armed_lifetime_snapshot_restores", restored.restore(snapshot))
	_check("armed_lifetime_hash_round_trips", restored.state_hash() == state_hash)
	var remaining := int(lifetime.get("expire_tick", 0)) - sim.tick_index
	if remaining > 1:
		sim.advance(remaining - 1)
		restored.advance(remaining - 1)
	_check("lifetime_pending_object_remains_alive_until_expiry", int((sim.structures[800] as Dictionary).get("health", 0)) > 0)
	sim.tick()
	restored.tick()
	_check("expiry_dispatches_authored_death_type", int((sim.structures[800] as Dictionary).get("health", 1)) == 0 and _has_lifetime_event(sim, "FADED"))
	_check("restored_expiry_continues_deterministically", restored.state_hash() == sim.state_hash())

	var opaque := _make_sim()
	var opaque_contract := _lifetime_contract(false, 100, 100)
	opaque_contract["extraction"] = "opaque"
	opaque.register_structure_module_contracts("FixtureTimedObject", [opaque_contract])
	_add_timed_structure(opaque, 900)
	opaque.tick()
	_check("opaque_lifetime_fails_closed", not (opaque.structures[900] as Dictionary).has("lifetime_update"))


func _aura_contract(starts_active: bool, target_enemy: bool, contained_only: bool) -> Dictionary:
	return {"module": "AttributeModifierAuraUpdate", "extraction": "typed", "tag": "ModuleTag_Aura", "line": 10, "fields": {
		"StartsActive": {"value": starts_active}, "BonusName": {"value": "FixtureAura"},
		"TriggeredBy": {"value": ["Upgrade_Leadership"]}, "ConflictsWith": {"value": ["Upgrade_Disabled"]},
		"RefreshDelay": {"milliseconds": 300.0}, "Range": {"value": 10.0, "expression": "10"},
		"ObjectFilter": {"value": ["ANY", "+INFANTRY", "-HERO"]}, "TargetEnemy": {"value": target_enemy},
		"MaxActiveRank": {"value": 1}, "AntiCategory": {"value": ["LEADERSHIP"]},
		"AllowSelf": {"value": true}, "RunWhileDead": {"value": target_enemy},
		"RequiredConditions": {"value": ["TAINT"]}, "AffectContainedOnly": {"value": contained_only},
	}}


func _lifetime_contract(wait: bool, min_ms: int, max_ms: int) -> Dictionary:
	return {"module": "LifetimeUpdate", "extraction": "typed", "tag": "ModuleTag_Lifetime", "line": 30, "fields": {
		"MinLifetime": {"expression": str(min_ms), "milliseconds": min_ms},
		"MaxLifetime": {"expression": str(max_ms), "milliseconds": max_ms},
		"WaitForWakeUp": {"value": wait}, "DeathType": {"value": "FADED"},
	}}


func _make_sim(with_modifier: bool = true) -> RetailSliceSim:
	var rules := {}
	for object_id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]:
		rules[object_id] = _unit_rule().duplicate(true)
	var gameplay := {"unit_rules": rules, "source_map_transform_scale": 1.0, "logic_random_seed": 71}
	if with_modifier:
		gameplay["attribute_modifier_rules"] = {"FixtureAura": {"category": "BUFF", "duration_ms": 500.0, "effects": [{"kind": "DAMAGE_MULT", "value": 1.5}], "stacking": {"replaceInCategoryIfLongest": true}}}
	var sim: RetailSliceSim = Sim.new()
	sim.setup({}, gameplay)
	sim.ai_enabled = false
	sim.base_loop_enabled = false
	sim.entities.clear()
	sim.structures.clear()
	return sim


func _spawn(sim: RetailSliceSim, id: int, team: int, at: Vector2, category: String, kind_of: Array) -> void:
	sim._add_battalion(id, team, at, "Fixture", Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, 0, _unit_rule())
	var row := sim.entities[id] as Dictionary
	row["category"] = category
	row["kind_of"] = kind_of


func _add_timed_structure(sim: RetailSliceSim, id: int) -> void:
	sim.structures[id] = {"id": id, "team": -1, "source_object_id": "FixtureTimedObject", "structure_kind": "timed", "kind": "timed", "position": Vector2(100, 100), "health": 10, "maximum_health": 10, "damage_remainders": {}, "queue": [], "upgrade_queue": []}


func _has_typed_aura(row: Dictionary) -> bool:
	for key_value in (row.get("timed_modifiers", {}) as Dictionary).keys():
		if String(key_value).begins_with("typed-aura"):
			return true
	return false


func _has_lifetime_event(sim: RetailSliceSim, death_type: String) -> bool:
	for event_value in sim.events:
		var event := event_value as Dictionary
		if String(event.get("kind", "")) == "lifetime.expired" and String(event.get("death_type", "")) == death_type:
			return true
	return false


func _unit_rule() -> Dictionary:
	return {"horde_id": Sim.SOLDIER_HORDE_ID, "category": "infantry", "speed": 1.0, "speed_source": 10.0, "acceleration": 1.0, "acceleration_source": 10.0, "turn_rate_degrees_per_second": 180.0, "braking": 1.0, "braking_source": 10.0, "attack_range": 0.1, "attack_range_source": 1.0, "minimum_attack_range": 0.0, "minimum_attack_range_source": 0.0, "vision_range": 1.0, "vision_range_source": 10.0, "delay_between_shots_ms": 1000.0, "pre_attack_delay_ms": 0.0, "firing_duration_ms": 0.0, "attack_period_ticks": 10, "pre_attack_ticks": 0, "firing_duration_ticks": 0, "member_damage": 1, "member_health": 100, "member_count": 1, "formation_positions": [Vector3.ZERO], "provenance": {}}


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("ATTRIBUTE_AURA_LIFETIME_RUNTIME_FAIL %s" % label)

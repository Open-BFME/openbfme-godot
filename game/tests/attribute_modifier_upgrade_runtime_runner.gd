extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED_CHECKS := 26

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var sim := _make_sim()
	sim._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = [
		_contract("ModuleTag_Any", ["Upgrade_A", "Upgrade_B"], false, ["Upgrade_Block"], "FixtureAny", true),
		_contract("ModuleTag_All", ["Upgrade_A", "Upgrade_B"], true, [], "FixtureAll", false),
	]
	_spawn(sim, 1, Sim.PLAYER_TEAM)
	var row := sim.entities[1] as Dictionary
	_check("typed_contract_attaches", (row.get("attribute_modifier_upgrades", []) as Array).size() == 2)
	_check("starts_inactive_without_trigger", not _has_upgrade_modifier(row, "ModuleTag_Any"))
	_check("custom_animation_is_presentation_receipt", _receipts(row).has("CustomAnimAndDuration:USER_2 1234"))

	sim.set_entity_upgrade_state(1, "Upgrade_A", true)
	row = sim.entities[1] as Dictionary
	_check("any_trigger_activates", _has_upgrade_modifier(row, "ModuleTag_Any"))
	_check("resolved_effect_changes_shared_damage_multiplier", is_equal_approx(sim._ability_outgoing_multiplier(row), 1.5))
	_check("requires_all_waits_for_second_trigger", not _has_upgrade_modifier(row, "ModuleTag_All"))

	sim.set_entity_upgrade_state(1, "upgrade_b", true)
	row = sim.entities[1] as Dictionary
	_check("upgrade_ids_are_case_insensitive", _has_upgrade_modifier(row, "ModuleTag_All"))
	_check("all_trigger_effect_uses_shared_modifier_core", is_equal_approx(sim._ability_speed_multiplier(row), 1.2))
	var active_hash := sim.state_hash()
	var bytes := sim.snapshot()
	var restored := _make_sim()
	_check("snapshot_restores", restored.restore(bytes))
	_check("snapshot_hash_round_trips", restored.state_hash() == active_hash)
	restored.tick()
	sim.tick()
	row = sim.entities[1] as Dictionary
	_check("restored_reconciliation_is_deterministic", restored.state_hash() == sim.state_hash())

	sim.set_entity_upgrade_state(1, "Upgrade_Block", true)
	row = sim.entities[1] as Dictionary
	_check("conflict_deactivates_any_trigger", not _has_upgrade_modifier(row, "ModuleTag_Any"))
	_check("conflict_removes_only_its_effect", is_equal_approx(sim._ability_outgoing_multiplier(row), 1.0) and is_equal_approx(sim._ability_speed_multiplier(row), 1.2))
	sim.set_entity_upgrade_state(1, "Upgrade_Block", false)
	sim.set_entity_upgrade_state(1, "Upgrade_A", false)
	row = sim.entities[1] as Dictionary
	_check("removed_required_trigger_deactivates_requires_all", not _has_upgrade_modifier(row, "ModuleTag_All"))
	_check("conflict_removal_reactivates_any_trigger", _has_upgrade_modifier(row, "ModuleTag_Any"))

	var api_remove: Dictionary = sim.set_entity_upgrade_state(1, "Upgrade_B", false)
	_check("entity_upgrade_api_removes_authored_trigger", bool(api_remove.get("ok", false)) and not _has_upgrade_modifier(row, "ModuleTag_Any"))
	var api_grant: Dictionary = sim.set_entity_upgrade_state(1, "Upgrade_B", true)
	_check("entity_upgrade_api_reconciles_immediately", bool(api_grant.get("ok", false)) and _has_upgrade_modifier(row, "ModuleTag_Any"))
	var team_remove: Dictionary = sim.set_entity_upgrade_state(1, "Upgrade_B", false)
	_check("entity_upgrade_cleanup_is_immediate", bool(team_remove.get("ok", false)) and not _has_upgrade_modifier(row, "ModuleTag_Any"))
	var team_grant: Dictionary = sim.set_team_upgrade_state(Sim.PLAYER_TEAM, "Upgrade_A", true)
	_check("team_upgrade_activates_owned_entities", bool(team_grant.get("ok", false)) and _has_upgrade_modifier(row, "ModuleTag_Any"))
	var team_clear: Dictionary = sim.set_team_upgrade_state(Sim.PLAYER_TEAM, "Upgrade_A", false)
	_check("team_upgrade_removal_deactivates_owned_entities", bool(team_clear.get("ok", false)) and not _has_upgrade_modifier(row, "ModuleTag_Any"))

	var unresolved := _make_sim()
	unresolved._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = [_contract("ModuleTag_Missing", ["Upgrade_A"], false, [], "MissingModifier", false)]
	_spawn(unresolved, 2, Sim.PLAYER_TEAM)
	unresolved.set_entity_upgrade_state(2, "Upgrade_A", true)
	var missing_row := unresolved.entities[2] as Dictionary
	_check("unresolved_modifier_fails_closed", not _has_upgrade_modifier(missing_row, "ModuleTag_Missing"))
	_check("unresolved_modifier_has_explicit_receipt", _unsupported(missing_row).has("unresolved_modifier_list:MissingModifier"))

	var opaque := _make_sim()
	var opaque_contract := _contract("ModuleTag_Opaque", ["Upgrade_A"], false, [], "FixtureAny", false)
	opaque_contract["extraction"] = "opaque"
	opaque._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = [opaque_contract]
	_spawn(opaque, 3, Sim.PLAYER_TEAM)
	_check("opaque_contract_fails_closed", not (opaque.entities[3] as Dictionary).has("attribute_modifier_upgrades"))

	var anti := _make_sim()
	anti._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = [_contract("ModuleTag_Leadership", ["Upgrade_Leadership"], false, [], "FixtureLeadership", false)]
	_spawn(anti, 4, Sim.PLAYER_TEAM)
	_spawn(anti, 90, Sim.ENEMY_TEAM)
	var anti_row := anti.entities[4] as Dictionary
	anti_row["leadership_suppressed_until_tick"] = 2
	anti.set_entity_upgrade_state(4, "Upgrade_Leadership", true)
	_check("anticategory_suppresses_ignore_category_modifier", not _has_upgrade_modifier(anti_row, "ModuleTag_Leadership"))
	anti.tick()
	_check("anticategory_remains_suppressed_for_exact_window", not _has_upgrade_modifier(anti.entities[4] as Dictionary, "ModuleTag_Leadership"))
	anti.tick()
	anti.tick()
	_check("modifier_reactivates_after_anticategory_expiry", _has_upgrade_modifier(anti.entities[4] as Dictionary, "ModuleTag_Leadership"))

	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("ATTRIBUTE_MODIFIER_UPGRADE_RUNTIME_FAIL liveness ran=%d expected=%d" % [ran, EXPECTED_CHECKS])
	print("ATTRIBUTE_MODIFIER_UPGRADE_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _contract(tag: String, triggers: Array, requires_all: bool, conflicts: Array, modifier: String, with_anim: bool) -> Dictionary:
	var fields := {
		"TriggeredBy": {"authored": " ".join(triggers), "value": triggers.duplicate(), "sourceIni": "data/ini/object/fixture.ini", "line": 11},
		"AttributeModifier": {"authored": modifier, "value": modifier, "sourceIni": "data/ini/object/fixture.ini", "line": 12},
		"ConflictsWith": {"authored": " ".join(conflicts), "value": conflicts.duplicate(), "sourceIni": "data/ini/object/fixture.ini", "line": 13},
		"RequiresAllTriggers": {"authored": "Yes" if requires_all else "No", "value": requires_all, "sourceIni": "data/ini/object/fixture.ini", "line": 14},
	}
	if with_anim:
		fields["deferredFields"] = [{"name": "CustomAnimAndDuration", "authored": "USER_2 1234", "sourceIni": "data/ini/object/fixture.ini", "line": 15, "reason": "presentation-or-unmodeled-upgrade-side-effect"}]
	return {"module": "AttributeModifierUpgrade", "runtimeStatus": "deferred", "extraction": "typed", "tag": tag, "sourceIni": "data/ini/object/fixture.ini", "line": 10, "fields": fields}


func _make_sim() -> RetailSliceSim:
	var rules := {}
	for object_id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]:
		rules[object_id] = _unit_rule().duplicate(true)
	var sim: RetailSliceSim = Sim.new()
	sim.setup({}, {"unit_rules": rules, "attribute_modifier_rules": {
		"FixtureAny": {"category": "UPGRADE_DAMAGE", "effects": [{"kind": "DAMAGE_MULT", "value": 1.5, "application": "multiplicative"}]},
		"FixtureAll": {"category": "UPGRADE_SPEED", "effects": [{"kind": "SPEED", "value": 1.2, "application": "multiplicative"}]},
		"FixtureLeadership": {"category": "LEADERSHIP", "effects": [{"kind": "DAMAGE_MULT", "value": 1.1, "application": "multiplicative"}], "stacking": {"ignoreIfAnticategoryActive": true}},
	}})
	sim.ai_enabled = false
	sim.base_loop_enabled = false
	sim.entities.clear()
	sim.structures.clear()
	return sim


func _spawn(sim: RetailSliceSim, id: int, team: int) -> void:
	sim._add_battalion(id, team, Vector2.ZERO, "Fixture", Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, 0, _unit_rule())


func _unit_rule() -> Dictionary:
	return {"horde_id": Sim.SOLDIER_HORDE_ID, "category": "infantry", "speed": 1.0, "speed_source": 10.0, "acceleration": 1.0, "acceleration_source": 10.0, "turn_rate_degrees_per_second": 180.0, "braking": 1.0, "braking_source": 10.0, "attack_range": 0.1, "attack_range_source": 1.0, "minimum_attack_range": 0.0, "minimum_attack_range_source": 0.0, "vision_range": 1.0, "vision_range_source": 10.0, "delay_between_shots_ms": 1000.0, "pre_attack_delay_ms": 0.0, "firing_duration_ms": 0.0, "attack_period_ticks": 10, "pre_attack_ticks": 0, "firing_duration_ticks": 0, "member_damage": 1, "member_health": 100, "member_count": 1, "formation_positions": [Vector3.ZERO], "provenance": {}}


func _has_upgrade_modifier(row: Dictionary, tag: String) -> bool:
	for entry_value in (row.get("timed_modifiers", {}) as Dictionary).values():
		var entry := entry_value as Dictionary
		if bool(entry.get("attribute_modifier_upgrade", false)) and String(entry.get("module_tag", "")) == tag:
			return true
	return false


func _receipts(row: Dictionary) -> Array:
	var found: Array = []
	for policy_value in row.get("attribute_modifier_upgrades", []) as Array:
		found.append_array((policy_value as Dictionary).get("presentation_receipts", []) as Array)
	return found


func _unsupported(row: Dictionary) -> Array:
	var found: Array = []
	for policy_value in row.get("attribute_modifier_upgrades", []) as Array:
		found.append_array((policy_value as Dictionary).get("unsupported_semantics", []) as Array)
	return found


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("ATTRIBUTE_MODIFIER_UPGRADE_RUNTIME_FAIL %s" % label)

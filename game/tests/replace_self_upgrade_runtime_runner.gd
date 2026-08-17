extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED := 26
var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var live: RetailSliceSim = Sim.new()
	# Keep the active selected-pack structure catalog, but make the legacy
	# default spawn roster self-contained. Under a RotWK pack those five seed
	# ids are intentionally not BFME2 ids; asking the pack to resolve them made
	# this runner print engine errors even though replacement assertions passed.
	live._ensure_parity()
	var live_rules := live._rules.duplicate(true)
	var live_unit_rules := (live_rules.get("unit_rules", {}) as Dictionary).duplicate(true)
	for object_id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]:
		if not live_unit_rules.has(object_id):
			live_unit_rules[object_id] = _rule()
	live_rules["unit_rules"] = live_unit_rules
	live_rules["faction_manifest"] = {"structure_armor": _fixture_structure_armor()}
	live.setup({}, live_rules)
	var live_spec := live._replace_self_structure_spec(0, "GondorCastleWallSegment")
	_check("selected_retail_template_resolves", not live_spec.is_empty() and int(live_spec.get("maximum_health", 0)) == 1500)
	var sim := _sim()
	var wall := sim.structures[50] as Dictionary
	sim._attach_replace_self_contract(wall, _contract())
	_check("typed_contract_attaches", (wall.get("replace_self_upgrades", []) as Array).size() == 1)
	_check("retail_repeated_additions_order_preserved", ((wall.get("replace_self_upgrades", []) as Array)[0] as Dictionary).get("and_then_add", []) == ["GondorCastleWallHub", "GondorCastleWallSegment"])
	sim.selected_ids.assign([50])
	sim.control_groups[1] = [50]
	sim.entities[9] = {"id": 9, "team": 2, "health": 100, "maximum_health": 100, "position": Vector2(4, 4), "object_status": {}}
	sim.contain_entity(50, 9)
	sim._step_replace_self_upgrades()
	var replaced := sim.structures[50] as Dictionary
	_check("primary_runtime_identity_preserved", int(replaced.get("id", 0)) == 50)
	_check("replacement_object_identity_exact", String(replaced.get("source_object_id", "")) == "GondorCastleWallSegment")
	_check("replacement_kind_resolved", String(replaced.get("structure_kind", "")) == "wall_segment")
	_check("ownership_preserved", int(replaced.get("team", -1)) == 2)
	_check("transform_preserved", Vector2(replaced.get("position", Vector2.ZERO)) == Vector2(4, 4) and Vector2(replaced.get("facing", Vector2.ZERO)) == Vector2.RIGHT)
	_check("damage_fraction_preserved", int(replaced.get("health", 0)) == 100 and int(replaced.get("maximum_health", 0)) == 200)
	_check("prior_upgrades_preserved", (replaced.get("completed_upgrades", []) as Array) == ["Upgrade_MenWallHub", "Upgrade_Common"])
	_check("selection_preserved", sim.selected_ids == [50])
	_check("control_group_preserved", (sim.control_groups[1] as Array) == [50])
	_check("incompatible_containment_ejected", not sim.entity_container.has(9) and sim.passenger_count(50) == 0)
	_check("two_additional_objects_created", sim.structures.has(3000) and sim.structures.has(3001))
	_check("additional_order_exact", String((sim.structures[3000] as Dictionary).get("source_object_id", "")) == "GondorCastleWallHub" and String((sim.structures[3001] as Dictionary).get("source_object_id", "")) == "GondorCastleWallSegment")
	_check("addition_ownership_transform", int((sim.structures[3000] as Dictionary).get("team", -1)) == 2 and Vector2((sim.structures[3001] as Dictionary).get("position", Vector2.ZERO)) == Vector2(4, 4))
	_check("replacement_not_death", not sim.events.any(func(event: Dictionary) -> bool: return String(event.get("kind", "")).contains("death")))
	var replace_event := sim.events.filter(func(event: Dictionary) -> bool: return String(event.get("kind", "")) == "upgrade.replace_self")
	_check("replacement_event_lists_order", replace_event.size() == 1 and (replace_event[0] as Dictionary).get("created_object_ids", []) == [3000, 3001])
	var snapshot := sim.snapshot()
	var hash := sim.state_hash()
	var restored := _sim()
	_check("snapshot_restores", restored.restore(snapshot))
	_check("state_hash_round_trips", restored.state_hash() == hash)
	_check("replacement_state_round_trips", String((restored.structures[50] as Dictionary).get("source_object_id", "")) == "GondorCastleWallSegment" and restored.structures.has(3001))

	var conflict := _sim()
	var conflict_wall := conflict.structures[50] as Dictionary
	conflict_wall["completed_upgrades"] = ["Upgrade_MenWallHub", "Upgrade_MenWallRegularGate"]
	conflict._attach_replace_self_contract(conflict_wall, _contract())
	conflict._step_replace_self_upgrades()
	_check("conflict_blocks_replacement", String((conflict.structures[50] as Dictionary).get("source_object_id", "")) == "MenWallUpgradeNodeSmall" and conflict.structures.size() == 1)
	var direct := _sim()
	var direct_wall := direct.structures[50] as Dictionary
	direct_wall["completed_upgrades"] = ["Upgrade_Common"]
	direct._attach_replace_self_contract(direct_wall, _contract())
	_check("command_api_grants_trigger_and_replaces", bool(direct.apply_replace_self_upgrade(50, "Upgrade_MenWallHub").get("ok", false)) and String((direct.structures[50] as Dictionary).get("source_object_id", "")) == "GondorCastleWallSegment")

	var unresolved := _sim()
	var unresolved_wall := unresolved.structures[50] as Dictionary
	var unresolved_contract := _contract()
	(unresolved_contract["fields"] as Dictionary)["ReplaceWith"] = {"value": "MissingRetailTemplate"}
	unresolved._attach_replace_self_contract(unresolved_wall, unresolved_contract)
	unresolved._step_replace_self_upgrades()
	var unresolved_policy := ((unresolved_wall.get("replace_self_upgrades", []) as Array)[0] as Dictionary)
	_check("unresolved_template_fails_closed", String(unresolved_wall.get("source_object_id", "")) == "MenWallUpgradeNodeSmall" and not bool(unresolved_policy.get("consumed", false)))
	_check("unresolved_template_receipted", (unresolved_policy.get("unsupported_semantics", []) as Array).has("unresolved_replacement_template:MissingRetailTemplate"))

	var opaque := _sim()
	var opaque_contract := _contract()
	opaque_contract["extraction"] = "opaque"
	opaque._attach_replace_self_contract(opaque.structures[50] as Dictionary, opaque_contract)
	_check("opaque_fails_closed", not (opaque.structures[50] as Dictionary).has("replace_self_upgrades"))
	print("REPLACE_SELF_UPGRADE_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 and passed == EXPECTED else 1)


func _contract() -> Dictionary:
	return {"module": "ReplaceSelfUpgrade", "extraction": "typed", "tag": "ModuleTag_Replace", "fields": {"ReplaceWith": {"value": "GondorCastleWallSegment"}, "AndThenAddA": [{"value": "GondorCastleWallHub", "line": 7273}, {"value": "GondorCastleWallSegment", "line": 7274}], "TriggeredBy": {"value": "Upgrade_MenWallHub"}, "ConflictsWith": {"value": ["Upgrade_MenWallRegularGate", "Upgrade_MenWallPosternGate", "Upgrade_MenWallTower", "Upgrade_MenWallTrebuchet"]}}}


func _sim() -> RetailSliceSim:
	var sim: RetailSliceSim = Sim.new()
	var unit_rules := {}
	for object_id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]:
		unit_rules[object_id] = _rule()
	sim.setup({}, {"unit_rules": unit_rules, "faction_manifest": {"structure_armor": _fixture_structure_armor()}, "replace_self_structure_templates": {"GondorCastleWallSegment": {"structure_kind": "wall_segment", "maximum_health": 200}, "GondorCastleWallHub": {"structure_kind": "wall_hub", "maximum_health": 300}}})
	sim.ai_enabled = false
	sim.base_loop_enabled = false
	sim.entities.clear()
	sim.structures.clear()
	sim.structures[50] = {"id": 50, "team": 2, "kind": "structure", "structure_kind": "wall_upgrade_node", "source_object_id": "MenWallUpgradeNodeSmall", "name": "Wall upgrade node", "position": Vector2(4, 4), "rally": Vector2(8, 4), "facing": Vector2.RIGHT, "health": 50, "maximum_health": 100, "construction_progress": 1.0, "level": 1, "completed_upgrades": ["Upgrade_MenWallHub", "Upgrade_Common"], "upgrade_queue": [], "production": [], "queue": [], "damage_remainders": {}, "income_per_payout": 0}
	sim._next_dynamic_structure_id = 3000
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


func _rule() -> Dictionary:
	return {"horde_id": Sim.SOLDIER_HORDE_ID, "category": "infantry", "speed": 1.0, "speed_source": 10.0, "acceleration": 1.0, "acceleration_source": 10.0, "turn_rate_degrees_per_second": 180.0, "braking": 1.0, "braking_source": 10.0, "attack_range": 1.0, "attack_range_source": 1.0, "minimum_attack_range": 0.0, "minimum_attack_range_source": 0.0, "vision_range": 10.0, "vision_range_source": 10.0, "delay_between_shots_ms": 1000.0, "pre_attack_delay_ms": 0.0, "firing_duration_ms": 0.0, "attack_period_ticks": 10, "pre_attack_ticks": 0, "firing_duration_ticks": 0, "member_damage": 1, "member_health": 100, "member_count": 1, "formation_positions": [Vector3.ZERO], "provenance": {}}


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("REPLACE_SELF_UPGRADE_RUNTIME_FAIL " + label)

extends SceneTree

## Focused runtime proof for the healing subset of PassiveAreaEffectBehavior.
## Uses synthetic authored moduleContracts and the real RetailSliceSim tick.

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED_CHECKS := 19

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var sim := _make_sim()
	var opaque_contract := _heal_contract()
	((opaque_contract["fields"] as Dictionary)["EffectRadius"] as Dictionary).erase("value")
	((opaque_contract["fields"] as Dictionary)["EffectRadius"] as Dictionary)["authored"] = "GONDOR_WELL_AOE_RADIUS"
	var merged := Sim._structure_contracts_with_passive_area_resolution({
		"registration": {"gameplay": {"passiveAreaEffect": {
			"radius": 200.0,
			"radiusAuthored": "GONDOR_WELL_AOE_RADIUS",
			"healPercentPerSecondAuthored": "2",
		}}},
	}, [opaque_contract])
	var merged_fields: Dictionary = (merged[0] as Dictionary).get("fields", {}) as Dictionary
	_check("dedicated_contract_resolves_authored_radius_define", Sim._passive_area_effect_number(merged_fields, "EffectRadius") == 200.0)
	_check("merge_preserves_opaque_ping_receipt", Sim._passive_area_effect_number(merged_fields, "PingDelay") == 300.0)
	sim.register_structure_module_contracts("GondorWell", merged)
	_add_structure(sim, 100, Sim.PLAYER_TEAM, Vector2.ZERO, "well", "GondorWell")
	_add_structure(sim, 101, Sim.PLAYER_TEAM, Vector2(0.5, 0.0), "well", "GondorWell")
	_spawn(sim, 1, Sim.PLAYER_TEAM, Vector2(1.0, 0.0), "infantry")
	_spawn(sim, 2, Sim.PLAYER_TEAM, Vector2(30.0, 0.0), "infantry")
	_spawn(sim, 3, Sim.PLAYER_TEAM, Vector2(1.0, 0.0), "monster")
	_spawn(sim, 4, Sim.ENEMY_TEAM, Vector2(15.0, 0.0), "infantry")
	_wound(sim.entities[1] as Dictionary, [0, 50])
	_wound(sim.entities[2] as Dictionary, [50, 50])
	_wound(sim.entities[3] as Dictionary, [50, 50])
	_wound(sim.entities[4] as Dictionary, [50, 50])

	sim.advance(2)
	_check("authored_ping_delay_blocks_early_heal", int((sim.entities[1] as Dictionary)["health"]) == 50)
	_check("live_tick_attaches_deferred_descriptor", (sim.structures[100] as Dictionary).has("passive_area_effect_heals"))
	# Adversarially stagger the second well. NonStackable must select one active
	# source globally, not let differently phased pings alternate and add up.
	var second_rules: Array = (sim.structures[101] as Dictionary).get("passive_area_effect_heals", []) as Array
	(second_rules[0] as Dictionary)["next_ping_tick"] = int((second_rules[0] as Dictionary)["next_ping_tick"]) + 1
	sim.advance(28)
	var healed_health := int((sim.entities[1] as Dictionary)["health"])
	_check("two_percent_per_second_for_three_seconds actual=%d" % healed_health, healed_health == 56)
	_check("dead_member_is_not_revived", int(((sim.entities[1] as Dictionary)["member_health"] as Array)[0]) == 0)
	_check("staggered_nonstackable_sources_do_not_alternate_stack", int((sim.entities[1] as Dictionary)["health"]) == 56)
	_check("authored_radius_excludes_distant_ally", int((sim.entities[2] as Dictionary)["health"]) == 100)
	_check("authored_filter_excludes_monster", int((sim.entities[3] as Dictionary)["health"]) == 100)
	_check("enemy_in_radius_is_not_healed", int((sim.entities[4] as Dictionary)["health"]) == 100)
	var snapshot := sim.snapshot()
	var snapshot_hash := sim.state_hash()
	var restored := _make_sim()
	restored.register_structure_module_contracts("GondorWell", merged)
	_check("passive_heal_snapshot_restores", restored.restore(snapshot))
	_check("passive_heal_remainder_and_cadence_hash_round_trip", restored.state_hash() == snapshot_hash)

	var gated := _make_sim()
	gated.register_structure_module_contracts("MenFortressCitadel", [_heal_contract("Upgrade_TestHealing")])
	_add_structure(gated, 200, Sim.PLAYER_TEAM, Vector2.ZERO, "fortress", "MenFortressCitadel")
	_spawn(gated, 10, Sim.PLAYER_TEAM, Vector2(1.0, 0.0), "infantry")
	_spawn(gated, 11, Sim.ENEMY_TEAM, Vector2(20.0, 0.0), "infantry")
	_wound(gated.entities[10] as Dictionary, [50, 50])
	gated.advance(30)
	_check("missing_authored_upgrade_suppresses_heal", int((gated.entities[10] as Dictionary)["health"]) == 100)
	(gated.structures[200] as Dictionary)["completed_upgrades"] = ["Upgrade_TestHealing"]
	gated.advance(30)
	_check("completed_authored_upgrade_enables_heal", int((gated.entities[10] as Dictionary)["health"]) == 106)

	var leadership := _make_sim()
	var leadership_contract := _heal_contract()
	(leadership_contract["fields"] as Dictionary).erase("HealPercentPerSecond")
	var leadership_merged := Sim._structure_contracts_with_passive_area_resolution({
		"registration": {"gameplay": {"passiveAreaEffect": {
			"radius": 200.0,
			"radiusAuthored": "200",
			"modifier": {
				"id": "FixtureLeadership",
				"category": "LEADERSHIP",
				"durationMs": 3000,
				"effects": [
					{"kind": "DAMAGE_MULT", "value": 1.5, "application": "multiplicative"},
					{"kind": "EXPERIENCE", "value": 2.0, "application": "multiplicative"},
				],
				"stacking": {"replaceInCategoryIfLongest": true, "ignoreIfAnticategoryActive": true},
			},
		}}},
	}, [leadership_contract])
	_check("resolved_modifier_merges_into_module_contract", typeof(((leadership_merged[0] as Dictionary).get("fields", {}) as Dictionary).get("ResolvedModifier")) == TYPE_DICTIONARY)
	leadership.register_structure_module_contracts("FixtureStatue", leadership_merged)
	_add_structure(leadership, 300, Sim.PLAYER_TEAM, Vector2.ZERO, "statue", "FixtureStatue")
	_spawn(leadership, 20, Sim.PLAYER_TEAM, Vector2(1.0, 0.0), "infantry")
	_spawn(leadership, 21, Sim.PLAYER_TEAM, Vector2(30.0, 0.0), "infantry")
	leadership.advance(1)
	_check("leadership_modifier_attaches", (leadership.structures[300] as Dictionary).has("passive_area_effect_modifiers"))
	_check("leadership_damage_multiplier_applies_in_radius", is_equal_approx(leadership._timed_modifier_product(leadership.entities[20] as Dictionary, "DAMAGE_MULT"), 1.5))
	_check("leadership_experience_multiplier_applies_in_radius", is_equal_approx(leadership._timed_modifier_product(leadership.entities[20] as Dictionary, "EXPERIENCE"), 2.0))
	_check("leadership_radius_excludes_distant_ally", is_equal_approx(leadership._timed_modifier_product(leadership.entities[21] as Dictionary, "DAMAGE_MULT"), 1.0))

	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("PASSIVE_AREA_EFFECT_HEAL_FAIL liveness ran=%d expected=%d" % [ran, EXPECTED_CHECKS])
	print("PASSIVE_AREA_EFFECT_HEAL_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _make_sim() -> RetailSliceSim:
	var rule := _unit_rule("infantry")
	var rules := {}
	for object_id in [
		Sim.SOLDIER_OBJECT_ID,
		Sim.SOLDIER_HORDE_ID,
		Sim.ARCHER_OBJECT_ID,
		Sim.TOWER_GUARD_OBJECT_ID,
		Sim.KNIGHT_OBJECT_ID,
	]:
		rules[object_id] = rule.duplicate(true)
	var sim: RetailSliceSim = Sim.new()
	sim.setup({}, {
		"unit_rules": rules,
		"source_map_transform_scale": 0.1,
		"faction_manifest": {"structure_armor": _fixture_structure_armor()},
	})
	sim.ai_enabled = false
	sim.base_loop_enabled = false
	sim.entities.clear()
	sim.structures.clear()
	return sim


func _spawn(sim: RetailSliceSim, id: int, team: int, at: Vector2, category: String) -> void:
	var rule := _unit_rule(category)
	sim._add_battalion(id, team, at, "Fixture", Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, 0, rule)


func _wound(row: Dictionary, health: Array) -> void:
	row["member_health"] = health.duplicate()
	var total := 0
	for value in health:
		total += int(value)
	row["health"] = total


func _add_structure(
	sim: RetailSliceSim, id: int, team: int, at: Vector2, kind: String, source_object_id: String
) -> void:
	sim.structures[id] = {
		"id": id,
		"team": team,
		"structure_kind": kind,
		"source_object_id": source_object_id,
		"position": at,
		"health": 1000,
		"maximum_health": 1000,
		"construction_progress": 1.0,
		"completed_upgrades": [],
		"queue": [],
		"upgrade_queue": [],
	}


func _heal_contract(upgrade_required: String = "") -> Dictionary:
	var fields := {
		"EffectRadius": {"authored": "200", "value": 200.0},
		"PingDelay": {"authored": "300", "value": 300.0},
		"HealPercentPerSecond": {"authored": "2%"},
		"AllowFilter": {"authored": "ANY +INFANTRY +CAVALRY -MONSTER -MACHINE -IMMOBILE +DOZER"},
		"NonStackable": {"authored": "Yes"},
	}
	if upgrade_required != "":
		fields["UpgradeRequired"] = {"authored": upgrade_required}
	return {
		"module": "PassiveAreaEffectBehavior",
		"tag": "ModuleTag_SplashOfHealingWater_Ahh",
		"fields": fields,
		"runtime_status": "deferred",
		"sourceIni": "data/ini/object/goodfaction/structures/men/well.ini",
		"line": 228,
	}


func _unit_rule(category: String) -> Dictionary:
	return {
		"horde_id": Sim.SOLDIER_HORDE_ID,
		"category": category,
		"speed": 1.0,
		"speed_source": 10.0,
		"acceleration": 1.0,
		"acceleration_source": 10.0,
		"turn_rate_degrees_per_second": 180.0,
		"braking": 1.0,
		"braking_source": 10.0,
		"attack_range": 1.0,
		"attack_range_source": 10.0,
		"minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0,
		"vision_range": 10.0,
		"vision_range_source": 100.0,
		"delay_between_shots_ms": 100.0,
		"pre_attack_delay_ms": 0.0,
		"firing_duration_ms": 0.0,
		"attack_period_ticks": 1,
		"pre_attack_ticks": 0,
		"firing_duration_ticks": 0,
		"member_damage": 1,
		"member_health": 100,
		"member_count": 2,
		"formation_positions": [Vector3.ZERO, Vector3(0.5, 0.0, 0.0)],
		"provenance": {},
	}


func _fixture_structure_armor() -> Dictionary:
	var armor := {}
	for kind_value in Sim.STRUCTURE_KINDS:
		armor[String(kind_value)] = {
			"set_id": "FixtureArmor",
			"damage_scalar": 1.0,
			"scalars": {"default": 1.0},
		}
	return armor


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("PASSIVE_AREA_EFFECT_HEAL_FAIL %s" % label)

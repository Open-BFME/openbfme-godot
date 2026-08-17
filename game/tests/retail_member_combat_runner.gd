extends SceneTree

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
var BattalionScript: Script

var passed := 0
var failed := 0


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_MEMBER_COMBAT_RUNNER")
	call_deferred("_run")


func _make_sim():
	var sim = SimScript.new()
	sim.setup({}, {
		"member_health": 100,
		"unit_rules": _unit_rules(5, 50),
	})
	sim.ai_enabled = false
	(sim.entities[1] as Dictionary)["position"] = Vector2.ZERO
	(sim.entities[1] as Dictionary)["destination"] = Vector2.ZERO
	(sim.entities[101] as Dictionary)["position"] = Vector2(1.0, 0.0)
	(sim.entities[101] as Dictionary)["destination"] = Vector2(1.0, 0.0)
	return sim


func _run() -> void:
	BattalionScript = load("res://src/retail_slice/retail_battalion.gd")
	_check("member_presentation_script_available", BattalionScript != null)
	if BattalionScript != null:
		_run_member_presentation_contract()
	var sim = _make_sim()
	var attacker: Dictionary = sim.entity(1)
	_check(
		"retail_source_ranges_are_unit_specific",
		is_equal_approx(float(attacker.get("attack_range", 0.0)), 1.15)
			and is_equal_approx(float(attacker.get("vision_range", 0.0)), 17.5)
			and is_equal_approx(float(sim.entity(2).get("attack_range", 0.0)), 30.0)
			and is_equal_approx(float(sim.entity(2).get("vision_range", 0.0)), 37.0)
	)
	_check(
		"member_health_initializes_independently",
		Array(attacker.get("member_health", [])) == [100, 100, 100, 100, 100]
			and int(attacker.get("member_maximum_health", 0)) == 100
			and int(attacker.get("health", 0)) == 500
	)
	_check("one_squad_attack_order_is_accepted", sim.issue_attack([1], 101) == 1)
	sim.advance(1)
	var first_tokens: Array = sim.entity(1).get("member_attack_tokens", [])
	_check(
		"member_attacks_start_on_individual_ticks",
		_count_value(first_tokens, 1) > 0 and _count_value(first_tokens, 0) > 0,
		str(first_tokens)
	)
	sim.advance(2)
	var target_after_first_hit: Dictionary = sim.entity(101)
	var first_hit_health: Array = target_after_first_hit.get("member_health", [])
	_check(
		"first_hit_damages_one_member_not_whole_horde",
		int(target_after_first_hit.get("health", 0)) == 450
			and _count_value(first_hit_health, 50) == 1
			and _count_value(first_hit_health, 100) == 4,
		str(first_hit_health)
	)
	sim.advance(3)
	var all_tokens: Array = sim.entity(1).get("member_attack_tokens", [])
	var target_after_cycle: Dictionary = sim.entity(101)
	var cycle_health: Array = target_after_cycle.get("member_health", [])
	_check("all_living_members_receive_own_attack_token", all_tokens == [1, 1, 1, 1, 1], str(all_tokens))
	_check(
		"aggregate_health_is_sum_of_member_health",
		int(target_after_cycle.get("health", -1)) == _sum_ints(cycle_health)
			and int(target_after_cycle.get("health", -1)) == 250,
		str(cycle_health)
	)
	_check("five_member_swings_are_authoritative", _count_event(sim.events, "combat.member_swing", 1) == 5)
	_check("five_member_hits_are_authoritative", _count_event(sim.events, "combat.hit", 1) == 5)
	var assigned_targets: Array = sim.entity(1).get("member_target_indices", [])
	var unique_targets: Dictionary = {}
	for target_index in assigned_targets:
		unique_targets[int(target_index)] = true
	_check(
		"living_attackers_hold_spatially_distributed_member_targets",
		unique_targets.size() == 5 and not unique_targets.has(-1),
		str(assigned_targets)
	)
	_check(
		"movement_authority_remains_at_squad_level",
		typeof(sim.entity(1).get("position")) == TYPE_VECTOR2
			and not sim.entity(1).has("member_positions")
			and int(sim.entity(1).get("target_id", 0)) == 101
	)

	var range_sim = _make_sim()
	(range_sim.entities[101] as Dictionary)["position"] = Vector2(3.0, 0.0)
	(range_sim.entities[101] as Dictionary)["destination"] = Vector2(3.0, 0.0)
	_check("remote_melee_order_is_accepted_as_movement", range_sim.issue_attack([1], 101) == 1)
	range_sim.advance(1)
	_check(
		"melee_does_not_hit_or_animate_outside_source_range",
		String(range_sim.entity(1).get("state", "")) == "run"
			and _count_event(range_sim.events, "combat.swing", 1) == 0
			and int(range_sim.entity(101).get("health", 0)) == 500,
		str(range_sim.entity(1))
	)

	_run_fortress_armor_contract()
	_run_armor_system_contract()
	_run_highlander_body_contract()
	_run_archer_dual_weapon_contract()
	_run_stance_contract()
	_run_auto_acquire_policy_contract()
	_run_mood_attack_check_cadence_contract()
	_run_corpse_lifecycle_contract()
	_run_ai_visual_acquisition_contract()
	_run_projectile_and_radius_contract()

	var replay = _make_sim()
	_check("replay_attack_order_is_accepted", replay.issue_attack([1], 101) == 1)
	sim.advance(14)
	replay.advance(20)
	_check("member_combat_replay_is_deterministic", sim.state_signature() == replay.state_signature())

	print("RETAIL_MEMBER_COMBAT_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _run_member_presentation_contract() -> void:
	var battalion = BattalionScript.new()
	battalion.entity_id = 7
	battalion.object_id = BattalionScript.DEFAULT_OBJECT_ID
	battalion.member_count = 15
	root.add_child(battalion)
	for member_index in range(15):
		var column: int = member_index % 5
		var row: int = int(member_index / 5)
		var slot := Vector3(
			(float(column) - 2.0) * 1.4,
			0.0,
			(float(row) - 1.0) * 1.25
		)
		var visual := Node3D.new()
		visual.position = slot
		battalion.add_child(visual)
		battalion.member_visuals[member_index] = visual
		battalion.member_formation_slots[member_index] = slot
		battalion.member_health_ratios[member_index] = 1.0
	battalion.set_production_exit_progress(0.0)
	_check("produced_horde_starts_hidden_at_doorway", battalion.emerged_member_count() == 0)
	battalion.set_production_exit_progress(0.4)
	_check("produced_members_reveal_progressively", battalion.emerged_member_count() == 6, str(battalion.emerged_member_count()))
	var doorway_member := (battalion.member_visuals[0] as Node3D)
	var doorway_position := doorway_member.position
	battalion.current_state = "run"
	battalion._update_member_positions(0.1)
	_check(
		"emerged_member_runs_from_doorway_into_formation",
		doorway_member.position.distance_to(battalion.member_formation_slot(0)) < doorway_position.distance_to(battalion.member_formation_slot(0))
	)
	battalion.set_production_exit_progress(1.0)
	_check("all_members_are_visible_after_exit", battalion.emerged_member_count() == 15)
	var target := Node3D.new()
	target.position = Vector3(0.0, 0.0, -5.0)
	root.add_child(target)
	battalion.set_attack_target(target, 1.0, 0.2)
	var base_slot: Vector3 = battalion.member_formation_slot(0)
	var engagement_slot: Vector3 = battalion.member_presentation_target(0, "attack")
	_check("idle_member_target_is_formation_slot", battalion.member_presentation_target(0, "idle").is_equal_approx(base_slot))
	_check("melee_member_target_advances_from_rigid_grid", not engagement_slot.is_equal_approx(base_slot), "%s -> %s" % [str(base_slot), str(engagement_slot)])
	_check(
		"melee_members_receive_distinct_engagement_slots",
		not battalion.member_presentation_target(0, "attack").is_equal_approx(battalion.member_presentation_target(1, "attack"))
			and not battalion.member_presentation_target(0, "attack").is_equal_approx(battalion.member_presentation_target(5, "attack"))
	)
	battalion.current_state = "attack"
	_check("all_living_members_begin_reflowing_toward_combat", battalion.reflowing_member_count() == 15, str(battalion.reflowing_member_count()))
	var before := (battalion.member_visuals[0] as Node3D).position
	battalion._update_member_positions(0.1)
	var after := (battalion.member_visuals[0] as Node3D).position
	_check(
		"member_reflow_moves_toward_own_engagement_slot",
		after.distance_to(engagement_slot) < before.distance_to(engagement_slot),
		"%s -> %s target=%s" % [str(before), str(after), str(engagement_slot)]
	)
	var archer = BattalionScript.new()
	archer.object_id = BattalionScript.ARCHER_OBJECT_ID
	archer.member_count = 1
	root.add_child(archer)
	archer.member_formation_slots[0] = Vector3(1.0, 0.0, 2.0)
	archer.set_attack_target(target, 1.0, 0.2)
	_check(
		"ranged_member_preserves_formation_attack_slot",
		archer.member_presentation_target(0, "attack").is_equal_approx(Vector3(1.0, 0.0, 2.0))
	)
	battalion.set_authoritative_position(Vector3.ZERO, true)
	battalion.set_authoritative_position(Vector3(10.0, 0.0, 0.0))
	battalion._update_root_presentation(0.05)
	_check(
		"authoritative_tick_motion_interpolates_instead_of_block_stepping",
		battalion.position.x > 0.0 and battalion.position.x < 10.0,
		str(battalion.position)
	)
	battalion.set_facing_direction(Vector2(0.0, -1.0), 90.0)
	battalion.set_facing_direction(Vector2(1.0, 0.0), 90.0)
	battalion._update_root_presentation(0.05)
	_check(
		"battalion_turns_toward_travel_direction_without_snapping",
		battalion.rotation.y < 0.0 and battalion.rotation.y > -PI * 0.5,
		str(battalion.rotation.y)
	)
	battalion.queue_free()
	archer.queue_free()
	target.queue_free()


func _run_fortress_armor_contract() -> void:
	var sim = SimScript.new()
	sim.setup({}, {
		"enable_base_loop": true,
		"member_health": 100,
		"unit_rules": _unit_rules(5, 50),
	})
	sim.ai_enabled = false
	var fortress_id: int = sim.fortress_id(SimScript.ENEMY_TEAM)
	var fortress: Dictionary = sim.structure(fortress_id)
	var fortress_position := Vector2(fortress.get("position", Vector2.ZERO))
	(sim.entities[1] as Dictionary)["position"] = fortress_position + Vector2(1.0, 0.0)
	(sim.entities[1] as Dictionary)["destination"] = Vector2((sim.entities[1] as Dictionary)["position"])
	_check("retail_fortress_keeps_source_maximum_health", int(fortress.get("maximum_health", 0)) == 7500)
	_check("fortress_attack_order_is_accepted", sim.issue_attack([1], fortress_id) == 1)
	sim.advance(2)
	_check(
		"structure_volley_does_not_land_as_one_synchronized_hit",
		int(sim.structure(fortress_id).get("health", 0)) == 7500,
		str(sim.structure(fortress_id))
	)
	sim.advance(4)
	_check(
		"fortress_armor_reduces_slash_volley_to_twenty_percent",
		int(sim.structure(fortress_id).get("health", 0)) == 7450,
		str(sim.structure(fortress_id))
	)


func _run_armor_system_contract() -> void:
	## armor.ini, compiled end-to-end (importer/openbfme_importer/armor_compiler.py):
	## attacker damage type vs the victim's authored ArmorSet. The injected
	## tables mirror the retail rows with armor.ini line provenance; the
	## pack-driven sweep in retail_slice_runner re-proves them from converted
	## documents.
	var sim = SimScript.new()
	sim.setup({}, {
		"enable_base_loop": true,
		"member_health": 100,
		"unit_rules": _unit_rules(5, 50),
	})
	sim.ai_enabled = false
	# KnightArmor (armor.ini:613-631): PIERCE 40% (line 618), SPECIALIST 200%
	# (line 619). SoldierArmor (armor.ini:483-500) upgrades to SoldierHeavyArmor
	# (armor.ini:502-519): PIERCE 20% with DamageScalar 120%.
	sim._unit_armor[SimScript.KNIGHT_OBJECT_ID] = {
		"set_id": "KnightArmor",
		"damage_scalar": 1.0,
		"scalars": {"default": 1.0, "slash": 0.40, "pierce": 0.40, "specialist": 2.0, "crush": 0.60, "cavalry": 1.0},
		"upgrades": {},
	}
	sim._unit_armor[SimScript.SOLDIER_OBJECT_ID] = {
		"set_id": "SoldierArmor",
		"damage_scalar": 1.0,
		"scalars": {"default": 1.0, "slash": 1.0, "pierce": 1.25, "specialist": 0.50, "crush": 1.25, "cavalry": 1.50},
		"upgrades": {
			"Upgrade_GondorHeavyArmor": {
				"set_id": "SoldierHeavyArmor",
				"damage_scalar": 1.20,
				"scalars": {"default": 0.50, "slash": 0.50, "pierce": 0.20, "specialist": 0.20, "cavalry": 1.20, "crush": 0.65, "siege": 0.50},
			},
		},
	}
	# GondorSwordUpgraded (weapon.ini:5537-5560): 90 SLASH
	# (GONDOR_SOLDIER_SWORD_UPGRADE, gamedata.ini:1113) with DamageScalar
	# 200% ANY +INFANTRY -HERO / 150% ANY +HERO.
	sim._unit_weapon_upgrades[SimScript.SOLDIER_OBJECT_ID] = {
		"Upgrade_GondorForgedBlades": {
			"kind": "weapon-swap",
			"damage": 90.0,
			"damage_type": "slash",
			"scalars": [
				{"percent": 2.0, "filter": "ANY +INFANTRY -HERO", "relation": "ANY", "plus": ["INFANTRY"], "minus": ["HERO"]},
				{"percent": 1.5, "filter": "ANY +HERO", "relation": "ANY", "plus": ["HERO"], "minus": []},
			],
		},
	}
	var knight: Dictionary = sim.entities[101]
	knight["object_id"] = SimScript.KNIGHT_OBJECT_ID
	knight["category"] = "cavalry"
	# Archer (pierce) vs KnightArmor: 50 x 0.40 = 20 (was 50 pre-armor).
	var archer: Dictionary = sim.entities[2]
	archer["damage_type"] = "pierce"
	sim._apply_member_damage(2, 0, 101, 50, "battalion", 0, 0)
	var pierce_hit := _last_hit_event(sim)
	_check(
		"archer_pierce_vs_knight_armor_scales_to_retail_forty_percent",
		int(knight.get("member_health", [])[0]) == 80
			and is_equal_approx(float(pierce_hit.get("armor_scalar", 0.0)), 0.40)
			and String(pierce_hit.get("damage_type", "")) == "pierce",
		"health=%s scalar=%s" % [str(knight.get("member_health", [])), str(pierce_hit.get("armor_scalar", ""))]
	)
	# Tower guard (specialist) vs KnightArmor: 50 x 2.00 = 100 (pike counter).
	var pike: Dictionary = sim.entities[1]
	pike["damage_type"] = "specialist"
	sim._apply_member_damage(1, 0, 101, 50, "battalion", 0, 1)
	var pike_hit := _last_hit_event(sim)
	_check(
		"pike_specialist_vs_knight_armor_doubles_to_retail_counter",
		int(knight.get("member_health", [])[1]) == 0
			and is_equal_approx(float(pike_hit.get("armor_scalar", 0.0)), 2.0),
		"health=%s scalar=%s" % [str(knight.get("member_health", [])), str(pike_hit.get("armor_scalar", ""))]
	)
	# Specialist vs the fortress's compiled table: 50 x 0.12 = 6.
	var fortress_id: int = sim.fortress_id(SimScript.ENEMY_TEAM)
	sim._apply_structure_damage(1, fortress_id, 50, "specialist")
	_check(
		"pike_vs_fortress_suffers_structure_penalty",
		int(sim.structure(fortress_id).get("health", 0)) == 7494,
		str(sim.structure(fortress_id).get("health", -1))
	)
	# Missing kinds stay recorded provisionals, never silent.
	_check(
		"non_fortress_legacy_kinds_are_recorded_provisionals",
		sim.structure_armor_provisional_kinds.has("farm")
			and sim.structure_armor_provisional_kinds.has("barracks")
			and sim.structure_armor_provisional_kinds.has("archery_range")
			and sim.structure_armor_provisional_kinds.has("stable")
			and not sim.structure_armor_provisional_kinds.has("fortress"),
		str(sim.structure_armor_provisional_kinds)
	)
	# Forge honesty: recorded per-horde equipment changes the effective armor /
	# damage by the compiled amount and nothing else.
	var soldier: Dictionary = sim.entities[1]
	soldier["category"] = "infantry"
	soldier["object_id"] = SimScript.SOLDIER_OBJECT_ID
	soldier["damage_type"] = "slash"
	var enemy_soldier: Dictionary = sim.entities[101]
	enemy_soldier["object_id"] = SimScript.SOLDIER_OBJECT_ID
	enemy_soldier["category"] = "infantry"
	sim._apply_equipment_to_horde(soldier, ["Upgrade_GondorForgedBlades"])
	sim._apply_equipment_to_horde(enemy_soldier, ["Upgrade_GondorHeavyArmor"])
	_check(
		"forge_equipment_is_recorded_per_horde_not_team_wide",
		(soldier.get("applied_upgrades", {}) as Dictionary).has("Upgrade_GondorForgedBlades")
			and (enemy_soldier.get("applied_upgrades", {}) as Dictionary).has("Upgrade_GondorHeavyArmor")
			and not (soldier.get("applied_upgrades", {}) as Dictionary).has("Upgrade_GondorHeavyArmor")
			and String(enemy_soldier.get("active_armor_upgrade", "")) == "Upgrade_GondorHeavyArmor",
		"attacker=%s victim=%s" % [str(soldier.get("applied_upgrades", "")), str(enemy_soldier.get("applied_upgrades", ""))]
	)
	# Forged blades: 90 (compiled, replaces base 40) x 2.00 vs infantry = 180
	# against 100 member health; the unupgraded 50 could never one-shot.
	sim._apply_member_damage(1, 0, 101, 90, "battalion", 0, 2)
	var blades_hit := _last_hit_event(sim)
	_check(
		"forged_blades_apply_compiled_damage_and_infantry_scalar",
		int(enemy_soldier.get("member_health", [])[2]) == 0
			and int(blades_hit.get("amount", 0)) == 100
			and is_equal_approx(float(blades_hit.get("weapon_factor", 0.0)), 2.0),
		"amount=%s factor=%s" % [str(blades_hit.get("amount", "")), str(blades_hit.get("weapon_factor", ""))]
	)
	# Heavy armor: pierce 50 x (0.20 x 1.20 DamageScalar) = 12, set swapped
	# only on the upgraded horde.
	(enemy_soldier.get("member_health", []) as Array)[3] = 100
	sim._apply_member_damage(2, 0, 101, 50, "battalion", 0, 3)
	var armor_hit := _last_hit_event(sim)
	_check(
		"heavy_armor_swaps_to_compiled_upgraded_set",
		int(enemy_soldier.get("member_health", [])[3]) == 88
			and is_equal_approx(float(armor_hit.get("armor_scalar", 0.0)), 0.24),
		"health=%s scalar=%s" % [str(enemy_soldier.get("member_health", [])), str(armor_hit.get("armor_scalar", ""))]
	)
	# Fire arrows (GondorArcherBowFireWarhead, weapon.ini:4678-4714): the
	# pierce nugget stays the primary hit while the authored flame bonus lands
	# as its own typed hit, scaled 25% vs non-structures
	# (GONDOR_ARCHER_FIRE_UPGRADE_DAMAGE = 32, gamedata.ini:1134).
	var fire_sim = _make_sim()
	var fire_archer: Dictionary = fire_sim.entities[2]
	fire_archer["damage_type"] = "pierce"
	fire_sim._unit_weapon_upgrades[SimScript.ARCHER_OBJECT_ID] = {
		"Upgrade_GondorArcherFireArrows": {
			"kind": "warhead-upgrade",
			"damage": 25.0,
			"damage_type": "pierce",
			"scalars": [],
			"bonus_nuggets": [
				{"damage": 1.0, "damage_type": "flame", "scalars": [{"percent": 500.0, "filter": "NONE +MINE", "relation": "NONE", "plus": ["MINE"], "minus": []}]},
				{"damage": 32.0, "damage_type": "flame", "scalars": [{"percent": 0.25, "filter": "ALL -STRUCTURE", "relation": "ALL", "plus": [], "minus": ["STRUCTURE"]}]},
			],
		},
	}
	fire_sim._apply_equipment_to_horde(fire_archer, ["Upgrade_GondorArcherFireArrows"])
	var fire_effect: Dictionary = fire_sim._applied_weapon_effect(fire_archer)
	_check("fire_arrows_warhead_upgrade_resolves_primary_and_bonus", String(fire_effect.get("kind", "")) == "warhead-upgrade" and float(fire_effect.get("damage", 0.0)) == 25.0 and (fire_effect.get("bonus_nuggets", []) as Array).size() == 2, str(fire_effect))
	var fire_target: Dictionary = fire_sim.entities[101]
	fire_target["category"] = "infantry"
	var fire_bonus_factor := fire_sim._damage_scalar_factor((fire_effect.get("bonus_nuggets", []) as Array)[1].get("scalars", []), fire_target, "battalion")
	var mine_bonus_factor := fire_sim._damage_scalar_factor((fire_effect.get("bonus_nuggets", []) as Array)[0].get("scalars", []), fire_target, "battalion")
	_check(
		"fire_arrows_bonus_scales_twenty_five_percent_vs_units_and_mine_nugget_stays_inert",
		is_equal_approx(fire_bonus_factor, 0.25) and is_equal_approx(mine_bonus_factor, 1.0),
		"bonus=%s mine=%s" % [str(fire_bonus_factor), str(mine_bonus_factor)]
	)
	var structure_factor := fire_sim._damage_scalar_factor((fire_effect.get("bonus_nuggets", []) as Array)[1].get("scalars", []), fire_sim.structure(fire_sim.fortress_id(SimScript.ENEMY_TEAM)), "structure")
	_check("fire_arrows_bonus_is_unscaled_vs_structures", is_equal_approx(structure_factor, 1.0), str(structure_factor))


func _run_highlander_body_contract() -> void:
	var rules := _unit_rules(5, 50)
	(rules[SimScript.KNIGHT_OBJECT_ID] as Dictionary)["highlander_body"] = true
	var build_rules := SimScript.STRUCTURE_BUILD_RULES.duplicate(true)
	(build_rules["fortress"] as Dictionary)["highlander_body"] = true
	var sim = SimScript.new()
	sim.setup({}, {
		"enable_base_loop": true,
		"unit_rules": rules,
		"faction_manifest": {
			"structure_build_rules": build_rules,
		},
	})
	sim.ai_enabled = false
	var normal: Dictionary = sim.entities[1]
	var highlander: Dictionary = sim.entities[103]
	_check(
		"ordinary_body_keeps_optional_policy_key_absent",
		not normal.has("highlander_body")
	)
	_check(
		"authored_highlander_policy_is_per_member",
		highlander.get("highlander_body") == true
	)
	# Distinguish the complete source ordering: Rallying Call first turns 200
	# into 400 DamageInfo input, Highlander clamps that to 199, then 40% armor
	# rounds to 80, leaving 120. Clamping before the outgoing boost leaves 40.
	highlander["object_id"] = SimScript.KNIGHT_OBJECT_ID
	normal["rally_until_tick"] = sim.tick_index + 1
	normal["rally_damage_mult"] = 2.0
	sim._unit_armor[SimScript.KNIGHT_OBJECT_ID] = {
		"set_id": "HighlanderProbeArmor",
		"damage_scalar": 1.0,
		"scalars": {"default": 0.4, "slash": 0.4},
		"upgrades": {},
	}
	sim._apply_member_damage(1, 0, 103, 200, "battalion", 0, 0, "slash")
	_check(
		"highlander_member_clamps_raw_before_armor",
		int((highlander.get("member_health", []) as Array)[0]) == 120,
		str(highlander.get("member_health", []))
	)
	normal["rally_until_tick"] = -1
	var mixed_health_before := int(
		(highlander.get("member_health", []) as Array)[1]
	)
	normal["damage_type"] = ""
	normal["damage_components"] = [
		{"damage_type": "slash", "value": 50.0},
		{"damage_type": "unresistable", "value": 50.0},
	]
	sim._apply_member_damage(1, 0, 103, 100, "battalion", 0, 1)
	var refusal_event: Dictionary = sim.events.back()
	_check(
		"highlander_mixed_unresistable_damage_refuses_without_mutation",
		int((highlander.get("member_health", []) as Array)[1])
				== mixed_health_before
			and String(refusal_event.get("kind", ""))
				== "combat.damage_refused"
			and String(refusal_event.get("reason", ""))
				== "highlander-mixed-unresistable",
		str(refusal_event)
	)
	normal["damage_components"] = []
	normal["damage_type"] = "slash"
	# With neutral armor, repeated ordinary hits consume every remaining raw
	# point except the final one, independently for the selected member.
	(sim._unit_armor[SimScript.KNIGHT_OBJECT_ID] as Dictionary)["scalars"] = {
		"default": 1.0,
		"slash": 1.0,
		"unresistable": 1.0,
	}
	sim._apply_member_damage(1, 0, 103, 9999, "battalion", 0, 0, "slash")
	sim._apply_member_damage(1, 0, 103, 9999, "battalion", 0, 0, "slash")
	_check(
		"highlander_member_repeated_normal_damage_stops_at_one",
		int((highlander.get("member_health", []) as Array)[0]) == 1
	)
	sim._apply_member_damage(
		1, 0, 103, 1, "battalion", 0, 0, "UNRESISTABLE"
	)
	_check(
		"highlander_member_unresistable_uses_normal_death_path",
		int((highlander.get("member_health", []) as Array)[0]) == 0
	)

	var fortress_id: int = sim.fortress_id(SimScript.ENEMY_TEAM)
	var fortress: Dictionary = sim.structures[fortress_id]
	_check(
		"highlander_structure_policy_is_authored_state",
		fortress.get("highlander_body") == true
	)
	fortress["health"] = 100
	fortress["maximum_health"] = 100
	sim._structure_armor["fortress"] = {
		"damage_scalar": 1.0,
		"scalars": {"default": 1.0, "slash": 1.0, "unresistable": 1.0},
	}
	sim._unit_weapon_upgrades[SimScript.SOLDIER_OBJECT_ID] = {
		"Upgrade_HighlanderStructureProbe": {
			"kind": "weapon-swap",
			"damage": 9999.0,
			"damage_type": "slash",
			"scalars": [{
				"percent": 2.0,
				"filter": "ANY +STRUCTURE",
				"relation": "ANY",
				"plus": ["STRUCTURE"],
				"minus": [],
			}],
		},
	}
	sim._apply_equipment_to_horde(
		normal, ["Upgrade_HighlanderStructureProbe"]
	)
	sim._apply_structure_damage(1, fortress_id, 9999, "slash")
	sim._apply_structure_damage(1, fortress_id, 9999, "slash")
	_check(
		"highlander_structure_repeated_normal_damage_stops_at_one",
		int(fortress.get("health", 0)) == 1
	)
	sim._apply_structure_damage(1, fortress_id, 1, "unresistable")
	_check(
		"highlander_structure_unresistable_uses_normal_destruction_path",
		int(fortress.get("health", -1)) == 0
	)
	var restored = SimScript.new()
	restored.setup({}, {
		"enable_base_loop": true,
		"unit_rules": rules,
		"faction_manifest": {"structure_build_rules": build_rules},
	})
	_check("highlander_snapshot_restores", restored.restore(sim.snapshot()))
	_check(
		"highlander_snapshot_restores_policy_and_hash",
		(restored.entities[103] as Dictionary).get("highlander_body") == true
			and (restored.structures[fortress_id] as Dictionary)
				.get("highlander_body") == true
			and restored.state_hash() == sim.state_hash()
	)
	var removal_sim = SimScript.new()
	removal_sim.setup({}, {"unit_rules": rules})
	removal_sim.ai_enabled = false
	var removable: Dictionary = removal_sim.entities[103]
	removable["health"] = 0
	removable["member_health"] = [0, 0, 0, 0, 0]
	removable["corpse_expire_tick"] = 0
	removal_sim._cleanup_expired_corpses()
	_check(
		"highlander_policy_does_not_block_non_damage_lifecycle_removal",
		not removal_sim.entities.has(103)
	)


func _last_hit_event(sim) -> Dictionary:
	## The most recent combat.hit event (music/voice intents may follow it).
	for index in range(sim.events.size() - 1, -1, -1):
		var event: Dictionary = sim.events[index]
		if String(event.get("kind", "")) == "combat.hit":
			return event
	return {}


func _run_archer_dual_weapon_contract() -> void:
	var ranged = _make_sim()
	(ranged.entities[2] as Dictionary)["position"] = Vector2.ZERO
	(ranged.entities[2] as Dictionary)["destination"] = Vector2.ZERO
	(ranged.entities[101] as Dictionary)["position"] = Vector2(10.0, 0.0)
	(ranged.entities[101] as Dictionary)["destination"] = Vector2(10.0, 0.0)
	_check("archer_ranged_order_is_accepted", ranged.issue_attack([2], 101) == 1)
	ranged.advance(1)
	_check(
		"archer_uses_bow_outside_close_switch_distance",
		String(ranged.entity(2).get("active_weapon_mode", "")) == "default"
			and is_equal_approx(float(ranged.entity(2).get("attack_range", 0.0)), 30.0)
			and int(ranged.entity(2).get("member_damage", 0)) == 25,
		str(ranged.entity(2))
	)
	var closing = _make_sim()
	(closing.entities[2] as Dictionary)["position"] = Vector2.ZERO
	(closing.entities[2] as Dictionary)["destination"] = Vector2.ZERO
	(closing.entities[101] as Dictionary)["position"] = Vector2(3.0, 0.0)
	(closing.entities[101] as Dictionary)["destination"] = Vector2(3.0, 0.0)
	_check("archer_close_order_is_accepted", closing.issue_attack([2], 101) == 1)
	closing.advance(1)
	_check(
		"archer_switches_to_melee_and_closes_to_melee_range",
		String(closing.entity(2).get("active_weapon_mode", "")) == "close"
			and String(closing.entity(2).get("state", "")) == "run"
			and is_equal_approx(float(closing.entity(2).get("attack_range", 0.0)), 1.15)
			and int(closing.entity(2).get("member_damage", 0)) == 5,
		str(closing.entity(2))
	)
	(closing.entities[2] as Dictionary)["position"] = Vector2(2.0, 0.0)
	(closing.entities[2] as Dictionary)["route"] = []
	closing.advance(1)
	_check(
		"archer_close_attack_uses_ini_melee_timing",
		String(closing.entity(2).get("state", "")) == "attack"
			and int(closing.entity(2).get("pre_attack_ticks", 0)) == 7
			and int(closing.entity(2).get("attack_period_ticks", 0)) == 34,
		str(closing.entity(2))
	)


func _run_stance_contract() -> void:
	var sim = _make_sim()
	_check("retail_stance_defaults_to_battle", String(sim.entity(1).get("stance", "")) == "Battle")
	_check("toggle_stance_accepts_selected_horde", sim.issue_toggle_stance([1]) == 1)
	_check("toggle_order_moves_battle_to_aggressive", String(sim.entity(1).get("stance", "")) == "Aggressive")
	_check("set_hold_ground_accepts_selected_horde", sim.issue_set_stance([1], "HoldGround") == 1)
	_check("hold_ground_state_persists", String(sim.entity(1).get("stance", "")) == "HoldGround")
	(sim.entities[1] as Dictionary)["position"] = Vector2.ZERO
	(sim.entities[1] as Dictionary)["route"] = []
	(sim.entities[1] as Dictionary)["target_id"] = 0
	(sim.entities[101] as Dictionary)["position"] = Vector2(10.0, 0.0)
	sim.advance(1)
	_check(
		"hold_ground_does_not_chase_outside_weapon_range",
		int(sim.entity(1).get("target_id", -1)) == 0 and String(sim.entity(1).get("state", "")) == "idle",
		str(sim.entity(1))
	)
	sim.issue_set_stance([1], "Battle")
	sim.advance(1)
	_check(
		"battle_stance_auto_acquires_inside_source_vision",
		int(sim.entity(1).get("target_id", 0)) == 101 and String(sim.entity(1).get("state", "")) == "run",
		str(sim.entity(1))
	)


func _run_auto_acquire_policy_contract() -> void:
	var disabled = _make_sim()
	var source: Dictionary = disabled.entities[1]
	_check(
		"missing_aiupdate_field_keeps_entity_state_absent",
		not source.has("auto_acquire_enabled")
			and not source.has("auto_acquire_attack_buildings")
			and not source.has("auto_acquire_while_stealthed")
			and not source.has("mood_attack_check_rate_ticks")
	)
	source["auto_acquire_enabled"] = false
	_check(
		"authored_no_disables_idle_acquisition",
		disabled._nearest_auto_target(source).is_empty()
	)
	_check(
		"authored_no_does_not_block_explicit_attack_orders",
		disabled.issue_attack([1], 101) == 1
	)

	var structures_only = _make_sim()
	(structures_only.entities[101] as Dictionary)["health"] = 0
	var structure_source: Dictionary = structures_only.entities[1]
	var enemy_fortress := 900
	structures_only.structures[enemy_fortress] = {
		"id": enemy_fortress,
		"team": SimScript.ENEMY_TEAM,
		"health": 1000,
		"maximum_health": 1000,
		"position": Vector2(5.0, 0.0),
		"structure_kind": "fortress",
	}
	structure_source["auto_acquire_attack_buildings"] = false
	_check(
		"yes_without_attack_buildings_skips_structures",
		structures_only._nearest_auto_target(structure_source).is_empty()
	)
	structure_source["auto_acquire_attack_buildings"] = true
	var structure_target: Dictionary = structures_only._nearest_auto_target(structure_source)
	_check(
		"attack_buildings_admits_hostile_structures",
		int(structure_target.get("id", 0)) == enemy_fortress
			and String(structure_target.get("kind", "")) == "structure",
		str(structure_target)
	)

	var stealth_source = _make_sim()
	stealth_source._spatial_rebuild()
	var cloaked: Dictionary = stealth_source.entities[1]
	cloaked["stealth_until_tick"] = stealth_source.tick_index + 10
	cloaked["auto_acquire_while_stealthed"] = false
	_check(
		"yes_without_stealthed_skips_cloaked_source_acquisition",
		stealth_source._nearest_auto_target(cloaked).is_empty()
	)
	cloaked["auto_acquire_while_stealthed"] = true
	_check(
		"stealthed_flag_allows_cloaked_source_acquisition",
		int(stealth_source._nearest_auto_target(cloaked).get("id", 0)) == 101
	)
	(stealth_source.entities[101] as Dictionary)["stealth_until_tick"] = stealth_source.tick_index + 10
	_check(
		"stealthed_flag_does_not_reveal_cloaked_targets",
		stealth_source._nearest_auto_target(cloaked).is_empty()
	)
	_check(
		"explicit_orders_remain_independent_of_target_auto_acquisition",
		stealth_source.issue_attack([1], 101) == 1
	)

	var temporal_source = _make_sim()
	temporal_source._spatial_rebuild()
	var temporal_row: Dictionary = temporal_source.entities[1]
	temporal_row["target_id"] = 101
	temporal_row["target_kind"] = "battalion"
	temporal_row["order_kind"] = "auto_attack"
	temporal_row["auto_acquire_while_stealthed"] = false
	temporal_row["stealth_until_tick"] = temporal_source.tick_index + 10
	temporal_source.advance(1)
	_check(
		"cloaking_source_drops_existing_disallowed_auto_target",
		int(temporal_source.entity(1).get("target_id", -1)) == 0
	)

	var temporal_target = _make_sim()
	temporal_target._spatial_rebuild()
	var target_row: Dictionary = temporal_target.entities[1]
	target_row["target_id"] = 101
	target_row["target_kind"] = "battalion"
	target_row["order_kind"] = "auto_attack"
	(temporal_target.entities[101] as Dictionary)["stealth_until_tick"] = temporal_target.tick_index + 10
	temporal_target.advance(1)
	_check(
		"cloaking_target_drops_existing_auto_target",
		int(temporal_target.entity(1).get("target_id", -1)) == 0
	)
	var explicit_target = _make_sim()
	explicit_target._spatial_rebuild()
	_check("explicit_transition_order_is_accepted", explicit_target.issue_attack([1], 101) == 1)
	(explicit_target.entities[101] as Dictionary)["stealth_until_tick"] = explicit_target.tick_index + 10
	explicit_target.advance(1)
	_check(
		"target_cloaking_does_not_cancel_explicit_attack",
		int(explicit_target.entity(1).get("target_id", 0)) == 101
	)


func _run_mood_attack_check_cadence_contract() -> void:
	var cadence = _make_sim()
	var source: Dictionary = cadence.entities[1]
	source["mood_attack_check_rate_ticks"] = 6
	for hostile_id in [101, 102]:
		(cadence.entities[hostile_id] as Dictionary)["position"] = Vector2(100.0, 0.0)
	cadence._spatial_rebuild()
	cadence.advance(1)
	var next_check := int(source.get("mood_next_check_tick", -1))
	var rng_after_first_scan: Array = cadence._logic_random_state.duplicate()
	_check(
		"first_eligible_idle_scan_schedules_authored_jittered_interval",
		next_check >= cadence.tick_index + 3
			and next_check <= cadence.tick_index + 9
			and rng_after_first_scan.size() == 6,
		str(source)
	)
	(cadence.entities[101] as Dictionary)["position"] = Vector2(5.0, 0.0)
	cadence._spatial_rebuild()
	var ticks_before_boundary: int = next_check - cadence.tick_index - 1
	if ticks_before_boundary > 0:
		cadence.advance(ticks_before_boundary)
	_check(
		"authored_cadence_blocks_rescan_before_boundary",
		int(cadence.entity(1).get("target_id", 0)) == 0
			and cadence._logic_random_state == rng_after_first_scan
	)
	var adopted = _make_sim()
	_check("mood_cadence_snapshot_restores", adopted.restore(cadence.snapshot()))
	_check("mood_cadence_restored_hash_matches", adopted.state_hash() == cadence.state_hash())
	cadence.advance(1)
	adopted.advance(1)
	_check(
		"authored_cadence_rescans_on_exact_boundary",
		int(cadence.entity(1).get("target_id", 0)) == 101
			and int(adopted.entity(1).get("target_id", 0)) == 101
	)
	_check(
		"only_first_scheduled_interval_consumes_rng",
		cadence._logic_random_state == rng_after_first_scan
			and adopted._logic_random_state == rng_after_first_scan
			and cadence.state_hash() == adopted.state_hash()
	)

	var routed = _make_sim()
	(routed.entities[1] as Dictionary)["mood_attack_check_rate_ticks"] = 5
	(routed.entities[1] as Dictionary)["route"] = [Vector2(5.0, 0.0)]
	routed.advance(1)
	_check(
		"routed_unit_does_not_scan_or_consume_rng",
		routed._logic_random_state.is_empty()
			and not (routed.entities[1] as Dictionary).has("mood_next_check_tick")
	)

	var disabled = _make_sim()
	var disabled_source: Dictionary = disabled.entities[1]
	disabled_source["mood_attack_check_rate_ticks"] = 5
	disabled_source["auto_acquire_enabled"] = false
	disabled.advance(1)
	_check(
		"disabled_auto_acquire_does_not_consume_mood_rng",
		disabled._logic_random_state.is_empty()
			and not disabled_source.has("mood_next_check_tick")
	)

	var explicit = _make_sim()
	(explicit.entities[1] as Dictionary)["mood_attack_check_rate_ticks"] = 5
	_check("explicit_mood_fixture_attack_is_accepted", explicit.issue_attack([1], 101) == 1)
	explicit.advance(1)
	_check(
		"explicit_attack_does_not_scan_or_consume_rng",
		explicit._logic_random_state.is_empty()
			and not (explicit.entities[1] as Dictionary).has("mood_next_check_tick")
			and bool((explicit.entities[1] as Dictionary).get(
				"mood_randomize_next_check", false
			))
	)
	_check("explicit_stop_reenters_idle", explicit.issue_stop([1]) == 1)
	explicit.advance(1)
	_check(
		"new_idle_epoch_consumes_one_new_jitter_draw",
		explicit._logic_random_state.size() == 6
			and not bool((explicit.entities[1] as Dictionary).get(
				"mood_randomize_next_check", true
			))
			and (explicit.entities[1] as Dictionary).has("mood_next_check_tick")
	)

	var attack_move = _make_sim()
	(attack_move.entities[1] as Dictionary)["mood_attack_check_rate_ticks"] = 5
	_check(
		"attack_move_mood_fixture_is_accepted",
		attack_move.issue_attack_move([1], Vector2(20.0, 0.0)) == 1
	)
	attack_move.advance(1)
	_check(
		"attack_move_does_not_consume_mood_rng",
		attack_move._logic_random_state.is_empty()
			and not (attack_move.entities[1] as Dictionary).has("mood_next_check_tick")
	)


func _run_corpse_lifecycle_contract() -> void:
	var sim = _make_sim()
	var corpse: Dictionary = sim.entities[101]
	corpse["member_health"] = [0, 0, 0, 0, 0]
	corpse["health"] = 0
	corpse["state"] = "death"
	corpse["corpse_expire_tick"] = sim.tick_index + SimScript.CORPSE_LIFETIME_TICKS
	sim.advance(SimScript.CORPSE_LIFETIME_TICKS - 1)
	_check("battalion_corpse_remains_for_source_readability_window", sim.entities.has(101))
	sim.advance(1)
	_check("battalion_corpse_is_culled_after_one_minute", not sim.entities.has(101))
	_check("corpse_expiry_emits_authoritative_event", _count_event(sim.events, "battalion.corpse_expired", 101) == 1)

func _run_ai_visual_acquisition_contract() -> void:
	var sim = _make_sim()
	(sim.entities[1] as Dictionary)["position"] = Vector2.ZERO
	(sim.entities[2] as Dictionary)["position"] = Vector2(-100.0, 0.0)
	(sim.entities[101] as Dictionary)["position"] = Vector2(40.0, 0.0)
	(sim.entities[101] as Dictionary)["route"] = []
	sim._update_enemy_ai()
	_check(
		"ai_advances_without_remote_combat_target",
		int(sim.entity(101).get("target_id", -1)) == 0
			and String(sim.entity(101).get("state", "")) == "run"
			and not Array(sim.entity(101).get("route", [])).is_empty(),
		str(sim.entity(101))
	)
	(sim.entities[101] as Dictionary)["position"] = Vector2(10.0, 0.0)
	(sim.entities[101] as Dictionary)["route"] = []
	sim._update_enemy_ai()
	_check("ai_acquires_only_inside_source_vision", int(sim.entity(101).get("target_id", 0)) == 1, str(sim.entity(101)))


func _run_projectile_and_radius_contract() -> void:
	var sim = _projectile_sim("ENEMIES")
	var target_before := int((sim.entities[2] as Dictionary).get("health", 0))
	var attackers: Array[int] = [1]
	_check("projectile_attack_order_is_accepted", sim.issue_attack(attackers, 2) == 1)
	var launch_tick := -1
	var impact_tick := -1
	for _index in 10:
		sim.advance(1)
		if not sim.projectiles.is_empty():
			var ids: Array = sim.projectiles.keys()
			ids.sort()
			var projectile := sim.projectiles[ids[0]] as Dictionary
			launch_tick = int(projectile.get("launch_tick", -1))
			impact_tick = int(projectile.get("impact_tick", -1))
			break
	_check("projectile_launch_is_observable", launch_tick >= 0 and sim.projectiles.size() == 1)
	_check("projectile_does_not_damage_on_release_tick", int((sim.entities[2] as Dictionary).get("health", 0)) == target_before)
	_check(
		"projectile_flight_ticks_use_distance_speed_formula",
		impact_tick - launch_tick == maxi(1, ceili(5.0 / 10.0 / SimScript.TICK_SECONDS)),
		"launch=%d impact=%d" % [launch_tick, impact_tick]
	)
	while sim.tick_index < impact_tick:
		sim.advance(1)
	_check("projectile_direct_damage_lands_on_impact", int((sim.entities[2] as Dictionary).get("health", 0)) == target_before - 200)
	_check("outer_radius_nugget_applies_linear_taper", int((sim.entities[3] as Dictionary).get("health", 0)) == 915, str((sim.entities[3] as Dictionary).get("member_health", [])))
	_check("radius_damage_excludes_outside_battalion", int((sim.entities[4] as Dictionary).get("health", 0)) == 1000)
	_check("enemy_only_radius_spares_allies", int((sim.entities[5] as Dictionary).get("health", 0)) == 1000)
	_check("projectile_impact_clears_authoritative_row", sim.projectiles.is_empty())

	var retarget = _projectile_sim("ENEMIES")
	(retarget.entities[2] as Dictionary)["member_health"] = [1000, 1000]
	(retarget.entities[2] as Dictionary)["member_maximum_health"] = 1000
	(retarget.entities[2] as Dictionary)["health"] = 2000
	retarget.issue_attack(attackers, 2)
	while retarget.projectiles.is_empty():
		retarget.advance(1)
	var projectile_ids: Array = retarget.projectiles.keys()
	projectile_ids.sort()
	var stored_member := int((retarget.projectiles[projectile_ids[0]] as Dictionary).get("member_index", -1))
	var health_values: Array = (retarget.entities[2] as Dictionary)["member_health"]
	health_values[stored_member] = 0
	(retarget.entities[2] as Dictionary)["member_health"] = health_values
	(retarget.entities[2] as Dictionary)["health"] = 1000
	var retarget_impact := int((retarget.projectiles[projectile_ids[0]] as Dictionary)["impact_tick"])
	while retarget.tick_index < retarget_impact:
		retarget.advance(1)
	_check("dead_stored_member_retargets_live_member", int((retarget.entities[2] as Dictionary).get("health", 0)) == 800)

	var cancelled = _projectile_sim("ENEMIES")
	cancelled.issue_attack(attackers, 2)
	while cancelled.projectiles.is_empty():
		cancelled.advance(1)
	(cancelled.entities[2] as Dictionary)["member_health"] = [0]
	(cancelled.entities[2] as Dictionary)["health"] = 0
	var cancel_ids: Array = cancelled.projectiles.keys()
	cancel_ids.sort()
	var cancel_impact := int((cancelled.projectiles[cancel_ids[0]] as Dictionary)["impact_tick"])
	while cancelled.tick_index < cancel_impact:
		cancelled.advance(1)
	_check("dead_target_cancels_projectile", cancelled.projectiles.is_empty() and _count_event(cancelled.events, "combat.projectile_cancelled", 1) == 1)

	var melee = _projectile_sim("ENEMIES", false)
	var melee_before := int((melee.entities[2] as Dictionary).get("health", 0))
	melee.issue_attack(attackers, 2)
	var melee_hit_tick := -1
	for _index in 10:
		melee.advance(1)
		if int((melee.entities[2] as Dictionary).get("health", 0)) < melee_before:
			melee_hit_tick = melee.tick_index
			break
	_check("melee_keeps_instant_release_damage", melee_hit_tick >= 0 and int((melee.entities[2] as Dictionary).get("health", 0)) == melee_before - 200)
	_check("melee_never_allocates_projectile_state", melee.projectiles.is_empty())


func _projectile_sim(affects: String, projectile_capable: bool = true):
	var attacker_rule := _unit_rule("ProjectileAttacker", 1, 200, 20.0, 20.0)
	attacker_rule["category"] = "siege"
	attacker_rule["damage_type"] = "siege"
	attacker_rule["damage_components"] = [
		{"value": 100.0, "damage_type": "siege", "radius": 2.0, "damage_taper_off": 0.0, "death_type": "EXPLODED", "damage_fx_type": "BIG_ROCK"},
		{"value": 100.0, "damage_type": "siege", "radius": 10.0, "damage_taper_off": 50.0, "death_type": "EXPLODED", "damage_fx_type": "BIG_ROCK"},
	]
	attacker_rule["radius_damage_affects"] = affects
	if projectile_capable:
		attacker_rule["projectile_object_id"] = "FixtureRockProjectile"
		attacker_rule["projectile_speed"] = 10.0
	var target_rule := _unit_rule("ProjectileTarget", 1, 1, 1.0, 20.0)
	target_rule["member_health"] = 1000
	var sim = SimScript.new()
	var rules := {
		SimScript.SOLDIER_OBJECT_ID: target_rule,
		SimScript.ARCHER_OBJECT_ID: target_rule,
		SimScript.TOWER_GUARD_OBJECT_ID: target_rule,
		SimScript.KNIGHT_OBJECT_ID: target_rule,
		"ProjectileAttacker": attacker_rule,
		"ProjectileTarget": target_rule,
	}
	sim.setup({}, {"unit_rules": rules, "spawn_initial_battalions": false})
	sim.ai_enabled = false
	sim.base_loop_enabled = false
	sim.entities.clear()
	sim.structures.clear()
	sim._add_battalion(1, SimScript.PLAYER_TEAM, Vector2.ZERO, "Attacker", "ProjectileAttacker", "ProjectileAttacker", 0, attacker_rule)
	sim._add_battalion(2, SimScript.ENEMY_TEAM, Vector2(5.0, 0.0), "Primary", "ProjectileTarget", "ProjectileTarget", 0, target_rule)
	sim._add_battalion(3, SimScript.ENEMY_TEAM, Vector2(8.0, 0.0), "Splash", "ProjectileTarget", "ProjectileTarget", 0, target_rule)
	sim._add_battalion(4, SimScript.ENEMY_TEAM, Vector2(16.0, 0.0), "Outside", "ProjectileTarget", "ProjectileTarget", 0, target_rule)
	sim._add_battalion(5, SimScript.PLAYER_TEAM, Vector2(8.0, 0.0), "Ally", "ProjectileTarget", "ProjectileTarget", 0, target_rule)
	for entity_id in [1, 2, 3, 4, 5]:
		(sim.entities[entity_id] as Dictionary)["auto_acquire_enabled"] = false
	sim._spatial_rebuild()
	return sim


func _unit_rules(member_count: int, member_damage: int) -> Dictionary:
	var rules := {
		SimScript.SOLDIER_OBJECT_ID: _unit_rule(SimScript.SOLDIER_HORDE_ID, member_count, member_damage, 1.15, 17.5),
		SimScript.ARCHER_OBJECT_ID: _unit_rule(SimScript.ARCHER_OBJECT_ID, member_count, member_damage, 30.0, 37.0),
		SimScript.TOWER_GUARD_OBJECT_ID: _unit_rule(SimScript.TOWER_GUARD_OBJECT_ID, member_count, member_damage, 3.5, 17.5),
		SimScript.KNIGHT_OBJECT_ID: _unit_rule(SimScript.KNIGHT_OBJECT_ID, member_count, member_damage, 1.15, 27.5),
	}
	var archer: Dictionary = rules[SimScript.ARCHER_OBJECT_ID]
	archer["weapon_modes"] = {
		"default": _weapon_mode("GondorArcherBow", 30.0, 25, 0, 2, 0),
		"close": _weapon_mode("GondorArcherBowMelee", 1.15, 5, 17, 7, 10),
	}
	archer["default_weapon_mode"] = "default"
	archer["close_weapon_mode"] = "close"
	archer["close_weapon_switch_distance"] = 4.0
	archer["close_weapon_switch_distance_source"] = 40.0
	(rules[SimScript.SOLDIER_OBJECT_ID] as Dictionary)["stances"] = _stance_contract("FighterHorde", 1.25, 1.10, 0.85, 0.75, 1.0)
	archer["stances"] = _stance_contract("ArcherHorde", 1.10, 1.30, 0.70, 0.80, 0.40)
	(rules[SimScript.TOWER_GUARD_OBJECT_ID] as Dictionary)["stances"] = _stance_contract("PikeHorde", 1.25, 1.10, 0.85, 0.75, 1.0)
	(rules[SimScript.KNIGHT_OBJECT_ID] as Dictionary)["stances"] = _stance_contract("CavalryHorde", 1.25, 1.10, 0.85, 0.75, 1.0)
	return rules


func _stance_contract(template: String, aggressive_damage: float, aggressive_incoming: float, hold_damage: float, hold_incoming: float, hold_speed: float) -> Dictionary:
	return {
		"template": template,
		"default": "Battle",
		"cycleOrder": ["HoldGround", "Battle", "Aggressive"],
		"states": {
			"HoldGround": {"damageMultiplier": hold_damage, "incomingDamageMultiplier": hold_incoming, "visionMultiplier": 0.1, "speedMultiplier": hold_speed},
			"Battle": {"damageMultiplier": 1.0, "incomingDamageMultiplier": 1.0, "visionMultiplier": 1.0, "speedMultiplier": 1.0},
			"Aggressive": {"damageMultiplier": aggressive_damage, "incomingDamageMultiplier": aggressive_incoming, "visionMultiplier": 2.0, "speedMultiplier": 1.0},
		},
	}


func _weapon_mode(name: String, attack_range: float, damage: int, delay_ticks: int, pre_ticks: int, firing_ticks: int) -> Dictionary:
	return {
		"name": name,
		"attack_range": attack_range,
		"attack_range_source": attack_range * 10.0,
		"minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0,
		"delay_between_shots_ms": float(delay_ticks * 100),
		"pre_attack_delay_ms": float(pre_ticks * 100),
		"firing_duration_ms": float(firing_ticks * 100),
		"attack_period_ticks": maxi(1, delay_ticks + pre_ticks + firing_ticks),
		"pre_attack_ticks": pre_ticks,
		"firing_duration_ticks": firing_ticks,
		"member_damage": damage,
	}


func _unit_rule(horde_id: String, member_count: int, member_damage: int, attack_range: float, vision_range: float) -> Dictionary:
	var positions: Array[Vector3] = []
	for index in range(member_count):
		positions.append(Vector3(float(index), 0.0, 0.0))
	return {
		"horde_id": horde_id,
		"speed": 1.0,
		"speed_source": 10.0,
		"acceleration": 1.0,
		"acceleration_source": 10.0,
		"turn_rate_degrees_per_second": 180.0,
		"braking": 1.0,
		"braking_source": 10.0,
		"attack_range": attack_range,
		"attack_range_source": attack_range * 10.0,
		"minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0,
		"vision_range": vision_range,
		"vision_range_source": vision_range * 10.0,
		"delay_between_shots_ms": 600.0,
		"pre_attack_delay_ms": 200.0,
		"firing_duration_ms": 200.0,
		"attack_period_ticks": 10,
		"pre_attack_ticks": 2,
		"firing_duration_ticks": 2,
		"member_damage": member_damage,
		"member_count": member_count,
		"formation_positions": positions,
		"provenance": {},
	}


func _count_value(values: Array, expected: int) -> int:
	var result := 0
	for value in values:
		if int(value) == expected:
			result += 1
	return result


func _sum_ints(values: Array) -> int:
	var result := 0
	for value in values:
		result += int(value)
	return result


func _count_event(events: Array, kind: String, entity_id: int) -> int:
	var result := 0
	for event_value in events:
		if typeof(event_value) != TYPE_DICTIONARY:
			continue
		var event: Dictionary = event_value
		if String(event.get("kind", "")) == kind and int(event.get("entity_id", 0)) == entity_id:
			result += 1
	return result


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_MEMBER_COMBAT PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_MEMBER_COMBAT FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])

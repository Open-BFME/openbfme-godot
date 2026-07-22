extends SceneTree

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
var BattalionScript: Script

var passed := 0
var failed := 0


func _initialize() -> void:
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
	_run_archer_dual_weapon_contract()
	_run_stance_contract()
	_run_corpse_lifecycle_contract()
	_run_ai_visual_acquisition_contract()

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

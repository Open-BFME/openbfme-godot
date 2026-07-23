extends SceneTree

## Hero-ability batch 1 proof: leadership passive auras, weapon-mode toggles,
## terror/fear, and generic timed-modifier kinds (VISION/HEALTH/RESIST_FEAR/
## CRUSH/EXPERIENCE) over the shared per-entity timed-modifier table.
## Fixture: the lockstep determinism harness extended with synthetic heroes
## carrying one ability of each family (the selected pack's compiled rows for
## these families are placeholders — importer follow-up — so rules here are
## synthetic and exercise exactly the fields the sim consumes).

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const CommandScript = preload("res://src/retail_slice/retail_command.gd")

const HERO_OBJECT_ID := "test.object.hero"
const HERO_UNIT_TYPE := "test.object.hero-horde"
const HERO2_OBJECT_ID := "test.object.hero-two"
const HERO2_UNIT_TYPE := "test.object.hero-two-horde"
const FLYER_OBJECT_ID := "test.object.flyer"
const FLYER_UNIT_TYPE := "test.object.flyer-horde"

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _make_sim():
	var sim = SimScript.new()
	sim._rules = _harness_rules()
	sim.setup({}, {})
	sim.ai_enabled = false
	for structure_id in sim.structure_ids():
		if structure_id != 1003:
			sim.structures.erase(structure_id)
	for entity_id in sim.entity_ids():
		if not [1, 2, 3, 101].has(entity_id):
			sim.entities.erase(entity_id)
	sim.expansion_pads.clear()
	sim._ai_production_plan.clear()
	sim._ai_build_order_index = SimScript.AI_BUILD_ORDER.size()
	sim._enemy_ai_construction_attempted = true
	sim._enemy_ai_construction_resolved = true
	# Synthetic hero roster: rules registered before spawn so ability state
	# attaches through the normal _add_battalion path.
	sim._rules["unit_rules"][HERO_OBJECT_ID] = _hero_rule()
	sim._rules["unit_rules"][HERO2_OBJECT_ID] = _hero_rule()
	sim._rules["unit_rules"][FLYER_OBJECT_ID] = _flyer_rule()
	sim._unit_ability_rules[HERO_UNIT_TYPE] = sim._scaled_ability_rules(_hero_ability_rules("TestLeadership"), 0.0)
	sim._unit_ability_rules[HERO2_UNIT_TYPE] = sim._scaled_ability_rules(_hero_ability_rules("TestLeadership"), 0.0)
	sim._unit_experience_rules[HERO_UNIT_TYPE] = {
		"max_level": 2,
		"initial_rank": 1,
		"levels": [
			{"rank": 1, "required_experience": 0, "experience_award": 0, "health_add": 0.0, "damage_add": 0.0},
			{"rank": 2, "required_experience": 100, "experience_award": 0, "health_add": 0.0, "damage_add": 0.0},
		],
	}
	return sim


func _harness_rules() -> Dictionary:
	return {
		"enable_base_loop": true,
		"starting_resources": 10000,
		"ai_attack_delay_ticks": 100000,
		"unit_rules": {
			SimScript.SOLDIER_OBJECT_ID: _unit_rule(SimScript.SOLDIER_HORDE_ID, false),
			SimScript.ARCHER_OBJECT_ID: _unit_rule(SimScript.ARCHER_OBJECT_ID, false),
			SimScript.TOWER_GUARD_OBJECT_ID: _unit_rule(SimScript.TOWER_GUARD_OBJECT_ID, false),
			SimScript.KNIGHT_OBJECT_ID: _unit_rule(SimScript.KNIGHT_OBJECT_ID, false),
			SimScript.BUILDER_OBJECT_ID: _unit_rule(SimScript.BUILDER_OBJECT_ID, true),
		},
	}


func _unit_rule(horde_id: String, is_builder: bool) -> Dictionary:
	return {
		"horde_id": horde_id,
		"speed": 1.0,
		"speed_source": 10.0,
		"acceleration": 1.0,
		"acceleration_source": 10.0,
		"turn_rate_degrees_per_second": 180.0,
		"braking": 1.0,
		"braking_source": 10.0,
		"attack_range": 1.15,
		"attack_range_source": 11.5,
		"minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0,
		"vision_range": 4.0,
		"vision_range_source": 40.0,
		"delay_between_shots_ms": 600.0,
		"pre_attack_delay_ms": 200.0,
		"firing_duration_ms": 200.0,
		"attack_period_ticks": 10,
		"pre_attack_ticks": 2,
		"firing_duration_ticks": 2,
		"member_damage": 10,
		"member_health": 200,
		"member_count": 1,
		"formation_positions": [Vector3.ZERO],
		"provenance": {},
		"is_builder": is_builder,
	}


func _hero_rule() -> Dictionary:
	var rule := _unit_rule(HERO_UNIT_TYPE, false)
	rule["category"] = "hero"
	rule["member_health"] = 800
	rule["member_damage"] = 40
	rule["default_weapon_mode"] = "default"
	rule["weapon_modes"] = {
		"default": {
			"name": "sword",
			"attack_range": 1.15,
			"attack_range_source": 11.5,
			"minimum_attack_range": 0.0,
			"minimum_attack_range_source": 0.0,
			"delay_between_shots_ms": 600.0,
			"pre_attack_delay_ms": 200.0,
			"firing_duration_ms": 200.0,
			"attack_period_ticks": 10,
			"pre_attack_ticks": 2,
			"firing_duration_ticks": 2,
			"member_damage": 40,
		},
		"bow": {
			"name": "bow",
			"attack_range": 12.0,
			"attack_range_source": 120.0,
			"minimum_attack_range": 0.0,
			"minimum_attack_range_source": 0.0,
			"delay_between_shots_ms": 1200.0,
			"pre_attack_delay_ms": 300.0,
			"firing_duration_ms": 200.0,
			"attack_period_ticks": 17,
			"pre_attack_ticks": 3,
			"firing_duration_ticks": 2,
			"member_damage": 18,
		},
	}
	return rule


func _flyer_rule() -> Dictionary:
	var rule := _unit_rule(FLYER_UNIT_TYPE, false)
	rule["is_flyer"] = true
	return rule


func _hero_ability_rules(bonus_name: String) -> Array[Dictionary]:
	## One synthetic ability per batch-1 family. Field names mirror the
	## converter's camelCase raw effect fields consumed by _scaled_ability_rules.
	return [
		{
			"ability_id": "Command_TestLeadership",
			"slot": 1,
			"special_power_id": "SpecialAbilityFakeLeadership",
			"targeting": "self",
			"cooldown_ticks": 0,
			"required_level": 1,
			"level_gate_resolved": true,
			"castable": false,
			"availability_reason": "button is authored NONPRESSABLE (passive display)",
			"effect": {
				"kind": "leadership-aura",
				"bonusName": bonus_name,
				"range": 10.0,
				"affectsSelf": false,
				"affects": "",
				"startsActive": true,
				"modifiers": [
					{"kind": "DAMAGE_MULT", "value": 2.0},
					{"kind": "ARMOR", "value": 0.25},
					{"kind": "EXPERIENCE", "value": 2.0},
				],
			},
		},
		{
			"ability_id": "Command_TestToggleWeapon",
			"slot": 2,
			"special_power_id": "SpecialAbilityToggleWeapon",
			"targeting": "self",
			"cooldown_ticks": 0,
			"required_level": 1,
			"level_gate_resolved": true,
			"castable": true,
			"availability_reason": "",
			"effect": {"kind": "weapon-toggle", "toggleMode": "bow"},
		},
		{
			"ability_id": "Command_TestScreech",
			"slot": 3,
			"special_power_id": "SpecialAbilityScreech",
			"targeting": "self",
			"cooldown_ticks": 20,
			"required_level": 1,
			"level_gate_resolved": true,
			"castable": true,
			"availability_reason": "",
			"effect": {
				"kind": "terror",
				"radius": 8.0,
				"durationMs": 2000.0,
				"scatterStrength": 3.0,
				"modifiers": [
					{"kind": "DAMAGE_MULT", "value": 0.5},
					{"kind": "ARMOR", "value": -0.5},
				],
			},
		},
		{
			"ability_id": "Command_TestWarCry",
			"slot": 4,
			"special_power_id": "SpecialAbilityWarCry",
			"targeting": "self",
			"cooldown_ticks": 0,
			"required_level": 1,
			"level_gate_resolved": true,
			"castable": true,
			"availability_reason": "",
			"effect": {
				"kind": "attribute-modifier",
				"durationMs": 1000.0,
				"range": 0.0,
				"modifiers": [
					{"kind": "VISION", "value": 2.0},
					{"kind": "HEALTH", "value": 1.5},
					{"kind": "RESIST_FEAR", "value": 1.0},
					{"kind": "CRUSH", "value": 1.5},
				],
			},
		},
	]


func _spawn(sim, id: int, team: int, at: Vector2, object_id: String, unit_type: String) -> Dictionary:
	sim._add_battalion(id, team, at, "T%d" % id, object_id, unit_type)
	return sim.entities[id]


func _table(row: Dictionary) -> Dictionary:
	return row.get("timed_modifiers", {}) as Dictionary


func _kill(row: Dictionary) -> void:
	var members: Array = row.get("member_health", [])
	for index in range(members.size()):
		members[index] = 0
	row["member_health"] = members
	row["health"] = 0
	row["state"] = "death"


func _run() -> void:
	_run_aura_checks()
	_run_toggle_checks()
	_run_terror_checks()
	_run_timed_buff_checks()
	_run_determinism_checks()
	print("RETAIL_HERO_ABILITY_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _run_aura_checks() -> void:
	var sim = _make_sim()
	for entity_id in sim.entity_ids():
		sim.entities.erase(entity_id)
	var hero: Dictionary = _spawn(sim, 5, 0, Vector2.ZERO, HERO_OBJECT_ID, HERO_UNIT_TYPE)
	var near_ally: Dictionary = _spawn(sim, 6, 0, Vector2(2.0, 0.0), SimScript.SOLDIER_OBJECT_ID, SimScript.SOLDIER_HORDE_ID)
	var far_ally: Dictionary = _spawn(sim, 7, 0, Vector2(50.0, 0.0), SimScript.SOLDIER_OBJECT_ID, SimScript.SOLDIER_HORDE_ID)
	var enemy: Dictionary = _spawn(sim, 105, 1, Vector2(-2.0, 0.0), SimScript.SOLDIER_OBJECT_ID, SimScript.SOLDIER_HORDE_ID)
	sim.advance(5)
	_check("aura_buffs_ally_in_radius", _table(near_ally).has("aura:TestLeadership"))
	_check("aura_damage_mult_applies_to_ally", is_equal_approx(sim._ability_outgoing_multiplier(near_ally), 2.0))
	_check("aura_armor_applies_to_ally", is_equal_approx(sim._ability_incoming_multiplier(near_ally), 0.75))
	_check("aura_skips_ally_out_of_radius", not _table(far_ally).has("aura:TestLeadership"))
	_check("aura_never_buffs_enemies", _table(enemy).is_empty())
	_check("aura_without_affects_self_skips_hero", not _table(hero).has("aura:TestLeadership"))
	# EXPERIENCE aura: hero 2 radiates onto hero 1 (heroes are allies), whose
	# synthetic chain levels at 100 XP: a 50-XP award doubles to 100.
	var hero2: Dictionary = _spawn(sim, 8, 0, Vector2(0.0, 2.0), HERO2_OBJECT_ID, HERO2_UNIT_TYPE)
	sim.advance(5)
	_check("aura_reaches_allied_hero", _table(hero).has("aura:TestLeadership"))
	sim._award_experience(hero, 50)
	_check("aura_experience_doubles_xp_intake", int(hero.get("experience_xp", 0)) == 100 and int(hero.get("level", 0)) == 2)
	# Same-named leaderships never stack: hero and hero2 share the bonus name,
	# the ally carries one aura key and the plain 2.0 damage factor.
	_check("same_named_auras_do_not_stack", _table(near_ally).size() == 1 and is_equal_approx(sim._ability_outgoing_multiplier(near_ally), 2.0))
	# Different bonus names stack across heroes: rename hero2's aura rule.
	sim._unit_ability_rules[HERO2_UNIT_TYPE] = sim._scaled_ability_rules(_hero_ability_rules("TestLeadershipTwo"), 0.0)
	sim.advance(10)
	_check("cross_hero_auras_stack", _table(near_ally).has("aura:TestLeadership") and _table(near_ally).has("aura:TestLeadershipTwo") and is_equal_approx(sim._ability_outgoing_multiplier(near_ally), 4.0))
	# Knockdown suspends the aura within one interval.
	hero2["knockdown_ticks"] = 60
	hero2["knocked_down"] = true
	sim.advance(10)
	_check("knocked_down_hero_stops_radiating", not _table(near_ally).has("aura:TestLeadershipTwo") and _table(near_ally).has("aura:TestLeadership"))
	# Death drops the surviving aura within one interval.
	_kill(hero)
	sim.advance(10)
	_check("dead_hero_aura_drops", not _table(near_ally).has("aura:TestLeadership"))
	# Leaving the radius drops the grant on the next recompute.
	hero2["knockdown_ticks"] = 0
	hero2["knocked_down"] = false
	sim._unit_ability_rules[HERO2_UNIT_TYPE] = sim._scaled_ability_rules(_hero_ability_rules("TestLeadership"), 0.0)
	sim.advance(10)
	_check("standing_hero_resumes_radiating", _table(near_ally).has("aura:TestLeadership"))
	near_ally["position"] = Vector2(80.0, 0.0)
	sim.advance(10)
	_check("ally_leaving_radius_loses_aura", not _table(near_ally).has("aura:TestLeadership"))


func _run_toggle_checks() -> void:
	var sim = _make_sim()
	for entity_id in sim.entity_ids():
		sim.entities.erase(entity_id)
	var hero: Dictionary = _spawn(sim, 5, 0, Vector2.ZERO, HERO_OBJECT_ID, HERO_UNIT_TYPE)
	var engage: Dictionary = sim.cast_ability(5, "Command_TestToggleWeapon", Vector2.ZERO)
	_check("toggle_cast_engages_bow_profile", bool(engage.get("ok", false)) and String(engage.get("mode", "")) == "bow")
	_check("toggle_swaps_attack_stats", is_equal_approx(float(hero.get("attack_range", 0.0)), 12.0) and int(hero.get("member_damage", 0)) == 18)
	_check("toggle_mode_field_persists", String(hero.get("weapon_toggle_mode", "")) == "bow")
	sim.advance(30)
	_check("toggle_survives_ticks", String(hero.get("weapon_toggle_mode", "")) == "bow" and sim._weapon_mode_for_distance(hero, 0.5) == "bow")
	# Snapshot round-trip preserves the toggled mode.
	var bytes: PackedByteArray = sim.snapshot()
	var restored = _make_sim()
	var restore_ok: bool = restored.restore(bytes)
	var restored_hero: Dictionary = restored.entities.get(5, {})
	_check("toggle_round_trips_snapshot", restore_ok and String(restored_hero.get("weapon_toggle_mode", "")) == "bow" and restored.state_hash() == sim.state_hash())
	# Toggling again releases back to the default profile.
	var release: Dictionary = sim.cast_ability(5, "Command_TestToggleWeapon", Vector2.ZERO)
	_check("toggle_recast_releases_default", bool(release.get("ok", false)) and String(release.get("mode", "")) == "default" and String(hero.get("weapon_toggle_mode", "")) == "")
	_check("release_restores_attack_stats", is_equal_approx(float(hero.get("attack_range", 0.0)), 1.15) and int(hero.get("member_damage", 0)) == 40)
	# Unknown toggle mode fails closed.
	var bad_rules: Array[Dictionary] = _hero_ability_rules("X")
	(((bad_rules[1] as Dictionary)["effect"]) as Dictionary)["toggleMode"] = "crossbow"
	sim._unit_ability_rules[HERO_UNIT_TYPE] = sim._scaled_ability_rules(bad_rules, 0.0)
	var bad: Dictionary = sim.cast_ability(5, "Command_TestToggleWeapon", Vector2.ZERO)
	_check("unknown_toggle_mode_fails_closed", not bool(bad.get("ok", false)) and String(bad.get("reason", "")).begins_with("toggle-mode-unavailable"))


func _run_terror_checks() -> void:
	var sim = _make_sim()
	for entity_id in sim.entity_ids():
		sim.entities.erase(entity_id)
	var hero: Dictionary = _spawn(sim, 5, 0, Vector2.ZERO, HERO_OBJECT_ID, HERO_UNIT_TYPE)
	var ground_enemy: Dictionary = _spawn(sim, 105, 1, Vector2(3.0, 0.0), SimScript.SOLDIER_OBJECT_ID, SimScript.SOLDIER_HORDE_ID)
	var flyer_enemy: Dictionary = _spawn(sim, 106, 1, Vector2(0.0, 3.0), FLYER_OBJECT_ID, FLYER_UNIT_TYPE)
	var far_enemy: Dictionary = _spawn(sim, 107, 1, Vector2(60.0, 0.0), SimScript.SOLDIER_OBJECT_ID, SimScript.SOLDIER_HORDE_ID)
	var resistant_enemy: Dictionary = _spawn(sim, 108, 1, Vector2(-3.0, 0.0), SimScript.SOLDIER_OBJECT_ID, SimScript.SOLDIER_HORDE_ID)
	sim._set_timed_modifier(resistant_enemy, "test:resist", [{"kind": "RESIST_FEAR", "value": 1.0}], sim.tick_index + 1000)
	var cast: Dictionary = sim.cast_ability(5, "Command_TestScreech", Vector2.ZERO)
	_check("terror_casts_and_counts_victims", bool(cast.get("ok", false)) and int(cast.get("affected", 0)) == 2)
	_check("terror_debuffs_enemy_in_radius", _table(ground_enemy).has("fear:Command_TestScreech"))
	_check("terror_damage_penalty_applies", is_equal_approx(sim._ability_outgoing_multiplier(ground_enemy), 0.5))
	_check("terror_armor_penalty_applies", is_equal_approx(sim._ability_incoming_multiplier(ground_enemy), 1.5))
	_check("terror_scatters_ground_enemy", Vector2(ground_enemy.get("position", Vector2.ZERO)).distance_to(Vector2.ZERO) > 3.5)
	_check("terror_never_knocks_down", not bool(ground_enemy.get("knocked_down", false)) and int(ground_enemy.get("knockdown_ticks", 0)) == 0)
	_check("flyer_debuffed_but_scatter_immune", _table(flyer_enemy).has("fear:Command_TestScreech") and Vector2(flyer_enemy.get("position", Vector2.ZERO)) == Vector2(0.0, 3.0))
	_check("terror_skips_enemy_out_of_radius", not _table(far_enemy).has("fear:Command_TestScreech"))
	_check("resist_fear_blocks_the_debuff", not _table(resistant_enemy).has("fear:Command_TestScreech"))
	_check("terror_honors_cooldown", String(sim.cast_ability(5, "Command_TestScreech", Vector2.ZERO).get("reason", "")) == "cooldown-active")
	# durationMs 2000 at 0.1s ticks = 20 ticks: present through the last tick,
	# gone on the exact expiry tick.
	var cast_tick: int = sim.tick_index
	sim.advance(19)
	_check("terror_debuff_holds_through_duration", sim.tick_index == cast_tick + 19 and _table(ground_enemy).has("fear:Command_TestScreech"))
	sim.advance(1)
	_check("terror_debuff_expires_on_exact_tick", not _table(ground_enemy).has("fear:Command_TestScreech"))
	_check("hero_row_untouched_by_terror", not _table(hero).has("fear:Command_TestScreech"))


func _run_timed_buff_checks() -> void:
	var sim = _make_sim()
	for entity_id in sim.entity_ids():
		sim.entities.erase(entity_id)
	var hero: Dictionary = _spawn(sim, 5, 0, Vector2.ZERO, HERO_OBJECT_ID, HERO_UNIT_TYPE)
	hero["member_health"] = [400]
	hero["health"] = 400
	hero["last_damage_tick"] = sim.tick_index
	var cast: Dictionary = sim.cast_ability(5, "Command_TestWarCry", Vector2.ZERO)
	_check("timed_buff_casts_on_hero", bool(cast.get("ok", false)) and _table(hero).has("ability:Command_TestWarCry"))
	_check("vision_kind_multiplies_vision", is_equal_approx(sim._ability_vision_multiplier(hero), 2.0))
	_check("resist_fear_kind_reads_active", sim._timed_modifier_active(hero, "RESIST_FEAR"))
	_check("crush_kind_multiplies_trample", is_equal_approx(sim._timed_modifier_product(hero, "CRUSH"), 1.5))
	var cast_tick: int = sim.tick_index
	sim.advance(4)
	# HEALTH 1.5 on 800 member max = (1.5-1.0)*800*0.1 = 40 healed per tick.
	_check("health_kind_regenerates_while_active", int(hero.get("health", 0)) == 400 + 4 * 40)
	sim.advance(5)
	_check("timed_buff_holds_through_duration", sim.tick_index == cast_tick + 9 and _table(hero).has("ability:Command_TestWarCry"))
	sim.advance(1)
	_check("timed_buff_expires_on_exact_tick", not _table(hero).has("ability:Command_TestWarCry"))
	var health_at_expiry := int(hero.get("health", 0))
	sim.advance(5)
	_check("regen_stops_at_expiry", int(hero.get("health", 0)) == health_at_expiry)
	# Recast refreshes the same keyed grant instead of stacking it.
	sim.cast_ability(5, "Command_TestWarCry", Vector2.ZERO)
	sim.cast_ability(5, "Command_TestWarCry", Vector2.ZERO)
	_check("recast_refreshes_instead_of_stacking", _table(hero).size() == 1 and is_equal_approx(sim._ability_vision_multiplier(hero), 2.0))


func _command(tick: int, seq: int, type: String, args: Dictionary, team: int = 0) -> Dictionary:
	return {"tick": tick, "team": team, "seq": seq, "type": type, "args": args}


func _determinism_sim():
	var sim = _make_sim()
	_spawn(sim, 5, 0, Vector2(-6.0, 0.0), HERO_OBJECT_ID, HERO_UNIT_TYPE)
	_spawn(sim, 6, 0, Vector2(-4.0, 0.0), SimScript.SOLDIER_OBJECT_ID, SimScript.SOLDIER_HORDE_ID)
	_spawn(sim, 105, 1, Vector2(-2.0, 1.0), SimScript.SOLDIER_OBJECT_ID, SimScript.SOLDIER_HORDE_ID)
	_spawn(sim, 106, 1, Vector2(-2.0, -1.0), FLYER_OBJECT_ID, FLYER_UNIT_TYPE)
	return sim


func _scripted_log() -> Array[Dictionary]:
	## All four families fire inside the twin run: passive auras radiate from
	## tick 5, plus scripted toggle/terror/buff casts and ordinary combat.
	return [
		_command(2, 1, "cast_ability", {"hero_id": 5, "ability_id": "Command_TestToggleWeapon", "target_point": Vector2.ZERO}),
		_command(10, 2, "cast_ability", {"hero_id": 5, "ability_id": "Command_TestWarCry", "target_point": Vector2.ZERO}),
		_command(20, 3, "cast_ability", {"hero_id": 5, "ability_id": "Command_TestScreech", "target_point": Vector2.ZERO}),
		_command(40, 4, "issue_attack", {"ids": [5], "target_id": 105}),
		_command(60, 5, "cast_ability", {"hero_id": 5, "ability_id": "Command_TestScreech", "target_point": Vector2.ZERO}),
		_command(120, 6, "cast_ability", {"hero_id": 5, "ability_id": "Command_TestToggleWeapon", "target_point": Vector2.ZERO}),
		_command(150, 7, "cast_ability", {"hero_id": 5, "ability_id": "Command_TestWarCry", "target_point": Vector2.ZERO}),
		_command(200, 8, "issue_move", {"ids": [5, 6], "destination": Vector2(-10.0, 2.0)}),
	]


func _run_determinism_checks() -> void:
	var commands := _scripted_log()
	var sim_a = _determinism_sim()
	var sim_b = _determinism_sim()
	var altered = _determinism_sim()
	var altered_commands := commands.duplicate(true)
	# The altered run skips one terror cast: divergence proves the hash is
	# sensitive to the timed-modifier table (fear entries + scatter).
	altered_commands.remove_at(2)
	var submissions_ok := true
	for cmd in commands:
		submissions_ok = sim_a.submit_command(cmd) and submissions_ok
		submissions_ok = sim_b.submit_command(cmd) and submissions_ok
	for cmd in altered_commands:
		submissions_ok = altered.submit_command(cmd) and submissions_ok
	var codec_ok := true
	for cmd in commands:
		var encoded: PackedByteArray = CommandScript.encode(cmd)
		var decoded: Dictionary = CommandScript.decode(encoded)
		codec_ok = codec_ok and not encoded.is_empty() and hash(decoded) == hash(cmd)
	_check("cast_ability_command_codec_round_trip", codec_ok and submissions_ok)
	var twin_ok := true
	var sensitivity_ok := false
	var snapshot_ok := true
	var restored = null
	var first_divergence := -1
	for expected_tick in range(1, 601):
		sim_a.tick()
		sim_b.tick()
		altered.tick()
		if restored != null:
			restored.tick()
		var hash_a: String = sim_a.state_hash()
		if hash_a != sim_b.state_hash() and first_divergence < 0:
			first_divergence = expected_tick
			twin_ok = false
		if hash_a != altered.state_hash():
			sensitivity_ok = true
		if expected_tick == 300:
			# Mid-everything: aura live, toggle engaged state, fear history,
			# pending commands still queued (none remain past 200, so the
			# table state itself is the payload).
			restored = _make_sim()
			snapshot_ok = restored.restore(sim_a.snapshot()) and restored.state_hash() == hash_a
		elif restored != null:
			snapshot_ok = snapshot_ok and restored.state_hash() == hash_a
	_check("twin_run_hash_equality_600_ticks_all_families", twin_ok, "first_divergence=%d" % first_divergence)
	_check("hash_sensitive_to_timed_modifier_table", sensitivity_ok)
	_check("snapshot_round_trip_mid_everything", snapshot_ok)
	# Direct hash sensitivity of the modifier table on otherwise identical sims.
	var probe_a = _determinism_sim()
	var probe_b = _determinism_sim()
	var baseline_equal: bool = probe_a.state_hash() == probe_b.state_hash()
	probe_b._set_timed_modifier(probe_b.entities[6] as Dictionary, "aura:Probe", [{"kind": "ARMOR", "value": 0.1}], 50)
	_check("modifier_table_alone_changes_hash", baseline_equal and probe_a.state_hash() != probe_b.state_hash())


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_HERO_ABILITY PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_HERO_ABILITY FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])

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


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_HERO_ABILITY_RUNNER")
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
	sim.force_ai_construction_complete()
	# Synthetic hero roster: rules registered before spawn so ability state
	# attaches through the normal _add_battalion path.
	sim._rules["unit_rules"][HERO_OBJECT_ID] = _hero_rule()
	sim._rules["unit_rules"][HERO2_OBJECT_ID] = _hero_rule()
	sim._rules["unit_rules"][FLYER_OBJECT_ID] = _flyer_rule()
	sim._unit_ability_rules[HERO_UNIT_TYPE] = sim._scaled_ability_rules(_combined_ability_rules("TestLeadership"), 0.0)
	sim._unit_ability_rules[HERO2_UNIT_TYPE] = sim._scaled_ability_rules(_combined_ability_rules("TestLeadership"), 0.0)
	sim._unit_experience_rules[SimScript.SOLDIER_HORDE_ID] = {
		"max_level": 2,
		"initial_rank": 1,
		"levels": [
			{"rank": 1, "required_experience": 0, "experience_award": 0, "health_add": 0.0, "damage_add": 0.0},
			{"rank": 2, "required_experience": 100, "experience_award": 0, "health_add": 0.0, "damage_add": 0.0},
		],
	}
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
		"mounted": {
			"name": "sword-mounted",
			"attack_range": 2.5,
			"attack_range_source": 25.0,
			"minimum_attack_range": 0.0,
			"minimum_attack_range_source": 0.0,
			"delay_between_shots_ms": 1400.0,
			"pre_attack_delay_ms": 500.0,
			"firing_duration_ms": 500.0,
			"attack_period_ticks": 14,
			"pre_attack_ticks": 5,
			"firing_duration_ticks": 5,
			"member_damage": 60,
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
			"ability_id": "Command_TestMount",
			"slot": 5,
			"special_power_id": "SpecialAbilityToggleMounted",
			"targeting": "self",
			"cooldown_ticks": 10,
			"required_level": 1,
			"level_gate_resolved": true,
			"castable": true,
			"availability_reason": "",
			"effect": {
				"kind": "mount-toggle",
				"mountedSpeed": 3.0,
				"mountedWeaponModeKey": "mounted",
				"mountedWeaponId": "TestSwordMounted",
				"unpackMs": 2000.0,
				"packMs": 2000.0,
			},
		},
		{
			"ability_id": "Command_TestCapture",
			"slot": 6,
			"special_power_id": "SpecialAbilityCaptureBuilding",
			"targeting": "enemy-object",
			"cooldown_ticks": 0,
			"required_level": 1,
			"level_gate_resolved": true,
			"castable": true,
			"availability_reason": "",
			"effect": {
				"kind": "capture-building",
				"startAbilityRange": 15.0,
				"unpackMs": 1.0,
				"preparationMs": 5000.0,
				"packMs": 1.0,
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


func _combined_ability_rules(bonus_name: String) -> Array[Dictionary]:
	var rules: Array[Dictionary] = _hero_ability_rules(bonus_name)
	rules.append_array(_long_tail_ability_rules())
	return rules


func _long_tail_ability_rules() -> Array[Dictionary]:
	## One synthetic ability per long-tail family (batch 3). Scale-free
	## magnitudes (experience, curse percentage, shot damage/count, durations)
	## are the measured retail values from the corpus INIs; ranges/radii are
	## synthetic sim-space values because these rules bind at scale 1.0.
	return [
		{
			# LevelGrantSpecialPower (theoden.ini King's Favor): Experience=50,
			# filter ANY +CAVALRY +INFANTRY ... -HERO ALLIES.
			"ability_id": "Command_TestKingsFavor",
			"slot": 7,
			"special_power_id": "SpecialAbilityKingsFavor",
			"targeting": "point",
			"cooldown_ticks": 30,
			"required_level": 1,
			"level_gate_resolved": true,
			"castable": true,
			"availability_reason": "",
			"effect": {
				"kind": "experience-grant",
				"experience": 50,
				"radiusEffect": 8.0,
				"startAbilityRange": 20.0,
				"affects": "ANY -HERO",
			},
		},
		{
			# ArrowStormUpdate (legolas.ini): damage 200 per shot (measured
			# LEGOLAS_ARROWSTORM_DAMAGE), ShotsPerBurst 3, PersistentPrepTime
			# 600 ms; shot count shortened for the check horizon.
			"ability_id": "Command_TestArrowStorm",
			"slot": 8,
			"special_power_id": "SpecialAbilityArrowStorm",
			"targeting": "point",
			"cooldown_ticks": 40,
			"required_level": 1,
			"level_gate_resolved": true,
			"castable": true,
			"availability_reason": "",
			"effect": {
				"kind": "arrow-storm",
				"weaponDamage": 200,
				"targetRadius": 6.0,
				"maxShots": 6,
				"shotsPerBurst": 3,
				"persistentPrepMs": 600.0,
				"startAbilityRange": 16.0,
				"canShootEmptyGround": true,
			},
		},
		{
			# ToggleHiddenSpecialAbilityUpdate (thranduil.ini Wild Walk):
			# forbidden TAKING_DAMAGE USING_ABILITY; duration shortened.
			"ability_id": "Command_TestWildWalk",
			"slot": 9,
			"special_power_id": "SpecialAbilityWildWalk",
			"targeting": "self",
			"cooldown_ticks": 10,
			"required_level": 1,
			"level_gate_resolved": true,
			"castable": true,
			"availability_reason": "",
			"effect": {
				"kind": "stealth-toggle",
				"effectDurationMs": 8000.0,
				"forbiddenConditions": ["TAKING_DAMAGE", "USING_ABILITY"],
			},
		},
		{
			# InvisibilitySpecialPower (thranduil.ini Move Unseen): ally
			# broadcast cloak, forbidden FIRING_ANY.
			"ability_id": "Command_TestMoveUnseen",
			"slot": 10,
			"special_power_id": "SpecialAbilityMoveUnseen",
			"targeting": "self",
			"cooldown_ticks": 20,
			"required_level": 1,
			"level_gate_resolved": true,
			"castable": true,
			"availability_reason": "",
			"effect": {
				"kind": "stealth-toggle",
				"effectDurationMs": 3000.0,
				"broadcastRadius": 5.0,
				"affects": "",
				"forbiddenConditions": ["FIRING_ANY"],
			},
		},
		{
			# TeleportSpecialAbilityUpdate (shelob.ini Tunnel): BusyForDuration
			# 1800 ms measured; MaxDistance synthetic (retail authors 9999999).
			"ability_id": "Command_TestTunnel",
			"slot": 11,
			"special_power_id": "SpecialAbilityWildShelobTunnel",
			"targeting": "point",
			"cooldown_ticks": 50,
			"required_level": 1,
			"level_gate_resolved": true,
			"castable": true,
			"availability_reason": "",
			"effect": {
				"kind": "teleport",
				"maxDistance": 100.0,
				"busyForDurationMs": 1800.0,
			},
		},
		{
			# CurseSpecialPower (witchking.ini Hour of the Witch-King):
			# CursePercentage 100% measured.
			"ability_id": "Command_TestCurse",
			"slot": 12,
			"special_power_id": "SpecialAbilityCurseEnemy",
			"targeting": "point",
			"cooldown_ticks": 60,
			"required_level": 1,
			"level_gate_resolved": true,
			"castable": true,
			"availability_reason": "",
			"effect": {
				"kind": "curse",
				"cursePercentage": 100.0,
				"startAbilityRange": 20.0,
				"radiusCursorRadius": 5.0,
			},
		},
		{
			# SpecialPowerModule AntiCategory=LEADERSHIP (boromir.ini Horn of
			# Gondor): anti-category duration 5000 ms measured.
			"ability_id": "Command_TestHorn",
			"slot": 13,
			"special_power_id": "SpecialAbilityHornOfGondor",
			"targeting": "self",
			"cooldown_ticks": 40,
			"required_level": 1,
			"level_gate_resolved": true,
			"castable": true,
			"availability_reason": "",
			"effect": {
				"kind": "leadership-strip",
				"attributeModifierRange": 7.0,
				"antiCategoryDurationMs": 5000.0,
			},
		},
	]


class NavStub extends RefCounted:
	## Minimal route provider: only the walkability probe, so the teleport
	## destination check can fail closed without a full navigation mesh.
	func is_local_inside_navigation(position: Vector2) -> bool:
		return position.x < 90.0


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
	_run_mount_checks()
	_run_capture_checks()
	_run_terror_checks()
	_run_timed_buff_checks()
	_run_experience_grant_checks()
	_run_arrow_storm_checks()
	_run_stealth_checks()
	_run_teleport_checks()
	_run_curse_checks()
	_run_leadership_strip_checks()
	_run_determinism_checks()
	_run_fortress_command_set_checks()
	print("RETAIL_HERO_ABILITY_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _run_fortress_command_set_checks() -> void:
	## The Men fortress surface must match what retail authors, derived from
	## DOC EVIDENCE (each converted playableUnit's producer bindings), never a
	## hardcoded roster: every hero authored on the fortress hero roster gets a
	## fortress production rule, and the porter's authored citadel train route
	## resolves to a fortress rule for manifest-derived (cross-faction) teams.
	var ManifestScript = load("res://src/retail_slice/retail_faction_manifest.gd")
	var AdapterScript = load("res://src/retail_slice/playable_unit_runtime_adapter.gd")
	var content_db = root.get_node_or_null("ContentDB")
	_check("fortress_content_db_present", content_db != null)
	if content_db == null:
		return
	var unit_runtimes: Dictionary = content_db.get_playable_unit_runtimes()
	var structure_runtimes: Dictionary = content_db.get_playable_structure_runtimes()
	_check("fortress_men_registries_loaded", not unit_runtimes.is_empty() and not structure_runtimes.is_empty())
	if unit_runtimes.is_empty() or structure_runtimes.is_empty():
		return
	var manifest: Dictionary = ManifestScript.from_registries("men", unit_runtimes, structure_runtimes)
	_check("fortress_men_manifest_builds", not manifest.has("_error"), String(manifest.get("_error", "")))
	if manifest.has("_error"):
		return
	var producer_registry: Dictionary = manifest.get("producer_kind_registry", {}) as Dictionary
	var registry_folded: Dictionary = {}
	for producer_id_value in producer_registry.keys():
		registry_folded[String(producer_id_value).to_lower()] = String(producer_registry[producer_id_value])
	# Doc-driven expectation: every men/gondor/rohan hero whose bindings put it
	# on a fortress-resolving hero roster.
	var scoped_runtimes: Dictionary = ManifestScript.faction_scoped_unit_runtimes(
		["men", "gondor", "rohan"], unit_runtimes, structure_runtimes,
		content_db.get_playable_unit_runtime_pack_index()
	)
	var expected_fortress_heroes: Dictionary = {}
	for object_id_value in scoped_runtimes.keys():
		var object_id := String(object_id_value)
		var lowered := object_id.to_lower()
		if not (lowered.begins_with("men") or lowered.begins_with("gondor") or lowered.begins_with("rohan")):
			continue
		var document: Dictionary = scoped_runtimes[object_id] as Dictionary
		if String(document.get("category", "")) != "hero":
			continue
		if AdapterScript.is_ring_hero_summon(document):
			continue
		for binding_value in AdapterScript.producer_bindings(document):
			var binding := binding_value as Dictionary
			var kind := String(registry_folded.get(String(binding.get("producer_source_object_id", "")).to_lower(), ""))
			if kind == "fortress" and String(binding.get("surface", "")) == "hero-roster":
				expected_fortress_heroes[String(AdapterScript.simulation_rule(document).get("unit_type", ""))] = true
				break
	_check("fortress_docs_author_hero_roster", expected_fortress_heroes.size() >= 2, "found %d" % expected_fortress_heroes.size())
	var manifest_fortress_heroes: Dictionary = {}
	var rules: Dictionary = manifest.get("unit_production_rules", {}) as Dictionary
	for unit_type_value in rules.keys():
		var rule: Dictionary = rules[unit_type_value] as Dictionary
		if String(rule.get("category", "")) == "hero" and (rule.get("producer_kinds", []) as Array).has("fortress"):
			manifest_fortress_heroes[String(unit_type_value)] = true
	var hero_sets_equal := manifest_fortress_heroes.size() == expected_fortress_heroes.size()
	for unit_type_value in expected_fortress_heroes.keys():
		if not manifest_fortress_heroes.has(String(unit_type_value)):
			hero_sets_equal = false
	_check(
		"fortress_manifest_heroes_match_doc_evidence", hero_sets_equal,
		"docs=%s manifest=%s" % [str(expected_fortress_heroes.keys()), str(manifest_fortress_heroes.keys())]
	)
	# The porter: the manifest names it as the builder, and the derived-team
	# projection resolves its authored citadel train route to a fortress rule
	# (the rule a cross-faction team's fortress trains it from).
	var builder_ids: Array = manifest.get("builder_unit_ids", []) as Array
	_check("fortress_manifest_names_builder", builder_ids.size() == 1 and String(builder_ids[0]) != "", str(builder_ids))
	if builder_ids.is_empty():
		return
	var sim = SimScript.new()
	sim._rules = {"playable_unit_runtimes": scoped_runtimes}
	var porter_rule: Dictionary = sim._derived_team_builder_rule(manifest, String(builder_ids[0]))
	_check(
		"fortress_porter_train_rule_resolves_from_doc",
		not porter_rule.is_empty()
			and (porter_rule.get("producer_kinds", []) as Array).has("fortress")
			and int(porter_rule.get("default_cost", 0)) > 0
			and String(porter_rule.get("command_id", "")) != "",
		str(porter_rule)
	)
	# Derived-team tables (the cross-faction seat): the full fortress command
	# set survives — every doc-evidenced hero AND the porter enter the team's
	# production order, not just the AI plan's single sample.
	var derived = SimScript.new()
	derived._rules = {
		"playable_unit_runtimes": scoped_runtimes,
		"faction_manifest": {"faction": "other-faction-placeholder"},
		"team_faction_manifests": {1: manifest},
	}
	derived._team_roster = [0, 1]
	derived._seed_team_manifest_tables()
	var derived_rules: Dictionary = derived.unit_production_rules_for_team(1)
	var derived_order: Array = derived.production_unit_order_for_team(1)
	var derived_ok := derived_rules.has(String(porter_rule.get("unit_type", "")))
	for unit_type_value in expected_fortress_heroes.keys():
		if not derived_order.has(String(unit_type_value)) or not derived_rules.has(String(unit_type_value)):
			derived_ok = false
	_check(
		"fortress_derived_team_keeps_heroes_and_porter",
		derived_ok and derived_order.has(String(porter_rule.get("unit_type", ""))),
		"order=%s" % str(derived_order)
	)


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


func _add_neutral_capturable(sim, id: int, at: Vector2, capturable: bool = true, team: int = SimScript.NEUTRAL_TEAM) -> Dictionary:
	sim.structures[id] = {
		"id": id,
		"team": team,
		"kind": "structure",
		"structure_kind": "signal_fire",
		"name": "Signal Fire",
		"position": at,
		"rally": at,
		"health": 1000,
		"maximum_health": 1000,
		"construction_progress": 1.0,
		"level": 1,
		"completed_upgrades": [],
		"upgrade_queue": [],
		"production": [],
		"queue": [],
		"damage_remainders": {},
		"income_per_payout": 0,
		"capturable": capturable,
	}
	return sim.structures[id]


func _run_mount_checks() -> void:
	var sim = _make_sim()
	for entity_id in sim.entity_ids():
		sim.entities.erase(entity_id)
	var hero: Dictionary = _spawn(sim, 5, 0, Vector2.ZERO, HERO_OBJECT_ID, HERO_UNIT_TYPE)
	var foot_speed := float(hero.get("speed", 0.0))
	var foot_speed_source := float(hero.get("speed_source", 0.0))
	# Wound the hero first: the swap must never touch the live health fraction.
	hero["member_health"] = [400]
	hero["health"] = 400
	var mount: Dictionary = sim.cast_ability(5, "Command_TestMount", Vector2.ZERO)
	_check("mount_cast_swaps_to_mounted_state", bool(mount.get("ok", false)) and bool(mount.get("mounted", false)) and bool(hero.get("mounted", false)))
	_check("mount_swaps_locomotor_speed", is_equal_approx(float(hero.get("speed", 0.0)), 3.0) and is_equal_approx(float(hero.get("speed_source", 0.0)), 3.0))
	_check("mount_pins_mounted_weapon_profile", String(hero.get("weapon_toggle_mode", "")) == "mounted" and is_equal_approx(float(hero.get("attack_range", 0.0)), 2.5) and int(hero.get("member_damage", 0)) == 60)
	_check("mount_preserves_absolute_health_same_body", int(hero.get("health", 0)) == 400 and int(hero.get("member_maximum_health", 0)) == 800)
	_check("mount_honors_cooldown", String(sim.cast_ability(5, "Command_TestMount", Vector2.ZERO).get("reason", "")) == "cooldown-active")
	sim.advance(10)
	_check("mount_state_survives_ticks", bool(hero.get("mounted", false)) and sim._weapon_mode_for_distance(hero, 0.5) == "mounted")
	# Snapshot round-trip preserves the mounted state and swapped stats.
	var bytes: PackedByteArray = sim.snapshot()
	var restored = _make_sim()
	var restore_ok: bool = restored.restore(bytes)
	var restored_hero: Dictionary = restored.entities.get(5, {})
	_check("mount_round_trips_snapshot", restore_ok and bool(restored_hero.get("mounted", false)) and is_equal_approx(float(restored_hero.get("speed", 0.0)), 3.0) and restored.state_hash() == sim.state_hash())
	# Dismount restores the recorded foot profile exactly.
	var dismount: Dictionary = sim.cast_ability(5, "Command_TestMount", Vector2.ZERO)
	_check("dismount_restores_foot_profile", bool(dismount.get("ok", false)) and not bool(dismount.get("mounted", true)) and not bool(hero.get("mounted", false)) and is_equal_approx(float(hero.get("speed", 0.0)), foot_speed) and is_equal_approx(float(hero.get("speed_source", 0.0)), foot_speed_source))
	_check("dismount_releases_weapon_pin", String(hero.get("weapon_toggle_mode", "")) == "" and is_equal_approx(float(hero.get("attack_range", 0.0)), 1.15) and int(hero.get("member_damage", 0)) == 40)
	# ChildObject-style max-health swap preserves the live health FRACTION
	# (synthetic magnitudes: no Men mount authors a body swap, the mechanism
	# is exercised here so a future compiled rule rides it unchanged).
	var rules: Array[Dictionary] = _hero_ability_rules("TestLeadership")
	for rule in rules:
		if String(rule.get("ability_id", "")) == "Command_TestMount":
			(rule["effect"] as Dictionary)["mountedMemberHealth"] = 1600.0
			(rule["effect"] as Dictionary)["dismountedMemberHealth"] = 800.0
	sim._unit_ability_rules[HERO_UNIT_TYPE] = sim._scaled_ability_rules(rules, 0.0)
	sim.advance(10)
	hero["member_health"] = [400]
	hero["health"] = 400
	sim.cast_ability(5, "Command_TestMount", Vector2.ZERO)
	_check("mount_body_swap_preserves_health_fraction", int(hero.get("member_maximum_health", 0)) == 1600 and int(hero.get("health", 0)) == 800)
	sim.advance(10)
	# Out-of-combat hero regeneration keeps ticking while mounted; the swap
	# contract is about the FRACTION at swap time, so measure just before.
	var health_before_dismount := int(hero.get("health", 0))
	var body_dismount: Dictionary = sim.cast_ability(5, "Command_TestMount", Vector2.ZERO)
	_check(
		"dismount_body_swap_preserves_health_fraction",
		bool(body_dismount.get("ok", false))
			and int(hero.get("member_maximum_health", 0)) == 800
			and int(hero.get("health", 0)) == roundi(float(health_before_dismount) / 1600.0 * 800.0),
		"max=%d health=%d before=%d" % [int(hero.get("member_maximum_health", 0)), int(hero.get("health", 0)), health_before_dismount]
	)
	# A compiled mounted weapon mode the unit rule does not carry fails closed.
	var bad_rules: Array[Dictionary] = _hero_ability_rules("TestLeadership")
	for rule in bad_rules:
		if String(rule.get("ability_id", "")) == "Command_TestMount":
			(rule["effect"] as Dictionary)["mountedWeaponModeKey"] = "warg"
	sim._unit_ability_rules[HERO_UNIT_TYPE] = sim._scaled_ability_rules(bad_rules, 0.0)
	sim.advance(10)
	var bad: Dictionary = sim.cast_ability(5, "Command_TestMount", Vector2.ZERO)
	_check("mount_unknown_weapon_mode_fails_closed", not bool(bad.get("ok", false)) and String(bad.get("reason", "")).begins_with("mount-mode-unavailable") and not bool(hero.get("mounted", false)))
	# A mount whose compiled speed did not resolve fails closed.
	var no_speed_rules: Array[Dictionary] = _hero_ability_rules("TestLeadership")
	for rule in no_speed_rules:
		if String(rule.get("ability_id", "")) == "Command_TestMount":
			(rule["effect"] as Dictionary).erase("mountedSpeed")
	sim._unit_ability_rules[HERO_UNIT_TYPE] = sim._scaled_ability_rules(no_speed_rules, 0.0)
	var no_speed: Dictionary = sim.cast_ability(5, "Command_TestMount", Vector2.ZERO)
	_check("mount_without_speed_fails_closed", not bool(no_speed.get("ok", false)) and String(no_speed.get("reason", "")) == "mount-speed-unresolved")


func _run_capture_checks() -> void:
	var sim = _make_sim()
	for entity_id in sim.entity_ids():
		sim.entities.erase(entity_id)
	for structure_id in sim.structure_ids():
		sim.structures.erase(structure_id)
	var hero: Dictionary = _spawn(sim, 5, 0, Vector2.ZERO, HERO_OBJECT_ID, HERO_UNIT_TYPE)
	var neutral: Dictionary = _add_neutral_capturable(sim, 500, Vector2(3.0, 0.0))
	var owned: Dictionary = _add_neutral_capturable(sim, 501, Vector2(0.0, 3.0), true, 1)
	var plain: Dictionary = _add_neutral_capturable(sim, 502, Vector2(-3.0, 0.0), false)
	var far_structure: Dictionary = _add_neutral_capturable(sim, 503, Vector2(60.0, 0.0))
	# Out of the authored StartAbilityRange fails before any channel starts.
	var far: Dictionary = sim.cast_ability(5, "Command_TestCapture", Vector2(60.0, 0.0))
	_check("capture_out_of_range_fails_closed", not bool(far.get("ok", false)) and String(far.get("reason", "")) == "out-of-range" and int(far_structure.get("team", -1)) == SimScript.NEUTRAL_TEAM)
	# A structure not flagged capturable is never a target.
	var flagless: Dictionary = sim.cast_ability(5, "Command_TestCapture", Vector2(-3.0, 0.0))
	_check("uncapturable_structure_fails_closed", not bool(flagless.get("ok", false)) and String(flagless.get("reason", "")) == "no-capturable-structure" and not bool(plain.get("capturable", false)))
	# Tier-1 honest scope: owned structures are excluded with their own reason.
	var enemy_cast: Dictionary = sim.cast_ability(5, "Command_TestCapture", Vector2(0.0, 3.0))
	_check("owned_structure_is_tier2_scope", not bool(enemy_cast.get("ok", false)) and String(enemy_cast.get("reason", "")) == "capture-tier1-neutral-only" and int(owned.get("team", -1)) == 1)
	# The real channel: authored 5002 ms envelope = 50 ticks at 0.1 s.
	var cast: Dictionary = sim.cast_ability(5, "Command_TestCapture", Vector2(3.0, 0.0))
	_check("capture_cast_starts_the_channel", bool(cast.get("ok", false)) and int(cast.get("structure_id", 0)) == 500 and not (hero.get("capture_channel", {}) as Dictionary).is_empty())
	_check("capture_recast_fails_while_channeling", String(sim.cast_ability(5, "Command_TestCapture", Vector2(3.0, 0.0)).get("reason", "")) == "capture-in-progress")
	sim.advance(1)
	_check("capture_channel_holds_the_hero", String(hero.get("state", "")) == "capture" and is_zero_approx(float(hero.get("current_speed", 1.0))))
	sim.advance(48)
	_check("capture_holds_through_the_envelope", int(neutral.get("team", -1)) == SimScript.NEUTRAL_TEAM and not (hero.get("capture_channel", {}) as Dictionary).is_empty())
	# Snapshot round-trip mid-channel.
	var bytes: PackedByteArray = sim.snapshot()
	var restored = _make_sim()
	var restore_ok: bool = restored.restore(bytes)
	_check("capture_channel_round_trips_snapshot", restore_ok and restored.state_hash() == sim.state_hash())
	sim.advance(1)
	_check("capture_completion_flips_the_team", int(neutral.get("team", -1)) == 0 and (hero.get("capture_channel", {}) as Dictionary).is_empty())
	var captured_event := false
	for event_value in sim.events:
		var event := event_value as Dictionary
		if String(event.get("kind", "")) == "structure.captured" and int(event.get("structure_id", 0)) == 500:
			captured_event = true
	_check("capture_completion_emits_the_event", captured_event)
	# The restored twin completes on the same tick with the same hash.
	restored.advance(1)
	_check("restored_channel_completes_identically", restored.state_hash() == sim.state_hash())
	# A move order interrupts the channel; the structure stays neutral.
	var second: Dictionary = _add_neutral_capturable(sim, 504, Vector2(0.0, -3.0))
	var recast: Dictionary = sim.cast_ability(5, "Command_TestCapture", Vector2(0.0, -3.0))
	sim.advance(5)
	var move_ids: Array[int] = [5]
	sim.issue_move(move_ids, Vector2(6.0, 6.0))
	sim.advance(5)
	_check(
		"move_order_cancels_the_channel",
		bool(recast.get("ok", false))
			and (hero.get("capture_channel", {}) as Dictionary).is_empty()
			and int(second.get("team", -1)) == SimScript.NEUTRAL_TEAM
	)


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


func _fresh_sim():
	var sim = _make_sim()
	for entity_id in sim.entity_ids():
		sim.entities.erase(entity_id)
	return sim


func _long_tail_variant(ability_id: String, mutator: Callable) -> Array[Dictionary]:
	var rules: Array[Dictionary] = _combined_ability_rules("TestLeadership")
	for rule in rules:
		if String(rule.get("ability_id", "")) == ability_id:
			mutator.call(rule["effect"] as Dictionary)
	return rules


func _run_experience_grant_checks() -> void:
	var sim = _fresh_sim()
	var hero: Dictionary = _spawn(sim, 5, 0, Vector2.ZERO, HERO_OBJECT_ID, HERO_UNIT_TYPE)
	var near_ally: Dictionary = _spawn(sim, 6, 0, Vector2(2.0, 0.0), SimScript.SOLDIER_OBJECT_ID, SimScript.SOLDIER_HORDE_ID)
	var far_ally: Dictionary = _spawn(sim, 7, 0, Vector2(50.0, 0.0), SimScript.SOLDIER_OBJECT_ID, SimScript.SOLDIER_HORDE_ID)
	var ally_hero: Dictionary = _spawn(sim, 8, 0, Vector2(1.0, 1.0), HERO2_OBJECT_ID, HERO2_UNIT_TYPE)
	var enemy: Dictionary = _spawn(sim, 105, 1, Vector2(3.0, 0.0), SimScript.SOLDIER_OBJECT_ID, SimScript.SOLDIER_HORDE_ID)
	var cast: Dictionary = sim.cast_ability(5, "Command_TestKingsFavor", Vector2.ZERO)
	_check("experience_grant_awards_measured_amount", bool(cast.get("ok", false)) and int(cast.get("affected", 0)) == 1 and int(near_ally.get("experience_xp", 0)) == 50)
	_check("experience_grant_filter_excludes_heroes", not ally_hero.has("experience_xp") or int(ally_hero.get("experience_xp", -1)) == 0)
	_check("experience_grant_skips_out_of_radius_ally", int(far_ally.get("experience_xp", 0)) == 0)
	_check("experience_grant_never_pays_enemies", int(enemy.get("experience_xp", 0)) == 0)
	_check("experience_grant_honors_cooldown", String(sim.cast_ability(5, "Command_TestKingsFavor", Vector2.ZERO).get("reason", "")) == "cooldown-active")
	sim.advance(30)
	# After the advance the hero's leadership aura (EXPERIENCE 2.0) rides the
	# ally: the second authored 50 doubles to 100 → 150 total, level 2 at the
	# authored 100 threshold.
	var second: Dictionary = sim.cast_ability(5, "Command_TestKingsFavor", Vector2.ZERO)
	_check("experience_grant_levels_through_authored_chain", bool(second.get("ok", false)) and int(near_ally.get("experience_xp", 0)) == 150 and int(near_ally.get("level", 0)) == 2)
	sim.advance(30)
	_check("experience_grant_out_of_range_fails_closed", String(sim.cast_ability(5, "Command_TestKingsFavor", Vector2(30.0, 0.0)).get("reason", "")) == "out-of-range")
	# No eligible recipient: the cast fails closed and consumes no cooldown.
	var lone = _fresh_sim()
	_spawn(lone, 5, 0, Vector2.ZERO, HERO_OBJECT_ID, HERO_UNIT_TYPE)
	var empty: Dictionary = lone.cast_ability(5, "Command_TestKingsFavor", Vector2.ZERO)
	_spawn(lone, 6, 0, Vector2(1.0, 0.0), SimScript.SOLDIER_OBJECT_ID, SimScript.SOLDIER_HORDE_ID)
	var retry: Dictionary = lone.cast_ability(5, "Command_TestKingsFavor", Vector2.ZERO)
	_check("experience_grant_no_recipients_fails_without_cooldown", String(empty.get("reason", "")) == "no-eligible-allies-in-radius" and bool(retry.get("ok", false)))
	# An unauthored Experience magnitude stays uncast-able.
	lone._unit_ability_rules[HERO_UNIT_TYPE] = lone._scaled_ability_rules(_long_tail_variant("Command_TestKingsFavor", func(effect: Dictionary) -> void: effect.erase("experience")), 0.0)
	lone.advance(30)
	_check("experience_grant_unmeasured_amount_fails_closed", String(lone.cast_ability(5, "Command_TestKingsFavor", Vector2.ZERO).get("reason", "")) == "experience-grant-fields-missing")
	_check("experience_grant_hero_row_untouched", not hero.has("experience_xp") or int(hero.get("experience_xp", 0)) == 0)


func _run_arrow_storm_checks() -> void:
	var sim = _fresh_sim()
	var hero: Dictionary = _spawn(sim, 5, 0, Vector2.ZERO, HERO_OBJECT_ID, HERO_UNIT_TYPE)
	var enemy_a: Dictionary = _spawn(sim, 105, 1, Vector2(2.0, 0.0), SimScript.SOLDIER_OBJECT_ID, SimScript.SOLDIER_HORDE_ID)
	var enemy_b: Dictionary = _spawn(sim, 106, 1, Vector2(0.0, 2.0), SimScript.SOLDIER_OBJECT_ID, SimScript.SOLDIER_HORDE_ID)
	var far_enemy: Dictionary = _spawn(sim, 107, 1, Vector2(50.0, 0.0), SimScript.SOLDIER_OBJECT_ID, SimScript.SOLDIER_HORDE_ID)
	var cast: Dictionary = sim.cast_ability(5, "Command_TestArrowStorm", Vector2(1.0, 1.0))
	_check("arrow_storm_cast_starts_the_channel", bool(cast.get("ok", false)) and not (hero.get("volley_channel", {}) as Dictionary).is_empty() and String(hero.get("state", "")) == "volley")
	# Zero the fresh cooldown so the recast reaches the channel guard itself,
	# then restore it for the cooldown check below.
	var storm_state: Dictionary = (hero.get("ability_states", {}) as Dictionary).get("Command_TestArrowStorm", {}) as Dictionary
	var storm_ready_tick := int(storm_state.get("cooldown_ready_tick", 0))
	storm_state["cooldown_ready_tick"] = 0
	_check("arrow_storm_recast_fails_while_channeling", String(sim.cast_ability(5, "Command_TestArrowStorm", Vector2(1.0, 1.0)).get("reason", "")) == "volley-in-progress")
	storm_state["cooldown_ready_tick"] = storm_ready_tick
	sim.advance(1)
	# First burst: 3 shots round-robin over [105, 106] at the measured 200
	# damage per shot — both single-member battalions die on their first hit.
	_check("arrow_storm_burst_kills_with_measured_damage", int(enemy_a.get("health", 0)) == 0 and int(enemy_b.get("health", 0)) == 0)
	_check("arrow_storm_skips_enemy_out_of_radius", int(far_enemy.get("health", 0)) == 200)
	sim.advance(7)
	_check("arrow_storm_channel_completes_after_max_shots", (hero.get("volley_channel", {}) as Dictionary).is_empty() and String(hero.get("state", "")) == "idle")
	_check("arrow_storm_honors_cooldown", String(sim.cast_ability(5, "Command_TestArrowStorm", Vector2(1.0, 1.0)).get("reason", "")) == "cooldown-active")
	# A move order cancels the channel mid-volley.
	var cancel_sim = _fresh_sim()
	var cancel_hero: Dictionary = _spawn(cancel_sim, 5, 0, Vector2.ZERO, HERO_OBJECT_ID, HERO_UNIT_TYPE)
	cancel_sim.cast_ability(5, "Command_TestArrowStorm", Vector2(1.0, 1.0))
	var move_ids: Array[int] = [5]
	cancel_sim.issue_move(move_ids, Vector2(6.0, 6.0))
	cancel_sim.advance(1)
	_check("arrow_storm_move_order_cancels_channel", (cancel_hero.get("volley_channel", {}) as Dictionary).is_empty())
	# Empty ground without the authored CanShootEmptyGround fails closed.
	cancel_sim._unit_ability_rules[HERO_UNIT_TYPE] = cancel_sim._scaled_ability_rules(_long_tail_variant("Command_TestArrowStorm", func(effect: Dictionary) -> void: effect["canShootEmptyGround"] = false), 0.0)
	cancel_sim.advance(60)
	_check("arrow_storm_empty_ground_fails_closed", String(cancel_sim.cast_ability(5, "Command_TestArrowStorm", Vector2(1.0, 1.0)).get("reason", "")) == "no-target")
	# An unmeasured per-shot damage stays uncast-able.
	cancel_sim._unit_ability_rules[HERO_UNIT_TYPE] = cancel_sim._scaled_ability_rules(_long_tail_variant("Command_TestArrowStorm", func(effect: Dictionary) -> void: effect.erase("weaponDamage")), 0.0)
	_check("arrow_storm_unmeasured_damage_fails_closed", String(cancel_sim.cast_ability(5, "Command_TestArrowStorm", Vector2(1.0, 1.0)).get("reason", "")) == "arrow-storm-fields-missing")


func _run_stealth_checks() -> void:
	var sim = _fresh_sim()
	var hero: Dictionary = _spawn(sim, 5, 0, Vector2.ZERO, HERO_OBJECT_ID, HERO_UNIT_TYPE)
	var enemy: Dictionary = _spawn(sim, 105, 1, Vector2(2.0, 0.0), SimScript.SOLDIER_OBJECT_ID, SimScript.SOLDIER_HORDE_ID)
	_check("stealth_hero_visible_before_cast", int((sim._nearest_auto_target(enemy) as Dictionary).get("id", 0)) == 5)
	var cast: Dictionary = sim.cast_ability(5, "Command_TestWildWalk", Vector2.ZERO)
	_check("stealth_cast_engages_cloak", bool(cast.get("ok", false)) and bool(cast.get("engaged", false)) and sim._stealth_active(hero))
	_check("stealth_blocks_enemy_auto_acquisition", (sim._nearest_auto_target(enemy) as Dictionary).is_empty())
	# USING_ABILITY: casting another ability drops the cloak.
	sim.cast_ability(5, "Command_TestWarCry", Vector2.ZERO)
	_check("stealth_breaks_on_using_ability", not sim._stealth_active(hero))
	# The visibility phases are done: drop the enemy so ordinary combat cannot
	# break the cloaks the later phases assert on.
	sim.entities.erase(105)
	sim.advance(10)
	sim.cast_ability(5, "Command_TestWildWalk", Vector2.ZERO)
	sim._apply_damage(105, 5, 10, "battalion")
	_check("stealth_breaks_on_taking_damage", not sim._stealth_active(hero))
	sim.advance(10)
	var engage: Dictionary = sim.cast_ability(5, "Command_TestWildWalk", Vector2.ZERO)
	sim.advance(10)
	var toggle_off: Dictionary = sim.cast_ability(5, "Command_TestWildWalk", Vector2.ZERO)
	_check("stealth_recast_toggles_off", bool(engage.get("engaged", false)) and bool(toggle_off.get("ok", false)) and not bool(toggle_off.get("engaged", true)) and not sim._stealth_active(hero))
	# Exact-tick expiry: 8000 ms at 0.1 s ticks = 80 ticks.
	sim.advance(10)
	sim.cast_ability(5, "Command_TestWildWalk", Vector2.ZERO)
	sim.advance(79)
	var still_active: bool = sim._stealth_active(hero)
	sim.advance(1)
	_check("stealth_expires_on_exact_tick", still_active and not sim._stealth_active(hero) and not hero.has("stealth_until_tick"))
	# Broadcast cloak (Move Unseen): allies inside the authored radius cloak
	# too; the FIRING_ANY condition breaks only the one who fires.
	var ally: Dictionary = _spawn(sim, 6, 0, Vector2(1.0, 0.0), SimScript.SOLDIER_OBJECT_ID, SimScript.SOLDIER_HORDE_ID)
	var far_ally: Dictionary = _spawn(sim, 7, 0, Vector2(50.0, 0.0), SimScript.SOLDIER_OBJECT_ID, SimScript.SOLDIER_HORDE_ID)
	var broadcast: Dictionary = sim.cast_ability(5, "Command_TestMoveUnseen", Vector2.ZERO)
	_check("stealth_broadcast_cloaks_allies_in_radius", bool(broadcast.get("ok", false)) and int(broadcast.get("affected", 0)) == 2 and sim._stealth_active(ally) and not sim._stealth_active(far_ally))
	var victim: Dictionary = _spawn(sim, 109, 1, Vector2(2.0, 2.0), SimScript.SOLDIER_OBJECT_ID, SimScript.SOLDIER_HORDE_ID)
	sim._apply_damage(5, 109, 10, "battalion")
	_check("stealth_breaks_on_firing_only_for_the_shooter", int(victim.get("health", 0)) < 200 and not sim._stealth_active(hero) and sim._stealth_active(ally))
	# An unauthored duration stays uncast-able.
	sim._unit_ability_rules[HERO_UNIT_TYPE] = sim._scaled_ability_rules(_long_tail_variant("Command_TestWildWalk", func(effect: Dictionary) -> void: effect.erase("effectDurationMs")), 0.0)
	sim.advance(30)
	_check("stealth_unmeasured_duration_fails_closed", String(sim.cast_ability(5, "Command_TestWildWalk", Vector2.ZERO).get("reason", "")) == "stealth-fields-missing")


func _run_teleport_checks() -> void:
	var sim = _fresh_sim()
	var hero: Dictionary = _spawn(sim, 5, 0, Vector2.ZERO, HERO_OBJECT_ID, HERO_UNIT_TYPE)
	var cast: Dictionary = sim.cast_ability(5, "Command_TestTunnel", Vector2(30.0, 30.0))
	_check("teleport_relocates_to_the_target_point", bool(cast.get("ok", false)) and Vector2(hero.get("position", Vector2.ZERO)) == Vector2(30.0, 30.0))
	_check("teleport_honors_cooldown", String(sim.cast_ability(5, "Command_TestTunnel", Vector2(40.0, 40.0)).get("reason", "")) == "cooldown-active")
	# The authored BusyForDuration (1800 ms = 18 ticks) holds the hero: a move
	# order lands but the hero only starts once the hold expires.
	var move_ids: Array[int] = [5]
	sim.issue_move(move_ids, Vector2(40.0, 40.0))
	sim.advance(10)
	var held: bool = Vector2(hero.get("position", Vector2.ZERO)) == Vector2(30.0, 30.0)
	sim.advance(15)
	_check("teleport_busy_envelope_holds_then_releases", held and Vector2(hero.get("position", Vector2.ZERO)) != Vector2(30.0, 30.0))
	# Beyond the authored MaxDistance fails closed.
	var short = _fresh_sim()
	_spawn(short, 5, 0, Vector2.ZERO, HERO_OBJECT_ID, HERO_UNIT_TYPE)
	short._unit_ability_rules[HERO_UNIT_TYPE] = short._scaled_ability_rules(_long_tail_variant("Command_TestTunnel", func(effect: Dictionary) -> void: effect["maxDistance"] = 10.0), 0.0)
	_check("teleport_beyond_max_distance_fails_closed", String(short.cast_ability(5, "Command_TestTunnel", Vector2(30.0, 30.0)).get("reason", "")) == "out-of-range")
	# An unwalkable destination fails closed (walkability probe stubbed).
	short._unit_ability_rules[HERO_UNIT_TYPE] = short._scaled_ability_rules(_long_tail_ability_rules(), 0.0)
	short.route_provider = NavStub.new()
	var blocked: Dictionary = short.cast_ability(5, "Command_TestTunnel", Vector2(95.0, 0.0))
	short.route_provider = null
	_check(
		"teleport_unwalkable_destination_fails_closed",
		String(blocked.get("reason", "")) == "destination-unwalkable" and Vector2((short.entities[5] as Dictionary).get("position", Vector2.ZERO)) == Vector2.ZERO
	)
	# An unauthored MaxDistance stays uncast-able.
	short._unit_ability_rules[HERO_UNIT_TYPE] = short._scaled_ability_rules(_long_tail_variant("Command_TestTunnel", func(effect: Dictionary) -> void: effect.erase("maxDistance")), 0.0)
	_check("teleport_unmeasured_distance_fails_closed", String(short.cast_ability(5, "Command_TestTunnel", Vector2(1.0, 0.0)).get("reason", "")) == "teleport-fields-missing")


func _run_curse_checks() -> void:
	var sim = _fresh_sim()
	_spawn(sim, 5, 0, Vector2.ZERO, HERO_OBJECT_ID, HERO_UNIT_TYPE)
	var enemy_hero: Dictionary = _spawn(sim, 105, 1, Vector2(10.0, 0.0), HERO2_OBJECT_ID, HERO2_UNIT_TYPE)
	var enemy_soldier: Dictionary = _spawn(sim, 106, 1, Vector2(10.0, 1.0), SimScript.SOLDIER_OBJECT_ID, SimScript.SOLDIER_HORDE_ID)
	var cast: Dictionary = sim.cast_ability(5, "Command_TestCurse", Vector2(10.0, 0.0))
	var screech_state: Dictionary = (enemy_hero.get("ability_states", {}) as Dictionary).get("Command_TestScreech", {}) as Dictionary
	_check("curse_targets_the_enemy_hero", bool(cast.get("ok", false)) and int(cast.get("target_id", 0)) == 105 and int(cast.get("affected", 0)) == 1)
	_check(
		"curse_restarts_recharges_at_measured_percentage",
		int(screech_state.get("cooldown_ready_tick", 0)) == sim.tick_index + 20 and int(cast.get("cursed_abilities", 0)) == 9
	)
	_check("curse_never_exceeds_one_full_recharge", int(((enemy_hero.get("ability_states", {}) as Dictionary).get("Command_TestCurse", {}) as Dictionary).get("cooldown_ready_tick", 0)) == sim.tick_index + 60)
	_check("curse_honors_cooldown", String(sim.cast_ability(5, "Command_TestCurse", Vector2(10.0, 0.0)).get("reason", "")) == "cooldown-active")
	_check("curse_leaves_non_hero_enemies_untouched", not enemy_soldier.has("ability_states") or (enemy_soldier.get("ability_states", {}) as Dictionary).is_empty())
	# Only enemy heroes are valid targets: a soldier-only point fails closed.
	var lone = _fresh_sim()
	_spawn(lone, 5, 0, Vector2.ZERO, HERO_OBJECT_ID, HERO_UNIT_TYPE)
	_spawn(lone, 106, 1, Vector2(0.0, -10.0), SimScript.SOLDIER_OBJECT_ID, SimScript.SOLDIER_HORDE_ID)
	_check("curse_without_enemy_hero_fails_closed", String(lone.cast_ability(5, "Command_TestCurse", Vector2(0.0, -10.0)).get("reason", "")) == "no-enemy-hero-in-radius")
	_check("curse_out_of_range_fails_closed", String(lone.cast_ability(5, "Command_TestCurse", Vector2(30.0, 0.0)).get("reason", "")) == "out-of-range")
	# An unmeasured percentage stays uncast-able.
	lone._unit_ability_rules[HERO_UNIT_TYPE] = lone._scaled_ability_rules(_long_tail_variant("Command_TestCurse", func(effect: Dictionary) -> void: effect.erase("cursePercentage")), 0.0)
	_check("curse_unmeasured_percentage_fails_closed", String(lone.cast_ability(5, "Command_TestCurse", Vector2(0.0, -10.0)).get("reason", "")) == "curse-fields-missing")


func _run_leadership_strip_checks() -> void:
	var sim = _fresh_sim()
	_spawn(sim, 5, 0, Vector2.ZERO, HERO_OBJECT_ID, HERO_UNIT_TYPE)
	# Everyone sits outside the 4.0 vision range so no auto-acquired combat
	# perturbs the aura bookkeeping the checks assert on.
	_spawn(sim, 105, 1, Vector2(5.0, 0.0), HERO2_OBJECT_ID, HERO2_UNIT_TYPE)
	var near_enemy: Dictionary = _spawn(sim, 106, 1, Vector2(4.5, 0.0), SimScript.SOLDIER_OBJECT_ID, SimScript.SOLDIER_HORDE_ID)
	var far_enemy: Dictionary = _spawn(sim, 107, 1, Vector2(50.0, 0.0), SimScript.SOLDIER_OBJECT_ID, SimScript.SOLDIER_HORDE_ID)
	_spawn(sim, 108, 1, Vector2(51.0, 0.0), HERO2_OBJECT_ID, HERO2_UNIT_TYPE)
	sim.advance(5)
	_check("strip_setup_auras_radiating", _table(near_enemy).has("aura:TestLeadership") and _table(far_enemy).has("aura:TestLeadership"))
	var cast: Dictionary = sim.cast_ability(5, "Command_TestHorn", Vector2.ZERO)
	_check("strip_cast_removes_leadership_grants", bool(cast.get("ok", false)) and int(cast.get("affected", 0)) == 2 and not _table(near_enemy).has("aura:TestLeadership"))
	_check("strip_leaves_enemies_out_of_radius", _table(far_enemy).has("aura:TestLeadership"))
	sim.advance(10)
	_check("strip_suppresses_regrant_through_duration", not _table(near_enemy).has("aura:TestLeadership") and _table(far_enemy).has("aura:TestLeadership"))
	_check("strip_honors_cooldown", String(sim.cast_ability(5, "Command_TestHorn", Vector2.ZERO).get("reason", "")) == "cooldown-active")
	sim.advance(45)
	_check("strip_suppression_expires_and_aura_returns", _table(near_enemy).has("aura:TestLeadership") and not near_enemy.has("leadership_suppressed_until_tick"))
	# No enemies inside the authored range: fail closed, no cooldown burned.
	var lone = _fresh_sim()
	_spawn(lone, 5, 0, Vector2.ZERO, HERO_OBJECT_ID, HERO_UNIT_TYPE)
	_check("strip_without_enemies_fails_closed", String(lone.cast_ability(5, "Command_TestHorn", Vector2.ZERO).get("reason", "")) == "no-enemies-in-radius")
	# An unauthored anti-category duration stays uncast-able.
	_spawn(lone, 106, 1, Vector2(2.0, 0.0), SimScript.SOLDIER_OBJECT_ID, SimScript.SOLDIER_HORDE_ID)
	lone._unit_ability_rules[HERO_UNIT_TYPE] = lone._scaled_ability_rules(_long_tail_variant("Command_TestHorn", func(effect: Dictionary) -> void: effect.erase("antiCategoryDurationMs")), 0.0)
	_check("strip_unmeasured_duration_fails_closed", String(lone.cast_ability(5, "Command_TestHorn", Vector2.ZERO).get("reason", "")) == "leadership-strip-fields-missing")


func _command(tick: int, seq: int, type: String, args: Dictionary, team: int = 0) -> Dictionary:
	return {"tick": tick, "team": team, "seq": seq, "type": type, "args": args}


func _determinism_sim():
	var sim = _make_sim()
	_spawn(sim, 5, 0, Vector2(-6.0, 0.0), HERO_OBJECT_ID, HERO_UNIT_TYPE)
	_spawn(sim, 6, 0, Vector2(-4.0, 0.0), SimScript.SOLDIER_OBJECT_ID, SimScript.SOLDIER_HORDE_ID)
	_spawn(sim, 105, 1, Vector2(-2.0, 1.0), SimScript.SOLDIER_OBJECT_ID, SimScript.SOLDIER_HORDE_ID)
	_spawn(sim, 106, 1, Vector2(-2.0, -1.0), FLYER_OBJECT_ID, FLYER_UNIT_TYPE)
	_spawn(sim, 108, 1, Vector2(2.0, 2.0), HERO2_OBJECT_ID, HERO2_UNIT_TYPE)
	_add_neutral_capturable(sim, 500, Vector2(-5.0, 1.0))
	return sim


func _scripted_log() -> Array[Dictionary]:
	## All six families fire inside the twin run: passive auras radiate from
	## tick 5, plus scripted toggle/mount/capture/terror/buff casts and
	## ordinary combat. The tick-40 attack order interrupts the capture
	## channel mid-envelope, so the cancel path is inside the hash too.
	return [
		_command(2, 1, "cast_ability", {"hero_id": 5, "ability_id": "Command_TestToggleWeapon", "target_point": Vector2.ZERO}),
		_command(4, 2, "cast_ability", {"hero_id": 5, "ability_id": "Command_TestCapture", "target_point": Vector2(-5.0, 1.0)}),
		_command(10, 3, "cast_ability", {"hero_id": 5, "ability_id": "Command_TestWarCry", "target_point": Vector2.ZERO}),
		_command(20, 4, "cast_ability", {"hero_id": 5, "ability_id": "Command_TestScreech", "target_point": Vector2.ZERO}),
		_command(40, 5, "issue_attack", {"ids": [5], "target_id": 105}),
		_command(60, 6, "cast_ability", {"hero_id": 5, "ability_id": "Command_TestScreech", "target_point": Vector2.ZERO}),
		_command(80, 7, "cast_ability", {"hero_id": 5, "ability_id": "Command_TestMount", "target_point": Vector2.ZERO}),
		_command(120, 8, "cast_ability", {"hero_id": 5, "ability_id": "Command_TestToggleWeapon", "target_point": Vector2.ZERO}),
		_command(150, 9, "cast_ability", {"hero_id": 5, "ability_id": "Command_TestWarCry", "target_point": Vector2.ZERO}),
		_command(180, 10, "cast_ability", {"hero_id": 5, "ability_id": "Command_TestMount", "target_point": Vector2.ZERO}),
		_command(200, 11, "issue_move", {"ids": [5, 6], "destination": Vector2(-10.0, 2.0)}),
		# Batch-3 long-tail families inside the twin-run hash: stealth engaged
		# then broken by the King's Favor cast (USING_ABILITY), the teleport
		# hold, the volley channel, the curse on the enemy hero, and the
		# leadership strip attempt.
		_command(210, 12, "cast_ability", {"hero_id": 5, "ability_id": "Command_TestWildWalk", "target_point": Vector2.ZERO}),
		_command(230, 13, "cast_ability", {"hero_id": 5, "ability_id": "Command_TestKingsFavor", "target_point": Vector2(-10.0, 2.0)}),
		_command(250, 14, "cast_ability", {"hero_id": 5, "ability_id": "Command_TestTunnel", "target_point": Vector2(-8.0, 0.0)}),
		_command(270, 15, "cast_ability", {"hero_id": 5, "ability_id": "Command_TestArrowStorm", "target_point": Vector2(-6.0, 0.0)}),
		_command(300, 16, "cast_ability", {"hero_id": 5, "ability_id": "Command_TestCurse", "target_point": Vector2(2.0, 2.0)}),
		_command(330, 17, "cast_ability", {"hero_id": 5, "ability_id": "Command_TestHorn", "target_point": Vector2.ZERO}),
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
	# Long-tail inertness: a battle with no casts never grows the new per-row
	# fields, so the pinned default-battle signature cannot move.
	var idle_a = _determinism_sim()
	var idle_b = _determinism_sim()
	idle_a.advance(120)
	idle_b.advance(120)
	var fields_clean := true
	for id in idle_a.entity_ids():
		var row: Dictionary = idle_a.entities[id]
		for field in ["stealth_until_tick", "stealth_forbidden", "volley_channel", "ability_hold_until_tick", "leadership_suppressed_until_tick"]:
			if row.has(field):
				fields_clean = false
	_check("uncast_families_stay_inert", fields_clean and idle_a.state_hash() == idle_b.state_hash())


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_HERO_ABILITY PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_HERO_ABILITY FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])

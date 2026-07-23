class_name RetailSliceSim
extends RefCounted

const PlayableUnitAdapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const CommandScript = preload("res://src/retail_slice/retail_command.gd")

const MAX_RETAINED_EVENT_HISTORY := 2048
const MAX_RETAINED_EVENTS_PER_KIND := 32
const MAX_RETAINED_STRUCTURE_TARGETS_PER_KIND := 256
## Deterministic battalion-level gameplay used by the private retail slice.
## Positions are X/Z world coordinates stored as Vector2 values.

const TICK_SECONDS := 0.1
const PLAYER_TEAM := 0
const ENEMY_TEAM := 1
# Unowned map structures (capture-building tier-1 targets). No player or AI
# ever acts as this team; it only owns capturable structures until captured.
const NEUTRAL_TEAM := 2
const MEMBER_ATTACK_STAGGER_WINDOW_TICKS := 4
const CORPSE_LIFETIME_TICKS := 600
const STANCE_ORDER: Array[String] = ["HoldGround", "Battle", "Aggressive"]
const FORMATION_ORDER: Array[String] = ["Line", "Block"]
## Line keeps authored formation slots. Block pulls members tighter (retail
## shield-wall / block feel) without inventing new combat numbers.
const FORMATION_SPACING := {"Line": 1.0, "Block": 0.55}
const STRUCTURE_KINDS: Array[String] = ["fortress", "farm", "barracks", "archery_range", "stable"]
const STRUCTURE_MAX_HEALTH: Dictionary = {
	"fortress": 7500,
	"farm": 2000,
	"barracks": 3000,
	"archery_range": 3000,
	"stable": 3000,
}
const STRUCTURE_BUILD_RULES: Dictionary = {
	"farm": {"cost": 300, "seconds": 22.0},
	"barracks": {"cost": 300, "seconds": 30.0},
	"archery_range": {"cost": 300, "seconds": 30.0},
	"stable": {"cost": 600, "seconds": 45.0},
	"fortress": {"cost": 5000, "seconds": 120.0},
}
const PLAYER_STRUCTURE_BASE := 1000
const ENEMY_STRUCTURE_BASE := 2000
const SOLDIER_OBJECT_ID := "bfme2.object.gondor-fighter"
const ARCHER_OBJECT_ID := "bfme2.object.gondor-archer"
const TOWER_GUARD_OBJECT_ID := "bfme2.object.gondor-tower-guard"
const KNIGHT_OBJECT_ID := "bfme2.object.gondor-knight"
const RANGER_OBJECT_ID := "bfme2.object.gondor-ranger"
const RANGER_HORDE_ID := "bfme2.object.gondor-ranger-horde"
const TREBUCHET_OBJECT_ID := "bfme2.object.gondor-trebuchet"
const BUILDER_OBJECT_ID := "bfme2.object.men-porter"
const SOLDIER_HORDE_ID := "bfme2.object.gondor-fighter-horde"
const PRODUCTION_DOOR_INSET_RADIUS := 0.9
const PRODUCTION_EXIT_RADIUS := 4.25
const PRODUCTION_EXIT_DURATION_TICKS := 18
## Minimal cavalry trample: one bonus hit while charging into an enemy within
## collision radius, at most once per TRAMPLE_COOLDOWN_TICKS. Fail-closed when
## the unit is not category "cavalry" (no invented trample for infantry).
const TRAMPLE_COLLISION_RADIUS := 2.5
const TRAMPLE_COOLDOWN_TICKS := 10
const TRAMPLE_DAMAGE_FACTOR := 0.5
## Knockback/knockdown lane. Trampled or blast-struck battalions are thrown
## away from the impact and stay incapacitated (no move/attack/orders) until
## the counter drains — retail infantry sprawl-then-stand behavior.
## 2.5 seconds at TICK_SECONDS = 0.1.
const KNOCKDOWN_DURATION_TICKS := 25
const TRAMPLE_KNOCKBACK_STRENGTH := 2.0
## Honest melee-vs-ranged discriminator for flyer targeting: converted melee
## weapons author source-unit ranges <= ~40 (MeleeWeapon family; harness
## fixtures use 11.5), while bows/crossbows/siege author 250+. Source-unit
## ranges are map-scale invariant, unlike the scaled attack_range.
const MELEE_ATTACK_RANGE_SOURCE_THRESHOLD := 50.0
## Retail eva.ini UnderAttack* blocks debounce global under-attack announces
## with TimeBetweenEventsMS = 30000 (30s → 300 sim ticks at 0.1s/tick).
const EVA_BASE_UNDER_ATTACK_DEBOUNCE_TICKS := 300
const UNIT_DAMAGE_TYPES: Dictionary = {
	SOLDIER_OBJECT_ID: "slash",
	TOWER_GUARD_OBJECT_ID: "specialist",
	ARCHER_OBJECT_ID: "pierce",
	RANGER_OBJECT_ID: "pierce",
	KNIGHT_OBJECT_ID: "cavalry",
	TREBUCHET_OBJECT_ID: "siege",
}
# armor.ini is now compiled end-to-end: unit ArmorSet tables ride each
# playableUnit document and structure ArmorSet tables each playableStructure
# document (see importer/openbfme_importer/armor_compiler.py). The legacy
# default rules have no documents, so the default fortress keeps a mirror of
# the retail FortressArmor table with per-line armor.ini provenance
# (armor.ini:1847-1863); pack-driven manifests replace it with the compiled
# table of every structure kind. This is the same legacy-default pattern as
# UNIT_DAMAGE_TYPES above, not a second source of truth.
const DEFAULT_STRUCTURE_ARMOR: Dictionary = {
	"fortress": {
		"set_id": "FortressArmor",
		"damage_scalar": 1.0,
		"scalars": {
			"default": 0.25,  # armor.ini:1848 DEFAULT 25%
			"crush": 0.01,  # armor.ini:1849 CRUSH 1%
			"slash": 0.20,  # armor.ini:1850 SLASH 20%
			"uruk": 0.15,  # armor.ini:1851 URUK 15%
			"specialist": 0.12,  # armor.ini:1852 SPECIALIST 12%
			"pierce": 0.01,  # armor.ini:1853 PIERCE 1%
			"flame": 0.05,  # armor.ini:1854 FLAME 5%
			"logical_fire": 0.0,  # armor.ini:1855,1861 LOGICAL_FIRE 0%
			"siege": 2.00,  # armor.ini:1856 SIEGE 200%
			"structural": 0.25,  # armor.ini:1857 STRUCTURAL 25%
			"hero": 0.50,  # armor.ini:1858 HERO 50%
			"hero_ranged": 0.10,  # armor.ini:1859 HERO_RANGED 10%
			"cavalry": 0.20,  # armor.ini:1860 CAVALRY 20%
			"cavalry_ranged": 0.01,  # armor.ini:1862 CAVALRY_RANGED 1%
		},
	},
}
# A structure kind with no compiled armor table (legacy defaults other than
# the fortress, or a stale pack document) uses this provisional scalar. It is
# recorded once per kind at configure — an explicit interim value, never a
# silent default.
const STRUCTURE_ARMOR_PROVISIONAL_SCALAR := 0.25
## Structure kinds with no compiled armor table (recorded provisionals).
var structure_armor_provisional_kinds: Array[String] = []
## Unit object ids whose document carries no armor block at all (stale pack):
## their incoming damage uses the recorded SAGE passthrough of 1.0.
var missing_armor_units: Array[String] = []
## Unit object ids with no authored damageType at all (their structure damage
## falls to the structure kind's DEFAULT scalar). Builders without weapons are
## excluded.
var missing_damage_type_units: Array[String] = []
const UNIT_PRODUCTION_RULES: Dictionary = {
	SOLDIER_HORDE_ID: {
		"producer_kind": "barracks",
		"object_id": SOLDIER_OBJECT_ID,
		"display_name": "Gondor Soldiers",
		"cost_rule": "soldier_cost",
		"build_ticks_rule": "soldier_build_ticks",
		"command_points_rule": "soldier_command_points",
		"default_cost": 200,
		"default_build_ticks": 200,
		"default_command_points": 60,
	},
	TOWER_GUARD_OBJECT_ID: {
		"producer_kind": "barracks",
		"object_id": TOWER_GUARD_OBJECT_ID,
		"display_name": "Tower Guard",
		"cost_rule": "tower_guard_cost",
		"build_ticks_rule": "tower_guard_build_ticks",
		"command_points_rule": "tower_guard_command_points",
		"default_cost": 400,
		"default_build_ticks": 200,
		"default_command_points": 60,
	},
	ARCHER_OBJECT_ID: {
		"producer_kind": "archery_range",
		"object_id": ARCHER_OBJECT_ID,
		"display_name": "Gondor Archers",
		"cost_rule": "archer_cost",
		"build_ticks_rule": "archer_build_ticks",
		"command_points_rule": "archer_command_points",
		"default_cost": 200,
		"default_build_ticks": 200,
		"default_command_points": 60,
	},
	KNIGHT_OBJECT_ID: {
		"producer_kind": "stable",
		"object_id": KNIGHT_OBJECT_ID,
		"display_name": "Gondor Knights",
		"cost_rule": "knight_cost",
		"build_ticks_rule": "knight_build_ticks",
		"command_points_rule": "knight_command_points",
		"default_cost": 550,
		"default_build_ticks": 250,
		"default_command_points": 80,
	},
}
const AI_PRODUCTION_PLAN: Array[String] = [
	SOLDIER_HORDE_ID,
	TOWER_GUARD_OBJECT_ID,
	ARCHER_OBJECT_ID,
	KNIGHT_OBJECT_ID,
]
## Deterministic per-difficulty AI profiles (no RNG anywhere). Every field is a
## deterministic scalar consumed by the SINGLE per-team controller; the five
## tiers are data, not five code paths. Permille fields (1000 == neutral) scale
## the base `_rules`-derived constant with integer math so the resource/economy
## handicap and cadences stay hash-stable across platforms.
##
## LEGACY / DEFAULT TIER == "medium": all-neutral (permilles 1000, deltas 0,
## scan_interval 15 == the historical `% 15` gate, no retreat, closest-target
## priority). A default 2-team roster {0:player, 1:ai} with no "difficulty" key
## resolves team 1 to "medium", so the controller issues the byte-identical
## command sequence the old ENEMY_TEAM AI did and the pinned battle signature
## (3CB9CA98) does not move. Retreat/regroup and weakest-fortress targeting are
## strictly "hard and up", so they can never touch the default match.
const AI_DIFFICULTY_PROFILES: Dictionary = {
	"easy": {
		"scan_interval": 45,
		"queue_interval_permille": 2000,
		"wave_size_delta": -2,
		"wave_patience_permille": 1600,
		"attack_delay_permille": 1800,
		"resource_permille": 550,
		"retreat_member_permille": 0,
		"weakest_fortress_priority": false,
		"extra_producer_cycles": 0,
	},
	"medium": {
		"scan_interval": 15,
		"queue_interval_permille": 1000,
		"wave_size_delta": 0,
		"wave_patience_permille": 1000,
		"attack_delay_permille": 1000,
		"resource_permille": 1000,
		"retreat_member_permille": 0,
		"weakest_fortress_priority": false,
		"extra_producer_cycles": 0,
	},
	"hard": {
		"scan_interval": 15,
		"queue_interval_permille": 700,
		"wave_size_delta": 2,
		"wave_patience_permille": 800,
		"attack_delay_permille": 700,
		"resource_permille": 1500,
		"retreat_member_permille": 200,
		"weakest_fortress_priority": false,
		"extra_producer_cycles": 1,
	},
	"brutal": {
		"scan_interval": 10,
		"queue_interval_permille": 550,
		"wave_size_delta": 4,
		"wave_patience_permille": 650,
		"attack_delay_permille": 550,
		"resource_permille": 2100,
		"retreat_member_permille": 250,
		"weakest_fortress_priority": true,
		"extra_producer_cycles": 2,
	},
	"morgoth": {
		"scan_interval": 5,
		"queue_interval_permille": 400,
		"wave_size_delta": 7,
		"wave_patience_permille": 500,
		"attack_delay_permille": 400,
		"resource_permille": 3000,
		"retreat_member_permille": 300,
		"weakest_fortress_priority": true,
		"extra_producer_cycles": 4,
	},
}
const AI_DEFAULT_DIFFICULTY: String = "medium"
## Outer controller cadence: the shared GCD of every tier's scan_interval, so a
## team executes on exactly the ticks its own scan_interval selects. medium's 15
## is a multiple of 5, so the default team still runs on 0,15,30,... — identical
## to the historical `tick_index % 15 == 0` gate.
const AI_CONTROLLER_BASE_INTERVAL: int = 5
# Default faction roster: entries flagged requires_unit_rule spawn only when the
# selected pack's unit rules define that object (the two Builder battalions).
const DEFAULT_SPAWN_ROSTER: Array = [
	{"id": 1, "team": PLAYER_TEAM, "anchor": "player_spawn_primary", "name": "Gondor Soldiers", "object_id": SOLDIER_OBJECT_ID, "unit_type": SOLDIER_HORDE_ID},
	{"id": 2, "team": PLAYER_TEAM, "anchor": "player_spawn_secondary", "name": "Gondor Archers", "object_id": ARCHER_OBJECT_ID, "unit_type": ARCHER_OBJECT_ID},
	{"id": 101, "team": ENEMY_TEAM, "anchor": "enemy_spawn_primary", "name": "Enemy Soldiers", "object_id": SOLDIER_OBJECT_ID, "unit_type": SOLDIER_HORDE_ID},
	{"id": 102, "team": ENEMY_TEAM, "anchor": "enemy_spawn_secondary", "name": "Enemy Tower Guard", "object_id": TOWER_GUARD_OBJECT_ID, "unit_type": TOWER_GUARD_OBJECT_ID},
	{"id": 103, "team": ENEMY_TEAM, "anchor": "enemy_reserve", "name": "Enemy Gondor Knights", "object_id": KNIGHT_OBJECT_ID, "unit_type": KNIGHT_OBJECT_ID},
	{"id": 3, "team": PLAYER_TEAM, "anchor": "player_builder", "name": "Builder", "object_id": BUILDER_OBJECT_ID, "unit_type": BUILDER_OBJECT_ID, "command_points": 0, "requires_unit_rule": true},
	{"id": 104, "team": ENEMY_TEAM, "anchor": "enemy_builder", "name": "Enemy Builder", "object_id": BUILDER_OBJECT_ID, "unit_type": BUILDER_OBJECT_ID, "command_points": 0, "requires_unit_rule": true},
]


func initial_battalion_count() -> int:
	var configured_unit_rules: Dictionary = _rules.get("unit_rules", {}) as Dictionary
	var count := 0
	for entry_value in _active_spawn_roster():
		var entry := entry_value as Dictionary
		if bool(entry.get("requires_unit_rule", false)) and not configured_unit_rules.has(String(entry.get("object_id", ""))):
			continue
		count += 1
	return count


func _active_spawn_roster() -> Array:
	return _spawn_roster if not _spawn_roster.is_empty() else DEFAULT_SPAWN_ROSTER


func _roster_team_ids() -> Array:
	## Ordered team ids for per-team dict seeding and the roster-ordered display
	## snapshot arrays. Falls back to the historical 2-team order when no roster
	## is seeded yet (e.g. tests that call _apply_gameplay_rules without setup()),
	## keeping every legacy path byte-identical.
	if _team_roster.is_empty():
		return [PLAYER_TEAM, ENEMY_TEAM]
	return _team_roster


func _seed_team_roster() -> void:
	## Rebuild the explicit team registry from the injected descriptor list, or
	## the default 2-team roster when none was injected. Ascending insertion in
	## descriptor order is what keeps the default {0,1} dict order (and therefore
	## the pinned battle signature) identical to the pre-registry literals.
	var source: Array = _pending_team_roster if not _pending_team_roster.is_empty() else DEFAULT_TEAM_ROSTER
	_team_roster = []
	_team_descriptors = {}
	for index in source.size():
		var descriptor := (source[index] as Dictionary).duplicate(true)
		var team := int(descriptor.get("team", index))
		descriptor["team"] = team
		if not _team_roster.has(team):
			_team_roster.append(team)
		_team_descriptors[team] = descriptor


func configure_team_roster(descriptors: Array) -> void:
	## Injection seam for the team registry. The descriptor list is consumed at
	## the next setup(); an empty list restores the default 2-team roster.
	_pending_team_roster = descriptors.duplicate(true)


func team_ids() -> Array:
	return _roster_team_ids().duplicate()


func team_descriptor(team: int) -> Dictionary:
	return (_team_descriptors.get(team, {}) as Dictionary).duplicate(true)


func team_is_ai(team: int) -> bool:
	return bool((_team_descriptors.get(team, {}) as Dictionary).get("is_ai", team != PLAYER_TEAM))


func team_difficulty(team: int) -> String:
	## The resolved difficulty tier a team's AI runs at. Reads the seeded per-team
	## AI state first (so it survives snapshot/restore), then the descriptor, then
	## the legacy default. An unknown tier name falls back to the default so an
	## injected typo can never desync determinism.
	var from_state: Dictionary = _team_ai_state.get(team, {}) as Dictionary
	var tier := String(from_state.get("difficulty", (_team_descriptors.get(team, {}) as Dictionary).get("difficulty", AI_DEFAULT_DIFFICULTY)))
	if not AI_DIFFICULTY_PROFILES.has(tier):
		tier = AI_DEFAULT_DIFFICULTY
	return tier


func _difficulty_profile(team: int) -> Dictionary:
	return AI_DIFFICULTY_PROFILES.get(team_difficulty(team), AI_DIFFICULTY_PROFILES[AI_DEFAULT_DIFFICULTY]) as Dictionary


func _seed_team_ai_state() -> void:
	## One AI-state record per rostered AI team, in ascending roster order. Each
	## record carries the team's difficulty tier and the per-team controller
	## counters. For the default {0,1} roster this yields exactly {1: medium} — the
	## single legacy AI — so its command stream (and the pinned signature) is
	## unchanged.
	_team_ai_state = {}
	for team in _roster_team_ids():
		if not team_is_ai(int(team)):
			continue
		var tier := String((_team_descriptors.get(team, {}) as Dictionary).get("difficulty", AI_DEFAULT_DIFFICULTY))
		if not AI_DIFFICULTY_PROFILES.has(tier):
			tier = AI_DEFAULT_DIFFICULTY
		_team_ai_state[int(team)] = {
			"difficulty": tier,
			"construction_attempted": false,
			"construction_resolved": false,
			"build_order_index": 0,
			"last_wave_tick": 0,
		}


func team_alliance(team: int) -> Variant:
	## Alliance id for a team from its descriptor, or null when the team declares
	## no alliance. Teams sharing a non-null alliance id are friendly; every other
	## distinct team pair is mutually hostile (the free-for-all default).
	var descriptor := _team_descriptors.get(team, {}) as Dictionary
	if descriptor.has("alliance") and descriptor.get("alliance") != null:
		return descriptor.get("alliance")
	return null


func _is_combatant_team(team: int) -> bool:
	## Only rostered teams fight. Non-rostered owners (the NEUTRAL_TEAM that holds
	## capturable flags, map props) are never hostile to anyone, exactly as the old
	## strict "other of {0,1}" flip never targeted a team-2 neutral structure.
	if _team_roster.is_empty():
		return team == PLAYER_TEAM or team == ENEMY_TEAM
	return _team_descriptors.has(team)


func _is_hostile(team_a: int, team_b: int) -> bool:
	## Alliance/hostility predicate (N-team). A team is never hostile to itself,
	## and non-combatant (non-rostered) owners are neutral. Two distinct rostered
	## teams are hostile unless they share the same non-null alliance id. Absent
	## alliance ids are the default free-for-all: every distinct team is mutually
	## hostile. For the historical {0,1} roster this returns exactly the old
	## "other team" boolean, so targeting/victory stay byte-identical.
	if team_a == team_b:
		return false
	if not _is_combatant_team(team_a) or not _is_combatant_team(team_b):
		return false
	var alliance_a: Variant = team_alliance(team_a)
	var alliance_b: Variant = team_alliance(team_b)
	if alliance_a != null and alliance_b != null and alliance_a == alliance_b:
		return false
	return true


func _hostile_living_ids(team: int) -> Array[int]:
	## Every living battalion hostile to `team`, ascending id order. For the
	## 2-team free-for-all this is identical to living_ids(other_team): the sole
	## hostile team's units in the same id order entity_ids() already produces.
	var result: Array[int] = []
	for id in entity_ids():
		var row: Dictionary = entities[id]
		if int(row["health"]) > 0 and _is_hostile(team, int(row.get("team", -1))):
			result.append(id)
	return result


func _hostile_living_structure_ids(team: int) -> Array[int]:
	## Living structures hostile to `team`, ascending id order (identical to
	## living_structure_ids(other_team) for the 2-team default).
	var result: Array[int] = []
	for id in structure_ids():
		var row: Dictionary = structures[id]
		if int(row.get("health", 0)) > 0 and _is_hostile(team, int(row.get("team", -1))):
			result.append(id)
	return result


func _seed_team_map(default_value: Variant) -> Dictionary:
	## Seed a per-team dict with a fresh copy of default_value per rostered team,
	## in roster order. Arrays/dicts are duplicated so teams never share mutable
	## state. For the default {0,1} roster this reproduces the old 2-key literals
	## exactly (same keys, same insertion order, same values).
	var seeded := {}
	for team in _roster_team_ids():
		match typeof(default_value):
			TYPE_ARRAY:
				seeded[team] = (default_value as Array).duplicate(true)
			TYPE_DICTIONARY:
				seeded[team] = (default_value as Dictionary).duplicate(true)
			_:
				seeded[team] = default_value
	return seeded


func _seed_next_dynamic_ids() -> Dictionary:
	## Per-team dynamic-id cursors. The formula reproduces the historical seeds
	## (team 0 -> 10, team 1 -> 110) and extends deterministically to team N.
	var seeded := {}
	for team in _roster_team_ids():
		seeded[team] = 10 + team * 100
	return seeded


func _seed_team_manifest_tables() -> void:
	## Point every rostered team at the compiled manifest table set. Today one
	## manifest drives every team, so each entry aliases the single global table
	## and behavior is byte-identical. The guarded per-team branch reads an
	## optional _rules["team_faction_manifests"] map so a future caller can
	## supply distinct manifests; when it is absent (today) all teams share.
	var provided: Dictionary = _rules.get("team_faction_manifests", {}) as Dictionary
	var shared_manifest: Dictionary = _rules.get("faction_manifest", {}) as Dictionary
	var shared_faction := String(shared_manifest.get("faction", ""))
	_team_manifests = {}
	_team_unit_production_rules = {}
	_team_structure_build_rules = {}
	_team_spawn_roster = {}
	_team_structure_max_health = {}
	_team_structure_armor = {}
	_team_ai_production_plan = {}
	_team_seed_structure_kinds = {}
	_team_structure_kinds = {}
	_team_production_unit_order = {}
	for team in _roster_team_ids():
		var manifest: Dictionary = provided.get(team, shared_manifest) as Dictionary
		_team_manifests[team] = manifest
		# A team whose injected manifest is the same faction that compiled the
		# global tables (the player faction) aliases those globals exactly — this
		# keeps the default same-faction path byte-identical, including the
		# ranger/trebuchet overlays that only live on the globals. A team on a
		# DIFFERENT faction derives its own tables straight from its manifest dict
		# (elf/men object ids never collide, so the sim's unit_rules stays a union).
		if manifest.is_empty() or String(manifest.get("faction", "")) == shared_faction:
			_team_unit_production_rules[team] = _unit_production_rules
			_team_structure_build_rules[team] = _structure_build_rules
			_team_spawn_roster[team] = _spawn_roster
			_team_structure_max_health[team] = _structure_max_health
			_team_structure_armor[team] = _structure_armor
			_team_ai_production_plan[team] = _ai_production_plan
			_team_seed_structure_kinds[team] = _seed_structure_kinds
			_team_structure_kinds[team] = _structure_kinds
			_team_production_unit_order[team] = _production_unit_order
		else:
			var kinds: Array = (manifest.get("structure_kinds", []) as Array).duplicate(true)
			var seed_kinds: Array = (manifest.get("seed_structure_kinds", kinds) as Array).duplicate(true)
			var plan: Array = (manifest.get("ai_production_plan", []) as Array).duplicate(true)
			_team_unit_production_rules[team] = (manifest.get("unit_production_rules", {}) as Dictionary).duplicate(true)
			_team_structure_build_rules[team] = (manifest.get("structure_build_rules", {}) as Dictionary).duplicate(true)
			_team_spawn_roster[team] = (manifest.get("spawn_roster", []) as Array).duplicate(true)
			_team_structure_max_health[team] = (manifest.get("structure_max_health", {}) as Dictionary).duplicate(true)
			_team_structure_armor[team] = (manifest.get("structure_armor", {}) as Dictionary).duplicate(true)
			_team_ai_production_plan[team] = plan
			_team_seed_structure_kinds[team] = seed_kinds if not seed_kinds.is_empty() else kinds
			_team_structure_kinds[team] = kinds
			# A fresh faction ships no trebuchet/ranger overlay, so its production
			# order is exactly its AI plan.
			_team_production_unit_order[team] = plan.duplicate(true)


func team_manifest_for(team: int) -> Dictionary:
	return _team_manifests.get(team, _rules.get("faction_manifest", {})) as Dictionary


func unit_production_rules_for_team(team: int) -> Dictionary:
	return _team_unit_production_rules.get(team, _unit_production_rules) as Dictionary


func structure_build_rules_for_team(team: int) -> Dictionary:
	return _team_structure_build_rules.get(team, _structure_build_rules) as Dictionary


func spawn_roster_for_team(team: int) -> Array:
	return _team_spawn_roster.get(team, _spawn_roster) as Array


func structure_max_health_for_team(team: int) -> Dictionary:
	return _team_structure_max_health.get(team, _structure_max_health) as Dictionary


func structure_armor_for_team(team: int) -> Dictionary:
	return _team_structure_armor.get(team, _structure_armor) as Dictionary


func ai_production_plan_for_team(team: int) -> Array:
	return _team_ai_production_plan.get(team, _ai_production_plan) as Array


func seed_structure_kinds_for_team(team: int) -> Array:
	return _team_seed_structure_kinds.get(team, _seed_structure_kinds) as Array


func structure_kinds_for_team(team: int) -> Array:
	return _team_structure_kinds.get(team, _structure_kinds) as Array


func production_unit_order_for_team(team: int) -> Array:
	return _team_production_unit_order.get(team, _production_unit_order) as Array

var tick_index := 0
var winner := -1
var ai_enabled := true
var selected_ids: Array[int] = []
var control_groups: Dictionary = {}
var events: Array[Dictionary] = []
var _event_digest := 0x811C9DC5
var entities: Dictionary = {}
var structures: Dictionary = {}
var team_resources: Dictionary = {PLAYER_TEAM: 0, ENEMY_TEAM: 0}
var team_command_points: Dictionary = {PLAYER_TEAM: 0, ENEMY_TEAM: 0}
## Explicit team registry (N-team foundation, step 1-2). Seeded at setup() from
## an injected descriptor list; defaults to the historical 2-team roster so
## every existing behavior stays byte-identical. Each descriptor is
## {team:int, faction:String, is_ai:bool, ...}. The ordered id list drives every
## per-team dict seed and the roster-ordered display-snapshot arrays.
const DEFAULT_TEAM_ROSTER: Array = [
	{"team": PLAYER_TEAM, "faction": "", "is_ai": false},
	{"team": ENEMY_TEAM, "faction": "", "is_ai": true},
]
var _team_roster: Array = []
var _team_descriptors: Dictionary = {}
var _pending_team_roster: Array = []
## Per-team faction plumbing (N-team foundation, step 1). Today one manifest
## drives every team, so each per-team entry below aliases the single compiled
## global table (byte-identical to the 2-team past). A future packet compiles
## distinct tables per manifest; consumers reach them via the *_for_team
## accessors. These maps are derived (rebuilt at setup/restore) so they are
## intentionally NOT part of the authoritative snapshot.
var _team_manifests: Dictionary = {}
var _team_unit_production_rules: Dictionary = {}
var _team_structure_build_rules: Dictionary = {}
var _team_spawn_roster: Dictionary = {}
var _team_structure_max_health: Dictionary = {}
var _team_structure_armor: Dictionary = {}
var _team_ai_production_plan: Dictionary = {}
var _team_seed_structure_kinds: Dictionary = {}
var _team_structure_kinds: Dictionary = {}
var _team_production_unit_order: Dictionary = {}
var command_point_cap := 200
var base_loop_enabled := false
var source_map_configured := false
var ford_gates: Array[Dictionary] = []
var source_player_starts: Dictionary = {}
var route_provider: RefCounted
var playable_outline := PackedVector2Array()
var last_route_rejection := ""
var _spawn_positions: Dictionary = {}
## Team -> spawn-anchor Vector2 for rostered teams beyond 0/1 (N-team spawn
## geometry). Populated from the map layer's team_start_centers; teams 0/1 keep
## deriving their anchors from spawn ids 1/2 and 101/102, so this is empty and
## inert for every 2-team match.
var _extra_team_centers: Dictionary = {}
var _home_layout: Dictionary = {}
var _rules: Dictionary = {}
var configuration_error := ""
var _unit_production_rules: Dictionary = {}
var _completed_hero_identities: Dictionary = {}
var _production_unit_order: Array[String] = []
var _ai_production_plan: Array[String] = []
var _unit_damage_types: Dictionary = {}
## member object id -> compiled armor contract (fractions, upgrade tables).
var _unit_armor: Dictionary = {}
## member object id -> upgrade id -> compiled WeaponSetUpgrade effect.
var _unit_weapon_upgrades: Dictionary = {}
## structure kind -> compiled armor table (fractions).
var _structure_armor: Dictionary = {}
var _spawn_roster: Array = []
var _structure_kinds: Array[String] = []
## Kinds pre-placed at match start. Full-faction manifests seed fortresses only;
## constructable kinds remain in _structure_kinds for the builder UI.
var _seed_structure_kinds: Array[String] = []
var _structure_max_health: Dictionary = {}
var _structure_build_rules: Dictionary = {}
var _unit_prerequisites: Dictionary = {}
var _structure_upgrade_contracts: Dictionary = {}
## Doc-driven PLAYER research/economy bindings per structure kind (the
## compiled research and upgradeEffects blocks); stale packs keep them empty
## and the recorded provisional forge contracts below stay the fallback.
var _structure_upgrade_effects: Dictionary = {}
var _compiled_research_kinds: Dictionary = {}
## Per-unit-type authored OBJECT purchase rows (the horde command set's
## OBJECT_UPGRADE buttons) and per-member LevelUpUpgrade effects (Basic
## Training). Eligibility always rides the compiled unit documents.
var _unit_upgrade_commands: Dictionary = {}
var _unit_level_upgrades: Dictionary = {}
var units_without_upgrade_commands: Array[String] = []
var _next_dynamic_id: Dictionary = {PLAYER_TEAM: 10, ENEMY_TEAM: 110}
var _next_dynamic_structure_id := 3000
var _next_event_sequence := 1
var _next_order_sequence := 1
var _music_state := ""
## Per-team AI controller state (N-team difficulty). One entry per AI team,
## keyed by team id, each carrying its own difficulty tier plus the counters the
## old single ENEMY_TEAM AI held in scalars (construction_attempted/resolved,
## build_order_index, last_wave_tick). Seeded at setup() from each is_ai
## descriptor; serialized/restored so the snapshot round-trips and twin runs stay
## hash-equal. For the default {0,1} roster this holds exactly {1: medium}, so the
## single legacy AI behaves byte-identically.
var _team_ai_state: Dictionary = {}
var _last_base_under_attack_tick := -100000
var _pending_commands: Dictionary = {}
var last_command_result: Variant = null
var _state_hash_static_digest := PackedByteArray()


func setup(map_configuration: Dictionary = {}, gameplay_rules: Dictionary = {}) -> void:
	_seed_team_roster()
	if not map_configuration.is_empty():
		_apply_map_configuration(map_configuration)
	elif _spawn_positions.is_empty():
		_apply_fallback_configuration()
	tick_index = 0
	winner = -1
	ai_enabled = true
	selected_ids.clear()
	reset_control_groups()
	events.clear()
	_event_digest = 0x811C9DC5
	entities.clear()
	structures.clear()
	_completed_hero_identities.clear()
	_next_event_sequence = 1
	_next_order_sequence = 1
	_music_state = ""
	_seed_team_ai_state()
	_last_base_under_attack_tick = -100000
	_pending_commands.clear()
	last_command_result = null
	_state_hash_static_digest.clear()
	last_route_rejection = ""
	team_power_points = _seed_team_map(1)
	purchased_powers = _seed_team_map([])
	_kills_toward_power_point = _seed_team_map(0)
	_reset_spellbook_match_state()
	clock_paused = false
	_apply_gameplay_rules(gameplay_rules if not gameplay_rules.is_empty() else _rules)
	team_upgrades = _seed_team_map({})
	_has_hero_units = false
	_register_forge_upgrade_contracts()
	_next_dynamic_id = _seed_next_dynamic_ids()
	_next_dynamic_structure_id = 3000
	if bool(_rules.get("spawn_initial_battalions", true)):
		var configured_unit_rules: Dictionary = _rules.get("unit_rules", {}) as Dictionary
		# Each rostered team seeds from ITS OWN faction's spawn roster (per-team
		# manifests). For the default same-faction roster every team aliases the
		# single global roster, so the union of team-filtered passes reproduces the
		# historical spawn set exactly; the snapshot serializes by sorted id, so the
		# per-team pass order never moves the signature.
		for team in _roster_team_ids():
			var team_roster: Array = spawn_roster_for_team(int(team))
			if team_roster.is_empty():
				team_roster = DEFAULT_SPAWN_ROSTER
			for entry_value in team_roster:
				var entry := entry_value as Dictionary
				if int(entry.get("team", PLAYER_TEAM)) != int(team):
					continue
				var object_id := String(entry.get("object_id", ""))
				if bool(entry.get("requires_unit_rule", false)) and not configured_unit_rules.has(object_id):
					continue
				var spawn_position: Vector2 = (
					Vector2(entry.get("position"))
					if typeof(entry.get("position")) == TYPE_VECTOR2
					else _spawn_anchor_position(String(entry.get("anchor", "")), int(team))
				)
				_add_battalion(
					int(entry.get("id", 0)),
					int(team),
					spawn_position,
					String(entry.get("name", "")),
					object_id,
					String(entry.get("unit_type", object_id)),
					int(entry.get("command_points", -1))
				)
	if base_loop_enabled:
		_initialize_base_loop()
	# Spellbook effect rules (summon stats) bake the source→sim scale, which
	# only exists once the gameplay rules are applied above: recompute them
	# now. Ownership/points already reset; configure only touches doc-derived
	# rows and match-scoped spellbook state.
	if _spellbook_ready and not _spellbook_document.is_empty():
		configure_spellbook_runtime(_spellbook_document)
	_emit_music("explore")


func _apply_map_configuration(configuration: Dictionary) -> void:
	var configured_spawns: Variant = configuration.get("spawn_positions", {})
	var configured_gates: Variant = configuration.get("ford_gates", [])
	var configured_starts: Variant = configuration.get("player_starts", {})
	var configured_provider: Variant = configuration.get("route_provider")
	var configured_outline: Variant = configuration.get("playable_outline", PackedVector2Array())
	if typeof(configured_spawns) != TYPE_DICTIONARY or typeof(configured_gates) != TYPE_ARRAY or typeof(configured_starts) != TYPE_DICTIONARY or typeof(configured_provider) != TYPE_OBJECT or typeof(configured_outline) != TYPE_PACKED_VECTOR2_ARRAY:
		_apply_fallback_configuration()
		return
	if configured_provider == null or not configured_provider.has_method("query_route"):
		_apply_fallback_configuration()
		return
	var candidate_spawns: Dictionary = configured_spawns
	for id in [1, 2, 101, 102]:
		if not candidate_spawns.has(id) or typeof(candidate_spawns[id]) != TYPE_VECTOR2:
			_apply_fallback_configuration()
			return
	var candidate_gates: Array = configured_gates
	if candidate_gates.size() != 3:
		_apply_fallback_configuration()
		return
	var validated_gates: Array[Dictionary] = []
	for value in candidate_gates:
		if typeof(value) != TYPE_DICTIONARY:
			_apply_fallback_configuration()
			return
		var gate: Dictionary = value
		if typeof(gate.get("edge_a")) != TYPE_VECTOR2 or typeof(gate.get("edge_b")) != TYPE_VECTOR2 or typeof(gate.get("center")) != TYPE_VECTOR2:
			_apply_fallback_configuration()
			return
		validated_gates.append(gate.duplicate(true))
	_spawn_positions = candidate_spawns.duplicate(true)
	ford_gates = validated_gates
	source_player_starts = (configured_starts as Dictionary).duplicate(true)
	route_provider = configured_provider as RefCounted
	playable_outline = (configured_outline as PackedVector2Array).duplicate()
	var configured_home_layout: Variant = configuration.get("home_layout", {})
	_home_layout = (configured_home_layout as Dictionary).duplicate(true) if typeof(configured_home_layout) == TYPE_DICTIONARY else {}
	# Optional per-team spawn anchors for rostered teams beyond 0/1 (N-team maps
	# expose all authored Player_N_Start centers here). Absent on 2-team configs.
	var configured_team_centers: Variant = configuration.get("team_start_centers", {})
	_extra_team_centers = {}
	if typeof(configured_team_centers) == TYPE_DICTIONARY:
		for team_key in (configured_team_centers as Dictionary).keys():
			var center_value: Variant = (configured_team_centers as Dictionary)[team_key]
			if typeof(center_value) == TYPE_VECTOR2:
				_extra_team_centers[int(team_key)] = center_value
	source_map_configured = bool(configuration.get("source_map_configured", false))


func _apply_fallback_configuration() -> void:
	_spawn_positions = {
		1: Vector2(-38.0, -14.0),
		2: Vector2(-38.0, 14.0),
		101: Vector2(38.0, -14.0),
		102: Vector2(38.0, 14.0),
	}
	ford_gates = [
		{"name": "fallback-south", "edge_a": Vector2(-11.0, -23.0), "edge_b": Vector2(11.0, -23.0), "center": Vector2(0.0, -23.0)},
		{"name": "fallback-center", "edge_a": Vector2(-11.0, 0.0), "edge_b": Vector2(11.0, 0.0), "center": Vector2.ZERO},
		{"name": "fallback-north", "edge_a": Vector2(-11.0, 23.0), "edge_b": Vector2(11.0, 23.0), "center": Vector2(0.0, 23.0)},
	]
	source_player_starts.clear()
	route_provider = null
	playable_outline = PackedVector2Array()
	_home_layout.clear()
	_extra_team_centers = {}
	source_map_configured = false


func _apply_gameplay_rules(gameplay_rules: Dictionary) -> void:
	_rules = gameplay_rules.duplicate(true)
	configuration_error = ""
	if not _configure_faction_manifest():
		return
	_unit_prerequisites.clear()
	_structure_upgrade_contracts.clear()
	_structure_upgrade_effects.clear()
	_compiled_research_kinds.clear()
	_unit_upgrade_commands.clear()
	_unit_level_upgrades.clear()
	units_without_upgrade_commands.clear()
	_unit_experience_rules.clear()
	_experience_unauthored_victims.clear()
	_configure_ranger_runtime_contract()
	_configure_trebuchet_runtime_contract()
	_configure_playable_unit_runtime_contracts()
	_configure_structure_upgrade_chains()
	_configure_structure_research_contracts()
	_configure_manifest_builders()
	_validate_faction_manifest_coherence()
	_record_structure_armor_provisionals()
	base_loop_enabled = bool(_rules.get("enable_base_loop", false))
	command_point_cap = maxi(60, int(_rules.get("command_point_cap", 200)))
	var starting_resources := maxi(0, int(_rules.get("starting_resources", 1200 if base_loop_enabled else 0)))
	team_resources = _seed_team_map(starting_resources)
	team_command_points = _seed_team_map(120)
	_seed_team_manifest_tables()


func _configure_faction_manifest() -> bool:
	## Every faction-scoped table flows through the manifest. Absent keys
	## default to exactly the historical Gondor constants, so a rules
	## dictionary without a manifest stays byte-identical to the old behavior.
	var manifest_value: Variant = _rules.get("faction_manifest", {})
	if typeof(manifest_value) != TYPE_DICTIONARY:
		configuration_error = "Faction manifest is not a dictionary"
		return false
	var manifest := manifest_value as Dictionary
	if manifest.has("_error"):
		configuration_error = "Faction manifest is invalid: %s" % String(manifest.get("_error", ""))
		return false
	var typed_expectations := {
		"unit_production_rules": TYPE_DICTIONARY,
		"ai_production_plan": TYPE_ARRAY,
		"structure_kinds": TYPE_ARRAY,
		"structure_max_health": TYPE_DICTIONARY,
		"structure_build_rules": TYPE_DICTIONARY,
		"structure_armor": TYPE_DICTIONARY,
		"unit_damage_types": TYPE_DICTIONARY,
		"spawn_roster": TYPE_ARRAY,
		"builder_unit_ids": TYPE_ARRAY,
	}
	for key in typed_expectations:
		if manifest.has(key) and typeof(manifest.get(key)) != int(typed_expectations[key]):
			configuration_error = "Faction manifest field '%s' has the wrong type" % String(key)
			return false
	_unit_production_rules = (manifest.get("unit_production_rules", UNIT_PRODUCTION_RULES) as Dictionary).duplicate(true)
	var plan: Array = Array(manifest.get("ai_production_plan", AI_PRODUCTION_PLAN))
	var kinds: Array = Array(manifest.get("structure_kinds", STRUCTURE_KINDS))
	for table in [plan, kinds]:
		for value in table as Array:
			if typeof(value) != TYPE_STRING or String(value).strip_edges() == "":
				configuration_error = "Faction manifest plan or structure kinds contain a non-string entry"
				return false
	_production_unit_order.assign(plan)
	_ai_production_plan.assign(plan)
	_structure_kinds.assign(kinds)
	var seed_kinds: Array = Array(manifest.get("seed_structure_kinds", kinds))
	for value in seed_kinds:
		if typeof(value) != TYPE_STRING or String(value).strip_edges() == "":
			configuration_error = "Faction manifest seed_structure_kinds contain a non-string entry"
			return false
		var seed_kind := String(value)
		var found_seed := false
		for kind_value in kinds:
			if String(kind_value) == seed_kind:
				found_seed = true
				break
		if not found_seed:
			configuration_error = "Faction manifest seed kind '%s' is not in structure_kinds" % seed_kind
			return false
	_seed_structure_kinds.clear()
	for value in seed_kinds:
		_seed_structure_kinds.append(String(value))
	if _seed_structure_kinds.is_empty():
		_seed_structure_kinds.assign(_structure_kinds)
	_structure_max_health = (manifest.get("structure_max_health", STRUCTURE_MAX_HEALTH) as Dictionary).duplicate(true)
	_structure_build_rules = (manifest.get("structure_build_rules", STRUCTURE_BUILD_RULES) as Dictionary).duplicate(true)
	_unit_damage_types = (manifest.get("unit_damage_types", UNIT_DAMAGE_TYPES) as Dictionary).duplicate(true)
	# Compiled per-kind structure armor (armor.ini via each structure document).
	# Legacy manifests without it keep the FortressArmor mirror with per-line
	# armor.ini provenance; kinds outside the mirror are recorded provisionals.
	_structure_armor = (manifest.get("structure_armor", DEFAULT_STRUCTURE_ARMOR) as Dictionary).duplicate(true)
	_spawn_roster = (manifest.get("spawn_roster", DEFAULT_SPAWN_ROSTER) as Array).duplicate(true)
	for kind_value in _structure_kinds:
		var kind := String(kind_value)
		if int(_structure_max_health.get(kind, 0)) <= 0:
			configuration_error = "Faction manifest structure kind '%s' has no positive maximum health" % kind
			return false
		var build_rule: Dictionary = _structure_build_rules.get(kind, {}) as Dictionary
		if int(build_rule.get("cost", -1)) < 0 or float(build_rule.get("seconds", 0.0)) <= 0.0:
			configuration_error = "Faction manifest structure kind '%s' has no valid build rule" % kind
			return false
	return true


func _record_structure_armor_provisionals() -> void:
	## Every structure kind must have either a compiled armor table or a
	## recorded provisional — the armor layer is never a silent 0.25.
	structure_armor_provisional_kinds.clear()
	for kind_value in _structure_kinds:
		var kind := String(kind_value)
		if _structure_armor.has(kind):
			continue
		structure_armor_provisional_kinds.append(kind)
		print("[RetailSliceSim] structure armor: kind '%s' has no compiled armor.ini table; structure damage uses the recorded provisional scalar %.2f" % [kind, STRUCTURE_ARMOR_PROVISIONAL_SCALAR])


func _compiled_armor_table(table_value: Variant) -> Dictionary:
	## Normalize one compiled armor.ini table to fraction scalars.
	if typeof(table_value) != TYPE_DICTIONARY:
		return {}
	var table := table_value as Dictionary
	var scalars := {}
	var default_percent := 100.0
	var default_value: Variant = table.get("default", {})
	if typeof(default_value) == TYPE_DICTIONARY:
		default_percent = float((default_value as Dictionary).get("percent", 100.0))
	scalars["default"] = default_percent / 100.0
	for key_value in (table.get("scalars", {}) as Dictionary).keys():
		var row: Variant = (table.get("scalars", {}) as Dictionary).get(key_value)
		if typeof(row) != TYPE_DICTIONARY:
			continue
		scalars[String(key_value)] = float((row as Dictionary).get("percent", default_percent)) / 100.0
	return {
		"damage_scalar": float((table.get("damageScalar", {}) as Dictionary).get("percent", 100.0)) / 100.0,
		"scalars": scalars,
	}


func _compiled_armor_rule(document: Dictionary) -> Dictionary:
	## Normalize a playableUnit document's compiled armor block. Three honest
	## states: a compiled table, the recorded SAGE passthrough for objects with
	## no authored ArmorSet, and {} for stale packs with no armor block at all
	## (recorded as an exclusion by the caller, passthrough 1.0 at damage time).
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var simulation: Dictionary = registration.get("simulation", {}) as Dictionary
	var resolved: Dictionary = simulation.get("resolved", {}) as Dictionary
	var armor_value: Variant = resolved.get("armor", null)
	if typeof(armor_value) != TYPE_DICTIONARY:
		return {}
	var armor := armor_value as Dictionary
	if armor.get("setId") == null or String(armor.get("setId", "")) == "":
		return {"passthrough": true, "semantic": String(armor.get("semantic", ""))}
	var rule := _compiled_armor_table(armor.get("table", {}))
	if rule.is_empty():
		return {}
	rule["set_id"] = String(armor.get("setId", ""))
	rule["passthrough"] = false
	var upgrades := {}
	for upgrade_value in armor.get("upgrades", []) as Array:
		if typeof(upgrade_value) != TYPE_DICTIONARY:
			continue
		var upgrade := upgrade_value as Dictionary
		var upgrade_id := String(upgrade.get("upgradeId", ""))
		var upgrade_table := _compiled_armor_table(upgrade.get("table", {}))
		if upgrade_id == "" or upgrade_table.is_empty():
			continue
		upgrade_table["set_id"] = String(upgrade.get("setId", ""))
		upgrades[upgrade_id] = upgrade_table
	rule["upgrades"] = upgrades
	return rule


func _parse_damage_scalar(entry: Dictionary) -> Dictionary:
	## Split a compiled DamageScalar row into an evaluable filter. Retail
	## evidence: `ANY +INFANTRY -HERO` (has INFANTRY, not HERO), `ALL -STRUCTURE`
	## (lacks STRUCTURE), `NONE +MINE` (has MINE — the first token never negates
	## the +kinds; see armor_compiler.py and weapon.ini usage).
	var relation := "ANY"
	var plus: Array[String] = []
	var minus: Array[String] = []
	for token in String(entry.get("filter", "")).split(" ", false):
		if token in ["ANY", "ALL", "NONE"]:
			relation = token
		elif token.begins_with("+"):
			plus.append(token.substr(1))
		elif token.begins_with("-"):
			minus.append(token.substr(1))
	return {
		"percent": float(entry.get("percent", 100.0)) / 100.0,
		"filter": String(entry.get("filter", "")),
		"relation": relation,
		"plus": plus,
		"minus": minus,
	}


func _compiled_damage_scalars(rows: Array) -> Array:
	var scalars: Array = []
	for row_value in rows:
		if typeof(row_value) != TYPE_DICTIONARY:
			continue
		scalars.append(_parse_damage_scalar(row_value as Dictionary))
	return scalars


func _compiled_weapon_upgrade_rules(document: Dictionary) -> Dictionary:
	## Normalize a document's compiled WeaponSetUpgrade effects keyed by
	## upgrade id. {} when the document carries none.
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var simulation: Dictionary = registration.get("simulation", {}) as Dictionary
	var resolved: Dictionary = simulation.get("resolved", {}) as Dictionary
	var combat: Dictionary = resolved.get("combat", {}) as Dictionary
	var base_damage_type := String(combat.get("damageType", "")).to_lower()
	var effects := {}
	for upgrade_value in combat.get("upgrades", []) as Array:
		if typeof(upgrade_value) != TYPE_DICTIONARY:
			continue
		var upgrade := upgrade_value as Dictionary
		var upgrade_id := String(upgrade.get("upgradeId", ""))
		var kind := String(upgrade.get("kind", ""))
		if upgrade_id == "":
			continue
		var effect := {"kind": kind, "scalars": _compiled_damage_scalars(upgrade.get("damageScalars", []) as Array)}
		match kind:
			"weapon-swap":
				effect["damage"] = float((upgrade.get("damage", {}) as Dictionary).get("value", 0.0))
				effect["damage_type"] = String(upgrade.get("damageType", "")).to_lower()
				effect["weapon_id"] = String(upgrade.get("weaponId", ""))
			"nugget-upgrade":
				var total := 0.0
				var types := {}
				for nugget_value in upgrade.get("nuggets", []) as Array:
					var nugget := nugget_value as Dictionary
					total += float((nugget.get("damage", {}) as Dictionary).get("value", 0.0))
					var nugget_type := String(nugget.get("damageType", "")).to_lower()
					if nugget_type != "":
						types[nugget_type] = true
					effect["scalars"] = (effect["scalars"] as Array) + _compiled_damage_scalars(nugget.get("damageScalars", []) as Array)
				effect["damage"] = total
				effect["damage_type"] = types.keys()[0] if types.size() == 1 else ""
				effect["weapon_id"] = String(upgrade.get("weaponId", ""))
			"warhead-upgrade":
				# The primary nugget keeps the base damage type (fire arrows keep
				# their pierce); every other nugget is an authored bonus hit.
				var bonus: Array = []
				for nugget_value in upgrade.get("nuggets", []) as Array:
					var nugget := nugget_value as Dictionary
					var nugget_type := String(nugget.get("damageType", "")).to_lower()
					var row := {
						"damage": float((nugget.get("damage", {}) as Dictionary).get("value", 0.0)),
						"damage_type": nugget_type,
						"scalars": _compiled_damage_scalars(nugget.get("damageScalars", []) as Array),
					}
					if nugget_type == base_damage_type and not effect.has("damage"):
						effect["damage"] = row["damage"]
						effect["damage_type"] = nugget_type
						effect["scalars"] = row["scalars"]
					else:
						bonus.append(row)
				effect["bonus_nuggets"] = bonus
				effect["warhead_id"] = String(upgrade.get("warheadId", ""))
			_:
				continue
		effects[upgrade_id] = effect
	return effects


func _compiled_level_up_rules(document: Dictionary) -> Dictionary:
	## Normalize a document's compiled LevelUpUpgrade modules (Basic Training)
	## keyed by upgrade id. {} when the document carries none.
	var gameplay: Dictionary = (document.get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary
	var rules := {}
	for row_value in gameplay.get("levelUpgrades", []) as Array:
		if typeof(row_value) != TYPE_DICTIONARY:
			continue
		var row := row_value as Dictionary
		var upgrade_id := String(row.get("upgradeId", ""))
		if upgrade_id == "":
			continue
		rules[upgrade_id] = {
			"levels_to_gain": int(row.get("levelsToGain", 1)),
			"level_cap": int(row.get("levelCap", 2)),
		}
	return rules


func _compiled_unit_upgrade_commands(document: Dictionary) -> Array:
	## Normalize the horde's compiled OBJECT_UPGRADE purchase rows. Rows with a
	## malformed identity fail the document to an empty surface; the caller
	## cross-checks every row against the compiled effect tables.
	var gameplay: Dictionary = (document.get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary
	var rows: Array = []
	for row_value in gameplay.get("upgradeCommands", []) as Array:
		if typeof(row_value) != TYPE_DICTIONARY:
			continue
		var row := row_value as Dictionary
		var needed: Array = []
		for needed_value in Array(row.get("neededUpgradeIds", [])):
			needed.append(String(needed_value))
		rows.append({
			"upgrade_id": String(row.get("upgradeId", "")),
			"command_id": String(row.get("commandId", "")),
			"command_set_id": String(row.get("commandSetId", "")),
			"slot": int(row.get("slot", 0)),
			"cost": maxi(0, int(row.get("cost", 0))),
			"duration_ticks": maxi(1, roundi(float(row.get("buildTimeSeconds", 0.0)) / TICK_SECONDS)),
			"cancelable": bool(row.get("cancelable", false)),
			"multi_select": bool(row.get("multiSelect", false)),
			"needed_upgrade_ids": needed,
			"needed_upgrade_any": bool(row.get("neededUpgradeAny", false)),
			"label_id": String(row.get("labelId", "")),
			"tooltip_id": String(row.get("tooltipId", "")),
			"image_id": String(row.get("buttonImageId", "")),
			"lacks_prerequisite_label_id": String(row.get("lacksPrerequisiteLabelId", "")),
		})
	return rows


func _filter_kind_matches(kind: String, target: Dictionary, target_kind: String) -> bool:
	var folded := kind.to_lower()
	if folded == "structure":
		return target_kind == "structure"
	if target_kind == "structure":
		# Object-id kinds (+EntMoot, +MordorCatapult) match structures by id.
		return folded == String(target.get("source_object_id", target.get("object_id", ""))).to_lower()
	match folded:
		"infantry":
			return String(target.get("category", "")) in ["infantry", "ranged-infantry"]
		"hero":
			return String(target.get("category", "")) == "hero"
		"cavalry":
			return String(target.get("category", "")) == "cavalry"
		"monster":
			return String(target.get("category", "")) == "monster"
		"machine", "siegeengine", "siege_weapon":
			return String(target.get("category", "")) == "siege"
		"mine", "summoned":
			# The slice fields no MINE/SUMMONED objects; recorded, never matched.
			return false
	return folded == String(target.get("object_id", "")).to_lower()


func _damage_scalar_factor(scalars: Array, target: Dictionary, target_kind: String) -> float:
	## First matching authored DamageScalar wins (retail authors them mutually
	## exclusive: 200% vs infantry, 150% vs heroes on GondorSwordUpgraded).
	for scalar_value in scalars:
		var scalar: Dictionary = scalar_value
		var plus: Array = scalar.get("plus", [])
		var plus_ok := true
		if not plus.is_empty():
			if String(scalar.get("relation", "ANY")) == "ALL":
				for kind_value in plus:
					if not _filter_kind_matches(String(kind_value), target, target_kind):
						plus_ok = false
						break
			else:
				plus_ok = false
				for kind_value in plus:
					if _filter_kind_matches(String(kind_value), target, target_kind):
						plus_ok = true
						break
		if not plus_ok:
			continue
		var minus_ok := true
		for kind_value in scalar.get("minus", []) as Array:
			if _filter_kind_matches(String(kind_value), target, target_kind):
				minus_ok = false
				break
		if minus_ok:
			return float(scalar.get("percent", 1.0))
	return 1.0


func _member_armor_scalar(target: Dictionary, damage_type: String) -> float:
	## Compiled armor.ini scalar: attacker damage type vs the victim's set
	## (its recorded applied armor upgrade swaps the set, last applied wins).
	var rule: Dictionary = _unit_armor.get(String(target.get("object_id", "")), {})
	if rule.is_empty() or bool(rule.get("passthrough", false)):
		return 1.0
	var table := rule
	var active := String(target.get("active_armor_upgrade", ""))
	if active != "":
		var upgrades: Dictionary = rule.get("upgrades", {})
		if (target.get("applied_upgrades", {}) as Dictionary).has(active) and upgrades.has(active):
			table = upgrades[active]
	var scalars: Dictionary = table.get("scalars", {})
	var key := damage_type.to_lower()
	return float(table.get("damage_scalar", 1.0)) * float(scalars.get(key, scalars.get("default", 1.0)))


func _applied_weapon_effect(row: Dictionary) -> Dictionary:
	## The compiled WeaponSetUpgrade effect this horde has recorded as applied.
	var effects: Dictionary = _unit_weapon_upgrades.get(String(row.get("object_id", "")), {})
	if effects.is_empty():
		return {}
	var applied: Dictionary = row.get("applied_upgrades", {})
	var upgrade_ids: Array = effects.keys()
	upgrade_ids.sort()
	for upgrade_id_value in upgrade_ids:
		if applied.has(String(upgrade_id_value)):
			return effects[upgrade_id_value]
	return {}


func _configure_manifest_builders() -> void:
	## A data-driven faction names its builder via the structure documents'
	## authored construct routes; the flag rides the normalized unit rule.
	if configuration_error != "":
		return
	var manifest: Dictionary = _rules.get("faction_manifest", {}) as Dictionary
	var configured_unit_rules: Dictionary = _rules.get("unit_rules", {}) as Dictionary
	for builder_value in manifest.get("builder_unit_ids", []) as Array:
		var builder_id := String(builder_value)
		if not configured_unit_rules.has(builder_id):
			configuration_error = "Faction manifest builder unit '%s' has no configured unit rule" % builder_id
			return
		var builder_rule: Dictionary = configured_unit_rules[builder_id] as Dictionary
		builder_rule["is_builder"] = true
		configured_unit_rules[builder_id] = builder_rule
	_rules["unit_rules"] = configured_unit_rules


func _validate_faction_manifest_coherence() -> void:
	## Only an explicitly supplied manifest is validated end-to-end; legacy
	## rule dictionaries keep their historical permissiveness.
	if configuration_error != "" or not _rules.has("faction_manifest"):
		return
	var manifest: Dictionary = _rules.get("faction_manifest", {}) as Dictionary
	if manifest.is_empty():
		return
	for unit_type_value in _ai_production_plan:
		var unit_type := String(unit_type_value)
		if not _unit_production_rules.has(unit_type):
			configuration_error = "Faction manifest AI plan entry '%s' has no production rule" % unit_type
			return
	var configured_unit_rules: Dictionary = _rules.get("unit_rules", {}) as Dictionary
	for entry_value in _spawn_roster:
		if typeof(entry_value) != TYPE_DICTIONARY:
			configuration_error = "Faction manifest spawn roster contains a non-object entry"
			return
		var entry := entry_value as Dictionary
		var object_id := String(entry.get("object_id", ""))
		if object_id == "" or int(entry.get("id", 0)) <= 0:
			configuration_error = "Faction manifest spawn roster entry is missing its identity"
			return
		if not bool(entry.get("requires_unit_rule", false)) and not configured_unit_rules.has(object_id):
			configuration_error = "Faction manifest spawn roster entry '%s' has no configured unit rule" % object_id
			return


func _configure_playable_unit_runtime_contracts() -> void:
	var value: Variant = _rules.get("playable_unit_runtimes", {})
	if typeof(value) != TYPE_DICTIONARY:
		configuration_error = "Playable-unit runtime registry is not a dictionary"
		return
	var configured_unit_rules: Dictionary = _rules.get("unit_rules", {}) as Dictionary
	var producer_kinds: Dictionary = _rules.get("producer_kind_by_source_object", {}) as Dictionary
	_unit_armor.clear()
	_unit_weapon_upgrades.clear()
	missing_armor_units.clear()
	missing_damage_type_units.clear()
	var object_ids: Array[String] = []
	for object_id_value in (value as Dictionary).keys():
		object_ids.append(String(object_id_value))
	object_ids.sort_custom(func(a: String, b: String) -> bool: return a.naturalnocasecmp_to(b) < 0)
	for object_id in object_ids:
		var document_value: Variant = (value as Dictionary).get(object_id)
		if typeof(document_value) != TYPE_DICTIONARY:
			configuration_error = "Playable-unit runtime '%s' is invalid" % object_id
			return
		# Compiled armor.ini contract + forge upgrade effects ride every fresh
		# document; a stale pack without the block is a recorded exclusion with
		# the SAGE passthrough, never an invented multiplier. One canonical,
		# document-derived id space: every rule registers under BOTH ids the
		# document itself resolves (runtime_member_id = primaryMemberObjectId,
		# runtime_unit_id = containerObjectId), so entity object_id lookups,
		# unit_type-keyed purchase surfaces, and gate checks read one space —
		# no hardcoded alias tables.
		var armor_member_id := PlayableUnitAdapter.runtime_member_id(document_value as Dictionary)
		var armor_unit_id := PlayableUnitAdapter.runtime_unit_id(document_value as Dictionary)
		var armor_id_space: Array[String] = [armor_member_id]
		if armor_unit_id != "" and armor_unit_id != armor_member_id:
			armor_id_space.append(armor_unit_id)
		var armor_rule := _compiled_armor_rule(document_value as Dictionary)
		if armor_rule.is_empty():
			if not missing_armor_units.has(armor_member_id):
				missing_armor_units.append(armor_member_id)
				print("[RetailSliceSim] unit '%s' has no compiled armor block (stale pack); incoming damage uses the recorded SAGE passthrough 1.0" % armor_member_id)
		elif not bool(armor_rule.get("passthrough", false)):
			for armor_id in armor_id_space:
				_unit_armor[armor_id] = armor_rule
		var weapon_upgrade_rules := _compiled_weapon_upgrade_rules(document_value as Dictionary)
		if not weapon_upgrade_rules.is_empty():
			for armor_id in armor_id_space:
				_unit_weapon_upgrades[armor_id] = weapon_upgrade_rules
		# Authored LevelUpUpgrade effects (Basic Training) and the horde's own
		# OBJECT_UPGRADE purchase buttons ride the same document; eligibility is
		# the compiled effect tables above — never a class-name guess.
		var level_up_rules := _compiled_level_up_rules(document_value as Dictionary)
		if not level_up_rules.is_empty():
			for armor_id in armor_id_space:
				_unit_level_upgrades[armor_id] = level_up_rules
		var purchase_rows := _compiled_unit_upgrade_commands(document_value as Dictionary)
		if purchase_rows.is_empty():
			if not units_without_upgrade_commands.has(armor_member_id):
				units_without_upgrade_commands.append(armor_member_id)
		else:
			var armor_upgrade_ids: Dictionary = (armor_rule.get("upgrades", {}) as Dictionary)
			for row_value in purchase_rows:
				var purchase := row_value as Dictionary
				var purchase_id := String(purchase.get("upgrade_id", ""))
				if (
					not _unit_weapon_upgrades.get(armor_member_id, {}).has(purchase_id)
					and not armor_upgrade_ids.has(purchase_id)
					and not _unit_level_upgrades.get(armor_member_id, {}).has(purchase_id)
				):
					configuration_error = "Playable-unit runtime '%s' authors purchase '%s' with no compiled weapon/armor/level effect" % [object_id, purchase_id]
					return
			var purchase_unit_type := String(PlayableUnitAdapter.simulation_rule(document_value as Dictionary).get("unit_type", ""))
			if purchase_unit_type != "":
				_unit_upgrade_commands[purchase_unit_type] = purchase_rows
		var simulation := PlayableUnitAdapter.simulation_rule(document_value as Dictionary)
		if simulation.is_empty():
			# The faction builder legitimately lacks combat evidence and costs
			# zero command points; it registers through the narrower builder
			# production projection instead of failing the whole roster.
			if _register_builder_production(document_value as Dictionary, configured_unit_rules, producer_kinds):
				continue
			configuration_error = "Playable-unit runtime '%s' has unresolved simulation evidence" % object_id
			return
		var unit_type := String(simulation["unit_type"])
		var member_id := String(simulation["object_id"])
		if (
			_unit_production_rules.has(unit_type)
			and String((_unit_production_rules[unit_type] as Dictionary).get("object_id", "")) != member_id
		):
			configuration_error = "Playable-unit runtime '%s' collides with production '%s'" % [object_id, unit_type]
			return
		var unit_rule := PlayableUnitAdapter.normalized_unit_rule(simulation, float(_rules.get("source_map_transform_scale", 0.0)))
		if unit_rule.is_empty():
			unit_rule = (configured_unit_rules.get(member_id, {}) as Dictionary).duplicate(true)
		if unit_rule.is_empty():
			configuration_error = "Playable-unit runtime '%s' has no normalized unit rule" % object_id
			return
		var producers: Array = simulation.get("producers", [])
		if producers.is_empty():
			configuration_error = "Playable-unit runtime '%s' has no producer" % object_id
			return
		var resolved_producers: Array[Dictionary] = []
		var resolved_producer_kinds: Array[String] = []
		for producer_value in producers:
			var producer := producer_value as Dictionary
			var source_producer := String(producer.get("producer_source_object_id", ""))
			var producer_kind := String(producer_kinds.get(source_producer, ""))
			if producer_kind == "":
				configuration_error = "Playable-unit runtime '%s' producer '%s' is not loaded by this faction slice" % [object_id, source_producer]
				return
			var route := producer.duplicate(true)
			route["producer_kind"] = producer_kind
			resolved_producers.append(route)
			if not resolved_producer_kinds.has(producer_kind):
				resolved_producer_kinds.append(producer_kind)
		var primary_producer := resolved_producers[0]
		unit_rule["category"] = String(simulation.get("category", unit_rule.get("category", "")))
		var horde_override: Dictionary = (_rules.get("horde_speed_overrides", {}) as Dictionary).get(member_id, {}) as Dictionary
		if not horde_override.is_empty() and int(unit_rule.get("member_count", 0)) > 1:
			# Retail hordes move at their horde LocomotorSet speed (the pack's
			# retail unit rules); the document speed is the unit-object
			# locomotor. Both stay recorded; the horde value drives movement.
			var scale := float(_rules.get("source_map_transform_scale", 0.0))
			unit_rule["speed"] = float(horde_override.get("speed", 0.0)) * scale
			unit_rule["speed_source"] = float(horde_override.get("speed", 0.0))
			var override_provenance: Dictionary = (unit_rule.get("provenance", {}) as Dictionary).duplicate(true)
			override_provenance["horde_locomotor"] = {
				"speed": float(horde_override.get("speed", 0.0)),
				"source": (horde_override.get("source", {}) as Dictionary).duplicate(true),
				"unit_object_speed": float(horde_override.get("unit_object_speed", 0.0)),
				"note": "horde movement uses the horde LocomotorSet; the document speed is the unit-object locomotor",
			}
			unit_rule["provenance"] = override_provenance
		configured_unit_rules[member_id] = unit_rule
		var doc_damage_type := String((simulation.get("combat", {}) as Dictionary).get("damageType", "")).to_lower()
		if doc_damage_type != "":
			# Source-authored BFME2 damage type (SLASH/PIERCE/CAVALRY/...);
			# fortress armor consumes it through the same scalar table the
			# historical Men constants use, unknown types keep the default.
			_unit_damage_types[member_id] = doc_damage_type
		elif not missing_damage_type_units.has(member_id):
			# A combat unit whose document authors no damageType is recorded at
			# configure — never only when it happens to spawn (retail Arwen
			# carries powers but no weapon); its structure damage falls to each
			# kind's DEFAULT armor scalar.
			missing_damage_type_units.append(member_id)
			print("[RetailSliceSim] unit '%s' has no authored damageType; its structure damage uses each kind's DEFAULT armor scalar" % member_id)
		_unit_production_rules[unit_type] = {
			"category": String(simulation.get("category", "")),
			"producer_kind": String(primary_producer["producer_kind"]),
			"producer_kinds": resolved_producer_kinds,
			"producer_routes": resolved_producers,
			"producer_source_object_id": String(primary_producer["producer_source_object_id"]),
			"object_id": member_id,
			"display_name": String(simulation["display_name"]),
			"default_cost": int(simulation["default_cost"]),
			"default_build_ticks": int(simulation["default_build_ticks"]),
			"default_command_points": int(simulation["default_command_points"]),
			"command_id": String(primary_producer.get("command_id", "")),
			"command_slot": int(primary_producer.get("slot", 0)),
			"surface": String(primary_producer.get("surface", "")),
		}
		if not _production_unit_order.has(unit_type):
			_production_unit_order.append(unit_type)
		var prerequisites_by_producer: Dictionary = {}
		for route in resolved_producers:
			var producer_kind := String(route["producer_kind"])
			var candidate: Array = (route.get("prerequisites", []) as Array).duplicate()
			if not prerequisites_by_producer.has(producer_kind):
				prerequisites_by_producer[producer_kind] = candidate
				continue
			# One authored route per producer level can list the same button with
			# different prerequisites (base/L2/L3 command sets). The unit is
			# trainable as soon as its cheapest authored variant's prerequisites
			# hold, so the effective requirement is the minimum-cardinality set;
			# ties keep deterministic document order.
			var existing: Array = prerequisites_by_producer[producer_kind]
			if candidate.size() < existing.size():
				prerequisites_by_producer[producer_kind] = candidate
		_unit_prerequisites[unit_type] = prerequisites_by_producer
		# Hero SPECIAL_POWER abilities (converted doc rows) register per unit
		# type; heroes without an abilities array simply carry none.
		var ability_rows := PlayableUnitAdapter.ability_rules(document_value as Dictionary)
		if not ability_rows.is_empty():
			_unit_ability_rules[unit_type] = _scaled_ability_rules(
				ability_rows, float(_rules.get("source_map_transform_scale", 0.0))
			)
		# ExperienceLevel chain (converted doc rows) registers per unit type;
		# units whose chain retail never authored carry no rule and never gain
		# XP — their kill payout is the recorded default, recorded per victim.
		var experience_rule := PlayableUnitAdapter.experience_rule(document_value as Dictionary)
		if not experience_rule.is_empty():
			_unit_experience_rules[unit_type] = experience_rule
	_rules["unit_rules"] = configured_unit_rules


func _register_builder_production(document: Dictionary, configured_unit_rules: Dictionary, producer_kinds: Dictionary) -> bool:
	var partial := PlayableUnitAdapter.builder_production_rule(document)
	if partial.is_empty():
		return false
	var member_id := String(partial["object_id"])
	var builder_rule: Dictionary = configured_unit_rules.get(member_id, {}) as Dictionary
	if builder_rule.is_empty() or not bool(builder_rule.get("is_builder", false)):
		return false
	var resolved_producers: Array[Dictionary] = []
	var resolved_producer_kinds: Array[String] = []
	for producer_value in partial["producers"] as Array:
		var producer := producer_value as Dictionary
		var producer_kind := String(producer_kinds.get(String(producer.get("producer_source_object_id", "")), ""))
		if producer_kind == "":
			return false
		var route := producer.duplicate(true)
		route["producer_kind"] = producer_kind
		resolved_producers.append(route)
		if not resolved_producer_kinds.has(producer_kind):
			resolved_producer_kinds.append(producer_kind)
	var unit_type := String(partial["unit_type"])
	var primary_producer := resolved_producers[0]
	_unit_production_rules[unit_type] = {
		"category": String(partial.get("category", "")),
		"producer_kind": String(primary_producer["producer_kind"]),
		"producer_kinds": resolved_producer_kinds,
		"producer_routes": resolved_producers,
		"producer_source_object_id": String(primary_producer.get("producer_source_object_id", "")),
		"object_id": member_id,
		"display_name": String(partial["display_name"]),
		"default_cost": int(partial["default_cost"]),
		"default_build_ticks": int(partial["default_build_ticks"]),
		"default_command_points": int(partial["default_command_points"]),
		"command_id": String(primary_producer.get("command_id", "")),
		"command_slot": int(primary_producer.get("slot", 0)),
		"surface": String(primary_producer.get("surface", "")),
	}
	if not _production_unit_order.has(unit_type):
		_production_unit_order.append(unit_type)
	var prerequisites_by_producer: Dictionary = {}
	for route_value in resolved_producers:
		var route := route_value as Dictionary
		var producer_kind := String(route["producer_kind"])
		if not prerequisites_by_producer.has(producer_kind):
			prerequisites_by_producer[producer_kind] = (route.get("prerequisites", []) as Array).duplicate()
	_unit_prerequisites[unit_type] = prerequisites_by_producer
	return true


func _configure_ranger_runtime_contract() -> void:
	var value: Variant = _rules.get("ranger_runtime", {})
	if typeof(value) != TYPE_DICTIONARY:
		configuration_error = "Ranger runtime contract is not a dictionary"
		return
	var contract := value as Dictionary
	if contract.is_empty():
		return
	if (
		String(contract.get("schema", "")) != "openbfme.ranger-runtime-contract"
		or int(contract.get("schemaVersion", -1)) != 0
		or String(contract.get("capabilityStatus", "")) != "rules-and-prerequisite-ready"
	):
		configuration_error = "Ranger runtime contract identity is invalid"
		return
	var production_value: Variant = contract.get("production")
	var prerequisite_value: Variant = contract.get("prerequisite")
	var unit_rule_value: Variant = _rules.get("ranger_unit_rule")
	if typeof(production_value) != TYPE_DICTIONARY or typeof(prerequisite_value) != TYPE_DICTIONARY or typeof(unit_rule_value) != TYPE_DICTIONARY:
		configuration_error = "Ranger runtime contract is missing production, prerequisite, or normalized unit rule"
		return
	var production := production_value as Dictionary
	var prerequisite := prerequisite_value as Dictionary
	var unit_rule := unit_rule_value as Dictionary
	var command_sets_value: Variant = contract.get("commandSets")
	var train_options_value: Variant = prerequisite.get("trainCommandOptions")
	var upgrade_options_value: Variant = prerequisite.get("options")
	if typeof(command_sets_value) != TYPE_ARRAY or typeof(train_options_value) != TYPE_ARRAY or typeof(upgrade_options_value) != TYPE_ARRAY:
		configuration_error = "Ranger runtime contract options are invalid"
		return
	if not _ranger_command_sets_are_valid(command_sets_value as Array):
		configuration_error = "Ranger runtime command-set transition is invalid"
		return
	var required_unit_rule_fields: Array[String] = [
		"horde_id", "member_count", "member_health", "member_damage",
		"speed", "speed_source", "acceleration", "acceleration_source",
		"turn_rate_degrees_per_second", "braking", "braking_source",
		"attack_range", "attack_range_source", "minimum_attack_range",
		"minimum_attack_range_source", "vision_range", "vision_range_source",
		"delay_between_shots_ms", "pre_attack_delay_ms", "firing_duration_ms",
		"attack_period_ticks", "pre_attack_ticks", "firing_duration_ticks",
		"clip_size", "clip_reload_time_ms", "continuous_fire_one",
		"continuous_fire_coast_ticks", "continuous_fire_rate_multiplier",
		"formation_positions", "provenance",
	]
	for field in required_unit_rule_fields:
		if not unit_rule.has(field):
			configuration_error = "Ranger normalized unit rule is missing %s" % field
			return
	var upgrade_id := String(prerequisite.get("upgradeId", ""))
	var upgrade_cost := int(prerequisite.get("cost", -1))
	var upgrade_ticks := roundi(float(prerequisite.get("buildTimeSeconds", -1.0)) / TICK_SECONDS)
	var ranger_cost := int(production.get("buildCost", -1))
	var ranger_ticks := roundi(float(production.get("buildTime", -1.0)) / TICK_SECONDS)
	var ranger_command_points := int(production.get("commandPoints", -1))
	if (
		String(production.get("id", "")) != "GondorRangerHorde"
		or String(prerequisite.get("trainCommandId", "")) != "Command_ConstructGondorRangerHorde"
		or not (train_options_value as Array).has("NEED_UPGRADE")
		or upgrade_id != "Upgrade_GondorArcheryRangeLevel2"
		or String(prerequisite.get("type", "")) != "OBJECT"
		or not (upgrade_options_value as Array).has("CANCELABLE")
		or int(prerequisite.get("levelsToGain", 0)) != 1
		or int(prerequisite.get("levelCap", 0)) != 3
		or String(prerequisite.get("fromCommandSet", "")) != "GondorArcheryCommandSet"
		or String(prerequisite.get("toCommandSet", "")) != "GondorArcheryCommandSetLevel2"
		or upgrade_cost < 0
		or upgrade_ticks <= 0
		or ranger_cost < 0
		or ranger_ticks <= 0
		or ranger_command_points <= 0
		or String(unit_rule.get("horde_id", "")) != RANGER_HORDE_ID
		or int(unit_rule.get("member_count", 0)) != 10
	):
		configuration_error = "Ranger runtime contract values are invalid"
		return
	var configured_unit_rules: Dictionary = _rules.get("unit_rules", {}) as Dictionary
	configured_unit_rules[RANGER_OBJECT_ID] = unit_rule.duplicate(true)
	_rules["unit_rules"] = configured_unit_rules
	_unit_production_rules[RANGER_HORDE_ID] = {
		"producer_kind": "archery_range",
		"object_id": RANGER_OBJECT_ID,
		"display_name": "Ithilien Rangers",
		"default_cost": ranger_cost,
		"default_build_ticks": ranger_ticks,
		"default_command_points": ranger_command_points,
	}
	_production_unit_order.append(RANGER_HORDE_ID)
	# Load-bearing only in the tiny/overlay pack: when a playableUnit ranger
	# document is fieldable, the doc-driven prerequisite map overwrites this
	# entry below; without a doc it is the only prerequisite source.
	_unit_prerequisites[RANGER_HORDE_ID] = upgrade_id
	# The overlay's own upgrade contract rides the same generic registration
	# the doc-driven structure chains use below; a doc-driven chain for the
	# same upgrade id overwrites it with the full authored chain.
	_register_structure_upgrade_contract(upgrade_id, {
		"structure_kind": "archery_range",
		"cost": upgrade_cost,
		"duration_ticks": upgrade_ticks,
		"levels_to_gain": 1,
		"level_cap": 3,
		"to_level": 2,
		"cancelable": true,
		"command_id": "",
		"from_command_set": String(prerequisite.get("fromCommandSet", "")),
		"to_command_set": String(prerequisite.get("toCommandSet", "")),
		"requires_upgrade_id": "",
		"health_add": 0,
		"production_multiplier": 1.0,
	})


func _configure_structure_upgrade_chains() -> void:
	## Generic doc-driven structure levels: every authored upgrade chain the
	## faction manifest projected from the playableStructure.* documents
	## (cost/time/level cap/command-set swap/per-level effects) registers here
	## keyed by its authored upgrade id. Malformed chains fail closed into
	## configuration_error instead of registering a partial contract.
	if configuration_error != "":
		return
	var manifest: Dictionary = _rules.get("faction_manifest", {}) as Dictionary
	var value: Variant = _rules.get(
		"structure_upgrade_chains",
		manifest.get("structure_upgrade_chains", {})
	)
	if typeof(value) != TYPE_DICTIONARY:
		configuration_error = "Structure upgrade chains are not a dictionary"
		return
	var kinds: Array[String] = []
	for kind_value in (value as Dictionary).keys():
		kinds.append(String(kind_value))
	kinds.sort()
	for kind in kinds:
		var chain_value: Variant = (value as Dictionary).get(kind)
		if typeof(chain_value) != TYPE_DICTIONARY:
			configuration_error = "Structure upgrade chain for '%s' is not a dictionary" % kind
			return
		var chain := chain_value as Dictionary
		var level_cap := int(chain.get("levelCap", 0))
		var steps_value: Variant = chain.get("steps")
		if level_cap < 2 or typeof(steps_value) != TYPE_ARRAY or (steps_value as Array).is_empty():
			configuration_error = "Structure upgrade chain for '%s' is malformed" % kind
			return
		var previous_to_level := 1
		for step_value in steps_value as Array:
			if typeof(step_value) != TYPE_DICTIONARY:
				configuration_error = "Structure upgrade chain for '%s' has a malformed step" % kind
				return
			var step := step_value as Dictionary
			var upgrade_id := String(step.get("upgradeId", ""))
			var to_level := int(step.get("toLevel", 0))
			var cost := int(step.get("cost", -1))
			var build_seconds := float(step.get("buildTimeSeconds", 0.0))
			var from_set := String(step.get("fromCommandSet", ""))
			var to_set := String(step.get("toCommandSet", ""))
			if (
				upgrade_id == ""
				or to_level <= previous_to_level
				or to_level > level_cap
				or cost < 0
				or build_seconds <= 0.0
				or from_set == ""
				or to_set == ""
				or int(step.get("levelsToGain", 0)) < 1
				or int(step.get("levelCap", 0)) != level_cap
			):
				configuration_error = "Structure upgrade chain for '%s' step '%s' is malformed" % [kind, upgrade_id]
				return
			var health_add := 0
			var production_multiplier := 1.0
			var unsupported: Array[String] = []
			for leaf_value in Array(step.get("effects", [])):
				if typeof(leaf_value) != TYPE_DICTIONARY:
					configuration_error = "Structure upgrade chain for '%s' has a malformed effect" % kind
					return
				var leaf := leaf_value as Dictionary
				for modifier_value in Array(leaf.get("modifiers", [])):
					if typeof(modifier_value) != TYPE_DICTIONARY:
						configuration_error = "Structure upgrade chain for '%s' has a malformed modifier" % kind
						return
					var modifier := modifier_value as Dictionary
					match String(modifier.get("kind", "")):
						"HEALTH":
							health_add += roundi(float(modifier.get("value", 0.0)))
						"PRODUCTION":
							production_multiplier *= float(modifier.get("value", 1.0))
						_:
							configuration_error = "Structure upgrade chain for '%s' has an unsupported modifier" % kind
							return
				for unsupported_value in Array(leaf.get("unsupportedModifiers", [])):
					unsupported.append(String(unsupported_value))
			var contract := {
				"structure_kind": kind,
				"cost": cost,
				"duration_ticks": maxi(1, roundi(build_seconds / TICK_SECONDS)),
				"levels_to_gain": int(step.get("levelsToGain", 1)),
				"level_cap": level_cap,
				"to_level": to_level,
				"cancelable": bool(step.get("cancelable", false)),
				"command_id": String(step.get("commandId", "")),
				"from_command_set": from_set,
				"to_command_set": to_set,
				"requires_upgrade_id": String(step.get("requiresUpgradeId", "")),
				"health_add": health_add,
				"production_multiplier": production_multiplier,
				# Purchase-surface + per-level model data from the doc: the HUD
				# button binds these, the structure presenter applies them.
				"slot": int(step.get("slot", 0)),
				"label_id": String(step.get("labelId", "")),
				"tooltip_id": String(step.get("tooltipId", "")),
				"image_id": String(step.get("buttonImageId", "")),
			}
			var presentation_value: Variant = step.get("presentation")
			if typeof(presentation_value) == TYPE_DICTIONARY:
				var presentation := presentation_value as Dictionary
				var visible: Array[String] = []
				var hidden: Array[String] = []
				for token_value in Array(presentation.get("visibleSubObjects", [])):
					visible.append(String(token_value))
				for token_value in Array(presentation.get("hiddenSubObjects", [])):
					hidden.append(String(token_value))
				contract["visible_sub_objects"] = visible
				contract["hidden_sub_objects"] = hidden
			if not unsupported.is_empty():
				unsupported.sort()
				contract["unsupported_modifiers"] = unsupported
			if not _register_structure_upgrade_contract(upgrade_id, contract):
				return
			previous_to_level = to_level


func _register_structure_upgrade_contract(upgrade_id: String, contract: Dictionary) -> bool:
	## One upgrade id binds exactly one structure kind; collisions across
	## sources fail closed instead of picking a winner.
	if _structure_upgrade_contracts.has(upgrade_id):
		var existing: Dictionary = _structure_upgrade_contracts[upgrade_id]
		if String(existing.get("structure_kind", "")) != String(contract.get("structure_kind", "")):
			configuration_error = "Structure upgrade '%s' binds two structure kinds" % upgrade_id
			return false
	_structure_upgrade_contracts[upgrade_id] = contract
	return true


func _configure_structure_research_contracts() -> void:
	## Doc-driven PLAYER technology sales: every authored research row the
	## faction manifest projected from the playableStructure.* documents
	## (cost/time/gate/button identity) registers here keyed by its authored
	## upgrade id. Malformed surfaces fail closed into configuration_error.
	if configuration_error != "":
		return
	var manifest: Dictionary = _rules.get("faction_manifest", {}) as Dictionary
	var research_value: Variant = _rules.get(
		"structure_research",
		manifest.get("structure_research", {})
	)
	if typeof(research_value) != TYPE_DICTIONARY:
		configuration_error = "Structure research surfaces are not a dictionary"
		return
	var effects_value: Variant = _rules.get(
		"structure_upgrade_effects",
		manifest.get("structure_upgrade_effects", {})
	)
	if typeof(effects_value) != TYPE_DICTIONARY:
		configuration_error = "Structure upgrade effects are not a dictionary"
		return
	var kinds: Array[String] = []
	for kind_value in (effects_value as Dictionary).keys():
		kinds.append(String(kind_value))
	kinds.sort()
	for kind in kinds:
		var container: Dictionary = (effects_value as Dictionary).get(kind, {}) as Dictionary
		var normalized_effects: Array = []
		for effect_value in Array(container.get("effects", [])):
			if typeof(effect_value) != TYPE_DICTIONARY:
				continue
			var effect := effect_value as Dictionary
			normalized_effects.append({
				"upgrade_id": String(effect.get("upgradeId", "")),
				"kind": String(effect.get("kind", "")),
				"apply_to_upgrade_ids": Array(effect.get("applyToUpgradeIds", [])).duplicate(),
				"percent": float(effect.get("percent", 0.0)),
				"upgrade_discount": bool(effect.get("upgradeDiscount", false)),
				"refund_percent": float(effect.get("refundPercent", 0.0)),
				"building_required": String(effect.get("buildingRequired", "")),
				"bonus_percent": float(effect.get("bonusPercent", 0.0)),
				"upgrade_must_be_present": String(effect.get("upgradeMustBePresent", "")),
				"label_id": String(effect.get("labelId", "")),
			})
		_structure_upgrade_effects[kind] = {
			"effects": normalized_effects,
			"unsupported_effects": Array(container.get("unsupportedEffects", [])).duplicate(true),
		}
	kinds.clear()
	for kind_value in (research_value as Dictionary).keys():
		kinds.append(String(kind_value))
	kinds.sort()
	for kind in kinds:
		var surface: Dictionary = (research_value as Dictionary).get(kind, {}) as Dictionary
		var rows: Array = surface.get("upgrades", []) as Array
		if rows.is_empty():
			configuration_error = "Structure research surface for '%s' is malformed" % kind
			return
		_compiled_research_kinds[kind] = true
		for row_value in rows:
			if typeof(row_value) != TYPE_DICTIONARY:
				configuration_error = "Structure research surface for '%s' has a malformed row" % kind
				return
			var row := row_value as Dictionary
			var upgrade_id := String(row.get("upgradeId", ""))
			var needed: Array[String] = []
			for needed_value in Array(row.get("neededUpgradeIds", [])):
				needed.append(String(needed_value))
			var contract := {
				"structure_kind": kind,
				"cost": maxi(0, int(row.get("cost", 0))),
				"duration_ticks": maxi(1, roundi(float(row.get("buildTimeSeconds", 0.0)) / TICK_SECONDS)),
				"level_cap": 99,
				"levels_to_gain": 0,
				"cancelable": bool(row.get("cancelable", false)),
				"to_command_set": "",
				"team_tech": true,
				# Research rows ride every per-level command set of the building;
				# the authored slot/labels/images surface them like chain steps.
				"research": true,
				"command_id": String(row.get("commandId", "")),
				"slot": int(row.get("slot", 0)),
				"label_id": String(row.get("labelId", "")),
				"tooltip_id": String(row.get("tooltipId", "")),
				"image_id": String(row.get("buttonImageId", "")),
				"lacks_prerequisite_label_id": String(row.get("lacksPrerequisiteLabelId", "")),
				"needed_upgrade_ids": needed,
				"needed_upgrade_any": bool(row.get("neededUpgradeAny", false)),
			}
			if not _register_structure_upgrade_contract(upgrade_id, contract):
				return


func _research_gate_unsatisfied(team: int, building: Dictionary, contract: Dictionary) -> String:
	## "" when the research's NeededUpgrade row is satisfied: each needed id is
	## a team technology or a completed structure upgrade on this building.
	var needed: Array = contract.get("needed_upgrade_ids", [])
	if needed.is_empty():
		return ""
	var owned: Dictionary = team_upgrades.get(team, {}) as Dictionary
	var completed: Array = building.get("completed_upgrades", [])
	var satisfied := 0
	var first_missing := ""
	for needed_value in needed:
		var needed_id := String(needed_value)
		if owned.has(needed_id) or completed.has(needed_id):
			satisfied += 1
		elif first_missing == "":
			first_missing = needed_id
	if bool(contract.get("needed_upgrade_any", false)):
		return "" if satisfied > 0 else (first_missing if first_missing != "" else String(needed[0]))
	return "" if satisfied == needed.size() else first_missing


func _configure_trebuchet_runtime_contract() -> void:
	var value: Variant = _rules.get("trebuchet_runtime", {})
	if typeof(value) != TYPE_DICTIONARY:
		configuration_error = "Trebuchet runtime contract is not a dictionary"
		return
	var contract := value as Dictionary
	if contract.is_empty():
		return
	if (
		String(contract.get("schema", "")) != "openbfme.trebuchet-runtime-contract"
		or int(contract.get("schemaVersion", -1)) != 0
		or String(contract.get("capabilityStatus", "")) != "bounded-direct-structure-ready"
	):
		configuration_error = "Trebuchet runtime contract identity is invalid"
		return
	var production: Dictionary = contract.get("production", {}) as Dictionary
	var unit: Dictionary = contract.get("unit", {}) as Dictionary
	var movement: Dictionary = unit.get("movement", {}) as Dictionary
	var combat: Dictionary = contract.get("combat", {}) as Dictionary
	var workshop: Dictionary = contract.get("workshop", {}) as Dictionary
	var workshop_stats: Dictionary = workshop.get("stats", {}) as Dictionary
	var scale := float(_rules.get("source_map_transform_scale", 0.0))
	if (
		scale <= 0.0
		or String(production.get("id", "")) != "GondorTrebuchet"
		or String(unit.get("objectId", "")) != "GondorTrebuchet"
		or String(workshop_stats.get("id", "")) != "GondorWorkshop"
		or String(workshop.get("trainCommandId", "")) != "Command_ConstructGondorTrebuchet"
		or String(movement.get("mode", "")) != "existing-generic-unit-path"
		or String(combat.get("scope", "")) != "direct-structure-first-slice"
		or String(combat.get("damageType", "")).to_lower() != "siege"
	):
		configuration_error = "Trebuchet runtime contract values are invalid"
		return
	var speed_source := float(movement.get("speed", 0.0))
	var attack_range_source := float(combat.get("attackRange", 0.0))
	var minimum_range_source := float(combat.get("minimumAttackRange", 0.0))
	var vision_source := float(unit.get("visionRange", 0.0))
	var delay_ms := float(combat.get("delayBetweenShotsMs", 0.0))
	var pre_attack_ms := float(combat.get("preAttackDelayMs", 0.0))
	var firing_ms := float(combat.get("firingDurationMs", 0.0))
	var health := int(unit.get("maximumHealth", 0))
	var damage := int(combat.get("damage", 0))
	var build_cost := int(production.get("buildCost", -1))
	var build_ticks := roundi(float(production.get("buildTime", -1.0)) / TICK_SECONDS)
	var command_points := int(production.get("commandPoints", -1))
	var workshop_cost := int(workshop_stats.get("buildCost", -1))
	var workshop_ticks := roundi(float(workshop_stats.get("buildTime", -1.0)) / TICK_SECONDS)
	var workshop_health := int(workshop_stats.get("maxHealth", 0))
	if (
		speed_source <= 0.0
		or attack_range_source <= 0.0
		or minimum_range_source <= 0.0
		or vision_source <= 0.0
		or attack_range_source <= minimum_range_source
		or health <= 0
		or damage <= 0
		or build_cost <= 0
		or build_ticks <= 0
		or command_points <= 0
		or workshop_cost <= 0
		or workshop_ticks <= 0
		or workshop_health <= 0
		or delay_ms <= 0.0
		or pre_attack_ms <= 0.0
		or firing_ms <= 0.0
	):
		configuration_error = "Trebuchet runtime numeric contract is invalid"
		return
	var configured_unit_rules: Dictionary = _rules.get("unit_rules", {}) as Dictionary
	configured_unit_rules[TREBUCHET_OBJECT_ID] = {
		"horde_id": TREBUCHET_OBJECT_ID,
		"member_count": 1,
		"member_health": health,
		"member_damage": damage,
		"speed": speed_source * scale,
		"speed_source": speed_source,
		"acceleration": speed_source * scale,
		"acceleration_source": speed_source,
		# Provisional: the typed trebuchet contract has no authored turn rate,
		# so the legacy 360°/s placeholder is retained and marked as such in
		# provenance (the doc-driven path normalizes the real value instead).
		"turn_rate_degrees_per_second": 360.0,
		"braking": speed_source * scale,
		"braking_source": speed_source,
		"attack_range": attack_range_source * scale,
		"attack_range_source": attack_range_source,
		"minimum_attack_range": minimum_range_source * scale,
		"minimum_attack_range_source": minimum_range_source,
		"vision_range": vision_source * scale,
		"vision_range_source": vision_source,
		"delay_between_shots_ms": delay_ms,
		"pre_attack_delay_ms": pre_attack_ms,
		"firing_duration_ms": firing_ms,
		"attack_period_ticks": maxi(1, roundi(delay_ms / (TICK_SECONDS * 1000.0))),
		"pre_attack_ticks": maxi(0, roundi(pre_attack_ms / (TICK_SECONDS * 1000.0))),
		"firing_duration_ticks": maxi(0, roundi(firing_ms / (TICK_SECONDS * 1000.0))),
		"clip_size": int(combat.get("clipSize", 0)),
		"clip_reload_time_ms": 0.0,
		"continuous_fire_one": 0,
		"continuous_fire_coast_ticks": 0,
		"continuous_fire_rate_multiplier": 1.0,
		"formation_positions": [Vector3.ZERO],
		"formation_positions_base": [Vector3.ZERO],
		"formation_mode": "Line",
		"provenance": {"contractSources": contract.get("sources", []).duplicate(true), "turn_rate_status": "provisional-no-authored-turn-rate"},
	}
	_rules["unit_rules"] = configured_unit_rules
	_unit_production_rules[TREBUCHET_OBJECT_ID] = {
		"producer_kind": "workshop",
		"object_id": TREBUCHET_OBJECT_ID,
		"display_name": "Gondor Trebuchet",
		"default_cost": build_cost,
		"default_build_ticks": build_ticks,
		"default_command_points": command_points,
	}
	if not _production_unit_order.has(TREBUCHET_OBJECT_ID):
		_production_unit_order.append(TREBUCHET_OBJECT_ID)
	_structure_max_health["workshop"] = workshop_health
	_structure_build_rules["workshop"] = {"cost": workshop_cost, "seconds": float(workshop_stats.get("buildTime", 0.0))}


func _ranger_command_sets_are_valid(command_sets: Array) -> bool:
	var expected := {
		"GondorArcheryCommandSet": "Command_PurchaseUpgradeGondorArcheryRangeLevel2",
		"GondorArcheryCommandSetLevel2": "Command_PurchaseUpgradeGondorArcheryRangeLevel3",
	}
	var matched: Dictionary = {}
	for command_set_value in command_sets:
		if typeof(command_set_value) != TYPE_DICTIONARY:
			return false
		var command_set := command_set_value as Dictionary
		var command_set_id := String(command_set.get("id", ""))
		if not expected.has(command_set_id):
			continue
		var commands_value: Variant = command_set.get("commands")
		if typeof(commands_value) != TYPE_ARRAY:
			return false
		var has_ranger := false
		var has_upgrade := false
		for command_value in commands_value as Array:
			if typeof(command_value) != TYPE_DICTIONARY:
				return false
			var command := command_value as Dictionary
			var command_id := String(command.get("id", ""))
			var slot := int(command.get("slot", 0))
			has_ranger = has_ranger or (command_id == "Command_ConstructGondorRangerHorde" and slot == 2)
			has_upgrade = has_upgrade or (command_id == String(expected[command_set_id]) and slot == 4)
		if not has_ranger or not has_upgrade:
			return false
		matched[command_set_id] = true
	return matched.size() == expected.size()


func _team_structure_base(team: int) -> int:
	## Disjoint seeded-structure id band per team. Teams 0/1 keep their historical
	## 1000/2000 bands (byte-identical); teams >=2 tile above the dynamic id range.
	if team == PLAYER_TEAM:
		return PLAYER_STRUCTURE_BASE
	if team == ENEMY_TEAM:
		return ENEMY_STRUCTURE_BASE
	return 10000 + team * 1000


func _initialize_base_loop() -> void:
	structures.clear()
	var layout := _home_layout if not _home_layout.is_empty() else _derive_home_layout()
	for team in _roster_team_ids():
		var team_layout: Dictionary = layout.get(team, layout.get(str(team), {}))
		var base_id := _team_structure_base(team)
		var team_seed_kinds := seed_structure_kinds_for_team(team)
		if team_seed_kinds.is_empty():
			team_seed_kinds = structure_kinds_for_team(team)
		var team_max_health := structure_max_health_for_team(team)
		var team_production_order := production_unit_order_for_team(team)
		var team_production_rules := unit_production_rules_for_team(team)
		for index in range(team_seed_kinds.size()):
			var kind := String(team_seed_kinds[index])
			var position := Vector2(team_layout.get(kind, _fallback_structure_position(team, index)))
			var maximum_health := int(team_max_health[kind])
			var production: Array[String] = []
			for unit_type in team_production_order:
				var production_rule: Dictionary = team_production_rules[unit_type]
				var producer_kinds_for_rule: Array = production_rule.get("producer_kinds", [String(production_rule.get("producer_kind", ""))])
				if producer_kinds_for_rule.has(kind):
					production.append(unit_type)
			structures[base_id + index + 1] = {
				"id": base_id + index + 1,
				"team": team,
				"kind": "structure",
				"structure_kind": kind,
				"name": kind.replace("_", " ").capitalize(),
				"position": position,
				"rally": Vector2(team_layout.get("rally", _fallback_rally_position(team))),
				"health": maximum_health,
				"maximum_health": maximum_health,
				"construction_progress": 1.0,
				"level": 1,
				"completed_upgrades": [],
				"upgrade_queue": [],
				"production": production,
				"queue": [],
				"damage_remainders": {},
				"income_per_payout": int(_rules.get("farm_income", 25)) if kind == "farm" else 0,
			}
	_seed_all_expansion_pads()


func _team_center(team: int) -> Vector2:
	## Each roster team's spawn anchor. Teams 0/1 keep their exact historical
	## centers derived from spawn ids 1/2 and 101/102; teams >=2 read their own
	## anchor injected by the map layer (Player_N_Start), falling back to the map
	## centroid so a base still seeds when a start was not supplied.
	if team == PLAYER_TEAM:
		return (Vector2(_spawn_positions[1]) + Vector2(_spawn_positions[2])) * 0.5
	if team == ENEMY_TEAM:
		return (Vector2(_spawn_positions[101]) + Vector2(_spawn_positions[102])) * 0.5
	if _extra_team_centers.has(team):
		return Vector2(_extra_team_centers[team])
	return _two_team_map_center()


func _two_team_map_center() -> Vector2:
	return ((Vector2(_spawn_positions[1]) + Vector2(_spawn_positions[2])) * 0.5 + (Vector2(_spawn_positions[101]) + Vector2(_spawn_positions[102])) * 0.5) * 0.5


func _map_centroid() -> Vector2:
	## Average of every rostered team's spawn anchor. For the {0,1} default this is
	## exactly the midpoint of the two team centers the old code used, so the
	## derived outward/rally directions are unchanged.
	var teams := _roster_team_ids()
	if teams.size() <= 2:
		return _two_team_map_center()
	var sum := Vector2.ZERO
	for team in teams:
		sum += _team_center(int(team))
	return sum / float(teams.size())


func _derive_home_layout() -> Dictionary:
	var map_center := _map_centroid()
	var result: Dictionary = {}
	for team in _roster_team_ids():
		var anchor := _team_center(int(team))
		var outward := anchor.direction_to(map_center) * -1.0
		if outward.length_squared() < 0.01:
			outward = Vector2.LEFT if team == PLAYER_TEAM else Vector2.RIGHT
		var side := Vector2(-outward.y, outward.x)
		result[team] = {
			"fortress": anchor + outward * 10.0,
			"farm": anchor + side * 11.0 + outward * 2.0,
			"barracks": anchor - side * 11.0 + outward * 1.0,
			"archery_range": anchor + side * 18.0 - outward * 3.0,
			"stable": anchor - side * 18.0 - outward * 3.0,
			"rally": anchor - outward * 8.0,
		}
	return result


func _fallback_structure_position(team: int, index: int) -> Vector2:
	var anchor := _team_center(team)
	# Structures tile away from the map centroid. Teams 0/1 keep their historical
	# left/right sign (player centroid-outward points -x on the two-corner maps,
	# enemy +x); teams >=2 derive the sign from their own outward direction.
	var sign_value := -1.0 if team == PLAYER_TEAM else 1.0
	if team != PLAYER_TEAM and team != ENEMY_TEAM:
		var outward := anchor.direction_to(_map_centroid()) * -1.0
		sign_value = signf(outward.x) if not is_zero_approx(outward.x) else 1.0
	if index < 5:
		return anchor + Vector2(sign_value * (8.0 + float(index) * 2.5), (float(index) - 2.0) * 7.0)
	# Factions whose manifests declare more base structures than the historical
	# Men five tile the overflow in bounded extra rows beside the original
	# layout, so every seeded structure stays near its team's anchor.
	var sub := index - 5
	var row := sub / 5
	var col := sub % 5
	return anchor + Vector2(sign_value * (20.5 + float(row) * 5.0 + float(col) * 2.5), (float(col) - 2.0) * 7.0)


func _fallback_rally_position(team: int) -> Vector2:
	# Fixture sims configured without a map (no player starts) must not crash
	# the AI muster path; real matches always carry spawn positions.
	if _spawn_positions.is_empty():
		return Vector2.ZERO
	return _team_center(team)


func _builder_spawn_position(team: int) -> Vector2:
	var layout := _home_layout if not _home_layout.is_empty() else _derive_home_layout()
	var team_layout: Dictionary = layout.get(team, layout.get(str(team), {}))
	return Vector2(team_layout.get("rally", _fallback_rally_position(team)))


func _spawn_anchor_position(anchor: String, team: int = PLAYER_TEAM) -> Vector2:
	## Named map anchors keep the faction roster data-driven while spawn
	## geometry stays derived from the cooked source map's player starts. Teams
	## beyond 0/1 resolve the generalized anchors around their own spawn center.
	if team != PLAYER_TEAM and team != ENEMY_TEAM:
		if anchor.ends_with("builder"):
			return _builder_spawn_position(team) if base_loop_enabled else _team_center(team) + Vector2(0.0, -4.0)
		return _team_center(team)
	match anchor:
		"player_spawn_primary":
			return Vector2(_spawn_positions[1])
		"player_spawn_secondary":
			return Vector2(_spawn_positions[2])
		"enemy_spawn_primary":
			return Vector2(_spawn_positions[101])
		"enemy_spawn_secondary":
			return Vector2(_spawn_positions[102])
		"enemy_reserve":
			return (Vector2(_spawn_positions[101]) + Vector2(_spawn_positions[102])) * 0.5
		"player_builder":
			return Vector2(_spawn_positions[1]) + Vector2(0.0, 4.0)
		"enemy_builder":
			return _builder_spawn_position(ENEMY_TEAM) if base_loop_enabled else Vector2(_spawn_positions[101]) + Vector2(0.0, -4.0)
	return Vector2(_spawn_positions[1])


func _add_battalion(
	id: int,
	team: int,
	at: Vector2,
	display_name: String,
	object_id: String = SOLDIER_OBJECT_ID,
	unit_type: String = SOLDIER_HORDE_ID,
	command_points: int = -1
) -> void:
	var unit_rules_value: Variant = _rules.get("unit_rules", {})
	var unit_rule: Dictionary = (
		(unit_rules_value as Dictionary).get(object_id, {}) as Dictionary
		if typeof(unit_rules_value) == TYPE_DICTIONARY
		else {}
	)
	if unit_rule.is_empty():
		push_error("RetailSliceSim missing selected-pack unit rule for %s" % object_id)
		return
	var member_health := maxi(1, int(unit_rule.get("member_health", _rules.get("member_health", 200))))
	var member_count := maxi(1, int(unit_rule.get("member_count", 0)))
	var maximum_health := member_health * member_count
	var member_health_values: Array[int] = []
	var member_attack_tokens: Array[int] = []
	var member_attack_start_ticks: Array[int] = []
	var member_attack_hit_ticks: Array[int] = []
	var member_attack_release_tokens: Array[int] = []
	var member_corpse_expire_ticks: Array[int] = []
	var member_target_indices: Array[int] = []
	var member_weapon_modes: Array[String] = []
	for _member_index in range(member_count):
		member_health_values.append(member_health)
		member_attack_tokens.append(0)
		member_attack_start_ticks.append(-1)
		member_attack_hit_ticks.append(-1)
		member_attack_release_tokens.append(0)
		member_corpse_expire_ticks.append(-1)
		member_target_indices.append(-1)
		member_weapon_modes.append(String(unit_rule.get("default_weapon_mode", "default")))
	var member_damage := maxi(1, int(unit_rule.get("member_damage", 0)))
	var fallback_weapon := {
		"name": "legacy-default",
		"attack_range": float(unit_rule["attack_range"]),
		"attack_range_source": float(unit_rule["attack_range_source"]),
		"minimum_attack_range": float(unit_rule["minimum_attack_range"]),
		"minimum_attack_range_source": float(unit_rule["minimum_attack_range_source"]),
		"delay_between_shots_ms": float(unit_rule["delay_between_shots_ms"]),
		"pre_attack_delay_ms": float(unit_rule["pre_attack_delay_ms"]),
		"firing_duration_ms": float(unit_rule["firing_duration_ms"]),
		"attack_period_ticks": maxi(1, int(unit_rule["attack_period_ticks"])),
		"pre_attack_ticks": maxi(0, int(unit_rule["pre_attack_ticks"])),
		"firing_duration_ticks": maxi(0, int(unit_rule["firing_duration_ticks"])),
		"member_damage": member_damage,
	}
	var weapon_modes: Dictionary = (unit_rule.get("weapon_modes", {}) as Dictionary).duplicate(true)
	if weapon_modes.is_empty():
		weapon_modes["default"] = fallback_weapon
	var battalion_damage := member_damage * member_count
	var committed_command_points := command_points
	if committed_command_points < 0:
		committed_command_points = _production_rule_value(unit_type, "command_points_rule", "default_command_points")
	if String(unit_rule.get("category", "")) == "hero":
		_has_hero_units = true
	entities[id] = {
		"id": id,
		"team": team,
		"name": display_name,
		"object_id": object_id,
		"position": at,
		"facing": Vector2.RIGHT if team == PLAYER_TEAM else Vector2.LEFT,
		"destination": at,
		"route": [],
		"route_cells": [],
		"route_ford": "",
		"order_sequence": 0,
		"state": "idle",
		"target_id": 0,
		"target_kind": "battalion",
		"health": maximum_health,
		"maximum_health": maximum_health,
		"member_maximum_health": member_health,
		"member_health": member_health_values,
		"damage": battalion_damage,
		"member_damage": member_damage,
		"speed": float(unit_rule["speed"]),
		"speed_source": float(unit_rule["speed_source"]),
		"current_speed": 0.0,
		"acceleration": float(unit_rule["acceleration"]),
		"acceleration_source": float(unit_rule["acceleration_source"]),
		"turn_rate_degrees_per_second": float(unit_rule["turn_rate_degrees_per_second"]),
		"braking": float(unit_rule["braking"]),
		"braking_source": float(unit_rule["braking_source"]),
		"category": String(unit_rule.get("category", "")),
		"trample_cooldown": 0,
		# Flyers ignore ground navigation and cannot be hit by melee or bowled
		# over; knockdown_ticks > 0 means sprawled on the ground (no acting,
		# no orders) until the counter drains. Plain dict entries so both
		# serialize through snapshot()/state_hash() automatically.
		"flying": bool(unit_rule.get("is_flyer", false)),
		"knockdown_ticks": 0,
		"knocked_down": false,
		"attack_range": float(unit_rule["attack_range"]),
		"attack_range_source": float(unit_rule["attack_range_source"]),
		"minimum_attack_range": float(unit_rule["minimum_attack_range"]),
		"minimum_attack_range_source": float(unit_rule["minimum_attack_range_source"]),
		"vision_range": float(unit_rule["vision_range"]),
		"vision_range_source": float(unit_rule["vision_range_source"]),
		"damage_type": _recorded_damage_type(object_id, unit_rule),
		"delay_between_shots_ms": float(unit_rule["delay_between_shots_ms"]),
		"pre_attack_delay_ms": float(unit_rule["pre_attack_delay_ms"]),
		"firing_duration_ms": float(unit_rule["firing_duration_ms"]),
		"attack_period_ticks": maxi(1, int(unit_rule["attack_period_ticks"])),
		"pre_attack_ticks": maxi(0, int(unit_rule["pre_attack_ticks"])),
		"firing_duration_ticks": maxi(0, int(unit_rule["firing_duration_ticks"])),
		"attack_cooldown": 0,
		"attack_windup": 0,
		"attack_sequence": 0,
		"continuous_fire_count": 0,
		"continuous_fire_expiration_tick": -1,
		"member_attack_tokens": member_attack_tokens,
		"member_attack_start_ticks": member_attack_start_ticks,
		"member_attack_hit_ticks": member_attack_hit_ticks,
		"member_attack_release_tokens": member_attack_release_tokens,
		"member_corpse_expire_ticks": member_corpse_expire_ticks,
		"corpse_expire_tick": -1,
		"member_target_indices": member_target_indices,
		"member_weapon_modes": member_weapon_modes,
		"weapon_modes": weapon_modes,
		"default_weapon_mode": String(unit_rule.get("default_weapon_mode", "default")),
		"close_weapon_mode": String(unit_rule.get("close_weapon_mode", "")),
		"close_weapon_switch_distance": float(unit_rule.get("close_weapon_switch_distance", 0.0)),
		"close_weapon_switch_distance_source": float(unit_rule.get("close_weapon_switch_distance_source", 0.0)),
		"unsupported_close_weapon": bool(unit_rule.get("unsupported_close_weapon", false)),
		"clip_size": int(unit_rule.get("clip_size", 0)),
		"clip_reload_time_ms": float(unit_rule.get("clip_reload_time_ms", 0.0)),
		"continuous_fire_one": int(unit_rule.get("continuous_fire_one", 0)),
		"continuous_fire_coast_ticks": int(unit_rule.get("continuous_fire_coast_ticks", 0)),
		"continuous_fire_rate_multiplier": float(unit_rule.get("continuous_fire_rate_multiplier", 1.0)),
		"active_weapon_mode": String(unit_rule.get("default_weapon_mode", "default")),
		"stance": String((unit_rule.get("stances", {}) as Dictionary).get("default", "Battle")),
		"stance_contract": (unit_rule.get("stances", {}) as Dictionary).duplicate(true),
		"formation_mode": "Line",
		"formation_positions_base": Array(unit_rule["formation_positions"]).duplicate(),
		"order_kind": "",
		"is_builder": bool(unit_rule.get("is_builder", false)),
		"last_damage_tick": -1000000,
		"construction_id": 0,
		"member_count": member_count,
		"horde_id": String(unit_rule["horde_id"]),
		"formation_positions": Array(unit_rule["formation_positions"]).duplicate(),
		"retail_rule_provenance": (unit_rule["provenance"] as Dictionary).duplicate(true),
		"unit_type": unit_type,
		"command_points": committed_command_points,
		"production_producer_id": 0,
		"production_exit_start_tick": -1,
		"production_exit_duration_ticks": 0,
		"production_exit_progress": 1.0,
		"production_exit_origin": at,
		"production_exit_destination": at,
		"production_rally": at,
		# Compiled forge equipment recorded per horde (retail applies it per
		# battalion); spawned hordes of a tech-owning team arrive equipped.
		"applied_upgrades": {},
		"active_armor_upgrade": "",
		# Shared timed-modifier table: every buff/debuff/aura source (timed
		# ability buffs, leadership auras, fear) writes a keyed entry
		# {modifiers, expires_tick}; damage/attack/speed/vision/experience
		# calculations consult one helper family over this table. Keying by
		# source name gives retail stacking for free: same-named grants
		# overwrite (no stack), different names stack. Plain dict entries so
		# it serializes through snapshot()/state_hash() automatically.
		"timed_modifiers": {},
		# TOGGLE_WEAPONSET state: non-empty pins combat to that compiled
		# weapon-mode profile until toggled back (persistent across snapshots).
		"weapon_toggle_mode": "",
		# SpecialAbilityToggleMounted state: true while riding (mounted speed
		# and, when authored, the "mounted" weapon-mode profile are live).
		"mounted": false,
		# Honored only when the compiled unit rule authors it (fear-resistance
		# extraction is an importer follow-up; absent means not resistant).
		"fear_resistant": bool(unit_rule.get("fear_resistant", false)),
	}
	for tech_id_value in (team_upgrades.get(team, {}) as Dictionary).keys():
		_apply_equipment_to_horde(entities[id], _equipment_ids_for_forge_upgrade(String(tech_id_value)))
	if String(unit_rule.get("category", "")) == "hero":
		_attach_hero_ability_state(entities[id])
	_attach_experience_state(entities[id])


func _recorded_damage_type(object_id: String, unit_rule: Dictionary) -> String:
	## No silent slash default: a combat unit without authored damageType is
	## recorded, and its structure damage falls to the kind's DEFAULT scalar.
	var damage_type := String((_unit_damage_types if not _unit_damage_types.is_empty() else UNIT_DAMAGE_TYPES).get(object_id, ""))
	if damage_type == "" and not bool(unit_rule.get("is_builder", false)) and not missing_damage_type_units.has(object_id):
		missing_damage_type_units.append(object_id)
		print("[RetailSliceSim] unit '%s' has no authored damageType; its structure damage uses each kind's DEFAULT armor scalar" % object_id)
	return damage_type


func entity(id: int) -> Dictionary:
	return entities.get(id, {})


func entity_ids() -> Array[int]:
	var result: Array[int] = []
	for value in entities.keys():
		result.append(int(value))
	result.sort()
	return result


func living_ids(team: int) -> Array[int]:
	var result: Array[int] = []
	for id in entity_ids():
		var row: Dictionary = entities[id]
		if int(row["team"]) == team and int(row["health"]) > 0:
			result.append(id)
	return result


func structure(id: int) -> Dictionary:
	return structures.get(id, {})


func structure_ids(team: int = -1) -> Array[int]:
	var result: Array[int] = []
	for value in structures.keys():
		var id := int(value)
		if team < 0 or int((structures[id] as Dictionary).get("team", -1)) == team:
			result.append(id)
	result.sort()
	return result


func living_structure_ids(team: int) -> Array[int]:
	var result: Array[int] = []
	for id in structure_ids(team):
		if int((structures[id] as Dictionary).get("health", 0)) > 0:
			result.append(id)
	return result


func fortress_id(team: int) -> int:
	for id in structure_ids(team):
		if String((structures[id] as Dictionary).get("structure_kind", "")) == "fortress":
			return id
	return 0


func producer_id(team: int, kind: String = "barracks") -> int:
	for id in structure_ids(team):
		var row: Dictionary = structures[id]
		if String(row.get("structure_kind", "")) == kind and int(row.get("health", 0)) > 0:
			return id
	return 0


func resources_for_team(team: int) -> int:
	return int(team_resources.get(team, 0))


func command_points_for_team(team: int) -> int:
	return int(team_command_points.get(team, 0))


func _production_rule_value(unit_type: String, rule_key: String, default_key: String) -> int:
	var production_rule: Dictionary = _unit_production_rules.get(unit_type, {})
	if production_rule.is_empty():
		return 0
	var gameplay_rule := String(production_rule.get(rule_key, ""))
	return int(_rules.get(gameplay_rule, int(production_rule.get(default_key, 0))))


func production_rule_ids() -> Array[String]:
	return _production_unit_order.duplicate()


func production_rule_display_name(unit_type: String) -> String:
	return String((_unit_production_rules.get(unit_type, {}) as Dictionary).get("display_name", ""))


func production_rule_category(unit_type: String) -> String:
	return String((_unit_production_rules.get(unit_type, {}) as Dictionary).get("category", ""))


func structure_build_rule_ids() -> Array[String]:
	var result: Array[String] = []
	for value in _structure_build_rules.keys():
		result.append(String(value))
	return result


func structure_build_rule(kind: String) -> Dictionary:
	return (_structure_build_rules.get(kind, {}) as Dictionary).duplicate(true)


func structure_maximum_health(kind: String) -> int:
	return int(_structure_max_health.get(kind, 0))


func required_upgrade_for_unit(unit_type: String, producer_kind: String = "") -> String:
	var required := required_upgrades_for_unit(unit_type, producer_kind)
	return String(required[0]) if not required.is_empty() else ""


func required_upgrades_for_unit(unit_type: String, producer_kind: String = "") -> Array:
	var value: Variant = _unit_prerequisites.get(unit_type, "")
	if typeof(value) == TYPE_DICTIONARY:
		return Array((value as Dictionary).get(producer_kind, [])).duplicate()
	return [String(value)] if String(value) != "" else []


# Retail armory tech tree: upgrade.ini:1555-1593 (Upgrade_GondorForgedBlades,
# Upgrade_GondorFireArrows, Upgrade_GondorHeavyArmor) research at the forge for
# GONDOR_TECH_*_BUILDCOST = 1000 and GONDOR_TECH_*_BUILDTIME = 30s
# (gamedata.ini:1281-1288). The slice's research conflates the retail PLAYER
# technology unlock with the per-battalion OBJECT purchase (300 each,
# gamedata.ini:1300-1306): completion auto-equips every matching horde,
# recorded per-horde in applied_upgrades. The per-battalion purchase command
# flow is a recorded gap, not an invented value. The damage/armor effects are
# the compiled WeaponSetUpgrade/ArmorUpgrade tables from each unit document —
# no hand-tuned multipliers remain.
const FORGE_UPGRADE_CONTRACTS := {
	"Upgrade_GondorForgedBlades": {
		"structure_kind": "forge", "cost": 1000, "duration_seconds": 30.0,
		"level_cap": 99, "levels_to_gain": 0, "cancelable": false, "team_tech": true,
	},
	"Upgrade_GondorFireArrows": {
		"structure_kind": "forge", "cost": 1000, "duration_seconds": 30.0,
		"level_cap": 99, "levels_to_gain": 0, "cancelable": false, "team_tech": true,
	},
	"Upgrade_GondorHeavyArmor": {
		"structure_kind": "forge", "cost": 1000, "duration_seconds": 30.0,
		"level_cap": 99, "levels_to_gain": 0, "cancelable": false, "team_tech": true,
	},
}
# commandbutton.ini:9549-9563 — ranger hordes purchase Upgrade_GondorFireArrows
# while archer hordes purchase Upgrade_GondorArcherFireArrows; both buttons
# share the same NeededUpgrade technology, so one research equips both.
const FORGE_UPGRADE_EQUIPMENT := {
	"Upgrade_GondorFireArrows": ["Upgrade_GondorFireArrows", "Upgrade_GondorArcherFireArrows"],
}
var team_upgrades: Dictionary = {}


func _register_forge_upgrade_contracts() -> void:
	if not _structure_build_rules.has("forge"):
		return
	if _compiled_research_kinds.has("forge"):
		# The compiled research surface (the forge's authored PLAYER technology
		# sales) replaces the recorded provisional contracts below; research
		# completion then grants the technology and the per-battalion purchase
		# path equips it — the retail two-tier flow, not the conflation.
		return
	for upgrade_id_value in FORGE_UPGRADE_CONTRACTS.keys():
		var upgrade_id := String(upgrade_id_value)
		if _structure_upgrade_contracts.has(upgrade_id):
			continue
		var source: Dictionary = FORGE_UPGRADE_CONTRACTS[upgrade_id]
		_structure_upgrade_contracts[upgrade_id] = {
			"structure_kind": String(source["structure_kind"]),
			"cost": int(source["cost"]),
			"duration_ticks": maxi(1, roundi(float(source["duration_seconds"]) / TICK_SECONDS)),
			"level_cap": int(source["level_cap"]),
			"levels_to_gain": int(source["levels_to_gain"]),
			"cancelable": bool(source["cancelable"]),
			"to_command_set": "",
			"team_tech": true,
			# Recorded provisional (stale pack without the compiled research
			# surface): completion still auto-equips matching hordes.
			"legacy_provisional": true,
		}


func _equipment_ids_for_forge_upgrade(upgrade_id: String) -> Array:
	## The OBJECT upgrade ids a completed research equips per horde (defaults
	## to the research id itself; fire arrows map to both authored buttons).
	return Array(FORGE_UPGRADE_EQUIPMENT.get(upgrade_id, [upgrade_id]))


func _sorted_upgrade_ids(applied: Variant) -> Array[String]:
	## Deterministic snapshot view of a horde's recorded equipment upgrades.
	var ids: Array[String] = []
	if typeof(applied) != TYPE_DICTIONARY:
		return ids
	for key_value in (applied as Dictionary).keys():
		ids.append(String(key_value))
	ids.sort()
	return ids


func _apply_equipment_to_horde(row: Dictionary, equipment: Array) -> void:
	## Record the compiled armor/weapon upgrade effects a horde carries. Retail
	## applies equipment per battalion; the slice records it per horde row.
	var object_id := String(row.get("object_id", ""))
	var armor_upgrades: Dictionary = (_unit_armor.get(object_id, {}) as Dictionary).get("upgrades", {})
	var weapon_upgrades: Dictionary = _unit_weapon_upgrades.get(object_id, {})
	var applied: Dictionary = row.get("applied_upgrades", {})
	var changed := false
	for upgrade_id_value in equipment:
		var upgrade_id := String(upgrade_id_value)
		if applied.has(upgrade_id):
			continue
		if armor_upgrades.has(upgrade_id):
			applied[upgrade_id] = tick_index
			# Retail ArmorUpgrade swaps the ArmorSet; the last applied swap wins.
			row["active_armor_upgrade"] = upgrade_id
			changed = true
		elif weapon_upgrades.has(upgrade_id):
			applied[upgrade_id] = tick_index
			changed = true
	if changed:
		row["applied_upgrades"] = applied
		_emit_event("battalion.upgrade_applied", 0, int(row.get("id", 0)), {"team": int(row.get("team", -1)), "upgrades": applied.keys()})


func _apply_team_upgrade_to_hordes(team: int, upgrade_id: String) -> void:
	var equipment := _equipment_ids_for_forge_upgrade(upgrade_id)
	for id in entity_ids():
		var row: Dictionary = entities[id]
		if int(row.get("team", -1)) != team:
			continue
		_apply_equipment_to_horde(row, equipment)


func queue_structure_upgrade(team: int, structure_id: int, upgrade_id: String) -> Dictionary:
	if not base_loop_enabled or winner != -1:
		return {"ok": false, "reason": "match-unavailable"}
	if not structures.has(structure_id):
		return {"ok": false, "reason": "unknown-structure"}
	var building: Dictionary = structures[structure_id]
	if int(building.get("team", -1)) != team:
		return {"ok": false, "reason": "wrong-owner"}
	if int(building.get("health", 0)) <= 0 or float(building.get("construction_progress", 0.0)) < 1.0:
		return {"ok": false, "reason": "structure-unavailable"}
	var contract: Dictionary = _structure_upgrade_contracts.get(upgrade_id, {})
	if contract.is_empty() or String(contract.get("structure_kind", "")) != String(building.get("structure_kind", "")):
		return {"ok": false, "reason": "unsupported-upgrade"}
	if Array(building.get("completed_upgrades", [])).has(upgrade_id):
		return {"ok": false, "reason": "already-completed"}
	if bool(contract.get("team_tech", false)) and (team_upgrades.get(team, {}) as Dictionary).has(upgrade_id):
		# Team techs are owned once, no matter which forge researched them.
		return {"ok": false, "reason": "already-completed"}
	var queue: Array = building.get("upgrade_queue", [])
	if not queue.is_empty():
		return {"ok": false, "reason": "upgrade-in-progress"}
	if int(building.get("level", 1)) >= int(contract.get("level_cap", 1)):
		return {"ok": false, "reason": "level-cap"}
	var required_prior := String(contract.get("requires_upgrade_id", ""))
	if required_prior != "" and not Array(building.get("completed_upgrades", [])).has(required_prior):
		# The authored chain sells each step on the command set the prior step
		# unlocks; L3 can never be purchased before L2.
		return {"ok": false, "reason": "missing-prior-upgrade", "required_upgrade": required_prior}
	var missing_gate := _research_gate_unsatisfied(team, building, contract)
	if missing_gate != "":
		# The button's authored NeededUpgrade row (a team technology or a
		# structure level) gates the research; retail shows it greyed.
		return {"ok": false, "reason": "missing-upgrade", "required_upgrade": missing_gate}
	var cost := maxi(0, int(contract.get("cost", 0)))
	if resources_for_team(team) < cost:
		return {"ok": false, "reason": "insufficient-resources", "cost": cost}
	var duration_ticks := maxi(1, int(contract.get("duration_ticks", 1)))
	var item := {
		"upgrade_id": upgrade_id,
		"cost": cost,
		"queued_tick": tick_index,
		"duration_ticks": duration_ticks,
		"complete_tick": tick_index + duration_ticks,
		"cancelable": bool(contract.get("cancelable", false)),
	}
	queue.append(item)
	building["upgrade_queue"] = queue
	team_resources[team] = resources_for_team(team) - cost
	_emit_event("upgrade.queued", structure_id, 0, {"team": team, "upgrade_id": upgrade_id, "complete_tick": int(item["complete_tick"])})
	return {"ok": true, "reason": "", "structure_id": structure_id, "item": item.duplicate(true)}


func structure_upgrade_queue_state(structure_id: int) -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	if not structures.has(structure_id):
		return result
	for item_value in Array((structures[structure_id] as Dictionary).get("upgrade_queue", [])):
		if typeof(item_value) != TYPE_DICTIONARY:
			continue
		var item := item_value as Dictionary
		var duration_ticks := maxi(1, int(item.get("duration_ticks", 1)))
		var elapsed_ticks := clampi(tick_index - int(item.get("queued_tick", tick_index)), 0, duration_ticks)
		var row := item.duplicate(true)
		row["elapsed_ticks"] = elapsed_ticks
		row["progress"] = float(elapsed_ticks) / float(duration_ticks)
		result.append(row)
	return result


func structure_upgrade_commands(structure_id: int) -> Array[Dictionary]:
	## The building's purchasable upgrade steps on its CURRENT command set,
	## doc-driven: the chain step whose authored from-command-set matches the
	## building's live set (its base set before any purchase). One entry per
	## pending step; completed/capped chains surface nothing.
	var result: Array[Dictionary] = []
	if not structures.has(structure_id):
		return result
	var building: Dictionary = structures[structure_id]
	var kind := String(building.get("structure_kind", ""))
	var current_set := String(building.get("command_set", ""))
	var completed: Array = building.get("completed_upgrades", [])
	var upgrade_ids: Array[String] = []
	for upgrade_id_value in _structure_upgrade_contracts.keys():
		upgrade_ids.append(String(upgrade_id_value))
	upgrade_ids.sort()
	for upgrade_id in upgrade_ids:
		var contract: Dictionary = _structure_upgrade_contracts[upgrade_id]
		if String(contract.get("structure_kind", "")) != kind:
			continue
		if bool(contract.get("team_tech", false)):
			if not bool(contract.get("research", false)):
				# Legacy team techs ride their own surface; compiled research
				# rows surface here exactly like chain steps.
				continue
			if (team_upgrades.get(int(building.get("team", -1)), {}) as Dictionary).has(upgrade_id):
				continue
			# Research rides every per-level command set of the building, so no
			# from-set match applies; the authored NeededUpgrade gate is data.
			var row := {
				"upgrade_id": upgrade_id,
				"command_id": String(contract.get("command_id", "")),
				"cost": int(contract.get("cost", 0)),
				"duration_ticks": int(contract.get("duration_ticks", 1)),
				"to_level": 0,
				"cancelable": bool(contract.get("cancelable", false)),
				"slot": int(contract.get("slot", 0)),
				"label_id": String(contract.get("label_id", "")),
				"tooltip_id": String(contract.get("tooltip_id", "")),
				"image_id": String(contract.get("image_id", "")),
				"research": true,
				"lacks_prerequisite_label_id": String(contract.get("lacks_prerequisite_label_id", "")),
				"needed_upgrade_ids": Array(contract.get("needed_upgrade_ids", [])).duplicate(),
				"gate_satisfied": _research_gate_unsatisfied(int(building.get("team", -1)), building, contract) == "",
			}
			result.append(row)
			continue
		if completed.has(upgrade_id):
			continue
		var from_set := String(contract.get("from_command_set", ""))
		if current_set == "":
			# Before any purchase the building sits on its base command set: the
			# chain step whose from-set is not itself another step's to-set.
			var is_downstream := false
			for other_id in upgrade_ids:
				var other: Dictionary = _structure_upgrade_contracts[other_id]
				if String(other.get("structure_kind", "")) == kind and String(other.get("to_command_set", "")) == from_set:
					is_downstream = true
					break
			if is_downstream:
				continue
		elif from_set != current_set:
			continue
		result.append({
			"upgrade_id": upgrade_id,
			"command_id": String(contract.get("command_id", "")),
			"cost": int(contract.get("cost", 0)),
			"duration_ticks": int(contract.get("duration_ticks", 1)),
			"to_level": int(contract.get("to_level", 0)),
			"cancelable": bool(contract.get("cancelable", false)),
			"slot": int(contract.get("slot", 0)),
			"label_id": String(contract.get("label_id", "")),
			"tooltip_id": String(contract.get("tooltip_id", "")),
			"image_id": String(contract.get("image_id", "")),
		})
	return result


## --- Per-battalion OBJECT upgrade purchases ---
## Retail's two-tier armory: a PLAYER technology researched at a building
## unlocks each horde's authored OBJECT_UPGRADE buttons. Eligibility is the
## unit document's compiled command set cross-checked against its compiled
## weapon/armor/level effect tables — never a class-name split. The purchase
## costs the authored amount (less any authored discount the team's
## structures declare) and applies to that one battalion.


func _sorted_unit_upgrade_commands(unit_type: String) -> Array:
	## Deterministic surface order: authored slot first, then upgrade id.
	var rows: Array = Array(_unit_upgrade_commands.get(unit_type, [])).duplicate(true)
	rows.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		if int(a.get("slot", 0)) != int(b.get("slot", 0)):
			return int(a.get("slot", 0)) < int(b.get("slot", 0))
		return String(a.get("upgrade_id", "")).naturalnocasecmp_to(String(b.get("upgrade_id", ""))) < 0
	)
	return rows


func _battalion_gate_unsatisfied(team: int, command: Dictionary) -> String:
	## "" when the purchase button's NeededUpgrade technology row is owned.
	var needed: Array = command.get("needed_upgrade_ids", [])
	if needed.is_empty():
		return ""
	var owned: Dictionary = team_upgrades.get(team, {}) as Dictionary
	var satisfied := 0
	var first_missing := ""
	for needed_value in needed:
		var needed_id := String(needed_value)
		if owned.has(needed_id):
			satisfied += 1
		elif first_missing == "":
			first_missing = needed_id
	if bool(command.get("needed_upgrade_any", false)):
		return "" if satisfied > 0 else (first_missing if first_missing != "" else String(needed[0]))
	return "" if satisfied == needed.size() else first_missing


func _team_has_required_object(team: int, requirement: String) -> bool:
	## Authored BuildingRequired/UpgradeMustBePresent filters ("ANY +Object"):
	## the team must field a living structure of any named object kind.
	var tokens := requirement.split(" ", false)
	var names: Array[String] = []
	for token in tokens:
		if token == "ANY" or token == "NONE":
			continue
		names.append(String(token).trim_prefix("+"))
	if names.is_empty():
		return requirement.strip_edges() == ""
	var registry: Dictionary = _rules.get("producer_kind_registry", {}) as Dictionary
	if registry.is_empty():
		registry = (_rules.get("faction_manifest", {}) as Dictionary).get("producer_kind_registry", {}) as Dictionary
	for name in names:
		var kind := ""
		for object_id_value in registry.keys():
			if String(object_id_value).to_lower() == name.to_lower():
				kind = String(registry[object_id_value])
				break
		if kind == "":
			continue
		for structure_id in structure_ids(team):
			var building: Dictionary = structures[structure_id]
			if String(building.get("structure_kind", "")) == kind and int(building.get("health", 0)) > 0:
				return true
	return false


func _discounted_battalion_upgrade_cost(team: int, command: Dictionary) -> int:
	## The authored CostModifierUpgrade discount (Iron Ore) applies while the
	## team owns the technology and fields the structure declaring it.
	var cost := int(command.get("cost", 0))
	var upgrade_id := String(command.get("upgrade_id", ""))
	var owned: Dictionary = team_upgrades.get(team, {}) as Dictionary
	for structure_id in structure_ids(team):
		var building: Dictionary = structures[structure_id]
		if int(building.get("health", 0)) <= 0:
			continue
		var bundle: Dictionary = _structure_upgrade_effects.get(String(building.get("structure_kind", "")), {})
		for effect_value in Array(bundle.get("effects", [])):
			var effect := effect_value as Dictionary
			if String(effect.get("kind", "")) != "upgrade-discount" or not bool(effect.get("upgrade_discount", false)):
				continue
			if not owned.has(String(effect.get("upgrade_id", ""))):
				continue
			var covered := false
			for covered_value in Array(effect.get("apply_to_upgrade_ids", [])):
				if String(covered_value) == upgrade_id:
					covered = true
					break
			if covered:
				cost = roundi(cost * (100.0 + float(effect.get("percent", 0.0))) / 100.0)
	return maxi(0, cost)


func battalion_upgrade_commands(entity_id: int) -> Array[Dictionary]:
	## The battalion's authored purchase surface with live gate/applied/cost
	## state; the same command-surface data the building research rows ride.
	var result: Array[Dictionary] = []
	if not entities.has(entity_id):
		return result
	var row: Dictionary = entities[entity_id]
	var team := int(row.get("team", -1))
	var applied: Dictionary = row.get("applied_upgrades", {})
	var queued: Array = row.get("upgrade_queue", [])
	for command_value in _sorted_unit_upgrade_commands(String(row.get("unit_type", ""))):
		var command := command_value as Dictionary
		var upgrade_id := String(command.get("upgrade_id", ""))
		var missing := _battalion_gate_unsatisfied(team, command)
		result.append({
			"upgrade_id": upgrade_id,
			"command_id": String(command.get("command_id", "")),
			"cost": _discounted_battalion_upgrade_cost(team, command),
			"base_cost": int(command.get("cost", 0)),
			"duration_ticks": int(command.get("duration_ticks", 1)),
			"slot": int(command.get("slot", 0)),
			"label_id": String(command.get("label_id", "")),
			"tooltip_id": String(command.get("tooltip_id", "")),
			"image_id": String(command.get("image_id", "")),
			"lacks_prerequisite_label_id": String(command.get("lacks_prerequisite_label_id", "")),
			"needed_upgrade_ids": Array(command.get("needed_upgrade_ids", [])).duplicate(),
			"cancelable": bool(command.get("cancelable", false)),
			"multi_select": bool(command.get("multi_select", false)),
			"research_owned": missing == "",
			"required_upgrade": missing,
			"applied": applied.has(upgrade_id),
			"queued": not queued.is_empty(),
		})
	return result


func queue_battalion_upgrade(team: int, entity_id: int, upgrade_id: String) -> Dictionary:
	if not base_loop_enabled or winner != -1:
		return {"ok": false, "reason": "match-unavailable"}
	if not entities.has(entity_id):
		return {"ok": false, "reason": "unknown-entity"}
	var row: Dictionary = entities[entity_id]
	if int(row.get("team", -1)) != team:
		return {"ok": false, "reason": "wrong-owner"}
	if int(row.get("health", 0)) <= 0:
		return {"ok": false, "reason": "entity-unavailable"}
	var command: Dictionary = {}
	for candidate_value in _sorted_unit_upgrade_commands(String(row.get("unit_type", ""))):
		if String((candidate_value as Dictionary).get("upgrade_id", "")) == upgrade_id:
			command = candidate_value
			break
	if command.is_empty():
		# Never authored for this unit: the compiled document offers no button.
		return {"ok": false, "reason": "unsupported-upgrade"}
	var applied: Dictionary = row.get("applied_upgrades", {})
	if applied.has(upgrade_id):
		return {"ok": false, "reason": "already-completed"}
	var queue: Array = row.get("upgrade_queue", [])
	if not queue.is_empty():
		return {"ok": false, "reason": "upgrade-in-progress"}
	var missing := _battalion_gate_unsatisfied(team, command)
	if missing != "":
		return {"ok": false, "reason": "missing-upgrade", "required_upgrade": missing}
	var cost := _discounted_battalion_upgrade_cost(team, command)
	if resources_for_team(team) < cost:
		return {"ok": false, "reason": "insufficient-resources", "cost": cost}
	var duration_ticks := maxi(1, int(command.get("duration_ticks", 1)))
	var item := {
		"upgrade_id": upgrade_id,
		"cost": cost,
		"queued_tick": tick_index,
		"duration_ticks": duration_ticks,
		"complete_tick": tick_index + duration_ticks,
		"cancelable": bool(command.get("cancelable", false)),
	}
	queue.append(item)
	row["upgrade_queue"] = queue
	team_resources[team] = resources_for_team(team) - cost
	_emit_event("battalion_upgrade.queued", 0, entity_id, {"team": team, "upgrade_id": upgrade_id, "complete_tick": int(item["complete_tick"])})
	return {"ok": true, "reason": "", "entity_id": entity_id, "item": item.duplicate(true)}


func battalion_upgrade_queue_state(entity_id: int) -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	if not entities.has(entity_id):
		return result
	for item_value in Array((entities[entity_id] as Dictionary).get("upgrade_queue", [])):
		if typeof(item_value) != TYPE_DICTIONARY:
			continue
		var item := item_value as Dictionary
		var duration_ticks := maxi(1, int(item.get("duration_ticks", 1)))
		var elapsed_ticks := clampi(tick_index - int(item.get("queued_tick", tick_index)), 0, duration_ticks)
		var row := item.duplicate(true)
		row["elapsed_ticks"] = elapsed_ticks
		row["progress"] = float(elapsed_ticks) / float(duration_ticks)
		result.append(row)
	return result


func _step_battalion_upgrades() -> void:
	for entity_id in entity_ids():
		var row: Dictionary = entities[entity_id]
		if int(row.get("health", 0)) <= 0:
			continue
		var queue: Array = row.get("upgrade_queue", [])
		if queue.is_empty():
			continue
		var item: Dictionary = queue[0]
		if tick_index < int(item.get("complete_tick", tick_index + 1)):
			continue
		queue.pop_front()
		row["upgrade_queue"] = queue
		var upgrade_id := String(item.get("upgrade_id", ""))
		var member_id := String(row.get("object_id", ""))
		var level_rule: Dictionary = (_unit_level_upgrades.get(member_id, {}) as Dictionary).get(upgrade_id, {})
		if not level_rule.is_empty():
			# Basic Training: the authored LevelUpUpgrade grants its level; the
			# banner-carrier member visual is the recorded unsupported remainder.
			var next_level := mini(
				int(level_rule.get("level_cap", 2)),
				int(row.get("level", 1)) + int(level_rule.get("levels_to_gain", 1))
			)
			row["level"] = next_level
			var applied: Dictionary = row.get("applied_upgrades", {})
			applied[upgrade_id] = tick_index
			row["applied_upgrades"] = applied
			_emit_event("battalion.upgrade_applied", 0, entity_id, {
				"team": int(row.get("team", -1)),
				"upgrades": [upgrade_id],
				"unsupported_effects": ["banner-carrier-member-spawn"],
			})
		else:
			_apply_equipment_to_horde(row, [upgrade_id])
		_emit_event("battalion_upgrade.completed", 0, entity_id, {
			"team": int(row.get("team", -1)),
			"upgrade_id": upgrade_id,
		})


func _apply_structure_death_refund(building: Dictionary) -> void:
	## Authored RefundDie rows (Siege Materials): the dying structure's own
	## document declares the refund while the team owns the technology and
	## maintains the required building.
	var team := int(building.get("team", -1))
	var kind := String(building.get("structure_kind", ""))
	var bundle: Dictionary = _structure_upgrade_effects.get(kind, {})
	var owned: Dictionary = team_upgrades.get(team, {}) as Dictionary
	for effect_value in Array(bundle.get("effects", [])):
		var effect := effect_value as Dictionary
		if String(effect.get("kind", "")) != "refund-on-death":
			continue
		var upgrade_id := String(effect.get("upgrade_id", ""))
		if not owned.has(upgrade_id):
			continue
		if not _team_has_required_object(team, String(effect.get("building_required", ""))):
			continue
		var build_rule: Dictionary = _structure_build_rules.get(kind, {})
		var refund := roundi(float(build_rule.get("cost", 0)) * float(effect.get("refund_percent", 0.0)) / 100.0)
		if refund <= 0:
			continue
		team_resources[team] = resources_for_team(team) + refund
		_emit_event("economy.refund", int(building.get("id", 0)), 0, {"team": team, "amount": refund, "upgrade_id": upgrade_id})


func _income_with_upgrade_bonus(team: int, building: Dictionary, base_income: int) -> int:
	## Authored TerrainResourceBehavior upgrade bonuses (Grand Harvest): the
	## resource structure's own document declares the percent while the team
	## owns the technology and maintains the required building.
	var bundle: Dictionary = _structure_upgrade_effects.get(String(building.get("structure_kind", "")), {})
	var owned: Dictionary = team_upgrades.get(team, {}) as Dictionary
	var income := base_income
	for effect_value in Array(bundle.get("effects", [])):
		var effect := effect_value as Dictionary
		if String(effect.get("kind", "")) != "income-bonus":
			continue
		if not owned.has(String(effect.get("upgrade_id", ""))):
			continue
		if not _team_has_required_object(team, String(effect.get("upgrade_must_be_present", ""))):
			continue
		income = roundi(income * float(effect.get("bonus_percent", 100.0)) / 100.0)
	return income


func _queued_command_points_for_team(team: int) -> int:
	var total := 0
	for structure_id in structure_ids(team):
		for item_value in Array((structures[structure_id] as Dictionary).get("queue", [])):
			if typeof(item_value) == TYPE_DICTIONARY:
				total += int((item_value as Dictionary).get("command_points", 0))
	return total


## --- Spellbook powers ---
## Tree, costs, OR prerequisite groups, authored palantir purchase slots,
## reload cooldowns, and cast bindings all derive from the selected pack's
## openbfme.spellbook-runtime document (menspellbook.json). A missing or
## malformed document fails closed: the tree stays empty and every purchase or
## cast rejects with "spellbook-unavailable". Powers whose converted effect
## leaves do not fully support a faithful runtime effect stay locked with the
## reason recorded on the power row — no invented effects.
##
## PROVISIONAL (recorded, not hidden): the retail power-point earn rate is not
## resolved from source data — the spellbook document carries no economy rule.
## The kill-based rate lives in rules key "power_point_kills" (default below)
## so the data lane can replace it without code edits; do not treat it as
## retail-final.
const POWER_POINT_KILLS := 3
const SPELLBOOK_SCHEMA := "openbfme.spellbook-runtime"
var team_power_points := {PLAYER_TEAM: 1, ENEMY_TEAM: 1}
var purchased_powers := {PLAYER_TEAM: [], ENEMY_TEAM: []}
var _kills_toward_power_point := {PLAYER_TEAM: 0, ENEMY_TEAM: 0}
var _spellbook_ready := false
var _spellbook_error := ""
var _spellbook_document: Dictionary = {}
var _spellbook_powers: Dictionary = {}
var _spellbook_order: Array[String] = []
var _spellbook_sciences: Dictionary = {}
var _spellbook_intrinsic: Array = []
var _science_to_power: Dictionary = {}
var _team_sciences := {PLAYER_TEAM: [], ENEMY_TEAM: []}
var _power_cooldown_until := {PLAYER_TEAM: {}, ENEMY_TEAM: {}}
## Picks made since the last ACCEPT: RESET refunds exactly these ("unspent
## picks" — casting a staged pick spends it and it can no longer be refunded).
var _staged_purchases := {PLAYER_TEAM: [], ENEMY_TEAM: []}
## Single-player pause seam for the spellbook orb: while the palantir is open
## the sim clock halts (ticks, production, AI — everything in tick()). The
## slice drives this through set_spellbook_orb_open; the escape-menu pause in
## the slice composes independently (either one halts the clock).
var clock_paused := false


func set_spellbook_orb_open(open: bool) -> void:
	clock_paused = open


func configure_spellbook_runtime(document: Dictionary) -> bool:
	_spellbook_ready = false
	_spellbook_error = ""
	_spellbook_document = document.duplicate(true)
	_spellbook_powers.clear()
	_spellbook_order.clear()
	_spellbook_sciences.clear()
	_spellbook_intrinsic.clear()
	_science_to_power.clear()
	if typeof(document) != TYPE_DICTIONARY or String(document.get("schema", "")) != SPELLBOOK_SCHEMA:
		_spellbook_error = "spellbook document is missing or not an %s" % SPELLBOOK_SCHEMA
		return false
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var power_tree: Dictionary = registration.get("powerTree", {}) as Dictionary
	var spell_book_object: Dictionary = registration.get("spellBook", {}) as Dictionary
	for intrinsic_value in Array(spell_book_object.get("intrinsicSciences", [])):
		if typeof(intrinsic_value) != TYPE_STRING or String(intrinsic_value).strip_edges() == "":
			_spellbook_error = "spellbook intrinsic sciences are malformed"
			return false
		_spellbook_intrinsic.append(String(intrinsic_value))
	# Sciences with an authored purchase block make up the palantir tree. Their
	# prerequisiteGroups are preserved OR groups: a science is purchasable when
	# ANY group is fully owned; an empty group list means no prerequisites.
	for science_value in Array(power_tree.get("sciences", [])):
		if typeof(science_value) != TYPE_DICTIONARY:
			_spellbook_error = "spellbook science entry is malformed"
			return false
		var science := science_value as Dictionary
		var science_id := String(science.get("id", ""))
		var purchase: Dictionary = science.get("purchase", {}) as Dictionary
		if science_id == "" or purchase.is_empty():
			continue
		var cost := int((science.get("pointCostMP", {}) as Dictionary).get("value", -1))
		var slot := int(purchase.get("slot", -1))
		if cost <= 0 or slot <= 0:
			_spellbook_error = "spellbook science '%s' has no resolved MP cost or purchase slot" % science_id
			return false
		var groups: Array = []
		var groups_well_formed := true
		for group_value in Array(science.get("prerequisiteGroups", [])):
			if typeof(group_value) != TYPE_ARRAY:
				groups_well_formed = false
				break
			var group: Array = []
			for member_value in group_value:
				if typeof(member_value) != TYPE_STRING or String(member_value).strip_edges() == "":
					groups_well_formed = false
					break
				group.append(String(member_value))
			if not groups_well_formed:
				break
			groups.append(group)
		if not groups_well_formed:
			_spellbook_error = "spellbook science '%s' prerequisite groups are malformed" % science_id
			return false
		_spellbook_sciences[science_id] = {"cost": cost, "slot": slot, "groups": groups}
	var leaves: Dictionary = registration.get("leaves", {}) as Dictionary
	var modifier_leaves: Dictionary = {}
	for modifier_value in Array(leaves.get("attributeModifiers", [])):
		if typeof(modifier_value) == TYPE_DICTIONARY:
			modifier_leaves[String((modifier_value as Dictionary).get("id", ""))] = modifier_value
	var object_leaves: Dictionary = {}
	for object_value in Array(leaves.get("objects", [])):
		if typeof(object_value) == TYPE_DICTIONARY:
			object_leaves[String((object_value as Dictionary).get("id", ""))] = object_value
	var ocl_leaves: Dictionary = {}
	for ocl_value in Array(leaves.get("objectCreationLists", [])):
		if typeof(ocl_value) == TYPE_DICTIONARY:
			ocl_leaves[String((ocl_value as Dictionary).get("id", ""))] = ocl_value
	var weapon_leaves: Dictionary = {}
	for weapon_value in Array(leaves.get("weapons", [])):
		if typeof(weapon_value) == TYPE_DICTIONARY:
			weapon_leaves[String((weapon_value as Dictionary).get("id", ""))] = weapon_value
	for power_value in Array(power_tree.get("powers", [])):
		if typeof(power_value) != TYPE_DICTIONARY:
			_spellbook_error = "spellbook power entry is malformed"
			return false
		var power := power_value as Dictionary
		var power_id := String(power.get("id", ""))
		if power_id == "":
			_spellbook_error = "spellbook power is missing its id"
			return false
		var cast: Dictionary = power.get("cast", {}) as Dictionary
		var effect_definition: Dictionary = power.get("effect", {}) as Dictionary
		var fields: Array = effect_definition.get("fields", []) as Array
		var references: Dictionary = effect_definition.get("references", {}) as Dictionary
		# The tree science is the required science carrying the purchase slot
		# (e.g. Rallying Call's requiredSciences pair collapses to the MP one).
		var science_id := ""
		for required_value in Array(power.get("requiredSciences", [])):
			var candidate := String(required_value)
			if _spellbook_sciences.has(candidate):
				science_id = candidate
				break
		var science_row: Dictionary = _spellbook_sciences.get(science_id, {}) as Dictionary
		var reload_ms := float((power.get("reloadTimeMs", {}) as Dictionary).get("value", 0.0))
		var row := {
			"id": power_id,
			"module": String(effect_definition.get("module", "")),
			"science_id": science_id,
			"cost": int(science_row.get("cost", 0)),
			"purchase_slot": int(science_row.get("slot", 0)),
			"cast_slot": int(cast.get("slot", 0)),
			"icon_ids": Array(cast.get("iconIds", [])),
			"needs_target_pos": Array(cast.get("options", [])).has("NEED_TARGET_POS"),
			"radius_cursor_source": float((power.get("radiusCursorRadius", {}) as Dictionary).get("value", 0.0)),
			"reload_ms": reload_ms,
			"reload_ticks": maxi(1, roundi(reload_ms / 1000.0 / TICK_SECONDS)),
			"sound_id": String(power.get("initiateSoundId", "")),
			"fx_lists": Array(references.get("fxLists", [])),
			"ocls": Array(references.get("objectCreationLists", [])),
		}
		if science_id == "":
			row["castable"] = false
			row["locked_reason"] = "power has no purchasable tree science in the document"
		elif reload_ms <= 0.0:
			row["castable"] = false
			row["locked_reason"] = "reloadTimeMs did not resolve to a positive value"
		else:
			var support := _spellbook_effect_support(row, fields, references, modifier_leaves, object_leaves, ocl_leaves, weapon_leaves)
			row["castable"] = bool(support.get("ok", false))
			row["locked_reason"] = String(support.get("reason", ""))
			row["effect"] = support.get("effect", {})
		_spellbook_powers[power_id] = row
	if _spellbook_powers.is_empty():
		_spellbook_error = "spellbook document carries no powers"
		return false
	# Science → tree power index so the palantir can draw prerequisite forks
	# without re-deriving tree logic in the presentation layer.
	_science_to_power.clear()
	for tree_power_id in _spellbook_powers.keys():
		var tree_science := String((_spellbook_powers[tree_power_id] as Dictionary).get("science_id", ""))
		if tree_science != "":
			_science_to_power[tree_science] = String(tree_power_id)
	var ordered: Array[String] = []
	for power_id_value in _spellbook_powers.keys():
		ordered.append(String(power_id_value))
	ordered.sort_custom(func(a: String, b: String) -> bool:
		var row_a: Dictionary = _spellbook_powers[a]
		var row_b: Dictionary = _spellbook_powers[b]
		if int(row_a.get("purchase_slot", 0)) != int(row_b.get("purchase_slot", 0)):
			return int(row_a.get("purchase_slot", 0)) < int(row_b.get("purchase_slot", 0))
		return a.naturalnocasecmp_to(b) < 0
	)
	var seen_slots: Dictionary = {}
	for power_id_value in ordered:
		var slot := int((_spellbook_powers[power_id_value] as Dictionary).get("purchase_slot", 0))
		if slot <= 0 or seen_slots.has(slot):
			_spellbook_error = "spellbook purchase slots are missing or duplicated"
			return false
		seen_slots[slot] = true
	_spellbook_order = ordered
	_reset_spellbook_match_state()
	_spellbook_ready = true
	return true


func _reset_spellbook_match_state() -> void:
	_team_sciences = _seed_team_map(_spellbook_intrinsic)
	_power_cooldown_until = _seed_team_map({})
	_staged_purchases = _seed_team_map([])
	_pending_power_effects.clear()
	_active_groves.clear()
	_summon_despawn_ticks.clear()


## Timed spellbook effect state (volley strikes, summon hatches, groves).
var _pending_power_effects: Array[Dictionary] = []
var _active_groves: Array[Dictionary] = []
## entity_id → tick the summoned battalion fades (authored summon lifetime).
var _summon_despawn_ticks: Dictionary = {}


func _spellbook_effect_support(power_row: Dictionary, fields: Array, references: Dictionary, modifier_leaves: Dictionary, object_leaves: Dictionary, ocl_leaves: Dictionary, weapon_leaves: Dictionary) -> Dictionary:
	## Evidence gate: a power becomes castable only when its converted leaves
	## fully determine the runtime effect. Anything unresolved stays locked
	## with the gap recorded (never an invented effect).
	var module := String(power_row.get("module", ""))
	var field_values: Dictionary = {}
	var field_resolved: Dictionary = {}
	for field_value in fields:
		if typeof(field_value) == TYPE_DICTIONARY:
			var field_row := field_value as Dictionary
			var field_key := String(field_row.get("key", ""))
			if field_row.has("resolvedText"):
				field_values[field_key] = String(field_row.get("resolvedText", ""))
			else:
				field_values[field_key] = String(field_row.get("value", ""))
			if field_row.has("resolved"):
				field_resolved[field_key] = float(field_row.get("resolved", 0.0))
			elif field_row.has("resolvedMax"):
				field_resolved[field_key] = float(field_row.get("resolvedMax", 0.0))
	match module:
		"PlayerHealSpecialPower":
			# HealAmount is the resolved fraction of member health restored; the
			# authored HealRadius define now resolves in the doc (cursor radius
			# remains the fallback when it does not). Rebuild-class powers are
			# the flat structure-heal shape: HealAsPercent No + STRUCTURE affects
			# + a flat authored amount.
			var amount := _spellbook_field_float(field_values, "HealAmount", 0.0)
			var radius := float(field_resolved.get("HealRadius", float(power_row.get("radius_cursor_source", 0.0))))
			var as_percent := String(field_values.get("HealAsPercent", "Yes")) != "No"
			if radius <= 0.0:
				return {"ok": false, "reason": "heal radius did not resolve in the document"}
			if as_percent and (amount <= 0.0 or amount > 1.0):
				return {"ok": false, "reason": "heal amount did not resolve in the document"}
			if not as_percent and amount <= 0.0:
				return {"ok": false, "reason": "flat structure-heal amount did not resolve in the document"}
			return {"ok": true, "effect": {"kind": "heal", "amount": amount, "as_percent": as_percent, "radius_source": radius, "affects": String(field_values.get("HealAffects", ""))}}
		"SpecialPowerModule":
			var modifier_id := String(field_values.get("AttributeModifier", ""))
			if modifier_id == "" or not modifier_leaves.has(modifier_id):
				return {"ok": false, "reason": "attribute modifier '%s' is not a converted leaf" % modifier_id}
			var modifier_fields: Dictionary = {}
			for field_value in Array((modifier_leaves[modifier_id] as Dictionary).get("fields", [])):
				if typeof(field_value) == TYPE_DICTIONARY:
					modifier_fields[String((field_value as Dictionary).get("key", ""))] = String((field_value as Dictionary).get("value", ""))
			var modifier_text := String(modifier_fields.get("Modifier", ""))
			var damage_mult := 0.0
			var modifier_parts := modifier_text.split(" ", false)
			if modifier_parts.size() == 2 and modifier_parts[0] == "DAMAGE_MULT" and modifier_parts[1].ends_with("%"):
				damage_mult = float(modifier_parts[1].trim_suffix("%")) / 100.0
			var duration_ms := _spellbook_field_float(modifier_fields, "Duration", 0.0)
			var range_source := _spellbook_field_float(field_values, "AttributeModifierRange", 0.0)
			if damage_mult <= 0.0 or duration_ms <= 0.0 or range_source <= 0.0:
				return {"ok": false, "reason": "attribute modifier leaf lacks a resolved damage mult, duration, or range"}
			return {"ok": true, "effect": {
				"kind": "attribute_modifier",
				"damage_mult": damage_mult,
				"duration_ticks": maxi(1, roundi(duration_ms / 1000.0 / TICK_SECONDS)),
				"range_source": range_source,
				"affects": String(field_values.get("AttributeModifierAffects", "")),
			}}
		"OCLSpecialPower":
			return _spellbook_ocl_support(power_row, references, object_leaves, ocl_leaves, weapon_leaves)
		"ElvenWoodSpecialPower":
			return _spellbook_grove_support(field_values, field_resolved, references, modifier_leaves, object_leaves)
		"CloudBreakSpecialPower":
			return _spellbook_cloudbreak_support(field_values, field_resolved)
		_:
			return {"ok": false, "reason": "unsupported effect module '%s'" % module}


func _spellbook_field_float(fields: Dictionary, key: String, fallback: float) -> float:
	var raw := String(fields.get(key, ""))
	if raw == "" or not raw.is_valid_float():
		return fallback
	return float(raw)


func _spellbook_ocl_support(power_row: Dictionary, references: Dictionary, object_leaves: Dictionary, ocl_leaves: Dictionary, weapon_leaves: Dictionary) -> Dictionary:
	## OCL powers dispatch on the spawned objects' converted evidence:
	## fire-weapon receptacles (volley/quake), summon eggs, or structures.
	var ocl_ids: Array = references.get("objectCreationLists", []) as Array
	if ocl_ids.is_empty():
		return {"ok": false, "reason": "power references no object-creation list"}
	var ocl_id := String(ocl_ids[0])
	var ocl: Dictionary = ocl_leaves.get(ocl_id, {}) as Dictionary
	if ocl.is_empty():
		return {"ok": false, "reason": "object-creation list '%s' is not a converted leaf" % ocl_id}
	var spawns: Array = []
	var missing: Array = []
	for create_value in Array(ocl.get("createObjects", [])):
		if typeof(create_value) != TYPE_DICTIONARY:
			continue
		var create := create_value as Dictionary
		for object_name_value in Array(create.get("objects", [])):
			var object_name := String(object_name_value)
			var leaf: Dictionary = object_leaves.get(object_name, {}) as Dictionary
			if leaf.is_empty():
				missing.append(object_name)
			else:
				spawns.append({"create": create, "leaf": leaf})
	if not missing.is_empty():
		return {"ok": false, "reason": "spawned object(s) %s are not converted leaves" % ", ".join(missing)}
	if spawns.is_empty():
		return {"ok": false, "reason": "object-creation list '%s' creates no objects" % ocl_id}
	var first_leaf: Dictionary = (spawns[0] as Dictionary)["leaf"]
	if not Array(first_leaf.get("fireWeapons", [])).is_empty():
		return _spellbook_fire_weapon_support(spawns, weapon_leaves)
	if typeof(first_leaf.get("hatch", null)) == TYPE_DICTIONARY:
		return _spellbook_summon_support(spawns, object_leaves, ocl_leaves, weapon_leaves)
	var body_kinds: Array = first_leaf.get("bodyKinds", []) as Array
	if body_kinds.has("StructureBody"):
		return _spellbook_structure_summon_support(spawns[0], weapon_leaves)
	return {"ok": false, "reason": "spawned object '%s' carries no fire-weapon, hatch, or structure evidence" % String(first_leaf.get("id", ""))}


func _spellbook_fire_weapon_support(spawns: Array, weapon_leaves: Dictionary) -> Dictionary:
	var strikes: Array = []
	for spawn_value in spawns:
		var spawn := spawn_value as Dictionary
		for fw_value in Array((spawn["leaf"] as Dictionary).get("fireWeapons", [])):
			var fw := fw_value as Dictionary
			var weapon_id := String(fw.get("weapon", ""))
			var weapon: Dictionary = weapon_leaves.get(weapon_id, {}) as Dictionary
			if weapon.is_empty():
				return {"ok": false, "reason": "fire-weapon '%s' is not a converted leaf" % weapon_id}
			var nuggets := _spellbook_weapon_damage_nuggets(weapon, weapon_leaves)
			if nuggets.is_empty():
				# Warning-shot phase: an authored fire entry with no damage.
				continue
			var delay_ms := float(fw.get("fireDelayMs", 0.0))
			for nugget_value in nuggets:
				var nugget := nugget_value as Dictionary
				strikes.append({
					"delay_ms": delay_ms + float(nugget.get("delaytime", 0.0)),
					"damage": float(nugget.get("damage", 0.0)),
					"radius_source": float(nugget.get("radius", 0.0)),
					"damage_type": String(nugget.get("damagetype", "")).to_lower(),
					"affects": String(weapon.get("radiusDamageAffects", "ENEMIES")),
				})
	if strikes.is_empty():
		return {"ok": false, "reason": "fire-weapon chain carries no resolved damage nuggets"}
	return {"ok": true, "effect": {"kind": "fire_weapon", "strikes": strikes}}


func _spellbook_weapon_damage_nuggets(weapon: Dictionary, weapon_leaves: Dictionary) -> Array:
	## Direct damage nuggets, else the first projectile nugget's warhead chain.
	var direct: Array = weapon.get("damageNuggets", []) as Array
	if not direct.is_empty():
		return direct
	for nugget_value in Array(weapon.get("nuggets", [])):
		var nugget := nugget_value as Dictionary
		if String(nugget.get("kind", "")).to_lower() != "projectilenugget":
			continue
		var warhead: Dictionary = weapon_leaves.get(String(nugget.get("warheadId", "")), {}) as Dictionary
		if not warhead.is_empty():
			return warhead.get("damageNuggets", []) as Array
	return []


func _spellbook_weapon_field(weapon: Dictionary, key: String) -> float:
	for field_value in Array(weapon.get("fields", [])):
		if typeof(field_value) != TYPE_DICTIONARY:
			continue
		var field_row := field_value as Dictionary
		if String(field_row.get("key", "")) != key:
			continue
		if field_row.has("resolvedMax"):
			return float(field_row.get("resolvedMax", 0.0))
		if field_row.has("resolved"):
			return float(field_row.get("resolved", 0.0))
		return 0.0
	return 0.0


func _spellbook_structure_summon_support(spawn: Dictionary, weapon_leaves: Dictionary) -> Dictionary:
	var leaf: Dictionary = spawn["leaf"]
	var health := int(leaf.get("maxHealth", 0))
	var weapon_id := String(leaf.get("weaponId", ""))
	var weapon: Dictionary = weapon_leaves.get(weapon_id, {}) as Dictionary
	var nuggets := _spellbook_weapon_damage_nuggets(weapon, weapon_leaves)
	var attack_range := _spellbook_weapon_field(weapon, "AttackRange")
	if health <= 0:
		return {"ok": false, "reason": "summoned structure health is not converted"}
	if weapon_id == "" or weapon.is_empty() or nuggets.is_empty() or attack_range <= 0.0:
		return {"ok": false, "reason": "summoned structure weapon '%s' is not fully converted" % weapon_id}
	var build_ms := 0.0
	for field_value in Array((spawn["create"] as Dictionary).get("fields", [])):
		if typeof(field_value) != TYPE_DICTIONARY:
			continue
		var field_row := field_value as Dictionary
		var field_key := String(field_row.get("key", ""))
		if field_key == "JustBuiltDuration" or field_key == "StartingBusyTime":
			build_ms = maxf(build_ms, float(field_row.get("resolved", 0.0)))
	return {"ok": true, "effect": {
		"kind": "structure_summon",
		"object_id": String(leaf.get("id", "")),
		"health": health,
		"build_ticks": maxi(1, roundi(build_ms / 1000.0 / TICK_SECONDS)),
		"weapon": {
			"damage": float((nuggets[0] as Dictionary).get("damage", 0.0)),
			"range_source": attack_range,
			"damage_type": String((nuggets[0] as Dictionary).get("damagetype", "")).to_lower(),
			"period_ms": _spellbook_weapon_field(weapon, "DelayBetweenShots"),
			"pre_attack_ms": _spellbook_weapon_field(weapon, "PreAttackDelay"),
			"firing_ms": _spellbook_weapon_field(weapon, "FiringDuration"),
			"affects": String(weapon.get("radiusDamageAffects", "ENEMIES")),
		},
	}}


func _spellbook_summon_support(spawns: Array, object_leaves: Dictionary, ocl_leaves: Dictionary, weapon_leaves: Dictionary) -> Dictionary:
	## Egg powers: the power OCL creates eggs; each egg hatches an OCL of
	## summoned battalions. The full chain must convert: egg hatch, hatch OCL,
	## target horde/member stats, member weapon, locomotion, summon lifetime.
	var first_leaf: Dictionary = (spawns[0] as Dictionary)["leaf"]
	var hatch: Dictionary = first_leaf.get("hatch", {}) as Dictionary
	var hatch_ocl_id := String(hatch.get("ocl", ""))
	var hatch_ocl: Dictionary = ocl_leaves.get(hatch_ocl_id, {}) as Dictionary
	if hatch_ocl.is_empty():
		return {"ok": false, "reason": "hatch OCL '%s' is not a converted leaf" % hatch_ocl_id}
	var egg_count := 1
	for field_value in Array(((spawns[0] as Dictionary)["create"] as Dictionary).get("fields", [])):
		if typeof(field_value) == TYPE_DICTIONARY and String((field_value as Dictionary).get("key", "")) == "Count":
			egg_count = maxi(1, int((field_value as Dictionary).get("resolved", 1)))
	var hatch_delay_ms := float(hatch.get("destructionDelayMs", 0.0))
	var targets: Array = []
	for create_value in Array(hatch_ocl.get("createObjects", [])):
		if typeof(create_value) != TYPE_DICTIONARY:
			continue
		var create := create_value as Dictionary
		var count := 1
		for field_value in Array(create.get("fields", [])):
			if typeof(field_value) == TYPE_DICTIONARY and String((field_value as Dictionary).get("key", "")) == "Count":
				count = maxi(1, int((field_value as Dictionary).get("resolved", 1)))
		for object_name_value in Array(create.get("objects", [])):
			var object_name := String(object_name_value)
			var target_leaf: Dictionary = object_leaves.get(object_name, {}) as Dictionary
			if target_leaf.is_empty():
				return {"ok": false, "reason": "summon target '%s' is not a converted leaf" % object_name}
			var verdict := _spellbook_summon_rule(target_leaf, object_leaves, weapon_leaves)
			if not bool(verdict.get("ok", false)):
				return {"ok": false, "reason": String(verdict.get("reason", "summon stats unresolved"))}
			targets.append({
				"object_id": object_name,
				"count": count * egg_count,
				"rule": verdict["rule"],
				"lifetime_ticks": int(verdict.get("lifetime_ticks", 0)),
			})
	if targets.is_empty():
		return {"ok": false, "reason": "hatch OCL '%s' spawns no converted targets" % hatch_ocl_id}
	return {"ok": true, "effect": {
		"kind": "summon",
		"hatch_delay_ticks": maxi(0, roundi(hatch_delay_ms / 1000.0 / TICK_SECONDS)),
		"targets": targets,
	}}


func _spellbook_summon_rule(target_leaf: Dictionary, object_leaves: Dictionary, weapon_leaves: Dictionary) -> Dictionary:
	## Project one summon target into the sim's unit-rule shape; every value
	## traces to the converted object/weapon/locomotor leaves.
	var horde: Dictionary = target_leaf.get("horde", {}) as Dictionary
	var member_id := String(target_leaf.get("id", "")) if horde.is_empty() else String(horde.get("memberObject", ""))
	var member_count := 1 if horde.is_empty() else int(horde.get("memberCount", 1))
	var member: Dictionary = object_leaves.get(member_id, {}) as Dictionary
	if member.is_empty():
		return {"ok": false, "reason": "summoned member '%s' is not a converted leaf" % member_id}
	if not member.has("maxHealth") and member.has("buildVariations"):
		for variation_value in Array(member.get("buildVariations", [])):
			var candidate: Dictionary = object_leaves.get(String(variation_value), {}) as Dictionary
			if candidate.has("maxHealth"):
				member = candidate
				break
	var member_health := int(member.get("maxHealth", 0))
	if member_health <= 0:
		return {"ok": false, "reason": "summoned member '%s' health is not converted" % member_id}
	var locomotor: Dictionary = member.get("locomotor", {}) as Dictionary
	var speed := float(locomotor.get("speed", 0.0))
	if speed <= 0.0:
		return {"ok": false, "reason": "summoned member '%s' locomotion is not converted" % member_id}
	var weapon_id := String(member.get("weaponId", ""))
	var weapon: Dictionary = weapon_leaves.get(weapon_id, {}) as Dictionary
	if weapon_id == "" or weapon.is_empty():
		return {"ok": false, "reason": "summoned member '%s' weapon is not a converted leaf" % member_id}
	var nuggets := _spellbook_weapon_damage_nuggets(weapon, weapon_leaves)
	if nuggets.is_empty():
		return {"ok": false, "reason": "summoned member weapon '%s' has no resolved damage" % weapon_id}
	var attack_range := _spellbook_weapon_field(weapon, "AttackRange")
	if attack_range <= 0.0:
		return {"ok": false, "reason": "summoned member weapon '%s' range is not converted" % weapon_id}
	var lifetime_ms := 0.0
	var lifetime_row: Dictionary = target_leaf.get("lifetime", {}) as Dictionary
	if not lifetime_row.is_empty():
		lifetime_ms = float(lifetime_row.get("maxMs", 0.0))
	if lifetime_ms <= 0.0:
		var member_lifetime: Dictionary = member.get("lifetime", {}) as Dictionary
		lifetime_ms = float(member_lifetime.get("maxMs", 0.0))
	if lifetime_ms <= 0.0:
		return {"ok": false, "reason": "summon lifetime is not converted"}
	var scale := _spellbook_world_scale()
	var source_positions: Array[Vector2] = []
	for rank_value in Array(horde.get("ranks", [])):
		for position_value in Array((rank_value as Dictionary).get("positions", [])):
			var pair: Array = position_value as Array
			if pair.size() >= 2:
				source_positions.append(Vector2(float(pair[0]), float(pair[1])))
	if source_positions.size() != member_count:
		source_positions.clear()
		for index in range(member_count):
			source_positions.append(Vector2(10.0 + float(index % 4) * 15.0, float(index / 4) * 15.0))
	var center := Vector2.ZERO
	for position in source_positions:
		center += position
	center /= float(maxi(1, source_positions.size()))
	var positions: Array[Vector3] = []
	for position in source_positions:
		positions.append(Vector3((position.y - center.y) * scale, 0.0, (position.x - center.x) * scale))
	var damage_nugget: Dictionary = nuggets[0]
	var delay_ms := _spellbook_weapon_field(weapon, "DelayBetweenShots")
	var clip_reload_ms := _spellbook_weapon_field(weapon, "ClipReloadTime")
	var period_ms := delay_ms if delay_ms > 0.0 else clip_reload_ms
	var pre_attack_ms := _spellbook_weapon_field(weapon, "PreAttackDelay")
	var firing_ms := _spellbook_weapon_field(weapon, "FiringDuration")
	var kind_of: Array = member.get("kindOf", []) as Array
	var category := "infantry"
	if kind_of.has("HERO"):
		category = "hero"
	elif kind_of.has("CAVALRY"):
		category = "cavalry"
	var vision := float(member.get("visionRange", 0.0))
	if vision <= 0.0:
		vision = attack_range
	var response_scale := PlayableUnitAdapter.HORDE_LOCOMOTION_RESPONSE_SCALE
	var rule := {
		"horde_id": String(target_leaf.get("id", "")),
		"member_count": member_count,
		"member_health": member_health,
		"member_damage": maxi(1, int(damage_nugget.get("damage", 0))),
		"category": category,
		"speed": speed * scale,
		"speed_source": speed,
		"acceleration": float(locomotor.get("acceleration", speed)) * scale * response_scale,
		"acceleration_source": float(locomotor.get("acceleration", speed)) * response_scale,
		"turn_rate_degrees_per_second": float(locomotor.get("turnRateDegreesPerSecond", 360.0)),
		"braking": float(locomotor.get("braking", speed)) * scale * response_scale,
		"braking_source": float(locomotor.get("braking", speed)) * response_scale,
		"attack_range": attack_range * scale,
		"attack_range_source": attack_range,
		"minimum_attack_range": _spellbook_weapon_field(weapon, "MinimumAttackRange") * scale,
		"minimum_attack_range_source": _spellbook_weapon_field(weapon, "MinimumAttackRange"),
		"vision_range": vision * scale,
		"vision_range_source": vision,
		"delay_between_shots_ms": delay_ms,
		"pre_attack_delay_ms": pre_attack_ms,
		"firing_duration_ms": firing_ms,
		"attack_period_ticks": maxi(1, roundi(period_ms / (TICK_SECONDS * 1000.0))),
		"pre_attack_ticks": maxi(0, roundi(pre_attack_ms / (TICK_SECONDS * 1000.0))),
		"firing_duration_ticks": maxi(0, roundi(firing_ms / (TICK_SECONDS * 1000.0))),
		"clip_size": int(_spellbook_weapon_field(weapon, "ClipSize")),
		"clip_reload_time_ms": clip_reload_ms,
		"formation_positions": positions,
		"default_weapon_mode": "default",
		"damage_type": String(damage_nugget.get("damagetype", "")).to_lower(),
		"provenance": {"source": "spellbook-summon", "object_id": String(target_leaf.get("id", ""))},
	}
	return {"ok": true, "rule": rule, "lifetime_ticks": maxi(1, roundi(lifetime_ms / 1000.0 / TICK_SECONDS))}


func _spellbook_grove_support(field_values: Dictionary, field_resolved: Dictionary, references: Dictionary, modifier_leaves: Dictionary, object_leaves: Dictionary) -> Dictionary:
	## Elven Wood: the converted ElvenGrove leaf carries the taint aura —
	## modifier leaf, refresh, range, filter, and the grove lifetime.
	var grove_id := String(field_values.get("ElvenGroveObject", ""))
	var grove: Dictionary = object_leaves.get(grove_id, {}) as Dictionary
	if grove.is_empty():
		return {"ok": false, "reason": "ElvenGrove object '%s' is not a converted leaf" % grove_id}
	var aura: Dictionary = grove.get("aura", {}) as Dictionary
	var deletion: Dictionary = grove.get("deletion", {}) as Dictionary
	if aura.is_empty() or deletion.is_empty():
		return {"ok": false, "reason": "ElvenGrove aura or lifetime is not converted"}
	var modifier_id := String(aura.get("modifier", ""))
	var modifier: Dictionary = modifier_leaves.get(modifier_id, {}) as Dictionary
	if modifier.is_empty():
		return {"ok": false, "reason": "grove aura modifier '%s' is not a converted leaf" % modifier_id}
	var armor_mult := 0.0
	var buff_duration_ms := 0.0
	for field_value in Array(modifier.get("fields", [])):
		if typeof(field_value) != TYPE_DICTIONARY:
			continue
		var field_row := field_value as Dictionary
		if String(field_row.get("key", "")) == "Modifier":
			var parts := String(field_row.get("value", "")).split(" ", false)
			if parts.size() == 2 and parts[0] == "ARMOR" and parts[1].ends_with("%"):
				armor_mult = float(parts[1].trim_suffix("%")) / 100.0
		elif String(field_row.get("key", "")) == "Duration":
			buff_duration_ms = float(field_row.get("resolved", field_row.get("value", 0.0)))
	var aura_range := float(aura.get("range", 0.0))
	var lifetime_ms := float(deletion.get("maxMs", 0.0))
	var filter := String(aura.get("objectFilter", ""))
	if armor_mult <= 0.0 or buff_duration_ms <= 0.0 or aura_range <= 0.0 or lifetime_ms <= 0.0:
		return {"ok": false, "reason": "grove aura modifier, range, or lifetime is not converted"}
	if filter == "" or (not filter.contains("ANY") and not filter.contains("+")):
		return {"ok": false, "reason": "grove aura object filter is not converted"}
	return {"ok": true, "effect": {
		"kind": "grove_aura",
		"armor_mult": armor_mult,
		"buff_duration_ticks": maxi(1, roundi(buff_duration_ms / 1000.0 / TICK_SECONDS)),
		"range_source": aura_range,
		"lifetime_ticks": maxi(1, roundi(lifetime_ms / 1000.0 / TICK_SECONDS)),
		"filter": filter,
		"modifier": modifier_id,
	}}


func _spellbook_cloudbreak_support(field_values: Dictionary, field_resolved: Dictionary) -> Dictionary:
	## Cloud Break: WeatherDuration resolves in the doc; the enemy disruption
	## is the module's authored affects filter over the weather duration.
	var duration_ms := float(field_resolved.get("WeatherDuration", 0.0))
	var affects := String(field_values.get("AttributeModifierAffects", ""))
	var weather := String(field_values.get("ChangeWeather", ""))
	if duration_ms <= 0.0 or affects == "" or weather == "":
		return {"ok": false, "reason": "cloud break duration, affects filter, or weather is not converted"}
	return {"ok": true, "effect": {
		"kind": "cloudbreak_stun",
		"duration_ticks": maxi(1, roundi(duration_ms / 1000.0 / TICK_SECONDS)),
		"affects": affects,
		"weather": weather,
	}}


func spellbook_available() -> bool:
	return _spellbook_ready


func spellbook_error() -> String:
	return _spellbook_error


func spellbook_power_ids() -> Array[String]:
	return _spellbook_order.duplicate()


func spellbook_power(power_id: String) -> Dictionary:
	return (_spellbook_powers.get(power_id, {}) as Dictionary).duplicate(true)


func _spellbook_world_scale() -> float:
	## Doc radii are source-map units; sim space is source * local transform.
	return maxf(0.000001, float(_rules.get("source_map_transform_scale", 1.0)))


func power_points(team: int) -> int:
	return int(team_power_points.get(team, 0))


func owned_sciences(team: int) -> Array:
	return (_team_sciences.get(team, []) as Array).duplicate()


func has_power(team: int, power_id: String) -> bool:
	return (purchased_powers.get(team, []) as Array).has(power_id)


func _science_owned(team: int, science_id: String) -> bool:
	return (_team_sciences.get(team, []) as Array).has(science_id)


func _power_prerequisites_met(team: int, science_id: String) -> bool:
	var science_row: Dictionary = _spellbook_sciences.get(science_id, {}) as Dictionary
	if science_row.is_empty():
		return false
	var groups: Array = science_row.get("groups", []) as Array
	if groups.is_empty():
		return true
	for group_value in groups:
		var satisfied := true
		for member in group_value as Array:
			if not _science_owned(team, String(member)):
				satisfied = false
				break
		if satisfied:
			return true
	return false


func can_purchase_power(team: int, power_id: String) -> Dictionary:
	if not _spellbook_ready:
		return {"ok": false, "reason": "spellbook-unavailable"}
	var row: Dictionary = _spellbook_powers.get(power_id, {}) as Dictionary
	if row.is_empty():
		return {"ok": false, "reason": "unknown-power"}
	if has_power(team, power_id):
		return {"ok": false, "reason": "already-purchased"}
	if not _power_prerequisites_met(team, String(row.get("science_id", ""))):
		return {"ok": false, "reason": "prerequisites-unmet"}
	if power_points(team) < int(row.get("cost", 0)):
		return {"ok": false, "reason": "insufficient-power-points"}
	return {"ok": true, "reason": ""}


func purchase_power(team: int, power_id: String, cost: int = -1) -> Dictionary:
	var verdict := can_purchase_power(team, power_id)
	if not bool(verdict.get("ok", false)):
		return verdict
	var row: Dictionary = _spellbook_powers[power_id]
	var doc_cost := int(row.get("cost", 0))
	if cost >= 0 and cost != doc_cost:
		return {"ok": false, "reason": "cost-mismatch"}
	team_power_points[team] = power_points(team) - doc_cost
	(purchased_powers[team] as Array).append(power_id)
	(_team_sciences[team] as Array).append(String(row.get("science_id", "")))
	(_staged_purchases[team] as Array).append({"power_id": power_id, "science_id": String(row.get("science_id", "")), "cost": doc_cost})
	_emit_event("power.purchased", 0, 0, {
		"team": team,
		"power_id": power_id,
		"science_id": String(row.get("science_id", "")),
		"cost": doc_cost,
		"purchase_slot": int(row.get("purchase_slot", 0)),
	})
	return {"ok": true, "reason": "", "cost": doc_cost}


func reset_spellbook_purchases(team: int) -> Dictionary:
	## Retail RESET: refund this session's unspent picks so the player re-picks.
	if not _spellbook_ready:
		return {"ok": false, "reason": "spellbook-unavailable"}
	var refunded := 0
	var restored: Array = []
	for entry_value in Array(_staged_purchases.get(team, [])):
		if typeof(entry_value) != TYPE_DICTIONARY:
			continue
		var entry := entry_value as Dictionary
		var power_id := String(entry.get("power_id", ""))
		refunded += int(entry.get("cost", 0))
		(purchased_powers[team] as Array).erase(power_id)
		(_team_sciences[team] as Array).erase(String(entry.get("science_id", "")))
		restored.append(power_id)
	_staged_purchases[team] = []
	if refunded > 0:
		team_power_points[team] = power_points(team) + refunded
	_emit_event("power.reset", 0, 0, {"team": team, "refunded": refunded, "powers": restored})
	return {"ok": true, "reason": "", "refunded": refunded, "powers": restored}


func accept_spellbook_purchases(team: int) -> Dictionary:
	## Retail ACCEPT (including closing the orb): the session's picks commit.
	if not _spellbook_ready:
		return {"ok": false, "reason": "spellbook-unavailable"}
	_staged_purchases[team] = []
	return {"ok": true, "reason": ""}


func power_cooldown_state(team: int, power_id: String) -> Dictionary:
	var row: Dictionary = _spellbook_powers.get(power_id, {}) as Dictionary
	if row.is_empty():
		return {}
	var total := int(row.get("reload_ticks", 1))
	var ready_tick := int((_power_cooldown_until.get(team, {}) as Dictionary).get(power_id, -1))
	var remaining := maxi(0, ready_tick - tick_index)
	return {
		"total_ticks": total,
		"remaining_ticks": remaining,
		"progress": 1.0 - (float(remaining) / float(maxi(1, total))),
	}


func spellbook_power_radius_sim(power_id: String) -> float:
	## The doc's resolved targeting-cursor radius mapped into sim units (the
	## targeting ring's size; 0 when the power/doc carries none).
	var row: Dictionary = _spellbook_powers.get(power_id, {}) as Dictionary
	if row.is_empty():
		return 0.0
	return float(row.get("radius_cursor_source", 0.0)) * _spellbook_world_scale()


func spellbook_ui_state(team: int) -> Dictionary:
	## Per-power orb state: the presentation layer never derives tree logic.
	var powers: Dictionary = {}
	for power_id in _spellbook_order:
		var row: Dictionary = _spellbook_powers[power_id]
		var owned := has_power(team, power_id)
		var prerequisites_met := _power_prerequisites_met(team, String(row.get("science_id", "")))
		var cooldown := power_cooldown_state(team, power_id)
		var staged := false
		for entry_value in Array(_staged_purchases.get(team, [])):
			if String((entry_value as Dictionary).get("power_id", "")) == power_id:
				staged = true
				break
		var locked_reason := ""
		if not owned:
			if not prerequisites_met:
				locked_reason = "prerequisites-unmet"
			elif power_points(team) < int(row.get("cost", 0)):
				locked_reason = "insufficient-power-points"
			elif not bool(row.get("castable", false)):
				locked_reason = String(row.get("locked_reason", "effect-unsupported"))
		var prereq_power_ids: Array = []
		var science_row: Dictionary = _spellbook_sciences.get(String(row.get("science_id", "")), {}) as Dictionary
		for group_value in Array(science_row.get("groups", [])):
			# Only groups this faction can ever complete draw a palantir fork:
			# every member must be intrinsic-owned or a tree science (groups
			# naming another faction's root, e.g. SCIENCE_DWARVES, are dead
			# paths the retail orb does not draw).
			var achievable := true
			for member in group_value as Array:
				var member_id := String(member)
				if not _spellbook_intrinsic.has(member_id) and not _science_to_power.has(member_id):
					achievable = false
					break
			if not achievable:
				continue
			for member in group_value as Array:
				var prereq_power_id := String(_science_to_power.get(String(member), ""))
				if prereq_power_id != "" and not prereq_power_ids.has(prereq_power_id):
					prereq_power_ids.append(prereq_power_id)
		powers[power_id] = {
			"id": power_id,
			"cost": int(row.get("cost", 0)),
			"purchase_slot": int(row.get("purchase_slot", 0)),
			"cast_slot": int(row.get("cast_slot", 0)),
			"owned": owned,
			"staged": staged,
			"prerequisites_met": prerequisites_met,
			"prereq_power_ids": prereq_power_ids,
			"purchasable": not owned and prerequisites_met and power_points(team) >= int(row.get("cost", 0)),
			"castable": bool(row.get("castable", false)),
			"locked_reason": locked_reason,
			"effect_locked_reason": String(row.get("locked_reason", "")),
			"needs_target_pos": bool(row.get("needs_target_pos", false)),
			"radius_cursor_source": float(row.get("radius_cursor_source", 0.0)),
			"sound_id": String(row.get("sound_id", "")),
			"cooldown": cooldown,
		}
	return {"points": power_points(team), "powers": powers}


func award_power_kill(team: int) -> void:
	var kills_per_point := maxi(1, int(_rules.get("power_point_kills", POWER_POINT_KILLS)))
	_kills_toward_power_point[team] = int(_kills_toward_power_point.get(team, 0)) + 1
	if int(_kills_toward_power_point[team]) >= kills_per_point:
		_kills_toward_power_point[team] = 0
		team_power_points[team] = power_points(team) + 1
		_emit_event("power.point_earned", 0, 0, {"team": team, "points": power_points(team)})


func cast_power(team: int, power_id: String, point: Vector2) -> Dictionary:
	if not _spellbook_ready:
		return {"ok": false, "reason": "spellbook-unavailable"}
	var row: Dictionary = _spellbook_powers.get(power_id, {}) as Dictionary
	if row.is_empty():
		return {"ok": false, "reason": "unknown-power"}
	if not has_power(team, power_id):
		return {"ok": false, "reason": "power-not-purchased"}
	if not bool(row.get("castable", false)):
		return {"ok": false, "reason": "effect-unsupported", "detail": String(row.get("locked_reason", ""))}
	var cooldown := power_cooldown_state(team, power_id)
	if int(cooldown.get("remaining_ticks", 0)) > 0:
		return {"ok": false, "reason": "power-recharging", "remaining_ticks": int(cooldown.get("remaining_ticks", 0))}
	var effect: Dictionary = row.get("effect", {}) as Dictionary
	var result := {"ok": false, "reason": "effect-unsupported"}
	match String(effect.get("kind", "")):
		"heal":
			result = _cast_spellbook_heal(team, effect, point)
		"attribute_modifier":
			result = _cast_spellbook_attribute_modifier(team, effect, point)
		"fire_weapon":
			result = _cast_spellbook_fire_weapon(team, effect, point)
		"summon":
			result = _cast_spellbook_summon(team, effect, point)
		"structure_summon":
			result = _cast_spellbook_structure_summon(team, effect, point)
		"grove_aura":
			result = _cast_spellbook_grove(team, effect, point)
		"cloudbreak_stun":
			result = _cast_spellbook_cloudbreak(team, effect, point)
	if not bool(result.get("ok", false)):
		return result
	(_power_cooldown_until[team] as Dictionary)[power_id] = tick_index + int(row.get("reload_ticks", 1))
	# A staged pick that gets cast is spent: RESET can no longer refund it.
	var staged: Array = _staged_purchases[team]
	for index in range(staged.size() - 1, -1, -1):
		if String((staged[index] as Dictionary).get("power_id", "")) == power_id:
			staged.remove_at(index)
	_emit_event("power.cast", 0, 0, {
		"team": team,
		"power_id": power_id,
		"science_id": String(row.get("science_id", "")),
		"sound_id": String(row.get("sound_id", "")),
		"effect_kind": String(effect.get("kind", "")),
		"radius_source": float(effect.get("radius_source", effect.get("range_source", 0.0))),
		"fx_lists": row.get("fx_lists", []),
		"ocls": row.get("ocls", []),
		"battalions": int(result.get("battalions", 0)),
		"point": [snappedf(point.x, 0.001), snappedf(point.y, 0.001)],
	})
	return result


func cast_heal(team: int, point: Vector2) -> Dictionary:
	return cast_power(team, "SpellBookHeal", point)


func cast_rally(team: int, point: Vector2) -> Dictionary:
	return cast_power(team, "SpellBookRallyingCall", point)


func _spellbook_object_kinds(row: Dictionary) -> Array:
	## Maps a battalion row onto the doc's object-kind vocabulary for the
	## authored HealAffects / AttributeModifierAffects filters.
	var kinds: Array = ["ANY"]
	if String(row.get("horde_id", "")) != "":
		kinds.append("HORDE")
	var category := String(row.get("category", ""))
	match category:
		"hero":
			kinds.append("HERO")
		"cavalry":
			kinds.append("CAVALRY")
		"infantry", "ranged-infantry":
			kinds.append("INFANTRY")
	if bool(row.get("is_builder", false)):
		kinds.append("DOZER")
	return kinds


func _spellbook_affects(row: Dictionary, filter_text: String) -> bool:
	## SAGE object-filter subset: space-separated ANY/+include/-exclude terms.
	var kinds := _spellbook_object_kinds(row)
	var included := false
	for term_value in filter_text.split(" ", false):
		var term := String(term_value)
		if term == "" or term == "NONE":
			continue
		if term == "ENEMIES" or term == "ALLIES":
			continue
		if term.begins_with("-"):
			if kinds.has(term.trim_prefix("-")):
				return false
		elif term.begins_with("+"):
			if kinds.has(term.trim_prefix("+")):
				included = true
		elif term == "ANY":
			included = true
		elif kinds.has(term):
			included = true
	return included


func _cast_spellbook_heal(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	var radius := float(effect.get("radius_source", 0.0)) * _spellbook_world_scale()
	if not bool(effect.get("as_percent", true)):
		return _cast_spellbook_structure_heal(team, effect, point, radius)
	var fraction := float(effect.get("amount", 0.5))
	var healed := 0
	for id in living_ids(team):
		var row: Dictionary = entities[id]
		if Vector2(row.get("position", Vector2.ZERO)).distance_to(point) > radius:
			continue
		if not _spellbook_affects(row, String(effect.get("affects", ""))):
			continue
		var maximum_member := int(row.get("member_maximum_health", 1))
		var heal_amount := maxi(1, roundi(float(maximum_member) * fraction))
		var health_values: Array = row.get("member_health", [])
		var restored := 0
		for member_index in health_values.size():
			var current := int(health_values[member_index])
			if current > 0 and current < maximum_member:
				restored += mini(maximum_member - current, heal_amount)
				health_values[member_index] = mini(maximum_member, current + heal_amount)
		if restored > 0:
			row["member_health"] = health_values
			row["health"] = 0
			for value in health_values:
				row["health"] = int(row["health"]) + int(value)
			healed += 1
	if healed == 0:
		return {"ok": false, "reason": "no-wounded-allies-in-range"}
	return {"ok": true, "reason": "", "battalions": healed}


func _cast_spellbook_structure_heal(team: int, effect: Dictionary, point: Vector2, radius: float) -> Dictionary:
	## Rebuild-class heal: the authored flat amount restores matching
	## structures in radius (HealAsPercent No + STRUCTURE affects filter).
	var affects := String(effect.get("affects", ""))
	if not affects.contains("STRUCTURE"):
		return {"ok": false, "reason": "no-wounded-allies-in-range"}
	var amount := int(effect.get("amount", 0))
	var healed := 0
	for structure_id in structure_ids(team):
		var structure: Dictionary = structures[structure_id]
		var health := int(structure.get("health", 0))
		var maximum := int(structure.get("maximum_health", 1))
		if health <= 0 or health >= maximum:
			continue
		if Vector2(structure.get("position", Vector2.ZERO)).distance_to(point) > radius:
			continue
		structure["health"] = mini(maximum, health + amount)
		healed += 1
	if healed == 0:
		return {"ok": false, "reason": "no-wounded-allies-in-range"}
	return {"ok": true, "reason": "", "battalions": healed}


func _cast_spellbook_attribute_modifier(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	var range_sim := float(effect.get("range_source", 0.0)) * _spellbook_world_scale()
	var duration_ticks := int(effect.get("duration_ticks", 1))
	var damage_mult := float(effect.get("damage_mult", 1.0))
	var rallied := 0
	for id in living_ids(team):
		var row: Dictionary = entities[id]
		if Vector2(row.get("position", Vector2.ZERO)).distance_to(point) > range_sim:
			continue
		# Aura-style filters (GENERIC_BUFF_RECIPIENT_OBJECT_FILTER) exclude the
		# HORDE container but include its members; evaluate member kinds so the
		# battalion-as-container distinction does not reject infantry hordes.
		if not _spellbook_member_affects(row, String(effect.get("affects", ""))):
			continue
		row["rally_until_tick"] = tick_index + duration_ticks
		row["rally_damage_mult"] = damage_mult
		rallied += 1
	if rallied == 0:
		return {"ok": false, "reason": "no-allies-in-range"}
	return {"ok": true, "reason": "", "battalions": rallied}


func _cast_spellbook_fire_weapon(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	## Volley/quake receptacles: the authored fire delays schedule the strikes;
	## each damage nugget detonates at cast point with its converted payload.
	var strikes: Array = effect.get("strikes", []) as Array
	if strikes.is_empty():
		return {"ok": false, "reason": "no-strikes"}
	for strike_value in strikes:
		var strike := strike_value as Dictionary
		_pending_power_effects.append({
			"kind": "strike",
			"fire_tick": tick_index + maxi(0, roundi(float(strike.get("delay_ms", 0.0)) / (TICK_SECONDS * 1000.0))),
			"team": team,
			"point": point,
			"damage": float(strike.get("damage", 0.0)),
			"radius_source": float(strike.get("radius_source", 0.0)),
			"damage_type": String(strike.get("damage_type", "")),
			"affects": String(strike.get("affects", "ENEMIES")),
		})
	return {"ok": true, "reason": "", "battalions": 0, "strikes": strikes.size()}


func _cast_spellbook_summon(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	## Summon eggs hatch after the authored destruction delay into battalions
	## whose stats and summon lifetime all come from the converted leaves.
	_pending_power_effects.append({
		"kind": "summon",
		"fire_tick": tick_index + int(effect.get("hatch_delay_ticks", 0)),
		"team": team,
		"point": point,
		"targets": (effect.get("targets", []) as Array).duplicate(true),
	})
	return {"ok": true, "reason": "", "battalions": 0}


func _cast_spellbook_structure_summon(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	## Lone Tower: the structure rises at the target point over the authored
	## just-built duration, then fires its converted bow on enemies in range.
	var structure_id := _next_dynamic_structure_id
	_next_dynamic_structure_id += 1
	var build_ticks := int(effect.get("build_ticks", 1))
	var health := int(effect.get("health", 1))
	var weapon: Dictionary = effect.get("weapon", {}) as Dictionary
	structures[structure_id] = {
		"id": structure_id,
		"team": team,
		"kind": "structure",
		"structure_kind": "lone_tower",
		"name": "Lone Tower",
		"position": point,
		"rally": point,
		"health": health,
		"maximum_health": health,
		"construction_progress": 0.0,
		"construction_elapsed_ticks": 0,
		"construction_build_ticks": build_ticks,
		"level": 1,
		"completed_upgrades": [],
		"upgrade_queue": [],
		"production": [],
		"queue": [],
		"damage_remainders": {},
		"income_per_payout": 0,
		"summon_object_id": String(effect.get("object_id", "")),
		"attack": {
			"damage": float(weapon.get("damage", 0.0)),
			"range": float(weapon.get("range_source", 0.0)) * _spellbook_world_scale(),
			"damage_type": String(weapon.get("damage_type", "")),
			"period_ticks": maxi(1, roundi(float(weapon.get("period_ms", 0.0)) / (TICK_SECONDS * 1000.0))),
			"pre_attack_ticks": maxi(0, roundi(float(weapon.get("pre_attack_ms", 0.0)) / (TICK_SECONDS * 1000.0))),
			"cooldown": 0,
			"affects": String(weapon.get("affects", "ENEMIES")),
		},
	}
	_emit_event("power.structure_summon", 0, structure_id, {"team": team, "object_id": String(effect.get("object_id", "")), "build_ticks": build_ticks, "health": health})
	return {"ok": true, "reason": "", "battalions": 0}


func _cast_spellbook_grove(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	_active_groves.append({
		"team": team,
		"point": point,
		"range_sim": float(effect.get("range_source", 0.0)) * _spellbook_world_scale(),
		"armor_mult": float(effect.get("armor_mult", 1.0)),
		"buff_duration_ticks": int(effect.get("buff_duration_ticks", 1)),
		"despawn_tick": tick_index + int(effect.get("lifetime_ticks", 1)),
		"filter": String(effect.get("filter", "")),
	})
	_emit_event("power.grove", 0, 0, {"team": team, "point": [snappedf(point.x, 0.001), snappedf(point.y, 0.001)], "lifetime_ticks": int(effect.get("lifetime_ticks", 0))})
	return {"ok": true, "reason": "", "battalions": 0}


func _cast_spellbook_cloudbreak(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	## Cloud Break: enemy units matching the authored filter are disrupted for
	## the weather duration (SPELL_CLOUDBREAK_DURATION).
	var duration_ticks := int(effect.get("duration_ticks", 1))
	var stunned := 0
	for id in living_ids(1 - team):
		var row: Dictionary = entities[id]
		if not _spellbook_affects(row, String(effect.get("affects", ""))):
			continue
		row["stun_until_tick"] = tick_index + duration_ticks
		row["route"] = []
		row["route_cells"] = []
		row["target_id"] = 0
		row["state"] = "idle"
		stunned += 1
	_emit_event("power.cloudbreak", 0, 0, {"team": team, "weather": String(effect.get("weather", "")), "stunned": stunned, "duration_ticks": duration_ticks})
	return {"ok": true, "reason": "", "battalions": stunned}


func _step_pending_power_effects() -> void:
	if _pending_power_effects.is_empty():
		return
	var remaining: Array[Dictionary] = []
	for effect in _pending_power_effects:
		if tick_index < int(effect.get("fire_tick", 0)):
			remaining.append(effect)
			continue
		match String(effect.get("kind", "")):
			"strike":
				_fire_power_strike(effect)
			"summon":
				_fire_power_summon(effect)
	_pending_power_effects = remaining


func _fire_power_strike(effect: Dictionary) -> void:
	## One detonation: converted damage over converted radius against the
	## authored affects teams (ENEMIES/ALLIES; NEUTRALS has no sim team).
	var point := Vector2(effect.get("point", Vector2.ZERO))
	var radius := float(effect.get("radius_source", 0.0)) * _spellbook_world_scale()
	var amount := float(effect.get("damage", 0.0))
	var caster_team := int(effect.get("team", -1))
	var affects := String(effect.get("affects", "ENEMIES"))
	var teams: Array[int] = []
	if affects.contains("ENEMIES"):
		teams.append(1 - caster_team)
	if affects.contains("ALLIES"):
		teams.append(caster_team)
	var hit_battalions := 0
	var hit_structures := 0
	for affected_team in teams:
		for id in living_ids(affected_team):
			var row: Dictionary = entities[id]
			if Vector2(row.get("position", Vector2.ZERO)).distance_to(point) > radius:
				continue
			_apply_area_damage_to_battalion(id, amount, String(effect.get("damage_type", "")))
			hit_battalions += 1
		for structure_id in structure_ids(affected_team):
			var structure: Dictionary = structures[structure_id]
			if int(structure.get("health", 0)) <= 0:
				continue
			if Vector2(structure.get("position", Vector2.ZERO)).distance_to(point) > radius:
				continue
			_apply_area_damage_to_structure(structure_id, amount, String(effect.get("damage_type", "")))
			hit_structures += 1
	_emit_event("power.strike", 0, 0, {
		"team": caster_team,
		"point": [snappedf(point.x, 0.001), snappedf(point.y, 0.001)],
		"damage": amount,
		"damage_type": String(effect.get("damage_type", "")),
		"battalions": hit_battalions,
		"structures": hit_structures,
	})


func _apply_area_damage_to_battalion(id: int, amount: float, damage_type: String) -> void:
	## AoE payload spread over the battalion's members (front-to-back, stance
	## and formation multipliers like focused combat).
	var row: Dictionary = entities.get(id, {})
	if row.is_empty() or int(row.get("health", 0)) <= 0:
		return
	var total := maxf(0.0, amount * float(_stance_state(row).get("incomingDamageMultiplier", 1.0)) * float(_formation_effects(row).get("incoming_damage_multiplier", 1.0)))
	var health_values: Array = row.get("member_health", [])
	var remaining := total
	for member_index in health_values.size():
		if remaining <= 0.0:
			break
		var current := int(health_values[member_index])
		if current <= 0:
			continue
		var applied := mini(float(current), remaining)
		health_values[member_index] = maxi(0, current - int(round(applied)))
		remaining -= applied
	row["member_health"] = health_values
	var aggregate := 0
	for value in health_values:
		aggregate += int(value)
	row["health"] = aggregate
	row["last_damage_tick"] = tick_index
	if aggregate <= 0:
		row["corpse_expire_tick"] = tick_index + CORPSE_LIFETIME_TICKS
		row["state"] = "death"
		row["target_id"] = 0
		_clear_pending_route(row, true)
		selected_ids.erase(id)
		if base_loop_enabled:
			var row_team := int(row.get("team", -1))
			team_command_points[row_team] = maxi(0, command_points_for_team(row_team) - int(row.get("command_points", 0)))
		prune_control_groups()
		_emit_event("battalion.defeated", 0, id)


func _apply_area_damage_to_structure(structure_id: int, amount: float, damage_type: String) -> void:
	var structure: Dictionary = structures.get(structure_id, {})
	if structure.is_empty():
		return
	var health := int(structure.get("health", 0))
	if health <= 0:
		return
	var new_health := maxi(0, health - int(round(amount)))
	structure["health"] = new_health
	if new_health <= 0:
		structure["health"] = 0


func _fire_power_summon(effect: Dictionary) -> void:
	## Hatch: spawn each converted summon target with its summon lifetime.
	var team := int(effect.get("team", -1))
	var point := Vector2(effect.get("point", Vector2.ZERO))
	var unit_rules_value: Variant = _rules.get("unit_rules", {})
	if typeof(unit_rules_value) != TYPE_DICTIONARY:
		return
	var unit_rules := unit_rules_value as Dictionary
	var spawned: Array = []
	for target_value in Array(effect.get("targets", [])):
		var target := target_value as Dictionary
		var rule: Dictionary = (target.get("rule", {}) as Dictionary).duplicate(true)
		var object_id := String(target.get("object_id", ""))
		if rule.is_empty():
			continue
		unit_rules[object_id] = rule
		# Doc-authored weapon damage type feeds the armor-scalar table the same
		# way playable-unit documents feed it (no inferred types).
		var summon_damage_type := String(rule.get("damage_type", ""))
		if summon_damage_type != "":
			_unit_damage_types[object_id] = summon_damage_type
		var count := int(target.get("count", 1))
		for index in range(count):
			var angle := TAU * float(index) / float(maxi(1, count))
			var spawn_point := point + Vector2(cos(angle), sin(angle)) * (1.0 if count <= 1 else 2.0)
			var entity_id := int(_next_dynamic_id.get(team, 1000))
			_next_dynamic_id[team] = entity_id + 1
			_add_battalion(entity_id, team, spawn_point, String(rule.get("horde_id", object_id)), object_id, object_id, 0)
			if not entities.has(entity_id):
				continue
			var lifetime_ticks := int(target.get("lifetime_ticks", 0))
			if lifetime_ticks > 0:
				_summon_despawn_ticks[entity_id] = tick_index + lifetime_ticks
			spawned.append(entity_id)
	_rules["unit_rules"] = unit_rules
	_emit_event("power.summon", 0, 0, {"team": team, "point": [snappedf(point.x, 0.001), snappedf(point.y, 0.001)], "spawned": spawned})


func _step_summon_despawns() -> void:
	if _summon_despawn_ticks.is_empty():
		return
	var expired: Array = []
	for entity_id_value in _summon_despawn_ticks.keys():
		var entity_id := int(entity_id_value)
		if not entities.has(entity_id):
			expired.append(entity_id)
			continue
		if tick_index < int(_summon_despawn_ticks[entity_id_value]):
			continue
		# The authored summon lifetime ends: the battalion fades (no kill
		# credit — there is no attacker).
		var row: Dictionary = entities[entity_id]
		var health_values: Array = row.get("member_health", [])
		for index in health_values.size():
			health_values[index] = 0
		row["member_health"] = health_values
		row["health"] = 0
		row["corpse_expire_tick"] = tick_index + CORPSE_LIFETIME_TICKS
		row["state"] = "death"
		row["target_id"] = 0
		_clear_pending_route(row, true)
		selected_ids.erase(entity_id)
		if base_loop_enabled:
			var row_team := int(row.get("team", -1))
			team_command_points[row_team] = maxi(0, command_points_for_team(row_team) - int(row.get("command_points", 0)))
		prune_control_groups()
		_emit_event("power.summon_expired", 0, entity_id, {"team": int(row.get("team", -1))})
		expired.append(entity_id)
	for entity_id in expired:
		_summon_despawn_ticks.erase(entity_id)


func _step_grove_auras() -> void:
	if _active_groves.is_empty():
		return
	var living: Array[Dictionary] = []
	for grove in _active_groves:
		if tick_index >= int(grove.get("despawn_tick", -1)):
			continue
		living.append(grove)
		var team := int(grove.get("team", -1))
		var point := Vector2(grove.get("point", Vector2.ZERO))
		var range_sim := float(grove.get("range_sim", 0.0))
		for id in living_ids(team):
			var row: Dictionary = entities[id]
			if Vector2(row.get("position", Vector2.ZERO)).distance_to(point) > range_sim:
				continue
			if not _spellbook_member_affects(row, String(grove.get("filter", ""))):
				continue
			row["grove_armor_until"] = tick_index + int(grove.get("buff_duration_ticks", 1))
			row["grove_armor_mult"] = float(grove.get("armor_mult", 1.0))
	_active_groves = living


func _spellbook_member_affects(row: Dictionary, filter_text: String) -> bool:
	## Aura filters (GENERIC_BUFF_RECIPIENT_OBJECT_FILTER) exclude the HORDE
	## container kind but include its infantry/cavalry members; the sim's
	## battalion is both, so the container distinction drops out of the kind
	## list before the authored terms are evaluated.
	var kinds := _spellbook_object_kinds(row)
	kinds.erase("HORDE")
	var included := false
	for term_value in filter_text.split(" ", false):
		var term := String(term_value)
		if term == "" or term == "NONE":
			continue
		if term == "ENEMIES" or term == "ALLIES":
			continue
		if term.begins_with("-"):
			if kinds.has(term.trim_prefix("-")):
				return false
		elif term.begins_with("+"):
			if kinds.has(term.trim_prefix("+")):
				included = true
		elif term == "ANY":
			included = true
		elif kinds.has(term):
			included = true
	return included


func _grove_armor_factor(target: Dictionary) -> float:
	## Elven Wood taint: the authored ARMOR 50% modifier rides the row while
	## the battalion stands in a living grove (refresh window from the leaf).
	if tick_index < int(target.get("grove_armor_until", -1)):
		return float(target.get("grove_armor_mult", 1.0))
	return 1.0


func _step_structure_weapons() -> void:
	## Summoned structures (Lone Tower): self-rise over the authored just-built
	## duration (no builder — OCL UseJustBuiltFlag), then the converted bow
	## fires on the nearest enemy battalion in range each converted period.
	for structure_id_value in structures.keys():
		var structure_id := int(structure_id_value)
		var structure: Dictionary = structures[structure_id]
		if int(structure.get("health", 0)) <= 0:
			continue
		if String(structure.get("summon_object_id", "")) != "" and float(structure.get("construction_progress", 1.0)) < 1.0:
			var elapsed := int(structure.get("construction_elapsed_ticks", 0)) + 1
			structure["construction_elapsed_ticks"] = elapsed
			var build_ticks := maxi(1, int(structure.get("construction_build_ticks", 1)))
			structure["construction_progress"] = minf(1.0, float(elapsed) / float(build_ticks))
			continue
		if float(structure.get("construction_progress", 1.0)) < 1.0:
			continue
		var attack_value: Variant = structure.get("attack", null)
		if typeof(attack_value) != TYPE_DICTIONARY:
			continue
		var attack := attack_value as Dictionary
		if tick_index < int(attack.get("cooldown", 0)):
			continue
		var team := int(structure.get("team", -1))
		var origin := Vector2(structure.get("position", Vector2.ZERO))
		var best_id := 0
		var best_distance := float(attack.get("range", 0.0))
		for id in living_ids(1 - team):
			var row: Dictionary = entities[id]
			var distance := origin.distance_to(Vector2(row.get("position", Vector2.ZERO)))
			if distance < best_distance:
				best_distance = distance
				best_id = id
		if best_id == 0:
			continue
		var target: Dictionary = entities[best_id]
		var health_values: Array = target.get("member_health", [])
		var member_index := -1
		for index in health_values.size():
			if int(health_values[index]) > 0:
				member_index = index
				break
		if member_index < 0:
			continue
		var amount := maxi(1, roundi(float(attack.get("damage", 0.0))))
		health_values[member_index] = maxi(0, int(health_values[member_index]) - amount)
		target["member_health"] = health_values
		var aggregate := 0
		for value in health_values:
			aggregate += int(value)
		target["health"] = aggregate
		target["last_damage_tick"] = tick_index
		if aggregate <= 0:
			target["corpse_expire_tick"] = tick_index + CORPSE_LIFETIME_TICKS
			target["state"] = "death"
			target["target_id"] = 0
			_clear_pending_route(target, true)
			selected_ids.erase(best_id)
			prune_control_groups()
			_emit_event("battalion.defeated", structure_id, best_id)
		else:
			_emit_event("power.structure_weapon_hit", structure_id, best_id, {"amount": amount})
		attack["cooldown"] = tick_index + int(attack.get("period_ticks", 1)) + int(attack.get("pre_attack_ticks", 0))


func validate_construct_site(builder_ids: Array[int], structure_kind: String, point: Vector2) -> Dictionary:
	# Non-mutating dry run of issue_construct's admission checks so the
	# placement ghost can tint valid/invalid while the player aims.
	return issue_construct(builder_ids, structure_kind, point, true)


# --- Fortress expansion pads (engine-spawned build plots) -------------------
# Pad slot layout around a fortress in local units. PROVISIONAL: retail
# spawns these at the fortress model's BUILDPLOT bones, which no converted
# asset preserves (checked GLBs, map data, docs). Slots keep the retail shape
# (4 corner + 2 side plots ringing the fortress) until bone evidence converts.
# Owner pass: plots sit ATTACHED to the fortress's corners/edges (REF-33,
# tight against the model), not floating in a wide arc — corner diagonals at
# ~8.5 units and sides at 7.5 hug the fortress's ~5-6 unit model half-extent
# while clearing its 4.0 placement radius plus a pad structure's footprint.
const EXPANSION_PAD_LAYOUT := {
	"corner": [Vector2(-6.0, -6.0), Vector2(6.0, -6.0), Vector2(-6.0, 6.0), Vector2(6.0, 6.0)],
	"side": [Vector2(0.0, -7.5), Vector2(0.0, 7.5)],
}
var expansion_pads: Dictionary = {}
var _expansion_build_rules: Dictionary = {}
var _next_expansion_structure_id := 9000


## rules: {kind: {"cost": int, "seconds": float, "health": int, "pad_kinds":
## Array, "name": String, "object_id": String}} — derived by the slice from
## the playable-structure expansion documents; empty rules fail closed.
func configure_expansion_rules(rules: Dictionary) -> void:
	_expansion_build_rules = rules.duplicate(true)


func _seed_expansion_pads_for(fortress_id: int) -> void:
	if expansion_pads.has(fortress_id) or not structures.has(fortress_id):
		return
	var fortress: Dictionary = structures[fortress_id]
	if String(fortress.get("structure_kind", "")) != "fortress":
		return
	var center := Vector2(fortress.get("position", Vector2.ZERO))
	var pads: Array = []
	for pad_kind in EXPANSION_PAD_LAYOUT.keys():
		for slot in range((EXPANSION_PAD_LAYOUT[pad_kind] as Array).size()):
			pads.append({
				"slot": slot,
				"pad_kind": pad_kind,
				"position": center + (EXPANSION_PAD_LAYOUT[pad_kind] as Array)[slot],
				"expansion_structure_id": 0,
			})
	expansion_pads[fortress_id] = pads


func _seed_all_expansion_pads() -> void:
	for structure_id in structure_ids():
		if String((structures[structure_id] as Dictionary).get("structure_kind", "")) == "fortress":
			_seed_expansion_pads_for(structure_id)


func expansion_pad_states(fortress_id: int) -> Array:
	return (expansion_pads.get(fortress_id, []) as Array).duplicate(true)


func expansion_commands_for(fortress_id: int) -> Array:
	## Expansion commands with a free matching pad (retail hides exhausted
	## options). Kinds with no free pad are simply unavailable.
	var result: Array = []
	if not expansion_pads.has(fortress_id):
		return result
	var pads: Array = expansion_pads[fortress_id]
	for kind_value in _expansion_build_rules.keys():
		var rule: Dictionary = _expansion_build_rules[kind_value]
		var pad_kinds: Array = rule.get("pad_kinds", [])
		for pad_value in pads:
			var pad: Dictionary = pad_value
			if int(pad.get("expansion_structure_id", 0)) == 0 and pad_kinds.has(String(pad.get("pad_kind", ""))):
				result.append(String(kind_value))
				break
	return result


func issue_expansion_construct(team: int, fortress_id: int, expansion_kind: String, requested_pad_index: int = -1) -> Dictionary:
	if not base_loop_enabled or winner != -1:
		return {"ok": false, "reason": "match-unavailable"}
	if not structures.has(fortress_id):
		return {"ok": false, "reason": "unknown-structure"}
	var fortress: Dictionary = structures[fortress_id]
	if int(fortress.get("team", -1)) != team:
		return {"ok": false, "reason": "wrong-owner"}
	if int(fortress.get("health", 0)) <= 0 or float(fortress.get("construction_progress", 0.0)) < 1.0:
		return {"ok": false, "reason": "structure-unavailable"}
	var rule: Dictionary = _expansion_build_rules.get(expansion_kind, {})
	if rule.is_empty():
		return {"ok": false, "reason": "unsupported-expansion"}
	if not expansion_pads.has(fortress_id):
		return {"ok": false, "reason": "no-expansion-pads"}
	var pad_kinds: Array = rule.get("pad_kinds", [])
	var pads: Array = expansion_pads[fortress_id]
	var pad_index := -1
	if requested_pad_index >= 0:
		# The player clicked a specific plot: it must be free and accept this
		# expansion kind (corner-only kinds reject side plots and vice versa).
		if requested_pad_index >= pads.size():
			return {"ok": false, "reason": "unknown-pad"}
		var requested_pad: Dictionary = pads[requested_pad_index]
		if int(requested_pad.get("expansion_structure_id", 0)) != 0:
			return {"ok": false, "reason": "pad-occupied"}
		if not pad_kinds.has(String(requested_pad.get("pad_kind", ""))):
			return {"ok": false, "reason": "pad-kind-mismatch"}
		pad_index = requested_pad_index
	else:
		for index in pads.size():
			var pad: Dictionary = pads[index]
			if int(pad.get("expansion_structure_id", 0)) == 0 and pad_kinds.has(String(pad.get("pad_kind", ""))):
				pad_index = index
				break
	if pad_index < 0:
		return {"ok": false, "reason": "no-free-pad"}
	var cost := maxi(0, int(rule.get("cost", 0)))
	if resources_for_team(team) < cost:
		return {"ok": false, "reason": "insufficient-resources", "cost": cost}
	team_resources[team] = resources_for_team(team) - cost
	var structure_id := _next_expansion_structure_id
	_next_expansion_structure_id += 1
	var maximum_health := int(rule.get("health", 1000))
	var position: Vector2 = (pads[pad_index] as Dictionary).get("position", Vector2.ZERO)
	var build_ticks := maxi(1, roundi(float(rule.get("seconds", 20.0)) / TICK_SECONDS))
	# Foundation behavior: the plot builds the expansion itself (no porter).
	structures[structure_id] = {
		"id": structure_id,
		"team": team,
		"kind": "structure",
		"structure_kind": expansion_kind,
		"name": String(rule.get("name", expansion_kind.replace("_", " ").capitalize())),
		"position": position,
		"rally": position + Vector2(2.0, 0.0),
		"health": maximum_health,
		"maximum_health": maximum_health,
		"construction_progress": 0.0,
		"construction_build_ticks": build_ticks,
		"construction_elapsed_ticks": 0,
		"builder_free": true,
		"builder_id": 0,
		"level": 1,
		"completed_upgrades": [],
		"upgrade_queue": [],
		"production": [],
		"queue": [],
		"damage_remainders": {},
		"income_per_payout": 0,
		"expansion_of_fortress": fortress_id,
		"expansion_pad_index": pad_index,
	}
	(pads[pad_index] as Dictionary)["expansion_structure_id"] = structure_id
	_emit_event("construction.started", 0, structure_id, {"team": team, "structure_kind": expansion_kind, "cost": cost, "build_ticks": build_ticks})
	return {"ok": true, "structure_id": structure_id, "cost": cost, "build_ticks": build_ticks}


func queue_unit(team: int, producer: int, unit_type: String = SOLDIER_HORDE_ID) -> Dictionary:
	if not base_loop_enabled or winner != -1:
		return {"ok": false, "reason": "match-unavailable"}
	if not structures.has(producer):
		return {"ok": false, "reason": "unknown-producer"}
	var building: Dictionary = structures[producer]
	if int(building.get("team", -1)) != team:
		return {"ok": false, "reason": "wrong-owner"}
	if int(building.get("health", 0)) <= 0 or float(building.get("construction_progress", 0.0)) < 1.0:
		return {"ok": false, "reason": "producer-unavailable"}
	if not Array(building.get("production", [])).has(unit_type):
		return {"ok": false, "reason": "unsupported-unit"}
	var production_rule: Dictionary = _unit_production_rules.get(unit_type, {})
	if production_rule.is_empty():
		return {"ok": false, "reason": "unsupported-unit"}
	if String(production_rule.get("category", "")) == "hero" and hero_unavailable(team, unit_type):
		return {"ok": false, "reason": "hero-unavailable"}
	var required_upgrades := required_upgrades_for_unit(unit_type, String(building.get("structure_kind", "")))
	for required_upgrade_value in required_upgrades:
		var required_upgrade := String(required_upgrade_value)
		if not Array(building.get("completed_upgrades", [])).has(required_upgrade):
			return {"ok": false, "reason": "missing-upgrade", "required_upgrade": required_upgrade}
	var queue: Array = building.get("queue", [])
	if queue.size() >= maxi(1, int(_rules.get("maximum_queue", 5))):
		return {"ok": false, "reason": "queue-full"}
	var cost := maxi(0, _production_rule_value(unit_type, "cost_rule", "default_cost"))
	# Zero command points is honored when the document says zero (retail
	# porters are free); the historical clamp to one is gone.
	var command_cost := maxi(0, _production_rule_value(unit_type, "command_points_rule", "default_command_points"))
	var queued_command_points := _queued_command_points_for_team(team)
	if resources_for_team(team) < cost:
		return {"ok": false, "reason": "insufficient-resources"}
	if command_points_for_team(team) + queued_command_points + command_cost > command_point_cap:
		return {"ok": false, "reason": "command-point-cap"}
	var build_ticks := maxi(1, _production_rule_value(unit_type, "build_ticks_rule", "default_build_ticks"))
	var production_multiplier := float(building.get("production_multiplier", 1.0))
	if production_multiplier > 0.0 and production_multiplier != 1.0:
		# The producer's authored PRODUCTION level factor scales its authored
		# build time (retail L2/L3 factory speed); rounding stays deterministic.
		build_ticks = maxi(1, roundi(float(build_ticks) / production_multiplier))
	var starts_at := tick_index if queue.is_empty() else int((queue.back() as Dictionary).get("complete_tick", tick_index))
	var item := {
		"unit_type": unit_type,
		"cost": cost,
		"command_points": command_cost,
		"queued_tick": tick_index,
		"start_tick": starts_at,
		"duration_ticks": build_ticks,
		"complete_tick": starts_at + build_ticks,
	}
	for route_value in Array(production_rule.get("producer_routes", [])):
		var route := route_value as Dictionary
		if String(route.get("producer_kind", "")) == String(building.get("structure_kind", "")):
			item["command_id"] = String(route.get("command_id", ""))
			break
	queue.append(item)
	building["queue"] = queue
	team_resources[team] = resources_for_team(team) - cost
	_emit_event("production.queued", producer, 0, {"team": team, "unit_type": unit_type, "command_id": String(item.get("command_id", "")), "complete_tick": int(item["complete_tick"])})
	return {"ok": true, "reason": "", "producer_id": producer, "item": item.duplicate(true)}


func hero_unavailable(team: int, unit_type: String) -> bool:
	var production_rule: Dictionary = _unit_production_rules.get(unit_type, {})
	if String(production_rule.get("category", "")) != "hero":
		return false
	if _completed_hero_identities.has("%d:%s" % [team, unit_type]):
		return true
	for structure_value in structures.values():
		var structure := structure_value as Dictionary
		if int(structure.get("team", -1)) != team:
			continue
		for item_value in Array(structure.get("queue", [])):
			if String((item_value as Dictionary).get("unit_type", "")) == unit_type:
				return true
	for entity_value in entities.values():
		var row := entity_value as Dictionary
		if (
			int(row.get("team", -1)) == team
			and int(row.get("health", 0)) > 0
			and String(row.get("unit_type", "")) == unit_type
		):
			return true
	return false


func production_queue_state(producer: int) -> Array[Dictionary]:
	var rows: Array[Dictionary] = []
	if not structures.has(producer):
		return rows
	var queue: Array = (structures[producer] as Dictionary).get("queue", [])
	for index in range(queue.size()):
		if typeof(queue[index]) != TYPE_DICTIONARY:
			continue
		var item: Dictionary = queue[index]
		var start_tick := int(item.get("start_tick", item.get("queued_tick", tick_index)))
		var complete_tick := int(item.get("complete_tick", start_tick + 1))
		var duration_ticks := maxi(1, int(item.get("duration_ticks", complete_tick - start_tick)))
		var active := index == 0
		var elapsed_ticks := clampi(tick_index - start_tick, 0, duration_ticks) if active else 0
		rows.append({
			"index": index,
			"unit_type": String(item.get("unit_type", SOLDIER_HORDE_ID)),
			"cost": int(item.get("cost", 0)),
			"command_points": int(item.get("command_points", 0)),
			"queued_tick": int(item.get("queued_tick", start_tick)),
			"start_tick": start_tick,
			"complete_tick": complete_tick,
			"duration_ticks": duration_ticks,
			"elapsed_ticks": elapsed_ticks,
			"progress": float(elapsed_ticks) / float(duration_ticks) if active else 0.0,
			"active": active,
		})
	return rows


func cancel_queued_unit(team: int, producer: int, queue_index: int = 0) -> Dictionary:
	if not structures.has(producer):
		return {"ok": false, "reason": "unknown-producer"}
	var building: Dictionary = structures[producer]
	if int(building.get("team", -1)) != team:
		return {"ok": false, "reason": "wrong-owner"}
	var queue: Array = building.get("queue", [])
	if queue_index < 0 or queue_index >= queue.size():
		return {"ok": false, "reason": "unknown-queue-item"}
	var cancelled: Dictionary = queue[queue_index]
	queue.remove_at(queue_index)
	var cursor := tick_index if queue_index == 0 else int((queue[queue_index - 1] as Dictionary).get("complete_tick", tick_index))
	for index in range(queue_index, queue.size()):
		var item: Dictionary = queue[index]
		var prior_start := int(item.get("start_tick", item.get("queued_tick", cursor)))
		var prior_complete := int(item.get("complete_tick", prior_start + 1))
		var duration_ticks := maxi(1, int(item.get("duration_ticks", prior_complete - prior_start)))
		item["start_tick"] = cursor
		item["duration_ticks"] = duration_ticks
		item["complete_tick"] = cursor + duration_ticks
		cursor += duration_ticks
	building["queue"] = queue
	var refund := maxi(0, int(cancelled.get("cost", 0)))
	team_resources[team] = resources_for_team(team) + refund
	_emit_event("production.cancelled", producer, 0, {
		"team": team,
		"unit_type": String(cancelled.get("unit_type", SOLDIER_HORDE_ID)),
		"queue_index": queue_index,
		"refund": refund,
	})
	return {
		"ok": true,
		"reason": "",
		"producer_id": producer,
		"queue_index": queue_index,
		"refund": refund,
		"item": cancelled.duplicate(true),
	}


func select_only(id: int) -> bool:
	if not _is_commandable(id):
		return false
	selected_ids.assign([id])
	_emit_event("voice.select", id, 0, _voice_event_identity(id))
	return true


func select_many(ids: Array[int]) -> int:
	# Multi-select (box selection / same-type selection): keeps only
	# commandable player battalions; one select voice line for the group.
	var accepted: Array[int] = []
	for id in ids:
		if _is_commandable(id) and not accepted.has(id):
			accepted.append(id)
	if accepted.is_empty():
		return 0
	selected_ids.assign(accepted)
	_emit_event("voice.select", accepted[0], 0, _voice_event_identity(accepted[0]))
	return accepted.size()


func toggle_selection(id: int) -> bool:
	if not _is_commandable(id):
		return false
	if selected_ids.has(id):
		selected_ids.erase(id)
	else:
		selected_ids.append(id)
		selected_ids.sort()
		_emit_event("voice.select", id, 0, _voice_event_identity(id))
	return true


func _voice_event_identity(id: int) -> Dictionary:
	# Voice-relevant events always carry the entity's own object id so the audio
	# layer never guesses an entity→object mapping (roster composition is data).
	if not entities.has(id):
		return {}
	var row: Dictionary = entities[id]
	return {"object_id": String(row.get("object_id", "")), "form": String(row.get("form", ""))}


func clear_selection() -> void:
	selected_ids.clear()


func assign_control_group(group: int, ids: Array[int]) -> Dictionary:
	if group < 1 or group > 9:
		return {"ok": false, "reason": "invalid-group", "group": group, "entity_ids": []}
	var assigned: Array[int] = []
	var candidates: Array[int] = ids.duplicate()
	candidates.sort()
	for id in candidates:
		if assigned.has(id) or not _is_living_player(id):
			continue
		assigned.append(id)
	control_groups[group] = assigned.duplicate()
	return {"ok": true, "reason": "", "group": group, "entity_ids": assigned}


func recall_control_group(group: int) -> Array[int]:
	if group < 1 or group > 9:
		return []
	_prune_control_group(group)
	var recalled: Array[int] = []
	for value: Variant in Array(control_groups.get(group, [])):
		recalled.append(int(value))
	return recalled


func prune_control_groups() -> void:
	for group in range(1, 10):
		_prune_control_group(group)


func reset_control_groups() -> void:
	control_groups.clear()
	for group in range(1, 10):
		control_groups[group] = []


func control_groups_snapshot() -> Array[Dictionary]:
	var rows: Array[Dictionary] = []
	for group in range(1, 10):
		var ids: Array[int] = []
		for value: Variant in Array(control_groups.get(group, [])):
			ids.append(int(value))
		ids.sort()
		rows.append({"group": group, "entity_ids": ids})
	return rows


# Keep a singular spelling available to presentation callers without making
# the serialized contract depend on a Dictionary's key ordering.
func control_group_snapshot() -> Array[Dictionary]:
	return control_groups_snapshot()


func issue_move(ids: Array[int], destination: Vector2, ack_kind: String = "order.move", team: int = PLAYER_TEAM) -> int:
	var accepted_ids: Array[int] = []
	last_route_rejection = ""
	for id in ids:
		if accepted_ids.has(id) or not _is_commandable_for_team(id, team):
			continue
		var row: Dictionary = entities[id]
		if not _assign_route(row, destination):
			continue
		row["target_id"] = 0
		row["target_kind"] = "battalion"
		row["attack_windup"] = 0
		row["attack_move"] = false
		_clear_member_targets(row)
		row["state"] = "run"
		row["order_kind"] = "move"
		accepted_ids.append(id)
	if not accepted_ids.is_empty():
		_stamp_order_sequence(accepted_ids)
		last_route_rejection = ""
		_emit_event(ack_kind, accepted_ids[0], 0, _voice_event_identity(accepted_ids[0]))
	return accepted_ids.size()


func issue_attack(ids: Array[int], target_id: int, team: int = PLAYER_TEAM) -> int:
	var target_kind := "battalion" if entities.has(target_id) else ("structure" if structures.has(target_id) else "")
	if target_kind == "":
		return 0
	var target: Dictionary = entities[target_id] if target_kind == "battalion" else structures[target_id]
	if int(target["health"]) <= 0:
		return 0
	var accepted_ids: Array[int] = []
	last_route_rejection = ""
	for id in ids:
		if accepted_ids.has(id) or not _is_commandable_for_team(id, team):
			continue
		var row: Dictionary = entities[id]
		if int(row["team"]) == int(target["team"]):
			continue
		if target_kind == "battalion" and not _can_engage_battalion(row, target):
			continue
		if not _assign_route(row, Vector2(target["position"])):
			continue
		row["target_id"] = target_id
		row["target_kind"] = target_kind
		row["attack_windup"] = 0
		row["attack_move"] = false
		_clear_member_targets(row)
		row["state"] = "run"
		row["order_kind"] = "attack"
		accepted_ids.append(id)
	if not accepted_ids.is_empty():
		_stamp_order_sequence(accepted_ids)
		last_route_rejection = ""
		var ack := _voice_event_identity(accepted_ids[0])
		ack["target_kind"] = target_kind
		_emit_event("voice.attack", accepted_ids[0], target_id, ack)
		_emit_music("battle")
	return accepted_ids.size()


func issue_attack_move(ids: Array[int], destination: Vector2, team: int = PLAYER_TEAM) -> int:
	# Retail answers an attack-move with an attack-class acknowledgement, not
	# the plain move line; the sim keeps the order kind distinct so the audio
	# layer picks the attack ack without guessing.
	var accepted := issue_move(ids, destination, "voice.attack", team)
	if accepted <= 0:
		return 0
	for id in ids:
		if not _is_commandable_for_team(id, team):
			continue
		var row: Dictionary = entities[id]
		if Vector2(row.get("destination", row["position"])).is_equal_approx(destination):
			row["attack_move"] = true
			row["attack_move_destination"] = destination
			row["order_kind"] = "attack_move"
	return accepted


func issue_stop(ids: Array[int], team: int = PLAYER_TEAM) -> int:
	var accepted_ids: Array[int] = []
	for id in ids:
		if accepted_ids.has(id) or not _is_commandable_for_team(id, team):
			continue
		var row: Dictionary = entities[id]
		row["target_id"] = 0
		row["target_kind"] = "battalion"
		row["attack_windup"] = 0
		row["attack_move"] = false
		_clear_member_attack_schedule(row)
		_clear_member_targets(row)
		_clear_pending_route(row, true)
		row["state"] = "idle"
		row["order_kind"] = ""
		accepted_ids.append(id)
	if not accepted_ids.is_empty():
		_stamp_order_sequence(accepted_ids)
		_emit_event("order.stop", accepted_ids[0], 0)
	return accepted_ids.size()


func issue_toggle_stance(ids: Array[int], team: int = PLAYER_TEAM) -> int:
	var accepted_ids: Array[int] = []
	for id in ids:
		if accepted_ids.has(id) or not _is_commandable_for_team(id, team):
			continue
		var row: Dictionary = entities[id]
		var index := STANCE_ORDER.find(String(row.get("stance", "Battle")))
		row["stance"] = STANCE_ORDER[posmod(index + 1, STANCE_ORDER.size())]
		accepted_ids.append(id)
	if not accepted_ids.is_empty():
		_stamp_order_sequence(accepted_ids)
		_emit_event("order.stance", accepted_ids[0], 0, {"stance": String((entities[accepted_ids[0]] as Dictionary)["stance"])})
	return accepted_ids.size()


func issue_set_stance(ids: Array[int], stance: String, team: int = PLAYER_TEAM) -> int:
	if not STANCE_ORDER.has(stance):
		return 0
	var accepted_ids: Array[int] = []
	for id in ids:
		if accepted_ids.has(id) or not _is_commandable_for_team(id, team):
			continue
		(entities[id] as Dictionary)["stance"] = stance
		accepted_ids.append(id)
	if not accepted_ids.is_empty():
		_stamp_order_sequence(accepted_ids)
		_emit_event("order.stance", accepted_ids[0], 0, {"stance": stance})
	return accepted_ids.size()


func issue_toggle_formation(ids: Array[int], team: int = PLAYER_TEAM) -> int:
	var accepted_ids: Array[int] = []
	for id in ids:
		if accepted_ids.has(id) or not _is_commandable_for_team(id, team):
			continue
		var row: Dictionary = entities[id]
		if bool(row.get("is_builder", false)):
			continue
		var index := FORMATION_ORDER.find(String(row.get("formation_mode", "Line")))
		var next_mode := FORMATION_ORDER[posmod(index + 1, FORMATION_ORDER.size())]
		row["formation_mode"] = next_mode
		_apply_formation_mode(row)
		accepted_ids.append(id)
	if not accepted_ids.is_empty():
		_stamp_order_sequence(accepted_ids)
		_emit_event(
			"order.formation",
			accepted_ids[0],
			0,
			{"formation": String((entities[accepted_ids[0]] as Dictionary)["formation_mode"])}
		)
	return accepted_ids.size()


func issue_set_formation(ids: Array[int], formation: String, team: int = PLAYER_TEAM) -> int:
	if not FORMATION_ORDER.has(formation):
		return 0
	var accepted_ids: Array[int] = []
	for id in ids:
		if accepted_ids.has(id) or not _is_commandable_for_team(id, team):
			continue
		var row: Dictionary = entities[id]
		if bool(row.get("is_builder", false)):
			continue
		row["formation_mode"] = formation
		_apply_formation_mode(row)
		accepted_ids.append(id)
	if not accepted_ids.is_empty():
		_stamp_order_sequence(accepted_ids)
		_emit_event("order.formation", accepted_ids[0], 0, {"formation": formation})
	return accepted_ids.size()


func _apply_formation_mode(row: Dictionary) -> void:
	var base: Array = row.get("formation_positions_base", row.get("formation_positions", [])) as Array
	if base.is_empty():
		return
	var scale := float(FORMATION_SPACING.get(String(row.get("formation_mode", "Line")), 1.0))
	var scaled: Array = []
	for slot_value in base:
		if typeof(slot_value) != TYPE_VECTOR3:
			scaled.append(Vector3.ZERO)
			continue
		var slot: Vector3 = slot_value
		scaled.append(Vector3(slot.x * scale, slot.y, slot.z * scale))
	row["formation_positions"] = scaled


func advance(ticks: int) -> void:
	for _index in range(maxi(0, ticks)):
		tick()


func tick() -> void:
	if clock_paused and not _has_pending_resume_command():
		# Preserve the single-player clock-stop seam. Lockstep advances paused
		# command ticks only after a deterministic resume has been scheduled.
		return
	tick_index += 1
	_apply_pending_commands_for_tick(tick_index)
	if clock_paused:
		# A lockstep pause still consumes deterministic command ticks so a
		# scheduled resume can execute. Gameplay below this seam remains frozen.
		return
	if winner != -1:
		_cleanup_expired_corpses()
		return
	if base_loop_enabled:
		_step_economy()
		_step_structure_upgrades()
		_step_battalion_upgrades()
		_step_production()
	_step_pending_power_effects()
	_step_grove_auras()
	_step_summon_despawns()
	_step_structure_weapons()
	if ai_enabled and tick_index % AI_CONTROLLER_BASE_INTERVAL == 0:
		_update_ai_controllers()
	for id in entity_ids():
		_step_entity(id)
	_step_battalion_separation()
	_step_construction()
	_step_hero_regeneration()
	_step_hero_abilities()
	_cleanup_expired_corpses()
	_resolve_victory()


func _has_pending_resume_command() -> bool:
	for command_tick_value in _pending_commands.keys():
		if int(command_tick_value) <= tick_index:
			continue
		for command_value in _pending_commands[command_tick_value] as Array:
			if String((command_value as Dictionary).get("type", "")) == "resume":
				return true
	return false


# Battalions that end up on top of each other jam movement and read as one
# blob. A gentle symmetric push separates overlapping pairs without breaking
# engagements or construction.
const BATTALION_SEPARATION_RADIUS := 1.4
const BATTALION_SEPARATION_PUSH := 0.35


func _step_battalion_separation() -> void:
	var ids := entity_ids()
	for index in range(ids.size()):
		var a: Dictionary = entities[ids[index]]
		# Only settled battalions get nudged apart: pushing marching columns
		# around mid-route disrupts ford crossings and formation moves.
		if int(a.get("health", 0)) <= 0 or String(a.get("state", "")) != "idle" or bool(a.get("flying", false)):
			continue
		for other_index in range(index + 1, ids.size()):
			var b: Dictionary = entities[ids[other_index]]
			if int(b.get("health", 0)) <= 0 or String(b.get("state", "")) != "idle" or bool(b.get("flying", false)):
				continue
			var a_position := Vector2(a["position"])
			var b_position := Vector2(b["position"])
			var offset := b_position - a_position
			var distance := offset.length()
			if distance >= BATTALION_SEPARATION_RADIUS or distance <= 0.001:
				continue
			# Pairs actively fighting each other stay engaged.
			if int(a.get("target_id", 0)) == int(b.get("id", 0)) or int(b.get("target_id", 0)) == int(a.get("id", 0)):
				continue
			var push := offset / distance * minf(BATTALION_SEPARATION_PUSH, (BATTALION_SEPARATION_RADIUS - distance) * 0.5)
			# Only separate onto ground units can actually stand on — pushing a
			# battalion into water/cliff cells strands it (the ford chokepoints
			# are exactly where pile-ups happen).
			var a_target := a_position - push
			var b_target := b_position + push
			if _position_walkable(a_target):
				a["position"] = a_target
			if _position_walkable(b_target):
				b["position"] = b_target


func _position_walkable(position: Vector2) -> bool:
	if route_provider == null or not route_provider.has_method("is_local_inside_navigation"):
		return true
	if not bool(route_provider.call("is_local_inside_navigation", position)):
		return false
	if route_provider.has_method("is_navigation_walkable") and route_provider.has_method("local_to_grid_cell"):
		return bool(route_provider.call("is_navigation_walkable", route_provider.call("local_to_grid_cell", position)))
	return true


# Provisional AutoHealBehavior shape (exact retail magnitudes are an M3 INI
# extraction item): heroes regenerate out of combat, dead members stay dead.
const HERO_REGEN_OUT_OF_COMBAT_SECONDS := 5.0
const HERO_REGEN_PERCENT_PER_SECOND := 0.01
var _has_hero_units := false


func _step_hero_regeneration() -> void:
	if not _has_hero_units:
		return
	for id in entity_ids():
		var row: Dictionary = entities[id]
		if String(row.get("category", "")) != "hero":
			continue
		var health := int(row.get("health", 0))
		var maximum := int(row.get("maximum_health", 0))
		if health <= 0 or health >= maximum:
			continue
		var ticks_since_damage := tick_index - int(row.get("last_damage_tick", -1000000))
		if float(ticks_since_damage) * TICK_SECONDS < HERO_REGEN_OUT_OF_COMBAT_SECONDS:
			continue
		var amount := maxi(1, roundi(float(maximum) * HERO_REGEN_PERCENT_PER_SECOND * TICK_SECONDS))
		var health_values: Array = row.get("member_health", [])
		var member_maximum := int(row.get("member_maximum_health", 0))
		var remaining := amount
		for index in range(health_values.size()):
			if remaining <= 0:
				break
			var current := int(health_values[index])
			if current <= 0 or current >= member_maximum:
				continue
			var healed := mini(remaining, member_maximum - current)
			health_values[index] = current + healed
			remaining -= healed
		row["member_health"] = health_values
		var aggregate := 0
		for value in health_values:
			aggregate += int(value)
		row["health"] = aggregate


# --- Hero ability surface (converted SPECIAL_POWER rows) ---
# Per-hero cast surface for converter-emitted abilities: cooldowns, authored
# level gates, and the effect kinds the converted leaves actually provide
# (weapon blasts, heals, OCL summons, attribute modifiers with duration).
# Abilities needing unimplemented systems stay unavailable with their
# converted reason; nothing is faked.
var _unit_ability_rules: Dictionary = {}

# Object-kind vocabulary for authored HealAffects/AttributeModifierAffects
# filters, mapped from sim categories.
const ABILITY_CATEGORY_KINDS := {
	"infantry": "INFANTRY",
	"ranged-infantry": "INFANTRY",
	"cavalry": "CAVALRY",
	"hero": "HERO",
	"monster": "MONSTER",
	"siege": "SIEGEENGINE",
}
const ABILITY_SUMMON_OFFSET_STEP := 1.75
const ABILITY_SUMMON_MAX_COUNT := 8
const ABILITY_ARMOR_CAP := 0.95
# Leadership auras recompute on a fixed cadence (deterministic, ascending
# entity ids). Each grant expires one interval after its last refresh, so an
# aura drops within half a second of the hero dying, being knocked down, or
# the ally leaving the radius.
const ABILITY_AURA_INTERVAL_TICKS := 5


func _scaled_ability_rules(rules: Array[Dictionary], source_scale: float) -> Array[Dictionary]:
	## Bind converted ability rows to this map's source scale. Retail ranges
	## arrive in source units and scale exactly like combat attack ranges.
	var output: Array[Dictionary] = []
	for rule in rules:
		var scaled := (rule as Dictionary).duplicate(true)
		var effect: Dictionary = (scaled.get("effect", {}) as Dictionary).duplicate(true)
		var scale := source_scale if source_scale > 0.0 else 1.0
		match String(effect.get("kind", "")):
			"weapon-blast":
				effect["damage_radius"] = float(effect.get("damageRadius", 0.0)) * scale
				var range_source := float(effect.get("attackRange", effect.get("startAbilityRange", 0.0)))
				effect["range"] = range_source * scale
				# Converter-emitted knockback magnitudes (source units) bind to
				# map scale like every other range. No compiled Men ability
				# carries these yet (MetaImpactNugget extraction is importer
				# follow-up); until then the keys stay 0 and the blast deals
				# damage without a shockwave — fail-closed, nothing invented.
				effect["knockback_radius"] = float(effect.get("knockbackRadius", 0.0)) * scale
				effect["knockback_strength"] = float(effect.get("knockbackStrength", 0.0)) * scale
			"heal":
				effect["radius_scaled"] = float(effect.get("radius", 0.0)) * scale
			"attribute-modifier":
				effect["duration_ticks"] = maxi(1, roundi(float(effect.get("durationMs", 0.0)) / (TICK_SECONDS * 1000.0)))
				effect["range_scaled"] = float(effect.get("range", 0.0)) * scale
			"leadership-aura":
				# AttributeModifierAuraUpdate: Range binds to map scale like every
				# other authored range; the modifier list itself is scale-free.
				effect["range_scaled"] = float(effect.get("range", 0.0)) * scale
			"terror":
				# FearNugget/TerrorSpecialPower: radius and the optional scatter
				# displacement are source units; FearDuration is milliseconds.
				effect["radius_scaled"] = float(effect.get("radius", 0.0)) * scale
				effect["duration_ticks"] = maxi(1, roundi(float(effect.get("durationMs", 0.0)) / (TICK_SECONDS * 1000.0)))
				effect["scatter_strength_scaled"] = float(effect.get("scatterStrength", 0.0)) * scale
			"mount-toggle":
				# Mounted LocomotorSet speed is source units, like every speed.
				effect["mounted_speed_scaled"] = float(effect.get("mountedSpeed", 0.0)) * scale
			"capture-building":
				# StartAbilityRange gates the cast like an attack range; the
				# channel is the authored unpack + preparation + pack envelope.
				effect["range"] = float(effect.get("startAbilityRange", 0.0)) * scale
				var channel_ms := float(effect.get("unpackMs", 0.0)) + float(effect.get("preparationMs", 0.0)) + float(effect.get("packMs", 0.0))
				effect["channel_ticks"] = maxi(1, roundi(channel_ms / (TICK_SECONDS * 1000.0)))
		scaled["effect"] = effect
		output.append(scaled)
	return output


func _attach_hero_ability_state(row: Dictionary) -> void:
	## Spawn-time ability state for one hero entity. Retail heroes enter at
	## rank 1; the authored level gates read this row's live level, which the
	## experience pipeline raises as the hero gains ranks.
	var states: Dictionary = {}
	for rule_value in _unit_ability_rules.get(String(row.get("unit_type", "")), []) as Array:
		var rule := rule_value as Dictionary
		states[String(rule.get("ability_id", ""))] = {
			"cooldown_ready_tick": 0,
			"cooldown_ticks": int(rule.get("cooldown_ticks", 0)),
		}
	row["level"] = 1
	row["ability_states"] = states


# --- Experience / veterancy ---
# Per-entity XP pool driven by the converted ExperienceLevel chains. A member
# kill pays the victim's authored ExperienceAward at the victim's current
# level; levels unlock at the authored cumulative thresholds; each level's
# authored permanent modifiers (HEALTH / DAMAGE_ADD) fold into the per-member
# base stats. Revived heroes re-enter through production at rank 1 with an
# empty pool, matching retail. Units retail never authored a chain for carry
# no rule: they pay the recorded default (zero) and the fallback is recorded
# per victim unit type instead of inventing an award.
var _unit_experience_rules: Dictionary = {}
var _experience_unauthored_victims: Dictionary = {}


func _attach_experience_state(row: Dictionary) -> void:
	## Spawn-time XP pool for one entity. Fielded units enter at rank 1 with no
	## experience; retail summons whose chain starts at the top rank (ring
	## hero, Treebeard) enter at their authored rank. Produced (revived)
	## heroes restart at the chain's entry rank, matching retail.
	var rule: Dictionary = _unit_experience_rules.get(String(row.get("unit_type", "")), {})
	if rule.is_empty():
		return
	row["level"] = int(rule.get("initial_rank", 1))
	row["experience_xp"] = 0
	row["experience_max_level"] = int(rule.get("max_level", 1))


func experience_rule_for_unit(unit_type: String) -> Dictionary:
	return (_unit_experience_rules.get(unit_type, {}) as Dictionary).duplicate(true)


func _experience_level_row(rule: Dictionary, rank: int) -> Dictionary:
	for row_value in Array(rule.get("levels", [])):
		var row := row_value as Dictionary
		if int(row.get("rank", 0)) == rank:
			return row
	return {}


func experience_state(entity_id: int) -> Dictionary:
	if not entities.has(entity_id):
		return {}
	var row: Dictionary = entities[entity_id]
	var rule: Dictionary = _unit_experience_rules.get(String(row.get("unit_type", "")), {})
	if rule.is_empty():
		return {}
	var level := int(row.get("level", 1))
	var state: Dictionary = {
		"level": level,
		"xp": int(row.get("experience_xp", 0)),
		"max_level": int(rule.get("max_level", 1)),
	}
	for row_value in Array(rule.get("levels", [])):
		var level_row := row_value as Dictionary
		if int(level_row.get("rank", 0)) > level:
			state["next_threshold"] = int(level_row.get("required_experience", 0))
			break
	return state


func experience_unauthored_victims() -> Array[String]:
	## Victim unit types whose kills paid the recorded default because retail
	## authors no ExperienceLevel chain for them (never an invented award).
	var output: Array[String] = []
	for value in _experience_unauthored_victims.keys():
		output.append(String(value))
	output.sort()
	return output


func _award_member_kill_experience(attacker_id: int, target: Dictionary) -> void:
	if not entities.has(attacker_id):
		return
	var attacker: Dictionary = entities[attacker_id]
	if int(attacker.get("health", 0)) <= 0:
		return
	var victim_rule: Dictionary = _unit_experience_rules.get(String(target.get("unit_type", "")), {})
	if victim_rule.is_empty():
		_experience_unauthored_victims[String(target.get("unit_type", ""))] = true
		return
	var victim_level := clampi(
		int(target.get("level", 1)),
		int(victim_rule.get("initial_rank", 1)),
		int(victim_rule.get("max_level", 1))
	)
	var award := int(_experience_level_row(victim_rule, victim_level).get("experience_award", 0))
	if award <= 0:
		return
	_award_experience(attacker, award)


func _award_experience(row: Dictionary, amount: int) -> void:
	var rule: Dictionary = _unit_experience_rules.get(String(row.get("unit_type", "")), {})
	if rule.is_empty() or amount <= 0 or int(row.get("health", 0)) <= 0:
		return
	# Leadership EXPERIENCE modifiers (and any timed EXPERIENCE buff) scale
	# the recipient's XP intake — retail's 200% leadership experience rule.
	var experience_factor := _timed_modifier_product(row, "EXPERIENCE")
	if experience_factor != 1.0:
		amount = maxi(1, roundi(float(amount) * experience_factor))
	var max_level := int(rule.get("max_level", 1))
	var level := int(row.get("level", 1))
	# The pool keeps counting at the authored cap (retail's tracker never
	# stops); only the level-ups stop at the last authored rank.
	var xp := int(row.get("experience_xp", 0)) + amount
	row["experience_xp"] = xp
	# Authored thresholds are cumulative; the next rank is the smallest
	# authored rank above the live one (chains are not always 1..N).
	while level < max_level:
		var next_row: Dictionary = {}
		for row_value in Array(rule.get("levels", [])):
			var candidate := row_value as Dictionary
			if int(candidate.get("rank", 0)) > level:
				next_row = candidate
				break
		if next_row.is_empty() or xp < int(next_row.get("required_experience", 0)):
			break
		level = int(next_row.get("rank", level + 1))
		_apply_experience_level_effects(row, next_row)
	if level != int(row.get("level", 1)):
		row["level"] = level
		_emit_event("battalion.level_up", int(row.get("id", 0)), 0, {
			"team": int(row.get("team", -1)),
			"unit_type": String(row.get("unit_type", "")),
			"level": level,
		})


func _apply_experience_level_effects(row: Dictionary, level_row: Dictionary) -> void:
	## SAGE level modifiers are permanent: additive kinds fold into the
	## per-member base stat (living members' current health rises by the same
	## authored amount), exactly the authored magnitudes, never scaled.
	var health_add := roundi(float(level_row.get("health_add", 0.0)))
	var damage_add := roundi(float(level_row.get("damage_add", 0.0)))
	if health_add != 0:
		row["member_maximum_health"] = int(row.get("member_maximum_health", 0)) + health_add
		row["maximum_health"] = int(row["member_maximum_health"]) * int(row.get("member_count", 1))
		var health_values: Array = row.get("member_health", [])
		for index in range(health_values.size()):
			if int(health_values[index]) > 0:
				health_values[index] = mini(int(row["member_maximum_health"]), int(health_values[index]) + health_add)
		row["member_health"] = health_values
		var aggregate := 0
		for value in health_values:
			aggregate += int(value)
		row["health"] = aggregate
	if damage_add != 0:
		row["member_damage"] = int(row.get("member_damage", 0)) + damage_add
		row["damage"] = int(row["member_damage"]) * int(row.get("member_count", 1))


func ability_rules_for_unit(unit_type: String) -> Array:
	return (_unit_ability_rules.get(unit_type, []) as Array).duplicate(true)


func ability_states_for(hero_id: int) -> Dictionary:
	if not entities.has(hero_id):
		return {}
	return ((entities[hero_id] as Dictionary).get("ability_states", {}) as Dictionary).duplicate(true)


func _ability_object_kind_tokens(row: Dictionary) -> Array[String]:
	var tokens: Array[String] = ["ANY"]
	var kind := String(ABILITY_CATEGORY_KINDS.get(String(row.get("category", "")), ""))
	if kind != "":
		tokens.append(kind)
	if String(row.get("horde_id", "")) != "" and String(row.get("unit_type", "")) != String(row.get("horde_id", "")):
		tokens.append("HORDE")
	return tokens


func _ability_filter_accepts(row: Dictionary, filter_text: String) -> bool:
	## Authored HealAffects/KindOf subset: ANY, +include, -exclude terms.
	if filter_text.strip_edges() == "":
		return true
	var kinds := _ability_object_kind_tokens(row)
	var included := false
	for term_value in filter_text.split(" ", false):
		var term := String(term_value)
		if term == "" or term == "NONE" or term == "ALLIES" or term == "ENEMIES":
			continue
		if term.begins_with("-"):
			if kinds.has(term.trim_prefix("-")):
				return false
		elif term.begins_with("+"):
			if kinds.has(term.trim_prefix("+")):
				included = true
		elif kinds.has(term):
			included = true
	return included


func cast_ability(hero_id: int, ability_id: String, target_point: Vector2) -> Dictionary:
	## Cast one converted hero ability, validating ownership, evidence,
	## cooldown, and the authored level gate before applying the bound effect.
	if not entities.has(hero_id):
		return {"ok": false, "reason": "unknown-hero"}
	var row: Dictionary = entities[hero_id]
	if String(row.get("category", "")) != "hero":
		return {"ok": false, "reason": "not-a-hero"}
	if int(row.get("health", 0)) <= 0:
		return {"ok": false, "reason": "hero-defeated"}
	var rule: Dictionary = {}
	for rule_value in _unit_ability_rules.get(String(row.get("unit_type", "")), []) as Array:
		if String((rule_value as Dictionary).get("ability_id", "")) == ability_id:
			rule = rule_value as Dictionary
			break
	if rule.is_empty():
		return {"ok": false, "reason": "unknown-ability"}
	if not bool(rule.get("castable", false)):
		var unavailable := String(rule.get("availability_reason", ""))
		return {"ok": false, "reason": "unimplemented" if unavailable == "" else "unimplemented:%s" % unavailable}
	if not bool(rule.get("level_gate_resolved", true)):
		return {"ok": false, "reason": "level-gate-unresolved"}
	var level := int(row.get("level", 1))
	var required_level := int(rule.get("required_level", 1))
	if level < required_level:
		return {"ok": false, "reason": "level-required", "level": level, "required_level": required_level}
	var states: Dictionary = row.get("ability_states", {}) as Dictionary
	var state: Dictionary = states.get(ability_id, {}) as Dictionary
	var ready_tick := int(state.get("cooldown_ready_tick", 0))
	if tick_index < ready_tick:
		return {"ok": false, "reason": "cooldown-active", "ready_tick": ready_tick}
	var effect: Dictionary = rule.get("effect", {}) as Dictionary
	var targeting := String(rule.get("targeting", "self"))
	var hero_position := Vector2(row.get("position", Vector2.ZERO))
	if targeting != "self":
		var range_limit := float(effect.get("range", 0.0))
		if range_limit > 0.0 and hero_position.distance_to(target_point) > range_limit:
			return {"ok": false, "reason": "out-of-range"}
	var effect_kind := String(effect.get("kind", "none"))
	var result: Dictionary = {}
	match effect_kind:
		"weapon-blast":
			if targeting == "enemy-object" and _ability_enemies_near(int(row.get("team", -1)), target_point, maxf(float(effect.get("damage_radius", 0.0)), 1.5)).is_empty():
				return {"ok": false, "reason": "no-target"}
			result = _apply_ability_weapon_blast(row, effect, target_point)
		"heal":
			var epicenter := target_point if targeting == "point" else hero_position
			result = _apply_ability_heal(row, effect, epicenter)
		"attribute-modifier":
			result = _apply_ability_modifier(row, ability_id, effect)
		"summon":
			result = _apply_ability_summon(row, effect, target_point if targeting == "point" else hero_position)
		"weapon-toggle":
			result = _apply_ability_weapon_toggle(row, effect)
		"terror":
			result = _apply_ability_terror(row, ability_id, effect)
		"mount-toggle":
			result = _apply_ability_mount_toggle(row, effect)
		"capture-building":
			result = _apply_ability_capture_building(row, effect, target_point)
		_:
			return {"ok": false, "reason": "no-effect"}
	if not bool(result.get("ok", false)):
		return result
	state["cooldown_ready_tick"] = tick_index + int(rule.get("cooldown_ticks", 0))
	states[ability_id] = state
	row["ability_states"] = states
	_emit_event("ability.cast", hero_id, 0, {
		"team": int(row.get("team", -1)),
		"ability_id": ability_id,
		"special_power_id": String(rule.get("special_power_id", "")),
		"effect_kind": effect_kind,
		"affected": int(result.get("affected", 0)),
		"summoned": result.get("summoned", []),
		"sound_id": String(rule.get("initiate_sound_id", rule.get("unit_specific_sound_id", ""))),
		"point": [snappedf(target_point.x, 0.001), snappedf(target_point.y, 0.001)],
	})
	return result


func _ability_enemies_near(team: int, point: Vector2, radius: float) -> Array[int]:
	var result: Array[int] = []
	for id in entity_ids():
		var row: Dictionary = entities[id]
		if int(row.get("team", -1)) == team or int(row.get("health", 0)) <= 0:
			continue
		if Vector2(row.get("position", Vector2.ZERO)).distance_to(point) <= radius:
			result.append(id)
	return result


func _apply_ability_weapon_blast(hero_row: Dictionary, effect: Dictionary, point: Vector2) -> Dictionary:
	## Converted SpecialWeapon blast: base DamageNugget damage at the target
	## point, radius-scaled when the weapon authors one.
	var attacker_id := int(hero_row.get("id", 0))
	var team := int(hero_row.get("team", -1))
	var radius := float(effect.get("damage_radius", 0.0))
	var damage := maxi(1, int(effect.get("damage", 1)))
	var affected := 0
	if radius > 0.0:
		for id in _ability_enemies_near(team, point, radius):
			_apply_damage(attacker_id, id, damage, "battalion")
			affected += 1
		for structure_id in structure_ids():
			var structure: Dictionary = structures[structure_id]
			if int(structure.get("team", -1)) == team or int(structure.get("health", 0)) <= 0:
				continue
			if Vector2(structure.get("position", Vector2.ZERO)).distance_to(point) <= radius:
				_apply_structure_damage(attacker_id, structure_id, damage)
				affected += 1
	else:
		var best_id := -1
		var best_distance := 2.0
		for id in _ability_enemies_near(team, point, 2.0):
			var distance := Vector2((entities[id] as Dictionary).get("position", Vector2.ZERO)).distance_to(point)
			if distance <= best_distance:
				best_distance = distance
				best_id = id
		if best_id >= 0:
			_apply_damage(attacker_id, best_id, damage, "battalion")
			affected = 1
	# Blast shockwave: abilities whose compiled rule authors knockback fields
	# throw enemies radially away from the impact point. Damage stays on the
	# damage_radius path above (knockback itself adds none).
	var knockback_radius := float(effect.get("knockback_radius", 0.0))
	var knockback_strength := float(effect.get("knockback_strength", 0.0))
	if knockback_radius > 0.0 and knockback_strength > 0.0:
		_apply_knockback(point, knockback_radius, knockback_strength, team, 0, "ability-blast", attacker_id)
	return {"ok": true, "reason": "", "effect": "weapon-blast", "affected": affected}


func _apply_ability_heal(hero_row: Dictionary, effect: Dictionary, epicenter: Vector2) -> Dictionary:
	## Converted heal leaf: flat burst (AutoHealBehavior) or max-health
	## fraction (PlayerHealSpecialPower) inside the authored radius, honoring
	## the authored kind filter.
	var hero_id := int(hero_row.get("id", 0))
	var team := int(hero_row.get("team", -1))
	var radius := float(effect.get("radius_scaled", 0.0))
	var amount := float(effect.get("amount", 0.0))
	var flat := String(effect.get("amountKind", "flat")) == "flat"
	var only_others := bool(effect.get("onlyOthers", false))
	var filter_text := String(effect.get("affects", ""))
	var healed := 0
	for id in living_ids(team):
		if only_others and id == hero_id:
			continue
		var row: Dictionary = entities[id]
		if not _ability_filter_accepts(row, filter_text):
			continue
		if radius > 0.0 and Vector2(row.get("position", Vector2.ZERO)).distance_to(epicenter) > radius:
			continue
		var maximum_member := int(row.get("member_maximum_health", 1))
		var amount_i := maxi(1, roundi(amount)) if flat else maxi(1, roundi(float(maximum_member) * amount))
		var health_values: Array = row.get("member_health", [])
		var restored := false
		for member_index in health_values.size():
			var current := int(health_values[member_index])
			if current <= 0 or current >= maximum_member:
				continue
			health_values[member_index] = mini(maximum_member, current + amount_i)
			restored = true
		if restored:
			row["member_health"] = health_values
			var aggregate := 0
			for value in health_values:
				aggregate += int(value)
			row["health"] = aggregate
			healed += 1
	if healed == 0:
		return {"ok": false, "reason": "no-wounded-allies-in-range"}
	return {"ok": true, "reason": "", "effect": "heal", "affected": healed}


func _apply_ability_modifier(hero_row: Dictionary, ability_id: String, effect: Dictionary) -> Dictionary:
	## Converted attribute modifier with authored duration: rides the hero (and
	## allies inside the authored range when the module authors one) until
	## expiry. Keyed by the ability id: a recast refreshes the same grant
	## instead of stacking it (retail same-named AttributeModifier rule).
	var hero_id := int(hero_row.get("id", 0))
	var team := int(hero_row.get("team", -1))
	var duration_ticks := int(effect.get("duration_ticks", 1))
	var modifiers: Array = (effect.get("modifiers", []) as Array).duplicate(true)
	var range_limit := float(effect.get("range_scaled", 0.0))
	var targets: Array[int] = [hero_id]
	if range_limit > 0.0:
		for id in living_ids(team):
			if id == hero_id:
				continue
			var ally: Dictionary = entities[id]
			if Vector2(ally.get("position", Vector2.ZERO)).distance_to(Vector2(hero_row.get("position", Vector2.ZERO))) <= range_limit:
				targets.append(id)
	var expiry := tick_index + duration_ticks
	for id in targets:
		_set_timed_modifier(entities[id] as Dictionary, "ability:%s" % ability_id, modifiers, expiry)
	return {"ok": true, "reason": "", "effect": "attribute-modifier", "affected": targets.size()}


func _apply_ability_weapon_toggle(hero_row: Dictionary, effect: Dictionary) -> Dictionary:
	## TOGGLE_WEAPONSET: pin combat to the compiled toggle weapon-mode profile
	## (Legolas knives, Aragorn bow class), or release back to the default set
	## when already engaged. Fail-closed: the mode must exist among the unit's
	## compiled weapon modes or nothing changes.
	var toggle_mode := String(effect.get("toggleMode", ""))
	var modes: Dictionary = hero_row.get("weapon_modes", {}) as Dictionary
	if toggle_mode == "" or not modes.has(toggle_mode):
		return {"ok": false, "reason": "toggle-mode-unavailable:%s" % toggle_mode}
	var engaged := String(hero_row.get("weapon_toggle_mode", "")) != toggle_mode
	hero_row["weapon_toggle_mode"] = toggle_mode if engaged else ""
	var resolved := toggle_mode if engaged else String(hero_row.get("default_weapon_mode", "default"))
	_apply_weapon_mode(hero_row, resolved)
	return {"ok": true, "reason": "", "effect": "weapon-toggle", "affected": 1, "mode": resolved, "engaged": engaged}


func _apply_ability_mount_toggle(hero_row: Dictionary, effect: Dictionary) -> Dictionary:
	## SpecialAbilityToggleMounted: the mounted state swaps the compiled
	## SET_MOUNTED locomotor speed and (when authored) pins combat to the
	## compiled "mounted" weapon-mode profile; dismounting restores the foot
	## stats. Health is untouched by the same-object condition swap — the
	## authored fraction is preserved exactly. When a future compiled rule
	## authors a mounted member health, the swap rescales preserving the live
	## health fraction instead of resetting it.
	if bool(hero_row.get("mounted", false)):
		# Dismount: restore the recorded foot profile.
		hero_row["speed"] = float(hero_row.get("dismounted_speed", hero_row.get("speed", 0.0)))
		hero_row["speed_source"] = float(hero_row.get("dismounted_speed_source", hero_row.get("speed_source", 0.0)))
		hero_row.erase("dismounted_speed")
		hero_row.erase("dismounted_speed_source")
		if String(hero_row.get("weapon_toggle_mode", "")) == "mounted":
			hero_row["weapon_toggle_mode"] = ""
			_apply_weapon_mode(hero_row, String(hero_row.get("default_weapon_mode", "default")))
		hero_row["mounted"] = false
		_rescale_member_health_preserving_fraction(hero_row, int(effect.get("dismountedMemberHealth", 0)))
		return {"ok": true, "reason": "", "effect": "mount-toggle", "affected": 1, "mounted": false}
	var mounted_speed_scaled := float(effect.get("mounted_speed_scaled", 0.0))
	if mounted_speed_scaled <= 0.0:
		return {"ok": false, "reason": "mount-speed-unresolved"}
	var mode := String(effect.get("mountedWeaponModeKey", ""))
	if mode != "" and not (hero_row.get("weapon_modes", {}) as Dictionary).has(mode):
		# The compiled rule authored a mounted weapon the unit rule does not
		# carry: never a partial swap.
		return {"ok": false, "reason": "mount-mode-unavailable:%s" % mode}
	hero_row["dismounted_speed"] = float(hero_row.get("speed", 0.0))
	hero_row["dismounted_speed_source"] = float(hero_row.get("speed_source", 0.0))
	hero_row["speed"] = mounted_speed_scaled
	hero_row["speed_source"] = float(effect.get("mountedSpeed", 0.0))
	if mode != "":
		hero_row["weapon_toggle_mode"] = mode
		_apply_weapon_mode(hero_row, mode)
	hero_row["mounted"] = true
	_rescale_member_health_preserving_fraction(hero_row, int(effect.get("mountedMemberHealth", 0)))
	return {"ok": true, "reason": "", "effect": "mount-toggle", "affected": 1, "mounted": true}


func _rescale_member_health_preserving_fraction(row: Dictionary, new_member_maximum: int) -> void:
	## ChildObject-style state swaps that change member max health keep the
	## live health FRACTION (retail mount contract). A zero/absent target max
	## (the Men mounts: same body both states) leaves health untouched.
	if new_member_maximum <= 0 or new_member_maximum == int(row.get("member_maximum_health", 0)):
		return
	var old_maximum := maxi(1, int(row.get("member_maximum_health", 1)))
	var health_values: Array = row.get("member_health", [])
	var aggregate := 0
	for index in range(health_values.size()):
		var current := int(health_values[index])
		if current > 0:
			health_values[index] = clampi(roundi(float(current) / float(old_maximum) * float(new_member_maximum)), 1, new_member_maximum)
		aggregate += int(health_values[index]) if int(health_values[index]) > 0 else 0
	row["member_maximum_health"] = new_member_maximum
	row["maximum_health"] = new_member_maximum * int(row.get("member_count", 1))
	row["member_health"] = health_values
	row["health"] = aggregate


func _apply_ability_capture_building(hero_row: Dictionary, effect: Dictionary, target_point: Vector2) -> Dictionary:
	## Capture building, tier-1 honest scope: a hero channels the authored
	## unpack+preparation+pack envelope on a NEUTRAL structure flagged
	## capturable; completion transfers ownership (_step_entity finishes or
	## cancels the channel). Owned structures are out of tier-1 scope and
	## fail closed with their own reason.
	if not (hero_row.get("capture_channel", {}) as Dictionary).is_empty():
		return {"ok": false, "reason": "capture-in-progress"}
	var hero_team := int(hero_row.get("team", -1))
	var best_id := 0
	var best_distance := 2.0
	var found_owned := false
	for structure_id in structure_ids():
		var structure: Dictionary = structures[structure_id]
		if int(structure.get("health", 0)) <= 0 or not bool(structure.get("capturable", false)):
			continue
		var distance := Vector2(structure.get("position", Vector2.ZERO)).distance_to(target_point)
		if distance > best_distance:
			continue
		if int(structure.get("team", -1)) != NEUTRAL_TEAM:
			found_owned = int(structure.get("team", -1)) != hero_team or found_owned
			continue
		best_distance = distance
		best_id = structure_id
	if best_id == 0:
		return {"ok": false, "reason": "capture-tier1-neutral-only" if found_owned else "no-capturable-structure"}
	var channel_ticks := maxi(1, int(effect.get("channel_ticks", 1)))
	hero_row["capture_channel"] = {
		"structure_id": best_id,
		"complete_tick": tick_index + channel_ticks,
	}
	# Channeling holds the hero: drop any live order/target so the capture
	# stance is unambiguous (a later order cancels the channel).
	hero_row["target_id"] = 0
	hero_row["target_kind"] = "battalion"
	hero_row["attack_windup"] = 0
	_clear_member_attack_schedule(hero_row)
	_clear_member_targets(hero_row)
	_clear_pending_route(hero_row, true)
	hero_row["state"] = "capture"
	_emit_event("structure.capture_started", int(hero_row.get("id", 0)), best_id, {
		"team": hero_team,
		"structure_id": best_id,
		"channel_ticks": channel_ticks,
	})
	return {"ok": true, "reason": "", "effect": "capture-building", "affected": 1, "structure_id": best_id}


func _step_capture_channel(row: Dictionary) -> bool:
	## Advance one entity's capture channel. Returns true while the channel
	## holds the hero in place (the entity step then goes no further).
	var channel: Dictionary = row.get("capture_channel", {}) as Dictionary
	if channel.is_empty():
		return false
	var structure_id := int(channel.get("structure_id", 0))
	var structure: Dictionary = structures.get(structure_id, {})
	var team := int(row.get("team", -1))
	var interrupted := (
		structure.is_empty()
		or int(structure.get("health", 0)) <= 0
		or int(structure.get("team", -1)) != NEUTRAL_TEAM
		or not (row["route"] as Array).is_empty()
		or int(row.get("target_id", 0)) != 0
	)
	if interrupted:
		row.erase("capture_channel")
		_emit_event("structure.capture_cancelled", int(row.get("id", 0)), structure_id, {"team": team})
		return false
	if tick_index >= int(channel.get("complete_tick", tick_index + 1)):
		structure["team"] = team
		row.erase("capture_channel")
		row["state"] = "idle"
		_emit_event("structure.captured", int(row.get("id", 0)), structure_id, {
			"team": team,
			"structure_id": structure_id,
			"structure_kind": String(structure.get("structure_kind", "")),
		})
		return false
	row["state"] = "capture"
	row["current_speed"] = 0.0
	return true


func _apply_ability_terror(hero_row: Dictionary, ability_id: String, effect: Dictionary) -> Dictionary:
	## Terror/fear (Screech family): every enemy battalion inside the authored
	## radius takes the compiled penalty modifiers for the authored duration,
	## and an authored scatter displaces ground victims away from the caster
	## through the shared walkable-fraction displacement — without the
	## knockdown sprawl (fleeing, not bowled over). Fear-resistant units (only
	## when the compiled rule authors the flag) and RESIST_FEAR carriers are
	## immune to the debuff; flyers are immune to the scatter only.
	var team := int(hero_row.get("team", -1))
	var origin := Vector2(hero_row.get("position", Vector2.ZERO))
	var radius := float(effect.get("radius_scaled", 0.0))
	var duration_ticks := int(effect.get("duration_ticks", 1))
	var modifiers: Array = (effect.get("modifiers", []) as Array).duplicate(true)
	if radius <= 0.0 or modifiers.is_empty():
		return {"ok": false, "reason": "terror-fields-missing"}
	var scatter := float(effect.get("scatter_strength_scaled", 0.0))
	var filter_text := String(effect.get("affects", ""))
	var expiry := tick_index + duration_ticks
	var affected := 0
	for id in _ability_enemies_near(team, origin, radius):
		var target: Dictionary = entities[id]
		if bool(target.get("fear_resistant", false)) or _timed_modifier_active(target, "RESIST_FEAR"):
			continue
		if not _ability_filter_accepts(target, filter_text):
			continue
		_set_timed_modifier(target, "fear:%s" % ability_id, modifiers, expiry)
		affected += 1
		if scatter > 0.0 and not bool(target.get("flying", false)):
			_apply_fear_scatter(origin, target, scatter)
	return {"ok": true, "reason": "", "effect": "terror", "affected": affected}


func _apply_fear_scatter(center: Vector2, row: Dictionary, strength: float) -> void:
	## Fear displacement: throw the victim radially away from the terror
	## source onto the nearest walkable spot (same deterministic fraction
	## ladder as knockback) and drop its orders — but never the knockdown
	## sprawl, so the battalion can flee/act again immediately.
	var position := Vector2(row.get("position", Vector2.ZERO))
	var distance := position.distance_to(center)
	var direction := (position - center) / distance if distance > 0.001 else Vector2.RIGHT
	var landed := position
	for fraction in [1.0, 0.5, 0.25]:
		var candidate := position + direction * strength * float(fraction)
		if _position_walkable(candidate):
			landed = candidate
			break
	row["position"] = landed
	row["current_speed"] = 0.0
	row["attack_windup"] = 0
	row["target_id"] = 0
	row["target_kind"] = "battalion"
	row["attack_move"] = false
	_clear_member_attack_schedule(row)
	_clear_member_targets(row)
	_clear_pending_route(row, true)
	row["state"] = "idle"
	_emit_event("combat.fear_scatter", int(row.get("id", 0)), 0, {
		"center": [snappedf(center.x, 0.001), snappedf(center.y, 0.001)],
		"landed": [snappedf(landed.x, 0.001), snappedf(landed.y, 0.001)],
	})


func _apply_ability_summon(hero_row: Dictionary, effect: Dictionary, point: Vector2) -> Dictionary:
	## Converted ObjectCreationList summon: each created object must resolve to
	## a converted unit rule or the cast fails closed (never a stand-in).
	var team := int(hero_row.get("team", -1))
	var summoned: Array = []
	var ordinal := 0
	for entry_value in effect.get("objects", []) as Array:
		var entry := entry_value as Dictionary
		var source_id := String(entry.get("id", ""))
		var unit_type := _summon_unit_type_for(source_id)
		if unit_type == "":
			return {"ok": false, "reason": "summon-object-unconverted:%s" % source_id}
		var rule: Dictionary = _unit_production_rules.get(unit_type, {}) as Dictionary
		var member_id := String(rule.get("object_id", ""))
		if member_id == "":
			return {"ok": false, "reason": "summon-object-unconverted:%s" % source_id}
		var count := clampi(int(entry.get("count", 1)), 1, ABILITY_SUMMON_MAX_COUNT)
		for _index in range(count):
			ordinal += 1
			var new_id := int(_next_dynamic_id.get(team, 0))
			_next_dynamic_id[team] = new_id + 1
			var offset := Vector2(ABILITY_SUMMON_OFFSET_STEP * float(ordinal), 0.0)
			_add_battalion(new_id, team, point + offset, String(rule.get("display_name", unit_type)), member_id, unit_type)
			if not entities.has(new_id):
				return {"ok": false, "reason": "summon-spawn-failed:%s" % source_id}
			_emit_event("unit.summoned", new_id, 0, {"team": team, "object_id": member_id, "unit_type": unit_type})
			summoned.append(new_id)
	return {"ok": true, "reason": "", "effect": "summon", "affected": summoned.size(), "summoned": summoned}


func _summon_unit_type_for(source_object_id: String) -> String:
	var member_id := PlayableUnitAdapter.runtime_object_id(source_object_id)
	for unit_type_value in _unit_production_rules.keys():
		if String((_unit_production_rules[unit_type_value] as Dictionary).get("object_id", "")) == member_id:
			return String(unit_type_value)
	return ""


# --- Shared timed-modifier core ---
# One mechanism serves timed ability buffs, leadership auras, and fear: a
# per-entity keyed table of {modifiers, expires_tick}. Multiplicative kinds
# (DAMAGE_MULT/SPEED/VISION/EXPERIENCE/CRUSH/HEALTH) compound across keys,
# ARMOR sums, flag kinds (INVULNERABLE/RESIST_FEAR) trigger at value >= 1.


func _set_timed_modifier(row: Dictionary, key: String, modifiers: Array, expires_tick: int) -> void:
	var table: Dictionary = row.get("timed_modifiers", {}) as Dictionary
	table[key] = {"modifiers": modifiers.duplicate(true), "expires_tick": expires_tick}
	row["timed_modifiers"] = table


func _timed_modifier_product(row: Dictionary, kind: String) -> float:
	var factor := 1.0
	for entry_value in (row.get("timed_modifiers", {}) as Dictionary).values():
		for modifier_value in (entry_value as Dictionary).get("modifiers", []) as Array:
			var modifier := modifier_value as Dictionary
			if String(modifier.get("kind", "")) == kind:
				factor *= float(modifier.get("value", 1.0))
	return factor


func _timed_modifier_active(row: Dictionary, kind: String) -> bool:
	for entry_value in (row.get("timed_modifiers", {}) as Dictionary).values():
		for modifier_value in (entry_value as Dictionary).get("modifiers", []) as Array:
			var modifier := modifier_value as Dictionary
			if String(modifier.get("kind", "")) == kind and float(modifier.get("value", 0.0)) >= 1.0:
				return true
	return false


func _ability_outgoing_multiplier(row: Dictionary) -> float:
	return _timed_modifier_product(row, "DAMAGE_MULT")


func _ability_incoming_multiplier(row: Dictionary) -> float:
	if _timed_modifier_active(row, "INVULNERABLE"):
		return 0.0
	var armor := 0.0
	for entry_value in (row.get("timed_modifiers", {}) as Dictionary).values():
		for modifier_value in (entry_value as Dictionary).get("modifiers", []) as Array:
			var modifier := modifier_value as Dictionary
			if String(modifier.get("kind", "")) == "ARMOR":
				armor += float(modifier.get("value", 0.0))
	return maxf(1.0 - ABILITY_ARMOR_CAP, 1.0 - armor)


func _ability_speed_multiplier(row: Dictionary) -> float:
	return _timed_modifier_product(row, "SPEED")


func _ability_vision_multiplier(row: Dictionary) -> float:
	return _timed_modifier_product(row, "VISION")


func _recompute_leadership_auras() -> void:
	## Passive leadership auras: every living, standing hero radiates its
	## compiled leadership-aura rows to allied battalions inside the authored
	## range (plus itself when AffectsSelf), keyed by the compiled BonusName.
	## Same-named leaderships never stack (one key), different heroes' auras
	## with different names do — the retail AttributeModifier stacking rule.
	## Sweep order is ascending entity id; grants expire one interval after
	## the last refresh, so death/knockdown/leaving-radius drops them.
	for id in entity_ids():
		var hero: Dictionary = entities[id]
		if String(hero.get("category", "")) != "hero":
			continue
		if int(hero.get("health", 0)) <= 0 or int(hero.get("knockdown_ticks", 0)) > 0:
			continue
		var rules: Array = _unit_ability_rules.get(String(hero.get("unit_type", "")), []) as Array
		if rules.is_empty():
			continue
		var team := int(hero.get("team", -1))
		var origin := Vector2(hero.get("position", Vector2.ZERO))
		var level := int(hero.get("level", 1))
		for rule_value in rules:
			var rule := rule_value as Dictionary
			var effect: Dictionary = rule.get("effect", {}) as Dictionary
			if String(effect.get("kind", "")) != "leadership-aura":
				continue
			if not bool(effect.get("startsActive", true)):
				# Upgrade-gated auras stay off until the gate system lands
				# (importer follow-up); fail closed, never invented-on.
				continue
			if level < int(rule.get("required_level", 1)):
				continue
			var bonus_name := String(effect.get("bonusName", ""))
			var range_limit := float(effect.get("range_scaled", 0.0))
			var modifiers: Array = effect.get("modifiers", []) as Array
			if bonus_name == "" or range_limit <= 0.0 or modifiers.is_empty():
				# Fail closed on placeholder rows that carry no aura data.
				continue
			var filter_text := String(effect.get("affects", ""))
			var expiry := tick_index + ABILITY_AURA_INTERVAL_TICKS
			for ally_id in living_ids(team):
				var ally: Dictionary = entities[ally_id]
				if ally_id == id:
					if not bool(effect.get("affectsSelf", false)):
						continue
				elif Vector2(ally.get("position", Vector2.ZERO)).distance_to(origin) > range_limit:
					continue
				if not _ability_filter_accepts(ally, filter_text):
					continue
				_set_timed_modifier(ally, "aura:%s" % bonus_name, modifiers, expiry)


func _step_hero_abilities() -> void:
	## Refresh leadership auras on the fixed cadence, expire timed modifiers
	## whose authored duration elapsed (exact tick), and apply HEALTH-modifier
	## regeneration. Cooldowns are absolute ready ticks, so nothing counts
	## down here.
	if tick_index % ABILITY_AURA_INTERVAL_TICKS == 0:
		_recompute_leadership_auras()
	for id in entity_ids():
		var row: Dictionary = entities[id]
		var table: Dictionary = row.get("timed_modifiers", {}) as Dictionary
		if table.is_empty():
			continue
		var expired: Array[String] = []
		for key_value in table.keys():
			if tick_index >= int((table[key_value] as Dictionary).get("expires_tick", -1)):
				expired.append(String(key_value))
		if not expired.is_empty():
			expired.sort()
			for key in expired:
				table.erase(key)
			row["timed_modifiers"] = table
		if table.is_empty() or int(row.get("health", 0)) <= 0:
			continue
		# HEALTH modifier regeneration (provisional interpretation, recorded:
		# value v > 1.0 restores (v - 1.0) of member max health per second
		# while active; exact retail magnitudes are an importer follow-up).
		var health_factor := _timed_modifier_product(row, "HEALTH")
		if health_factor > 1.0:
			var member_maximum := int(row.get("member_maximum_health", 0))
			var amount := maxi(1, roundi(float(member_maximum) * (health_factor - 1.0) * TICK_SECONDS))
			var health_values: Array = row.get("member_health", [])
			var remaining := amount
			var restored := false
			for index in range(health_values.size()):
				if remaining <= 0:
					break
				var current := int(health_values[index])
				if current <= 0 or current >= member_maximum:
					continue
				var healed := mini(remaining, member_maximum - current)
				health_values[index] = current + healed
				remaining -= healed
				restored = true
			if restored:
				row["member_health"] = health_values
				var aggregate := 0
				for value in health_values:
					aggregate += int(value)
				row["health"] = aggregate


func _step_economy() -> void:
	var interval := maxi(1, int(_rules.get("farm_payout_ticks", 50)))
	if tick_index % interval != 0:
		return
	for id in structure_ids():
		var row: Dictionary = structures[id]
		var income := int(row.get("income_per_payout", 0))
		if income <= 0 or int(row.get("health", 0)) <= 0 or float(row.get("construction_progress", 0.0)) < 1.0:
			continue
		var team := int(row.get("team", -1))
		income = _income_with_upgrade_bonus(team, row, income)
		income = _ai_resource_handicap(team, income)
		team_resources[team] = resources_for_team(team) + income
		_emit_event("economy.payout", id, 0, {"team": team, "amount": income})


func _ai_resource_handicap(team: int, income: int) -> int:
	## Per-difficulty economy handicap. Non-AI teams and any tier at the neutral
	## 1000 permille (the legacy/default "medium") return the income untouched, so
	## the default match's resource curve — and the pinned signature — never move.
	## Higher tiers gain a deterministic integer bonus, lower tiers a penalty.
	if not _team_ai_state.has(team):
		return income
	var permille := int(_difficulty_profile(team).get("resource_permille", 1000))
	if permille == 1000:
		return income
	return int(income * permille / 1000)


func _step_structure_upgrades() -> void:
	for structure_id in structure_ids():
		var building: Dictionary = structures[structure_id]
		if int(building.get("health", 0)) <= 0:
			continue
		var queue: Array = building.get("upgrade_queue", [])
		if queue.is_empty():
			continue
		var item: Dictionary = queue[0]
		if tick_index < int(item.get("complete_tick", tick_index + 1)):
			continue
		var upgrade_id := String(item.get("upgrade_id", ""))
		var contract: Dictionary = _structure_upgrade_contracts.get(upgrade_id, {})
		if contract.is_empty():
			configuration_error = "queued structure upgrade lost its contract"
			continue
		queue.pop_front()
		building["upgrade_queue"] = queue
		var completed: Array = building.get("completed_upgrades", [])
		if not completed.has(upgrade_id):
			completed.append(upgrade_id)
		building["completed_upgrades"] = completed
		if bool(contract.get("team_tech", false)):
			var team := int(building.get("team", -1))
			var owned: Dictionary = team_upgrades.get(team, {}) as Dictionary
			owned[upgrade_id] = true
			team_upgrades[team] = owned
			if bool(contract.get("legacy_provisional", false)):
				# Recorded provisional only (stale pack): the conflated research
				# auto-equips matching hordes. Compiled research grants the
				# technology; the per-battalion purchase path equips it.
				_apply_team_upgrade_to_hordes(team, upgrade_id)
		building["level"] = mini(
			int(contract.get("level_cap", 1)),
			int(building.get("level", 1)) + int(contract.get("levels_to_gain", 0))
		)
		building["command_set"] = String(contract.get("to_command_set", ""))
		# Per-level authored effects ride the completed upgrade: health additions
		# raise the building's pool, PRODUCTION factors compound into its
		# build-speed multiplier (SAGE level modifiers are permanent).
		var health_add := int(contract.get("health_add", 0))
		if health_add != 0:
			building["maximum_health"] = int(building.get("maximum_health", 0)) + health_add
			building["health"] = int(building.get("health", 0)) + health_add
		var production_factor := float(contract.get("production_multiplier", 1.0))
		if production_factor != 1.0:
			building["production_multiplier"] = snappedf(
				float(building.get("production_multiplier", 1.0)) * production_factor,
				0.0001
			)
		_emit_event("upgrade.completed", structure_id, 0, {
			"team": int(building.get("team", -1)),
			"upgrade_id": upgrade_id,
			"level": int(building["level"]),
			"command_set": String(building["command_set"]),
			"health_add": health_add,
			"production_multiplier": float(building.get("production_multiplier", 1.0)),
		})


func _step_production() -> void:
	for id in structure_ids():
		var building: Dictionary = structures[id]
		if int(building.get("health", 0)) <= 0:
			continue
		var queue: Array = building.get("queue", [])
		if queue.is_empty():
			continue
		var item: Dictionary = queue[0]
		if tick_index < int(item.get("complete_tick", tick_index + 1)):
			continue
		queue.pop_front()
		building["queue"] = queue
		var team := int(building.get("team", -1))
		var new_id := int(_next_dynamic_id.get(team, 10 if team == PLAYER_TEAM else 110))
		_next_dynamic_id[team] = new_id + 1
		var production_origin := Vector2(building.get("position", Vector2.ZERO))
		var rally := Vector2(building.get("rally", production_origin))
		var exit_direction := production_origin.direction_to(rally)
		if exit_direction.length_squared() <= 0.000001:
			exit_direction = Vector2.RIGHT if team == PLAYER_TEAM else Vector2.LEFT
		var door_point := production_origin + exit_direction * PRODUCTION_DOOR_INSET_RADIUS
		var create_point := production_origin + exit_direction * PRODUCTION_EXIT_RADIUS
		var unit_type := String(item.get("unit_type", SOLDIER_HORDE_ID))
		# The queued item names its own unit type; the historical Gondor defaults
		# only rescue a queue row that lost its type entirely.
		var production_rule: Dictionary = _unit_production_rules.get(unit_type, _unit_production_rules.get(SOLDIER_HORDE_ID, {}))
		var object_id := String(production_rule.get("object_id", unit_type))
		var display_name := String(production_rule.get("display_name", unit_type))
		var committed_command_points := int(item.get("command_points", 60))
		# QueueProductionExitUpdate uses a create point at the producer doorway,
		# reveals the horde there, and only then sends it to the rally point.
		_add_battalion(new_id, team, door_point, display_name, object_id, unit_type, committed_command_points)
		if String(production_rule.get("category", "")) == "hero":
			_completed_hero_identities["%d:%s" % [team, unit_type]] = true
			if team == PLAYER_TEAM:
				_emit_event("eva.hero_created", new_id, 0, {"team": team, "object_id": object_id, "unit_type": unit_type})
		var produced: Dictionary = entities[new_id]
		produced["production_producer_id"] = id
		produced["production_exit_start_tick"] = tick_index
		produced["production_exit_duration_ticks"] = PRODUCTION_EXIT_DURATION_TICKS
		produced["production_exit_progress"] = 0.0
		produced["production_exit_origin"] = door_point
		produced["production_exit_destination"] = create_point
		produced["production_rally"] = rally
		produced["facing"] = exit_direction
		team_command_points[team] = command_points_for_team(team) + committed_command_points
		_emit_event("production.complete", id, new_id, {
			"team": team,
			"unit_type": unit_type,
			"object_id": object_id,
			"category": String(production_rule.get("category", "")),
			"production_origin": production_origin,
			"create_point": create_point,
			"rally": rally,
			"exit_duration_ticks": PRODUCTION_EXIT_DURATION_TICKS,
			"exit_route_accepted": false,
		})


func _step_entity(id: int) -> void:
	var row: Dictionary = entities[id]
	if int(row["health"]) <= 0:
		row["state"] = "death"
		_clear_pending_route(row, true)
		return
	if tick_index < int(row.get("stun_until_tick", -1)):
		# Cloud Break disruption: the battalion holds position and cannot act
		# until the authored weather duration elapses.
		row["current_speed"] = 0.0
		return
	var knockdown_ticks := int(row.get("knockdown_ticks", 0))
	if knockdown_ticks > 0:
		# Knocked-down battalions lie incapacitated: no movement, attacks, or
		# ability steps until the counter drains, then they stand back up.
		# Being bowled over interrupts a capture channel outright.
		if not (row.get("capture_channel", {}) as Dictionary).is_empty():
			var channel: Dictionary = row.get("capture_channel", {}) as Dictionary
			row.erase("capture_channel")
			_emit_event("structure.capture_cancelled", id, int(channel.get("structure_id", 0)), {"team": int(row.get("team", -1))})
		knockdown_ticks -= 1
		row["knockdown_ticks"] = knockdown_ticks
		row["current_speed"] = 0.0
		if knockdown_ticks <= 0:
			row["knocked_down"] = false
			row["state"] = "idle"
			_emit_event("combat.stand_up", id, 0)
		else:
			row["state"] = "knocked_down"
		return
	row["attack_cooldown"] = maxi(0, int(row["attack_cooldown"]) - 1)
	if _step_capture_channel(row):
		return
	if _step_production_exit(row):
		return
	if bool(row.get("is_builder", false)):
		var construction_id := int(row.get("construction_id", 0))
		if construction_id != 0 and structures.has(construction_id):
			var site: Dictionary = structures[construction_id]
			if float(site.get("construction_progress", 1.0)) < 1.0:
				var site_position := Vector2(site.get("position", row["position"]))
				# A successful navigation query may end at the closest walkable
				# perimeter cell rather than the structure's obstructed center.
				# Exhausting that accepted route is arrival; requiring another
				# invented center-distance strands real-map construction sites.
				if String(row.get("order_kind", "")) == "construct" and ((row["route"] as Array).is_empty() or Vector2(row["position"]).distance_to(site_position) <= 2.0):
					_clear_pending_route(row, true)
					row["state"] = "construct"
					return
		if not (row["route"] as Array).is_empty():
			_step_route(row)
		else:
			row["state"] = "idle"
		return
	var target_id := int(row["target_id"])
	if target_id == 0 and (row["route"] as Array).is_empty() and not bool(row.get("attack_move", false)):
		var auto_target := _nearest_auto_target(row)
		if not auto_target.is_empty():
			target_id = int(auto_target["id"])
			row["target_id"] = target_id
			row["target_kind"] = String(auto_target["kind"])
			row["order_kind"] = "auto_attack"
	if target_id == 0 and bool(row.get("attack_move", false)) and not (row["route"] as Array).is_empty():
		var acquired := _nearest_attack_move_target(row)
		if acquired != 0:
			row["target_id"] = acquired
			row["target_kind"] = "battalion"
			target_id = acquired
	if target_id != 0:
		var target_kind := String(row.get("target_kind", "battalion"))
		if not _target_alive(target_id, target_kind):
			row["target_id"] = 0
			row["target_kind"] = "battalion"
			row["attack_windup"] = 0
			_clear_member_attack_schedule(row)
			_clear_member_targets(row)
			if bool(row.get("attack_move", false)) and _assign_route(row, Vector2(row.get("attack_move_destination", row["position"]))):
				row["state"] = "run"
			else:
				_clear_pending_route(row, true)
				row["state"] = "idle"
			return
		if target_kind == "battalion" and not _can_engage_battalion(row, entities[target_id] as Dictionary):
			# Safety net for targets acquired through paths other than the
			# guarded acquisition funnels (e.g. retaliation): melee cannot
			# chase an airborne battalion it can never reach.
			row["target_id"] = 0
			row["target_kind"] = "battalion"
			row["attack_windup"] = 0
			_clear_member_attack_schedule(row)
			_clear_member_targets(row)
			_clear_pending_route(row, true)
			row["state"] = "idle"
			return
		var target_position := _target_position(target_id, target_kind)
		# Retail/OpenSAGE range is center-to-center: Weapon.cs:54-58 compares the
		# attacker's Translation against the target position without adding either
		# object's bounding radius.
		var distance := Vector2(row["position"]).distance_to(target_position)
		var selected_weapon_mode := _weapon_mode_for_distance(row, distance)
		if selected_weapon_mode == "unsupported-close":
			row["state"] = "idle"
			row["attack_windup"] = 0
			_clear_pending_route(row, true)
			_clear_member_attack_schedule(row)
			return
		_apply_weapon_mode(row, selected_weapon_mode)
		var minimum_range := float(row.get("minimum_attack_range", 0.0))
		if distance <= float(row["attack_range"]) and (minimum_range <= 0.0 or distance >= minimum_range):
			row["state"] = "attack"
			_clear_pending_route(row, true)
			_step_member_attacks(id, row, target_id, target_kind)
			return
		row["attack_windup"] = 0
		_clear_member_attack_schedule(row)
		if (
			String(row.get("stance", "Battle")) == "HoldGround"
			and String(row.get("order_kind", "")) in ["", "auto_attack"]
		):
			# HoldGround never leaves its post to chase: an auto-acquired or
			# retaliation target that steps out of weapon range is dropped
			# instead of pursued. Explicit player attack orders still pursue.
			row["target_id"] = 0
			row["target_kind"] = "battalion"
			_clear_pending_route(row, true)
			row["state"] = "idle"
			return
		if minimum_range > 0.0 and distance < minimum_range:
			# Inside minimum range: back away to re-establish the firing ring.
			# Routing toward the target here walked min-range units (trebuchets)
			# into their attacker and jammed them permanently.
			var away := Vector2(row["position"]) - target_position
			var direction := away / away.length() if away.length() > 0.001 else Vector2.RIGHT
			var fallback := Vector2(row["position"]) + direction * (minimum_range - distance + 1.0)
			_clear_pending_route(row, true)
			if not _assign_route(row, fallback):
				row["state"] = "idle"
				return
			row["state"] = "run"
			_step_route(row)
			return
		if (row["route"] as Array).is_empty():
			if not _assign_route(row, target_position):
				row["target_id"] = 0
				_clear_pending_route(row, true)
				row["state"] = "idle"
				return
		row["state"] = "run"
		_step_route(row)
		return
	if not (row["route"] as Array).is_empty():
		row["state"] = "run"
		_step_route(row)
	else:
		row["attack_move"] = false
		row["state"] = "idle"


func set_structure_rally(team: int, structure_id: int, position: Vector2) -> Dictionary:
	if not structures.has(structure_id):
		return {"ok": false, "reason": "unknown-structure"}
	var row: Dictionary = structures[structure_id]
	if int(row.get("team", -1)) != team:
		return {"ok": false, "reason": "not-owned"}
	if int(row.get("health", 0)) <= 0:
		return {"ok": false, "reason": "structure-destroyed"}
	if playable_outline.size() >= 3 and not Geometry2D.is_point_in_polygon(position, playable_outline):
		return {"ok": false, "reason": "outside-playable-area"}
	row["rally"] = position
	_emit_event("structure.rally_set", 0, structure_id, {"team": team})
	return {"ok": true}


func issue_construct(ids: Array[int], structure_kind: String, position: Vector2, dry_run: bool = false, team: int = PLAYER_TEAM) -> Dictionary:
	return _issue_construct_for_team(team, ids, structure_kind, position, dry_run)


# Approximate footprint radii in local units (world scale ~0.0265/source unit).
# Placement is legal when the two footprints plus a small working margin do
# not overlap — the previous flat 7.0-unit exclusion wasted most of the base.
const STRUCTURE_PLACEMENT_RADII := {
	"fortress": 4.0,
	"stable": 2.6,
	"barracks": 2.4,
	"archery_range": 2.4,
	"workshop": 2.4,
	"farm": 2.2,
}
const PLACEMENT_CLEARANCE_MARGIN := 0.4


func _structure_placement_radius(structure_kind: String) -> float:
	return float(STRUCTURE_PLACEMENT_RADII.get(structure_kind, 2.4))


func _issue_construct_for_team(team: int, ids: Array[int], structure_kind: String, position: Vector2, dry_run: bool = false) -> Dictionary:
	if not base_loop_enabled or winner != -1:
		return {"ok": false, "reason": "match-unavailable"}
	if team != PLAYER_TEAM and team != ENEMY_TEAM:
		return {"ok": false, "reason": "invalid-team"}
	if not _structure_build_rules.has(structure_kind):
		return {"ok": false, "reason": "unsupported-structure"}
	if playable_outline.size() >= 3 and not Geometry2D.is_point_in_polygon(position, playable_outline):
		return {"ok": false, "reason": "outside-playable-area"}
	var new_radius := _structure_placement_radius(structure_kind)
	for existing_id in structure_ids():
		var existing_row: Dictionary = structures[existing_id] as Dictionary
		var existing_position := Vector2(existing_row.get("position", Vector2.ZERO))
		var clearance := new_radius + _structure_placement_radius(String(existing_row.get("structure_kind", ""))) + PLACEMENT_CLEARANCE_MARGIN
		if existing_position.distance_to(position) < clearance:
			return {"ok": false, "reason": "site-obstructed"}
	var builder_id := 0
	for value in ids:
		var id := int(value)
		if not entities.has(id):
			continue
		var row: Dictionary = entities[id]
		if int(row.get("team", -1)) != team or int(row.get("health", 0)) <= 0 or not bool(row.get("is_builder", false)):
			continue
		builder_id = id
		break
	if builder_id == 0:
		return {"ok": false, "reason": "builder-required"}
	var build_rule: Dictionary = _structure_build_rules[structure_kind]
	var cost := int(build_rule["cost"])
	if resources_for_team(team) < cost:
		return {"ok": false, "reason": "insufficient-resources", "cost": cost}
	if dry_run:
		return {"ok": true, "reason": "", "dry_run": true, "cost": cost}
	var structure_id := _next_dynamic_structure_id
	_next_dynamic_structure_id += 1
	var maximum_health := int(_structure_max_health[structure_kind])
	var production: Array[String] = []
	for unit_type in _production_unit_order:
		var production_rule: Dictionary = _unit_production_rules[unit_type]
		var producer_kinds_for_rule: Array = production_rule.get("producer_kinds", [String(production_rule.get("producer_kind", ""))])
		if producer_kinds_for_rule.has(structure_kind):
			production.append(unit_type)
	var build_ticks := maxi(1, roundi(float(build_rule["seconds"]) / TICK_SECONDS))
	structures[structure_id] = {
		"id": structure_id,
		"team": team,
		"kind": "structure",
		"structure_kind": structure_kind,
		"name": structure_kind.replace("_", " ").capitalize(),
		"position": position,
		"rally": position + Vector2(4.0, 0.0),
		"health": maximum_health,
		"maximum_health": maximum_health,
		"construction_progress": 0.0,
		"level": 1,
		"completed_upgrades": [],
		"upgrade_queue": [],
		"construction_build_ticks": build_ticks,
		"construction_elapsed_ticks": 0,
		"builder_id": builder_id,
		"production": production,
		"queue": [],
		"damage_remainders": {},
		"income_per_payout": int(_rules.get("farm_income", 25)) if structure_kind == "farm" else 0,
	}
	team_resources[team] = resources_for_team(team) - cost
	var builder: Dictionary = entities[builder_id]
	var previous_site_id := int(builder.get("construction_id", 0))
	if previous_site_id != 0 and structures.has(previous_site_id):
		# Redirecting a busy builder cancels its unfinished site with a full
		# refund; otherwise the site would linger as unfinishable scaffolding.
		var previous_site: Dictionary = structures[previous_site_id]
		if float(previous_site.get("construction_progress", 1.0)) < 1.0:
			var previous_team := int(previous_site.get("team", team))
			team_resources[previous_team] = resources_for_team(previous_team) + int(_structure_build_rules.get(String(previous_site.get("structure_kind", "")), {}).get("cost", 0))
			structures.erase(previous_site_id)
			_emit_event("construction.cancelled", builder_id, previous_site_id, {"team": previous_team})
	builder["construction_id"] = structure_id
	builder["order_kind"] = "construct"
	builder["target_id"] = 0
	_clear_member_targets(builder)
	if not _assign_route(builder, position):
		structures.erase(structure_id)
		team_resources[team] = resources_for_team(team) + cost
		builder["construction_id"] = 0
		return {"ok": false, "reason": last_route_rejection if last_route_rejection != "" else "route-rejected"}
	_emit_event("construction.started", builder_id, structure_id, {"team": team, "structure_kind": structure_kind, "cost": cost, "build_ticks": build_ticks, "object_id": String(builder.get("object_id", ""))})
	return {"ok": true, "builder_id": builder_id, "structure_id": structure_id, "cost": cost, "build_ticks": build_ticks}


func _step_construction() -> void:
	for structure_id in structure_ids():
		var site: Dictionary = structures[structure_id]
		if float(site.get("construction_progress", 1.0)) >= 1.0:
			continue
		if bool(site.get("builder_free", false)):
			# Foundation behavior: plot-built expansions rise without a porter.
			var elapsed := int(site.get("construction_elapsed_ticks", 0)) + 1
			var build_ticks := maxi(1, int(site.get("construction_build_ticks", 1)))
			site["construction_elapsed_ticks"] = elapsed
			site["construction_progress"] = minf(1.0, float(elapsed) / float(build_ticks))
			if elapsed >= build_ticks:
				_emit_event("construction.completed", 0, structure_id, {"team": int(site.get("team", -1)), "structure_kind": String(site.get("structure_kind", ""))})
			continue
		var builder_id := int(site.get("builder_id", 0))
		if builder_id != 0 and (not entities.has(builder_id) or int((entities[builder_id] as Dictionary).get("health", 0)) <= 0):
			# A dead builder can never finish its site. The husk is destroyed
			# instead of stalling forever; the builder's assignment clears too.
			if entities.has(builder_id):
				(entities[builder_id] as Dictionary)["construction_id"] = 0
			site["builder_id"] = 0
			if int(site.get("health", 0)) > 0:
				site["health"] = 0
				_emit_event("structure.destroyed", 0, structure_id, {"reason": "construction-builder-unavailable"})
			continue
		if not entities.has(builder_id):
			continue
		var builder: Dictionary = entities[builder_id]
		if int(builder.get("health", 0)) <= 0 or String(builder.get("state", "")) != "construct":
			continue
		# The builder only advances the site it is currently assigned to. An
		# abandoned site sharing the same builder id must not leech progress
		# from (and then hijack completion of) the active build.
		if int(builder.get("construction_id", 0)) != structure_id:
			continue
		var elapsed := int(site.get("construction_elapsed_ticks", 0)) + 1
		var build_ticks := maxi(1, int(site.get("construction_build_ticks", 1)))
		site["construction_elapsed_ticks"] = elapsed
		site["construction_progress"] = minf(1.0, float(elapsed) / float(build_ticks))
		if elapsed >= build_ticks:
			builder["construction_id"] = 0
			builder["order_kind"] = ""
			builder["state"] = "idle"
			var site_team := int(site.get("team", -1))
			if _team_ai_state.has(site_team):
				(_team_ai_state[site_team] as Dictionary)["construction_resolved"] = true
			if String(site.get("structure_kind", "")) == "fortress":
				_seed_expansion_pads_for(structure_id)
			_emit_event("construction.completed", builder_id, structure_id, {"team": int(site.get("team", -1)), "structure_kind": String(site.get("structure_kind", ""))})
func _nearest_attack_move_target(row: Dictionary) -> int:
	var origin := Vector2(row.get("position", Vector2.ZERO))
	var limit := maxf(float(row.get("attack_range", 1.0)), float(row.get("vision_range", 17.5)) * _ability_vision_multiplier(row))
	var result := 0
	var best := limit
	for candidate in _hostile_living_ids(int(row.get("team", PLAYER_TEAM))):
		if not _can_engage_battalion(row, entities[candidate] as Dictionary):
			continue
		var distance := origin.distance_to(Vector2((entities[candidate] as Dictionary).get("position", Vector2.ZERO)))
		if distance <= best:
			best = distance
			result = candidate
	return result


func _nearest_auto_target(row: Dictionary) -> Dictionary:
	var self_team := int(row.get("team", PLAYER_TEAM))
	var origin := Vector2(row.get("position", Vector2.ZERO))
	var stance := String(row.get("stance", "Battle"))
	var stance_state := _stance_state(row, stance)
	var limit := float(row.get("vision_range", 0.0)) * float(stance_state.get("visionMultiplier", 1.0)) * _ability_vision_multiplier(row)
	if stance == "HoldGround":
		var modes: Dictionary = row.get("weapon_modes", {}) as Dictionary
		var default_mode: Dictionary = modes.get(String(row.get("default_weapon_mode", "default")), {}) as Dictionary
		limit = float(default_mode.get("attack_range", row.get("attack_range", 0.0)))
	if limit <= 0.0:
		return {}
	var best_id := 0
	var best_kind := ""
	var best_distance := limit
	for candidate in _hostile_living_ids(self_team):
		if not _can_engage_battalion(row, entities[candidate] as Dictionary):
			continue
		var distance := origin.distance_to(Vector2((entities[candidate] as Dictionary).get("position", Vector2.ZERO)))
		if distance <= best_distance:
			best_distance = distance
			best_id = candidate
			best_kind = "battalion"
	for candidate in _hostile_living_structure_ids(self_team):
		var distance := origin.distance_to(Vector2((structures[candidate] as Dictionary).get("position", Vector2.ZERO)))
		if distance <= best_distance:
			best_distance = distance
			best_id = candidate
			best_kind = "structure"
	return {"id": best_id, "kind": best_kind} if best_id != 0 else {}


func _step_production_exit(row: Dictionary) -> bool:
	var duration := int(row.get("production_exit_duration_ticks", 0))
	var start_tick := int(row.get("production_exit_start_tick", -1))
	if duration <= 0 or start_tick < 0:
		row["production_exit_progress"] = 1.0
		return false
	var elapsed := maxi(0, tick_index - start_tick)
	var progress := clampf(float(elapsed) / float(duration), 0.0, 1.0)
	row["production_exit_progress"] = progress
	var exit_origin := Vector2(row.get("production_exit_origin", row.get("position", Vector2.ZERO)))
	var exit_destination := Vector2(row.get("production_exit_destination", exit_origin))
	row["position"] = exit_origin.lerp(exit_destination, smoothstep(0.0, 1.0, progress))
	var exit_direction := exit_origin.direction_to(exit_destination)
	if exit_direction.length_squared() > 0.000001:
		row["facing"] = exit_direction
	if elapsed >= duration:
		row["production_exit_start_tick"] = -1
		row["production_exit_duration_ticks"] = 0
		row["production_exit_progress"] = 1.0
		var rally := Vector2(row.get("production_rally", exit_destination))
		if not rally.is_equal_approx(exit_destination) and _assign_route(row, rally):
			row["state"] = "run"
		else:
			_clear_pending_route(row, true)
			row["state"] = "idle"
		_emit_event("production.exit_complete", int(row.get("production_producer_id", 0)), int(row.get("id", 0)), {
			"rally": rally,
			"exit_destination": exit_destination,
		})
		return false
	# The horde presentation reveals retail members through the door one by one.
	# Keep its authoritative root at the source-derived doorway until all members
	# have emerged, then let the already accepted rally route advance normally.
	row["state"] = "run"
	return true


func _step_member_attacks(attacker_id: int, row: Dictionary, target_id: int, target_kind: String) -> void:
	var member_health_values: Array = row.get("member_health", [])
	var start_ticks: Array = row.get("member_attack_start_ticks", [])
	var hit_ticks: Array = row.get("member_attack_hit_ticks", [])
	var tokens: Array = row.get("member_attack_tokens", [])
	var target_indices: Array = row.get("member_target_indices", [])
	var weapon_modes: Array = row.get("member_weapon_modes", [])
	var release_tokens: Array = row.get("member_attack_release_tokens", [])
	if member_health_values.is_empty() or start_ticks.size() != member_health_values.size() or hit_ticks.size() != member_health_values.size() or tokens.size() != member_health_values.size() or target_indices.size() != member_health_values.size() or weapon_modes.size() != member_health_values.size() or release_tokens.size() != member_health_values.size():
		return
	if target_kind == "battalion":
		_ensure_member_target_assignments(row, entities[target_id] as Dictionary)
		target_indices = row.get("member_target_indices", [])
	if int(row.get("attack_cooldown", 0)) == 0:
		var pre_attack_ticks := maxi(0, int(row.get("pre_attack_ticks", 0)))
		var maximum_stagger := maxi(0, MEMBER_ATTACK_STAGGER_WINDOW_TICKS - 1)
		var coast_ticks := maxi(0, int(row.get("continuous_fire_coast_ticks", 0)))
		var expiration_tick := int(row.get("continuous_fire_expiration_tick", -1))
		if expiration_tick < 0 or tick_index >= expiration_tick:
			row["continuous_fire_count"] = 0
		var continuous_threshold := maxi(0, int(row.get("continuous_fire_one", 0)))
		var rate_multiplier := maxf(1.0, float(row.get("continuous_fire_rate_multiplier", 1.0)))
		var base_reload_or_delay_ms := float(row.get("delay_between_shots_ms", 0.0))
		if int(row.get("clip_size", 0)) == 1:
			base_reload_or_delay_ms = float(row.get("clip_reload_time_ms", base_reload_or_delay_ms))
		# SAGE captures the possible-next-shot frame before FiringTracker promotes
		# the shot count into the next continuous-fire tier.
		var coast_anchor_ms := base_reload_or_delay_ms
		if continuous_threshold > 0 and int(row.get("continuous_fire_count", 0)) > continuous_threshold:
			coast_anchor_ms = floorf(coast_anchor_ms / rate_multiplier)
		row["attack_sequence"] = int(row.get("attack_sequence", 0)) + 1
		row["continuous_fire_count"] = int(row.get("continuous_fire_count", 0)) + 1
		var attack_sequence := int(row["attack_sequence"])
		for member_index in range(member_health_values.size()):
			if int(member_health_values[member_index]) <= 0:
				start_ticks[member_index] = -1
				hit_ticks[member_index] = -1
				continue
			var stagger := posmod(attacker_id + member_index * 3 + attack_sequence, MEMBER_ATTACK_STAGGER_WINDOW_TICKS)
			weapon_modes[member_index] = String(row.get("active_weapon_mode", "default"))
			start_ticks[member_index] = tick_index + stagger
			# Every member owns its attack boundary. This avoids the old whole-
			# horde structure impact while preserving deterministic replay.
			hit_ticks[member_index] = tick_index + stagger + pre_attack_ticks
		var reload_or_delay_ms := base_reload_or_delay_ms
		if continuous_threshold > 0 and int(row["continuous_fire_count"]) > continuous_threshold:
			reload_or_delay_ms = floorf(reload_or_delay_ms / rate_multiplier)
		var cadence_ms := (
			float(row.get("pre_attack_delay_ms", 0.0))
			+ float(row.get("firing_duration_ms", 0.0))
			+ reload_or_delay_ms
		)
		var cadence_ticks := maxi(1, roundi(cadence_ms / (TICK_SECONDS * 1000.0)))
		var coast_anchor_ticks := maxi(1, roundi(coast_anchor_ms / (TICK_SECONDS * 1000.0)))
		row["continuous_fire_expiration_tick"] = tick_index + coast_anchor_ticks + coast_ticks
		row["attack_cooldown"] = maxi(
			cadence_ticks,
			pre_attack_ticks + maximum_stagger + 1
		)
		row["attack_windup"] = pre_attack_ticks + maximum_stagger
		_emit_event("combat.swing", attacker_id, target_id, {
			"attack_sequence": attack_sequence,
			"living_members": _living_member_count(row),
			"object_id": String(row.get("object_id", "")),
		})
	for member_index in range(member_health_values.size()):
		if int(member_health_values[member_index]) <= 0:
			continue
		if int(start_ticks[member_index]) == tick_index:
			tokens[member_index] = int(tokens[member_index]) + 1
			start_ticks[member_index] = -1
			_emit_event("combat.member_swing", attacker_id, target_id, {
				"member_index": member_index,
				"target_member_index": int(target_indices[member_index]),
				"weapon_mode": String(weapon_modes[member_index]),
				"member_attack_token": int(tokens[member_index]),
				"attack_sequence": int(row.get("attack_sequence", 0)),
			})
		if int(hit_ticks[member_index]) == tick_index:
			hit_ticks[member_index] = -1
			if String(weapon_modes[member_index]) != "close":
				release_tokens[member_index] = int(release_tokens[member_index]) + 1
				_emit_event("combat.member_fire", attacker_id, target_id, {
					"member_index": member_index,
					"target_member_index": int(target_indices[member_index]),
					"weapon_mode": String(weapon_modes[member_index]),
					"member_release_token": int(release_tokens[member_index]),
				})
			if _target_alive(target_id, target_kind):
				var forced_target := int(target_indices[member_index]) if target_kind == "battalion" else -1
				# A recorded WeaponSetUpgrade replaces the horde's base weapon
				# damage with the compiled upgraded damage (no invented
				# multipliers); its target-filtered DamageScalars apply per hit
				# inside _apply_member_damage.
				var weapon_effect := _applied_weapon_effect(row)
				var outgoing_damage := float(row.get("member_damage", 1))
				if float(weapon_effect.get("damage", 0.0)) > 0.0:
					outgoing_damage = float(weapon_effect.get("damage"))
				var swing_damage := maxi(1, roundi(outgoing_damage * float(_stance_state(row).get("damageMultiplier", 1.0)) * _ability_outgoing_multiplier(row)))
				_apply_member_damage(
					attacker_id,
					member_index,
					target_id,
					swing_damage,
					target_kind,
					int(row.get("attack_sequence", 0)),
					forced_target
				)
				# Upgrade-gated bonus nuggets (fire arrows' authored flame
				# component) land as their own typed hits on the same victim.
				for nugget_value in weapon_effect.get("bonus_nuggets", []) as Array:
					if not _target_alive(target_id, target_kind):
						break
					var nugget: Dictionary = nugget_value
					var bonus_target: Dictionary = structures.get(target_id, {}) if target_kind == "structure" else entities.get(target_id, {})
					var bonus_factor := _damage_scalar_factor(nugget.get("scalars", []) as Array, bonus_target, target_kind)
					var bonus_amount := maxi(0, roundi(float(nugget.get("damage", 0.0)) * bonus_factor))
					if bonus_amount <= 0:
						continue
					_apply_member_damage(
						attacker_id,
						member_index,
						target_id,
						bonus_amount,
						target_kind,
						int(row.get("attack_sequence", 0)),
						forced_target,
						String(nugget.get("damage_type", ""))
					)
				_apply_hero_cleave(attacker_id, row, target_id, swing_damage)
	row["member_attack_start_ticks"] = start_ticks
	row["member_attack_hit_ticks"] = hit_ticks
	row["member_attack_tokens"] = tokens
	row["member_target_indices"] = target_indices
	row["member_weapon_modes"] = weapon_modes
	row["member_attack_release_tokens"] = release_tokens
	row["attack_windup"] = maxi(0, int(row.get("attack_windup", 0)) - 1)


# Provisional hero cleave: a hero's melee swing carries a small splash to
# other enemy battalions beside the primary target (retail heroes sweep
# multiple soldiers per swing; exact magnitudes are an M3 extraction item).
const HERO_CLEAVE_RADIUS := 1.6
const HERO_CLEAVE_DAMAGE_FRACTION := 0.35


func _apply_hero_cleave(attacker_id: int, row: Dictionary, primary_target_id: int, swing_damage: int) -> void:
	if String(row.get("category", "")) != "hero":
		return
	if float(row.get("attack_range_source", 0.0)) > 100.0:
		return
	var cleave_damage := maxi(1, roundi(float(swing_damage) * HERO_CLEAVE_DAMAGE_FRACTION))
	var origin := Vector2(row.get("position", Vector2.ZERO))
	for candidate in _hostile_living_ids(int(row.get("team", PLAYER_TEAM))):
		if candidate == primary_target_id:
			continue
		var candidate_row: Dictionary = entities[candidate]
		if origin.distance_to(Vector2(candidate_row.get("position", Vector2.ZERO))) > HERO_CLEAVE_RADIUS:
			continue
		_apply_member_damage(attacker_id, -1, candidate, cleave_damage, "battalion", int(row.get("attack_sequence", 0)))


func _clear_member_attack_schedule(row: Dictionary) -> void:
	var start_ticks: Array = row.get("member_attack_start_ticks", [])
	var hit_ticks: Array = row.get("member_attack_hit_ticks", [])
	for index in range(start_ticks.size()):
		start_ticks[index] = -1
	for index in range(hit_ticks.size()):
		hit_ticks[index] = -1
	row["member_attack_start_ticks"] = start_ticks
	row["member_attack_hit_ticks"] = hit_ticks


func _clear_member_targets(row: Dictionary) -> void:
	var targets: Array = row.get("member_target_indices", [])
	for index in range(targets.size()):
		targets[index] = -1
	row["member_target_indices"] = targets


func _weapon_mode_for_distance(row: Dictionary, distance: float) -> String:
	# An engaged TOGGLE_WEAPONSET pins the battalion to its toggled compiled
	# profile: retail's WEAPONSET_TOGGLE_1 condition overrides the range-based
	# selection entirely until the player toggles back.
	var toggle_mode := String(row.get("weapon_toggle_mode", ""))
	if toggle_mode != "" and (row.get("weapon_modes", {}) as Dictionary).has(toggle_mode):
		return toggle_mode
	var close_mode := String(row.get("close_weapon_mode", ""))
	var switch_distance := float(row.get("close_weapon_switch_distance", 0.0))
	if bool(row.get("unsupported_close_weapon", false)) and switch_distance > 0.0 and distance <= switch_distance:
		return "unsupported-close"
	if close_mode != "" and switch_distance > 0.0 and distance <= switch_distance:
		return close_mode
	return String(row.get("default_weapon_mode", "default"))


# Formations carry real combat behavior, not just spacing (provisional
# magnitudes; retail per-formation modifiers are an M3 INI extraction item).
# Block reads as the braced shield-wall stance: tighter, tougher, slower, and
# resistant to cavalry impact.
const FORMATION_EFFECTS := {
	"Block": {
		"incoming_damage_multiplier": 0.85,
		"speed_multiplier": 0.85,
		"trample_damage_multiplier": 0.5,
	},
}


func _formation_effects(row: Dictionary) -> Dictionary:
	return FORMATION_EFFECTS.get(String(row.get("formation_mode", "Line")), {}) as Dictionary


func _stance_state(row: Dictionary, requested: String = "") -> Dictionary:
	var contract: Dictionary = row.get("stance_contract", {}) as Dictionary
	var states: Dictionary = contract.get("states", {}) as Dictionary
	var stance := requested if requested != "" else String(row.get("stance", "Battle"))
	var selected: Dictionary = states.get(stance, {}) as Dictionary
	if not selected.is_empty():
		return selected
	return {
		"damageMultiplier": 1.0,
		"incomingDamageMultiplier": 1.0,
		"visionMultiplier": 1.0,
		"speedMultiplier": 1.0,
	}


func _apply_weapon_mode(row: Dictionary, mode: String) -> void:
	var modes: Dictionary = row.get("weapon_modes", {}) as Dictionary
	var selected: Dictionary = modes.get(mode, modes.get(String(row.get("default_weapon_mode", "default")), {})) as Dictionary
	if selected.is_empty():
		return
	var prior := String(row.get("active_weapon_mode", ""))
	row["active_weapon_mode"] = mode
	for field in [
		"attack_range", "attack_range_source", "minimum_attack_range",
		"minimum_attack_range_source", "delay_between_shots_ms",
		"pre_attack_delay_ms", "firing_duration_ms", "attack_period_ticks",
		"pre_attack_ticks", "firing_duration_ticks", "member_damage", "clip_size",
		"clip_reload_time_ms", "continuous_fire_one", "continuous_fire_coast_ticks",
		"continuous_fire_rate_multiplier",
	]:
		if selected.has(field):
			row[field] = selected[field]
	if prior != "" and prior != mode:
		_clear_member_attack_schedule(row)


func _ensure_member_target_assignments(attacker: Dictionary, target: Dictionary) -> void:
	var attacker_health: Array = attacker.get("member_health", [])
	var target_health: Array = target.get("member_health", [])
	var assignments: Array = attacker.get("member_target_indices", [])
	if assignments.size() != attacker_health.size() or target_health.is_empty():
		return
	var use_counts: Array[int] = []
	use_counts.resize(target_health.size())
	use_counts.fill(0)
	for member_index in range(assignments.size()):
		var candidate := int(assignments[member_index])
		if int(attacker_health[member_index]) <= 0 or candidate < 0 or candidate >= target_health.size() or int(target_health[candidate]) <= 0:
			assignments[member_index] = -1
		else:
			use_counts[candidate] += 1
	for member_index in range(assignments.size()):
		if int(attacker_health[member_index]) <= 0 or int(assignments[member_index]) >= 0:
			continue
		var attacker_position := _member_world_position(attacker, member_index)
		var best_index := -1
		var best_score := INF
		for target_index in range(target_health.size()):
			if int(target_health[target_index]) <= 0:
				continue
			var target_position := _member_world_position(target, target_index)
			var score := float(use_counts[target_index]) * 10000.0 + attacker_position.distance_squared_to(target_position)
			if score < best_score:
				best_score = score
				best_index = target_index
		if best_index >= 0:
			assignments[member_index] = best_index
			use_counts[best_index] += 1
	attacker["member_target_indices"] = assignments


func _member_world_position(row: Dictionary, member_index: int) -> Vector2:
	var origin := Vector2(row.get("position", Vector2.ZERO))
	var positions: Array = row.get("formation_positions", [])
	if member_index < 0 or member_index >= positions.size() or typeof(positions[member_index]) != TYPE_VECTOR3:
		return origin
	var slot: Vector3 = positions[member_index]
	var local := Vector2(slot.x, slot.z)
	var facing := Vector2(row.get("facing", Vector2.RIGHT))
	if facing.length_squared() <= 0.000001:
		return origin + local
	return origin + local.rotated(facing.angle())


func _living_member_count(row: Dictionary) -> int:
	var result := 0
	for health_value in Array(row.get("member_health", [])):
		if int(health_value) > 0:
			result += 1
	return result


# Movement blocking hugs the placement footprints (STRUCTURE_PLACEMENT_RADII
# + a step of walkway). Oversized rings strand builders parked at a finished
# site inside the "wall" of the building they just raised and block the
# approach to tightly-packed neighbor sites.
const STRUCTURE_BLOCK_RADIUS := {
	"fortress": 4.6,
	"stable": 3.0,
	"barracks": 2.8,
	"archery_range": 2.8,
	"workshop": 2.8,
	"farm": 2.6,
	"forge": 2.6,
	"well": 2.2,
	"marketplace": 2.6,
	"statue": 2.0,
	"battle_tower": 2.2,
	"wall_hub": 2.2,
}


func _deflect_around_structures(position: Vector2, attack_target_id: int) -> Vector2:
	# Battalions slide around building footprints instead of clipping through
	# them. The battalion's own attack target is exempt so melee can close in.
	for structure_id in structure_ids():
		if structure_id == attack_target_id:
			continue
		var structure_row: Dictionary = structures[structure_id]
		if int(structure_row.get("health", 0)) <= 0:
			continue
		# Construction sites do not block movement: builders must reach their
		# own site, and scaffolding is passable until the structure completes.
		if float(structure_row.get("construction_progress", 1.0)) < 1.0:
			continue
		var radius := float(STRUCTURE_BLOCK_RADIUS.get(String(structure_row.get("structure_kind", "")), 2.8))
		var center := Vector2(structure_row.get("position", Vector2.ZERO))
		var offset := position - center
		var distance := offset.length()
		if distance < radius and distance > 0.001:
			position = center + offset / distance * radius
	return position


func _step_route(row: Dictionary) -> void:
	var route: Array = row["route"]
	if route.is_empty():
		# Brake toward stop when no route remains.
		var idle_speed := maxf(0.0, float(row.get("current_speed", 0.0)) - float(row.get("braking", float(row.get("speed", 0.0)))) * TICK_SECONDS)
		row["current_speed"] = idle_speed
		return
	var position := Vector2(row["position"])
	var waypoint := Vector2(route[0])
	var max_speed := float(row["speed"]) * float(_stance_state(row).get("speedMultiplier", 1.0)) * float(_formation_effects(row).get("speed_multiplier", 1.0)) * _ability_speed_multiplier(row)
	# Fall back to a snappy ramp (10x max speed per second) when accel/brake
	# were not authored, so missing fields never pin units at zero velocity.
	var acceleration := float(row.get("acceleration", 0.0))
	if acceleration <= 0.0:
		acceleration = max_speed * 10.0
	var braking := float(row.get("braking", 0.0))
	if braking <= 0.0:
		braking = max_speed * 10.0
	var current_speed := float(row.get("current_speed", 0.0))
	# Accelerate toward max; near the final waypoint begin braking for snappier
	# stops. Braking only ever applies on the last leg, so summing the whole
	# remaining route was wasted per-tick work.
	var stop_distance := (current_speed * current_speed) / maxf(0.001, 2.0 * braking)
	if route.size() <= 1 and position.distance_to(waypoint) <= stop_distance:
		current_speed = maxf(0.0, current_speed - braking * TICK_SECONDS)
	else:
		current_speed = minf(max_speed, current_speed + acceleration * TICK_SECONDS)
	row["current_speed"] = current_speed
	var step_distance := current_speed * TICK_SECONDS
	var movement_direction := position.direction_to(waypoint)
	if movement_direction.length_squared() > 0.000001:
		row["facing"] = movement_direction
	var pre_move_gap := position.distance_to(waypoint)
	if pre_move_gap <= maxf(step_distance, 0.001):
		position = waypoint
		route.pop_front()
	else:
		position += position.direction_to(waypoint) * step_distance
	if not bool(row.get("flying", false)):
		# Flyers pass straight over building footprints.
		position = _deflect_around_structures(position, int(row.get("target_id", 0)))
	# Grid routes ignore structure footprints, so a waypoint can sit inside a
	# blocked disc; deflection then pins the unit on the ring making zero
	# progress. Only a sustained stall pops the waypoint — a single flat tick
	# is normal while sliding tangentially around a footprint.
	if not route.is_empty() and Vector2(route[0]) == waypoint and step_distance > 0.001 \
			and position.distance_to(waypoint) >= pre_move_gap - 0.001:
		var stall_ticks := int(row.get("route_stall_ticks", 0)) + 1
		row["route_stall_ticks"] = stall_ticks
		if stall_ticks >= 3:
			row["route_stall_ticks"] = 0
			route.pop_front()
	else:
		row["route_stall_ticks"] = 0
	row["position"] = position
	row["route"] = route
	# Minimal cavalry trample while charging into enemies at speed.
	if String(row.get("category", "")) == "cavalry" and current_speed > max_speed * 0.4:
		_try_cavalry_trample(row)
	if route.is_empty():
		_clear_pending_route(row, int(row["target_id"]) == 0)
		if int(row["target_id"]) == 0:
			row["state"] = "idle"


func _try_cavalry_trample(row: Dictionary) -> void:
	## One bonus damage pulse when a charging cavalry battalion overlaps an
	## enemy within TRAMPLE_COLLISION_RADIUS. Cooldown prevents continuous
	## ticks. The struck battalion is also bowled over: knocked down and
	## displaced away from the charge through the shared knockback core
	## (retail infantry fly aside, sprawl, then stand up). Flyers are immune.
	var cooldown := int(row.get("trample_cooldown", 0))
	if cooldown > 0:
		row["trample_cooldown"] = cooldown - 1
		return
	var team := int(row.get("team", PLAYER_TEAM))
	var origin := Vector2(row.get("position", Vector2.ZERO))
	var best_id := 0
	var best_distance := TRAMPLE_COLLISION_RADIUS
	for candidate in _hostile_living_ids(team):
		if bool((entities[candidate] as Dictionary).get("flying", false)):
			continue
		var distance := origin.distance_to(Vector2((entities[candidate] as Dictionary).get("position", Vector2.ZERO)))
		if distance <= best_distance:
			best_distance = distance
			best_id = candidate
	if best_id == 0:
		return
	var damage := maxi(1, int(round(float(row.get("member_damage", 1)) * float(row.get("member_count", 1)) * TRAMPLE_DAMAGE_FACTOR * _timed_modifier_product(row, "CRUSH"))))
	# A braced shield wall blunts the charge (retail pike/shield counterplay).
	damage = maxi(1, roundi(float(damage) * float(_formation_effects(entities[best_id] as Dictionary).get("trample_damage_multiplier", 1.0))))
	_apply_damage(int(row.get("id", 0)), best_id, damage, "battalion")
	row["trample_cooldown"] = TRAMPLE_COOLDOWN_TICKS
	_emit_event("combat.trample", int(row.get("id", 0)), best_id, {"amount": damage, "category": "cavalry"})
	# Damage keeps its existing single-victim semantics above; the knockdown
	# sweep covers everything the charge plows through around the impact.
	_apply_knockback(origin, TRAMPLE_COLLISION_RADIUS, TRAMPLE_KNOCKBACK_STRENGTH, team, 0, "trample", int(row.get("id", 0)))


func _apply_knockback(center: Vector2, radius: float, strength: float, source_team: int, damage: int, damage_reason: String, source_id: int = 0) -> int:
	## Deterministic radial knockback: sweep enemy battalions in ascending id
	## order, throw each away from the center (clamped to walkable ground),
	## knock them down for KNOCKDOWN_DURATION_TICKS, and apply the optional
	## damage through the existing damage path. Allies and flyers are immune.
	var affected := 0
	for id in entity_ids():
		var row: Dictionary = entities[id]
		if int(row.get("team", -1)) == source_team or int(row.get("health", 0)) <= 0:
			continue
		if bool(row.get("flying", false)):
			# Airborne units cannot be bowled over by ground shockwaves.
			continue
		var position := Vector2(row.get("position", Vector2.ZERO))
		var distance := position.distance_to(center)
		if distance > radius:
			continue
		var direction := (position - center) / distance if distance > 0.001 else Vector2.RIGHT
		# Try the full throw first, then shorter deterministic fractions so a
		# victim near water/cliff lands on the nearest walkable spot instead
		# of being stranded on unwalkable cells.
		var landed := position
		for fraction in [1.0, 0.5, 0.25]:
			var candidate := position + direction * strength * float(fraction)
			if _position_walkable(candidate):
				landed = candidate
				break
		row["position"] = landed
		row["knockdown_ticks"] = KNOCKDOWN_DURATION_TICKS
		row["knocked_down"] = true
		row["current_speed"] = 0.0
		row["attack_windup"] = 0
		row["target_id"] = 0
		row["target_kind"] = "battalion"
		row["attack_move"] = false
		_clear_member_attack_schedule(row)
		_clear_member_targets(row)
		_clear_pending_route(row, true)
		row["state"] = "knocked_down"
		if damage > 0:
			_apply_damage(source_id, id, damage, "battalion")
		_emit_event("combat.knockback", source_id, id, {
			"reason": damage_reason,
			"center": [snappedf(center.x, 0.001), snappedf(center.y, 0.001)],
			"landed": [snappedf(landed.x, 0.001), snappedf(landed.y, 0.001)],
			"knockdown_ticks": KNOCKDOWN_DURATION_TICKS,
		})
		affected += 1
	return affected


func _apply_damage(attacker_id: int, target_id: int, amount: int, target_kind: String = "battalion") -> void:
	if target_kind == "structure":
		_apply_structure_damage(attacker_id, target_id, amount)
		return
	if not entities.has(target_id):
		return
	var target: Dictionary = entities[target_id]
	var remaining := maxi(0, amount)
	var damage_type := ""
	if entities.has(attacker_id):
		damage_type = String((entities[attacker_id] as Dictionary).get("damage_type", ""))
	while remaining > 0 and int(target.get("health", 0)) > 0:
		var target_member := _choose_target_member(target, attacker_id, 0, int(target.get("attack_sequence", 0)))
		if target_member < 0:
			break
		var health_values: Array = target.get("member_health", [])
		# Each member must receive exactly the raw amount its compiled factors
		# reduce to a kill; capping at current member health would grind
		# geometrically against sub-1.0 armor and stall short of lethal.
		var factor := _incoming_damage_factor(attacker_id, target, "battalion", damage_type)
		if factor <= 0.0:
			# A 0% armor match (e.g. LOGICAL_FIRE vs fortress) makes the hit a
			# retail no-op; the remaining raw damage has no path through.
			break
		var applied := mini(remaining, maxi(1, ceili(float(health_values[target_member]) / factor)))
		_apply_member_damage(attacker_id, -1, target_id, applied, "battalion", int(target.get("attack_sequence", 0)), target_member)
		remaining -= applied


func _incoming_damage_factor(attacker_id: int, target: Dictionary, target_kind: String, damage_type: String) -> float:
	## The full compiled multiplier an incoming hit applies before rounding:
	## weapon DamageScalar target filters, the armor.ini set (with recorded
	## applied armor upgrades), stance, formation, and ability/aura factors.
	var weapon_factor := 1.0
	if entities.has(attacker_id):
		var attacker_effect := _applied_weapon_effect(entities[attacker_id] as Dictionary)
		if not attacker_effect.is_empty():
			weapon_factor = _damage_scalar_factor(attacker_effect.get("scalars", []) as Array, target, target_kind)
	return (
		weapon_factor
		* _member_armor_scalar(target, damage_type)
		* float(_stance_state(target).get("incomingDamageMultiplier", 1.0))
		* float(_formation_effects(target).get("incoming_damage_multiplier", 1.0))
		* _ability_incoming_multiplier(target)
		* _grove_armor_factor(target)
	)


func _apply_member_damage(
	attacker_id: int,
	attacker_member_index: int,
	target_id: int,
	amount: int,
	target_kind: String,
	attack_sequence: int,
	forced_target_member: int = -1,
	damage_type_override: String = ""
) -> void:
	if target_kind == "structure":
		_apply_structure_damage(attacker_id, target_id, amount, damage_type_override)
		return
	if not entities.has(target_id):
		return
	var target: Dictionary = entities[target_id]
	if int(target.get("health", 0)) <= 0:
		return
	var health_values: Array = target.get("member_health", [])
	var target_member := forced_target_member
	if target_member < 0:
		target_member = _choose_target_member(target, attacker_id, attacker_member_index, attack_sequence)
	if target_member < 0 or target_member >= health_values.size():
		return
	var prior_health := int(health_values[target_member])
	if prior_health <= 0:
		return
	# armor.ini, compiled end-to-end: attacker damage type vs the victim's
	# authored ArmorSet (a recorded applied armor upgrade swaps the set).
	var damage_type := damage_type_override
	var weapon_factor := 1.0
	if entities.has(attacker_id):
		var attacker: Dictionary = entities[attacker_id]
		if damage_type == "":
			damage_type = String(attacker.get("damage_type", ""))
		var attacker_effect := _applied_weapon_effect(attacker)
		if not attacker_effect.is_empty():
			weapon_factor = _damage_scalar_factor(attacker_effect.get("scalars", []) as Array, target, target_kind)
	var stance_adjusted_amount := maxi(0, roundi(
		float(amount)
		* _incoming_damage_factor(attacker_id, target, target_kind, damage_type)
	))
	if entities.has(attacker_id) and tick_index < int((entities[attacker_id] as Dictionary).get("rally_until_tick", -1)):
		# Rallying Call: the converted SpellBookRallyingCallModifier leaf
		# (DAMAGE_MULT 150%, 60s) rides the row from cast time until expiry.
		stance_adjusted_amount = int(ceil(stance_adjusted_amount * float((entities[attacker_id] as Dictionary).get("rally_damage_mult", 1.5))))
	health_values[target_member] = maxi(0, prior_health - stance_adjusted_amount)
	target["member_health"] = health_values
	target["last_damage_tick"] = tick_index
	var aggregate_health := 0
	for health_value in health_values:
		aggregate_health += int(health_value)
	target["health"] = aggregate_health
	_emit_event("combat.hit", attacker_id, target_id, {
		"attacker_member_index": attacker_member_index,
		"target_member_index": target_member,
		"amount": mini(prior_health, stance_adjusted_amount),
		"target_member_health": int(health_values[target_member]),
		"target_object_id": String(target.get("object_id", "")),
		"damage_type": damage_type,
		"armor_scalar": _member_armor_scalar(target, damage_type),
		"weapon_factor": weapon_factor,
	})
	if prior_health > 0 and int(health_values[target_member]) == 0:
		var corpse_ticks: Array = target.get("member_corpse_expire_ticks", [])
		if target_member < corpse_ticks.size():
			corpse_ticks[target_member] = tick_index + CORPSE_LIFETIME_TICKS
			target["member_corpse_expire_ticks"] = corpse_ticks
		_emit_event("battalion.member_defeated", attacker_id, target_id, {"member_index": target_member, "object_id": String(target.get("object_id", ""))})
		# Veterancy: the kill pays the victim's authored ExperienceAward at the
		# victim's current level into the attacker's XP pool.
		_award_member_kill_experience(attacker_id, target)
	if int(target["target_id"]) == 0 and int(target["health"]) > 0 and entities.has(attacker_id) \
			and int(target.get("knockdown_ticks", 0)) <= 0 \
			and _can_engage_battalion(target, entities[attacker_id] as Dictionary):
		# Sprawled battalions cannot retaliate, and melee never chases an
		# airborne attacker it can never reach.
		var attacker_position := Vector2((entities[attacker_id] as Dictionary)["position"])
		var target_distance := Vector2(target["position"]).distance_to(attacker_position)
		if String(target.get("stance", "Battle")) == "HoldGround":
			# HoldGround retaliates without abandoning its post: engage only if
			# the attacker is already inside weapon range.
			if target_distance <= float(target.get("attack_range", 0.0)):
				target["target_id"] = attacker_id
				_stamp_order_sequence([target_id])
				_emit_music("battle")
		elif _assign_route(target, attacker_position):
			target["target_id"] = attacker_id
			_stamp_order_sequence([target_id])
			_emit_music("battle")
	if int(target["health"]) == 0:
		target["corpse_expire_tick"] = tick_index + CORPSE_LIFETIME_TICKS
		target["state"] = "death"
		target["target_id"] = 0
		_clear_pending_route(target, true)
		selected_ids.erase(target_id)
		# A produced hero's death releases its identity: the fortress may train
		# it again (retail hero revival at the fortress). Spawn-roster heroes
		# were never completed identities, so this only affects produced ones.
		var defeated_identity := "%d:%s" % [int(target.get("team", -1)), String(target.get("unit_type", ""))]
		if _completed_hero_identities.has(defeated_identity):
			_completed_hero_identities.erase(defeated_identity)
			_emit_event("hero.identity_released", target_id, 0, {"unit_type": String(target.get("unit_type", ""))})
		if base_loop_enabled:
			var target_team := int(target.get("team", -1))
			team_command_points[target_team] = maxi(0, command_points_for_team(target_team) - int(target.get("command_points", _rules.get("soldier_command_points", 60))))
		prune_control_groups()
		if entities.has(attacker_id):
			award_power_kill(int((entities[attacker_id] as Dictionary).get("team", -1)))
		_emit_event("battalion.defeated", attacker_id, target_id, {
			"object_id": String(target.get("object_id", "")),
			"team": int(target.get("team", -1)),
			"category": String(target.get("category", "")),
		})


func _cleanup_expired_corpses() -> void:
	var expired: Array[int] = []
	for id in entity_ids():
		var row: Dictionary = entities[id]
		var expire_tick := int(row.get("corpse_expire_tick", -1))
		if int(row.get("health", 0)) <= 0 and expire_tick >= 0 and tick_index >= expire_tick:
			expired.append(id)
	for id in expired:
		entities.erase(id)
		selected_ids.erase(id)
		_emit_event("battalion.corpse_expired", id, 0)
	if not expired.is_empty():
		prune_control_groups()


func _choose_target_member(
	target: Dictionary,
	attacker_id: int,
	attacker_member_index: int,
	attack_sequence: int
) -> int:
	var health_values: Array = target.get("member_health", [])
	if health_values.is_empty():
		return -1
	var start := posmod(attacker_id * 31 + maxi(0, attacker_member_index) * 17 + attack_sequence * 13, health_values.size())
	for offset in range(health_values.size()):
		var candidate := posmod(start + offset, health_values.size())
		if int(health_values[candidate]) > 0:
			return candidate
	return -1


func _apply_structure_damage(attacker_id: int, target_id: int, amount: int, damage_type_override: String = "") -> void:
	if not structures.has(target_id):
		return
	var target: Dictionary = structures[target_id]
	if int(target.get("health", 0)) <= 0:
		return
	var damage_type := damage_type_override
	if damage_type == "":
		damage_type = "default"
		if entities.has(attacker_id):
			damage_type = String((entities[attacker_id] as Dictionary).get("damage_type", "default"))
	# Every structure kind scales by its own compiled armor.ini table; kinds
	# without one use the recorded provisional (logged once at configure).
	var kind := String(target.get("structure_kind", ""))
	var table: Dictionary = _structure_armor.get(kind, {})
	var scalar := STRUCTURE_ARMOR_PROVISIONAL_SCALAR
	if not table.is_empty():
		var scalars: Dictionary = table.get("scalars", {})
		scalar = float(table.get("damage_scalar", 1.0)) * float(scalars.get(damage_type.to_lower(), scalars.get("default", 1.0)))
	var weapon_factor := 1.0
	if entities.has(attacker_id):
		var attacker_effect := _applied_weapon_effect(entities[attacker_id] as Dictionary)
		if not attacker_effect.is_empty():
			weapon_factor = _damage_scalar_factor(attacker_effect.get("scalars", []) as Array, target, "structure")
	var remainder_by_type: Dictionary = target.get("damage_remainders", {})
	var accumulated := float(remainder_by_type.get(damage_type, 0.0)) + float(maxi(0, amount)) * weapon_factor * scalar
	var applied := floori(accumulated)
	remainder_by_type[damage_type] = accumulated - float(applied)
	target["damage_remainders"] = remainder_by_type
	if applied <= 0:
		return
	target["health"] = maxi(0, int(target["health"]) - applied)
	var structure_kind := String(target.get("structure_kind", ""))
	_emit_event("combat.hit_structure", attacker_id, target_id, {
		"raw_amount": maxi(0, amount),
		"applied_amount": applied,
		"damage_type": damage_type,
		"armor_scalar": scalar,
		"structure_kind": structure_kind,
		"team": int(target.get("team", -1)),
		"health": int(target["health"]),
		"maximum_health": int(target.get("maximum_health", 0)),
	})
	if int(target.get("team", -1)) == PLAYER_TEAM and tick_index - _last_base_under_attack_tick >= EVA_BASE_UNDER_ATTACK_DEBOUNCE_TICKS:
		# EVA "base under attack" announces the first hit on a player structure,
		# then stays quiet for the retail 30s TimeBetweenEventsMS debounce.
		_last_base_under_attack_tick = tick_index
		_emit_event("eva.base_under_attack", 0, target_id, {"team": PLAYER_TEAM, "structure_kind": structure_kind})
	if int(target["health"]) == 0:
		var queue: Array = target.get("queue", [])
		# Queued costs stay spent, matching the deterministic no-refund contract.
		queue.clear()
		target["queue"] = queue
		var upgrade_queue: Array = target.get("upgrade_queue", [])
		upgrade_queue.clear()
		target["upgrade_queue"] = upgrade_queue
		# Authored RefundDie rows (Siege Materials) refund their compiled
		# percent when the owning team keeps the required building.
		_apply_structure_death_refund(target)
		_emit_event("structure.destroyed", attacker_id, target_id, {"structure_kind": structure_kind, "team": int(target.get("team", -1))})
		if int(target.get("team", -1)) == PLAYER_TEAM:
			_emit_event("eva.building_lost", 0, target_id, {"team": PLAYER_TEAM, "structure_kind": structure_kind})


func _target_alive(target_id: int, target_kind: String) -> bool:
	if target_kind == "structure":
		return structures.has(target_id) and int((structures[target_id] as Dictionary).get("health", 0)) > 0
	return entities.has(target_id) and int((entities[target_id] as Dictionary).get("health", 0)) > 0


func _target_position(target_id: int, target_kind: String) -> Vector2:
	var row: Dictionary = structures.get(target_id, {}) if target_kind == "structure" else entities.get(target_id, {})
	return Vector2(row.get("position", Vector2.ZERO))


func _update_ai_controllers() -> void:
	## Runs the single data-driven controller once per AI team, in ascending team
	## order, each on its own difficulty cadence. For the default {0,1} roster this
	## is exactly one team (team 1 @ medium == legacy), gated on `tick_index % 15`,
	## so it fires on the identical ticks — and issues the identical commands — the
	## old ENEMY_TEAM-bound AI did. Iteration is over the sorted AI-state keys so
	## multiple AI teams resolve deterministically regardless of insertion order.
	var teams: Array = _team_ai_state.keys()
	teams.sort()
	for team_value in teams:
		var team := int(team_value)
		var ai_state: Dictionary = _team_ai_state[team]
		var profile := _difficulty_profile(team)
		if tick_index % maxi(1, int(profile.get("scan_interval", 15))) != 0:
			continue
		_run_ai_for_team(team, profile, ai_state)


func _ensure_ai_state(team: int) -> Dictionary:
	## Lazily materialize a default (legacy/medium) AI-state record for a team so
	## the back-compat fixture seams below never touch a missing key.
	if not _team_ai_state.has(team):
		_team_ai_state[team] = {
			"difficulty": AI_DEFAULT_DIFFICULTY,
			"construction_attempted": false,
			"construction_resolved": false,
			"build_order_index": 0,
			"last_wave_tick": 0,
		}
	return _team_ai_state[team]


func _update_enemy_ai() -> void:
	## Back-compat shim: fixtures that drove the historical single ENEMY_TEAM AI
	## directly still resolve to the same code, now at the medium (== legacy) tier.
	_run_ai_for_team(ENEMY_TEAM, _difficulty_profile(ENEMY_TEAM), _ensure_ai_state(ENEMY_TEAM))


func ai_base_build_order() -> Array[String]:
	## Back-compat shim over the per-team derivation for the default ENEMY_TEAM.
	return _ai_build_order_for_team(ENEMY_TEAM)


func force_ai_construction_complete(team: int = ENEMY_TEAM) -> void:
	## Test seam: mark a team's AI construction finished and its build order
	## exhausted, freezing the base-building phase so a fixture can isolate a later
	## behavior deterministically (replaces the old three-scalar poke).
	var state := _ensure_ai_state(team)
	state["construction_attempted"] = true
	state["construction_resolved"] = true
	state["build_order_index"] = AI_BUILD_ORDER.size()


func _run_ai_for_team(team: int, profile: Dictionary, ai_state: Dictionary) -> void:
	if base_loop_enabled and not bool(ai_state.get("construction_attempted", false)):
		ai_state["construction_attempted"] = true
		if not _start_ai_farm(team):
			ai_state["construction_resolved"] = true
		else:
			ai_state["build_order_index"] = 1
	if base_loop_enabled and not bool(ai_state.get("construction_resolved", false)) and not _ai_construction_is_viable(team):
		_abandon_ai_construction(team)
		ai_state["construction_resolved"] = true
	if base_loop_enabled and not bool(ai_state.get("construction_resolved", false)):
		return
	if base_loop_enabled:
		_step_ai_base_building(team, ai_state)
	var queue_interval := maxi(15, int(_rules.get("ai_queue_interval_ticks", 60)) * int(profile.get("queue_interval_permille", 1000)) / 1000)
	if base_loop_enabled and tick_index % queue_interval == 0:
		var plan: Array = ai_production_plan_for_team(team)
		if plan.is_empty():
			plan = _ai_production_plan if not _ai_production_plan.is_empty() else AI_PRODUCTION_PLAN
		var team_rules := unit_production_rules_for_team(team)
		for unit_type in plan:
			var production_rule: Dictionary = team_rules.get(unit_type, {})
			if production_rule.is_empty():
				continue
			var producer := producer_id(team, String(production_rule.get("producer_kind", "")))
			if producer != 0:
				queue_unit(team, producer, unit_type)
	# Give hostiles one full production window before the first wave. Economy and
	# production still advance during the preparation time. Higher tiers commit
	# sooner (shorter attack delay), lower tiers later.
	var attack_delay := maxi(0, int(_rules.get("ai_attack_delay_ticks", 0)) * int(profile.get("attack_delay_permille", 1000)) / 1000)
	if base_loop_enabled and tick_index < attack_delay:
		return
	# Damaged battalions pull back to regroup (retreat/regroup is strictly hard+;
	# the neutral tiers pass a 0 threshold and this is a no-op, keeping the default
	# match byte-identical).
	if base_loop_enabled:
		_ai_apply_retreat(team, profile)
	var weakest := bool(profile.get("weakest_fortress_priority", false))
	var hostiles := _hostile_living_ids(team)
	var enemy_fortress := _ai_primary_hostile_fortress(team, weakest) if base_loop_enabled else 0
	if hostiles.is_empty() and enemy_fortress == 0:
		return
	# Fresh units mass into a wave at the fortress muster point and strike
	# together instead of trickling one battalion at a time.
	if base_loop_enabled:
		var wave_size := maxi(2, int(_rules.get("ai_wave_size", 4)) + int(profile.get("wave_size_delta", 0)))
		var mustering: Array[int] = []
		for id in living_ids(team):
			var row: Dictionary = entities[id]
			if bool(row.get("is_builder", false)) or bool(row.get("ai_in_wave", false)):
				continue
			if int(row["target_id"]) != 0:
				continue
			mustering.append(id)
		# A stalled economy must not hold the last understrength group at the
		# muster point forever — after the patience window it attacks anyway.
		var patience := int(_rules.get("ai_wave_patience_ticks", 1200)) * int(profile.get("wave_patience_permille", 1000)) / 1000
		var wave_ready := mustering.size() >= wave_size
		if not wave_ready and not mustering.is_empty() and tick_index - int(ai_state.get("last_wave_tick", 0)) > patience:
			wave_ready = true
		if wave_ready and not mustering.is_empty():
			ai_state["last_wave_tick"] = tick_index
			for id in mustering:
				(entities[id] as Dictionary)["ai_in_wave"] = true
		else:
			var muster := _fallback_rally_position(team)
			var muster_fortress := fortress_id(team)
			if muster_fortress != 0:
				muster = Vector2((structures[muster_fortress] as Dictionary).get("rally", muster))
			for id in mustering:
				var row: Dictionary = entities[id]
				if (row["route"] as Array).is_empty() and Vector2(row["position"]).distance_to(muster) > 6.0:
					if _assign_route(row, muster):
						row["state"] = "run"
	for id in living_ids(team):
		var row: Dictionary = entities[id]
		# Builders construct; they are not combat battalions and have no weapon.
		if bool(row.get("is_builder", false)):
			continue
		if base_loop_enabled and not bool(row.get("ai_in_wave", false)):
			continue
		if int(row["target_id"]) != 0:
			continue
		var target_id := 0
		var target_kind := "battalion"
		if hostiles.is_empty() and enemy_fortress != 0:
			target_id = enemy_fortress
			target_kind = "structure"
		else:
			target_id = hostiles[0]
			var closest_distance := Vector2(row["position"]).distance_to(Vector2((entities[target_id] as Dictionary)["position"]))
			for candidate in hostiles:
				var distance := Vector2(row["position"]).distance_to(Vector2((entities[candidate] as Dictionary)["position"]))
				if distance < closest_distance or (is_equal_approx(distance, closest_distance) and candidate < target_id):
					target_id = candidate
					closest_distance = distance
		var target_position := _target_position(target_id, target_kind)
		var target_distance := Vector2(row["position"]).distance_to(target_position)
		if target_kind == "structure":
			# Once no defending battalion remains, the objective fortress is the
			# strategic target. Assign it before routing so the target's own
			# footprint is exempt and melee units can close to weapon range.
			if _assign_route(row, target_position):
				row["target_id"] = target_id
				row["target_kind"] = target_kind
				row["attack_windup"] = 0
				row["state"] = "run"
				_stamp_order_sequence([id])
				_emit_music("battle")
			continue
		var vision_range := maxf(float(row.get("attack_range", 1.15)), float(row.get("vision_range", 17.5)))
		if target_distance > vision_range:
			# Strategic AI can advance toward the opposing base, but it does not gain
			# a live combat target or attack animation through unexplored distance.
			# Advance toward the candidate we are actually trying to acquire; when no
			# hostile battalions remain, target_position is the objective fortress.
			var strategic_destination := target_position
			if (row["route"] as Array).is_empty() or Vector2(row.get("destination", row["position"])).distance_to(strategic_destination) > 1.0:
				if _assign_route(row, strategic_destination):
					row["target_id"] = 0
					row["target_kind"] = "battalion"
					row["attack_windup"] = 0
					row["state"] = "run"
					_stamp_order_sequence([id])
			continue
		if _assign_route(row, target_position):
			row["target_id"] = target_id
			row["target_kind"] = target_kind
			row["attack_windup"] = 0
			row["state"] = "run"
			_stamp_order_sequence([id])
			_emit_music("battle")


func _ai_primary_hostile_fortress(team: int, weakest: bool) -> int:
	## The objective fortress for `team`. Default (closest) tiers take the lowest-id
	## hostile team's fortress; for the {0,1} roster that is exactly
	## `fortress_id(PLAYER_TEAM)`, so the default path is byte-identical. Weakest-
	## priority tiers (brutal/morgoth) instead march the whole wave onto the
	## lowest-health hostile fortress (deterministic, tie-broken by id).
	var best := 0
	var best_health := 0
	for other_value in _roster_team_ids():
		var other := int(other_value)
		if not _is_hostile(team, other):
			continue
		var fortress := fortress_id(other)
		if fortress == 0:
			continue
		var health := int((structures[fortress] as Dictionary).get("health", 0))
		if best == 0:
			best = fortress
			best_health = health
		elif weakest and health < best_health:
			best = fortress
			best_health = health
	return best


func _ai_apply_retreat(team: int, profile: Dictionary) -> void:
	## Army management (hard+): a committed battalion whose surviving members have
	## fallen below the tier's retreat fraction pulls out of the current wave and
	## routes home to regroup, rejoining the next muster. The threshold is a
	## permille of member count (integer, no floats), so a 0 threshold — every
	## neutral tier — returns immediately and never perturbs the default match.
	var permille := int(profile.get("retreat_member_permille", 0))
	if permille <= 0:
		return
	var muster := _fallback_rally_position(team)
	var muster_fortress := fortress_id(team)
	if muster_fortress != 0:
		muster = Vector2((structures[muster_fortress] as Dictionary).get("rally", muster))
	for id in living_ids(team):
		var row: Dictionary = entities[id]
		if bool(row.get("is_builder", false)) or not bool(row.get("ai_in_wave", false)):
			continue
		# A battalion already storming an enemy structure presses the assault — it
		# does not turn tail at the gates (turtling there would cede the base race).
		if String(row.get("target_kind", "")) == "structure" and int(row.get("target_id", 0)) != 0:
			continue
		var members: Array = row.get("member_health", [])
		if members.is_empty():
			continue
		var alive := 0
		for value in members:
			if int(value) > 0:
				alive += 1
		if alive * 1000 >= members.size() * permille:
			continue
		row["ai_in_wave"] = false
		row["target_id"] = 0
		row["target_kind"] = "battalion"
		row["attack_windup"] = 0
		if _assign_route(row, muster):
			row["state"] = "run"


# After the proven first farm, the AI keeps developing: economy first, then
# one of each military producer. Provisional order pending a real strategy
# layer; every site goes through the same admission path as a player build.
const AI_BUILD_ORDER: Array[String] = [
	"farm", "barracks", "farm", "archery_range", "stable", "farm", "workshop",
]


func _step_ai_base_building(team: int, ai_state: Dictionary) -> void:
	# A dead porter is retrained first, even once the authored build order is
	# exhausted (factions with few buildable kinds would otherwise never
	# recover their builder).
	var living_builders: Array[int] = []
	for id in living_ids(team):
		if bool((entities[id] as Dictionary).get("is_builder", false)):
			living_builders.append(id)
	if living_builders.is_empty():
		_ai_train_builder(team)
		return
	var order := _ai_build_order_for_team(team)
	var index := int(ai_state.get("build_order_index", 0))
	if index >= order.size():
		return
	if _ai_construction_is_viable(team):
		return
	var kind := String(order[index])
	var build_rule: Dictionary = structure_build_rules_for_team(team).get(kind, {})
	if build_rule.is_empty():
		ai_state["build_order_index"] = index + 1
		return
	if resources_for_team(team) < int(build_rule.get("cost", 0)):
		return
	var anchor := Vector2((entities[living_builders[0]] as Dictionary).get("position", Vector2.ZERO))
	var team_fortress := fortress_id(team)
	if team_fortress != 0:
		anchor = Vector2((structures[team_fortress] as Dictionary).get("position", Vector2.ZERO))
	# The default (medium/easy) search is exactly the historical four rings; tiers
	# that build extra producers get additional outer rings so the larger base has
	# room. Untouched for the default match, so its placement stays byte-identical.
	var radii: Array = [10.0, 14.0, 18.0, 22.0]
	if int(_difficulty_profile(team).get("extra_producer_cycles", 0)) > 0:
		radii = [10.0, 14.0, 18.0, 22.0, 26.0, 30.0, 34.0, 38.0]
	for radius_value in radii:
		var radius := float(radius_value)
		for direction_index in range(8):
			var angle := TAU * float(direction_index) / 8.0
			var candidate: Vector2 = anchor + Vector2(cos(angle), sin(angle)) * radius
			if bool(_issue_construct_for_team(team, living_builders, kind, candidate).get("ok", false)):
				ai_state["build_order_index"] = index + 1
				return


func _ai_build_order_for_team(team: int) -> Array[String]:
	## Faction-derived build order: farm first when the faction declares one,
	## then each AI-plan unit's producer kind in plan order. The fortress is
	## never rebuilt (it is the seeded structure). The historical Men order is
	## just this derivation over the Gondor plan.
	##
	## Economy aggression + build variety (hard and up): the tier appends extra
	## farm->producer CYCLES, giving higher tiers a genuinely larger production
	## base (more parallel producers + income) — the lasting military-throughput
	## edge that banked gold alone cannot buy against a producer-bound opponent.
	## medium/easy add zero cycles, so the default build order is byte-identical.
	var order: Array[String] = []
	var has_farm := structure_build_rules_for_team(team).has("farm")
	var team_rules := unit_production_rules_for_team(team)
	var producers: Array[String] = []
	for unit_type_value in ai_production_plan_for_team(team):
		var rule: Dictionary = team_rules.get(String(unit_type_value), {}) as Dictionary
		var kind := String(rule.get("producer_kind", ""))
		if kind != "" and kind != "fortress" and not producers.has(kind):
			producers.append(kind)
	if has_farm:
		order.append("farm")
	for kind in producers:
		order.append(kind)
	var cycles := int(_difficulty_profile(team).get("extra_producer_cycles", 0))
	for _cycle in range(cycles):
		if has_farm:
			order.append("farm")
		for kind in producers:
			order.append(kind)
	return order


func _ai_train_builder(team: int) -> void:
	## The porter died: retrain it from the fortress so the base can keep
	## developing. Never double-queue a builder that is already alive or
	## already in a production queue.
	var manifest: Dictionary = team_manifest_for(team)
	var team_rules := unit_production_rules_for_team(team)
	for builder_value in manifest.get("builder_unit_ids", []) as Array:
		var builder_id := String(builder_value)
		var already_covered := false
		for id in living_ids(team):
			if String((entities[id] as Dictionary).get("object_id", "")) == builder_id:
				already_covered = true
				break
		if not already_covered:
			for structure_id in structure_ids(team):
				for item_value in Array((structures[structure_id] as Dictionary).get("queue", [])):
					if String((item_value as Dictionary).get("unit_type", "")) == builder_id:
						already_covered = true
						break
				if already_covered:
					break
		if already_covered:
			return
		for unit_type_value in team_rules.keys():
			var unit_type := String(unit_type_value)
			var rule: Dictionary = team_rules[unit_type]
			if String(rule.get("object_id", "")) != builder_id:
				continue
			var producer := producer_id(team, String(rule.get("producer_kind", "fortress")))
			if producer != 0:
				queue_unit(team, producer, unit_type)
			return


func _start_ai_farm(team: int) -> bool:
	var builder_ids: Array[int] = []
	for id in living_ids(team):
		if bool((entities[id] as Dictionary).get("is_builder", false)):
			builder_ids.append(id)
	if builder_ids.is_empty():
		return false
	var builder_position := Vector2((entities[builder_ids[0]] as Dictionary).get("position", Vector2.ZERO))
	# A bounded clockwise search is deterministic and uses the same admission,
	# obstruction, route, cost, and construction path as a player MenPorter.
	for direction in [Vector2.UP, Vector2.RIGHT, Vector2.DOWN, Vector2.LEFT]:
		var candidate: Vector2 = builder_position + direction * 10.0
		var result := _issue_construct_for_team(team, builder_ids, "farm", candidate)
		if bool(result.get("ok", false)):
			return true
	return false


func _ai_construction_is_viable(team: int) -> bool:
	for id in living_ids(team):
		var builder: Dictionary = entities[id]
		if not bool(builder.get("is_builder", false)):
			continue
		var construction_id := int(builder.get("construction_id", 0))
		if construction_id != 0 and structures.has(construction_id) and int((structures[construction_id] as Dictionary).get("health", 0)) > 0:
			return true
	return false


func _abandon_ai_construction(team: int) -> void:
	for id in entity_ids():
		var builder: Dictionary = entities[id]
		if int(builder.get("team", -1)) != team or not bool(builder.get("is_builder", false)) or int(builder.get("construction_id", 0)) == 0:
			continue
		var construction_id := int(builder.get("construction_id", 0))
		if structures.has(construction_id):
			var site: Dictionary = structures[construction_id]
			site["builder_id"] = 0
			if int(site.get("health", 0)) > 0 and float(site.get("construction_progress", 1.0)) < 1.0:
				site["health"] = 0
				_emit_event("structure.destroyed", 0, construction_id, {"reason": "construction-builder-unavailable"})
		builder["construction_id"] = 0
		builder["order_kind"] = ""
		_clear_pending_route(builder, true)
		if int(builder.get("health", 0)) > 0:
			builder["state"] = "idle"


func _build_route(from: Vector2, to: Vector2) -> Array[Vector2]:
	var result := _query_route(from, to)
	if not bool(result.get("valid", false)):
		return []
	var points: Array[Vector2] = []
	points.assign(result.get("points", []))
	return points


func _assign_route(row: Dictionary, destination: Vector2) -> bool:
	if bool(row.get("flying", false)):
		# Flyers ignore ground navigation entirely: straight-line route over
		# water, void, and structures — no walkability query, no ford logic.
		var direct: Array[Vector2] = []
		direct.append(destination)
		var no_cells: Array[Vector2i] = []
		row["destination"] = destination
		row["route"] = direct
		row["route_cells"] = no_cells
		row["route_ford"] = ""
		return true
	var result := _query_route(Vector2(row["position"]), destination)
	if not bool(result.get("valid", false)):
		last_route_rejection = String(result.get("reason", "route-rejected"))
		return false
	var points: Array[Vector2] = []
	points.assign(result.get("points", []))
	if points.is_empty():
		last_route_rejection = String(result.get("reason", ""))
		if last_route_rejection.is_empty():
			last_route_rejection = "empty-route"
		return false
	var cells: Array[Vector2i] = []
	cells.assign(result.get("cells", []))
	row["destination"] = destination
	row["route"] = points
	row["route_cells"] = cells
	row["route_ford"] = String(result.get("ford_name", ""))
	return not points.is_empty()


func _query_route(from: Vector2, to: Vector2) -> Dictionary:
	if route_provider != null and route_provider.has_method("query_route"):
		var value: Variant = route_provider.call("query_route", from, to)
		if typeof(value) == TYPE_DICTIONARY:
			return value as Dictionary
	# The non-retail fallback remains bounded and direct. The selected retail
	# slice cannot reach this branch because configuration requires a provider.
	return {"valid": true, "reason": "", "points": [to], "cells": [], "ford_name": ""}


func _team_defeated(team: int) -> bool:
	## A team is eliminated when its fortress is razed (base loop) or it has no
	## living battalions (non-base). Exactly the old per-team fortress/liveness
	## test: a team that never held a fortress (harness sims with none seeded) is
	## NOT counted as defeated in base-loop mode, so those sims never spuriously
	## resolve — the pinned battle, whose fortresses persist at 0 health when
	## razed, still resolves the instant one falls.
	if base_loop_enabled:
		var fortress := fortress_id(team)
		return fortress != 0 and int((structures[fortress] as Dictionary).get("health", 0)) <= 0
	return living_ids(team).is_empty()


func _surviving_teams() -> Array:
	## Roster teams that are not yet eliminated, in roster order.
	var survivors: Array = []
	for team in _roster_team_ids():
		if not _team_defeated(team):
			survivors.append(team)
	return survivors


func _resolve_victory() -> void:
	## Last-alliance-standing over the whole roster. The match ends when no two
	## surviving teams are mutually hostile (a single alliance, or one team, or
	## none remain). `winner` stays a single int for snapshot compat: the LOWEST
	## surviving team id (documented tie-break); with no survivors it is the
	## lowest rostered team so a mutual wipe still terminates deterministically.
	## For the historical {0,1} roster this reduces to the old team0-vs-team1
	## comparison and emits the identical observer-frame events, so the pinned
	## battle signature does not move.
	var survivors := _surviving_teams()
	for i in survivors.size():
		for j in range(i + 1, survivors.size()):
			if _is_hostile(int(survivors[i]), int(survivors[j])):
				return
	if survivors.is_empty():
		var roster := _roster_team_ids()
		winner = int(roster[0]) if not roster.is_empty() else PLAYER_TEAM
	else:
		winner = int(survivors[0])
	for id in living_ids(winner):
		var row: Dictionary = entities[id]
		row["target_id"] = 0
		_clear_pending_route(row, true)
		row["state"] = "victory"
	# EVA/music are framed off the sim's local observer (PLAYER_TEAM): victorious
	# when the observer's alliance is the surviving one, defeated otherwise. A
	# true per-observer HUD frame is a HUD-packet concern; the sim carries the
	# single-observer frame the default path has always emitted.
	var observer_won := winner != -1 and not _is_hostile(PLAYER_TEAM, winner)
	if observer_won:
		_emit_event("match.victory", 0, 0)
		_emit_event("eva.enemy_defeated", 0, 0, {"team": PLAYER_TEAM})
		_emit_music("victory")
	else:
		_emit_event("match.defeat", 0, 0)
		_emit_event("eva.ally_defeated", 0, 0, {"team": PLAYER_TEAM})
		_emit_music("defeat")


func _is_commandable(id: int) -> bool:
	return _is_commandable_for_team(id, PLAYER_TEAM)


func _is_commandable_for_team(id: int, team: int) -> bool:
	# Knocked-down battalions are incapacitated: orders bounce off until the
	# battalion stands back up (retail sprawled infantry take no commands).
	return entities.has(id) and int((entities[id] as Dictionary)["team"]) == team and int((entities[id] as Dictionary)["health"]) > 0 and int((entities[id] as Dictionary).get("knockdown_ticks", 0)) <= 0 and winner == -1


func _is_melee_attacker(row: Dictionary) -> bool:
	## Melee-vs-ranged discriminator for flyer targeting (see the threshold
	## constant): source-unit weapon range is the honest, scale-invariant
	## signal present on every converted unit rule.
	return float(row.get("attack_range_source", 0.0)) < MELEE_ATTACK_RANGE_SOURCE_THRESHOLD


func _can_engage_battalion(attacker: Dictionary, target: Dictionary) -> bool:
	## Flyers soar out of melee reach: only ranged weapons can acquire or hit
	## an airborne battalion.
	return not (bool(target.get("flying", false)) and _is_melee_attacker(attacker))


func _is_living_player(id: int) -> bool:
	return entities.has(id) and int((entities[id] as Dictionary)["team"]) == PLAYER_TEAM and int((entities[id] as Dictionary)["health"]) > 0


func _prune_control_group(group: int) -> void:
	var retained: Array[int] = []
	for value: Variant in Array(control_groups.get(group, [])):
		var id := int(value)
		if _is_living_player(id) and not retained.has(id):
			retained.append(id)
	retained.sort()
	control_groups[group] = retained


func _stamp_order_sequence(ids: Array[int]) -> int:
	var sequence := _next_order_sequence
	_next_order_sequence += 1
	for id in ids:
		if entities.has(id):
			(entities[id] as Dictionary)["order_sequence"] = sequence
	return sequence


func _clear_pending_route(row: Dictionary, settle_destination: bool) -> void:
	row["route"] = []
	row["route_cells"] = []
	row["route_ford"] = ""
	if settle_destination:
		row["destination"] = Vector2(row["position"])


func _emit_music(state: String) -> void:
	if state == _music_state:
		return
	_music_state = state
	_emit_event("music.%s" % state, 0, 0)


func _emit_event(kind: String, entity_id: int, target_id: int, data: Dictionary = {}) -> void:
	var event := {
		"sequence": _next_event_sequence,
		"tick": tick_index,
		"kind": kind,
		"entity_id": entity_id,
		"target_id": target_id,
	}
	for key in data.keys():
		if not event.has(key):
			event[key] = data[key]
	events.append(event)
	for byte in JSON.stringify(event).to_utf8_buffer():
		_event_digest = ((_event_digest ^ int(byte)) * 16777619) & 0xFFFFFFFF
	_next_event_sequence += 1


func compact_consumed_events() -> int:
	if events.size() <= MAX_RETAINED_EVENT_HISTORY:
		return 0
	var retained: Array[Dictionary] = []
	var retained_by_kind: Dictionary = {}
	var retained_structure_targets: Dictionary = {}
	for event in events:
		var kind := String(event.get("kind", ""))
		var retention_key := kind
		var retention_limit := MAX_RETAINED_EVENTS_PER_KIND
		if kind == "combat.hit_structure" or kind == "structure.destroyed":
			if int(retained_structure_targets.get(kind, 0)) >= MAX_RETAINED_STRUCTURE_TARGETS_PER_KIND:
				continue
			retention_key = "%s:%d" % [kind, int(event.get("target_id", 0))]
			retention_limit = 1
		var retained_count := int(retained_by_kind.get(retention_key, 0))
		if retained_count >= retention_limit:
			continue
		retained.append(event)
		retained_by_kind[retention_key] = retained_count + 1
		if kind == "combat.hit_structure" or kind == "structure.destroyed":
			retained_structure_targets[kind] = int(retained_structure_targets.get(kind, 0)) + 1
		if retained.size() >= MAX_RETAINED_EVENT_HISTORY:
			break
	var removed := events.size() - retained.size()
	events = retained
	return removed


func state_snapshot() -> Dictionary:
	var rows: Array[Dictionary] = []
	for id in entity_ids():
		var entity_row: Dictionary = entities[id]
		var position := Vector2(entity_row["position"])
		var destination := Vector2(entity_row["destination"])
		var route_rows: Array[Array] = []
		for value: Variant in Array(entity_row.get("route", [])):
			var point := Vector2(value)
			route_rows.append([snappedf(point.x, 0.001), snappedf(point.y, 0.001)])
		var route_cell_rows: Array[Array] = []
		for value: Variant in Array(entity_row.get("route_cells", [])):
			var cell := Vector2i(value)
			route_cell_rows.append([cell.x, cell.y])
		rows.append({
			"id": id,
			"team": int(entity_row["team"]),
			"name": String(entity_row.get("name", "")),
			"object_id": String(entity_row.get("object_id", SOLDIER_OBJECT_ID)),
			"unit_type": String(entity_row.get("unit_type", SOLDIER_HORDE_ID)),
			"command_points": int(entity_row.get("command_points", 0)),
			"position": [snappedf(position.x, 0.001), snappedf(position.y, 0.001)],
			"facing": [snappedf(float((entity_row.get("facing", Vector2.RIGHT) as Vector2).x), 0.001), snappedf(float((entity_row.get("facing", Vector2.RIGHT) as Vector2).y), 0.001)],
			"destination": [snappedf(destination.x, 0.001), snappedf(destination.y, 0.001)],
			"state": String(entity_row["state"]),
			"target": int(entity_row["target_id"]),
			"target_kind": String(entity_row.get("target_kind", "battalion")),
			"health": int(entity_row["health"]),
			"member_maximum_health": int(entity_row.get("member_maximum_health", 0)),
			"member_health": Array(entity_row.get("member_health", [])).duplicate(),
			"level": int(entity_row.get("level", 1)),
			"experience_xp": int(entity_row.get("experience_xp", 0)),
			"attack_cooldown": int(entity_row.get("attack_cooldown", 0)),
			"attack_windup": int(entity_row.get("attack_windup", 0)),
			"attack_sequence": int(entity_row.get("attack_sequence", 0)),
			"continuous_fire_count": int(entity_row.get("continuous_fire_count", 0)),
			"continuous_fire_expiration_tick": int(entity_row.get("continuous_fire_expiration_tick", -1)),
			"member_attack_tokens": Array(entity_row.get("member_attack_tokens", [])).duplicate(),
			"member_attack_start_ticks": Array(entity_row.get("member_attack_start_ticks", [])).duplicate(),
			"member_attack_hit_ticks": Array(entity_row.get("member_attack_hit_ticks", [])).duplicate(),
			"attack_range": snappedf(float(entity_row.get("attack_range", 0.0)), 0.001),
			"vision_range": snappedf(float(entity_row.get("vision_range", 0.0)), 0.001),
			"damage_type": String(entity_row.get("damage_type", "")),
			"applied_upgrades": _sorted_upgrade_ids(entity_row.get("applied_upgrades", {})),
			"active_armor_upgrade": String(entity_row.get("active_armor_upgrade", "")),
			"production_producer_id": int(entity_row.get("production_producer_id", 0)),
			"production_exit_start_tick": int(entity_row.get("production_exit_start_tick", -1)),
			"production_exit_duration_ticks": int(entity_row.get("production_exit_duration_ticks", 0)),
			"production_exit_progress": snappedf(float(entity_row.get("production_exit_progress", 1.0)), 0.001),
			"route": route_rows,
			"route_cells": route_cell_rows,
			"route_ford": String(entity_row.get("route_ford", "")),
			"order_sequence": int(entity_row.get("order_sequence", 0)),
			"flying": bool(entity_row.get("flying", false)),
			"knocked_down": bool(entity_row.get("knocked_down", false)),
			"knockdown_ticks": int(entity_row.get("knockdown_ticks", 0)),
		})
	var structure_rows: Array[Dictionary] = []
	for id in structure_ids():
		var structure_row: Dictionary = structures[id]
		var position := Vector2(structure_row.get("position", Vector2.ZERO))
		var rally := Vector2(structure_row.get("rally", Vector2.ZERO))
		var queue_rows: Array[Dictionary] = []
		for item_value in Array(structure_row.get("queue", [])):
			if typeof(item_value) != TYPE_DICTIONARY:
				continue
			var item: Dictionary = item_value
			queue_rows.append({
				"unit_type": String(item.get("unit_type", "")),
				"cost": int(item.get("cost", 0)),
				"command_points": int(item.get("command_points", 0)),
				"queued_tick": int(item.get("queued_tick", 0)),
				"start_tick": int(item.get("start_tick", item.get("queued_tick", 0))),
				"duration_ticks": int(item.get("duration_ticks", 0)),
				"complete_tick": int(item.get("complete_tick", 0)),
			})
		var upgrade_queue_rows: Array[Dictionary] = []
		for item_value in Array(structure_row.get("upgrade_queue", [])):
			if typeof(item_value) != TYPE_DICTIONARY:
				continue
			var item: Dictionary = item_value
			upgrade_queue_rows.append({
				"upgrade_id": String(item.get("upgrade_id", "")),
				"cost": int(item.get("cost", 0)),
				"queued_tick": int(item.get("queued_tick", 0)),
				"duration_ticks": int(item.get("duration_ticks", 0)),
				"complete_tick": int(item.get("complete_tick", 0)),
				"cancelable": bool(item.get("cancelable", false)),
			})
		structure_rows.append({
			"id": id,
			"team": int(structure_row.get("team", -1)),
			"kind": String(structure_row.get("structure_kind", "")),
			"position": [snappedf(position.x, 0.001), snappedf(position.y, 0.001)],
			"rally": [snappedf(rally.x, 0.001), snappedf(rally.y, 0.001)],
			"health": int(structure_row.get("health", 0)),
			"maximum_health": int(structure_row.get("maximum_health", 0)),
			"construction_progress": snappedf(float(structure_row.get("construction_progress", 0.0)), 0.001),
			"level": int(structure_row.get("level", 1)),
			"completed_upgrades": Array(structure_row.get("completed_upgrades", [])).duplicate(),
			"damage_remainders": (structure_row.get("damage_remainders", {}) as Dictionary).duplicate(true),
			"queue": queue_rows,
			"upgrade_queue": upgrade_queue_rows,
		})
	var gate_rows: Array[Dictionary] = []
	for gate in ford_gates:
		var edge_a := Vector2(gate.get("edge_a", Vector2.ZERO))
		var edge_b := Vector2(gate.get("edge_b", Vector2.ZERO))
		gate_rows.append({
			"name": String(gate.get("name", "")),
			"edge_a": [snappedf(edge_a.x, 0.001), snappedf(edge_a.y, 0.001)],
			"edge_b": [snappedf(edge_b.x, 0.001), snappedf(edge_b.y, 0.001)],
		})
	var completed_hero_identities: Array[String] = []
	for identity_value in _completed_hero_identities.keys():
		completed_hero_identities.append(String(identity_value))
	completed_hero_identities.sort()
	# Roster-ordered per-team arrays (N-team foundation). For the default {0,1}
	# roster these serialize byte-identically to the prior 2-element literals —
	# same order, same values — so the pinned battle signature does not move.
	var resources_row: Array = []
	var command_points_row: Array = []
	var next_dynamic_ids_row: Array = []
	var power_points_row: Array = []
	var purchased_powers_row: Array = []
	var team_upgrades_row: Array = []
	for team in _roster_team_ids():
		resources_row.append(resources_for_team(team))
		command_points_row.append(command_points_for_team(team))
		next_dynamic_ids_row.append(int(_next_dynamic_id.get(team, 10 + team * 100)))
		power_points_row.append(power_points(team))
		purchased_powers_row.append((purchased_powers.get(team, []) as Array).duplicate())
		team_upgrades_row.append((team_upgrades.get(team, {}) as Dictionary).duplicate(true))
	return {
		"tick": tick_index,
		"winner": winner,
		"base_loop_enabled": base_loop_enabled,
		"resources": resources_row,
		"command_points": command_points_row,
		"command_point_cap": command_point_cap,
		"next_dynamic_ids": next_dynamic_ids_row,
		"selected": selected_ids.duplicate(),
		"control_groups": control_groups_snapshot(),
		"next_order_sequence": _next_order_sequence,
		"music": _music_state,
		"source_map_configured": source_map_configured,
		"ford_gates": gate_rows,
		"completed_hero_identities": completed_hero_identities,
		# Spellbook/tech state is gameplay-relevant (casts, team damage
		# multipliers), so it is part of the deterministic snapshot, not an
		# undocumented side channel.
		"power_points": power_points_row,
		"purchased_powers": purchased_powers_row,
		"team_upgrades": team_upgrades_row,
		"entities": rows,
		"structures": structure_rows,
		"next_event_sequence": _next_event_sequence,
		"event_digest": _event_digest,
	}


func state_signature() -> String:
	var value: int = 0x811C9DC5
	for byte in JSON.stringify(state_snapshot()).to_utf8_buffer():
		value = ((value ^ int(byte)) * 16777619) & 0xFFFFFFFF
	return "%08X" % value


func submit_command(cmd: Dictionary) -> bool:
	if not CommandScript.validate(cmd) or int(cmd["tick"]) <= tick_index:
		return false
	var command_tick := int(cmd["tick"])
	var commands: Array = _pending_commands.get(command_tick, [])
	commands.append(cmd.duplicate(true))
	_pending_commands[command_tick] = commands
	return true


func apply_command(cmd: Dictionary) -> void:
	last_command_result = null
	if not CommandScript.validate(cmd):
		return
	var args: Dictionary = cmd["args"]
	var team := int(cmd["team"])
	match String(cmd["type"]):
		"issue_move":
			last_command_result = issue_move(_command_ids(args.get("ids", [])), Vector2(args.get("destination", Vector2.ZERO)), "order.move", team)
		"issue_attack":
			last_command_result = issue_attack(_command_ids(args.get("ids", [])), int(args.get("target_id", 0)), team)
		"issue_attack_move":
			last_command_result = issue_attack_move(_command_ids(args.get("ids", [])), Vector2(args.get("destination", Vector2.ZERO)), team)
		"issue_stop":
			last_command_result = issue_stop(_command_ids(args.get("ids", [])), team)
		"issue_toggle_stance":
			last_command_result = issue_toggle_stance(_command_ids(args.get("ids", [])), team)
		"issue_set_stance":
			last_command_result = issue_set_stance(_command_ids(args.get("ids", [])), String(args.get("stance", "Battle")), team)
		"issue_toggle_formation":
			last_command_result = issue_toggle_formation(_command_ids(args.get("ids", [])), team)
		"issue_set_formation":
			last_command_result = issue_set_formation(_command_ids(args.get("ids", [])), String(args.get("formation", "Line")), team)
		"issue_construct":
			last_command_result = issue_construct(_command_ids(args.get("ids", [])), String(args.get("structure_kind", "")), Vector2(args.get("position", Vector2.ZERO)), bool(args.get("dry_run", false)), team)
		"issue_expansion_construct":
			last_command_result = issue_expansion_construct(team, int(args.get("fortress_id", 0)), String(args.get("expansion_kind", "")), int(args.get("pad_index", -1)))
		"queue_unit":
			last_command_result = queue_unit(team, int(args.get("producer", 0)), String(args.get("unit_type", SOLDIER_HORDE_ID)))
		"queue_structure_upgrade":
			last_command_result = queue_structure_upgrade(team, int(args.get("structure_id", 0)), String(args.get("upgrade_id", "")))
		"queue_battalion_upgrade":
			last_command_result = queue_battalion_upgrade(team, int(args.get("entity_id", 0)), String(args.get("upgrade_id", "")))
		"cancel_queued_unit":
			last_command_result = cancel_queued_unit(team, int(args.get("producer", 0)), int(args.get("queue_index", 0)))
		"purchase_power":
			last_command_result = purchase_power(team, String(args.get("power_id", "")), int(args.get("cost", -1)))
		"reset_spellbook_purchases":
			last_command_result = reset_spellbook_purchases(team)
		"accept_spellbook_purchases":
			last_command_result = accept_spellbook_purchases(team)
		"set_spellbook_orb_open":
			set_spellbook_orb_open(bool(args.get("open", false)))
		"cast_power":
			last_command_result = cast_power(team, String(args.get("power_id", "")), Vector2(args.get("point", Vector2.ZERO)))
		"cast_heal":
			last_command_result = cast_heal(team, Vector2(args.get("point", Vector2.ZERO)))
		"cast_rally":
			last_command_result = cast_rally(team, Vector2(args.get("point", Vector2.ZERO)))
		"cast_ability":
			last_command_result = cast_ability(int(args.get("hero_id", 0)), String(args.get("ability_id", "")), Vector2(args.get("target_point", Vector2.ZERO)))
		"set_structure_rally":
			last_command_result = set_structure_rally(team, int(args.get("structure_id", 0)), Vector2(args.get("position", Vector2.ZERO)))
		"pause":
			clock_paused = true
			last_command_result = true
		"resume":
			clock_paused = false
			last_command_result = true


func _command_ids(value: Variant) -> Array[int]:
	var ids: Array[int] = []
	if typeof(value) != TYPE_ARRAY:
		return ids
	for id_value in value as Array:
		if typeof(id_value) == TYPE_INT:
			ids.append(int(id_value))
	return ids


func _apply_pending_commands_for_tick(command_tick: int) -> void:
	if not _pending_commands.has(command_tick):
		return
	var commands: Array = _pending_commands[command_tick]
	_pending_commands.erase(command_tick)
	commands.sort_custom(_command_order_less)
	for command_value in commands:
		apply_command(command_value as Dictionary)


func _command_order_less(a: Dictionary, b: Dictionary) -> bool:
	var a_team := int(a.get("team", 0))
	var b_team := int(b.get("team", 0))
	return a_team < b_team or (a_team == b_team and int(a.get("seq", 0)) < int(b.get("seq", 0)))


func state_hash() -> String:
	var dynamic_state := _authoritative_state()
	var static_state: Dictionary = {}
	for key in _state_hash_static_keys():
		static_state[key] = dynamic_state[key]
		dynamic_state.erase(key)
	if _state_hash_static_digest.is_empty():
		var static_context := HashingContext.new()
		static_context.start(HashingContext.HASH_SHA256)
		static_context.update(var_to_bytes(_canonicalize(static_state)))
		_state_hash_static_digest = static_context.finish()
	var context := HashingContext.new()
	context.start(HashingContext.HASH_SHA256)
	context.update(_state_hash_static_digest)
	context.update(var_to_bytes(_canonicalize(dynamic_state)))
	return context.finish().hex_encode()


func snapshot() -> PackedByteArray:
	return var_to_bytes(_authoritative_state())


func restore(bytes: PackedByteArray) -> bool:
	if bytes.is_empty():
		return false
	var decoded: Variant = bytes_to_var(bytes)
	if typeof(decoded) != TYPE_DICTIONARY:
		return false
	var state := decoded as Dictionary
	for required_key in ["tick_index", "entities", "structures", "team_resources", "pending_commands", "next_dynamic_id"]:
		if not state.has(required_key):
			return false
	_restore_authoritative_state(state)
	selected_ids.clear()
	reset_control_groups()
	events.clear()
	last_command_result = null
	_state_hash_static_digest.clear()
	return true


func _state_hash_static_keys() -> Array[String]:
	return [
		"source_map_configured", "ford_gates", "source_player_starts", "playable_outline",
		"spawn_positions", "home_layout", "rules", "unit_production_rules",
		"production_unit_order", "ai_production_plan", "unit_damage_types", "unit_armor",
		"unit_weapon_upgrades", "structure_armor", "spawn_roster", "structure_kinds",
		"seed_structure_kinds", "structure_max_health", "structure_build_rules",
		"unit_prerequisites", "structure_upgrade_contracts", "structure_upgrade_effects",
		"compiled_research_kinds", "unit_upgrade_commands", "unit_level_upgrades",
		"spellbook_ready", "spellbook_document", "spellbook_powers", "spellbook_order",
		"spellbook_sciences", "spellbook_intrinsic", "science_to_power",
		"expansion_build_rules", "unit_ability_rules", "unit_experience_rules",
	]


func _authoritative_state() -> Dictionary:
	return {
		"tick_index": tick_index,
		"winner": winner,
		"ai_enabled": ai_enabled,
		"entities": entities,
		"structures": structures,
		"team_resources": team_resources,
		"team_command_points": team_command_points,
		"command_point_cap": command_point_cap,
		"base_loop_enabled": base_loop_enabled,
		"source_map_configured": source_map_configured,
		"ford_gates": ford_gates,
		"source_player_starts": source_player_starts,
		"playable_outline": playable_outline,
		"spawn_positions": _spawn_positions,
		"home_layout": _home_layout,
		"rules": _rules,
		"unit_production_rules": _unit_production_rules,
		"completed_hero_identities": _completed_hero_identities,
		"production_unit_order": _production_unit_order,
		"ai_production_plan": _ai_production_plan,
		"unit_damage_types": _unit_damage_types,
		"unit_armor": _unit_armor,
		"unit_weapon_upgrades": _unit_weapon_upgrades,
		"structure_armor": _structure_armor,
		"spawn_roster": _spawn_roster,
		"structure_kinds": _structure_kinds,
		"seed_structure_kinds": _seed_structure_kinds,
		"structure_max_health": _structure_max_health,
		"structure_build_rules": _structure_build_rules,
		"unit_prerequisites": _unit_prerequisites,
		"structure_upgrade_contracts": _structure_upgrade_contracts,
		"structure_upgrade_effects": _structure_upgrade_effects,
		"compiled_research_kinds": _compiled_research_kinds,
		"unit_upgrade_commands": _unit_upgrade_commands,
		"unit_level_upgrades": _unit_level_upgrades,
		"next_dynamic_id": _next_dynamic_id,
		"next_dynamic_structure_id": _next_dynamic_structure_id,
		"next_order_sequence": _next_order_sequence,
		"team_ai_state": _team_ai_state,
		"team_upgrades": team_upgrades,
		"team_power_points": team_power_points,
		"purchased_powers": purchased_powers,
		"kills_toward_power_point": _kills_toward_power_point,
		"spellbook_ready": _spellbook_ready,
		"spellbook_document": _spellbook_document,
		"spellbook_powers": _spellbook_powers,
		"spellbook_order": _spellbook_order,
		"spellbook_sciences": _spellbook_sciences,
		"spellbook_intrinsic": _spellbook_intrinsic,
		"science_to_power": _science_to_power,
		"team_sciences": _team_sciences,
		"power_cooldown_until": _power_cooldown_until,
		"staged_purchases": _staged_purchases,
		"clock_paused": clock_paused,
		"pending_power_effects": _pending_power_effects,
		"active_groves": _active_groves,
		"summon_despawn_ticks": _summon_despawn_ticks,
		"expansion_pads": expansion_pads,
		"expansion_build_rules": _expansion_build_rules,
		"next_expansion_structure_id": _next_expansion_structure_id,
		"has_hero_units": _has_hero_units,
		"unit_ability_rules": _unit_ability_rules,
		"unit_experience_rules": _unit_experience_rules,
		"pending_commands": _pending_commands,
	}


func _restore_authoritative_state(state: Dictionary) -> void:
	tick_index = int(state["tick_index"])
	winner = int(state["winner"])
	ai_enabled = bool(state["ai_enabled"])
	entities = state["entities"]
	structures = state["structures"]
	team_resources = state["team_resources"]
	team_command_points = state["team_command_points"]
	command_point_cap = int(state["command_point_cap"])
	base_loop_enabled = bool(state["base_loop_enabled"])
	source_map_configured = bool(state["source_map_configured"])
	ford_gates = state["ford_gates"]
	source_player_starts = state["source_player_starts"]
	playable_outline = state["playable_outline"]
	_spawn_positions = state["spawn_positions"]
	_home_layout = state["home_layout"]
	_rules = state["rules"]
	_unit_production_rules = state["unit_production_rules"]
	_completed_hero_identities = state["completed_hero_identities"]
	_production_unit_order = state["production_unit_order"]
	_ai_production_plan = state["ai_production_plan"]
	_unit_damage_types = state["unit_damage_types"]
	_unit_armor = state["unit_armor"]
	_unit_weapon_upgrades = state["unit_weapon_upgrades"]
	_structure_armor = state["structure_armor"]
	_spawn_roster = state["spawn_roster"]
	_structure_kinds = state["structure_kinds"]
	_seed_structure_kinds = state["seed_structure_kinds"]
	_structure_max_health = state["structure_max_health"]
	_structure_build_rules = state["structure_build_rules"]
	_unit_prerequisites = state["unit_prerequisites"]
	_structure_upgrade_contracts = state["structure_upgrade_contracts"]
	_structure_upgrade_effects = state["structure_upgrade_effects"]
	_compiled_research_kinds = state["compiled_research_kinds"]
	_unit_upgrade_commands = state["unit_upgrade_commands"]
	_unit_level_upgrades = state["unit_level_upgrades"]
	_next_dynamic_id = state["next_dynamic_id"]
	_next_dynamic_structure_id = int(state["next_dynamic_structure_id"])
	_next_order_sequence = int(state["next_order_sequence"])
	_team_ai_state = state.get("team_ai_state", {})
	team_upgrades = state["team_upgrades"]
	team_power_points = state["team_power_points"]
	purchased_powers = state["purchased_powers"]
	_kills_toward_power_point = state["kills_toward_power_point"]
	_spellbook_ready = bool(state["spellbook_ready"])
	_spellbook_document = state["spellbook_document"]
	_spellbook_powers = state["spellbook_powers"]
	_spellbook_order = state["spellbook_order"]
	_spellbook_sciences = state["spellbook_sciences"]
	_spellbook_intrinsic = state["spellbook_intrinsic"]
	_science_to_power = state["science_to_power"]
	_team_sciences = state["team_sciences"]
	_power_cooldown_until = state["power_cooldown_until"]
	_staged_purchases = state["staged_purchases"]
	clock_paused = bool(state["clock_paused"])
	_pending_power_effects = state["pending_power_effects"]
	_active_groves = state["active_groves"]
	_summon_despawn_ticks = state["summon_despawn_ticks"]
	expansion_pads = state["expansion_pads"]
	_expansion_build_rules = state["expansion_build_rules"]
	_next_expansion_structure_id = int(state["next_expansion_structure_id"])
	_has_hero_units = bool(state["has_hero_units"])
	_unit_ability_rules = state["unit_ability_rules"]
	_unit_experience_rules = state["unit_experience_rules"]
	_pending_commands = state["pending_commands"]
	# Reconstruct the derived team registry + per-team manifest aliases from the
	# restored authoritative dicts. Roster order matches setup()'s ascending
	# seeding; the manifest tables realias the restored global tables. Neither is
	# part of the snapshot, so this does not affect the restored hash.
	_reseed_roster_from_state()
	_seed_team_manifest_tables()


func _reseed_roster_from_state() -> void:
	var ids: Array = team_resources.keys()
	ids.sort()
	_team_roster = []
	for id_value in ids:
		var team := int(id_value)
		_team_roster.append(team)
		if not _team_descriptors.has(team):
			_team_descriptors[team] = {"team": team, "faction": "", "is_ai": team != PLAYER_TEAM}


func _canonicalize(value: Variant) -> Variant:
	if typeof(value) == TYPE_DICTIONARY:
		var source := value as Dictionary
		var keys := source.keys()
		keys.sort_custom(_canonical_key_less)
		var rows: Array = []
		for key in keys:
			rows.append([_canonicalize(key), _canonicalize(source[key])])
		return rows
	if typeof(value) == TYPE_ARRAY:
		var rows: Array = []
		for item in value as Array:
			rows.append(_canonicalize(item))
		return rows
	return value


func _canonical_key_less(a: Variant, b: Variant) -> bool:
	return "%02d:%s" % [typeof(a), var_to_str(a)] < "%02d:%s" % [typeof(b), var_to_str(b)]

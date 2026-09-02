class_name RetailSliceSim
extends RefCounted

const PlayableUnitAdapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const ParitySystems = preload("res://src/retail_slice/retail_slice_parity.gd")
const ProjectileSystemScript = preload("res://src/retail_slice/retail_sim_projectiles.gd")
const EconomySystemScript = preload("res://src/retail_slice/retail_sim_economy.gd")
const ExperienceSystemScript = preload("res://src/retail_slice/retail_sim_experience.gd")
const AiSystemScript = preload("res://src/retail_slice/retail_sim_ai.gd")
const CombatSystemScript = preload("res://src/retail_slice/retail_sim_combat.gd")
const PersistenceSystemScript = preload("res://src/retail_slice/retail_sim_persistence.gd")
const ProductionSystemScript = preload("res://src/retail_slice/retail_sim_production.gd")
const MovementSystemScript = preload("res://src/retail_slice/retail_sim_movement.gd")
const AbilitiesSystemScript = preload("res://src/retail_slice/retail_sim_abilities.gd")
## State/tuning owned by the sim; logic lives in retail_sim_upgrades.gd
var _named_wall_slot_types: Dictionary = {}
const POWER_POINT_KILLS := 3
const SPELLBOOK_SCHEMA := "openbfme.spellbook-runtime"
var team_power_points = {PLAYER_TEAM: 1, ENEMY_TEAM: 1}
var purchased_powers = {PLAYER_TEAM: [], ENEMY_TEAM: []}
var _kills_toward_power_point = {PLAYER_TEAM: 0, ENEMY_TEAM: 0}
var _spellbook_ready := false
var _spellbook_error := ""
var _spellbook_document: Dictionary = {}
var _spellbook_powers: Dictionary = {}
var _spellbook_order: Array[String] = []
var _spellbook_sciences: Dictionary = {}
var _spellbook_intrinsic: Array = []
var _science_to_power: Dictionary = {}
var _spellbook_command_points_upgrade: Dictionary = {}
var _team_sciences = {PLAYER_TEAM: [], ENEMY_TEAM: []}
var _power_cooldown_until = {PLAYER_TEAM: {}, ENEMY_TEAM: {}}
var _consumed_nonpressable_powers: Dictionary = {PLAYER_TEAM: {}, ENEMY_TEAM: {}}
var _scavenger_bounty_percent: Dictionary = {PLAYER_TEAM: 0.0, ENEMY_TEAM: 0.0}
var _staged_purchases = {PLAYER_TEAM: [], ENEMY_TEAM: []}
var _team_spellbooks: Dictionary = {}
var clock_paused := false

const UpgradesSystemScript = preload("res://src/retail_slice/retail_sim_upgrades.gd")
const PowerCastsSystemScript = preload("res://src/retail_slice/retail_sim_power_casts.gd")
var _power_casts_system = null
func _power_casts_subsystem():
	if _power_casts_system == null:
		_power_casts_system = PowerCastsSystemScript.new(self)
	return _power_casts_system

const SpellbookCompileSystemScript = preload("res://src/retail_slice/retail_sim_spellbook_compile.gd")
var _spellbook_compile_system = null
func _spellbook_compile_subsystem():
	if _spellbook_compile_system == null:
		_spellbook_compile_system = SpellbookCompileSystemScript.new(self)
	return _spellbook_compile_system

const PowersSystemScript = preload("res://src/retail_slice/retail_sim_powers.gd")
const BasesSystemScript = preload("res://src/retail_slice/retail_sim_bases.gd")
const ContractsBehaviorsSystemScript = preload("res://src/retail_slice/retail_sim_contracts_behaviors.gd")
var _contracts_behaviors_system = null
func _contracts_behaviors_subsystem():
	if _contracts_behaviors_system == null:
		_contracts_behaviors_system = ContractsBehaviorsSystemScript.new(self)
	return _contracts_behaviors_system

const ContractsAbilitiesSystemScript = preload("res://src/retail_slice/retail_sim_contracts_abilities.gd")
var _contracts_abilities_system = null
func _contracts_abilities_subsystem():
	if _contracts_abilities_system == null:
		_contracts_abilities_system = ContractsAbilitiesSystemScript.new(self)
	return _contracts_abilities_system

const ContractsSystemScript = preload("res://src/retail_slice/retail_sim_module_contracts.gd")
const FogOfWarScript = preload("res://src/retail_slice/retail_fog_of_war.gd")
const CommandScript = preload("res://src/retail_slice/retail_command.gd")
const SelectionPick = preload("res://src/retail_slice/retail_selection_pick.gd")
const StructureArmorContract = preload("res://src/retail_slice/structure_armor_contract.gd")

const MAX_RETAINED_EVENT_HISTORY := 2048
const MAX_RETAINED_EVENTS_PER_KIND := 32
const MAX_RETAINED_STRUCTURE_TARGETS_PER_KIND := 256
## Deterministic battalion-level gameplay used by the private retail slice.
## Positions are X/Z world coordinates stored as Vector2 values.

const TICK_SECONDS := 0.1
## Expiry tick for a cloak retail authors no duration for (a bare
## ToggleHiddenSpecialAbilityUpdate). It is not a timer: at 0.1s ticks this is
## roughly 3.4 years of simulated match time, so only a recast or a
## ForbiddenCondition ever ends the cloak.
const STEALTH_UNTIMED_EXPIRY_TICK := 0x3FFFFFFF
const PLAYER_TEAM := 0
const ENEMY_TEAM := 1
# Unowned map structures (capture-building tier-1 targets). No player or AI
# ever acts as this team; it only owns capturable structures until captured.
const NEUTRAL_TEAM := 2
## Dedicated creep owner (retail PlyrCreeps): hostile to every rostered team,
## never rostered itself — never a victory participant and excluded from
## spellbook/economy/AI production. Distinct from NEUTRAL_TEAM, which only
## owns capturable structures. High sentinel so no N-team roster id collides.
const CREEP_TEAM := 9999
## Non-combatant owner for map-authored castle fixtures whose originalOwner is
## not a rostered player (PlyrCivilian, PlyrNeutral, retail's malformed
## "/team"): never hostile, never a victory participant, like NEUTRAL_TEAM but
## collision-free against N-team rosters (NEUTRAL_TEAM = 2 IS rostered on
## 3+ player maps).
const CASTLE_CIVILIAN_TEAM := 9998
## First structure id handed to seeded castle fixtures (scenario structures
## take 60001+, scenario units 70001+, dynamic structures 3000+).
const CASTLE_FIXTURE_FIRST_ID := 80001
const CAPTURABLE_NEUTRAL_FIRST_ID := 90001
const SCENARIO_STRUCTURE_FIRST_ID := 60001
const SCENARIO_UNIT_FIRST_ID := 70001
## Outpost AutoDepositUpdate from gamedata.ini OUTPOST_MONEY_*.
const OUTPOST_DEPOSIT_MS := 10000
const OUTPOST_DEPOSIT_AMOUNT := 60
const OUTPOST_CAPTURE_BONUS := 0
## CaptureBuilding.inc shared by every infantry/hero capture button.
const CAPTURE_BUILDING_RANGE_SOURCE := 15.0
const CAPTURE_BUILDING_UNPACK_MS := 1.0
const CAPTURE_BUILDING_PREPARATION_MS := 15000.0
const CAPTURE_BUILDING_PACK_MS := 1.0
const MEMBER_ATTACK_STAGGER_WINDOW_TICKS := 4
const CORPSE_LIFETIME_TICKS := 600
const STANCE_ORDER: Array[String] = ["HoldGround", "Battle", "Aggressive"]
const FORMATION_ORDER: Array[String] = ["Line", "Block"]
## Line keeps authored formation slots. Block pulls members tighter (retail
## shield-wall / block feel) without inventing new combat numbers.
const FORMATION_SPACING := {"Line": 1.0, "Block": 0.55}
## Retail authors its "block" formation as a SEPARATE object with its own
## RankInfo, not as a scalar squeeze. Comparing the authored pair
## GondorFighterHorde (menhordes.ini:109-111) against its AlternateFormation
## GondorFighterHordeBlock (menhordes.ini:350-352):
##
##   depth   (retail X): 50/30/10 -> 34/22/10, pitch 20 -> 12  = 0.60
##   lateral (retail Y): 0,+-20,+-40 -> 0,+-10,+-20, pitch 20 -> 10 = 0.50
##
## The runtime adapter swaps axes (playable_unit_runtime_adapter.gd:377): retail
## Y becomes sim x, retail X becomes sim z, so "lateral" scales x and "depth"
## scales z. Gated on retail_formation_movement; the isotropic 0.55 above stays
## the default until the owner re-mints the pin.
##
## Stored as plain floats, NOT as a Vector2: Vector2 components are 32-bit, and
## routing the legacy 0.55 through one would round it to 0.550000011920929 and
## move the pinned state hash on a run that never opted in.
const FORMATION_SPACING_RETAIL := {
	"Line": {"lateral": 1.0, "depth": 1.0},
	"Block": {"lateral": 0.5, "depth": 0.6},
}
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
# the fortress, or a stale pack document) has its damage refused loudly. It is
# recorded once per kind at configure — an explicit interim value, never a
# silent default.
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
	# Q80: no DEFAULT_SPAWN_ROSTER fallback — the manifest roster is the
	# roster, empty included.
	return _spawn_roster


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


static func normalize_authored_start_assignments(descriptors: Array) -> Array:
	## Normalize menu/lobby Player_N_Start numbers without coercion. Positive
	## exact integers become the engine's zero-based mpStartIndex; zero stays
	## absent; malformed, negative, and duplicate assignments carry an invalid
	## marker and no plausible value. The roster itself is preserved so invalid
	## input cannot silently select the legacy default roster instead.
	var normalized: Array = []
	var start_owners := {}
	for index in descriptors.size():
		var entry := (descriptors[index] as Dictionary).duplicate(true)
		var authored_start: Variant = entry.get("start_index", 0)
		entry.erase("start_index")
		entry.erase("start_index_invalid")
		if typeof(authored_start) != TYPE_INT or int(authored_start) < 0:
			entry["start_index_invalid"] = true
		elif int(authored_start) > 0:
			var internal_start := int(authored_start) - 1
			if start_owners.has(internal_start):
				var previous_index := int(start_owners[internal_start])
				var previous := normalized[previous_index] as Dictionary
				previous.erase("start_index")
				previous["start_index_invalid"] = true
				normalized[previous_index] = previous
				entry["start_index_invalid"] = true
			else:
				entry["start_index"] = internal_start
				start_owners[internal_start] = index
		normalized.append(entry)
	return normalized


func configure_team_roster(descriptors: Array) -> void:
	## Injection seam for the team registry. The descriptor list is consumed at
	## the next setup(); an empty list restores the default 2-team roster.
	_pending_team_roster = descriptors.duplicate(true)


func team_ids() -> Array:
	return _roster_team_ids().duplicate()


func team_descriptor(team: int) -> Dictionary:
	return (_team_descriptors.get(team, {}) as Dictionary).duplicate(true)


func team_retail_side(team: int) -> Dictionary:
	## The RETAIL SIDE TOKEN for a rostered team - the `Side =` value from
	## playertemplate.ini, which is the vocabulary retail scripts compare
	## (SKIRMISH_PLAYER_FACTION is `player->getSide() == <authored string>` in
	## the retail engine, an exact match). Answers {"side": String} or
	## {"reason": String}; it NEVER guesses. Resolution is two plain lookups,
	## no iteration, so it is order-independent:
	##
	##   1. the team's pack faction id - roster descriptor first (the menu
	##      path), then the team manifest (the legacy env-driven path, whose
	##      default descriptors carry faction "");
	##   2. that id through _rules["retail_faction_sides"], the versioned
	##      pack-faction -> side table the match configuration injects (hashed
	##      with the rest of the rules).
	##
	## A faction the table does not carry REFUSES with the faction named. It
	## must never fall through to answering the pack id (or "") as though it
	## were a side: every retail gate would then answer false-but-plausible
	## instead of refusing visibly, which is this bug's original shape.
	var descriptor := _team_descriptors.get(team, {}) as Dictionary
	var pack_faction := String(descriptor.get("faction", ""))
	if pack_faction == "":
		pack_faction = String((_team_manifests.get(team, {}) as Dictionary).get("faction", ""))
	if pack_faction == "":
		return {"reason": "team %d carries no faction (neither its roster descriptor nor its team manifest names one)" % team}
	var sides: Dictionary = _rules.get("retail_faction_sides", {}) as Dictionary
	if not sides.has(pack_faction):
		return {
			"reason":
			"pack faction '%s' (team %d) has no retail side mapping in retail_faction_sides; refusing rather than answering as a side that merely fails to match" % [pack_faction, team]
		}
	return {"side": String(sides[pack_faction])}


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
	if team_a == CREEP_TEAM or team_b == CREEP_TEAM:
		# Retail PlyrCreeps semantics: the creep owner is hostile to every
		# rostered combatant team (and they to it), never to itself and never
		# to non-rostered owners (the NEUTRAL_TEAM capturable holder). Creeps
		# are not combatants, so victory resolution never counts them.
		var other := team_b if team_a == CREEP_TEAM else team_a
		return other != CREEP_TEAM and _is_combatant_team(other)
	if not _is_combatant_team(team_a) or not _is_combatant_team(team_b):
		return false
	var alliance_a: Variant = team_alliance(team_a)
	var alliance_b: Variant = team_alliance(team_b)
	if alliance_a != null and alliance_b != null and alliance_a == alliance_b:
		return false
	return true


func team_relationship(observer_team: int, subject_team: int) -> String:
	## Presentation-facing classification over the authoritative roster. This is
	## read-only and deliberately returns "unavailable" for neutral/unrostered
	## owners so callers cannot mislabel an unknown subject as an enemy.
	if observer_team == subject_team and _is_combatant_team(observer_team):
		return "local"
	if not _is_combatant_team(observer_team) or not _is_combatant_team(subject_team):
		return "unavailable"
	return "enemy" if _is_hostile(observer_team, subject_team) else "allied"


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


# ---------------------------------------------------------------------------
# Uniform spatial index over battalion positions.
#
# Why: acquisition used to scan every hostile battalion for every acquiring
# battalion, which is O(n^2) and was measured at 95% of tick cost at 4000 units
# (see tests/sim_scale_bench_runner.gd). A uniform grid is the right shape here:
# battalions are roughly uniformly dense, the rebuild is a cheap O(n) sweep, and
# integer cell keys keep it exactly reproducible across machines. A quadtree
# would buy nothing on a uniform density field and costs determinism review.
#
# DETERMINISM CONTRACT. The index is a lookup accelerator only - it never
# decides an outcome:
#   * Cell buckets are UNORDERED. Nothing may depend on bucket order. Callers
#     that need id order sort the gathered ids; callers that pick a "best"
#     candidate use an order-independent total order (distance ascending, then
#     id descending) that provably reproduces the old ascending-id scan with
#     its `distance <= best` tie-break, where the LAST (highest) id wins ties.
#   * Cell membership is derived from float positions by floori(), which is
#     bit-exact for identical inputs, so every client files every unit in the
#     same cell.
#   * The exact distance test is always re-applied to the LIVE row position, so
#     a conservative (over-wide) cell sweep can never change a result - only
#     cost. Every sweep here is conservative by construction.
#   * The index carries no state into entity rows, so state_hash()/snapshot()
#     are untouched.
#
# Liveness: the index is rebuilt at the top of every tick (self-healing after
# restore(), setup(), spawns and corpse cleanup) and kept live inside the tick
# by _spatial_sync() at every position write. Removals are not hooked; queries
# re-validate against `entities` and re-apply the health filter, exactly as the
# old _hostile_living_ids() scan did.
const SPATIAL_CELL_SIZE := 8.0
## Cell indices are clamped to this magnitude so a corrupt position can never
## produce an absurd key. Real map coordinates are orders of magnitude inside it.
const SPATIAL_CELL_LIMIT := 30000
const SPATIAL_CELL_STRIDE := 65536

## Candidate filters for _spatial_nearest_hostile, matching the predicate sets
## the pre-index scans applied.
const SPATIAL_FILTER_ENGAGE := 1  # _can_engage_battalion(source, candidate)
const SPATIAL_FILTER_STEALTH := 2  # skip candidates whose stealth is active
const SPATIAL_FILTER_NOT_FLYING := 4  # skip airborne candidates

# Buckets are partitioned by team: team -> {cell_key -> Array[int]}. Acquisition
# asks for hostiles, so filing by team lets a sweep skip every friendly bucket
# outright instead of loading each allied row only to reject it. In a two-team
# match that halves the rows a sweep touches and removes the per-candidate
# hostility test from the inner loop entirely.
var _spatial_cells: Dictionary = {}
var _spatial_entity_cell: Dictionary = {}
var _spatial_entity_team: Dictionary = {}
# Inclusive bounding box of occupied cells per team. Used only to skip provably
# empty regions of a sweep; it may be wider than the true occupancy (removals
# never shrink it) which is safe because it is a superset.
var _spatial_team_box: Dictionary = {}
# Cached hostile-team lists, keyed by the asking team. Rebuilt with the index;
# team relationships never change inside a tick.
var _spatial_hostile_teams: Dictionary = {}
# Ring offsets are a pure function of the ring index, so they are built once and
# shared. Interleaved [dx0, dy0, dx1, dy1, ...] in a packed array: rebuilding
# these per query was allocating tens of thousands of vectors per tick.
var _spatial_ring_cache: Array[PackedInt32Array] = []


## State/tuning owned by the sim; logic lives in retail_sim_spatial.gd
const STRUCTURE_DEFLECT_GATHER_RADIUS := 4.6  # max STRUCTURE_BLOCK_RADIUS (fortress); the footprint corridor only shrinks radii
var _structures_mutation_serial := 0
var _structure_spatial_serial := -1
var _structure_spatial_cells: Dictionary = {}
const SPATIAL_UNBOUNDED_RANGE := 1.0e9

const SpatialSystemScript = preload("res://src/retail_slice/retail_sim_spatial.gd")
var _spatial_system = null
func _spatial_subsystem():
	if _spatial_system == null:
		_spatial_system = SpatialSystemScript.new(self)
	return _spatial_system

func _spatial_axis_cell(value: float) -> int:
	return _spatial_subsystem()._spatial_axis_cell(value)

func _spatial_key(cx: int, cy: int) -> int:
	return _spatial_subsystem()._spatial_key(cx, cy)

func _spatial_rebuild() -> void:
	_spatial_subsystem()._spatial_rebuild()

func _spatial_sync(row: Dictionary) -> void:
	_spatial_subsystem()._spatial_sync(row)

func _spatial_hostile_team_list(team: int) -> Array:
	return _spatial_subsystem()._spatial_hostile_team_list(team)

func _spatial_gather(point: Vector2, radius: float) -> Array[int]:
	return _spatial_subsystem()._spatial_gather(point, radius)

func _spatial_gather_sorted(point: Vector2, radius: float) -> Array[int]:
	return _spatial_subsystem()._spatial_gather_sorted(point, radius)

func _note_structure_table_mutation() -> void:
	_spatial_subsystem()._note_structure_table_mutation()

func _structure_spatial_index() -> Dictionary:
	return _spatial_subsystem()._structure_spatial_index()

func _structure_ids_near(position: Vector2) -> Array[int]:
	return _spatial_subsystem()._structure_ids_near(position)

func _structure_ids_within_gather_radius(position: Vector2, gather_radius: float) -> Array[int]:
	return _spatial_subsystem()._structure_ids_within_gather_radius(position, gather_radius)

func _spatial_nearest_hostile(source: Dictionary, team: int, origin: Vector2, limit: float, filters: int, prefer_lowest_id: bool = false) -> int:
	return _spatial_subsystem()._spatial_nearest_hostile(source, team, origin, limit, filters, prefer_lowest_id)

func _spatial_ring_offsets(ring: int) -> PackedInt32Array:
	return _spatial_subsystem()._spatial_ring_offsets(ring)

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
	# Cross-faction detection: with more than one distinct faction in the
	# match, the compiled GLOBAL tables are a union of every faction's playable
	# runtimes, so each team's production surface must be scoped to ITS OWN
	# faction (a Men fortress must not offer Isengard heroes). Single-faction
	# matches (the default, and men-v-men lobbies) keep empty scopes and stay
	# byte-identical.
	var distinct_factions: Dictionary = {}
	if shared_faction != "":
		distinct_factions[shared_faction] = true
	for provided_value in provided.values():
		var provided_faction := String((provided_value as Dictionary).get("faction", ""))
		if provided_faction != "":
			distinct_factions[provided_faction] = true
	var cross_faction_match := distinct_factions.size() > 1
	_team_manifests = {}
	_team_production_scopes = {}
	_team_unit_production_rules = {}
	_team_structure_build_rules = {}
	_team_spawn_roster = {}
	_team_structure_max_health = {}
	_team_structure_bounty_values = {}
	_team_structure_armor = {}
	_team_ai_production_plan = {}
	_team_seed_structure_kinds = {}
	_team_structure_kinds = {}
	_team_production_unit_order = {}
	_team_structure_upgrade_contracts = {}
	_team_structure_upgrade_effects = {}
	_team_compiled_research_kinds = {}
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
			_team_structure_bounty_values[team] = _structure_bounty_values
			_team_structure_armor[team] = _structure_armor
			_team_ai_production_plan[team] = _ai_production_plan
			_team_seed_structure_kinds[team] = _seed_structure_kinds
			_team_structure_kinds[team] = _structure_kinds
			_team_production_unit_order[team] = _production_unit_order
			# The upgrade-contract tables are hashed authoritative state (built once
			# from the global manifest). A same-faction team aliases them BY REFERENCE
			# so the default roster stays byte-identical AND picks up the forge
			# fallback the global registration adds after setup.
			_team_structure_upgrade_contracts[team] = _structure_upgrade_contracts
			_team_structure_upgrade_effects[team] = _structure_upgrade_effects
			_team_compiled_research_kinds[team] = _compiled_research_kinds
			if cross_faction_match:
				# The globals this team aliases carry EVERY faction's registered
				# rules; scope its structures' production to its own manifest
				# (plus its builder and the men-only ranger/trebuchet overlays).
				_team_production_scopes[team] = _manifest_production_scope(manifest if not manifest.is_empty() else shared_manifest)
		else:
			var kinds: Array = (manifest.get("structure_kinds", []) as Array).duplicate(true)
			var seed_kinds: Array = (manifest.get("seed_structure_kinds", kinds) as Array).duplicate(true)
			var plan: Array = (manifest.get("ai_production_plan", []) as Array).duplicate(true)
			var team_rules: Dictionary = (manifest.get("unit_production_rules", {}) as Dictionary).duplicate(true)
			# The faction manifest intentionally leaves its builder out of
			# unit_production_rules (the spawn/roster passes own the builder);
			# a derived team still needs the porter's authored train rule at its
			# fortress, projected from the team's own playableUnit document —
			# the per-team mirror of _register_builder_production.
			for builder_value in (manifest.get("builder_unit_ids", []) as Array):
				var builder_rule := _derived_team_builder_rule(manifest, String(builder_value))
				if not builder_rule.is_empty() and not team_rules.has(String(builder_rule["unit_type"])):
					team_rules[String(builder_rule["unit_type"])] = builder_rule
			_team_unit_production_rules[team] = team_rules
			_team_structure_build_rules[team] = (manifest.get("structure_build_rules", {}) as Dictionary).duplicate(true)
			_team_spawn_roster[team] = (manifest.get("spawn_roster", []) as Array).duplicate(true)
			_team_structure_max_health[team] = (manifest.get("structure_max_health", {}) as Dictionary).duplicate(true)
			_team_structure_bounty_values[team] = (manifest.get("structure_bounty_values", {}) as Dictionary).duplicate(true)
			_team_structure_armor[team] = (manifest.get("structure_armor", {}) as Dictionary).duplicate(true)
			_team_ai_production_plan[team] = plan
			_team_seed_structure_kinds[team] = seed_kinds if not seed_kinds.is_empty() else kinds
			_team_structure_kinds[team] = kinds
			# Full production surface for this team's structures: EVERY manifest
			# rule (the fortress hero roster, every line unit, the porter), in
			# deterministic order — not just the AI plan's one-per-producer
			# sample, which silently stripped the hero roster and the porter
			# from a derived team's fortress.
			var team_order: Array = team_rules.keys()
			team_order.sort_custom(func(a, b) -> bool: return String(a).naturalnocasecmp_to(String(b)) < 0)
			_team_production_unit_order[team] = team_order
			if cross_faction_match:
				_team_production_scopes[team] = _manifest_production_scope(manifest)
			# Compile THIS team's own structure-upgrade contracts / effects /
			# research kinds straight from its manifest (the same normalization the
			# global path runs, but scoped to derived-not-hashed per-team dicts so a
			# cross-faction match fields each faction's full combat roster while the
			# default single-manifest roster stays byte-identical). "Binds two
			# structure kinds" fail-close now scopes per team: the target dicts are
			# distinct, so Men and Elf contracts never collide across teams.
			var team_contracts: Dictionary = {}
			var team_effects: Dictionary = {}
			var team_research: Dictionary = {}
			var source := _manifest_upgrade_source(manifest)
			_compile_structure_upgrade_chains(source, team_contracts)
			_compile_structure_castle_upgrades(source, team_contracts)
			_compile_structure_research_contracts(source, team_contracts, team_effects, team_research)
			_team_structure_upgrade_contracts[team] = team_contracts
			_team_structure_upgrade_effects[team] = team_effects
			_team_compiled_research_kinds[team] = team_research


func _manifest_production_scope(manifest: Dictionary) -> Dictionary:
	## Unit types a team on this manifest may surface at its structures. Only
	## consulted in cross-faction matches; {} (single-faction) means unscoped.
	var scope: Dictionary = {}
	for unit_type_value in (manifest.get("unit_production_rules", {}) as Dictionary).keys():
		scope[String(unit_type_value)] = true
	for builder_value in (manifest.get("builder_unit_ids", []) as Array):
		var builder_id := String(builder_value)
		if builder_id != "":
			scope[builder_id] = true
		var builder_rule := _derived_team_builder_rule(manifest, builder_id)
		if not builder_rule.is_empty():
			scope[String(builder_rule["unit_type"])] = true
	if String(manifest.get("faction", "")) == "men":
		# The tiny-pack ranger/trebuchet overlays are Men content registered
		# outside the manifest tables; full Men packs ship them as playable
		# runtimes (already in the rules above), so this stays a superset.
		scope[RANGER_HORDE_ID] = true
		scope[TREBUCHET_OBJECT_ID] = true
	return scope


func production_scope_for_team(team: int) -> Dictionary:
	return _team_production_scopes.get(team, {}) as Dictionary


func created_hero_owner_team(unit_type: String) -> int:
	## The team that made this created hero, or -1 for everything else.
	return int(_created_hero_owner_teams.get(unit_type, -1))


func _derived_team_builder_rule(manifest: Dictionary, builder_member_id: String) -> Dictionary:
	## Projects a faction builder's authored train rule (retail: the fortress
	## citadel trains porters) from the team's own playableUnit document,
	## resolving producers through THAT manifest's producer registry. {} when
	## the document or any authored route cannot be proven — fail-closed, the
	## team then simply has no porter train rule (and the gates say so).
	if builder_member_id == "":
		return {}
	var runtimes_value: Variant = _rules.get("playable_unit_runtimes", {})
	if typeof(runtimes_value) != TYPE_DICTIONARY:
		return {}
	var document: Dictionary = {}
	for document_value in (runtimes_value as Dictionary).values():
		if typeof(document_value) != TYPE_DICTIONARY:
			continue
		if PlayableUnitAdapter.runtime_member_id(document_value as Dictionary) == builder_member_id:
			document = document_value as Dictionary
			break
	if document.is_empty():
		return {}
	var partial := PlayableUnitAdapter.builder_production_rule(document)
	if partial.is_empty():
		return {}
	var producer_kinds_folded: Dictionary = {}
	for producer_id_value in (manifest.get("producer_kind_registry", {}) as Dictionary).keys():
		producer_kinds_folded[String(producer_id_value).to_lower()] = String((manifest.get("producer_kind_registry", {}) as Dictionary)[producer_id_value])
	var resolved_producers: Array[Dictionary] = []
	var resolved_producer_kinds: Array[String] = []
	for producer_value in partial["producers"] as Array:
		var producer := producer_value as Dictionary
		var producer_kind := String(producer_kinds_folded.get(String(producer.get("producer_source_object_id", "")).to_lower(), ""))
		if producer_kind == "":
			return {}
		var route := producer.duplicate(true)
		route["producer_kind"] = producer_kind
		resolved_producers.append(route)
		if not resolved_producer_kinds.has(producer_kind):
			resolved_producer_kinds.append(producer_kind)
	if resolved_producers.is_empty():
		return {}
	var primary_producer := resolved_producers[0]
	return {
		"category": String(partial.get("category", "")),
		"producer_kind": String(primary_producer["producer_kind"]),
		"producer_kinds": resolved_producer_kinds,
		"producer_routes": resolved_producers,
		"producer_source_object_id": String(primary_producer.get("producer_source_object_id", "")),
		"object_id": String(partial["object_id"]),
		"unit_type": String(partial["unit_type"]),
		"display_name": String(partial["display_name"]),
		"default_cost": int(partial["default_cost"]),
		"default_build_ticks": int(partial["default_build_ticks"]),
		"default_command_points": int(partial["default_command_points"]),
		"command_id": String(primary_producer.get("command_id", "")),
		"command_slot": int(primary_producer.get("slot", 0)),
		"surface": String(primary_producer.get("surface", "")),
	}


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


func structure_bounty_values_for_team(team: int) -> Dictionary:
	return _team_structure_bounty_values.get(team, _structure_bounty_values) as Dictionary


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


func structure_upgrade_contracts_for_team(team: int) -> Dictionary:
	return _team_structure_upgrade_contracts.get(team, _structure_upgrade_contracts) as Dictionary


func structure_upgrade_effects_for_team(team: int) -> Dictionary:
	return _team_structure_upgrade_effects.get(team, _structure_upgrade_effects) as Dictionary


func structure_create_grants_for_team(team: int) -> Dictionary:
	var grants := (
		team_manifest_for(team).get("structure_create_grants", {}) as Dictionary
	).duplicate(true)
	for kind_value in _expansion_build_rules.keys():
		var rule := _expansion_build_rules[kind_value] as Dictionary
		var rows: Variant = rule.get("create_grants")
		if typeof(rows) == TYPE_ARRAY and not (rows as Array).is_empty():
			grants[String(kind_value)] = (rows as Array).duplicate(true)
	return grants


func structure_inherit_upgrades_for_team(team: int) -> Dictionary:
	return (
		team_manifest_for(team).get("structure_inherit_upgrades", {}) as Dictionary
	)


func structure_auto_deposit_updates_for_team(team: int) -> Dictionary:
	return (
		team_manifest_for(team).get("structure_auto_deposit_updates", {})
		as Dictionary
	)


func structure_source_object_ids_for_team(team: int) -> Dictionary:
	return (
		team_manifest_for(team).get("structure_source_object_ids", {}) as Dictionary
	)


func compiled_research_kinds_for_team(team: int) -> Dictionary:
	return _team_compiled_research_kinds.get(team, _compiled_research_kinds) as Dictionary

var tick_index := 0
var winner := -1
var ai_enabled := true
var selected_ids: Array[int] = []
var control_groups: Dictionary = {}
var events: Array[Dictionary] = []
var _event_digest := 0x811C9DC5
var entities: Dictionary = {}
var structures: Dictionary = {}
## Scenario props are authoritative placement/presentation state, but never
## combat entities. Keeping them out of `entities` and `structures` means an
## inert retail web/rock cannot accidentally acquire selection, ownership,
## damage, AI, or production merely because a map or OCL instantiated it.
var scenario_props: Dictionary = {}
var scenario_bezier_presentation_requests: Array = []
var _next_scenario_prop_id := 400000
## World pickups are authoritative lightweight objects. The descriptor only
## determines which kinds an AI horde seeks; the pickup's own collide consumer
## owns its eventual reward/removal.
var pickup_objects: Dictionary = {}
var _next_pickup_object_id := 60000
var respawn_schedules: Dictionary = {} # entity id -> authored due/cost/template state
## Deterministic, spawned-body lane for typed PhysicsBehavior contracts. This
## is deliberately separate from ordinary battalion locomotion: retail only
## wakes PhysicsBehavior motion when an object is thrown/knocked back, and
## attaching gravity to every living unit would be fabricated behavior.
## Rows carry their complete mutable flight/recovery state and are serialized
## empty-is-absent below.
var physics_objects: Dictionary = {}
var _next_physics_object_id := 50000
## Q81a extraction #1: member-projectile logic lives in
## retail_sim_projectiles.gd; the sim keeps the authoritative STATE
## (projectiles/_next_projectile_id — tests read and write it directly, and
## serialization is untouched) plus one-line delegates under the original
## names so call sites and tick order are byte-identical.
var _projectile_system = null


func _projectiles_subsystem():
	if _projectile_system == null:
		_projectile_system = ProjectileSystemScript.new(self)
	return _projectile_system


## Q81 extraction #2: economy/income logic lives in retail_sim_economy.gd;
## state (team_resources, per-structure auto_deposit rows) stays on the sim.
var _economy_system = null


## Q81 extraction #3: experience/veterancy logic lives in
## retail_sim_experience.gd; XP state stays on entity rows.
var _experience_system = null


## Q81 extraction #4: the skirmish-AI controller lives in retail_sim_ai.gd
## (extracted AS-IS; Q83 Phase 2 replaces its guts with authored data).
var _ai_system = null


## Q81 extraction #5: the damage-resolution core lives in
## retail_sim_combat.gd; entity/structure state stays on the sim.
var _combat_system = null


## Q81 extraction #6: hashing/snapshot/restore live in
## retail_sim_persistence.gd; the memoized static digest stays on the sim.
var _persistence_system = null


## Q81 extraction #7: production queues / command points live in
## retail_sim_production.gd; queue state stays on structure rows.
var _production_system = null


## Q81 extraction #8: routing/locomotion stepping lives in
## retail_sim_movement.gd; route state stays on entity rows.
var _movement_system = null


## Q81 extraction #9: hero/unit ability EFFECTS live in
## retail_sim_abilities.gd; cast gating/dispatch stays on the sim.
var _abilities_system = null


## Q81 extraction #10: upgrade queues/effects live in
## retail_sim_upgrades.gd; contract COMPILATION stays with sim config.
var _upgrades_system = null


## Q81 extraction #11: the spellbook/powers family lives in
## retail_sim_powers.gd (support compilation, purchase/science/rank,
## casting, weather/ping/summon stepping).
var _powers_system = null


## Q81 extraction #12: structure weapons + expansion pads/build plots +
## castle behavior + unpackable bases live in retail_sim_bases.gd.
var _bases_system = null


## Q81 extraction #13: the retail behavior-module contract family lives
## in retail_sim_module_contracts.gd (every _attach_* binder + its
## steppers) - the seam Q82 formalizes into the module framework.
var _contracts_system = null


func _contracts_subsystem():
	if _contracts_system == null:
		_contracts_system = ContractsSystemScript.new(self)
	return _contracts_system


func _bases_subsystem():
	if _bases_system == null:
		_bases_system = BasesSystemScript.new(self)
	return _bases_system


func _powers_subsystem():
	if _powers_system == null:
		_powers_system = PowersSystemScript.new(self)
	return _powers_system


func _upgrades_subsystem():
	if _upgrades_system == null:
		_upgrades_system = UpgradesSystemScript.new(self)
	return _upgrades_system


func _abilities_subsystem():
	if _abilities_system == null:
		_abilities_system = AbilitiesSystemScript.new(self)
	return _abilities_system


func _movement_subsystem():
	if _movement_system == null:
		_movement_system = MovementSystemScript.new(self)
	return _movement_system


func _production_subsystem():
	if _production_system == null:
		_production_system = ProductionSystemScript.new(self)
	return _production_system


func _persistence_subsystem():
	if _persistence_system == null:
		_persistence_system = PersistenceSystemScript.new(self)
	return _persistence_system


func _combat_subsystem():
	if _combat_system == null:
		_combat_system = CombatSystemScript.new(self)
	return _combat_system


func _ai_subsystem():
	if _ai_system == null:
		_ai_system = AiSystemScript.new(self)
	return _ai_system


func _experience_subsystem():
	if _experience_system == null:
		_experience_system = ExperienceSystemScript.new(self)
	return _experience_system


func _economy_subsystem():
	if _economy_system == null:
		_economy_system = EconomySystemScript.new(self)
	return _economy_system


## Ordinary weapon projectiles are authoritative sim rows, separate from the
## physics-object table used by fling/Bezier carriers.
var projectiles: Dictionary = {}
var _next_projectile_id := 70000
## Created-hero award contracts are derived from their ordinary playable-unit
## runtime documents. Mutable tallies/results remain absent for hero-less
## matches so the historical snapshot signature does not move.
var _cah_award_contracts: Dictionary = {}
var _cah_award_tallies: Dictionary = {}
var cah_award_results: Dictionary = {}
var team_resources: Dictionary = {PLAYER_TEAM: 0, ENEMY_TEAM: 0}
var team_command_points: Dictionary = {PLAYER_TEAM: 0, ENEMY_TEAM: 0}
## Residual parity subsystems (FoW, path, wall helpers, OCL leaf registry, mood/meta).
## Never store a back-ref from parity to this sim (RefCounted cycle → leak/AV).
var parity = null


func _ensure_parity() -> void:
	if parity == null:
		parity = ParitySystems.new()
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
# Cross-faction production scoping (team -> {unit_type: true}); empty in
# single-faction matches so the historical tables stay byte-identical.
var _team_production_scopes: Dictionary = {}
var _team_structure_build_rules: Dictionary = {}
var _team_spawn_roster: Dictionary = {}
var _team_structure_max_health: Dictionary = {}
var _team_structure_bounty_values: Dictionary = {}
var _team_structure_armor: Dictionary = {}
var _team_ai_production_plan: Dictionary = {}
var _team_seed_structure_kinds: Dictionary = {}
var _team_structure_kinds: Dictionary = {}
var _team_production_unit_order: Dictionary = {}
## Per-team structure-upgrade contract/effect/research tables. Derived (rebuilt at
## setup/restore), intentionally NOT part of the authoritative snapshot: for the
## default same-faction roster every entry aliases the hashed global dict by
## reference (byte-identical), and a cross-faction team carries its own compiled
## copy. Readers reach them through the *_for_team accessors, which fall back to
## the global dict so any call site without a rostered team stays unchanged.
var _team_structure_upgrade_contracts: Dictionary = {}
var _team_structure_upgrade_effects: Dictionary = {}
var _team_compiled_research_kinds: Dictionary = {}
var command_point_cap := 200
## Script-authored per-team {total, maximum}. Empty means the global cap
## remains the exact legacy default and contributes no bytes to snapshots.
var command_point_overrides_by_team: Dictionary = {}
var base_loop_enabled := false
var source_map_configured := false
var ford_gates: Array[Dictionary] = []
var source_player_starts: Dictionary = {}
var route_provider: RefCounted
var playable_outline := PackedVector2Array()
var last_route_rejection := ""
## Observational Q64 receipt; deterministic backoff state itself lives on each
## affected entity row and is included in snapshots/save state below.
var ai_route_backoff_skip_count := 0
## Observational castle-AI site-search receipts (never serialized, never read by
## the sim itself). `castle_ai_site_dry_runs` counts construct dry-runs issued
## by the site search; `castle_ai_site_reject_prints` counts per-site rejection
## lines actually printed. The owner's v0.2.8 run.log carried 99,466 of the
## latter for one map, so both are asserted by castle_ai_site_scan_unit_test.
var castle_ai_site_dry_runs := 0
var castle_ai_site_reject_prints := 0
## team -> {structure kind: reason} print de-duplicator for kind-level construct
## refusals. Diagnostics only: never serialized, never consulted by the sim.
var _castle_ai_kind_refusals_logged: Dictionary = {}
## team -> {structure kind: last printed CASTLE_AI_CELL_SCAN line}. Same purpose,
## same non-serialized diagnostics-only status.
var _castle_ai_scan_summaries_logged: Dictionary = {}
var _spawn_positions: Dictionary = {}
## Team -> spawn-anchor Vector2 for rostered teams beyond 0/1 (N-team spawn
## geometry). Populated from the map layer's team_start_centers; teams 0/1 keep
## deriving their anchors from spawn ids 1/2 and 101/102, so this is empty and
## inert for every 2-team match.
var _extra_team_centers: Dictionary = {}
var _configured_team_start_indices: Dictionary = {}
var _home_layout: Dictionary = {}
var _map_script_waypoints: Dictionary = {}
var _ai_build_waypoints: Dictionary = {}
var _castle_ai_base_layouts: Array[Dictionary] = []
var _castle_ai_base_fallback := "generic-any"
var _source_map_axis_x := Vector2.RIGHT
var _source_map_axis_z := Vector2.DOWN
var _rules: Dictionary = {}
var configuration_error := ""
var _unit_production_rules: Dictionary = {}
## unit type -> owning team, for created heroes only. Every peer registers EVERY
## seat's created heroes (identical rule tables are what keeps a lockstep match
## in step), so the owner has to be recorded or seat A's fortress would happily
## train seat B's hero. Retail units never appear here.
var _created_hero_owner_teams: Dictionary = {}
var _completed_hero_identities: Dictionary = {}
var _production_unit_order: Array[String] = []
var _ai_production_plan: Array[String] = []
var _unit_damage_types: Dictionary = {}
## member object id -> compiled damageComponents rows, for retail weapons whose
## DamageNuggets author different types (ArwenSword: HERO + SLASH). Each row is
## {"damage_type": String, "value": float}; an empty type means the nugget
## authors none and resolves against the victim's DEFAULT armor column.
var _unit_damage_components: Dictionary = {}
## member object id -> compiled armor contract (fractions, upgrade tables).
var _unit_armor: Dictionary = {}
## member object id -> upgrade id -> compiled WeaponSetUpgrade effect.
var _unit_weapon_upgrades: Dictionary = {}
## DeathWeapon id -> resolved radial payload. Selected/future pack assembly may
## inject this table through rules.death_weapon_rules; the module contract still
## schedules and reports an unresolved weapon id when no payload was converted.
var _death_weapon_rules: Dictionary = {}
## structure kind -> compiled armor table (fractions).
var _structure_armor: Dictionary = {}
var _spawn_roster: Array = []
var _structure_kinds: Array[String] = []
## Kinds pre-placed at match start. Full-faction manifests seed fortresses only;
## constructable kinds remain in _structure_kinds for the builder UI.
var _seed_structure_kinds: Array[String] = []
var _structure_max_health: Dictionary = {}
var _structure_bounty_values: Dictionary = {}
var _structure_build_rules: Dictionary = {}
var _unit_prerequisites: Dictionary = {}
## Optional ANY-of production gate per unit type: {unit_type: {producer_kind:
## [upgrade_id, ...]}}. Authored by the button's `NeededUpgradeAny = Yes`
## (PURE RETAIL 2.01 commandbutton.ini:6327, Command_ConstructGondorRangerHorde);
## owning ANY ONE member satisfies the group, while `_unit_prerequisites` stays
## the ALL-of set. Packs built before the converter emitted `prerequisiteAnyOf`
## register no group at all, so their gate keeps behaving exactly as it did
## (pure ALL-of).
##
## CITATION REBASED 2026-08-04. The old reference (`:7513-7519`) was a line in
## the fan-patched (Unofficial 2.02) tree, which authors 44 `NeededUpgradeAny`
## buttons. Retail authors NINE, and every one of them names exactly ONE
## `NeededUpgrade` token - so on retail data an ANY-of group is always a
## single-element set and is behaviourally identical to the ALL-of gate. This
## consumer is still correct and still required (the flag is genuinely
## authored), but do not look for a multi-token retail example: there is none.
var _unit_prerequisite_any_groups: Dictionary = {}
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
## Horde/member runtime id -> authored BannerCarriersAllowed contract.
var _unit_banner_carriers: Dictionary = {}
## Banner runtime object id (adapter slug) -> max(died, melee-free) respawn ticks.
## Absent key means retail authored no DiedRespawnTime; never invent a timer.
var _banner_respawn_ticks_by_object: Dictionary = {}
## Structure source object id (e.g. MenFortress) -> castleBehavior pack contract.
var _castle_behavior_by_source: Dictionary = {}
var units_without_upgrade_commands: Array[String] = []
var _next_dynamic_id: Dictionary = {PLAYER_TEAM: 10, ENEMY_TEAM: 110}
var _next_dynamic_structure_id := 3000
var _next_event_sequence := 1
var _typed_audio_roll_sequence := 0
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
## Descriptor-backed map placements are opt-in for direct/headless sims; the
## live selected-pack scene enables them whenever its edition registry exists.
var scenario_map_placements_enabled := false
## Retail horde formation semantics. Every positive authored turn rate is
## honoured unconditionally; the opt-in gameplay rule remains only for the
## broader group-cohesion/reform fixture lane described below.
##
## When enabled the sim honours three things the retail data authors and this
## sim previously ignored outright:
##
##   1. Turn rate. locomotor.ini authors TurnTime per horde locomotor
##      (NormalMeleeHordeLocomotor TurnTime=2000 -> 180 deg/s;
##      NormalCavalryHordeLocomotor TurnTime=1000 -> 360 deg/s). The importer
##      lands it on the row as turn_rate_degrees_per_second with provenance.
##   2. Wheel vs reform. locomotor.ini:717
##      "MaxTurnWithoutReform = 45 ; Try to turn beyond this angle, and we will
##      reform instead of wheel". Inside the threshold the horde wheels (turns
##      while advancing); beyond it the horde reforms - it pivots about its own
##      centre without translating, which is also what TurnWhileMoving = No on
##      the same melee locomotors demands.
##   3. Group cohesion. locomotor.ini:713 "WaitForFormation = Yes ; When moving
##      into formations, these guys stop & wait for others." A multi-battalion
##      move order caps every member of the group at the slowest authored speed
##      so the group arrives together instead of scattering by class.
##
## See workspace/scratch/opus17-formation-extraction.md for the full citation set.
var retail_formation_movement := false
## SAGE legs-locomotor speed model opt-in (rules key "retail_locomotor_physics"):
## goal speed scales to zero at 45 deg off heading, braking begins at
## 0.5*v^2/braking*1.05 of on-path distance to the order destination, and the
## per-tick speed change never overshoots the goal speed. Absent resolves false
## so every pinned runner stays byte-identical; retail_vertical_slice.gd sets
## it only when OPENBFME_LOCOMOTOR_PHYSICS=1. Formulas from the GPL Generals
## Locomotor.cpp (moveTowardsPositionLegs, calcSlowDownDist); no code copied.
var retail_locomotor_physics := false
## Diagnostic-only, never serialized: one fallback line per unit type whose
## document did not author TurnTime/TurnRate. Such rows retain the pre-change
## snap/direct movement path instead of receiving a made-up rate.
var _turn_rate_fallback_unit_types: Dictionary = {}
var _target_route_resolution_unit_types: Dictionary = {}
var _scenario_map_placements: Array = []
var _scenario_map_seeded_source_indices: Dictionary = {}
var _next_scenario_unit_id := SCENARIO_UNIT_FIRST_ID
var _next_scenario_structure_id := SCENARIO_STRUCTURE_FIRST_ID
var capturable_neutrals_enabled := false
var _capturable_placements: Array = []
var _next_capturable_structure_id := CAPTURABLE_NEUTRAL_FIRST_ID
## Map-authored castle structures (opt-in gameplay rule
## "enable_castle_fixtures"; default off keeps every legacy runner and the
## pinned scenario byte-identical — the fog lane's absent-unless-enabled
## pattern). Placements arrive with the map configuration (already validated
## and translated by RetailMapData) and stay inert until the rule enables
## seeding.
var castle_fixtures_enabled := false
var _castle_fixture_placements: Array = []
var _next_castle_fixture_id := CASTLE_FIXTURE_FIRST_ID
var ring_mechanic_enabled := false
## Retail shroud. DERIVED state, never hashed - see retail_fog_of_war.gd.
var fog_of_war_enabled := false
var _fog_of_war = null
var _ring_contract: Dictionary = {}
var _ring_delivery_kinds: Dictionary = {}
# Map script executors evaluate on tick 1 before the ring step. Four additional
# ticks cover their initial interval/condition settling window, so tick 5 is the
# first fallback opportunity. Script-spawn dedupe below makes this exact bound
# non-load-bearing: a later authored spawn is absorbed instead of duplicating it.
const RING_FALLBACK_TICK := 5
const RING_DEFAULT_GOLLUM := "NeutralGollum_RingHero"
const RING_DEFAULT_ITEM := "TheDroppedRing"


func setup(map_configuration: Dictionary = {}, gameplay_rules: Dictionary = {}) -> void:
	_ensure_parity()
	parity.clear()
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
	scenario_props.clear()
	scenario_bezier_presentation_requests.clear()
	_scenario_map_seeded_source_indices.clear()
	_next_scenario_prop_id = 400000
	pickup_objects.clear()
	_next_pickup_object_id = 60000
	respawn_schedules.clear()
	physics_objects.clear()
	_next_physics_object_id = 50000
	projectiles.clear()
	_next_projectile_id = 70000
	_shared_ability_cooldowns.clear()
	_note_structure_table_mutation()
	_structure_footprint_radius_cache.clear()
	# MATCH-SCOPED base-loop state resets WITH the structures it pointed at.
	# Stale expansion-pad keys would both survive a reset AND block re-seeding
	# (_seed_expansion_pads_for early-returns on an existing key), leaving
	# phantom pads for dead structure ids and stale pad occupancy inside the
	# HASHED state - a reused sim would diverge from a freshly built one at
	# tick 0. Script unit references are match state for the same reason.
	expansion_pads.clear()
	build_plots.clear()
	_next_expansion_structure_id = 9000
	script_unit_references.clear()
	script_entity_references.clear()
	create_object_die_pending.clear()
	# Script-built OBJECT_TYPE_LIST stores are match STATE (mutated by script
	# actions, persisted by retail save games), never configuration: a reused
	# sim must not carry one match's lists into the next.
	script_object_type_lists.clear()
	# Script-team identities are match configuration and worlds retain their
	# bindings across reset_match(). Mutable member handles/recruitable flags
	# point into the old match and are stripped.
	for script_team_name in script_teams.keys():
		var previous := script_teams[script_team_name] as Dictionary
		script_teams[script_team_name] = _script_team_definition(previous)
	building_permissions_by_team.clear()
	command_point_overrides_by_team.clear()
	# Team behavior state (TEAM_STATE + custom-state tokens) is match state by
	# the same rule: mutated by script actions, save-persisted in retail
	# (Team::xfer writes m_state), and one match's AI blackboard must never
	# leak into the next - a reused sim would diverge from a fresh one on the
	# first TEAM_STATE_IS the adopted AI evaluates.
	team_behavior_states.clear()
	# Sequential-script heads are match state (queued behavior scripts progress
	# across ticks). Clear so a reused sim does not inherit another match's AI
	# behavior chains.
	sequential_script_queues.clear()
	production_controls_by_team.clear()
	tech_buildability.clear()
	script_team_references.clear()
	player_progression.clear()
	player_economy_extras.clear()
	player_diplomacy_overrides.clear()
	script_event_counts.clear()
	containment.clear()
	entity_container.clear()
	script_areas.clear()
	script_waypoints.clear()
	var effective_rules := gameplay_rules if not gameplay_rules.is_empty() else _rules
	var ring_waypoint_family := String(
		(effective_rules.get("ring_system", {}) as Dictionary).get(
			"waypointFamily", "SpawnPoint_SkirmishGollum_"
		)
	)
	var ring_waypoints_enabled := bool(effective_rules.get("allow_ring_heroes", false))
	for waypoint_name in _map_script_waypoints.keys():
		if not ring_waypoints_enabled and String(waypoint_name).begins_with(ring_waypoint_family):
			continue
		script_waypoints[String(waypoint_name)] = _map_script_waypoints[waypoint_name]
	script_waypoint_paths.clear()
	team_created_edge.clear()
	match_script_flags.clear()
	attack_priority_names.clear()
	selection_script_names.clear()
	script_surface_bag.clear()
	# The logic random stream is match state too: cleared back to the
	# never-drawn form so a reused sim re-derives the words from the (rules-
	# configured) seed on first draw, exactly like a freshly built one.
	_logic_random_state.clear()
	# The script interpreter's memory (counters/flags/timers/enable bits/tick)
	# is match state too. clear() IN PLACE, never rebind: attached
	# SageScriptEnvs hold this dictionary by reference, and a rebind would
	# leave them writing to an orphan outside the hash boundary. The view's
	# report-once table resets with the store so a stray field that returns
	# after a reset is reported again.
	script_env_state.clear()
	_script_env_view_reported.clear()
	# Registered executors survive a match reset (their bodies are match
	# configuration), but both clocks just moved: rebase the recorded env/sim
	# offsets from the reset values and lift any quarantine - a reset match
	# starts with consistent clocks again.
	_script_executor_faults.clear()
	_rebase_script_executor_offsets()
	_completed_hero_identities.clear()
	_hero_peak_ranks_by_team.clear()
	_next_event_sequence = 1
	_typed_audio_roll_sequence = 0
	_next_order_sequence = 1
	_music_state = ""
	_seed_team_ai_state()
	_last_base_under_attack_tick = -100000
	_pending_commands.clear()
	_turn_rate_fallback_unit_types.clear()
	_target_route_resolution_unit_types.clear()
	last_command_result = null
	_state_hash_static_digest.clear()
	last_route_rejection = ""
	ai_route_backoff_skip_count = 0
	team_power_points = _seed_team_map(1)
	purchased_powers = _seed_team_map([])
	_kills_toward_power_point = _seed_team_map(0)
	_reset_spellbook_match_state()
	clock_paused = false
	_apply_gameplay_rules(gameplay_rules if not gameplay_rules.is_empty() else _rules)
	if configuration_error != "":
		# Q80: a refused configuration seeds NOTHING. Continuing here is how
		# half-configured sims used to run on invented fallback tables and
		# produce plausible-but-wrong results that passed review.
		return
	if ring_mechanic_enabled:
		script_surface_bag["game_mode"] = String(_ring_contract.get("modeToken", "ringheroes"))
		script_surface_bag["ring_mechanic"] = {
			"gollum_id": 0, "gollum_spawned": false, "ring_active": false,
			"ring_position": Vector2.ZERO, "carrier_id": 0,
		}
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
			# Q80: an empty authored roster spawns nothing. The old
			# DEFAULT_SPAWN_ROSTER fallback here invented gondor battalions
			# for any team whose roster was empty.
			var team_roster: Array = spawn_roster_for_team(int(team))
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
	# Base-flag CONFIG (position/cost/health) survives setup like the expansion
	# rules. The dynamic unpack state is reset EXPLICITLY here (rows back to
	# packed) and above (expansion pads, the expansion id counter and script
	# unit references cleared with the structures they pointed at) - none of
	# it resets by itself.
	for base_name in unpackable_base_names():
		var flag_row: Dictionary = unpackable_bases[base_name]
		flag_row["unpacked_by"] = -1
		flag_row["structure_id"] = 0
	_next_scenario_unit_id = SCENARIO_UNIT_FIRST_ID
	_next_scenario_structure_id = SCENARIO_STRUCTURE_FIRST_ID
	if scenario_map_placements_enabled:
		_seed_scenario_map_placements()
	_next_capturable_structure_id = CAPTURABLE_NEUTRAL_FIRST_ID
	if capturable_neutrals_enabled:
		_seed_capturable_neutrals()
	_next_castle_fixture_id = CASTLE_FIXTURE_FIRST_ID
	if castle_fixtures_enabled:
		_seed_castle_fixtures()
	if ring_mechanic_enabled:
		_mark_ring_delivery_structures()
	# Spellbook effect rules (summon stats) bake the source→sim scale, which
	# only exists once the gameplay rules are applied above: recompute them
	# now. Ownership/points already reset; configure only touches doc-derived
	# rows and match-scoped spellbook state.
	if _spellbook_ready and not _spellbook_document.is_empty():
		configure_spellbook_runtime(_spellbook_document)
	_emit_music("explore")


func _apply_map_configuration(configuration: Dictionary) -> void:
	_capturable_placements = []
	var configured_camps: Variant = configuration.get("capturable_placements", [])
	if typeof(configured_camps) == TYPE_ARRAY:
		for camp_value in configured_camps as Array:
			if typeof(camp_value) != TYPE_DICTIONARY:
				continue
			var camp := camp_value as Dictionary
			if (
				typeof(camp.get("position")) != TYPE_VECTOR2
				or String(camp.get("type_name", "")) == ""
				or typeof(camp.get("source_index")) != TYPE_INT
			):
				continue
			_capturable_placements.append(camp.duplicate(true))
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
	var configured_axis_x: Variant = configuration.get("source_map_axis_x", Vector2.RIGHT)
	var configured_axis_z: Variant = configuration.get("source_map_axis_z", Vector2.DOWN)
	_source_map_axis_x = Vector2(configured_axis_x).normalized() if typeof(configured_axis_x) == TYPE_VECTOR2 and not Vector2(configured_axis_x).is_zero_approx() else Vector2.RIGHT
	_source_map_axis_z = Vector2(configured_axis_z).normalized() if typeof(configured_axis_z) == TYPE_VECTOR2 and not Vector2(configured_axis_z).is_zero_approx() else Vector2.DOWN
	var configured_home_layout: Variant = configuration.get("home_layout", {})
	_home_layout = (configured_home_layout as Dictionary).duplicate(true) if typeof(configured_home_layout) == TYPE_DICTIONARY else {}
	_castle_ai_base_layouts = []
	var configured_ai_layouts: Variant = configuration.get("castle_ai_base_layouts", [])
	if typeof(configured_ai_layouts) == TYPE_ARRAY:
		for layout_value in configured_ai_layouts as Array:
			if typeof(layout_value) == TYPE_DICTIONARY:
				_castle_ai_base_layouts.append((layout_value as Dictionary).duplicate(true))
	_castle_ai_base_fallback = String(configuration.get("castle_ai_base_fallback", "generic-any"))
	# Optional per-team spawn anchors for rostered teams beyond 0/1 (N-team maps
	# expose all authored Player_N_Start centers here). Absent on 2-team configs.
	var configured_team_centers: Variant = configuration.get("team_start_centers", {})
	_extra_team_centers = {}
	_map_script_waypoints.clear()
	_ai_build_waypoints.clear()
	var configured_waypoints: Variant = configuration.get("script_waypoints", {})
	if typeof(configured_waypoints) == TYPE_DICTIONARY:
		for waypoint_name in (configured_waypoints as Dictionary).keys():
			var waypoint_position: Variant = (configured_waypoints as Dictionary)[waypoint_name]
			if typeof(waypoint_position) == TYPE_VECTOR2:
				_map_script_waypoints[String(waypoint_name)] = waypoint_position
	var configured_ai_build_waypoints: Variant = configuration.get("ai_build_waypoints", {})
	if typeof(configured_ai_build_waypoints) == TYPE_DICTIONARY:
		for waypoint_name in (configured_ai_build_waypoints as Dictionary).keys():
			var waypoint_position: Variant = (configured_ai_build_waypoints as Dictionary)[waypoint_name]
			if typeof(waypoint_position) == TYPE_VECTOR2:
				_ai_build_waypoints[String(waypoint_name)] = waypoint_position
	if typeof(configured_team_centers) == TYPE_DICTIONARY:
		for team_key in (configured_team_centers as Dictionary).keys():
			var center_value: Variant = (configured_team_centers as Dictionary)[team_key]
			if typeof(center_value) == TYPE_VECTOR2:
				_extra_team_centers[int(team_key)] = center_value
	# Optional map-owned start assignment. Validate the complete table before
	# applying any row: a malformed entry must not leave a partially credible
	# START_POSITION_IS surface. Explicit roster descriptors (menu/WotR setup)
	# are authoritative and are never overwritten by the legacy map defaults.
	var configured_team_starts: Variant = configuration.get("team_start_indices", {})
	_configured_team_start_indices = (
		(configured_team_starts as Dictionary).duplicate(true)
		if typeof(configured_team_starts) == TYPE_DICTIONARY
		else {}
	)
	var validated_team_starts := {}
	var team_starts_valid := typeof(configured_team_starts) == TYPE_DICTIONARY
	# An injected menu/WotR roster owns its assignments, including explicit
	# unset or invalid rows. Map defaults belong only to the true legacy roster.
	if team_starts_valid and _pending_team_roster.is_empty():
		for team_key in (configured_team_starts as Dictionary).keys():
			var start_value: Variant = (configured_team_starts as Dictionary)[team_key]
			if (
				typeof(team_key) != TYPE_INT
				or typeof(start_value) != TYPE_INT
				or int(start_value) < 0
				or not _team_descriptors.has(int(team_key))
			):
				team_starts_valid = false
				break
			validated_team_starts[int(team_key)] = int(start_value)
	if team_starts_valid:
		for team in validated_team_starts:
			var descriptor := (_team_descriptors.get(team, {}) as Dictionary).duplicate(true)
			if not descriptor.has("start_index"):
				descriptor["start_index"] = int(validated_team_starts[team])
				_team_descriptors[team] = descriptor
	source_map_configured = bool(configuration.get("source_map_configured", false))
	# Complete authored non-road Object stream for exact registry admission.
	# Invalid rows are inert; no map type is inferred or repaired here.
	_scenario_map_placements = []
	var configured_scenario: Variant = configuration.get("scenario_object_placements", [])
	if typeof(configured_scenario) == TYPE_ARRAY:
		for placement_value in configured_scenario as Array:
			if typeof(placement_value) != TYPE_DICTIONARY:
				continue
			var placement := placement_value as Dictionary
			if (
				typeof(placement.get("position")) != TYPE_VECTOR2
				or String(placement.get("type_name", "")) == ""
				or typeof(placement.get("source_index")) != TYPE_INT
				or typeof(placement.get("source_position")) != TYPE_VECTOR3
				or typeof(placement.get("properties", {})) != TYPE_DICTIONARY
			):
				continue
			_scenario_map_placements.append({
				"type_name": String(placement.get("type_name", "")),
				"source_index": int(placement.get("source_index", -1)),
				"source_position": Vector3(placement.get("source_position")),
				"position": Vector2(placement.get("position")),
				"yaw": float(placement.get("yaw", 0.0)),
				"properties": (placement.get("properties", {}) as Dictionary).duplicate(true),
			})
	# Lane L2b castle fixtures, same contract as the creep-lair block above:
	# validated and translated upstream by RetailMapData, inert here until the
	# opt-in rule seeds them; malformed rows are dropped so seeding never
	# guesses.
	_castle_fixture_placements = []
	var configured_fixtures: Variant = configuration.get("castle_fixture_placements", [])
	if typeof(configured_fixtures) == TYPE_ARRAY:
		for fixture_value in configured_fixtures as Array:
			if typeof(fixture_value) != TYPE_DICTIONARY:
				continue
			var fixture := fixture_value as Dictionary
			if (
				typeof(fixture.get("position")) != TYPE_VECTOR2
				or String(fixture.get("type_name", "")) == ""
				or typeof(fixture.get("source_index")) != TYPE_INT
				or typeof(fixture.get("maximum_health")) not in [TYPE_INT, TYPE_FLOAT]
			):
				continue
			var fixture_row := {
				"type_name": String(fixture.get("type_name", "")),
				"role": String(fixture.get("role", "")),
				"kind_of": (fixture.get("kind_of", []) as Array).duplicate(),
				"source_index": int(fixture.get("source_index", -1)),
				"position": Vector2(fixture.get("position")),
				"elevation": float(fixture.get("elevation", 0.0)),
				"yaw": float(fixture.get("yaw", 0.0)),
				"owner": String(fixture.get("owner", "")),
				"maximum_health": float(fixture.get("maximum_health", 1.0)),
				"armor": String(fixture.get("armor", "")),
				"indestructible": bool(fixture.get("indestructible", false)),
				"enabled": bool(fixture.get("enabled", true)),
				"targetable": bool(fixture.get("targetable", true)),
			}
			if fixture.has("initial_health"):
				fixture_row["initial_health"] = float(fixture.get("initial_health"))
			if fixture.has("garrison"):
				fixture_row["garrison"] = (fixture.get("garrison", {}) as Dictionary).duplicate(true)
			if fixture.has("gate"):
				fixture_row["gate"] = (fixture.get("gate") as Dictionary).duplicate(true)
			_castle_fixture_placements.append(fixture_row)


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
	_map_script_waypoints.clear()
	_ai_build_waypoints.clear()
	_castle_ai_base_layouts.clear()
	_castle_ai_base_fallback = "generic-any"
	_source_map_axis_x = Vector2.RIGHT
	_source_map_axis_z = Vector2.DOWN
	_extra_team_centers = {}
	_configured_team_start_indices = {}
	source_map_configured = false
	_scenario_map_placements = []
	_castle_fixture_placements = []


func _apply_gameplay_rules(gameplay_rules: Dictionary) -> void:
	_rules = gameplay_rules.duplicate(true)
	configuration_error = ""
	_snapshot_scenario_runtime_tables()
	if _rules.has("_scenario_registry_error"):
		configuration_error = String(_rules["_scenario_registry_error"])
		return
	ring_mechanic_enabled = bool(_rules.get("allow_ring_heroes", false))
	_configure_ring_mechanic_contract()
	if not _configure_faction_manifest():
		return
	if not _configure_scenario_structure_armor_projection():
		return
	_unit_prerequisites.clear()
	_unit_prerequisite_any_groups.clear()
	_structure_upgrade_contracts.clear()
	_structure_upgrade_effects.clear()
	_compiled_research_kinds.clear()
	_unit_upgrade_commands.clear()
	_unit_level_upgrades.clear()
	_unit_banner_carriers.clear()
	_banner_respawn_ticks_by_object.clear()
	units_without_upgrade_commands.clear()
	_unit_experience_rules.clear()
	_cah_award_contracts.clear()
	_cah_award_tallies.clear()
	cah_award_results.clear()
	_unit_module_contracts.clear()
	_structure_module_contracts.clear()
	_configure_death_weapon_rules_from_rules()
	_castle_upgrade_grants.clear()
	_experience_unauthored_victims.clear()
	_configure_ranger_runtime_contract()
	_configure_trebuchet_runtime_contract()
	_configure_playable_unit_runtime_contracts()
	_configure_playable_structure_module_contracts()
	_configure_structure_upgrade_chains()
	_configure_structure_research_contracts()
	_configure_manifest_builders()
	_validate_faction_manifest_coherence()
	_record_structure_armor_provisionals()
	base_loop_enabled = bool(_rules.get("enable_base_loop", false))
	# BFME1 build-plots-only toggle. Absent (every legacy runner and the untouched
	# menu) resolves false, so the freeform construction path stays byte-identical.
	build_plots_only = bool(_rules.get("build_plots_only", false))
	build_plots.clear()
	# Neutral creep-lair opt-in. Absent (every legacy runner and the untouched
	# menu) resolves false, so the default match — and the pinned battle
	# signature — stays byte-identical.
	if _rules.has("enable_scenario_map_placements") and not bool(_rules.get("enable_scenario_map_placements", false)):
		_rules.erase("enable_scenario_map_placements")
	scenario_map_placements_enabled = bool(_rules.get("enable_scenario_map_placements", false))
	capturable_neutrals_enabled = bool(_rules.get("enable_capturable_neutrals", false))
	# Map-authored castle fixtures (lane L2b). Absent-unless-enabled, same
	# hashed-rules contract as the fog lane's enable_fog_of_war: the key
	# only lives in `_rules` (which is hashed) when a match opts in. An
	# explicit false is stripped so it cannot desync a peer that omitted
	# the key; every legacy runner and the pinned scenario stay
	# byte-identical.
	if _rules.has("enable_castle_fixtures") and not bool(_rules.get("enable_castle_fixtures", false)):
		_rules.erase("enable_castle_fixtures")
	castle_fixtures_enabled = bool(_rules.get("enable_castle_fixtures", false))
	# Retail turn-rate / wheel-vs-reform / group-cohesion opt-in. Absent (every
	# legacy runner and the untouched menu) resolves false, so the pinned
	# behaviour signature stays byte-identical.
	retail_formation_movement = bool(_rules.get("retail_formation_movement", false))
	retail_locomotor_physics = bool(_rules.get("retail_locomotor_physics", false))
	# Retail shroud/fog-of-war opt-in. Absent resolves false, which is what every
	# legacy runner and the pinned scenario get. Note this flag CANNOT move any
	# hash even when true: the fog grid is derived presentation state and
	# contributes zero bytes to _authoritative_state() (retail_fog_of_war.gd's
	# header states the contract and retail_fog_of_war_runner asserts it).
	fog_of_war_enabled = bool(_rules.get("enable_fog_of_war", false))
	_fog_of_war = null
	command_point_cap = maxi(60, int(_rules.get("command_point_cap", 200)))
	var starting_resources := maxi(0, int(_rules.get("starting_resources", 1200 if base_loop_enabled else 0)))
	team_resources = _seed_team_map(starting_resources)
	team_command_points = _seed_team_map(120)
	_seed_team_manifest_tables()


## Skirmish-AI state (owned by the sim; logic lives in retail_sim_skirmish_ai.gd).
## Configured from the pack's raw openbfme.skirmish-ai document; false means the
## AI keeps its manifest production plan (stale-pack limitation, named in Q83).
var skirmish_ai_configured := false
var skirmish_ai_plans_by_side: Dictionary = {}
var skirmish_ai_difficulty: Dictionary = {}
var skirmish_ai_brutal_cheats: Dictionary = {}
const SkirmishAiSystemScript = preload("res://src/retail_slice/retail_sim_skirmish_ai.gd")
var _skirmish_ai_system = null
func _skirmish_ai_subsystem():
	if _skirmish_ai_system == null:
		_skirmish_ai_system = SkirmishAiSystemScript.new(self)
	return _skirmish_ai_system

func configure_skirmish_ai(document: Dictionary) -> Dictionary:
	return _skirmish_ai_subsystem().configure_skirmish_ai(document)

func skirmish_ai_plan_for_side(side: String) -> Dictionary:
	return _skirmish_ai_subsystem().skirmish_ai_plan_for_side(side)

const RingSystemScript = preload("res://src/retail_slice/retail_sim_ring.gd")
var _ring_system = null
func _ring_subsystem():
	if _ring_system == null:
		_ring_system = RingSystemScript.new(self)
	return _ring_system

func _configure_ring_mechanic_contract() -> void:
	_ring_subsystem()._configure_ring_mechanic_contract()

func _compiled_ring_runtime_contract(runtime: Dictionary) -> Dictionary:
	return _ring_subsystem()._compiled_ring_runtime_contract(runtime)

func _ring_state() -> Dictionary:
	return _ring_subsystem()._ring_state()

func _mark_ring_delivery_structures() -> void:
	_ring_subsystem()._mark_ring_delivery_structures()

func _mark_ring_delivery_structure(row: Dictionary) -> void:
	_ring_subsystem()._mark_ring_delivery_structure(row)

func _ring_gollum_object_id() -> String:
	return _ring_subsystem()._ring_gollum_object_id()

func _ring_spawn_team() -> int:
	return _ring_subsystem()._ring_spawn_team()

func _ring_eva(name: String) -> String:
	return _ring_subsystem()._ring_eva(name)

func ring_presentation_contract() -> Dictionary:
	return _ring_subsystem().ring_presentation_contract()

func _is_ring_gollum(row: Dictionary) -> bool:
	return _ring_subsystem()._is_ring_gollum(row)

func _existing_ring_gollum_id() -> int:
	return _ring_subsystem()._existing_ring_gollum_id()

func _spawn_ring_gollum_fallback() -> void:
	_ring_subsystem()._spawn_ring_gollum_fallback()

func _configure_ring_gollum(row: Dictionary) -> void:
	_ring_subsystem()._configure_ring_gollum(row)

func _drop_ring(at: Vector2, source_id: int, reason: String) -> void:
	_ring_subsystem()._drop_ring(at, source_id, reason)

func _on_ring_entity_death(entity_id: int, row: Dictionary) -> void:
	_ring_subsystem()._on_ring_entity_death(entity_id, row)

func _step_ring_gollum(gollum_id: int) -> void:
	_ring_subsystem()._step_ring_gollum(gollum_id)

func _step_ring_mechanic() -> void:
	_ring_subsystem()._step_ring_mechanic()

func _ring_pickup_eligible(candidate: Dictionary) -> bool:
	return _ring_subsystem()._ring_pickup_eligible(candidate)

const ConfigSystemScript = preload("res://src/retail_slice/retail_sim_config.gd")
var _config_system = null
func _config_subsystem():
	if _config_system == null:
		_config_system = ConfigSystemScript.new(self)
	return _config_system

func _configure_faction_manifest() -> bool:
	return _config_subsystem()._configure_faction_manifest()

func _record_structure_armor_provisionals() -> void:
	_config_subsystem()._record_structure_armor_provisionals()

func _compiled_armor_table(table_value: Variant) -> Dictionary:
	return _config_subsystem()._compiled_armor_table(table_value)

func _scenario_structure_kind(document: Dictionary) -> String: # de-static'd: moved to subsystem
	return _config_subsystem()._scenario_structure_kind(document)

func _scenario_structure_armor_projection(document: Dictionary) -> Dictionary:
	return _config_subsystem()._scenario_structure_armor_projection(document)

func _configure_scenario_structure_armor_projection() -> bool:
	return _config_subsystem()._configure_scenario_structure_armor_projection()

func _compiled_armor_rule(document: Dictionary) -> Dictionary:
	return _config_subsystem()._compiled_armor_rule(document)

func _parse_damage_scalar(entry: Dictionary) -> Dictionary:
	return _config_subsystem()._parse_damage_scalar(entry)

func _compiled_damage_scalars(rows: Array) -> Array:
	return _config_subsystem()._compiled_damage_scalars(rows)

func _compiled_weapon_upgrade_rules(document: Dictionary) -> Dictionary:
	return _config_subsystem()._compiled_weapon_upgrade_rules(document)

func _compiled_level_up_rules(document: Dictionary) -> Dictionary:
	return _config_subsystem()._compiled_level_up_rules(document)

func _compiled_banner_carrier(document: Dictionary) -> Dictionary:
	return _config_subsystem()._compiled_banner_carrier(document)

func _compiled_banner_respawn_ticks(document: Dictionary) -> int:
	return _config_subsystem()._compiled_banner_respawn_ticks(document)

func _compiled_castle_behavior(document: Dictionary) -> Dictionary:
	return _config_subsystem()._compiled_castle_behavior(document)

func configure_castle_behaviors(by_source_object_id: Dictionary) -> void:
	_config_subsystem().configure_castle_behaviors(by_source_object_id)

func _retail_source_to_sim_offset(offset_source: Vector2) -> Vector2:
	return _config_subsystem()._retail_source_to_sim_offset(offset_source)

func _compiled_unit_upgrade_commands(document: Dictionary) -> Array:
	return _config_subsystem()._compiled_unit_upgrade_commands(document)

func _filter_kind_matches(kind: String, target: Dictionary, target_kind: String) -> bool:
	return _config_subsystem()._filter_kind_matches(kind, target, target_kind)

func _damage_scalar_factor(scalars: Array, target: Dictionary, target_kind: String) -> float:
	return _config_subsystem()._damage_scalar_factor(scalars, target, target_kind)

func _compiled_damage_components(combat: Dictionary, source_scale: float = 1.0) -> Array:
	return _config_subsystem()._compiled_damage_components(combat, source_scale)

func _damage_components_for(attacker_id: int, damage_type_override: String) -> Array:
	return _config_subsystem()._damage_components_for(attacker_id, damage_type_override)

func _weighted_armor_scalar(scalars: Dictionary, components: Array, damage_type: String) -> float:
	return _config_subsystem()._weighted_armor_scalar(scalars, components, damage_type)

func _member_armor_scalar(target: Dictionary, damage_type: String, components: Array = []) -> float:
	return _config_subsystem()._member_armor_scalar(target, damage_type, components)

func _applied_weapon_effect(row: Dictionary) -> Dictionary:
	return _config_subsystem()._applied_weapon_effect(row)

func _configure_manifest_builders() -> void:
	_config_subsystem()._configure_manifest_builders()

func _validate_faction_manifest_coherence() -> void:
	_config_subsystem()._validate_faction_manifest_coherence()

func _compiled_ring_gollum_rule(registration: Dictionary) -> Dictionary:
	return _config_subsystem()._compiled_ring_gollum_rule(registration)

func _configure_playable_unit_runtime_contracts() -> void:
	_config_subsystem()._configure_playable_unit_runtime_contracts()

func _register_builder_production(document: Dictionary, configured_unit_rules: Dictionary, producer_kinds: Dictionary) -> bool:
	return _config_subsystem()._register_builder_production(document, configured_unit_rules, producer_kinds)

func _configure_ranger_runtime_contract() -> void:
	_config_subsystem()._configure_ranger_runtime_contract()

func _global_upgrade_source() -> Dictionary:
	return _config_subsystem()._global_upgrade_source()

func _manifest_upgrade_source(manifest: Dictionary) -> Dictionary:
	return _config_subsystem()._manifest_upgrade_source(manifest)

func _configure_structure_upgrade_chains() -> void:
	_config_subsystem()._configure_structure_upgrade_chains()

func _compile_structure_castle_upgrades(source: Dictionary, contracts: Dictionary) -> void:
	_config_subsystem()._compile_structure_castle_upgrades(source, contracts)

func _compile_structure_upgrade_chains(source: Dictionary, contracts: Dictionary) -> void:
	_config_subsystem()._compile_structure_upgrade_chains(source, contracts)

func _register_structure_upgrade_contract(contracts: Dictionary, upgrade_id: String, contract: Dictionary) -> bool:
	return _config_subsystem()._register_structure_upgrade_contract(contracts, upgrade_id, contract)

func _configure_structure_research_contracts() -> void:
	_config_subsystem()._configure_structure_research_contracts()

func _compile_structure_research_contracts(source: Dictionary, contracts: Dictionary, upgrade_effects: Dictionary, research_kinds: Dictionary) -> void:
	_config_subsystem()._compile_structure_research_contracts(source, contracts, upgrade_effects, research_kinds)

func _normalized_command_set_upgrade_effect(effect: Dictionary) -> Dictionary:
	return _config_subsystem()._normalized_command_set_upgrade_effect(effect)

func _research_gate_unsatisfied(team: int, building: Dictionary, contract: Dictionary) -> String:
	return _config_subsystem()._research_gate_unsatisfied(team, building, contract)

func _configure_trebuchet_runtime_contract() -> void:
	_config_subsystem()._configure_trebuchet_runtime_contract()

func _ranger_command_sets_are_valid(command_sets: Array) -> bool:
	return _config_subsystem()._ranger_command_sets_are_valid(command_sets)

func _team_structure_base(team: int) -> int:
	## Disjoint seeded-structure id band per team. Teams 0/1 keep their historical
	## 1000/2000 bands (byte-identical); teams >=2 tile above the dynamic id range.
	if team == PLAYER_TEAM:
		return PLAYER_STRUCTURE_BASE
	if team == ENEMY_TEAM:
		return ENEMY_STRUCTURE_BASE
	return 10000 + team * 1000


const SpawnSystemScript = preload("res://src/retail_slice/retail_sim_spawn.gd")
var _spawn_system = null
func _spawn_subsystem():
	if _spawn_system == null:
		_spawn_system = SpawnSystemScript.new(self)
	return _spawn_system

func _initialize_base_loop() -> void:
	_spawn_subsystem()._initialize_base_loop()

func _team_center(team: int) -> Vector2:
	return _spawn_subsystem()._team_center(team)

func _two_team_map_center() -> Vector2:
	return _spawn_subsystem()._two_team_map_center()

func _map_centroid() -> Vector2:
	return _spawn_subsystem()._map_centroid()

func _derive_home_layout() -> Dictionary:
	return _spawn_subsystem()._derive_home_layout()

func _fallback_structure_position(team: int, index: int) -> Vector2:
	return _spawn_subsystem()._fallback_structure_position(team, index)

func _fallback_rally_position(team: int) -> Vector2:
	return _spawn_subsystem()._fallback_rally_position(team)

func _builder_spawn_position(team: int) -> Vector2:
	return _spawn_subsystem()._builder_spawn_position(team)

func _spawn_anchor_position(anchor: String, team: int = PLAYER_TEAM) -> Vector2:
	return _spawn_subsystem()._spawn_anchor_position(anchor, team)

func _add_battalion(id: int, team: int, at: Vector2, display_name: String, object_id: String = SOLDIER_OBJECT_ID, unit_type: String = SOLDIER_HORDE_ID, command_points: int = -1, unit_rule_override: Dictionary = {}, cached_build_cost: int = -1) -> void:
	_spawn_subsystem()._add_battalion(id, team, at, display_name, object_id, unit_type, command_points, unit_rule_override, cached_build_cost)

func _recorded_damage_type(object_id: String, unit_rule: Dictionary) -> String:
	## No silent slash default: a combat unit without authored damageType is
	## recorded, and its structure damage falls to the kind's DEFAULT scalar.
	# Q80: manifest tables are required, so the mirror is authoritative —
	# no UNIT_DAMAGE_TYPES constant fallback.
	var damage_type := String(_unit_damage_types.get(object_id, ""))
	# A weapon whose nuggets author several types has no single damageType by
	# construction; it is typed per component, not missing a type.
	if damage_type == "" and not (_unit_damage_components.get(object_id, []) as Array).is_empty():
		return damage_type
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
	return _production_subsystem().producer_id(team, kind)


func resources_for_team(team: int) -> int:
	return int(team_resources.get(team, 0))


func command_points_for_team(team: int) -> int:
	return int(team_command_points.get(team, 0))


func _command_points_upgrade_bonus(team: int) -> int:
	var contract: Dictionary = (
		_team_tree(team).get("command_points_upgrade", {}) as Dictionary
	)
	if contract.is_empty():
		return 0
	var owned: Dictionary = team_upgrades.get(team, {}) as Dictionary
	if not owned.has(String(contract.get("triggeredBy", ""))):
		return 0
	if not _team_has_required_object(
		team, String(contract.get("requiredObject", ""))
	):
		return 0
	return int(contract.get("commandPoints", 0))


func command_point_total_for_team(team: int) -> int:
	if command_point_overrides_by_team.has(team):
		return int((command_point_overrides_by_team[team] as Dictionary).get(
			"total", command_point_cap
		))
	return command_point_cap + _command_points_upgrade_bonus(team)


func command_point_maximum_for_team(team: int) -> int:
	if command_point_overrides_by_team.has(team):
		return int((command_point_overrides_by_team[team] as Dictionary).get(
			"maximum", command_point_cap
		))
	return command_point_cap + _command_points_upgrade_bonus(team)


func override_command_points_for_team(team: int, total: int, maximum: int) -> bool:
	if not _roster_team_ids().has(team) or total < 0 or maximum < 0 or total > maximum:
		return false
	command_point_overrides_by_team[team] = {
		"total": total,
		"maximum": maximum,
	}
	_state_hash_static_digest.clear()
	return true


func _production_rule_value(unit_type: String, rule_key: String, default_key: String) -> int:
	return _production_subsystem()._production_rule_value(unit_type, rule_key, default_key)


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


func required_upgrade_any_group_for_unit(unit_type: String, producer_kind: String = "") -> Array:
	## The authored ANY-of production gate: owning ANY ONE of these upgrades
	## satisfies it. Empty for every unit whose button never set
	## `NeededUpgradeAny` and for every pack built before the converter emitted
	## the field, which leaves the gate pure ALL-of exactly as before.
	var value: Variant = _unit_prerequisite_any_groups.get(unit_type, null)
	if typeof(value) != TYPE_DICTIONARY:
		return []
	return Array((value as Dictionary).get(producer_kind, [])).duplicate()


func unlock_upgrades_for_unit(unit_type: String, producer_kind: String = "") -> Array:
	## Every upgrade id that participates in this unit's production gate, ALL-of
	## and ANY-of together. For discovery/UI listing only -- it says nothing
	## about how the gate combines them; use `production_gate_unsatisfied` to
	## evaluate ownership.
	var result: Array = required_upgrades_for_unit(unit_type, producer_kind)
	for value in required_upgrade_any_group_for_unit(unit_type, producer_kind):
		if not result.has(value):
			result.append(value)
	return result


func production_gate_unsatisfied(
	unit_type: String, producer_kind: String, completed_upgrades: Array,
	team_completed_upgrades: Array = []
) -> String:
	return _production_subsystem().production_gate_unsatisfied(unit_type, producer_kind, completed_upgrades, team_completed_upgrades)


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
	## Legacy Men fallback, now per team. Each rostered team registers the forge
	## provisionals into ITS OWN contract table, gated by ITS OWN build rules and
	## compiled research: a team whose forge already sells compiled PLAYER
	## technology skips the fallback. For the default same-faction roster every
	## team aliases the global dict by reference, so team 0 writes the provisionals
	## into the (hashed) global exactly once and every later team sees them
	## present and skips — byte-identical to the old single global registration.
	for team in _roster_team_ids():
		_register_forge_upgrade_contracts_for_team(int(team))



func _register_forge_upgrade_contracts_for_team(team: int) -> void:
	_upgrades_subsystem()._register_forge_upgrade_contracts_for_team(team)

func _equipment_ids_for_forge_upgrade(upgrade_id: String) -> Array:
	return _upgrades_subsystem()._equipment_ids_for_forge_upgrade(upgrade_id)

func _sorted_upgrade_ids(applied: Variant) -> Array[String]:
	return _upgrades_subsystem()._sorted_upgrade_ids(applied)

func _apply_equipment_to_horde(row: Dictionary, equipment: Array) -> void:
	_upgrades_subsystem()._apply_equipment_to_horde(row, equipment)

func _apply_team_upgrade_to_hordes(team: int, upgrade_id: String) -> void:
	_upgrades_subsystem()._apply_team_upgrade_to_hordes(team, upgrade_id)

func _apply_structure_create_grants(building: Dictionary, apply_create_when_complete: bool, apply_build_complete: bool) -> void:
	_upgrades_subsystem()._apply_structure_create_grants(building, apply_create_when_complete, apply_build_complete)

func _apply_structure_granted_upgrade(building: Dictionary, grant: Dictionary) -> void:
	_upgrades_subsystem()._apply_structure_granted_upgrade(building, grant)

func _apply_structure_inherit_upgrades(building: Dictionary) -> void:
	_upgrades_subsystem()._apply_structure_inherit_upgrades(building)

func queue_structure_upgrade(team: int, structure_id: int, upgrade_id: String) -> Dictionary:
	return _upgrades_subsystem().queue_structure_upgrade(team, structure_id, upgrade_id)

func structure_upgrade_queue_state(structure_id: int) -> Array[Dictionary]:
	return _upgrades_subsystem().structure_upgrade_queue_state(structure_id)

func _document_is_wall_upgrade_slot(document: Dictionary) -> bool: # de-static'd: moved to subsystem
	return _upgrades_subsystem()._document_is_wall_upgrade_slot(document)

func structure_upgrade_commands(structure_id: int) -> Array[Dictionary]:
	return _upgrades_subsystem().structure_upgrade_commands(structure_id)

func _sorted_unit_upgrade_commands(unit_type: String) -> Array:
	return _upgrades_subsystem()._sorted_unit_upgrade_commands(unit_type)

func _battalion_gate_unsatisfied(team: int, command: Dictionary) -> String:
	return _upgrades_subsystem()._battalion_gate_unsatisfied(team, command)

func _team_has_required_object(team: int, requirement: String) -> bool:
	return _upgrades_subsystem()._team_has_required_object(team, requirement)

func _discounted_battalion_upgrade_cost(team: int, command: Dictionary) -> int:
	return _upgrades_subsystem()._discounted_battalion_upgrade_cost(team, command)

func battalion_upgrade_commands(entity_id: int) -> Array[Dictionary]:
	return _upgrades_subsystem().battalion_upgrade_commands(entity_id)

func queue_battalion_upgrade(team: int, entity_id: int, upgrade_id: String) -> Dictionary:
	return _upgrades_subsystem().queue_battalion_upgrade(team, entity_id, upgrade_id)

func battalion_upgrade_queue_state(entity_id: int) -> Array[Dictionary]:
	return _upgrades_subsystem().battalion_upgrade_queue_state(entity_id)

func _step_battalion_upgrades() -> void:
	_upgrades_subsystem()._step_battalion_upgrades()

func _apply_structure_death_refund(building: Dictionary) -> void:
	_upgrades_subsystem()._apply_structure_death_refund(building)

func _apply_refund_die_on_death(owner: Dictionary) -> void:
	_upgrades_subsystem()._apply_refund_die_on_death(owner)

func _team_has_required_building_filter(team: int, filter: Array) -> bool:
	return _upgrades_subsystem()._team_has_required_building_filter(team, filter)

func _income_with_upgrade_bonus(team: int, building: Dictionary, base_income: int) -> int:
	return _upgrades_subsystem()._income_with_upgrade_bonus(team, building, base_income)

func _queued_command_points_for_team(team: int) -> int:
	return _upgrades_subsystem()._queued_command_points_for_team(team)

func set_spellbook_orb_open(open: bool) -> void:
	_powers_subsystem().set_spellbook_orb_open(open)


func configure_spellbook_runtime(document: Dictionary) -> bool:
	return _powers_subsystem().configure_spellbook_runtime(document)


var _team_spellbook_errors: Dictionary = {}


func configure_team_spellbook_runtime(team: int, document: Dictionary) -> bool:
	return _powers_subsystem().configure_team_spellbook_runtime(team, document)


func team_spellbook_error(team: int) -> String:
	return _powers_subsystem().team_spellbook_error(team)


func team_has_spellbook_override(team: int) -> bool:
	return _powers_subsystem().team_has_spellbook_override(team)


func _spellbook_global_bundle() -> Dictionary:
	return _powers_subsystem()._spellbook_global_bundle()


func _spellbook_global_bundle_copy() -> Dictionary:
	return _powers_subsystem()._spellbook_global_bundle_copy()


func _apply_spellbook_bundle(bundle: Dictionary) -> void:
	_powers_subsystem()._apply_spellbook_bundle(bundle)


func _team_tree(team: int) -> Dictionary:
	return _powers_subsystem()._team_tree(team)


func _reset_spellbook_match_state() -> void:
	_powers_subsystem()._reset_spellbook_match_state()


var _pending_power_effects: Array[Dictionary] = []
var _active_groves: Array[Dictionary] = []
## Live "ping" field effects: retail's PalantirVisionPing / FarSeeingPing /
## FrozenLandPing family — an IMMOBILE, UNATTACKABLE, weaponless object dropped
## at the cast point whose whole job is a bounded VisionRange reveal and/or an
## AttributeModifierAuraUpdate, removed by its authored LifetimeUpdate.
## EMPTY-IS-ABSENT in the serialized state (see _serialize_state).
var _field_pings: Array[Dictionary] = []
## entity_id → tick the summoned battalion fades (authored summon lifetime).
var _summon_despawn_ticks: Dictionary = {}
## entity_id -> true for live summoned battalions that carry converted auras.
## This is intentionally independent from LifetimeUpdate: an aura summon may
## be lifetime-less and must still refresh until the shared death boundary.
var _summon_aura_source_ids: Dictionary = {}


func _spellbook_effect_support(power_row: Dictionary, fields: Array, references: Dictionary, modifier_leaves: Dictionary, object_leaves: Dictionary, ocl_leaves: Dictionary, weapon_leaves: Dictionary) -> Dictionary:
	return _spellbook_compile_subsystem()._spellbook_effect_support(power_row, fields, references, modifier_leaves, object_leaves, ocl_leaves, weapon_leaves)


func _spellbook_field_float(fields: Dictionary, key: String, fallback: float) -> float:
	return _spellbook_compile_subsystem()._spellbook_field_float(fields, key, fallback)


func _parse_modifier_row(value: String) -> Dictionary:
	return _spellbook_compile_subsystem()._parse_modifier_row(value)


func _spellbook_ocl_support(power_row: Dictionary, references: Dictionary, modifier_leaves: Dictionary, object_leaves: Dictionary, ocl_leaves: Dictionary, weapon_leaves: Dictionary, create_location: String = "", secondary_object_filter: String = "") -> Dictionary:
	return _spellbook_compile_subsystem()._spellbook_ocl_support(power_row, references, modifier_leaves, object_leaves, ocl_leaves, weapon_leaves, create_location, secondary_object_filter)


func _spellbook_has_unconverted_hatch_payload(leaf: Dictionary, object_leaves: Dictionary, ocl_leaves: Dictionary) -> bool:
	return _spellbook_compile_subsystem()._spellbook_has_unconverted_hatch_payload(leaf, object_leaves, ocl_leaves)


func _spellbook_hatch_payload_leaves(leaf: Dictionary, object_leaves: Dictionary, ocl_leaves: Dictionary) -> Array:
	return _spellbook_compile_subsystem()._spellbook_hatch_payload_leaves(leaf, object_leaves, ocl_leaves)


func _spellbook_ocl_named_gap(spawns: Array, object_leaves: Dictionary, ocl_leaves: Dictionary, create_location: String, secondary_object_filter: String) -> Dictionary:
	return _spellbook_compile_subsystem()._spellbook_ocl_named_gap(spawns, object_leaves, ocl_leaves, create_location, secondary_object_filter)


var _weather_effects: Array[Dictionary] = []


func _spellbook_weather_modifier_support(field_values: Dictionary, field_resolved: Dictionary, modifier_leaves: Dictionary) -> Dictionary:
	return _spellbook_compile_subsystem()._spellbook_weather_modifier_support(field_values, field_resolved, modifier_leaves)


func _spellbook_weather_anticategory_support(field_values: Dictionary, field_resolved: Dictionary) -> Dictionary:
	return _spellbook_compile_subsystem()._spellbook_weather_anticategory_support(field_values, field_resolved)


func _spellbook_untamed_allegiance_support(field_values: Dictionary, field_resolved: Dictionary) -> Dictionary:
	return _spellbook_compile_subsystem()._spellbook_untamed_allegiance_support(field_values, field_resolved)


func _spellbook_fire_weapon_support(spawns: Array, weapon_leaves: Dictionary) -> Dictionary:
	return _spellbook_compile_subsystem()._spellbook_fire_weapon_support(spawns, weapon_leaves)


func _spellbook_weapon_damage_nuggets(weapon: Dictionary, weapon_leaves: Dictionary) -> Array:
	return _spellbook_compile_subsystem()._spellbook_weapon_damage_nuggets(weapon, weapon_leaves)


func _spellbook_weapon_field(weapon: Dictionary, key: String) -> float:
	return _spellbook_compile_subsystem()._spellbook_weapon_field(weapon, key)


func _spellbook_structure_summon_support(spawn: Dictionary, weapon_leaves: Dictionary) -> Dictionary:
	return _spellbook_compile_subsystem()._spellbook_structure_summon_support(spawn, weapon_leaves)


func _spellbook_direct_summon_support(spawns: Array, modifier_leaves: Dictionary, object_leaves: Dictionary, weapon_leaves: Dictionary) -> Dictionary:
	return _spellbook_compile_subsystem()._spellbook_direct_summon_support(spawns, modifier_leaves, object_leaves, weapon_leaves)


func _spellbook_create_pick_count(create: Dictionary, multiplier: int = 1) -> int:
	return _spellbook_compile_subsystem()._spellbook_create_pick_count(create, multiplier)


func _spellbook_create_enabled(create: Dictionary, owned_upgrades: Dictionary = {}) -> bool:
	return _spellbook_compile_subsystem()._spellbook_create_enabled(create, owned_upgrades)


func _spellbook_summon_support(spawns: Array, modifier_leaves: Dictionary, object_leaves: Dictionary, ocl_leaves: Dictionary, weapon_leaves: Dictionary) -> Dictionary:
	return _spellbook_compile_subsystem()._spellbook_summon_support(spawns, modifier_leaves, object_leaves, ocl_leaves, weapon_leaves)


func _spellbook_summon_literal_preview(spawns: Array, object_leaves: Dictionary, ocl_leaves: Dictionary) -> Dictionary:
	return _spellbook_compile_subsystem()._spellbook_summon_literal_preview(spawns, object_leaves, ocl_leaves)


func _spellbook_summon_rule(target_leaf: Dictionary, modifier_leaves: Dictionary, object_leaves: Dictionary, weapon_leaves: Dictionary) -> Dictionary:
	return _spellbook_compile_subsystem()._spellbook_summon_rule(target_leaf, modifier_leaves, object_leaves, weapon_leaves)


func _spellbook_summon_aura_rules(member: Dictionary, modifier_leaves: Dictionary) -> Dictionary:
	return _spellbook_compile_subsystem()._spellbook_summon_aura_rules(member, modifier_leaves)


func _spellbook_one_summon_aura_rule(member: Dictionary, aura: Dictionary, modifier_leaves: Dictionary, allow_marker_modifiers: bool = false) -> Dictionary:
	return _spellbook_compile_subsystem()._spellbook_one_summon_aura_rule(member, aura, modifier_leaves, allow_marker_modifiers)


func _spellbook_grove_chain(references: Dictionary, object_leaves: Dictionary, ocl_leaves: Dictionary) -> Dictionary:
	return _spellbook_compile_subsystem()._spellbook_grove_chain(references, object_leaves, ocl_leaves)


func _spellbook_grove_support(field_values: Dictionary, field_resolved: Dictionary, references: Dictionary, modifier_leaves: Dictionary, object_leaves: Dictionary, ocl_leaves: Dictionary = {}, object_field: String = "ElvenGroveObject") -> Dictionary:
	return _spellbook_compile_subsystem()._spellbook_grove_support(field_values, field_resolved, references, modifier_leaves, object_leaves, ocl_leaves, object_field)


func _spellbook_field_ping_support(spawns: Array, modifier_leaves: Dictionary) -> Dictionary:
	return _spellbook_compile_subsystem()._spellbook_field_ping_support(spawns, modifier_leaves)


func _spellbook_ping_invisibility_rules(leaf: Dictionary) -> Dictionary:
	return _spellbook_compile_subsystem()._spellbook_ping_invisibility_rules(leaf)


func _spellbook_cloudbreak_support(field_values: Dictionary, field_resolved: Dictionary) -> Dictionary:
	return _spellbook_compile_subsystem()._spellbook_cloudbreak_support(field_values, field_resolved)


func spellbook_available() -> bool:
	return _powers_subsystem().spellbook_available()


func spellbook_error() -> String:
	return _powers_subsystem().spellbook_error()


func spellbook_power_ids() -> Array[String]:
	return _powers_subsystem().spellbook_power_ids()


func spellbook_power(power_id: String) -> Dictionary:
	return _powers_subsystem().spellbook_power(power_id)


func _spellbook_world_scale() -> float:
	return _powers_subsystem()._spellbook_world_scale()


func power_points(team: int) -> int:
	return _powers_subsystem().power_points(team)


func owned_sciences(team: int) -> Array:
	return _powers_subsystem().owned_sciences(team)


func has_power(team: int, power_id: String) -> bool:
	return _powers_subsystem().has_power(team, power_id)


func _science_owned(team: int, science_id: String) -> bool:
	return _powers_subsystem()._science_owned(team, science_id)


func _power_prerequisites_met(team: int, science_id: String) -> bool:
	return _powers_subsystem()._power_prerequisites_met(team, science_id)


func can_purchase_power(team: int, power_id: String) -> Dictionary:
	return _powers_subsystem().can_purchase_power(team, power_id)


func purchase_power(team: int, power_id: String, cost: int = -1) -> Dictionary:
	return _powers_subsystem().purchase_power(team, power_id, cost)


func _nonpressable_purchase_activation(team: int, power_id: String, row: Dictionary) -> Dictionary:
	return _powers_subsystem()._nonpressable_purchase_activation(team, power_id, row)


func reset_spellbook_purchases(team: int) -> Dictionary:
	return _powers_subsystem().reset_spellbook_purchases(team)


func accept_spellbook_purchases(team: int) -> Dictionary:
	return _powers_subsystem().accept_spellbook_purchases(team)


func power_cooldown_state(team: int, power_id: String) -> Dictionary:
	return _powers_subsystem().power_cooldown_state(team, power_id)


func spellbook_power_radius_sim(power_id: String) -> float:
	return _powers_subsystem().spellbook_power_radius_sim(power_id)


func spellbook_ui_state(team: int) -> Dictionary:
	return _powers_subsystem().spellbook_ui_state(team)


func award_power_kill(team: int) -> void:
	_powers_subsystem().award_power_kill(team)


var _player_rank_ladder: Array[Dictionary] = []
var _player_rank_ladder_error := ""
var _team_player_rank: Dictionary = {}
var _team_player_skill_points: Dictionary = {}
var _team_player_rank_granted: Dictionary = {}

const RANK_SCIENCE_PURCHASE_POINTS_GRANTED_FIELD := "SciencePurchasePointsGranted"
const RANK_SKILL_POINTS_NEEDED_FIELD := "SkillPointsNeededDefault"


func configure_player_rank_science_grants(rows: Array) -> bool:
	return _powers_subsystem().configure_player_rank_science_grants(rows)


func _reject_player_rank_ladder(reason: String) -> bool:
	return _powers_subsystem()._reject_player_rank_ladder(reason)


func _rank_ladder_integer(row: Dictionary, key: String) -> int:
	return _powers_subsystem()._rank_ladder_integer(row, key)


func player_rank_ladder_error() -> String:
	return _powers_subsystem().player_rank_ladder_error()


func player_rank_ladder_size() -> int:
	return _powers_subsystem().player_rank_ladder_size()


func player_rank(team: int) -> int:
	return _powers_subsystem().player_rank(team)


func player_skill_points(team: int) -> int:
	return _powers_subsystem().player_skill_points(team)


func advance_player_rank(team: int, rank: int) -> Dictionary:
	return _powers_subsystem().advance_player_rank(team, rank)


func award_player_skill_points(team: int, amount: int) -> Dictionary:
	return _powers_subsystem().award_player_skill_points(team, amount)


const SCIENCE_PURCHASE_POINT_COST_FIELD := "SciencePurchasePointCost"
const SCIENCE_PURCHASE_POINT_COST_MP_FIELD := "SciencePurchasePointCostMP"
const SCIENCE_IS_GRANTABLE_FIELD := "IsGrantable"
const SCIENCE_PREREQUISITE_SCIENCES_FIELD := "PrerequisiteSciences"


func _spellbook_science_document_rows(team: int) -> Array:
	return _powers_subsystem()._spellbook_science_document_rows(team)


func _spellbook_science_document_row(team: int, science_id: String) -> Dictionary:
	return _powers_subsystem()._spellbook_science_document_row(team, science_id)


func _science_field_receipt(row: Dictionary, field: String) -> Dictionary:
	return _powers_subsystem()._science_field_receipt(row, field)


func science_purchase_cost(science_id: String, multiplayer: bool) -> int:
	return _powers_subsystem().science_purchase_cost(science_id, multiplayer)


func science_purchase_cost_receipt(science_id: String, multiplayer: bool) -> Dictionary:
	return _powers_subsystem().science_purchase_cost_receipt(science_id, multiplayer)


func science_is_grantable(science_id: String) -> bool:
	return _powers_subsystem().science_is_grantable(science_id)


func grant_science(team: int, science_id: String) -> Dictionary:
	return _powers_subsystem().grant_science(team, science_id)


func _science_prerequisites_met(team: int, row: Dictionary) -> bool:
	return _powers_subsystem()._science_prerequisites_met(team, row)


func _cast_spellbook_scavenger(team: int, effect: Dictionary) -> Dictionary:
	return _power_casts_subsystem()._cast_spellbook_scavenger(team, effect)


func _award_scavenger_bounty(attacker_id: int, victim: Dictionary, victim_kind: String) -> int:
	return _economy_subsystem().award_scavenger_bounty(attacker_id, victim, victim_kind)


func cast_power(team: int, power_id: String, point: Vector2) -> Dictionary:
	return _power_casts_subsystem().cast_power(team, power_id, point)


func cast_heal(team: int, point: Vector2) -> Dictionary:
	return _power_casts_subsystem().cast_heal(team, point)


func cast_rally(team: int, point: Vector2) -> Dictionary:
	return _power_casts_subsystem().cast_rally(team, point)


func _spellbook_object_kinds(row: Dictionary) -> Array:
	return _power_casts_subsystem()._spellbook_object_kinds(row)


func _spellbook_affects(row: Dictionary, filter_text: String) -> bool:
	return _power_casts_subsystem()._spellbook_affects(row, filter_text)


func _spellbook_filter_has_kind_terms(filter_text: String) -> bool:
	return _power_casts_subsystem()._spellbook_filter_has_kind_terms(filter_text)


func _cast_spellbook_heal(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	return _power_casts_subsystem()._cast_spellbook_heal(team, effect, point)


func _cast_spellbook_structure_heal(team: int, effect: Dictionary, point: Vector2, radius: float) -> Dictionary:
	return _power_casts_subsystem()._cast_spellbook_structure_heal(team, effect, point, radius)


func _cast_spellbook_attribute_modifier(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	return _power_casts_subsystem()._cast_spellbook_attribute_modifier(team, effect, point)


func _cast_spellbook_fire_weapon(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	return _power_casts_subsystem()._cast_spellbook_fire_weapon(team, effect, point)


func _cast_spellbook_summon(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	return _power_casts_subsystem()._cast_spellbook_summon(team, effect, point)


func _terminal_visible_summon_count(targets: Array) -> int:
	return _power_casts_subsystem()._terminal_visible_summon_count(targets)


func _spellbook_resolve_summon_targets(target_groups: Array) -> Array:
	return _power_casts_subsystem()._spellbook_resolve_summon_targets(target_groups)


func _cast_spellbook_structure_summon(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	return _power_casts_subsystem()._cast_spellbook_structure_summon(team, effect, point)


func _cast_spellbook_grove(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	return _power_casts_subsystem()._cast_spellbook_grove(team, effect, point)


func _cast_spellbook_field_ping(team: int, power_id: String, effect: Dictionary, point: Vector2) -> Dictionary:
	return _power_casts_subsystem()._cast_spellbook_field_ping(team, power_id, effect, point)


func team_revealed_regions(team: int) -> Array:
	return _power_casts_subsystem().team_revealed_regions(team)


func field_ping_count() -> int:
	return _power_casts_subsystem().field_ping_count()


func _step_field_pings() -> void:
	_power_casts_subsystem()._step_field_pings()


func _revoke_field_ping_invisibility(ping: Dictionary) -> void:
	_power_casts_subsystem()._revoke_field_ping_invisibility(ping)


func _cast_spellbook_cloudbreak(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	return _power_casts_subsystem()._cast_spellbook_cloudbreak(team, effect, point)


func _revoke_opposing_weather_for_cloudbreak(team: int) -> void:
	_power_casts_subsystem()._revoke_opposing_weather_for_cloudbreak(team)


func _cast_spellbook_weather_modifier(team: int, power_id: String, effect: Dictionary) -> Dictionary:
	return _power_casts_subsystem()._cast_spellbook_weather_modifier(team, power_id, effect)


func _cast_spellbook_weather_anticategory(team: int, power_id: String, effect: Dictionary) -> Dictionary:
	return _power_casts_subsystem()._cast_spellbook_weather_anticategory(team, power_id, effect)


func _apply_weather_modifier(entry: Dictionary) -> int:
	return _power_casts_subsystem()._apply_weather_modifier(entry)


func _set_leadership_suppression_source(row: Dictionary, source_key: String, expire_tick: int) -> void:
	_power_casts_subsystem()._set_leadership_suppression_source(row, source_key, expire_tick)


func _erase_leadership_suppression_source(row: Dictionary, source_key: String) -> void:
	_power_casts_subsystem()._erase_leadership_suppression_source(row, source_key)


func _refresh_leadership_suppression(row: Dictionary) -> int:
	return _power_casts_subsystem()._refresh_leadership_suppression(row)


func _apply_weather_anticategory(entry: Dictionary) -> int:
	return _power_casts_subsystem()._apply_weather_anticategory(entry)


func _step_weather_effects() -> void:
	_power_casts_subsystem()._step_weather_effects()


func active_weather_effects() -> Array:
	return _power_casts_subsystem().active_weather_effects()


func _migrate_restored_weather_sources() -> void:
	_power_casts_subsystem()._migrate_restored_weather_sources()


func _cast_spellbook_creep_allegiance(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	return _power_casts_subsystem()._cast_spellbook_creep_allegiance(team, effect, point)



func _apply_scenario_structure_faction_command_set(row: Dictionary, team: int) -> Dictionary:
	return _config_subsystem()._apply_scenario_structure_faction_command_set(row, team)

func _structure_command_set_upgrade_effects(row: Dictionary) -> Array[Dictionary]:
	return _config_subsystem()._structure_command_set_upgrade_effects(row)

func _reconcile_structure_command_set_upgrades(row: Dictionary) -> Dictionary:
	return _config_subsystem()._reconcile_structure_command_set_upgrades(row)

func _dictionary_has_casefolded_key(values: Dictionary, expected: String) -> bool:
	for key_value in values.keys():
		if String(key_value).nocasecmp_to(expected) == 0:
			return true
	return false


func _array_has_casefolded_string(values: Array, expected: String) -> bool:
	for value in values:
		if String(value).nocasecmp_to(expected) == 0:
			return true
	return false


func _refresh_team_command_set_upgrades(team: int) -> void:
	for structure_id in structure_ids(team):
		_reconcile_structure_command_set_upgrades(structures[structure_id] as Dictionary)


func _step_pending_power_effects() -> void:
	_power_casts_subsystem()._step_pending_power_effects()


func _fire_death_weapon(effect: Dictionary) -> void:
	_power_casts_subsystem()._fire_death_weapon(effect)


func _fire_power_strike(effect: Dictionary) -> void:
	_power_casts_subsystem()._fire_power_strike(effect)


func _apply_area_damage_to_battalion(id: int, amount: float, damage_type: String) -> void:
	_power_casts_subsystem()._apply_area_damage_to_battalion(id, amount, damage_type)


func _apply_area_damage_to_structure(structure_id: int, amount: float, damage_type: String) -> void:
	_power_casts_subsystem()._apply_area_damage_to_structure(structure_id, amount, damage_type)


func _fire_power_summon(effect: Dictionary) -> void:
	_power_casts_subsystem()._fire_power_summon(effect)


func _spawn_summon_targets(team: int, point: Vector2, targets: Array) -> Array:
	return _power_casts_subsystem()._spawn_summon_targets(team, point, targets)


func spawn_script_object(object_type: String, team: int, at: Vector2, ring_fallback := false, scenario_surface: String = "script-spawn") -> int:
	return _power_casts_subsystem().spawn_script_object(object_type, team, at, ring_fallback, scenario_surface)


func _step_summon_despawns() -> void:
	_power_casts_subsystem()._step_summon_despawns()


func _step_summon_auras() -> void:
	_power_casts_subsystem()._step_summon_auras()


func _refresh_one_summon_aura(source_id: int, source: Dictionary, aura: Dictionary) -> void:
	_power_casts_subsystem()._refresh_one_summon_aura(source_id, source, aura)


func _summon_aura_allows_relation(aura: Dictionary, same_team: bool) -> bool:
	return _power_casts_subsystem()._summon_aura_allows_relation(aura, same_team)


func _step_grove_auras() -> void:
	_power_casts_subsystem()._step_grove_auras()


func _spellbook_member_affects(
	row: Dictionary, filter_text: String, same_team: Variant = null
) -> bool:
	return _power_casts_subsystem()._spellbook_member_affects(row, filter_text, same_team)


func _step_structure_weapons() -> void:
	_bases_subsystem()._step_structure_weapons()


func _structure_weapon_period_ticks(attack: Dictionary) -> int:
	return _bases_subsystem()._structure_weapon_period_ticks(attack)


func _resolve_structure_weapon_impact(
	structure_id: int, attack: Dictionary, pending: Dictionary
) -> void:
	_bases_subsystem()._resolve_structure_weapon_impact(structure_id, attack, pending)


func validate_construct_site(builder_ids: Array[int], structure_kind: String, point: Vector2, team: int = PLAYER_TEAM) -> Dictionary:
	return _bases_subsystem().validate_construct_site(builder_ids, structure_kind, point, team)


const EXPANSION_PAD_LAYOUT := {
	"corner": [Vector2(-6.0, -6.0), Vector2(6.0, -6.0), Vector2(-6.0, 6.0), Vector2(6.0, 6.0)],
	"side": [Vector2(0.0, -7.5), Vector2(0.0, 7.5)],
}
var expansion_pads: Dictionary = {}
var _expansion_build_rules: Dictionary = {}
var _next_expansion_structure_id := 9000

# --- BFME1 build-plots-only mode -------------------------------------------
# When on, freeform porter placement is disallowed: issue_construct only
# succeeds on a designated empty build plot. Default off keeps today's
# byte-identical BFME2 freeform behavior (this flag is absent from _rules for
# every legacy runner, so the default path and the pinned signature never
# move). Threaded from the menu via GameState.retail_build_plots_only ->
# gameplay_rules["build_plots_only"] -> _apply_gameplay_rules.
var build_plots_only: bool = false
# team:int -> Array[Dictionary{position:Vector2, occupant_structure_id:int}].
# Empty unless build_plots_only is on. Serialized in the authoritative state so
# snapshots round-trip plot occupancy.
var build_plots: Dictionary = {}
# DOCUMENTED CHOICE: converted maps carry no BFME1 build-plot markers (checked
# map data and docs — none preserve retail's BUILDPLOT bones), so plots-only
# mode seeds a deterministic fixed ring of 8 standalone plots at radius 12.0
# local units around each team's fortress (falling back to the team center).
# The ring clears the fortress (4.0 placement radius) and its expansion pads.
const BUILD_PLOT_RING_OFFSETS: Array = [
	Vector2(12.0, 0.0), Vector2(8.485, 8.485), Vector2(0.0, 12.0), Vector2(-8.485, 8.485),
	Vector2(-12.0, 0.0), Vector2(-8.485, -8.485), Vector2(0.0, -12.0), Vector2(8.485, -8.485),
]
# A construct click within this many local units of a free plot's center snaps
# to that plot; farther clicks are rejected ("pick an empty plot").
const BUILD_PLOT_PICK_RADIUS := 5.0


## rules: {kind: {"cost": int, "seconds": float, "health": int, "pad_kinds":
## Array, "name": String, "object_id": String}} — derived by the slice from
## the playable-structure expansion documents; empty rules fail closed.
func configure_expansion_rules(rules: Dictionary) -> void:
	_bases_subsystem().configure_expansion_rules(rules)


func _seed_expansion_pads_for(fortress_id: int) -> void:
	_bases_subsystem()._seed_expansion_pads_for(fortress_id)


func _unpack_castle_behavior_for_structure(structure_id: int) -> bool:
	return _bases_subsystem()._unpack_castle_behavior_for_structure(structure_id)


func _castle_behavior_for_structure(structure: Dictionary) -> Dictionary:
	return _bases_subsystem()._castle_behavior_for_structure(structure)


func _spawn_castle_piece_structure(
	fortress_id: int,
	fortress: Dictionary,
	source_object_id: String,
	object_id: String,
	at: Vector2,
	elevation_source: float,
	angle_radians: float,
	maximum_health: int,
	piece_index: int
) -> int:
	return _bases_subsystem()._spawn_castle_piece_structure(fortress_id, fortress, source_object_id, object_id, at, elevation_source, angle_radians, maximum_health, piece_index)


func _seed_all_expansion_pads() -> void:
	_bases_subsystem()._seed_all_expansion_pads()


func expansion_pad_states(fortress_id: int) -> Array:
	return _bases_subsystem().expansion_pad_states(fortress_id)


func _seed_build_plots_for_team(team: int) -> void:
	_bases_subsystem()._seed_build_plots_for_team(team)


func _seed_all_build_plots() -> void:
	_bases_subsystem()._seed_all_build_plots()


func _reconcile_build_plots(team: int) -> void:
	_bases_subsystem()._reconcile_build_plots(team)


func build_plot_states(team: int) -> Array:
	return _bases_subsystem().build_plot_states(team)


func _free_build_plot_index_near(team: int, position: Vector2) -> int:
	return _bases_subsystem()._free_build_plot_index_near(team, position)


func expansion_commands_for(fortress_id: int) -> Array:
	return _bases_subsystem().expansion_commands_for(fortress_id)


func issue_expansion_construct(team: int, fortress_id: int, expansion_kind: String, requested_pad_index: int = -1) -> Dictionary:
	return _bases_subsystem().issue_expansion_construct(team, fortress_id, expansion_kind, requested_pad_index)


var unpackable_bases: Dictionary = {}


func configure_unpackable_bases(bases: Dictionary) -> bool:
	return _bases_subsystem().configure_unpackable_bases(bases)


func unpackable_base_names() -> Array[String]:
	return _bases_subsystem().unpackable_base_names()


func unpackable_base_state(base_name: String) -> Dictionary:
	return _bases_subsystem().unpackable_base_state(base_name)


func base_flag_unpackable(base_name: String) -> Dictionary:
	return _bases_subsystem().base_flag_unpackable(base_name)


func unpack_base(team: int, base_name: String, free: bool) -> Dictionary:
	return _bases_subsystem().unpack_base(team, base_name, free)


var map_named_object_namespace: Dictionary = {}


## Script-world state (owned by the sim; logic lives in retail_sim_script_world.gd)
var logic_random_draws: int = 0
var script_env_view_faults: int = 0
var _script_executors: Dictionary = {}
var _script_executor_expected_ticks: Dictionary = {}
var script_wiring_faults: int = 0
var script_unit_references: Dictionary = {}
var script_entity_references: Dictionary = {}
var create_object_die_pending: Array = []
var building_permissions_by_team: Dictionary = {}
var script_object_type_lists: Dictionary = {}
var script_teams: Dictionary = {}
var team_behavior_states: Dictionary = {}
var sequential_script_queues: Dictionary = {}
var production_controls_by_team: Dictionary = {}
var tech_buildability: Dictionary = {}
var script_team_references: Dictionary = {}
var player_progression: Dictionary = {}  # team -> {rank, skill_points, ...}
var player_economy_extras: Dictionary = {}  # team -> counters
var player_diplomacy_overrides: Dictionary = {}  # team -> other_team -> relation int
var script_event_counts: Dictionary = {}  # key -> count
var containment: Dictionary = {}  # container_structure_id -> Array[entity_id]
var entity_container: Dictionary = {}  # entity_id -> structure_id
var script_areas: Dictionary = {}  # name -> {center:Vector2, radius:float, impassable:bool}
var script_waypoints: Dictionary = {}  # name -> Vector2
var script_waypoint_paths: Dictionary = {}  # name -> Array[String]
var team_created_edge: Dictionary = {}  # script_team -> bool (one-shot edge)
var match_script_flags: Dictionary = {}  # global match flags
var attack_priority_names: Dictionary = {}  # team -> String
var selection_script_names: Array = []  # diagnostic selection list
var _logic_random_state: Array = []
var script_env_state: Dictionary = {}
var _script_env_view_reported: Dictionary = {}
var _script_executor_faults: Dictionary = {}

const ScriptWorldSystemScript = preload("res://src/retail_slice/retail_sim_script_world.gd")
var _script_world_system = null
func _script_world_subsystem():
	if _script_world_system == null:
		_script_world_system = ScriptWorldSystemScript.new(self)
	return _script_world_system

func configure_map_named_object_namespace(names: Array) -> void:
	_script_world_subsystem().configure_map_named_object_namespace(names)

func map_named_object_namespace_declared() -> bool:
	return _script_world_subsystem().map_named_object_namespace_declared()

func map_declares_named_object(name: String) -> bool:
	return _script_world_subsystem().map_declares_named_object(name)

func set_building_allowed(team: int, object_type: String, allowed: bool) -> bool:
	return _script_world_subsystem().set_building_allowed(team, object_type, allowed)

func _building_object_identity_key(value: String) -> String:
	return _script_world_subsystem()._building_object_identity_key(value)

func building_permission_for_kind(team: int, structure_kind: String) -> Dictionary:
	return _script_world_subsystem().building_permission_for_kind(team, structure_kind)

func bind_script_entity_reference(team: int, reference: String, entity_id: int) -> bool:
	return _script_world_subsystem().bind_script_entity_reference(team, reference, entity_id)

func script_entity_reference(team: int, reference: String) -> int:
	return _script_world_subsystem().script_entity_reference(team, reference)

func bind_script_unit_reference(team: int, reference: String, structure_id: int) -> bool:
	return _script_world_subsystem().bind_script_unit_reference(team, reference, structure_id)

func bind_script_unit_reference_to_base(team: int, reference: String, base_name: String) -> bool:
	return _script_world_subsystem().bind_script_unit_reference_to_base(team, reference, base_name)

func script_unit_reference(team: int, reference: String) -> int:
	return _script_world_subsystem().script_unit_reference(team, reference)

func script_unit_reference_base(team: int, reference: String) -> String:
	return _script_world_subsystem().script_unit_reference_base(team, reference)

func change_object_type_list(list_name: String, object_type: String, add: bool) -> Dictionary:
	return _script_world_subsystem().change_object_type_list(list_name, object_type, add)

func object_type_list_names() -> Array[String]:
	return _script_world_subsystem().object_type_list_names()

func has_object_type_list(list_name: String) -> bool:
	return _script_world_subsystem().has_object_type_list(list_name)

func resolve_object_type_names(object_type_list: String) -> Array:
	return _script_world_subsystem().resolve_object_type_names(object_type_list)

func _script_owner_exists(owner: int) -> bool:
	return _script_world_subsystem()._script_owner_exists(owner)

func register_script_team(team_name: String, owner: int, default_team: bool = false, handles: Array = [], membership_complete: bool = true, unresolved_members: Array = [], unmodeled_object_count: int = 0, dynamic_default_roster: bool = true, marker_only: bool = false) -> Dictionary:
	return _script_world_subsystem().register_script_team(team_name, owner, default_team, handles, membership_complete, unresolved_members, unmodeled_object_count, dynamic_default_roster, marker_only)

func _script_member_less(a: Dictionary, b: Dictionary) -> bool:
	return _script_world_subsystem()._script_member_less(a, b)

func script_team_owner(team_name: String) -> Dictionary:
	return _script_world_subsystem().script_team_owner(team_name)

func transfer_script_team_controlling_player(team_name: String, destination_owner: int) -> Dictionary:
	return _script_world_subsystem().transfer_script_team_controlling_player(team_name, destination_owner)

func script_team_members(team_name: String, living_only: bool = true) -> Dictionary:
	return _script_world_subsystem().script_team_members(team_name, living_only)

func _script_team_definition(record: Dictionary) -> Dictionary:
	return _script_world_subsystem()._script_team_definition(record)

func set_script_team_recruitable(team_name: String, enabled: bool) -> Dictionary:
	return _script_world_subsystem().set_script_team_recruitable(team_name, enabled)

func _script_team_state_view() -> Dictionary:
	return _script_world_subsystem()._script_team_state_view()

func set_team_behavior_state(team: String, token: String) -> Dictionary:
	return _script_world_subsystem().set_team_behavior_state(team, token)

func team_behavior_state(team: String) -> Dictionary:
	return _script_world_subsystem().team_behavior_state(team)

func set_team_custom_state(team: String, token: String, enabled: bool) -> Dictionary:
	return _script_world_subsystem().set_team_custom_state(team, token, enabled)

func team_custom_states(team: String) -> Dictionary:
	return _script_world_subsystem().team_custom_states(team)

func _prune_team_behavior_key(team: String, key: String) -> void:
	_script_world_subsystem()._prune_team_behavior_key(team, key)

func queue_team_sequential_script(script_team: String, script_name: String, times_to_loop: int) -> Dictionary:
	return _script_world_subsystem().queue_team_sequential_script(script_team, script_name, times_to_loop)

func clear_team_sequential_scripts(script_team: String) -> Dictionary:
	return _script_world_subsystem().clear_team_sequential_scripts(script_team)

func mark_team_sequential_busy(script_team: String) -> void:
	_script_world_subsystem().mark_team_sequential_busy(script_team)

func mark_team_sequential_idle(script_team: String) -> void:
	_script_world_subsystem().mark_team_sequential_idle(script_team)

func set_entity_object_status(entity_id: int, status: String, enabled: bool) -> Dictionary:
	return _script_world_subsystem().set_entity_object_status(entity_id, status, enabled)

func entity_has_object_status(entity_id: int, status: String) -> bool:
	return _script_world_subsystem().entity_has_object_status(entity_id, status)

func set_entities_object_status(entity_ids: Array, status: String, enabled: bool) -> Dictionary:
	return _script_world_subsystem().set_entities_object_status(entity_ids, status, enabled)

func _production_controls_for(team: int) -> Dictionary:
	return _script_world_subsystem()._production_controls_for(team)

func _set_production_control_flag(team: int, key: String, enabled: bool) -> Dictionary:
	return _script_world_subsystem()._set_production_control_flag(team, key, enabled)

func set_auto_build_enabled(team: int, enabled: bool) -> Dictionary:
	return _script_world_subsystem().set_auto_build_enabled(team, enabled)

func set_base_construction_enabled(team: int, enabled: bool) -> Dictionary:
	return _script_world_subsystem().set_base_construction_enabled(team, enabled)

func set_factories_enabled(team: int, enabled: bool) -> Dictionary:
	return _script_world_subsystem().set_factories_enabled(team, enabled)

func set_base_construction_speed(team: int, factor: float) -> Dictionary:
	return _script_world_subsystem().set_base_construction_speed(team, factor)

func set_unit_construction_enabled(team: int, object_type: String, enabled: bool) -> Dictionary:
	return _script_world_subsystem().set_unit_construction_enabled(team, object_type, enabled)

func production_control_enabled(team: int, key: String) -> bool:
	return _script_world_subsystem().production_control_enabled(team, key)

func unit_construction_enabled(team: int, object_type: String) -> bool:
	return _script_world_subsystem().unit_construction_enabled(team, object_type)

func set_tech_buildability(object_type: String, buildability: int) -> Dictionary:
	return _script_world_subsystem().set_tech_buildability(object_type, buildability)

func has_prerequisite_to_build(team: int, object_type: String) -> Dictionary:
	return _script_world_subsystem().has_prerequisite_to_build(team, object_type)

func bind_script_team_reference(owner_team: int, reference: String, script_team: String) -> Dictionary:
	return _script_world_subsystem().bind_script_team_reference(owner_team, reference, script_team)

func script_team_reference(owner_team: int, reference: String) -> String:
	return _script_world_subsystem().script_team_reference(owner_team, reference)

func set_entity_stopping_distance(entity_id: int, distance: float) -> Dictionary:
	return _script_world_subsystem().set_entity_stopping_distance(entity_id, distance)

func set_entities_idle_until(entity_ids: Array, until_tick: int) -> Dictionary:
	return _script_world_subsystem().set_entities_idle_until(entity_ids, until_tick)

func set_entities_spin_until(entity_ids: Array, until_tick: int) -> Dictionary:
	return _script_world_subsystem().set_entities_spin_until(entity_ids, until_tick)

func issue_hunt(ids: Array[int], team: int = PLAYER_TEAM) -> int:
	return _script_world_subsystem().issue_hunt(ids, team)

func _player_prog(team: int) -> Dictionary:
	return _script_world_subsystem()._player_prog(team)

func _set_player_prog_value(team: int, key: String, value: Variant) -> Dictionary:
	return _script_world_subsystem()._set_player_prog_value(team, key, value)

func set_diplomacy_override(from_team: int, to_team: int, relation: int) -> Dictionary:
	return _script_world_subsystem().set_diplomacy_override(from_team, to_team, relation)

func clear_diplomacy_override(from_team: int, to_team: int) -> Dictionary:
	return _script_world_subsystem().clear_diplomacy_override(from_team, to_team)

func clear_all_diplomacy_overrides(from_team: int) -> Dictionary:
	return _script_world_subsystem().clear_all_diplomacy_overrides(from_team)

func set_attack_priority_entry(set_name: String, target_kind: String, target_name: String, priority: int) -> void:
	_script_world_subsystem().set_attack_priority_entry(set_name, target_kind, target_name, priority)

func set_default_attack_priority_entry(set_name: String, priority: int) -> void:
	_script_world_subsystem().set_default_attack_priority_entry(set_name, priority)

func set_team_ai_priority(team: int, priority: int) -> Dictionary:
	return _script_world_subsystem().set_team_ai_priority(team, priority)

func adjust_team_ai_priority(team: int, delta: int) -> Dictionary:
	return _script_world_subsystem().adjust_team_ai_priority(team, delta)

func _add_player_prog_value(team: int, key: String, delta: int) -> Dictionary:
	return _script_world_subsystem()._add_player_prog_value(team, key, delta)

func set_entity_bool_flag(entity_id: int, flag: String, enabled: bool) -> Dictionary:
	return _script_world_subsystem().set_entity_bool_flag(entity_id, flag, enabled)

func entity_bool_flag(entity_id: int, flag: String) -> bool:
	return _script_world_subsystem().entity_bool_flag(entity_id, flag)

func set_entities_bool_flag(entity_ids: Array, flag: String, enabled: bool) -> Dictionary:
	return _script_world_subsystem().set_entities_bool_flag(entity_ids, flag, enabled)

func set_entity_string_state(entity_id: int, key: String, value: String) -> Dictionary:
	return _script_world_subsystem().set_entity_string_state(entity_id, key, value)

func entity_string_state(entity_id: int, key: String) -> String:
	return _script_world_subsystem().entity_string_state(entity_id, key)

func set_entity_timed_flag(entity_id: int, flag: String, until_tick: int) -> Dictionary:
	return _script_world_subsystem().set_entity_timed_flag(entity_id, flag, until_tick)

func entity_timed_flag_active(entity_id: int, flag: String) -> bool:
	return _script_world_subsystem().entity_timed_flag_active(entity_id, flag)

func script_set_health_percent(entity_id: int, percent: float) -> Dictionary:
	return _script_world_subsystem().script_set_health_percent(entity_id, percent)

func script_kill_entity(entity_id: int) -> Dictionary:
	return _script_world_subsystem().script_kill_entity(entity_id)

func script_damage_entity(entity_id: int, amount: float) -> Dictionary:
	return _script_world_subsystem().script_damage_entity(entity_id, amount)

func contain_entity(structure_id: int, entity_id: int) -> Dictionary:
	return _script_world_subsystem().contain_entity(structure_id, entity_id)

func exit_entity_container(entity_id: int) -> Dictionary:
	return _script_world_subsystem().exit_entity_container(entity_id)

func passenger_count(structure_id: int) -> int:
	return _script_world_subsystem().passenger_count(structure_id)

func register_script_area(name: String, center: Vector2, radius: float) -> void:
	_script_world_subsystem().register_script_area(name, center, radius)

func register_script_waypoint(name: String, position: Vector2) -> void:
	_script_world_subsystem().register_script_waypoint(name, position)

func register_script_waypoint_path(name: String, points: Array) -> void:
	_script_world_subsystem().register_script_waypoint_path(name, points)

func area_contains(name: String, position: Vector2) -> Dictionary:
	return _script_world_subsystem().area_contains(name, position)

func bump_script_event(key: String, amount: int = 1) -> void:
	_script_world_subsystem().bump_script_event(key, amount)

func script_event_count(key: String) -> int:
	return _script_world_subsystem().script_event_count(key)

func mark_team_created(script_team: String) -> void:
	_script_world_subsystem().mark_team_created(script_team)

func team_created_is_set(script_team: String) -> bool:
	return _script_world_subsystem().team_created_is_set(script_team)

func clear_team_created_edges() -> void:
	_script_world_subsystem().clear_team_created_edges()

func set_entity_team(entity_id: int, new_team: int) -> Dictionary:
	return _script_world_subsystem().set_entity_team(entity_id, new_team)

func delete_entity(entity_id: int) -> Dictionary:
	return _script_world_subsystem().delete_entity(entity_id)

func _executor_for_script(script_name: String) -> SageScriptExecutor:
	return _script_world_subsystem()._executor_for_script(script_name)

func _step_sequential_scripts() -> void:
	_script_world_subsystem()._step_sequential_scripts()

static func _logic_random_seed_words(seed_value: int) -> Array:
	return ScriptWorldSystemScript._logic_random_seed_words(seed_value)

static func _logic_random_draw32(words: Array) -> int:
	return ScriptWorldSystemScript._logic_random_draw32(words)

func logic_random_int(low: int, high: int) -> int:
	return _script_world_subsystem().logic_random_int(low, high)

func logic_random_real(low: float, high: float) -> float:
	return _script_world_subsystem().logic_random_real(low, high)

func attach_script_env(env: SageScriptEnv, team: int) -> bool:
	return _script_world_subsystem().attach_script_env(env, team)

func _script_env_state_view() -> Dictionary:
	return _script_world_subsystem()._script_env_state_view()

func _report_script_env_view_fault(team_key: Variant, path: String) -> void:
	_script_world_subsystem()._report_script_env_view_fault(team_key, path)

func _sorted_dictionary_keys(source: Dictionary) -> Array:
	return _script_world_subsystem()._sorted_dictionary_keys(source)

func register_script_executor(executor: SageScriptExecutor, team: int) -> bool:
	return _script_world_subsystem().register_script_executor(executor, team)

func unregister_script_executor(team: int) -> bool:
	return _script_world_subsystem().unregister_script_executor(team)

func registered_script_executor_teams() -> Array:
	return _script_world_subsystem().registered_script_executor_teams()

func _step_script_executors() -> void:
	_script_world_subsystem()._step_script_executors()

func _quarantine_script_executor(team_key: Variant, reason: String) -> void:
	_script_world_subsystem()._quarantine_script_executor(team_key, reason)

func _rebase_script_executor_offsets() -> void:
	_script_world_subsystem()._rebase_script_executor_offsets()

func count_objects_of_types(team: int, type_names: Array, include_dead: bool) -> int:
	return _script_world_subsystem().count_objects_of_types(team, type_names, include_dead)

func living_object_levels_of_types(team: int, type_names: Array) -> Array[int]:
	return _script_world_subsystem().living_object_levels_of_types(team, type_names)

func nearest_object_of_types(origin: Vector2, type_names: Array, owner_teams: Array) -> Dictionary:
	return _script_world_subsystem().nearest_object_of_types(origin, type_names, owner_teams)

func fieldable_object_type(name: String) -> bool:
	return _script_world_subsystem().fieldable_object_type(name)

func _object_type_probe(type_names: Array) -> Dictionary:
	return _script_world_subsystem()._object_type_probe(type_names)

func _entity_matches_types(row: Dictionary, probe: Dictionary) -> bool:
	return _script_world_subsystem()._entity_matches_types(row, probe)

func _structure_matches_types(row: Dictionary, probe: Dictionary) -> bool:
	return _script_world_subsystem()._structure_matches_types(row, probe)

func _structure_kinds_matching_probe(team: int, probe: Dictionary) -> Dictionary:
	return _script_world_subsystem()._structure_kinds_matching_probe(team, probe)

func _structure_source_registry(manifest: Dictionary) -> Dictionary:
	return _script_world_subsystem()._structure_source_registry(manifest)

func trainable_unit_type_for(team: int, object_type: String) -> String:
	## Resolve a retail object-type name (or an already-runtime unit id) to
	## the production-rule key `queue_unit` trains it by, or "" when this
	## team's production rules do not model it - the caller must refuse, not
	## guess a cost. Keys are visited sorted so a (mis-)configured duplicate
	## resolves identically on every peer.
	var runtime_id := PlayableUnitAdapter.runtime_object_id(object_type)
	var keys: Array = unit_production_rules_for_team(team).keys()
	keys.sort()
	for key_value in keys:
		var key := String(key_value)
		if key == object_type or key == runtime_id:
			return key
	return ""


func unit_command_point_cost(unit_type: String) -> int:
	return _production_subsystem().unit_command_point_cost(unit_type)


func expansion_kind_for_object_id(object_id: String) -> String:
	## The expansion kind whose build rule declares `object_id`, or "" when no
	## configured rule does (the caller must refuse an unmodeled type rather
	## than answer for it). Kinds are visited in sorted order so a (mis-)
	## configured duplicate object id resolves identically on every peer.
	var kinds := _expansion_build_rules.keys()
	kinds.sort()
	for kind_value in kinds:
		if String((_expansion_build_rules[kind_value] as Dictionary).get("object_id", "")) == object_id:
			return String(kind_value)
	return ""


func producer_queue_limit(producer_row: Dictionary) -> int:
	return _production_subsystem().producer_queue_limit(producer_row)


func queue_unit(team: int, producer: int, unit_type: String = SOLDIER_HORDE_ID) -> Dictionary:
	return _production_subsystem().queue_unit(team, producer, unit_type)


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
	return _production_subsystem().production_queue_state(producer)


func cancel_queued_unit(team: int, producer: int, queue_index: int = 0) -> Dictionary:
	return _production_subsystem().cancel_queued_unit(team, producer, queue_index)


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


## State/tuning owned by the sim; logic lives in retail_sim_orders.gd
const FORMATION_MODIFIER_KEY := "formation"

const OrdersSystemScript = preload("res://src/retail_slice/retail_sim_orders.gd")
var _orders_system = null
func _orders_subsystem():
	if _orders_system == null:
		_orders_system = OrdersSystemScript.new(self)
	return _orders_system

func issue_move(ids: Array[int], destination: Vector2, ack_kind: String = "order.move", team: int = PLAYER_TEAM) -> int:
	return _orders_subsystem().issue_move(ids, destination, ack_kind, team)

func _apply_group_speed_cap(accepted_ids: Array[int]) -> void:
	_orders_subsystem()._apply_group_speed_cap(accepted_ids)

func issue_attack(ids: Array[int], target_id: int, team: int = PLAYER_TEAM) -> int:
	return _orders_subsystem().issue_attack(ids, target_id, team)

func issue_attack_move(ids: Array[int], destination: Vector2, team: int = PLAYER_TEAM) -> int:
	return _orders_subsystem().issue_attack_move(ids, destination, team)

func issue_stop(ids: Array[int], team: int = PLAYER_TEAM) -> int:
	return _orders_subsystem().issue_stop(ids, team)

func issue_toggle_stance(ids: Array[int], team: int = PLAYER_TEAM) -> int:
	return _orders_subsystem().issue_toggle_stance(ids, team)

func issue_set_stance(ids: Array[int], stance: String, team: int = PLAYER_TEAM) -> int:
	return _orders_subsystem().issue_set_stance(ids, stance, team)

func _authored_formation_toggle(document: Dictionary) -> Dictionary:
	return _orders_subsystem()._authored_formation_toggle(document)

func horde_formation_toggle(row: Dictionary) -> Dictionary:
	return _orders_subsystem().horde_formation_toggle(row)

func _formation_order_admitted(row: Dictionary) -> bool:
	return _orders_subsystem()._formation_order_admitted(row)

func issue_toggle_formation(ids: Array[int], team: int = PLAYER_TEAM) -> int:
	return _orders_subsystem().issue_toggle_formation(ids, team)

func issue_set_formation(ids: Array[int], formation: String, team: int = PLAYER_TEAM) -> int:
	return _orders_subsystem().issue_set_formation(ids, formation, team)

func _apply_formation_attribute_modifier(row: Dictionary) -> void:
	_orders_subsystem()._apply_formation_attribute_modifier(row)

func _apply_formation_mode(row: Dictionary) -> void:
	_orders_subsystem()._apply_formation_mode(row)

func advance(ticks: int) -> void:
	for _index in range(maxi(0, ticks)):
		tick()


func tick() -> void:
	if clock_paused and not _has_pending_resume_command():
		# Preserve the single-player clock-stop seam. Lockstep advances paused
		# command ticks only after a deterministic resume has been scheduled.
		return
	tick_index += 1
	# Refile every battalion before anything queries. This is the index's
	# self-healing seam: it absorbs whatever happened between ticks (restore,
	# corpse cleanup, direct roster edits) so no query can read stale cells.
	# Inside the tick the index is kept live by _spatial_sync() at each position
	# write, because acquisition must observe moves made earlier in the same tick.
	_spatial_rebuild()
	_apply_pending_commands_for_tick(tick_index)
	if clock_paused:
		# A lockstep pause still consumes deterministic command ticks so a
		# scheduled resume can execute. Gameplay below this seam remains frozen.
		return
	if winner != -1:
		_step_slow_death_core()
		_cleanup_expired_corpses()
		return
	# THE SCRIPT SEAM: registered script executors step exactly here - once
	# per gameplay-advancing tick, after this tick's commands, before any
	# gameplay subsystem, frozen by the same pause/winner gates as gameplay.
	# The full ordering contract and its enforcement live at
	# _step_script_executors(); with nothing registered this is a no-op and
	# the tick is byte-identical to the pre-wiring engine (the b177804c pin).
	_step_script_executors()
	_step_ring_mechanic()
	# Script time-freeze freezes GAMEPLAY only AFTER scripts step, so a script
	# that freezes time can still run its unfreeze action on a later script step.
	_ensure_parity()
	if bool(parity.time_frozen):
		_step_slow_death_core()
		_cleanup_expired_corpses()
		return
	# FoW consumer: each living unit reveals fog cells for its owner by vision.
	_step_fog_from_vision()
	# The retail shroud grid, off unless the match authored `enable_fog_of_war`.
	# Placed AFTER the legacy pass and touching none of its state, so a fog-off
	# match is byte-identical below this line and the 3000-tick pin cannot move.
	_step_shroud_grid()
	if base_loop_enabled:
		_step_economy()
		_step_structure_upgrades()
		_step_battalion_upgrades()
		_step_production()
	_step_pending_power_effects()
	_step_physics_objects()
	_step_projectiles()
	_step_scenario_bezier_projectiles()
	_step_ship_runtime()
	_step_lifetime_updates()
	_step_attribute_modifier_auras()
	_step_large_group_bonus_updates()
	_step_hit_reactions()
	_step_animal_ai_updates()
	_step_threat_finders()
	_step_radiate_fear_updates()
	_step_poisoned_behaviors()
	_step_damage_fields()
	_step_spawn_unit_behaviors()
	_step_flammable_updates()
	_step_fire_spread_updates()
	_step_passive_area_effect_heals()
	_step_passive_area_effect_modifiers()
	_step_grove_auras()
	_step_field_pings()
	_step_weather_effects()
	_step_summon_despawns()
	_step_summon_auras()
	_step_pickup_stuff_updates()
	_step_auto_abilities()
	_step_ai_special_power_updates()
	_step_respawn_updates()
	_step_fire_weapon_updates()
	_step_deletion_updates()
	_step_give_upgrade_updates()
	_step_gate_updates()
	_step_dynamic_portals()
	_step_monitor_condition_updates()
	_step_special_enemy_sense_updates()
	_step_invisibility_updates()
	_step_stealth_detectors()
	_step_slaved_updates()
	_step_spawn_behaviors()
	_step_rebuild_holes()
	_step_attach_updates()
	_step_stealth_updates()
	_step_object_creation_upgrades()
	_step_replace_self_upgrades()
	_step_attribute_modifier_upgrades()
	_step_geometry_upgrades()
	_step_emotion_trackers()
	_step_ocl_updates()
	_step_structure_weapons()
	if ai_enabled and tick_index % AI_CONTROLLER_BASE_INTERVAL == 0:
		_update_ai_controllers()
	for id in entity_ids():
		_step_entity(id)
	_step_active_pickup_collisions()
	_step_banner_carriers()
	_step_structure_eviction()
	_step_battalion_separation()
	_step_construction()
	_step_auto_heal_updates()
	_step_hero_regeneration()
	_step_hero_abilities()
	_step_slow_death_core()
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
## How far past the separation radius the neighbourhood query reaches, so that a
## battalion drifting under its own pushes stays inside the set it gathered. This
## is a performance margin, not a correctness one: exceeding it falls back to the
## full sweep (see _step_battalion_separation).
const BATTALION_SEPARATION_QUERY_SLACK := 16.0 * BATTALION_SEPARATION_PUSH



func _step_battalion_separation() -> void:
	_stepper_subsystem()._step_battalion_separation()

func _position_walkable(position: Vector2) -> bool:
	return _stepper_subsystem()._position_walkable(position)

func _step_hero_regeneration() -> void:
	_stepper_subsystem()._step_hero_regeneration()

func _attach_auto_heal_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_auto_heal_contract(row, contract)

func _step_auto_heal_updates() -> void:
	_contracts_subsystem()._step_auto_heal_updates()

func _apply_auto_heal_pulse(row: Dictionary, battalion: bool) -> void:
	_contracts_subsystem()._apply_auto_heal_pulse(row, battalion)

func _scaled_ability_rules(rules: Array[Dictionary], source_scale: float) -> Array[Dictionary]:
	return _contracts_subsystem()._scaled_ability_rules(rules, source_scale)

func _attach_hero_ability_state(row: Dictionary) -> void:
	_contracts_subsystem()._attach_hero_ability_state(row)

func _attach_module_contracts(row: Dictionary) -> void:
	_contracts_subsystem()._attach_module_contracts(row)


func module_contracts_for_unit_type(unit_type: String) -> Array:
	return _contracts_subsystem().module_contracts_for_unit_type(unit_type)


func register_unit_module_contracts(unit_type: String, contracts: Array) -> void:
	_contracts_subsystem().register_unit_module_contracts(unit_type, contracts)


func register_structure_module_contracts(key: String, contracts: Array) -> void:
	_contracts_subsystem().register_structure_module_contracts(key, contracts)


func _contracts_have_executable_refund_die(contracts: Array) -> bool:
	return _contracts_subsystem()._contracts_have_executable_refund_die(contracts)


func _stamp_refund_die_creation_cost(row: Dictionary, cost: int) -> void:
	_contracts_subsystem()._stamp_refund_die_creation_cost(row, cost)


func _configure_playable_structure_module_contracts() -> void:
	_contracts_subsystem()._configure_playable_structure_module_contracts()


func register_castle_upgrade_grants(source_object_id: String, contracts: Array) -> void:
	_contracts_subsystem().register_castle_upgrade_grants(source_object_id, contracts)


func _castle_upgrade_field(fields: Dictionary, key: String) -> String:
	return _contracts_subsystem()._castle_upgrade_field(fields, key)


func castle_upgrade_grants_for(trigger_upgrade_id: String) -> Array:
	return _contracts_subsystem().castle_upgrade_grants_for(trigger_upgrade_id)


func _apply_castle_upgrade_grants(building: Dictionary, trigger_upgrade_id: String) -> void:
	_contracts_subsystem()._apply_castle_upgrade_grants(building, trigger_upgrade_id)


func _content_db_ref():
	return _contracts_subsystem()._content_db_ref()


func _snapshot_scenario_runtime_tables() -> void:
	_contracts_subsystem()._snapshot_scenario_runtime_tables()


func scenario_spawn_contract(object_id: String, surface: String) -> Dictionary:
	return _contracts_subsystem().scenario_spawn_contract(object_id, surface)


func scenario_unit_rule(object_id: String, surface: String) -> Dictionary:
	return _contracts_subsystem().scenario_unit_rule(object_id, surface)


func spawn_scenario_unit(
	object_id: String, team: int, at: Vector2, surface: String, requested_id: int = -1
) -> int:
	return _contracts_subsystem().spawn_scenario_unit(object_id, team, at, surface, requested_id)


func spawn_scenario_structure(
	object_id: String, team: int, at: Vector2, surface: String, requested_id: int = -1
) -> int:
	return _contracts_subsystem().spawn_scenario_structure(object_id, team, at, surface, requested_id)


func spawn_scenario_prop(object_id: String, at: Vector2, surface: String) -> int:
	return _contracts_subsystem().spawn_scenario_prop(object_id, at, surface)


func launch_scenario_bezier_projectile(
	prop_id: int, target: Vector2, duration_ticks: int, bounce_duration_ticks: int = -1
) -> Dictionary:
	return _contracts_subsystem().launch_scenario_bezier_projectile(prop_id, target, duration_ticks, bounce_duration_ticks)


func sample_bezier_projectile_trajectory(
	trajectory: Dictionary, start: Vector2, target: Vector2, progress: float
) -> Vector3:
	return _contracts_subsystem().sample_bezier_projectile_trajectory(trajectory, start, target, progress)


func _step_scenario_bezier_projectiles() -> void:
	_contracts_subsystem()._step_scenario_bezier_projectiles()


func spawn_scenario_object(
	object_id: String, team: int, at: Vector2, surface: String
) -> Dictionary:
	return _contracts_subsystem().spawn_scenario_object(object_id, team, at, surface)


func _scenario_structure_instantiation_rule(document: Dictionary) -> Dictionary:
	return _contracts_subsystem()._scenario_structure_instantiation_rule(document)


func _scenario_prop_is_passive(document: Dictionary) -> bool:
	return _contracts_subsystem()._scenario_prop_is_passive(document)


func spawn_scenario_pickup(object_id: String, at: Vector2, surface: String) -> int:
	return _contracts_subsystem().spawn_scenario_pickup(object_id, at, surface)


func _salvage_oracle_receipt_valid(receipt: Dictionary) -> bool:
	return _contracts_subsystem()._salvage_oracle_receipt_valid(receipt)


func collect_salvage_crate(pickup_id: int, picker_id: int) -> Dictionary:
	return _contracts_subsystem().collect_salvage_crate(pickup_id, picker_id)


func _step_active_pickup_collisions() -> void:
	_contracts_subsystem()._step_active_pickup_collisions()


func _grant_one_authored_rank(row: Dictionary) -> void:
	_contracts_subsystem()._grant_one_authored_rank(row)


func _scenario_document_for_kind(kind: String, object_id: String, surface: String) -> Dictionary:
	return _contracts_subsystem()._scenario_document_for_kind(kind, object_id, surface)


func _casefolded_scenario_document(registry: Dictionary, object_id: String) -> Dictionary:
	return _contracts_subsystem()._casefolded_scenario_document(registry, object_id)


func _scenario_runtime_tables_present() -> bool:
	return _contracts_subsystem()._scenario_runtime_tables_present()


func _scenario_document_admits(kind: String, document: Dictionary, surface: String) -> bool:
	return _contracts_subsystem()._scenario_document_admits(kind, document, surface)


func _attach_structure_module_contracts(row: Dictionary) -> void:
	_contracts_subsystem()._attach_structure_module_contracts(row)


func _module_contract_value(fields: Dictionary, key: String, fallback: Variant = null) -> Variant:
	return _contracts_subsystem()._module_contract_value(fields, key, fallback)


func _typed_contract_tokens(fields: Dictionary, key: String) -> Array[String]:
	return _contracts_subsystem()._typed_contract_tokens(fields, key)


func _typed_contract_raw_tokens(fields: Dictionary, key: String) -> Array[String]:
	return _contracts_subsystem()._typed_contract_raw_tokens(fields, key)


func _attach_buildable_hero_list_upgrade_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_buildable_hero_list_upgrade_contract(row, contract)


func _attach_allow_banner_spawn_upgrade_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_allow_banner_spawn_upgrade_contract(row, contract)


func structure_allows_banner_spawn(structure_id: int) -> bool:
	return _contracts_subsystem().structure_allows_banner_spawn(structure_id)


func _attach_spell_recharge_modifier_upgrade_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_spell_recharge_modifier_upgrade_contract(row, contract)


func spell_recharge_ticks_for_team(team: int, base_ticks: int) -> int:
	return _contracts_subsystem().spell_recharge_ticks_for_team(team, base_ticks)


func _ship_contract_delay_ticks(milliseconds: float) -> int:
	return _contracts_subsystem()._ship_contract_delay_ticks(milliseconds)


func _attach_horde_contain_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_horde_contain_contract(row, contract)


func _resolve_horde_payload_count(payload: Dictionary, defines: Dictionary) -> Dictionary:
	return _contracts_subsystem()._resolve_horde_payload_count(payload, defines)


func admit_horde_member(horde_id: int, object_id: String, kind_of: Array, rank: int = 0, health: int = 1) -> Dictionary:
	return _contracts_subsystem().admit_horde_member(horde_id, object_id, kind_of, rank, health)


func eject_horde_member(horde_id: int, member_index: int) -> Dictionary:
	return _contracts_subsystem().eject_horde_member(horde_id, member_index)


func apply_horde_contained_damage(horde_id: int, member_index: int, amount: int, death_type: String = "NORMAL") -> Dictionary:
	return _contracts_subsystem().apply_horde_contained_damage(horde_id, member_index, amount, death_type)


func horde_amoeba_melee_reach(horde_id: int, target_position: Vector2, target_is_building: bool = false) -> Dictionary:
	return _contracts_subsystem().horde_amoeba_melee_reach(horde_id, target_position, target_is_building)


func _attach_ai_update_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_ai_update_contract(row, contract)


func _attach_horde_ai_update_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_horde_ai_update_contract(row, contract)


func trigger_horde_cower(entity_id: int) -> Dictionary:
	return _contracts_subsystem().trigger_horde_cower(entity_id)


func _attach_pickup_stuff_update_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_abilities_subsystem()._attach_pickup_stuff_update_contract(row, contract)


func register_pickup_object(kind_of: Array, position: Vector2, object_id: String = "") -> int:
	return _contracts_abilities_subsystem().register_pickup_object(kind_of, position, object_id)


func remove_pickup_object(pickup_id: int) -> void:
	_contracts_abilities_subsystem().remove_pickup_object(pickup_id)


func _step_pickup_stuff_updates() -> void:
	_contracts_abilities_subsystem()._step_pickup_stuff_updates()


func _nearest_pickup_for(row: Dictionary, policy: Dictionary) -> int:
	return _contracts_abilities_subsystem()._nearest_pickup_for(row, policy)


func _attach_auto_ability_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_abilities_subsystem()._attach_auto_ability_contract(row, contract)


func _resolve_auto_ability_range(value: Variant) -> float:
	return _contracts_abilities_subsystem()._resolve_auto_ability_range(value)


func set_auto_ability_active(entity_id: int, special_ability: String, active: bool) -> Dictionary:
	return _contracts_abilities_subsystem().set_auto_ability_active(entity_id, special_ability, active)


func _step_auto_abilities() -> void:
	_contracts_abilities_subsystem()._step_auto_abilities()


func _ability_id_for_special_power(row: Dictionary, special_power: String) -> String:
	return _contracts_abilities_subsystem()._ability_id_for_special_power(row, special_power)


func _auto_ability_query_target(source: Dictionary, behavior: Dictionary, minimum: float, maximum: float) -> int:
	return _contracts_abilities_subsystem()._auto_ability_query_target(source, behavior, minimum, maximum)


func _auto_ability_filter_accepts(source: Dictionary, candidate: Dictionary, tokens: Array) -> bool:
	return _contracts_abilities_subsystem()._auto_ability_filter_accepts(source, candidate, tokens)


func _attach_ai_special_power_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_abilities_subsystem()._attach_ai_special_power_contract(row, contract)


func _resolve_ai_special_power_expression(value: Variant) -> Dictionary:
	return _contracts_abilities_subsystem()._resolve_ai_special_power_expression(value)


func _ai_special_power_type_supported(ai_type: String) -> bool:
	return _contracts_abilities_subsystem()._ai_special_power_type_supported(ai_type)


func _step_ai_special_power_updates() -> void:
	_contracts_abilities_subsystem()._step_ai_special_power_updates()


func _ability_auto_blocked_model_condition(row: Dictionary, rule: Dictionary) -> String:
	return _contracts_abilities_subsystem()._ability_auto_blocked_model_condition(row, rule)


func _ai_special_power_cast(entity_id: int, row: Dictionary, policy: Dictionary, point: Vector2) -> Dictionary:
	return _contracts_abilities_subsystem()._ai_special_power_cast(entity_id, row, policy, point)


func _ai_special_power_stance(ai_type: String) -> String:
	return _contracts_abilities_subsystem()._ai_special_power_stance(ai_type)


func _ai_special_power_desired_stance(row: Dictionary) -> String:
	return _contracts_abilities_subsystem()._ai_special_power_desired_stance(row)


func _ai_special_power_target(source: Dictionary, policy: Dictionary) -> Dictionary:
	return _contracts_abilities_subsystem()._ai_special_power_target(source, policy)


func _ai_special_power_ability_rule(source: Dictionary, command: String) -> Dictionary:
	return _contracts_abilities_subsystem()._ai_special_power_ability_rule(source, command)


func _ai_special_power_is_self(ai_type: String) -> bool:
	return _contracts_abilities_subsystem()._ai_special_power_is_self(ai_type)


func _ai_special_power_targets_allies(ai_type: String) -> bool:
	return _contracts_abilities_subsystem()._ai_special_power_targets_allies(ai_type)


func _ai_special_power_targets_structure(ai_type: String) -> bool:
	return _contracts_abilities_subsystem()._ai_special_power_targets_structure(ai_type)


func _ai_special_power_cluster_score(source: Dictionary, point: Vector2, radius: float, allies: bool) -> int:
	return _contracts_abilities_subsystem()._ai_special_power_cluster_score(source, point, radius, allies)


func _ai_special_power_structure_site_clear(point: Vector2, radius: float) -> bool:
	return _contracts_abilities_subsystem()._ai_special_power_structure_site_clear(point, radius)


func _attach_respawn_update_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_abilities_subsystem()._attach_respawn_update_contract(row, contract)


func _schedule_respawn_update(entity_id: int, row: Dictionary, death_type:String="NORMAL", attacker_id:int=0) -> void:
	_contracts_abilities_subsystem()._schedule_respawn_update(entity_id, row, death_type, attacker_id)


func request_respawn(entity_id: int) -> Dictionary:
	return _contracts_abilities_subsystem().request_respawn(entity_id)


func _step_respawn_updates() -> void:
	_contracts_abilities_subsystem()._step_respawn_updates()


func _respawn_anchor(team: int, filter: Array) -> int:
	return _contracts_abilities_subsystem()._respawn_anchor(team, filter)


func _attach_fire_weapon_update_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_abilities_subsystem()._attach_fire_weapon_update_contract(row, contract)


func _step_fire_weapon_updates() -> void:
	_contracts_abilities_subsystem()._step_fire_weapon_updates()


func _attach_deletion_update_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_abilities_subsystem()._attach_deletion_update_contract(row, contract)


func _resolve_deletion_bound(value: Variant) -> Dictionary:
	return _contracts_abilities_subsystem()._resolve_deletion_bound(value)


func _step_deletion_updates() -> void:
	_contracts_abilities_subsystem()._step_deletion_updates()


func _attach_production_update_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_abilities_subsystem()._attach_production_update_contract(row, contract)


func _attach_getting_built_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_abilities_subsystem()._attach_getting_built_contract(row, contract)


func _resolve_build_seconds(value:Variant)->Dictionary:
	return _contracts_abilities_subsystem()._resolve_build_seconds(value)


func _attach_building_behavior_contract(row:Dictionary,contract:Dictionary)->void:
	_contracts_abilities_subsystem()._attach_building_behavior_contract(row, contract)


func _attach_queue_production_exit_contract(row:Dictionary,contract:Dictionary)->void:
	_contracts_abilities_subsystem()._attach_queue_production_exit_contract(row, contract)


func _attach_rebuild_hole_expose_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_abilities_subsystem()._attach_rebuild_hole_expose_contract(row, contract)


func _attach_rebuild_hole_behavior_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_abilities_subsystem()._attach_rebuild_hole_behavior_contract(row, contract)


func _expose_rebuild_hole(owner_id: int, owner: Dictionary, attacker_id: int) -> int:
	return _contracts_abilities_subsystem()._expose_rebuild_hole(owner_id, owner, attacker_id)


func _step_rebuild_holes() -> void:
	_contracts_abilities_subsystem()._step_rebuild_holes()


func _typed_contract_numbers(fields:Dictionary,key:String)->Array[float]:
	return _contracts_abilities_subsystem()._typed_contract_numbers(fields, key)


func _attach_banner_carrier_update_contract(row:Dictionary,contract:Dictionary)->void:
	_contracts_abilities_subsystem()._attach_banner_carrier_update_contract(row, contract)


func _resolve_respawn_body_expression(field:Variant)->Dictionary:
	return _contracts_abilities_subsystem()._resolve_respawn_body_expression(field)


func _attach_respawn_body_contract(row:Dictionary,contract:Dictionary)->void:
	_contracts_abilities_subsystem()._attach_respawn_body_contract(row, contract)


func _resolve_contract_milliseconds(field:Variant,define_key:String)->Dictionary:
	return _contracts_abilities_subsystem()._resolve_contract_milliseconds(field, define_key)


func _attach_give_upgrade_contract(row:Dictionary,contract:Dictionary)->void:
	_contracts_abilities_subsystem()._attach_give_upgrade_contract(row, contract)


func request_give_upgrade(source_id:int,target_id:int,upgrade_id:String,special_power:String="")->Dictionary:
	return _contracts_abilities_subsystem().request_give_upgrade(source_id, target_id, upgrade_id, special_power)


func _step_give_upgrade_updates()->void:
	_contracts_abilities_subsystem()._step_give_upgrade_updates()


func _attach_gate_open_close_contract(row:Dictionary,contract:Dictionary)->void:
	_contracts_abilities_subsystem()._attach_gate_open_close_contract(row, contract)


func _attach_ai_gate_contract(row:Dictionary,contract:Dictionary)->void:
	_contracts_abilities_subsystem()._attach_ai_gate_contract(row, contract)


func _attach_fake_pathfind_portal_contract(row:Dictionary,contract:Dictionary)->void:
	_contracts_abilities_subsystem()._attach_fake_pathfind_portal_contract(row, contract)


func request_gate_open(structure_id:int,requester_id:int=0)->Dictionary:
	return _contracts_abilities_subsystem().request_gate_open(structure_id, requester_id)


func _sync_gate_passage(structure_id: int) -> void:
	_contracts_abilities_subsystem()._sync_gate_passage(structure_id)


func gate_portal_allows(structure_id:int,requester_id:int)->bool:
	return _contracts_abilities_subsystem().gate_portal_allows(structure_id, requester_id)


func _step_gate_updates()->void:
	_contracts_abilities_subsystem()._step_gate_updates()


func _typed_effect_graph(contract: Dictionary, kind: String, target_mode: String) -> Dictionary:
	return _contracts_abilities_subsystem()._typed_effect_graph(contract, kind, target_mode)


func _attach_stop_special_power_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_abilities_subsystem()._attach_stop_special_power_contract(row, contract)


func _active_special_power_channel_key(row: Dictionary, target_template: String) -> String:
	return _contracts_abilities_subsystem()._active_special_power_channel_key(row, target_template)


func activate_stop_special_power(entity_id: int, special_power_template: String, team: int = -1) -> Dictionary:
	return _contracts_abilities_subsystem().activate_stop_special_power(entity_id, special_power_template, team)


func _attach_unleash_special_power_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_abilities_subsystem()._attach_unleash_special_power_contract(row, contract)


func _unleash_owned_slave(owner_id: int, owner: Dictionary, policy: Dictionary) -> int:
	return _contracts_abilities_subsystem()._unleash_owned_slave(owner_id, owner, policy)


func activate_unleash_special_power(owner_id: int, special_power_template: String, team: int = -1) -> Dictionary:
	return _contracts_abilities_subsystem().activate_unleash_special_power(owner_id, special_power_template, team)


func _attach_special_enemy_sense_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_abilities_subsystem()._attach_special_enemy_sense_contract(row, contract)


func _special_enemy_sense_filter_accepts(target: Dictionary, filter: Array) -> bool:
	return _contracts_abilities_subsystem()._special_enemy_sense_filter_accepts(target, filter)


func _step_special_enemy_sense_updates() -> void:
	_contracts_abilities_subsystem()._step_special_enemy_sense_updates()


func _resolve_invisibility_expression(field: Variant, key: String) -> Dictionary:
	return _contracts_abilities_subsystem()._resolve_invisibility_expression(field, key)


func _attach_invisibility_update_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_abilities_subsystem()._attach_invisibility_update_contract(row, contract)


func set_invisibility_update_active(object_id: int, enabled: bool, tag: String = "") -> Dictionary:
	return _contracts_abilities_subsystem().set_invisibility_update_active(object_id, enabled, tag)


func _invisibility_upgrade_gate(row: Dictionary, policy: Dictionary) -> bool:
	return _contracts_abilities_subsystem()._invisibility_upgrade_gate(row, policy)


func _invisibility_condition_set(row: Dictionary) -> Dictionary:
	return _contracts_abilities_subsystem()._invisibility_condition_set(row)


func _invisibility_source_key(object_id: int, policy: Dictionary, prefix: String = "module") -> String:
	return _contracts_abilities_subsystem()._invisibility_source_key(object_id, policy, prefix)


func _invisibility_source_active(row: Dictionary, source_key: String) -> bool:
	return _contracts_abilities_subsystem()._invisibility_source_active(row, source_key)


func _set_invisibility_source(target: Dictionary, source_key: String, policy: Dictionary, enabled: bool, source_id: int) -> void:
	_contracts_abilities_subsystem()._set_invisibility_source(target, source_key, policy, enabled, source_id)


func _revoke_invisibility_policy_sources(object_id: int, row: Dictionary, policy: Dictionary) -> void:
	_contracts_abilities_subsystem()._revoke_invisibility_policy_sources(object_id, row, policy)


func _step_invisibility_updates() -> void:
	_contracts_abilities_subsystem()._step_invisibility_updates()


func _step_invisibility_broadcast(source_id: int, source: Dictionary, policy: Dictionary, blocked: bool) -> void:
	_contracts_abilities_subsystem()._step_invisibility_broadcast(source_id, source, policy, blocked)


func _attach_stealth_detector_contract(row:Dictionary,contract:Dictionary)->void:
	_contracts_abilities_subsystem()._attach_stealth_detector_contract(row, contract)


func _step_stealth_detectors()->void:
	_contracts_abilities_subsystem()._step_stealth_detectors()


func _attach_slaved_update_contract(row:Dictionary,contract:Dictionary)->void:
	_contracts_abilities_subsystem()._attach_slaved_update_contract(row, contract)


func bind_slave(slave_id:int,master_id:int)->Dictionary:
	return _contracts_abilities_subsystem().bind_slave(slave_id, master_id)


func _step_slaved_updates()->void:
	_contracts_abilities_subsystem()._step_slaved_updates()


func _kill_slave_for_master_death(slave_id:int,slave:Dictionary)->void:
	_contracts_abilities_subsystem()._kill_slave_for_master_death(slave_id, slave)


func _attach_castle_upgrade_contract(row:Dictionary,contract:Dictionary)->void:
	_contracts_abilities_subsystem()._attach_castle_upgrade_contract(row, contract)


func apply_castle_upgrade_trigger(structure_id:int,trigger_upgrade_id:String)->Dictionary:
	return _contracts_abilities_subsystem().apply_castle_upgrade_trigger(structure_id, trigger_upgrade_id)


func _attach_spawn_behavior_contract(row:Dictionary,contract:Dictionary)->void:
	_contracts_abilities_subsystem()._attach_spawn_behavior_contract(row, contract)


func _step_spawn_behaviors()->void:
	_contracts_abilities_subsystem()._step_spawn_behaviors()


func _spawn_behavior_member(owner_id:int,owner:Dictionary,policy:Dictionary)->int:
	return _contracts_abilities_subsystem()._spawn_behavior_member(owner_id, owner, policy)


func _spawn_behavior_reclaim_orphan(owner_id:int,owner:Dictionary,policy:Dictionary)->int:
	return _contracts_abilities_subsystem()._spawn_behavior_reclaim_orphan(owner_id, owner, policy)


func _spawn_behavior_closest_orphan(owner:Dictionary,template:String)->int:
	return _contracts_abilities_subsystem()._spawn_behavior_closest_orphan(owner, template)


func _spawn_behavior_template_equivalent(candidate:Dictionary,template:String)->bool:
	return _contracts_abilities_subsystem()._spawn_behavior_template_equivalent(candidate, template)


func _attach_stealth_update_contract(row:Dictionary,contract:Dictionary)->void:
	_contracts_abilities_subsystem()._attach_stealth_update_contract(row, contract)


func set_stealth_update_active(entity_id:int,enabled:bool)->Dictionary:
	return _contracts_abilities_subsystem().set_stealth_update_active(entity_id, enabled)


func _step_stealth_updates()->void:
	_contracts_abilities_subsystem()._step_stealth_updates()


func _attach_object_creation_upgrade_contract(row:Dictionary,contract:Dictionary)->void:
	_contracts_abilities_subsystem()._attach_object_creation_upgrade_contract(row, contract)


func _attach_attribute_modifier_upgrade_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_abilities_subsystem()._attach_attribute_modifier_upgrade_contract(row, contract)


func _attribute_modifier_upgrade_owned(row: Dictionary, upgrade_id: String) -> bool:
	return _contracts_abilities_subsystem()._attribute_modifier_upgrade_owned(row, upgrade_id)


func _attribute_modifier_upgrade_should_activate(row: Dictionary, policy: Dictionary) -> bool:
	return _contracts_abilities_subsystem()._attribute_modifier_upgrade_should_activate(row, policy)


func _attribute_modifier_upgrade_key(policy: Dictionary) -> String:
	return _contracts_abilities_subsystem()._attribute_modifier_upgrade_key(policy)


func _reconcile_attribute_modifier_upgrades(row: Dictionary) -> void:
	_contracts_abilities_subsystem()._reconcile_attribute_modifier_upgrades(row)


func _step_attribute_modifier_upgrades() -> void:
	_contracts_abilities_subsystem()._step_attribute_modifier_upgrades()


func _attach_geometry_upgrade_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_abilities_subsystem()._attach_geometry_upgrade_contract(row, contract)


func _geometry_upgrade_should_activate(row: Dictionary, policy: Dictionary) -> bool:
	return _contracts_abilities_subsystem()._geometry_upgrade_should_activate(row, policy)


func _reconcile_geometry_upgrades(row: Dictionary) -> void:
	_contracts_abilities_subsystem()._reconcile_geometry_upgrades(row)


func _step_geometry_upgrades() -> void:
	_contracts_abilities_subsystem()._step_geometry_upgrades()


func _emotion_expression_value(fields: Dictionary, key: String, unsupported: Array[String]) -> float:
	return _contracts_abilities_subsystem()._emotion_expression_value(fields, key, unsupported)


func _attach_emotion_tracker_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_abilities_subsystem()._attach_emotion_tracker_contract(row, contract)


func trigger_entity_emotion(entity_id: int, emotion_name: String, duration_ticks: int = -1) -> Dictionary:
	return _contracts_abilities_subsystem().trigger_entity_emotion(entity_id, emotion_name, duration_ticks)


func _step_emotion_trackers() -> void:
	_contracts_abilities_subsystem()._step_emotion_trackers()


func _attach_castle_member_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_behaviors_subsystem()._attach_castle_member_contract(row, contract)


func _dispatch_castle_member_destroyed(structure_id: int, member: Dictionary, attacker_id: int, reason: String) -> void:
	_contracts_behaviors_subsystem()._dispatch_castle_member_destroyed(structure_id, member, attacker_id, reason)


func _attach_inactive_body_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_behaviors_subsystem()._attach_inactive_body_contract(row, contract)


func _attach_squish_collide_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_behaviors_subsystem()._attach_squish_collide_contract(row, contract)


func _squish_collision_admitted(victim: Dictionary) -> bool:
	return _contracts_behaviors_subsystem()._squish_collision_admitted(victim)


func _attach_horde_member_collide_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_behaviors_subsystem()._attach_horde_member_collide_contract(row, contract)


func _attach_notify_crushing_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_behaviors_subsystem()._attach_notify_crushing_contract(row, contract)


func _attach_flammable_update_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_behaviors_subsystem()._attach_flammable_update_contract(row, contract)


func record_flame_damage(object_id: int, amount: float) -> Dictionary:
	return _contracts_behaviors_subsystem().record_flame_damage(object_id, amount)


func _step_flammable_updates() -> void:
	_contracts_behaviors_subsystem()._step_flammable_updates()


func _set_row_object_status(row: Dictionary, status: String, enabled: bool) -> void:
	_contracts_behaviors_subsystem()._set_row_object_status(row, status, enabled)


func _attach_dynamic_portal_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_behaviors_subsystem()._attach_dynamic_portal_contract(row, contract)


func _step_dynamic_portals() -> void:
	_contracts_behaviors_subsystem()._step_dynamic_portals()


func request_dynamic_portal_route(portal_id: int, entity_id: int, from_index: int, to_index: int) -> Dictionary:
	return _contracts_behaviors_subsystem().request_dynamic_portal_route(portal_id, entity_id, from_index, to_index)


func _attach_foundation_ai_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_behaviors_subsystem()._attach_foundation_ai_contract(row, contract)


func foundation_build_variation(structure_id: int) -> Dictionary:
	return _contracts_behaviors_subsystem().foundation_build_variation(structure_id)


func _attach_dual_weapon_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_behaviors_subsystem()._attach_dual_weapon_contract(row, contract)


func _attach_refund_die_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_behaviors_subsystem()._attach_refund_die_contract(row, contract)


func _cache_refund_die_build_cost(row: Dictionary) -> void:
	_contracts_behaviors_subsystem()._cache_refund_die_build_cost(row)


func _attach_wall_hub_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_behaviors_subsystem()._attach_wall_hub_contract(row, contract)


func _wall_hub_distance(field: Dictionary, defines: Dictionary) -> Dictionary:
	return _contracts_behaviors_subsystem()._wall_hub_distance(field, defines)


func request_wall_hub_plan(structure_id: int, option: String, endpoint: Vector2) -> Dictionary:
	return _contracts_behaviors_subsystem().request_wall_hub_plan(structure_id, option, endpoint)


func _attach_attach_update_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_behaviors_subsystem()._attach_attach_update_contract(row, contract)


func request_attach_update(child_id: int, parent_id: int, parent_kind: String = "entity") -> Dictionary:
	return _contracts_behaviors_subsystem().request_attach_update(child_id, parent_id, parent_kind)


func _step_attach_updates() -> void:
	_contracts_behaviors_subsystem()._step_attach_updates()


func _detach_attach_update(child: Dictionary, reason: String) -> void:
	_contracts_behaviors_subsystem()._detach_attach_update(child, reason)


func _attach_filter_probe(row: Dictionary, kind: String) -> Dictionary:
	return _contracts_behaviors_subsystem()._attach_filter_probe(row, kind)


func _set_attach_parent_status(parent: Dictionary, child_id: int, statuses: Array, enabled: bool) -> void:
	_contracts_behaviors_subsystem()._set_attach_parent_status(parent, child_id, statuses, enabled)


func _attach_monitor_condition_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_behaviors_subsystem()._attach_monitor_condition_contract(row, contract)


func _monitor_condition_route(value: Variant) -> Dictionary:
	return _contracts_behaviors_subsystem()._monitor_condition_route(value)


func _step_monitor_condition_updates() -> void:
	_contracts_behaviors_subsystem()._step_monitor_condition_updates()


func _step_monitor_condition_row(object_id: int, row: Dictionary) -> void:
	_contracts_behaviors_subsystem()._step_monitor_condition_row(object_id, row)


func _upper_token_set(value: Variant) -> Dictionary:
	return _contracts_behaviors_subsystem()._upper_token_set(value)


func _monitor_flags_match(active: Dictionary, required: Array) -> bool:
	return _contracts_behaviors_subsystem()._monitor_flags_match(active, required)


func set_entity_upgrade_state(entity_id: int, upgrade_id: String, installed: bool) -> Dictionary:
	return _contracts_behaviors_subsystem().set_entity_upgrade_state(entity_id, upgrade_id, installed)


func set_team_upgrade_state(team: int, upgrade_id: String, installed: bool) -> Dictionary:
	return _contracts_behaviors_subsystem().set_team_upgrade_state(team, upgrade_id, installed)


func _step_object_creation_upgrades()->void:
	_contracts_behaviors_subsystem()._step_object_creation_upgrades()


func _consume_object_creation_upgrade(owner_id:int,owner:Dictionary,policy:Dictionary)->void:
	_contracts_behaviors_subsystem()._consume_object_creation_upgrade(owner_id, owner, policy)


func _attach_replace_self_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_behaviors_subsystem()._attach_replace_self_contract(row, contract)


func apply_replace_self_upgrade(structure_id: int, trigger_upgrade_id: String) -> Dictionary:
	return _contracts_behaviors_subsystem().apply_replace_self_upgrade(structure_id, trigger_upgrade_id)


func _step_replace_self_upgrades() -> void:
	_contracts_behaviors_subsystem()._step_replace_self_upgrades()


func _apply_due_replace_self_policy(structure_id: int, row: Dictionary, trigger_upgrade_id: String) -> Dictionary:
	return _contracts_behaviors_subsystem()._apply_due_replace_self_policy(structure_id, row, trigger_upgrade_id)


func _replace_self_structure_spec(team: int, object_id: String) -> Dictionary:
	return _contracts_behaviors_subsystem()._replace_self_structure_spec(team, object_id)


func _playable_structure_runtime_document(object_id: String) -> Dictionary:
	return _contracts_behaviors_subsystem()._playable_structure_runtime_document(object_id)


func _structure_attack_from_combat(combat: Dictionary) -> Dictionary:
	return _contracts_behaviors_subsystem()._structure_attack_from_combat(combat)


func _replacement_structure_row(structure_id: int, previous: Dictionary, object_id: String, spec: Dictionary, preserve_state: bool) -> Dictionary:
	return _contracts_behaviors_subsystem()._replacement_structure_row(structure_id, previous, object_id, spec, preserve_state)


func _reconcile_replacement_containment(structure_id: int, replacement: Dictionary) -> void:
	_contracts_behaviors_subsystem()._reconcile_replacement_containment(structure_id, replacement)


func _replace_self_receipt(policy: Dictionary, receipt: String) -> void:
	_contracts_behaviors_subsystem()._replace_self_receipt(policy, receipt)


func _attach_citadel_slaughter_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_behaviors_subsystem()._attach_citadel_slaughter_contract(row, contract)


func enter_citadel_slaughter(structure_id: int, entity_id: int) -> Dictionary:
	return _contracts_behaviors_subsystem().enter_citadel_slaughter(structure_id, entity_id)


func _consume_citadel_ring_entry(structure_id: int, entity_id: int, citadel: Dictionary, passenger: Dictionary, policy: Dictionary) -> Dictionary:
	return _contracts_behaviors_subsystem()._consume_citadel_ring_entry(structure_id, entity_id, citadel, passenger, policy)


func _resolve_citadel_slaughter_death(structure_id: int, citadel: Dictionary) -> void:
	_contracts_behaviors_subsystem()._resolve_citadel_slaughter_death(structure_id, citadel)


func _attach_ocl_update_contract(row:Dictionary,contract:Dictionary)->void:
	_contracts_behaviors_subsystem()._attach_ocl_update_contract(row, contract)


func _step_ocl_updates()->void:
	_contracts_behaviors_subsystem()._step_ocl_updates()


func _attach_stances_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_behaviors_subsystem()._attach_stances_contract(row, contract)


func set_entity_stance(entity_id: int, stance: String) -> Dictionary:
	return _contracts_behaviors_subsystem().set_entity_stance(entity_id, stance)


func toggle_entity_stance(entity_id: int) -> Dictionary:
	return _contracts_behaviors_subsystem().toggle_entity_stance(entity_id)


func _normalize_stance_name(stance: String) -> String:
	return _contracts_behaviors_subsystem()._normalize_stance_name(stance)


func _apply_stance_modifier(row: Dictionary) -> void:
	_contracts_behaviors_subsystem()._apply_stance_modifier(row)


func _attach_attribute_modifier_aura_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_behaviors_subsystem()._attach_attribute_modifier_aura_contract(row, contract)


func _resolve_lifetime_bound(field: Variant) -> Dictionary:
	return _contracts_behaviors_subsystem()._resolve_lifetime_bound(field)


func _attach_lifetime_update_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_behaviors_subsystem()._attach_lifetime_update_contract(row, contract)


func _arm_lifetime(row: Dictionary) -> bool:
	return _contracts_behaviors_subsystem()._arm_lifetime(row)


func wake_lifetime(object_id: int, object_kind: String = "entity") -> Dictionary:
	return _contracts_behaviors_subsystem().wake_lifetime(object_id, object_kind)


func _step_lifetime_updates() -> void:
	_contracts_behaviors_subsystem()._step_lifetime_updates()


func _expire_lifetime_entity(entity_id: int, row: Dictionary, death_type: String) -> void:
	_contracts_behaviors_subsystem()._expire_lifetime_entity(entity_id, row, death_type)


func _expire_lifetime_structure(structure_id: int, row: Dictionary, death_type: String) -> void:
	_contracts_behaviors_subsystem()._expire_lifetime_structure(structure_id, row, death_type)


func _step_attribute_modifier_auras() -> void:
	_contracts_behaviors_subsystem()._step_attribute_modifier_auras()


func _step_attribute_modifier_aura_source(source: Dictionary) -> void:
	_contracts_behaviors_subsystem()._step_attribute_modifier_aura_source(source)


func _aura_source_active(source: Dictionary, rule: Dictionary) -> bool:
	return _contracts_behaviors_subsystem()._aura_source_active(source, rule)


func _aura_has_upgrade(upgrades: Array, applied: Dictionary, sought: String) -> bool:
	return _contracts_behaviors_subsystem()._aura_has_upgrade(upgrades, applied, sought)


func _apply_typed_aura_to_target(source_id: int, target: Dictionary, rule: Dictionary, modifier: Dictionary, refresh_ticks: int) -> void:
	_contracts_behaviors_subsystem()._apply_typed_aura_to_target(source_id, target, rule, modifier, refresh_ticks)


func _attach_model_condition_sound_selector(row: Dictionary, contract: Dictionary) -> void:
	_contracts_behaviors_subsystem()._attach_model_condition_sound_selector(row, contract)


func _attach_random_sound_selector(row: Dictionary, contract: Dictionary) -> void:
	_contracts_behaviors_subsystem()._attach_random_sound_selector(row, contract)


func _attach_upgrade_sound_selector(row: Dictionary, contract: Dictionary) -> void:
	_contracts_behaviors_subsystem()._attach_upgrade_sound_selector(row, contract)


func _attach_large_group_audio_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_behaviors_subsystem()._attach_large_group_audio_contract(row, contract)


func emit_typed_audio_intent(entity_id: int, sound_role: String) -> Dictionary:
	return _contracts_behaviors_subsystem().emit_typed_audio_intent(entity_id, sound_role)


func emit_large_group_audio_intent(entity_ids_value: Array, sound_role: String) -> Dictionary:
	return _contracts_behaviors_subsystem().emit_large_group_audio_intent(entity_ids_value, sound_role)


func _audio_sequence_roll(entity_id: int, role: String) -> float:
	return _contracts_behaviors_subsystem()._audio_sequence_roll(entity_id, role)


func _attach_fire_spread_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_behaviors_subsystem()._attach_fire_spread_contract(row, contract)


func set_fire_spread_active(object_id: int, active: bool) -> Dictionary:
	return _contracts_behaviors_subsystem().set_fire_spread_active(object_id, active)


func _fire_spread_delay(object_id: int, policy: Dictionary) -> int:
	return _contracts_behaviors_subsystem()._fire_spread_delay(object_id, policy)


func _step_fire_spread_updates() -> void:
	_contracts_behaviors_subsystem()._step_fire_spread_updates()


func _audio_active_conditions(row: Dictionary) -> Dictionary:
	return _contracts_behaviors_subsystem()._audio_active_conditions(row)


func _audio_sound_field(role: String) -> String:
	return _contracts_behaviors_subsystem()._audio_sound_field(role)


func _attach_radiate_fear_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_behaviors_subsystem()._attach_radiate_fear_contract(row, contract)


func activate_radiate_fear(entity_id: int, special_power: int) -> Dictionary:
	return _contracts_behaviors_subsystem().activate_radiate_fear(entity_id, special_power)


func _step_radiate_fear_updates() -> void:
	_contracts_behaviors_subsystem()._step_radiate_fear_updates()


func _fear_victim_filter_accepts(target: Dictionary, tokens: Array) -> bool:
	return _contracts_behaviors_subsystem()._fear_victim_filter_accepts(target, tokens)


func _attach_poisoned_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_behaviors_subsystem()._attach_poisoned_contract(row, contract)


func apply_poison(entity_id: int, damage_per_pulse: float) -> Dictionary:
	return _contracts_behaviors_subsystem().apply_poison(entity_id, damage_per_pulse)


func _step_poisoned_behaviors() -> void:
	_contracts_behaviors_subsystem()._step_poisoned_behaviors()


func _attach_damage_field_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_behaviors_subsystem()._attach_damage_field_contract(row, contract)


func _step_damage_fields() -> void:
	_contracts_behaviors_subsystem()._step_damage_fields()


func _attach_spawn_unit_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_behaviors_subsystem()._attach_spawn_unit_contract(row, contract)


func request_spawn_unit_command(owner_id: int, command: String) -> Dictionary:
	return _contracts_behaviors_subsystem().request_spawn_unit_command(owner_id, command)


func _step_spawn_unit_behaviors() -> void:
	_contracts_behaviors_subsystem()._step_spawn_unit_behaviors()


func _attach_hit_reaction_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_behaviors_subsystem()._attach_hit_reaction_contract(row, contract)


func record_hit_reaction(entity_id: int, damage: float) -> Dictionary:
	return _contracts_behaviors_subsystem().record_hit_reaction(entity_id, damage)


func _step_hit_reactions() -> void:
	_contracts_behaviors_subsystem()._step_hit_reactions()


func _attach_animal_ai_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_behaviors_subsystem()._attach_animal_ai_contract(row, contract)


func _step_animal_ai_updates() -> void:
	_contracts_behaviors_subsystem()._step_animal_ai_updates()


func _attach_threat_finder_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_behaviors_subsystem()._attach_threat_finder_contract(row, contract)


func _step_threat_finders() -> void:
	_contracts_behaviors_subsystem()._step_threat_finders()


func _nearest_hostile_entity(source_id: int, radius_source: float) -> int:
	return _contracts_behaviors_subsystem()._nearest_hostile_entity(source_id, radius_source)


func _attach_large_group_bonus_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_behaviors_subsystem()._attach_large_group_bonus_contract(row, contract)


func _step_large_group_bonus_updates() -> void:
	_contracts_behaviors_subsystem()._step_large_group_bonus_updates()


const TransportSystemScript = preload("res://src/retail_slice/retail_sim_transport.gd")
var _transport_system = null
func _transport_subsystem():
	if _transport_system == null:
		_transport_system = TransportSystemScript.new(self)
	return _transport_system

func _attach_siege_engine_contain_contract(row: Dictionary, contract: Dictionary) -> void:
	_transport_subsystem()._attach_siege_engine_contain_contract(row, contract)

func _attach_container_family_contract(row: Dictionary, contract: Dictionary) -> void:
	_transport_subsystem()._attach_container_family_contract(row, contract)

func _container_contract_offset(fields: Dictionary, key: String) -> Vector2:
	return _transport_subsystem()._container_contract_offset(fields, key)

func _container_string_rows(fields: Dictionary, key: String) -> Array[String]:
	return _transport_subsystem()._container_string_rows(fields, key)

func _attach_horde_transport_contract(row: Dictionary, contract: Dictionary) -> void:
	_transport_subsystem()._attach_horde_transport_contract(row, contract)

func _attach_ship_slow_death_contract(row: Dictionary, contract: Dictionary) -> void:
	_transport_subsystem()._attach_ship_slow_death_contract(row, contract)

func _attach_slow_death_core_contract(row: Dictionary, contract: Dictionary) -> void:
	_transport_subsystem()._attach_slow_death_core_contract(row, contract)

func load_transport_entity(carrier_id: int, entity_id: int, manual_pickup: bool = false) -> Dictionary:
	return _transport_subsystem().load_transport_entity(carrier_id, entity_id, manual_pickup)

func issue_garrison(team: int, entity_id: int, structure_id: int) -> Dictionary:
	return _transport_subsystem().issue_garrison(team, entity_id, structure_id)

func issue_exit_garrison(team: int, entity_id: int) -> Dictionary:
	return _transport_subsystem().issue_exit_garrison(team, entity_id)

func _transport_entry_voice_candidates(passenger_object_id: String, carrier_object_id: String) -> Array[String]:
	return _transport_subsystem()._transport_entry_voice_candidates(passenger_object_id, carrier_object_id)

func load_siege_crew(carrier_id: int, entity_id: int) -> Dictionary:
	return _transport_subsystem().load_siege_crew(carrier_id, entity_id)

func _update_siege_crew_state(carrier_id: int) -> void:
	_transport_subsystem()._update_siege_crew_state(carrier_id)

func _transport_filter_accepts(row: Dictionary, filter: Array) -> bool: # de-static'd: moved to transport subsystem
	return _transport_subsystem()._transport_filter_accepts(row, filter)

func _transport_filter_key(value: String) -> String: # de-static'd: moved to transport subsystem
	return _transport_subsystem()._transport_filter_key(value)

func _transport_bone_for(passenger: Dictionary, bones: Array) -> String: # de-static'd: moved to transport subsystem
	return _transport_subsystem()._transport_bone_for(passenger, bones)

func request_transport_exit(entity_id: int) -> Dictionary:
	return _transport_subsystem().request_transport_exit(entity_id)

func request_tunnel_exit(entity_id: int, destination_id: int) -> Dictionary:
	return _transport_subsystem().request_tunnel_exit(entity_id, destination_id)

func _step_ship_runtime() -> void:
	_transport_subsystem()._step_ship_runtime()

func _finish_transport_exit(carrier_id: int, entity_id: int, destination_id: int = -1) -> void:
	_transport_subsystem()._finish_transport_exit(carrier_id, entity_id, destination_id)

func _resolve_container_death(carrier_id: int, carrier: Dictionary) -> void:
	_transport_subsystem()._resolve_container_death(carrier_id, carrier)

func _update_container_weapon_state(carrier_id: int) -> void:
	_transport_subsystem()._update_container_weapon_state(carrier_id)

func _apply_transport_passenger_damage(attacker_id: int, passenger_id: int, amount: int) -> void:
	_transport_subsystem()._apply_transport_passenger_damage(attacker_id, passenger_id, amount)

func _begin_ship_slow_death(carrier_id: int, carrier: Dictionary, death_type: String) -> bool:
	return _transport_subsystem()._begin_ship_slow_death(carrier_id, carrier, death_type)


const PhysicsSystemScript = preload("res://src/retail_slice/retail_sim_physics.gd")
var _physics_system = null
func _physics_subsystem():
	if _physics_system == null:
		_physics_system = PhysicsSystemScript.new(self)
	return _physics_system

func spawn_physics_object(source_object_id: String, position: Vector2, height_source: float, horizontal_velocity: Vector2, vertical_velocity_source: float, contract: Dictionary) -> int:
	return _physics_subsystem().spawn_physics_object(source_object_id, position, height_source, horizontal_velocity, vertical_velocity_source, contract)

func _step_physics_objects() -> void:
	_physics_subsystem()._step_physics_objects()

func _step_projectiles() -> void:
	_physics_subsystem()._step_projectiles()

func _resolve_member_projectile_impact(projectile_id: int, projectile: Dictionary) -> void:
	_physics_subsystem()._resolve_member_projectile_impact(projectile_id, projectile)

func _radius_relation_allowed(attacker_team: int, victim_team: int, affects: String) -> bool:
	return _physics_subsystem()._radius_relation_allowed(attacker_team, victim_team, affects)

func _tapered_radius_amount(amount: float, distance: float, radius: float, taper_off: float) -> int:
	return _physics_subsystem()._tapered_radius_amount(amount, distance, radius, taper_off)

func _apply_radius_damage(attacker_id: int, origin: Vector2, radius: float, amount: float, damage_type: String, taper_off: float, affects: String, exclude_target_id: int, death_type: String = "NORMAL") -> void:
	_physics_subsystem()._apply_radius_damage(attacker_id, origin, radius, amount, damage_type, taper_off, affects, exclude_target_id, death_type)

func _step_airborne_physics_object(id: int, row: Dictionary, gravity_source: float) -> void:
	_physics_subsystem()._step_airborne_physics_object(id, row, gravity_source)

func _resolve_fling_landing(projectile: Dictionary) -> void:
	_physics_subsystem()._resolve_fling_landing(projectile)

func _begin_physics_recovery(row: Dictionary) -> void:
	_physics_subsystem()._begin_physics_recovery(row)

func _step_physics_recovery_phase(row: Dictionary, next_phase: String) -> void:
	_physics_subsystem()._step_physics_recovery_phase(row, next_phase)

const FieldEffectsSystemScript = preload("res://src/retail_slice/retail_sim_field_effects.gd")
var _field_effects_system = null
func _field_effects_subsystem():
	if _field_effects_system == null:
		_field_effects_system = FieldEffectsSystemScript.new(self)
	return _field_effects_system

func _attach_fire_weapon_when_dead_contract(row: Dictionary, contract: Dictionary) -> void:
	_field_effects_subsystem()._attach_fire_weapon_when_dead_contract(row, contract)

func register_death_weapon_rule(weapon_id: String, rule: Dictionary) -> bool:
	return _field_effects_subsystem().register_death_weapon_rule(weapon_id, rule)

func _configure_death_weapon_rules_from_rules() -> void:
	_field_effects_subsystem()._configure_death_weapon_rules_from_rules()

func _step_passive_area_effect_heals() -> void:
	_field_effects_subsystem()._step_passive_area_effect_heals()

func _step_passive_area_effect_modifiers() -> void:
	_field_effects_subsystem()._step_passive_area_effect_modifiers()

func _structure_has_completed_upgrade(structure: Dictionary, upgrade_id: String) -> bool: # de-static'd: moved to subsystem
	return _field_effects_subsystem()._structure_has_completed_upgrade(structure, upgrade_id)

func _apply_passive_area_effect_heal(target: Dictionary, candidate: Dictionary) -> void:
	_field_effects_subsystem()._apply_passive_area_effect_heal(target, candidate)

func _attach_experience_state(row: Dictionary) -> void:
	_field_effects_subsystem()._attach_experience_state(row)

func _refresh_banner_carrier_state(row: Dictionary) -> void:
	_field_effects_subsystem()._refresh_banner_carrier_state(row)

func _spawn_banner_carrier_entity(parent: Dictionary, rule: Dictionary) -> void:
	_field_effects_subsystem()._spawn_banner_carrier_entity(parent, rule)

func _sync_banner_entity_transform(parent: Dictionary, banner_id: int, rule: Dictionary) -> void:
	_field_effects_subsystem()._sync_banner_entity_transform(parent, banner_id, rule)

func _step_banner_carriers() -> void:
	_field_effects_subsystem()._step_banner_carriers()

func _on_banner_carrier_defeated(banner: Dictionary) -> void:
	_field_effects_subsystem()._on_banner_carrier_defeated(banner)

func _step_banner_replenishment(banner:Dictionary) -> void:
	_field_effects_subsystem()._step_banner_replenishment(banner)

func _record_hero_rank_attainment(row: Dictionary) -> void:
	_experience_subsystem()._record_hero_rank_attainment(row)

func hero_rank_attainment(team: int, rank: int) -> Dictionary:
	return _experience_subsystem().hero_rank_attainment(team, rank)

func debug_force_max_level(entity_ids: Array) -> int:
	return _experience_subsystem().debug_force_max_level(entity_ids)

func debug_restore_health(entity_ids: Array) -> int:
	return _experience_subsystem().debug_restore_health(entity_ids)

func experience_rule_for_unit(unit_type: String) -> Dictionary:
	return _experience_subsystem().experience_rule_for_unit(unit_type)

func _experience_level_row(rule: Dictionary, rank: int) -> Dictionary:
	return _experience_subsystem()._experience_level_row(rule, rank)

func experience_state(entity_id: int) -> Dictionary:
	return _experience_subsystem().experience_state(entity_id)

func experience_unauthored_victims() -> Array[String]:
	return _experience_subsystem().experience_unauthored_victims()

func _award_member_kill_experience(attacker_id: int, target: Dictionary) -> void:
	_experience_subsystem()._award_member_kill_experience(attacker_id, target)

func _cah_tally_for(unit_type: String) -> Dictionary:
	return _experience_subsystem()._cah_tally_for(unit_type)

func _record_cah_member_kill(attacker_id: int, target: Dictionary) -> void:
	_experience_subsystem()._record_cah_member_kill(attacker_id, target)

func _record_cah_structure_kill(attacker_id: int, target: Dictionary) -> void:
	_experience_subsystem()._record_cah_structure_kill(attacker_id, target)

func _cah_openplay_multiplayer() -> bool:
	## Awardsystem.ini separates skirmish and open-play multiplayer counters.
	## The explicit session rule is agreed before setup on every lockstep peer;
	## absent remains the historical solo/skirmish path.
	return String(_rules.get("session_mode", "skirmish")) == "openplay-mp"


func _award_experience(row: Dictionary, amount: int) -> void:
	_experience_subsystem().award_experience(row, amount)


func _apply_experience_level_effects(row: Dictionary, level_row: Dictionary) -> void:
	_experience_subsystem().apply_experience_level_effects(row, level_row)


const AbilityRuntimeSystemScript = preload("res://src/retail_slice/retail_sim_ability_runtime.gd")
var _ability_runtime_system = null
func _ability_runtime_subsystem():
	if _ability_runtime_system == null:
		_ability_runtime_system = AbilityRuntimeSystemScript.new(self)
	return _ability_runtime_system

func ability_rules_for_unit(unit_type: String) -> Array:
	return _ability_runtime_subsystem().ability_rules_for_unit(unit_type)

func _ensure_capture_building_ability(unit_type: String, document: Dictionary) -> void:
	_ability_runtime_subsystem()._ensure_capture_building_ability(unit_type, document)

func ability_states_for(hero_id: int) -> Dictionary:
	return _ability_runtime_subsystem().ability_states_for(hero_id)

func _ability_object_kind_tokens(row: Dictionary) -> Array[String]:
	return _ability_runtime_subsystem()._ability_object_kind_tokens(row)

func _ability_filter_accepts(row: Dictionary, filter_text: String) -> bool:
	return _ability_runtime_subsystem()._ability_filter_accepts(row, filter_text)

func cast_ability(hero_id: int, ability_id: String, target_point: Vector2, team: int = -1) -> Dictionary:
	return _ability_runtime_subsystem().cast_ability(hero_id, ability_id, target_point, team)

func _apply_ability_activate_module_graph(row: Dictionary, ability_id: String, effect: Dictionary, target_point: Vector2, targeting: String) -> Dictionary:
	return _abilities_subsystem()._apply_ability_activate_module_graph(row, ability_id, effect, target_point, targeting)

func _activate_module_target_identity(row: Dictionary, target_point: Vector2, targeting: String) -> Dictionary:
	return _ability_runtime_subsystem()._activate_module_target_identity(row, target_point, targeting)

func _step_activate_module_graph(row: Dictionary) -> void:
	_ability_runtime_subsystem()._step_activate_module_graph(row)

func _activate_module_route_target(row: Dictionary, channel: Dictionary, mode: String) -> Dictionary:
	return _ability_runtime_subsystem()._activate_module_route_target(row, channel, mode)

func _activate_module_effect_range(effect: Dictionary, effect_range: float) -> Dictionary:
	return _ability_runtime_subsystem()._activate_module_effect_range(effect, effect_range)

func _dispatch_activate_module_leaf(row: Dictionary, ability_id: String, effect: Dictionary, point: Vector2) -> Dictionary:
	return _ability_runtime_subsystem()._dispatch_activate_module_leaf(row, ability_id, effect, point)

func _validate_special_power_activation(row: Dictionary, contract: Dictionary, targeting: String, target_point: Vector2) -> Dictionary:
	return _ability_runtime_subsystem()._validate_special_power_activation(row, contract, targeting, target_point)

func _ability_token_filter_accepts(row: Dictionary, tokens: Array) -> bool:
	return _ability_runtime_subsystem()._ability_token_filter_accepts(row, tokens)

func _apply_special_power_unit_cost(row: Dictionary, contract: Dictionary) -> void:
	_ability_runtime_subsystem()._apply_special_power_unit_cost(row, contract)

func _ability_fx_list_ids(effect: Dictionary) -> Array:
	return _ability_runtime_subsystem()._ability_fx_list_ids(effect)

func _ability_fx_radius(effect: Dictionary) -> float:
	return _ability_runtime_subsystem()._ability_fx_radius(effect)

func _ability_enemies_near(team: int, point: Vector2, radius: float) -> Array[int]:
	return _ability_runtime_subsystem()._ability_enemies_near(team, point, radius)

func _apply_ability_weapon_blast(hero_row: Dictionary, effect: Dictionary, point: Vector2) -> Dictionary:
	return _abilities_subsystem()._apply_ability_weapon_blast(hero_row, effect, point)

func _apply_ability_heal(hero_row: Dictionary, effect: Dictionary, epicenter: Vector2) -> Dictionary:
	return _abilities_subsystem()._apply_ability_heal(hero_row, effect, epicenter)

func _apply_ability_modifier(hero_row: Dictionary, ability_id: String, effect: Dictionary) -> Dictionary:
	return _abilities_subsystem()._apply_ability_modifier(hero_row, ability_id, effect)

func _apply_ability_weapon_toggle(hero_row: Dictionary, effect: Dictionary) -> Dictionary:
	return _abilities_subsystem()._apply_ability_weapon_toggle(hero_row, effect)

func _siege_deploy_target(source: Dictionary, effect: Dictionary, point: Vector2, contract: Dictionary) -> int:
	return _ability_runtime_subsystem()._siege_deploy_target(source, effect, point, contract)

func _apply_ability_siege_deploy(row: Dictionary, effect: Dictionary, point: Vector2, contract: Dictionary) -> Dictionary:
	return _abilities_subsystem()._apply_ability_siege_deploy(row, effect, point, contract)

func _toggle_deploy_set_model_condition(row: Dictionary, condition: String) -> void:
	_ability_runtime_subsystem()._toggle_deploy_set_model_condition(row, condition)

func _toggle_deploy_set_modifier(row: Dictionary, modifiers: Array, active: bool) -> void:
	_ability_runtime_subsystem()._toggle_deploy_set_modifier(row, modifiers, active)

func _apply_ability_toggle_deploy(row: Dictionary, effect: Dictionary) -> Dictionary:
	return _abilities_subsystem()._apply_ability_toggle_deploy(row, effect)

func _step_toggle_deploy(row: Dictionary) -> void:
	_ability_runtime_subsystem()._step_toggle_deploy(row)

func _step_siege_deploy(row: Dictionary) -> void:
	_ability_runtime_subsystem()._step_siege_deploy(row)

func _apply_ability_weapon_mode_special_power(row: Dictionary, effect: Dictionary) -> Dictionary:
	return _abilities_subsystem()._apply_ability_weapon_mode_special_power(row, effect)

func _attach_weapon_mode_special_power_contract(row: Dictionary, contract: Dictionary) -> void:
	_ability_runtime_subsystem()._attach_weapon_mode_special_power_contract(row, contract)

func _resolve_weapon_mode_special_power_profile(row: Dictionary, flags: Array, lock_slot: String) -> String:
	return _ability_runtime_subsystem()._resolve_weapon_mode_special_power_profile(row, flags, lock_slot)

func set_weapon_mode_special_power_paused(entity_id: int, special_power_template: String, paused: bool) -> Dictionary:
	return _ability_runtime_subsystem().set_weapon_mode_special_power_paused(entity_id, special_power_template, paused)

func activate_weapon_mode_special_power(entity_id: int, special_power_template: String, team: int = -1) -> Dictionary:
	return _ability_runtime_subsystem().activate_weapon_mode_special_power(entity_id, special_power_template, team)

func _step_weapon_mode_special_powers(row: Dictionary) -> void:
	_ability_runtime_subsystem()._step_weapon_mode_special_powers(row)

func _end_weapon_mode_special_power(row: Dictionary, policy: Dictionary) -> void:
	_ability_runtime_subsystem()._end_weapon_mode_special_power(row, policy)

func _apply_ability_dominate_enemy(row: Dictionary, ability_id: String, effect: Dictionary, target_point: Vector2, targeting: String) -> Dictionary:
	return _abilities_subsystem()._apply_ability_dominate_enemy(row, ability_id, effect, target_point, targeting)

func _apply_ability_grab_passenger(row: Dictionary, ability_id: String, effect: Dictionary, target_point: Vector2) -> Dictionary:
	return _abilities_subsystem()._apply_ability_grab_passenger(row, ability_id, effect, target_point)

func _apply_ability_repair_structure(row: Dictionary, ability_id: String, effect: Dictionary, target_point: Vector2) -> Dictionary:
	return _abilities_subsystem()._apply_ability_repair_structure(row, ability_id, effect, target_point)

func _step_repair_structure(row: Dictionary) -> void:
	_ability_runtime_subsystem()._step_repair_structure(row)

func _grab_passenger_target(source: Dictionary, effect: Dictionary, point: Vector2) -> int:
	return _ability_runtime_subsystem()._grab_passenger_target(source, effect, point)

func _step_grab_passenger(row: Dictionary) -> void:
	_ability_runtime_subsystem()._step_grab_passenger(row)

func _eject_grabbed_passengers(carrier_id: int, carrier: Dictionary) -> void:
	_ability_runtime_subsystem()._eject_grabbed_passengers(carrier_id, carrier)

func _apply_ability_fling_passenger(row: Dictionary, ability_id: String, effect: Dictionary) -> Dictionary:
	return _abilities_subsystem()._apply_ability_fling_passenger(row, ability_id, effect)

func release_grabbed_passenger(carrier_id: int, release_index: int = 0) -> Dictionary:
	return _ability_runtime_subsystem().release_grabbed_passenger(carrier_id, release_index)

func _step_fling_passenger(row: Dictionary) -> void:
	_ability_runtime_subsystem()._step_fling_passenger(row)

func _spawn_fling_physics_object(carrier: Dictionary, passenger: Dictionary, effect: Dictionary) -> int:
	return _ability_runtime_subsystem()._spawn_fling_physics_object(carrier, passenger, effect)

func _step_dominate_enemy(row: Dictionary) -> void:
	_ability_runtime_subsystem()._step_dominate_enemy(row)

func _dominate_enemy_candidates(source: Dictionary, effect: Dictionary, point: Vector2, targeting: String) -> Array[int]:
	return _ability_runtime_subsystem()._dominate_enemy_candidates(source, effect, point, targeting)

func _dominate_enemy_filter_accepts(source: Dictionary, target: Dictionary, filter_text: String) -> bool:
	return _ability_runtime_subsystem()._dominate_enemy_filter_accepts(source, target, filter_text)

func _dominate_enemy_convert(source: Dictionary, target: Dictionary, permanent: bool = true, temporary_duration_ticks: int = 0) -> void:
	_ability_runtime_subsystem()._dominate_enemy_convert(source, target, permanent, temporary_duration_ticks)

func _step_temporary_defect(row: Dictionary) -> void:
	_ability_runtime_subsystem()._step_temporary_defect(row)

func _apply_ability_mount_toggle(hero_row: Dictionary, effect: Dictionary) -> Dictionary:
	return _abilities_subsystem()._apply_ability_mount_toggle(hero_row, effect)

func _apply_ability_special_disguise(row: Dictionary, effect: Dictionary) -> Dictionary:
	return _abilities_subsystem()._apply_ability_special_disguise(row, effect)

func special_disguise_opacity(row: Dictionary) -> float:
	return _ability_runtime_subsystem().special_disguise_opacity(row)

func _step_special_disguise(row: Dictionary) -> void:
	_ability_runtime_subsystem()._step_special_disguise(row)

func cancel_special_disguise(entity_id: int, reason: String = "explicit", suppress_exit_fx: bool = false) -> Dictionary:
	return _ability_runtime_subsystem().cancel_special_disguise(entity_id, reason, suppress_exit_fx)

func _cancel_special_disguise_row(row: Dictionary, reason: String, suppress_exit_fx: bool) -> Dictionary:
	return _ability_runtime_subsystem()._cancel_special_disguise_row(row, reason, suppress_exit_fx)

func _emit_special_disguise_presentation(row: Dictionary, role: String, template_id: String, force_mounted: bool) -> void:
	_ability_runtime_subsystem()._emit_special_disguise_presentation(row, role, template_id, force_mounted)

func _rescale_member_health_preserving_fraction(row: Dictionary, new_member_maximum: int) -> void:
	_ability_runtime_subsystem()._rescale_member_health_preserving_fraction(row, new_member_maximum)

func _apply_ability_capture_building(hero_row: Dictionary, effect: Dictionary, target_point: Vector2) -> Dictionary:
	return _abilities_subsystem()._apply_ability_capture_building(hero_row, effect, target_point)

func _step_capture_channel(row: Dictionary) -> bool:
	return _ability_runtime_subsystem()._step_capture_channel(row)

func _apply_ability_terror(hero_row: Dictionary, ability_id: String, effect: Dictionary) -> Dictionary:
	return _abilities_subsystem()._apply_ability_terror(hero_row, ability_id, effect)

func _apply_fear_scatter(center: Vector2, row: Dictionary, strength: float) -> void:
	_ability_runtime_subsystem()._apply_fear_scatter(center, row, strength)

func _apply_ability_summon(hero_row: Dictionary, effect: Dictionary, point: Vector2) -> Dictionary:
	return _abilities_subsystem()._apply_ability_summon(hero_row, effect, point)

func _apply_ability_summon_chain(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	return _abilities_subsystem()._apply_ability_summon_chain(team, effect, point)

func _summon_unit_type_for(source_object_id: String) -> String:
	return _ability_runtime_subsystem()._summon_unit_type_for(source_object_id)

func _apply_ability_experience_grant(hero_row: Dictionary, effect: Dictionary, point: Vector2) -> Dictionary:
	return _ability_runtime_subsystem()._apply_ability_experience_grant(hero_row, effect, point)

func _apply_ability_arrow_storm(hero_row: Dictionary, effect: Dictionary, point: Vector2) -> Dictionary:
	return _abilities_subsystem()._apply_ability_arrow_storm(hero_row, effect, point)

func _step_volley_channel(row: Dictionary) -> bool:
	return _ability_runtime_subsystem()._step_volley_channel(row)

func _apply_ability_stealth_toggle(hero_row: Dictionary, effect: Dictionary) -> Dictionary:
	return _abilities_subsystem()._apply_ability_stealth_toggle(hero_row, effect)

func _stealth_active(row: Dictionary) -> bool:
	return _ability_runtime_subsystem()._stealth_active(row)

func _grant_stealth(row: Dictionary, until_tick: int, forbidden: Array) -> void:
	_ability_runtime_subsystem()._grant_stealth(row, until_tick, forbidden)

func _clear_stealth(row: Dictionary) -> void:
	_ability_runtime_subsystem()._clear_stealth(row)

func _break_stealth(row: Dictionary, condition: String) -> void:
	_ability_runtime_subsystem()._break_stealth(row, condition)

func _apply_ability_teleport(hero_row: Dictionary, effect: Dictionary, point: Vector2) -> Dictionary:
	return _abilities_subsystem()._apply_ability_teleport(hero_row, effect, point)

func _apply_ability_curse(hero_row: Dictionary, effect: Dictionary, point: Vector2) -> Dictionary:
	return _abilities_subsystem()._apply_ability_curse(hero_row, effect, point)

func _apply_ability_leadership_strip(hero_row: Dictionary, effect: Dictionary) -> Dictionary:
	return _abilities_subsystem()._apply_ability_leadership_strip(hero_row, effect)

func _set_timed_modifier(row: Dictionary, key: String, modifiers: Array, expires_tick: int) -> void:
	_ability_runtime_subsystem()._set_timed_modifier(row, key, modifiers, expires_tick)

func _timed_modifier_product(row: Dictionary, kind: String) -> float:
	return _ability_runtime_subsystem()._timed_modifier_product(row, kind)

func _timed_modifier_active(row: Dictionary, kind: String) -> bool:
	return _ability_runtime_subsystem()._timed_modifier_active(row, kind)

func _ability_outgoing_multiplier(row: Dictionary) -> float:
	return _ability_runtime_subsystem()._ability_outgoing_multiplier(row)

func _ability_incoming_multiplier(row: Dictionary) -> float:
	return _ability_runtime_subsystem()._ability_incoming_multiplier(row)

func _ability_speed_multiplier(row: Dictionary) -> float:
	return _ability_runtime_subsystem()._ability_speed_multiplier(row)

func _ability_vision_multiplier(row: Dictionary) -> float:
	return _ability_runtime_subsystem()._ability_vision_multiplier(row)

func _recompute_leadership_auras() -> void:
	_ability_runtime_subsystem()._recompute_leadership_auras()

func _step_hero_abilities() -> void:
	_ability_runtime_subsystem()._step_hero_abilities()

func _step_economy() -> void:
	_economy_subsystem().step_economy()


func _initialize_structure_auto_deposit(structure: Dictionary) -> void:
	_economy_subsystem().initialize_structure_auto_deposit(structure)


func _auto_deposit_upgrade_boost(team: int, rule: Dictionary) -> int:
	return _economy_subsystem().auto_deposit_upgrade_boost(team, rule)


func _step_auto_deposit_updates() -> void:
	_economy_subsystem().step_auto_deposit_updates()


func _award_auto_deposit_capture(
	structure: Dictionary, new_team: int
) -> int:
	return _economy_subsystem().award_auto_deposit_capture(structure, new_team)


func _ai_resource_handicap(team: int, income: int) -> int:
	return _economy_subsystem().ai_resource_handicap(team, income)


func _step_structure_upgrades() -> void:
	_upgrades_subsystem()._step_structure_upgrades()


func _step_production() -> void:
	_production_subsystem()._step_production()


## State/tuning owned by the sim; logic lives in retail_sim_stepper.gd
const HERO_REGEN_OUT_OF_COMBAT_SECONDS := 5.0
const HERO_REGEN_PERCENT_PER_SECOND := 0.01
var _has_hero_units := false
var _unit_ability_rules: Dictionary = {}
var _shared_ability_cooldowns: Dictionary = {} # team:special-power -> ready tick
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
const ABILITY_AURA_INTERVAL_TICKS := 5
var _unit_experience_rules: Dictionary = {}
var _experience_unauthored_victims: Dictionary = {}
var _unit_module_contracts: Dictionary = {}
var _structure_module_contracts: Dictionary = {}
var _castle_upgrade_grants: Dictionary = {}
var _hero_peak_ranks_by_team: Dictionary = {}

const StepperSystemScript = preload("res://src/retail_slice/retail_sim_stepper.gd")
var _stepper_system = null
func _stepper_subsystem():
	if _stepper_system == null:
		_stepper_system = StepperSystemScript.new(self)
	return _stepper_system

func _step_entity(id: int) -> void:
	_stepper_subsystem()._step_entity(id)

func set_structure_rally(team: int, structure_id: int, position: Vector2) -> Dictionary:
	return _stepper_subsystem().set_structure_rally(team, structure_id, position)

const ConstructionSystemScript = preload("res://src/retail_slice/retail_sim_construction.gd")
var _construction_system = null
func _construction_subsystem():
	if _construction_system == null:
		_construction_system = ConstructionSystemScript.new(self)
	return _construction_system

func structure_sell_command(structure_id: int) -> Dictionary:
	return _construction_subsystem().structure_sell_command(structure_id)

func sell_structure(team: int, structure_id: int) -> Dictionary:
	return _construction_subsystem().sell_structure(team, structure_id)

func structure_command_slot(structure_id: int, command_id: String) -> int:
	return _construction_subsystem().structure_command_slot(structure_id, command_id)

func _compiled_sell_slot_for(building: Dictionary) -> Dictionary:
	return _construction_subsystem()._compiled_sell_slot_for(building)

func _compiled_command_slots_for(building: Dictionary) -> Array:
	return _construction_subsystem()._compiled_command_slots_for(building)

func _direct_or_first_command_set_slots(sets: Array) -> Array:
	return _construction_subsystem()._direct_or_first_command_set_slots(sets)

func _clear_expansion_pad_occupant(structure_id: int) -> void:
	_construction_subsystem()._clear_expansion_pad_occupant(structure_id)

func issue_construct(ids: Array[int], structure_kind: String, position: Vector2, dry_run: bool = false, team: int = PLAYER_TEAM) -> Dictionary:
	return _construction_subsystem().issue_construct(ids, structure_kind, position, dry_run, team)

func _structure_placement_radius(structure_kind: String) -> float:
	return _construction_subsystem()._structure_placement_radius(structure_kind)

func _authored_structure_placement_radius(team: int, structure_kind: String) -> float:
	return _construction_subsystem()._authored_structure_placement_radius(team, structure_kind)

func _authored_site_foundation_fixture_contains(fixture: Dictionary, site: Vector2) -> bool:
	return _construction_subsystem()._authored_site_foundation_fixture_contains(fixture, site)

func _issue_construct_for_team(team: int, ids: Array[int], structure_kind: String, position: Vector2, dry_run: bool = false, authored_castle_site: bool = false) -> Dictionary:
	return _construction_subsystem()._issue_construct_for_team(team, ids, structure_kind, position, dry_run, authored_castle_site)

func debug_finish_team_work(team: int) -> Dictionary:
	return _construction_subsystem().debug_finish_team_work(team)

func debug_level_up_battalions(ids: Array) -> Dictionary:
	return _construction_subsystem().debug_level_up_battalions(ids)

func _step_construction() -> void:
	_construction_subsystem()._step_construction()

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
const COMBAT_FALLBACK_STRUCTURE_SOURCE_RADIUS := 5.0
## Attack/footprint state (owned by the sim; logic lives in retail_sim_attacks.gd)
var _member_fire_ticks: Dictionary = {}
var _structure_footprint_source_cache: Dictionary = {}
var _structure_footprint_radius_cache: Dictionary = {}
var _footprint_fallback_reported: Dictionary = {}

const AttacksSystemScript = preload("res://src/retail_slice/retail_sim_attacks.gd")
var _attacks_system = null
func _attacks_subsystem():
	if _attacks_system == null:
		_attacks_system = AttacksSystemScript.new(self)
	return _attacks_system

func _nearest_attack_move_target(row: Dictionary) -> int:
	return _attacks_subsystem()._nearest_attack_move_target(row)

func _nearest_auto_target(row: Dictionary) -> Dictionary:
	return _attacks_subsystem()._nearest_auto_target(row)

func _rearm_mood_idle_cadence(row: Dictionary) -> void:
	_attacks_subsystem()._rearm_mood_idle_cadence(row)

func _step_production_exit(row: Dictionary) -> bool:
	return _production_subsystem()._step_production_exit(row)

func _step_member_attacks(attacker_id: int, row: Dictionary, target_id: int, target_kind: String) -> void:
	_attacks_subsystem()._step_member_attacks(attacker_id, row, target_id, target_kind)

func _member_weapon_has_projectile(row: Dictionary) -> bool:
	return _attacks_subsystem()._member_weapon_has_projectile(row)

func _scaled_projectile_components(components: Array, outgoing_amount: int) -> Array:
	return _attacks_subsystem()._scaled_projectile_components(components, outgoing_amount)

func _launch_member_projectile(attacker_id: int, member_index: int, row: Dictionary, target_id: int, target_kind: String, forced_target: int, swing_damage: int, weapon_effect: Dictionary, release_token: int) -> void:
	_attacks_subsystem()._launch_member_projectile(attacker_id, member_index, row, target_id, target_kind, forced_target, swing_damage, weapon_effect, release_token)

func _apply_member_bonus_nuggets(attacker_id: int, member_index: int, row: Dictionary, target_id: int, target_kind: String, forced_target: int, weapon_effect: Dictionary) -> void:
	_attacks_subsystem()._apply_member_bonus_nuggets(attacker_id, member_index, row, target_id, target_kind, forced_target, weapon_effect)

func _clear_member_attack_schedule(row: Dictionary) -> void:
	_attacks_subsystem()._clear_member_attack_schedule(row)

func _clear_member_targets(row: Dictionary) -> void:
	_attacks_subsystem()._clear_member_targets(row)

func _weapon_mode_for_distance(row: Dictionary, distance: float) -> String:
	return _attacks_subsystem()._weapon_mode_for_distance(row, distance)

func _formation_effects(row: Dictionary) -> Dictionary:
	return _attacks_subsystem()._formation_effects(row)

func _stance_state(row: Dictionary, requested: String = "") -> Dictionary:
	return _attacks_subsystem()._stance_state(row, requested)

func _apply_weapon_mode(row: Dictionary, mode: String) -> bool:
	return _attacks_subsystem()._apply_weapon_mode(row, mode)

func member_weapon_condition_tokens(entity_id: int) -> Array:
	return _attacks_subsystem().member_weapon_condition_tokens(entity_id)

func weapon_condition_deferred_reasons(entity_id: int) -> Array:
	return _attacks_subsystem().weapon_condition_deferred_reasons(entity_id)

func _live_weapon_set_condition_tokens(row: Dictionary) -> Array:
	return _attacks_subsystem()._live_weapon_set_condition_tokens(row)

func _mark_member_release(attacker_id: int, member_index: int) -> void:
	_attacks_subsystem()._mark_member_release(attacker_id, member_index)

func _ensure_member_target_assignments(attacker: Dictionary, target: Dictionary) -> void:
	_attacks_subsystem()._ensure_member_target_assignments(attacker, target)

func _member_world_position(row: Dictionary, member_index: int) -> Vector2:
	return _attacks_subsystem()._member_world_position(row, member_index)

func _living_member_count(row: Dictionary) -> int:
	return _attacks_subsystem()._living_member_count(row)

## Structure eviction advances by the same per-tick push as battalion separation:
## displacement makes that eviction a walk, not a jump (state constant used by the collision module).
const STRUCTURE_EVICTION_STEP = BATTALION_SEPARATION_PUSH

const CollisionSystemScript = preload("res://src/retail_slice/retail_sim_collision.gd")
var _collision_system = null
func _collision_subsystem():
	if _collision_system == null:
		_collision_system = CollisionSystemScript.new(self)
	return _collision_system

func _structure_footprint_radius(structure_row: Dictionary) -> float:
	return _collision_subsystem()._structure_footprint_radius(structure_row)

func _resolved_footprint_source_radius(source_object_id: String, structure_kind: String) -> float:
	return _collision_subsystem()._resolved_footprint_source_radius(source_object_id, structure_kind)

func _target_footprint_radius(target_id: int, target_kind: String) -> float:
	return _collision_subsystem()._target_footprint_radius(target_id, target_kind)

func _point_segment_distance(point: Vector2, a: Vector2, b: Vector2) -> float:
	return _collision_subsystem()._point_segment_distance(point, a, b)

func _castle_footprint_pass_through(position: Vector2, attack_target_id: int, attack_target_kind: String) -> Dictionary:
	return _collision_subsystem()._castle_footprint_pass_through(position, attack_target_id, attack_target_kind)

func _castle_gate_blocking_discs(structure_row: Dictionary, mover: Dictionary) -> Array[Dictionary]:
	return _collision_subsystem()._castle_gate_blocking_discs(structure_row, mover)

func _deflect_around_structures(position: Vector2, row: Dictionary, travel_step: Vector2 = Vector2.ZERO, structure_id_list: Array[int] = []) -> Vector2:
	return _collision_subsystem()._deflect_around_structures(position, row, travel_step, structure_id_list)

func _tangential_slide_point(center: Vector2, radius: float, radial_direction: Vector2, travel_step: Vector2, destination: Vector2 = Vector2.INF) -> Vector2:
	return _collision_subsystem()._tangential_slide_point(center, radius, radial_direction, travel_step, destination)

func _step_structure_eviction() -> void:
	_collision_subsystem()._step_structure_eviction()

func _is_engaged_in_range(row: Dictionary) -> bool:
	return _collision_subsystem()._is_engaged_in_range(row)

func _eviction_fallback_direction(row: Dictionary) -> Vector2:
	return _collision_subsystem()._eviction_fallback_direction(row)

func _should_honor_turn_rate(row: Dictionary) -> bool:
	return _collision_subsystem()._should_honor_turn_rate(row)

func _report_turn_rate_fallback(row: Dictionary) -> void:
	_collision_subsystem()._report_turn_rate_fallback(row)

func _should_reform(row: Dictionary) -> bool:
	return _collision_subsystem()._should_reform(row)

func _retail_reform_threshold_degrees(row: Dictionary) -> float:
	return _movement_subsystem()._retail_reform_threshold_degrees(row)

func _retail_turn_rate_degrees(row: Dictionary) -> float:
	return _movement_subsystem()._retail_turn_rate_degrees(row)

func _step_retail_heading(row: Dictionary, movement_direction: Vector2, braking: float, effective_turn_rate_degrees_per_second: float) -> bool:
	return _collision_subsystem()._step_retail_heading(row, movement_direction, braking, effective_turn_rate_degrees_per_second)

func _step_route(row: Dictionary) -> void:
	_movement_subsystem()._step_route(row)

func _consume_route_point_layer(row: Dictionary) -> void:
	_collision_subsystem()._consume_route_point_layer(row)

func _should_attempt_crush(row: Dictionary, translation_speed: float, max_speed: float) -> bool:
	return _collision_subsystem()._should_attempt_crush(row, translation_speed, max_speed)

func _try_cavalry_trample(row: Dictionary) -> void:
	_collision_subsystem()._try_cavalry_trample(row)

func _apply_crush_deceleration(crusher: Dictionary, victim: Dictionary) -> void:
	_collision_subsystem()._apply_crush_deceleration(crusher, victim)

func _resume_order_after_knockdown(row: Dictionary) -> bool:
	return _collision_subsystem()._resume_order_after_knockdown(row)

func _apply_knockback(center: Vector2, radius: float, strength: float, source_team: int, damage: int, damage_reason: String, source_id: int = 0, taper_off: float = 0.0, z_mult: float = 1.0) -> int:
	return _collision_subsystem()._apply_knockback(center, radius, strength, source_team, damage, damage_reason, source_id, taper_off, z_mult)

func _apply_damage(attacker_id: int, target_id: int, amount: int, target_kind: String = "battalion", death_type: String = "NORMAL", damage_type_override: String = "") -> void:
	_combat_subsystem()._apply_damage(attacker_id, target_id, amount, target_kind, death_type, damage_type_override)

func _incoming_damage_factor(attacker_id: int, target: Dictionary, target_kind: String, damage_type: String, components: Array = []) -> float:
	return _damage_subsystem()._incoming_damage_factor(attacker_id, target, target_kind, damage_type, components)

func _active_armor_table(target: Dictionary) -> Dictionary:
	return _damage_subsystem()._active_armor_table(target)

func _flanked_penalty_multiplier(target: Dictionary) -> float:
	return _damage_subsystem()._flanked_penalty_multiplier(target)

func _flanking_outgoing_multiplier(attacker: Dictionary, target: Dictionary) -> float:
	return _damage_subsystem()._flanking_outgoing_multiplier(attacker, target)

func _is_flanking_hit(attacker_id: int, target: Dictionary) -> bool:
	return _damage_subsystem()._is_flanking_hit(attacker_id, target)

func _member_body_damage_factor(target: Dictionary, damage_type: String, components: Array = []) -> float:
	return _damage_subsystem()._member_body_damage_factor(target, damage_type, components)

func _highlander_raw_damage_amount(raw_amount: float, current_health: int, damage_type: String, damage_components: Array) -> float:
	return _damage_subsystem()._highlander_raw_damage_amount(raw_amount, current_health, damage_type, damage_components)

func _structure_uses_highlander_body(target: Dictionary) -> bool:
	return _damage_subsystem()._structure_uses_highlander_body(target)

func _apply_member_damage(attacker_id: int, attacker_member_index: int, target_id: int, amount: int, target_kind: String, attack_sequence: int, forced_target_member: int = -1, damage_type_override: String = "", death_type: String = "NORMAL", damage_components_override: Variant = null) -> void:
	_damage_subsystem()._apply_member_damage(attacker_id, attacker_member_index, target_id, amount, target_kind, attack_sequence, forced_target_member, damage_type_override, death_type, damage_components_override)

func _bookkeep_battalion_death(entity_id: int, row: Dictionary, death_type: String, defeated_members: Array[int], attacker_id: int = 0) -> Dictionary:
	return _damage_subsystem()._bookkeep_battalion_death(entity_id, row, death_type, defeated_members, attacker_id)

func _release_command_points_once(row: Dictionary) -> void:
	_production_subsystem()._release_command_points_once(row)

func _entity_command_point_commitment(row: Dictionary) -> int:
	return _production_subsystem()._entity_command_point_commitment(row)

func _apply_playable_unit_death_policy(row: Dictionary, death_type: String, defeated_members: Array[int]) -> Dictionary:
	return _damage_subsystem()._apply_playable_unit_death_policy(row, death_type, defeated_members)

func _slow_death_core_matches(policy: Dictionary, death_type: String) -> bool:
	return _damage_subsystem()._slow_death_core_matches(policy, death_type)

func _slow_death_delay_ticks(milliseconds: float) -> int:
	return _damage_subsystem()._slow_death_delay_ticks(milliseconds)

func _begin_slow_death_core(row: Dictionary, death_type: String) -> bool:
	return _damage_subsystem()._begin_slow_death_core(row, death_type)

func _record_slow_death_phase(row: Dictionary, phase: String) -> void:
	_damage_subsystem()._record_slow_death_phase(row, phase)

func _step_slow_death_core() -> void:
	_damage_subsystem()._step_slow_death_core()

func _award_own_guys_die_experience(row: Dictionary, defeated_count: int) -> void:
	_damage_subsystem()._award_own_guys_die_experience(row, defeated_count)

func _slow_death_fade_ticks(row: Dictionary, death_type: String) -> int:
	return _damage_subsystem()._slow_death_fade_ticks(row, death_type)

func _schedule_fire_weapon_when_dead(row: Dictionary, death_type: String, source_kind: String) -> void:
	_damage_subsystem()._schedule_fire_weapon_when_dead(row, death_type, source_kind)

func _death_mux_matches(policy: Dictionary, death_type: String) -> bool: # de-static'd: moved to subsystem
	return _damage_subsystem()._death_mux_matches(policy, death_type)

func _death_status_mux_matches(row: Dictionary, policy: Dictionary) -> bool: # de-static'd: moved to subsystem
	return _damage_subsystem()._death_status_mux_matches(row, policy)

func _create_object_die_matches(row: Dictionary, death_type: String) -> bool:
	return _damage_subsystem()._create_object_die_matches(row, death_type)

func _consume_create_object_die(row: Dictionary, death_type: String) -> void:
	_damage_subsystem()._consume_create_object_die(row, death_type)

func hatch_create_object_die_entry(entry: Dictionary) -> Dictionary:
	return _damage_subsystem().hatch_create_object_die_entry(entry)

func _ocl_leaf_lookup(ocl_id: String) -> Dictionary:
	return _damage_subsystem()._ocl_leaf_lookup(ocl_id)

func register_ocl_leaf(ocl_id: String, leaf: Dictionary) -> void:
	_damage_subsystem().register_ocl_leaf(ocl_id, leaf)

func register_object_leaf(object_id: String, leaf: Dictionary) -> void:
	_damage_subsystem().register_object_leaf(object_id, leaf)

func ingest_ocl_leaves_from_document(document: Dictionary) -> int:
	return _damage_subsystem().ingest_ocl_leaves_from_document(document)

func fog_of_war():
	return _fog_subsystem().fog_of_war()

func _shroud_clearing_radius(row: Dictionary) -> float:
	return _fog_subsystem()._shroud_clearing_radius(row)

func source_transform_scale() -> float:
	return _fog_subsystem().source_transform_scale()

func refresh_fog_of_war() -> void:
	_fog_subsystem().refresh_fog_of_war()

func _step_shroud_grid() -> void:
	_fog_subsystem()._step_shroud_grid()

func _structure_shroud_clearing_radius(srow: Dictionary) -> float:
	return _fog_subsystem()._structure_shroud_clearing_radius(srow)

func _step_fog_from_vision() -> void:
	_fog_subsystem()._step_fog_from_vision()

const NeutralsSystemScript = preload("res://src/retail_slice/retail_sim_neutrals.gd")
var _neutrals_system = null
func _neutrals_subsystem():
	if _neutrals_system == null:
		_neutrals_system = NeutralsSystemScript.new(self)
	return _neutrals_system


const FogSystemScript = preload("res://src/retail_slice/retail_sim_fog.gd")
var _fog_system = null
func _fog_subsystem():
	if _fog_system == null:
		_fog_system = FogSystemScript.new(self)
	return _fog_system

const DamageSystemScript = preload("res://src/retail_slice/retail_sim_damage.gd")
var _damage_system = null
func _damage_subsystem():
	if _damage_system == null:
		_damage_system = DamageSystemScript.new(self)
	return _damage_system

func _keep_object_die_matches(row: Dictionary, death_type: String) -> bool:
	return _damage_subsystem()._keep_object_die_matches(row, death_type)

func _destroy_die_matches(row: Dictionary, owner_role: String, death_type: String) -> bool:
	return _damage_subsystem()._destroy_die_matches(row, owner_role, death_type)

func _cleanup_expired_corpses() -> void:
	_damage_subsystem()._cleanup_expired_corpses()

func _choose_target_member(target: Dictionary, attacker_id: int, attacker_member_index: int, attack_sequence: int) -> int:
	return _damage_subsystem()._choose_target_member(target, attacker_id, attacker_member_index, attack_sequence)

func _apply_structure_damage(attacker_id: int, target_id: int, amount: int, damage_type_override: String = "", damage_components_override: Variant = null) -> void:
	_damage_subsystem()._apply_structure_damage(attacker_id, target_id, amount, damage_type_override, damage_components_override)

func _target_alive(target_id: int, target_kind: String) -> bool:
	return _damage_subsystem()._target_alive(target_id, target_kind)

func _target_position(target_id: int, target_kind: String) -> Vector2:
	return _damage_subsystem()._target_position(target_id, target_kind)

func _seed_scenario_map_placements() -> void:
	_neutrals_subsystem()._seed_scenario_map_placements()

func _castle_fixture_team(owner: String) -> int:
	return _neutrals_subsystem()._castle_fixture_team(owner)

func _seed_capturable_neutrals() -> void:
	_neutrals_subsystem()._seed_capturable_neutrals()

func _link_capture_flags(seeded_ids: Array[int]) -> void:
	_neutrals_subsystem()._link_capture_flags(seeded_ids)

func _transfer_linked_capture(flag_row: Dictionary, team: int) -> void:
	_neutrals_subsystem()._transfer_linked_capture(flag_row, team)

func _seed_castle_fixtures() -> void:
	_neutrals_subsystem()._seed_castle_fixtures()

func _attach_castle_fixture_garrison(row: Dictionary, garrison: Dictionary) -> void:
	_neutrals_subsystem()._attach_castle_fixture_garrison(row, garrison)

func _update_ai_controllers() -> void:
	_ai_subsystem().update_ai_controllers()


func _ensure_ai_state(team: int) -> Dictionary:
	return _ai_subsystem().ensure_ai_state(team)


func _update_enemy_ai() -> void:
	_ai_subsystem().run_ai_for_team(ENEMY_TEAM, _difficulty_profile(ENEMY_TEAM), _ensure_ai_state(ENEMY_TEAM))


func ai_base_build_order() -> Array[String]:
	return _ai_subsystem().ai_build_order_for_team(ENEMY_TEAM)


func force_ai_construction_complete(team: int = ENEMY_TEAM) -> void:
	var state := _ensure_ai_state(team)
	state["construction_attempted"] = true
	state["construction_resolved"] = true
	state["build_order_index"] = AI_BUILD_ORDER.size()


func _run_ai_for_team(team: int, profile: Dictionary, ai_state: Dictionary) -> void:
	_ai_subsystem().run_ai_for_team(team, profile, ai_state)


func _ai_assign_target_route_with_backoff(
	row: Dictionary,
	target_kind: String,
	target_id: int,
	target_position: Vector2,
	profile: Dictionary
) -> bool:
	return _ai_subsystem().ai_assign_target_route_with_backoff(row, target_kind, target_id, target_position, profile)


func _ai_primary_hostile_fortress(team: int, weakest: bool) -> int:
	return _ai_subsystem().ai_primary_hostile_fortress(team, weakest)


func _ai_apply_retreat(team: int, profile: Dictionary) -> void:
	_ai_subsystem().ai_apply_retreat(team, profile)


const AI_BUILD_ORDER: Array[String] = [
	"farm", "barracks", "farm", "archery_range", "stable", "farm", "workshop",
]


func _step_ai_base_building(team: int, ai_state: Dictionary) -> void:
	_ai_subsystem().step_ai_base_building(team, ai_state)


func _ai_build_order_for_team(team: int) -> Array[String]:
	return _ai_subsystem().ai_build_order_for_team(team)


func _ai_train_builder(team: int) -> void:
	_ai_subsystem().ai_train_builder(team)


func _start_ai_farm(team: int, ai_state: Dictionary = {}) -> bool:
	return _ai_subsystem().start_ai_farm(team, ai_state)


func _ai_start_waypoint_name(team: int) -> String:
	return _ai_subsystem().ai_start_waypoint_name(team)


func _castle_ai_home(team: int) -> Dictionary:
	return _ai_subsystem().castle_ai_home(team)


func _castle_ai_layout(team: int) -> Dictionary:
	return _ai_subsystem().castle_ai_layout(team)


func _structure_kind_for_source_object(team: int, object_id: String) -> String:
	return _ai_subsystem().structure_kind_for_source_object(team, object_id)


func _castle_ai_project_source_offset(offset_source: Vector2) -> Vector2:
	return _ai_subsystem().castle_ai_project_source_offset(offset_source)


const CASTLE_AI_GENERIC_CELL_SOURCE_PREFIX := "generic-navigation-cell:"

## Construct refusals that `_issue_construct_for_team` returns BEFORE it looks at
## the requested position: the match/team gate, the faction's own structure
## build-rule table, and the building-permission identity. None of them can turn
## from "no" to "yes" by moving the site, so one receipt settles the whole
## (team, structure kind) pair and the map scan is skipped entirely.
const CASTLE_AI_KIND_LEVEL_REFUSALS := [
	"match-unavailable",
	"invalid-team",
	"unsupported-structure",
	"building-permission-identity-unresolved",
	"building-disallowed",
]

## Retail's skirmish AI does not search the map for base sites: aidata.ini
## authors the whole base outright. `SkirmishBuildList Gondor`
## (retail extract, data/ini/default/aidata.ini:145-202) places eight
## structures, and the farthest from that list's centroid (997.51, 1132.77) is
## GondorWorkshop at X:821.34 Y:1365.61 — 291.98 source units away. That is the
## measured retail base footprint radius, and it is what bounds ONE tick of the
## generic navigation-cell fallback below: a tick may examine at most a square
## of that radius in cells. The scan resumes from the ring it stopped on, so
## total reach is unchanged — only the per-tick cost is capped.
const CASTLE_AI_RETAIL_BASE_EXTENT_SOURCE := 291.9753598042349


func _castle_ai_authored_candidates(team: int, structure_kind: String) -> Array[Dictionary]:
	return _ai_subsystem().castle_ai_authored_candidates(team, structure_kind)


func _try_castle_ai_site(
	team: int,
	ai_state: Dictionary,
	builder_ids: Array[int],
	structure_kind: String,
	candidate: Vector2,
	source: String
) -> bool:
	return _ai_subsystem().try_castle_ai_site(team, ai_state, builder_ids, structure_kind, candidate, source)


func _try_castle_ai_construction(
	team: int,
	ai_state: Dictionary,
	builder_ids: Array[int],
	structure_kind: String
) -> bool:
	return _ai_subsystem().try_castle_ai_construction(team, ai_state, builder_ids, structure_kind)


func _castle_ai_store_scan_cursors(ai_state: Dictionary, cursors: Dictionary) -> void:
	_ai_subsystem().castle_ai_store_scan_cursors(ai_state, cursors)


func _castle_ai_generic_scan_cell_budget(home_cell: Vector2i) -> int:
	return _ai_subsystem().castle_ai_generic_scan_cell_budget(home_cell)


func _castle_ai_kind_level_refusal(
	team: int,
	builder_ids: Array[int],
	structure_kind: String,
	probe: Vector2
) -> String:
	return _ai_subsystem().castle_ai_kind_level_refusal(team, builder_ids, structure_kind, probe)


func _ai_construction_is_viable(team: int) -> bool:
	return _ai_subsystem().ai_construction_is_viable(team)


func _abandon_ai_construction(team: int) -> void:
	_ai_subsystem().abandon_ai_construction(team)


func _build_route(from: Vector2, to: Vector2) -> Array[Vector2]:
	return _movement_subsystem()._build_route(from, to)


func _assign_route(row: Dictionary, destination: Vector2) -> bool:
	return _movement_subsystem()._assign_route(row, destination)


func _assign_target_route(row: Dictionary, target_position: Vector2) -> bool:
	return _movement_subsystem()._assign_target_route(row, target_position)


func _query_route(from: Vector2, to: Vector2) -> Dictionary:
	return _movement_subsystem()._query_route(from, to)


func _query_route_for_row(row: Dictionary, from: Vector2, to: Vector2) -> Dictionary:
	return _movement_subsystem()._query_route_for_row(row, from, to)


func _row_route_uses_walk_surface(row: Dictionary, destination: Vector2) -> bool:
	return _movement_subsystem()._row_route_uses_walk_surface(row, destination)


func _is_naval_row(row: Dictionary) -> bool:
	return _movement_subsystem()._is_naval_row(row)


## PRESENTATION-ONLY: the LOCAL machine's seat for selection/control-group
## gating. Never part of the authoritative state or the lockstep hash — each
## peer's selection is local. Defaults to sim.PLAYER_TEAM (solo and the host seat);
## the slice sets the lockstep guest's real seat (team 1) so the guest selects
## and controls its OWN army instead of a hardcoded team 0.
var local_seat_team: int = PLAYER_TEAM

const VictorySystemScript = preload("res://src/retail_slice/retail_sim_victory.gd")
var _victory_system = null
func _victory_subsystem():
	if _victory_system == null:
		_victory_system = VictorySystemScript.new(self)
	return _victory_system

func _team_defeated(team: int) -> bool:
	return _victory_subsystem()._team_defeated(team)

func _surviving_teams() -> Array:
	return _victory_subsystem()._surviving_teams()

func _evaluate_cah_match_awards() -> void:
	_victory_subsystem()._evaluate_cah_match_awards()

func _resolve_victory() -> void:
	_victory_subsystem()._resolve_victory()

func _is_commandable(id: int) -> bool:
	return _is_commandable_for_team(id, local_seat_team)


func _is_commandable_for_team(id: int, team: int) -> bool:
	# Knocked-down battalions are incapacitated: orders bounce off until the
	# battalion stands back up (retail sprawled infantry take no commands).
	return entities.has(id) and not entity_container.has(id) and not bool((entities[id] as Dictionary).get("is_banner_carrier", false)) and int((entities[id] as Dictionary)["team"]) == team and int((entities[id] as Dictionary)["health"]) > 0 and int((entities[id] as Dictionary).get("knockdown_ticks", 0)) <= 0 and winner == -1


func _is_melee_attacker(row: Dictionary) -> bool:
	## Melee-vs-ranged discriminator for flyer targeting (see the threshold
	## constant): source-unit weapon range is the honest, scale-invariant
	## signal present on every converted unit rule.
	return float(row.get("attack_range_source", 0.0)) < MELEE_ATTACK_RANGE_SOURCE_THRESHOLD


func _can_engage_battalion(attacker: Dictionary, target: Dictionary) -> bool:
	## Flyers soar out of melee reach: only ranged weapons can acquire or hit
	## an airborne battalion.
	return not bool(target.get("presentation_hidden", false)) and not (bool(target.get("flying", false)) and _is_melee_attacker(attacker))


func _is_living_player(id: int) -> bool:
	# Control groups are per-seat presentation state: the guest's groups hold
	# the guest's own living units, never a hardcoded team 0.
	return entities.has(id) and int((entities[id] as Dictionary)["team"]) == local_seat_team and int((entities[id] as Dictionary)["health"]) > 0


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
	_movement_subsystem()._clear_pending_route(row, settle_destination)


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
		if entity_row.has("ai_route_backoff"):
			(rows[-1] as Dictionary)["ai_route_backoff"] = (entity_row.get("ai_route_backoff", {}) as Dictionary).duplicate(true)
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
		var snapshot_structure := structure_rows[-1] as Dictionary
		var gate_behavior := structure_row.get("gate_behavior", {}) as Dictionary
		if not gate_behavior.is_empty():
			# Resting gate state is gameplay state, not merely the transition event.
			# Keep the explicit field list deterministic and leave non-gates absent.
			snapshot_structure["gate_behavior"] = {
				"open": bool(gate_behavior.get("open", false)),
				"pathing_open": bool(gate_behavior.get("pathing_open", false)),
				"open_fraction": snappedf(float(gate_behavior.get("open_fraction", 0.0)), 0.001),
				"close_tick": int(gate_behavior.get("close_tick", -1)),
			}
		if typeof(structure_row.get("attack")) == TYPE_DICTIONARY:
			snapshot_structure["attack"] = (structure_row["attack"] as Dictionary).duplicate(true)
		if String(structure_row.get("spawned_weapon_object_id", "")) != "":
			snapshot_structure["spawned_weapon_object_id"] = String(structure_row["spawned_weapon_object_id"])
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
	var building_permission_rows: Array[Array] = []
	var permission_teams: Array = building_permissions_by_team.keys()
	permission_teams.sort()
	for team_value in permission_teams:
		var permissions: Dictionary = building_permissions_by_team[team_value]
		var object_types: Array = permissions.keys()
		object_types.sort()
		for object_type_value in object_types:
			building_permission_rows.append([
				int(team_value),
				String(object_type_value),
				bool(permissions[object_type_value]),
			])
	var hero_rank_history: Array[Array] = []
	var hero_rank_teams: Array = _hero_peak_ranks_by_team.keys()
	hero_rank_teams.sort()
	for team_value in hero_rank_teams:
		var team := int(team_value)
		var team_history: Dictionary = _hero_peak_ranks_by_team[team_value]
		var identities: Array = team_history.keys()
		identities.sort()
		for identity_value in identities:
			hero_rank_history.append([
				team,
				String(identity_value),
				int(team_history[identity_value]),
			])
	# Roster-ordered per-team arrays (N-team foundation). For the default {0,1}
	# roster these serialize byte-identically to the prior 2-element literals —
	# same order, same values — so the pinned battle signature does not move.
	var resources_row: Array = []
	var command_points_row: Array = []
	var command_point_override_rows: Array = []
	var next_dynamic_ids_row: Array = []
	var power_points_row: Array = []
	var purchased_powers_row: Array = []
	var team_upgrades_row: Array = []
	for team in _roster_team_ids():
		resources_row.append(resources_for_team(team))
		command_points_row.append(command_points_for_team(team))
		if command_point_overrides_by_team.has(team):
			var override: Dictionary = command_point_overrides_by_team[team]
			command_point_override_rows.append([
				team, int(override["total"]), int(override["maximum"])
			])
		next_dynamic_ids_row.append(int(_next_dynamic_id.get(team, 10 + team * 100)))
		power_points_row.append(power_points(team))
		purchased_powers_row.append((purchased_powers.get(team, []) as Array).duplicate())
		team_upgrades_row.append((team_upgrades.get(team, {}) as Dictionary).duplicate(true))
	var snapshot_row := {
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
	if not hero_rank_history.is_empty():
		snapshot_row["hero_peak_ranks"] = hero_rank_history
	if not building_permission_rows.is_empty():
		snapshot_row["building_permissions"] = building_permission_rows
	if not command_point_override_rows.is_empty():
		snapshot_row["command_point_overrides"] = command_point_override_rows
	# Garrison/transport containment (L4): empty-is-absent, sorted ids, so a
	# non-castle match hashes byte-identically and a garrisoned horde is part
	# of the lockstep signature directly (not only via its snapped position).
	if not containment.is_empty():
		var containment_rows: Array = []
		var carrier_ids: Array = containment.keys()
		carrier_ids.sort()
		for carrier_id in carrier_ids:
			var occupant_ids: Array = (containment[carrier_id] as Array).duplicate()
			occupant_ids.sort()
			containment_rows.append([int(carrier_id), occupant_ids])
		snapshot_row["containment"] = containment_rows
	return snapshot_row


func state_signature() -> String:
	return _persistence_subsystem().state_signature()


func toggle_gate(team: int, structure_id: int) -> Dictionary:
	if not structures.has(structure_id):
		return {"ok": false, "reason": "gate-missing"}
	var gate: Dictionary = structures[structure_id]
	if int(gate.get("team", -1)) != team:
		return {"ok": false, "reason": "not-owner"}
	if int(gate.get("health", 0)) <= 0:
		return {"ok": false, "reason": "gate-destroyed"}
	var policy: Dictionary = gate.get("gate_behavior", {})
	if policy.is_empty():
		return {"ok": false, "reason": "typed-gate-contract-missing"}
	if bool(policy.get("open", false)):
		policy["open"] = false
		policy["pathing_open"] = false
		policy["open_fraction"] = 0.0
		policy["close_tick"] = -1
		gate["gate_behavior"] = policy
		_emit_event("gate.closed", structure_id, 0, {"manual": true})
		_sync_gate_passage(structure_id)
		return {"ok": true, "reason": "", "open": false}
	var opened := request_gate_open(structure_id)
	if not bool(opened.get("ok", false)):
		return opened
	policy = gate.get("gate_behavior", {})
	policy["close_tick"] = -1
	gate["gate_behavior"] = policy
	return {"ok": true, "reason": "", "open": true}


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
		"issue_garrison":
			last_command_result = issue_garrison(team, int(args.get("entity_id", 0)), int(args.get("structure_id", 0)))
		"issue_exit_garrison":
			last_command_result = issue_exit_garrison(team, int(args.get("entity_id", 0)))
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
			last_command_result = cast_ability(int(args.get("hero_id", 0)), String(args.get("ability_id", "")), Vector2(args.get("target_point", Vector2.ZERO)), team)
		"set_structure_rally":
			last_command_result = set_structure_rally(team, int(args.get("structure_id", 0)), Vector2(args.get("position", Vector2.ZERO)))
		"toggle_gate":
			last_command_result = toggle_gate(team, int(args.get("structure_id", 0)))
		"sell_structure":
			last_command_result = sell_structure(team, int(args.get("structure_id", 0)))
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
	return _persistence_subsystem().state_hash()


func snapshot() -> PackedByteArray:
	return _persistence_subsystem().snapshot()


func restore(bytes: PackedByteArray) -> bool:
	return _persistence_subsystem().restore(bytes)


func _state_hash_static_keys() -> Array[String]:
	return _persistence_subsystem().state_hash_static_keys()


func _authoritative_state() -> Dictionary:
	return _persistence_subsystem().authoritative_state()


func _restore_authoritative_state(state: Dictionary) -> void:
	_persistence_subsystem().restore_authoritative_state(state)


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
	return _persistence_subsystem().canonicalize(value)


func _canonical_key_less(a: Variant, b: Variant) -> bool:
	return _persistence_subsystem().canonical_key_less(a, b)


var script_surface_bag: Dictionary = {}  # generic key->value residual stores


func surface_bag_get(key: String, default: Variant = null) -> Variant:
	return script_surface_bag.get(key, default)


func surface_bag_set(key: String, value: Variant) -> void:
	script_surface_bag[key] = value


func surface_bag_bool(key: String, default: bool = false) -> bool:
	return bool(script_surface_bag.get(key, default))


func surface_bag_int(key: String, default: int = 0) -> int:
	return int(script_surface_bag.get(key, default))


func surface_bag_inc(key: String, amount: int = 1) -> int:
	var next := surface_bag_int(key) + amount
	script_surface_bag[key] = next
	return next


func surface_bag_dict(key: String) -> Dictionary:
	var row: Dictionary = script_surface_bag.get(key, {}) as Dictionary
	return row


func surface_bag_put_dict(key: String, row: Dictionary) -> void:
	script_surface_bag[key] = row


func script_teleport_entity(entity_id: int, position: Vector2) -> Dictionary:
	if winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if not entities.has(entity_id):
		return {"ok": false, "reason": "entity %d missing" % entity_id}
	var row := entities[entity_id] as Dictionary
	row["position"] = position
	entities[entity_id] = row
	return {"ok": true, "reason": ""}


func script_spawn_entity(unit_type: String, team: int, at: Vector2, scenario_surface: String = "script-spawn") -> Dictionary:
	if winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if not _script_owner_exists(team):
		return {"ok": false, "reason": "team %d unavailable" % team}
	var scenario_contract := scenario_spawn_contract(unit_type, scenario_surface)
	var scenario_kind := String(scenario_contract.get("kind", ""))
	if scenario_kind in ["structure", "prop"]:
		var scenario_result := spawn_scenario_object(
			unit_type, team, at, scenario_surface
		)
		if bool(scenario_result.get("ok", false)):
			var result_id := int(scenario_result.get("id", -1))
			var script_result := {
				"ok": true,
				"object_kind": scenario_kind,
				"object_id": result_id,
				"reason": "",
			}
			script_result["structure_id" if scenario_kind == "structure" else "prop_id"] = result_id
			return script_result
		return {"ok": false, "reason": String(scenario_result.get("reason", "scenario-spawn-failed"))}
	var eid := spawn_script_object(unit_type, team, at, false, scenario_surface)
	if eid > 0:
		return {"ok": true, "entity_id": eid, "reason": ""}
	## Fallback for script-surface probes: allocate a minimal living entity row
	## when the content pack has no full unit rule for the type. Marks the row
	## as script-spawned so later systems can distinguish it from authored units.
	if unit_type == "":
		return {"ok": false, "reason": "empty unit type"}
	# A known scenario identity must never fall through to the legacy synthetic
	# script probe when its document is malformed, the surface is wrong, or it is
	# a non-unit kind. That would turn a refusal into an invented gameplay unit.
	for key in ["scenario_unit_runtimes", "scenario_structure_runtimes", "scenario_prop_runtimes"]:
		var registry_value: Variant = _rules.get(key, {})
		if typeof(registry_value) == TYPE_DICTIONARY and not _casefolded_scenario_document(registry_value as Dictionary, unit_type).is_empty():
			return {"ok": false, "reason": "scenario-admission-rejected:%s:%s" % [unit_type, scenario_surface]}
	if _scenario_runtime_tables_present():
		return {"ok": false, "reason": "scenario-document-missing:%s:%s" % [unit_type, scenario_surface]}
	if not _next_dynamic_id.has(team):
		_next_dynamic_id[team] = 900000 + team * 1000
	eid = int(_next_dynamic_id[team])
	_next_dynamic_id[team] = eid + 1
	entities[eid] = {
		"id": eid,
		"team": team,
		"position": at,
		"unit_type": unit_type,
		"health": 100,
		"maximum_health": 100,
		"state": "idle",
		"script_spawned": true,
	}
	return {"ok": true, "entity_id": eid, "reason": ""}


func transfer_entities_to_team(entity_ids: Array, new_team: int) -> Dictionary:
	for id_value in entity_ids:
		var result := set_entity_team(int(id_value), new_team)
		if not bool(result.get("ok", false)):
			return result
	return {"ok": true, "reason": ""}

static func _structure_contracts_with_passive_area_resolution(
	document: Dictionary, contracts: Array
) -> Array:
	## Opaque moduleContracts preserve EffectRadius's authored define token;
	## playable_structure_compiler also emits the resolved numeric radius in its
	## dedicated passiveAreaEffect contract. Merge those two receipts before the
	## runtime indexes the module, without changing the underlying document.
	var output := contracts.duplicate(true)
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var gameplay: Dictionary = registration.get("gameplay", {}) as Dictionary
	var passive_value: Variant = gameplay.get("passiveAreaEffect")
	if typeof(passive_value) != TYPE_DICTIONARY:
		return output
	var passive := passive_value as Dictionary
	var index := -1
	for contract_index in output.size():
		if String((output[contract_index] as Dictionary).get("module", "")) == "PassiveAreaEffectBehavior":
			index = contract_index
			break
	if index < 0:
		output.append({
			"module": "PassiveAreaEffectBehavior",
			"fields": {},
			"runtime_status": "deferred",
			"source_ini": String(passive.get("sourceIni", "")),
			"line": int(passive.get("line", 0)),
			"tag": "",
			"executable": false,
		})
		index = output.size() - 1
	var contract := (output[index] as Dictionary).duplicate(true)
	var fields := (contract.get("fields", {}) as Dictionary).duplicate(true)
	var radius := float(passive.get("radius", 0.0))
	if radius > 0.0:
		fields["EffectRadius"] = {
			"authored": String(passive.get("radiusAuthored", radius)),
			"value": radius,
		}
	if (
		not fields.has("HealPercentPerSecond")
		and String(passive.get("healPercentPerSecondAuthored", "")) != ""
	):
		var heal_text := String(passive.get("healPercentPerSecondAuthored", ""))
		# The dedicated structure contract stores the numeric percent token
		# without its trailing sign in current packs ("2" for authored "2%").
		if heal_text.is_valid_float():
			heal_text += "%"
		fields["HealPercentPerSecond"] = {
			"authored": heal_text,
		}
	if String(passive.get("upgradeRequired", "")) != "":
		fields["UpgradeRequired"] = {"authored": String(passive.get("upgradeRequired", ""))}
	if typeof(passive.get("modifier")) == TYPE_DICTIONARY:
		fields["ResolvedModifier"] = (passive.get("modifier") as Dictionary).duplicate(true)
	contract["fields"] = fields
	output[index] = contract
	return output


static func _passive_area_effect_field(fields: Dictionary, key: String) -> String:
	var raw: Variant = fields.get(key, fields.get(key.to_lower(), null))
	if typeof(raw) == TYPE_DICTIONARY:
		var authored := String((raw as Dictionary).get("authored", ""))
		if authored != "":
			return authored.strip_edges()
		return String((raw as Dictionary).get("value", "")).strip_edges()
	if typeof(raw) in [TYPE_STRING, TYPE_STRING_NAME, TYPE_INT, TYPE_FLOAT]:
		return String(raw).strip_edges()
	return ""


static func _passive_area_effect_number(fields: Dictionary, key: String) -> float:
	var raw: Variant = fields.get(key, fields.get(key.to_lower(), null))
	if typeof(raw) == TYPE_DICTIONARY:
		var row := raw as Dictionary
		if typeof(row.get("value")) in [TYPE_INT, TYPE_FLOAT]:
			return float(row.get("value"))
	var text := _passive_area_effect_field(fields, key)
	return float(text) if text.is_valid_float() else 0.0


static func _passive_area_effect_percent(text: String) -> float:
	var value := text.strip_edges()
	if not value.ends_with("%"):
		return 0.0
	value = value.trim_suffix("%").strip_edges()
	return float(value) / 100.0 if value.is_valid_float() else 0.0


static func _passive_area_effect_yes(fields: Dictionary, key: String) -> bool:
	return _passive_area_effect_field(fields, key).to_lower() in ["yes", "true", "1"]



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
const UpgradesSystemScript = preload("res://src/retail_slice/retail_sim_upgrades.gd")
const PowersSystemScript = preload("res://src/retail_slice/retail_sim_powers.gd")
const BasesSystemScript = preload("res://src/retail_slice/retail_sim_bases.gd")
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


func _spatial_axis_cell(value: float) -> int:
	return clampi(floori(value / SPATIAL_CELL_SIZE), -SPATIAL_CELL_LIMIT, SPATIAL_CELL_LIMIT)


func _spatial_key(cx: int, cy: int) -> int:
	return cx * SPATIAL_CELL_STRIDE + cy


func _spatial_rebuild() -> void:
	## Full O(n) rebuild. Runs once per tick before anything queries, so the
	## index cannot drift from `entities` across restore/spawn/despawn seams.
	_spatial_cells.clear()
	_spatial_entity_cell.clear()
	_spatial_entity_team.clear()
	_spatial_team_box.clear()
	_spatial_hostile_teams.clear()
	for key in entities.keys():
		var row := entities[key] as Dictionary
		if not bool(row.get("presentation_hidden", false)):
			_spatial_sync(row)


func _spatial_sync(row: Dictionary) -> void:
	## File `row` under the cell its current position falls in, moving it out of
	## its previous cell if it changed. Called after every position write.
	var id := int(row.get("id", 0))
	if id == 0:
		return
	var team := int(row.get("team", -1))
	var position := Vector2(row.get("position", Vector2.ZERO))
	var cx := _spatial_axis_cell(position.x)
	var cy := _spatial_axis_cell(position.y)
	var key := _spatial_key(cx, cy)
	var previous: Variant = _spatial_entity_cell.get(id)
	if previous != null:
		var previous_team := int(_spatial_entity_team.get(id, team))
		if int(previous) == key and previous_team == team:
			return
		var previous_cells: Dictionary = _spatial_cells.get(previous_team, {}) as Dictionary
		var old_bucket: Array = previous_cells.get(int(previous), []) as Array
		old_bucket.erase(id)
		if old_bucket.is_empty():
			previous_cells.erase(int(previous))
	if not _spatial_cells.has(team):
		_spatial_cells[team] = {}
		# A team appearing for the first time (first summon, first creep spawn)
		# invalidates the cached hostile-team lists: a battalion stepped later in
		# this same tick must be able to acquire it, exactly as the old full scan
		# over `entities` would have.
		_spatial_hostile_teams.clear()
	var cells: Dictionary = _spatial_cells[team]
	if not cells.has(key):
		cells[key] = []
	(cells[key] as Array).append(id)
	_spatial_entity_cell[id] = key
	_spatial_entity_team[id] = team
	var box: Variant = _spatial_team_box.get(team)
	if box == null:
		_spatial_team_box[team] = [cx, cx, cy, cy]
	else:
		var extents: Array = box as Array
		extents[0] = mini(int(extents[0]), cx)
		extents[1] = maxi(int(extents[1]), cx)
		extents[2] = mini(int(extents[2]), cy)
		extents[3] = maxi(int(extents[3]), cy)


func _spatial_hostile_team_list(team: int) -> Array:
	## Teams hostile to `team` that currently have indexed battalions. Cached per
	## rebuild; _is_hostile() is then paid once per team rather than per candidate.
	var cached: Variant = _spatial_hostile_teams.get(team)
	if cached != null:
		return cached as Array
	var result: Array = []
	for other_value in _spatial_cells.keys():
		var other := int(other_value)
		if _is_hostile(team, other):
			result.append(other)
	result.sort()
	_spatial_hostile_teams[team] = result
	return result


func _spatial_gather(point: Vector2, radius: float) -> Array[int]:
	## Every indexed id, on any team, whose cell overlaps the axis-aligned box
	## around the disc. A conservative superset: callers re-apply the exact
	## distance test. The returned order is unspecified - sort it when order is
	## observable.
	var result: Array[int] = []
	if radius < 0.0:
		return result
	for team_value in _spatial_cells.keys():
		var box: Variant = _spatial_team_box.get(int(team_value))
		if box == null:
			continue
		var extents: Array = box as Array
		var low_cx := maxi(_spatial_axis_cell(point.x - radius), int(extents[0]))
		var high_cx := mini(_spatial_axis_cell(point.x + radius), int(extents[1]))
		var low_cy := maxi(_spatial_axis_cell(point.y - radius), int(extents[2]))
		var high_cy := mini(_spatial_axis_cell(point.y + radius), int(extents[3]))
		var cells: Dictionary = _spatial_cells[int(team_value)]
		for cx in range(low_cx, high_cx + 1):
			for cy in range(low_cy, high_cy + 1):
				var bucket: Variant = cells.get(_spatial_key(cx, cy))
				if bucket == null:
					continue
				for id in bucket as Array:
					result.append(int(id))
	return result


func _spatial_gather_sorted(point: Vector2, radius: float) -> Array[int]:
	## _spatial_gather() in ascending id order, for callers whose visit order is
	## observable (damage application, event emission, modifier grants).
	var result := _spatial_gather(point, radius)
	result.sort()
	return result


## Broad-phase for _deflect_around_structures (lane L2b item 6). A castle map
## seeds hundreds of live structures (Carn Dum: 260) and the deflection loop
## used to walk ALL of them per moving entity per tick. Structures never move,
## so a spatial bucket index over their centres stays valid until the table is
## mutated; `_structures_mutation_serial` is bumped at every mutation site and
## the index rebuilds lazily on the first query after a change.
##
## Exactness contract: the gather returns every structure whose blocking disc
## (radius <= STRUCTURE_DEFLECT_GATHER_RADIUS) can overlap the query point, in
## ascending id order — the same visit order as the old full scan. Any centre
## outside the gathered box is further than the maximum radius away, and the
## deflection loop skips those rows with zero side effects, so the result is
## byte-identical to the full scan.
const STRUCTURE_DEFLECT_GATHER_RADIUS := 4.6  # max STRUCTURE_BLOCK_RADIUS (fortress); the footprint corridor only shrinks radii

var _structures_mutation_serial := 0
var _structure_spatial_serial := -1
var _structure_spatial_cells: Dictionary = {}


func _note_structure_table_mutation() -> void:
	_structures_mutation_serial += 1


func _structure_spatial_index() -> Dictionary:
	if _structure_spatial_serial != _structures_mutation_serial:
		_structure_spatial_cells.clear()
		for id_value in structures.keys():
			var row: Dictionary = structures[id_value]
			var position := Vector2(row.get("position", Vector2.ZERO))
			var key := _spatial_key(_spatial_axis_cell(position.x), _spatial_axis_cell(position.y))
			if not _structure_spatial_cells.has(key):
				_structure_spatial_cells[key] = []
			(_structure_spatial_cells[key] as Array).append(int(id_value))
		_structure_spatial_serial = _structures_mutation_serial
	return _structure_spatial_cells


func _structure_ids_near(position: Vector2) -> Array[int]:
	return _structure_ids_within_gather_radius(position, STRUCTURE_DEFLECT_GATHER_RADIUS)


func _structure_ids_within_gather_radius(position: Vector2, gather_radius: float) -> Array[int]:
	var index := _structure_spatial_index()
	var low_cx := _spatial_axis_cell(position.x - gather_radius)
	var high_cx := _spatial_axis_cell(position.x + gather_radius)
	var low_cy := _spatial_axis_cell(position.y - gather_radius)
	var high_cy := _spatial_axis_cell(position.y + gather_radius)
	var result: Array[int] = []
	for cx in range(low_cx, high_cx + 1):
		for cy in range(low_cy, high_cy + 1):
			var bucket: Variant = index.get(_spatial_key(cx, cy))
			if bucket == null:
				continue
			for id_value in bucket as Array:
				result.append(int(id_value))
	result.sort()
	return result


## Effectively unbounded search range for callers that scanned every hostile.
## The sweep is still cheap: ring_limit below is clamped to the union of the
## hostile teams' occupied cell boxes, so this bounds the tie-break, not the work.
const SPATIAL_UNBOUNDED_RANGE := 1.0e9


func _spatial_nearest_hostile(
	source: Dictionary, team: int, origin: Vector2, limit: float, filters: int,
	prefer_lowest_id: bool = false
) -> int:
	## Nearest living hostile battalion within `limit` of `origin`, reproducing
	## the old full scan exactly.
	##
	## The old scans walked ascending ids with `if distance <= best`, so the
	## winner is the minimum distance with the HIGHEST id among exact ties, and
	## `best` starting at `limit` means a candidate at exactly `limit` is
	## accepted. The update rule below encodes that as a total order, which makes
	## the result independent of visit order and therefore safe to compute from
	## an expanding ring sweep.
	if limit <= 0.0:
		return 0
	var hostile_teams := _spatial_hostile_team_list(team)
	if hostile_teams.is_empty():
		return 0
	# Union of the hostile teams' occupied boxes: nothing outside it can match,
	# so the sweep is clipped to it and the ring count is capped by it.
	var box_min_cx := 0
	var box_max_cx := -1
	var box_min_cy := 0
	var box_max_cy := -1
	for team_value in hostile_teams:
		var extents: Array = _spatial_team_box.get(int(team_value), []) as Array
		if extents.is_empty():
			continue
		if box_max_cx < box_min_cx:
			box_min_cx = int(extents[0])
			box_max_cx = int(extents[1])
			box_min_cy = int(extents[2])
			box_max_cy = int(extents[3])
		else:
			box_min_cx = mini(box_min_cx, int(extents[0]))
			box_max_cx = maxi(box_max_cx, int(extents[1]))
			box_min_cy = mini(box_min_cy, int(extents[2]))
			box_max_cy = maxi(box_max_cy, int(extents[3]))
	if box_max_cx < box_min_cx:
		return 0

	var best_id := 0
	var best_distance := limit
	var origin_cx := _spatial_axis_cell(origin.x)
	var origin_cy := _spatial_axis_cell(origin.y)
	# Hoisted filter state: _can_engage_battalion() only consults the candidate's
	# `flying` flag when the source is a melee attacker, and _stealth_active() is
	# a tick comparison. Both are resolved once here so the inner loop over
	# candidates makes no function calls beyond the distance itself.
	var reject_flyers := (filters & SPATIAL_FILTER_NOT_FLYING) != 0
	if (filters & SPATIAL_FILTER_ENGAGE) != 0 and _is_melee_attacker(source):
		reject_flyers = true
	var check_stealth := (filters & SPATIAL_FILTER_STEALTH) != 0
	# Rings beyond this are entirely outside `limit` or outside the occupied box.
	var ring_limit := floori(limit / SPATIAL_CELL_SIZE) + 2
	ring_limit = mini(ring_limit, maxi(
		maxi(absi(origin_cx - box_min_cx), absi(origin_cx - box_max_cx)),
		maxi(absi(origin_cy - box_min_cy), absi(origin_cy - box_max_cy))
	))
	for ring in range(0, ring_limit + 1):
		# Every point of a ring-`ring` cell is at least (ring - 1) * cell away
		# from `origin`, so once that floor passes the best distance found, no
		# further ring can contain a candidate that wins the tie-break.
		if ring > 0 and float(ring - 1) * SPATIAL_CELL_SIZE > best_distance:
			break
		var offsets := _spatial_ring_offsets(ring)
		var offset_index := 0
		var offset_count := offsets.size()
		while offset_index < offset_count:
			var cx: int = origin_cx + offsets[offset_index]
			var cy: int = origin_cy + offsets[offset_index + 1]
			offset_index += 2
			if cx < box_min_cx or cx > box_max_cx:
				continue
			if cy < box_min_cy or cy > box_max_cy:
				continue
			var cell_key := _spatial_key(cx, cy)
			for team_value in hostile_teams:
				var bucket: Variant = (_spatial_cells[int(team_value)] as Dictionary).get(cell_key)
				if bucket == null:
					continue
				for id_value in bucket as Array:
					var candidate := int(id_value)
					var candidate_row: Variant = entities.get(candidate)
					if candidate_row == null:
						continue
					var candidate_dict: Dictionary = candidate_row
					if int(candidate_dict.get("health", 0)) <= 0:
						continue
					if reject_flyers and bool(candidate_dict.get("flying", false)):
						continue
					var distance := origin.distance_to(Vector2(candidate_dict.get("position", Vector2.ZERO)))
					if check_stealth and _stealth_active(candidate_dict):
						var detection_source := float(candidate_dict.get("invisibility_detection_range_source", -1.0))
						var detection_range := detection_source * float(_rules.get("source_unit_scale", 0.1))
						if detection_source < 0.0 or distance > detection_range:
							continue
					var wins := distance < best_distance
					if not wins and distance == best_distance:
						# Exact equality, never is_equal_approx: a tolerance
						# comparison is not transitive, so it cannot define the
						# total order a ring sweep needs.
						wins = (candidate < best_id or best_id == 0) if prefer_lowest_id else (candidate > best_id)
					if wins:
						best_distance = distance
						best_id = candidate
	return best_id


func _spatial_ring_offsets(ring: int) -> PackedInt32Array:
	## Cell offsets at Chebyshev distance exactly `ring`, as interleaved dx/dy.
	## Walked as a perimeter so the sweep stays O(ring) per ring rather than
	## O(ring^2), and cached because the table never varies.
	while _spatial_ring_cache.size() <= ring:
		var index := _spatial_ring_cache.size()
		var offsets := PackedInt32Array()
		if index == 0:
			offsets.append(0)
			offsets.append(0)
		else:
			for dx in range(-index, index + 1):
				offsets.append(dx)
				offsets.append(-index)
				offsets.append(dx)
				offsets.append(index)
			for dy in range(-index + 1, index):
				offsets.append(-index)
				offsets.append(dy)
				offsets.append(index)
				offsets.append(dy)
		_spatial_ring_cache.append(offsets)
	return _spatial_ring_cache[ring]


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


func _configure_ring_mechanic_contract() -> void:
	_ring_contract = (_rules.get("ring_system", {}) as Dictionary).duplicate(true)
	if String(_ring_contract.get("schema", "")) == "openbfme.ring-system-runtime":
		var compiled_contract := _compiled_ring_runtime_contract(_ring_contract)
		if compiled_contract.is_empty():
			configuration_error = "Compiled ring-system runtime registration is invalid"
		else:
			_ring_contract = compiled_contract
	for registry_key in ["ring_runtime_documents", "playable_unit_runtimes"]:
		var registry: Variant = _rules.get(registry_key, {})
		if typeof(registry) != TYPE_DICTIONARY:
			continue
		for document_value in (registry as Dictionary).values():
			if typeof(document_value) != TYPE_DICTIONARY:
				continue
			var mechanic: Dictionary = (document_value as Dictionary).get("ringMechanic", {}) as Dictionary
			for block_name in ["gollum", "ring"]:
				var block: Variant = mechanic.get(block_name, {})
				if typeof(block) == TYPE_DICTIONARY:
					_ring_contract.merge(block as Dictionary, false)
					if not (block as Dictionary).is_empty():
						var document_object_id := String((document_value as Dictionary).get("objectId", ""))
						if document_object_id != "":
							_ring_contract["gollumObjectId" if block_name == "gollum" else "ringObjectId"] = document_object_id
						if block_name == "ring" and (block as Dictionary).has("scanRange"):
							_ring_contract["pickupRange"] = float((block as Dictionary)["scanRange"])
	_ring_delivery_kinds.clear()
	for kind_value in (_rules.get("ring_delivery_structure_kinds", []) as Array):
		_ring_delivery_kinds[String(kind_value)] = true
	var producer_kinds: Dictionary = _rules.get("producer_kind_by_source_object", {}) as Dictionary
	var structure_runtimes: Variant = _rules.get("playable_structure_runtimes", {})
	if typeof(structure_runtimes) == TYPE_DICTIONARY:
		for document_value in (structure_runtimes as Dictionary).values():
			if typeof(document_value) != TYPE_DICTIONARY:
				continue
			var document := document_value as Dictionary
			var delivery: Variant = (document.get("ringMechanic", {}) as Dictionary).get("delivery", {})
			if typeof(delivery) != TYPE_DICTIONARY or (delivery as Dictionary).is_empty():
				continue
			var kind := String(producer_kinds.get(String(document.get("objectId", "")), ""))
			if kind != "":
				_ring_delivery_kinds[kind] = (delivery as Dictionary).duplicate(true)
	if ring_mechanic_enabled and _ring_contract.is_empty():
		_ring_contract = {
			"waypointFamily": "SpawnPoint_SkirmishGollum_", "spawnTeam": CREEP_TEAM,
			"modeToken": "ringheroes", "gollumObjectId": RING_DEFAULT_GOLLUM,
			"ringObjectId": RING_DEFAULT_ITEM, "pickupRange": 10.0,
			"deliveryRange": 10.0, "status": "HOLDING_THE_RING",
		}
		print("[RetailSliceSim] RING_CONTRACT_LIMITATION stale-pack-no-data-ring-system; using named retail constants until data/ring/system.json is shipped")


func _compiled_ring_runtime_contract(runtime: Dictionary) -> Dictionary:
	## Consume the importer's canonical openbfme.ring-system-runtime envelope.
	## This is intentionally a projection of its registration, not a second
	## hand-authored Gollum table in the sim.
	if int(runtime.get("schemaVersion", -1)) != 0:
		return {}
	var registration_value: Variant = runtime.get("registration", {})
	if typeof(registration_value) != TYPE_DICTIONARY:
		return {}
	var registration := registration_value as Dictionary
	var system_value: Variant = registration.get("system", {})
	var objects_value: Variant = registration.get("objects", {})
	if typeof(system_value) != TYPE_DICTIONARY or typeof(objects_value) != TYPE_DICTIONARY:
		return {}
	var system := system_value as Dictionary
	var objects := objects_value as Dictionary
	var spawn_value: Variant = system.get("spawn", {})
	if typeof(spawn_value) != TYPE_DICTIONARY:
		return {}
	var spawn := spawn_value as Dictionary
	var gollum_id := String(spawn.get("objectId", ""))
	var gollum_value: Variant = objects.get(gollum_id, {})
	if gollum_id == "" or typeof(gollum_value) != TYPE_DICTIONARY:
		return {}
	var gollum := gollum_value as Dictionary
	var parent_id := String(gollum.get("parentObjectId", ""))
	var parent_value: Variant = objects.get(parent_id, {})
	if parent_id == "" or typeof(parent_value) != TYPE_DICTIONARY:
		return {}
	var parent := parent_value as Dictionary
	var locomotors_value: Variant = parent.get("locomotors", {})
	var body_value: Variant = parent.get("body", {})
	var animal_value: Variant = gollum.get("animalAI", {})
	if typeof(locomotors_value) != TYPE_DICTIONARY or typeof(body_value) != TYPE_DICTIONARY \
			or typeof(animal_value) != TYPE_DICTIONARY:
		return {}
	var locomotors := locomotors_value as Dictionary
	var body := body_value as Dictionary
	var animal := animal_value as Dictionary
	if float(locomotors.get("normal", 0.0)) <= 0.0 or int(body.get("maxHealth", 0)) <= 0:
		return {}
	var spawn_team_value: Variant = spawn.get("team", "")
	var spawn_team := CREEP_TEAM if String(spawn_team_value) == "PlyrCreeps" else int(spawn_team_value) if typeof(spawn_team_value) == TYPE_INT else -1
	if spawn_team < 0:
		return {}
	var contract := {
		"waypointFamily": String(spawn.get("waypointFamily", "")),
		"spawnTeam": spawn_team,
		"modeToken": String(system.get("modeToken", "")),
		"gollumObjectId": gollum_id,
		"ringObjectId": "TheDroppedRing",
		"evaEvents": {},
		"evaEventCatalog": Array(system.get("evaEvents", [])).duplicate(),
		"heroesByFaction": (system.get("ringHeroesByFaction", {}) as Dictionary).duplicate(true),
		"wanderPercentage": int(animal.get("wanderPercentage", 0)),
		"detectionRange": float((parent.get("camouflage", {}) as Dictionary).get("detectionRange", 0.0)),
		"fleeEnemyRange": float(animal.get("fleeRange", 0.0)),
		"fleeDistance": float(animal.get("fleeDistance", 0.0)),
		"_compiledRegistration": registration.duplicate(true),
	}
	var ring_value: Variant = objects.get("TheDroppedRing", {})
	if typeof(ring_value) == TYPE_DICTIONARY:
		var attach_value: Variant = ((ring_value as Dictionary).get("ringMechanic", {}) as Dictionary).get("attach", {})
		if typeof(attach_value) == TYPE_DICTIONARY:
			var attach := attach_value as Dictionary
			contract["pickupRange"] = float(attach.get("scanRange", 0.0))
			contract["status"] = String(attach.get("parentStatus", ""))
			contract["attachFilter"] = (attach.get("filter", {}) as Dictionary).duplicate(true)
	return contract


func _ring_state() -> Dictionary:
	if not script_surface_bag.has("ring_mechanic") \
			or typeof(script_surface_bag["ring_mechanic"]) != TYPE_DICTIONARY:
		script_surface_bag["ring_mechanic"] = {
			"gollum_id": 0, "gollum_spawned": false, "ring_active": false,
			"ring_position": Vector2.ZERO, "carrier_id": 0,
		}
	return script_surface_bag["ring_mechanic"] as Dictionary


func _mark_ring_delivery_structures() -> void:
	for structure_id in structure_ids():
		_mark_ring_delivery_structure(structures[structure_id] as Dictionary)


func _mark_ring_delivery_structure(row: Dictionary) -> void:
	if not ring_mechanic_enabled:
		return
	var kind := String(row.get("structure_kind", ""))
	if _ring_delivery_kinds.has(kind):
		row["ring_delivery"] = (_ring_delivery_kinds[kind] as Dictionary).duplicate(true) \
			if typeof(_ring_delivery_kinds[kind]) == TYPE_DICTIONARY else {}


func _ring_gollum_object_id() -> String:
	return String(_ring_contract.get("gollumObjectId", RING_DEFAULT_GOLLUM))


func _ring_spawn_team() -> int:
	var value: Variant = _ring_contract.get("spawnTeam", CREEP_TEAM)
	return int(value) if typeof(value) in [TYPE_INT, TYPE_FLOAT] else CREEP_TEAM


func _ring_eva(name: String) -> String:
	return String((_ring_contract.get("evaEvents", {}) as Dictionary).get(name, ""))


func ring_presentation_contract() -> Dictionary:
	var offset_value: Variant = _ring_contract.get("carrierOffsetSource", Vector2(0.0, -10.0))
	var offset := Vector2(0.0, -10.0)
	if typeof(offset_value) == TYPE_VECTOR2:
		offset = offset_value as Vector2
	elif typeof(offset_value) == TYPE_ARRAY and (offset_value as Array).size() >= 2:
		offset = Vector2(float((offset_value as Array)[0]), float((offset_value as Array)[1]))
	elif typeof(offset_value) == TYPE_DICTIONARY:
		var offset_row := offset_value as Dictionary
		offset = Vector2(float(offset_row.get("x", 0.0)), float(offset_row.get("y", -10.0)))
	return {
		"enabled": ring_mechanic_enabled,
		"object_id": String(_ring_contract.get("ringObjectId", RING_DEFAULT_ITEM)),
		"status": String(_ring_contract.get("status", "HOLDING_THE_RING")),
		"offset_source": offset,
	}


func _is_ring_gollum(row: Dictionary) -> bool:
	var wanted := _ring_gollum_object_id()
	return String(row.get("object_id", "")) == wanted \
		or String(row.get("unit_type", "")) == wanted \
		or String(row.get("source_object_id", "")) == wanted


func _existing_ring_gollum_id() -> int:
	for entity_id in entity_ids():
		var row: Dictionary = entities[entity_id]
		if _is_ring_gollum(row) and int(row.get("health", 0)) > 0:
			return entity_id
	return 0


func _spawn_ring_gollum_fallback() -> void:
	var state := _ring_state()
	if bool(state.get("gollum_spawned", false)):
		return
	var existing := _existing_ring_gollum_id()
	if existing != 0:
		state["gollum_id"] = existing
		state["gollum_spawned"] = true
		return
	var family := String(_ring_contract.get("waypointFamily", "SpawnPoint_SkirmishGollum_"))
	var rolled := logic_random_int(1, 8)
	var waypoint := "%s%d" % [family, rolled]
	var at: Vector2
	if script_waypoints.has(waypoint):
		at = Vector2(script_waypoints[waypoint])
	else:
		var candidates: Array[String] = []
		for name_value in script_waypoints.keys():
			if String(name_value).begins_with(family):
				candidates.append(String(name_value))
		candidates.sort()
		if not candidates.is_empty():
			waypoint = candidates[posmod(rolled - 1, candidates.size())]
			at = Vector2(script_waypoints[waypoint])
		else:
			waypoint = "fallback-map-centre"
			at = Vector2.ZERO
	push_warning("RING_GOLLUM_FALLBACK scripted spawn absent after tick %d; deterministic fallback waypoint=%s roll=%d" % [RING_FALLBACK_TICK, waypoint, rolled])
	var gollum_id := spawn_script_object(
		_ring_gollum_object_id(), _ring_spawn_team(), at, true
	)
	if gollum_id <= 0:
		push_error("RING_GOLLUM_FALLBACK_FAILED stale pack lacks playable Gollum rule (%s)" % _ring_gollum_object_id())
		state["gollum_spawned"] = true
		state["limitation"] = "stale-pack-no-playable-gollum"
		return
	state["gollum_id"] = gollum_id
	state["gollum_spawned"] = true
	_configure_ring_gollum(entities[gollum_id] as Dictionary)
	_emit_event("ring.gollum_spawned", gollum_id, 0, {"waypoint": waypoint, "fallback": true})


func _configure_ring_gollum(row: Dictionary) -> void:
	var source_scale := maxf(0.000001, float(_rules.get("source_map_transform_scale", 1.0)))
	row["ring_gollum"] = true
	row["ring_wander_percentage"] = int(_ring_contract.get("wanderPercentage", 80))
	row["ring_detection_range"] = float(_ring_contract.get("detectionRange", 120.0)) * source_scale
	row["ring_flee_enemy_range"] = float(_ring_contract.get("fleeEnemyRange", 300.0)) * source_scale
	row["ring_flee_distance"] = float(_ring_contract.get("fleeDistance", 800.0)) * source_scale
	if not _stealth_active(row):
		_grant_stealth(row, 0x3FFFFFFF, [])


func _drop_ring(at: Vector2, source_id: int, reason: String) -> void:
	var state := _ring_state()
	var old_carrier := int(state.get("carrier_id", 0))
	if old_carrier != 0 and entities.has(old_carrier):
		set_entity_object_status(old_carrier, String(_ring_contract.get("status", "HOLDING_THE_RING")), false)
	state["ring_active"] = true
	state["ring_position"] = at
	state["carrier_id"] = 0
	_emit_event("ring.dropped", source_id, 0, {"position": at, "reason": reason, "object_id": String(_ring_contract.get("ringObjectId", RING_DEFAULT_ITEM)), "eva": _ring_eva("dropped")})


func _on_ring_entity_death(entity_id: int, row: Dictionary) -> void:
	if not ring_mechanic_enabled:
		return
	var state := _ring_state()
	if entity_id == int(state.get("gollum_id", 0)) or bool(row.get("ring_gollum", false)):
		_drop_ring(Vector2(row.get("position", Vector2.ZERO)), entity_id, "gollum-killed")
	elif entity_id == int(state.get("carrier_id", 0)):
		_drop_ring(Vector2(row.get("position", Vector2.ZERO)), entity_id, "carrier-killed")
	elif bool(row.get("ring_hero", false)):
		_drop_ring(Vector2(row.get("position", Vector2.ZERO)), entity_id, "ring-hero-killed")


func _step_ring_gollum(gollum_id: int) -> void:
	if not entities.has(gollum_id):
		return
	var row: Dictionary = entities[gollum_id]
	if int(row.get("health", 0)) <= 0:
		return
	var position := Vector2(row.get("position", Vector2.ZERO))
	var detect_range := float(row.get("ring_detection_range", 120.0))
	var gollum_team := int(row.get("team", _ring_spawn_team()))
	var detector := _spatial_nearest_hostile(row, gollum_team, position, detect_range, 0, true)
	if detector != 0:
		if _stealth_active(row):
			_clear_stealth(row)
	else:
		if not _stealth_active(row):
			_grant_stealth(row, 0x3FFFFFFF, [])
	var flee_range := float(row.get("ring_flee_enemy_range", 300.0))
	var threat := _spatial_nearest_hostile(row, gollum_team, position, flee_range, 0, true)
	if threat != 0:
		var away := Vector2((entities[threat] as Dictionary).get("position", position)).direction_to(position)
		if away.length_squared() < 0.000001:
			away = Vector2.RIGHT.rotated(float(posmod(gollum_id, 8)) * TAU / 8.0)
		var destination := position + away * float(row.get("ring_flee_distance", 800.0))
		if _assign_route(row, destination):
			row["state"] = "run"
			row["ring_flee_target"] = threat
			_emit_event("ring.gollum_flee", gollum_id, threat, {"destination": destination})
		return
	if not (row.get("route", []) as Array).is_empty() or (tick_index + gollum_id) % 20 != 0:
		return
	if logic_random_int(1, 100) > int(row.get("ring_wander_percentage", 80)):
		return
	var angle := TAU * float(logic_random_int(0, 359)) / 360.0
	var distance := float(logic_random_int(8, 24))
	if _assign_route(row, position + Vector2.RIGHT.rotated(angle) * distance):
		row["state"] = "run"
		row["ring_wander_count"] = int(row.get("ring_wander_count", 0)) + 1
		_emit_event("ring.gollum_wander", gollum_id, 0, {"destination": row.get("destination", position)})


func _step_ring_mechanic() -> void:
	if not ring_mechanic_enabled:
		return
	var state := _ring_state()
	if not bool(state.get("gollum_spawned", false)):
		for entity_id in entity_ids():
			if _is_ring_gollum(entities[entity_id] as Dictionary):
				state["gollum_id"] = entity_id
				state["gollum_spawned"] = true
				_configure_ring_gollum(entities[entity_id] as Dictionary)
				_emit_event("ring.gollum_spawned", entity_id, 0, {"fallback": false})
				break
	if not bool(state.get("gollum_spawned", false)) and tick_index >= RING_FALLBACK_TICK:
		_spawn_ring_gollum_fallback()
	var gollum_id := int(state.get("gollum_id", 0))
	if gollum_id != 0:
		_step_ring_gollum(gollum_id)
	var carrier_id := int(state.get("carrier_id", 0))
	if carrier_id != 0 and entities.has(carrier_id) and int((entities[carrier_id] as Dictionary).get("health", 0)) > 0:
		var carrier: Dictionary = entities[carrier_id]
		state["ring_position"] = Vector2(carrier.get("position", Vector2.ZERO))
		_ensure_parity()
		for team in _roster_team_ids():
			parity.fog_reveal(int(team), state["ring_position"], 1.0, true)
		for structure_id in structure_ids(int(carrier.get("team", -1))):
			var structure: Dictionary = structures[structure_id]
			if not structure.has("ring_delivery"):
				continue
			var delivery: Dictionary = structure.get("ring_delivery", {}) as Dictionary
			var range := float(delivery.get("scanRange", _ring_contract.get("deliveryRange", 10.0))) * maxf(0.000001, float(_rules.get("source_map_transform_scale", 1.0)))
			if Vector2(structure.get("position", Vector2.ZERO)).distance_to(Vector2(carrier.get("position", Vector2.ZERO))) <= range:
				var team := int(carrier.get("team", -1))
				var owned: Dictionary = team_upgrades.get(team, {}) as Dictionary
				owned["Upgrade_RingHero"] = true
				team_upgrades[team] = owned
				var completed: Array = structure.get("completed_upgrades", [])
				if not completed.has("Upgrade_FortressRingHero"):
					completed.append("Upgrade_FortressRingHero")
					completed.sort()
				structure["completed_upgrades"] = completed
				set_entity_object_status(carrier_id, String(_ring_contract.get("status", "HOLDING_THE_RING")), false)
				state["ring_active"] = false
				state["carrier_id"] = 0
				_emit_event("ring.delivered", carrier_id, structure_id, {"team": team, "eva": _ring_eva("delivered")})
				return
	if not bool(state.get("ring_active", false)) or int(state.get("carrier_id", 0)) != 0:
		return
	var ring_position := Vector2(state.get("ring_position", Vector2.ZERO))
	var pickup_range := float(_ring_contract.get("pickupRange", 10.0)) * maxf(0.000001, float(_rules.get("source_map_transform_scale", 1.0)))
	for entity_id in entity_ids():
		var candidate: Dictionary = entities[entity_id]
		if not _ring_pickup_eligible(candidate):
			continue
		if Vector2(candidate.get("position", Vector2.ZERO)).distance_to(ring_position) > pickup_range:
			continue
		state["ring_active"] = false
		state["carrier_id"] = entity_id
		set_entity_object_status(entity_id, String(_ring_contract.get("status", "HOLDING_THE_RING")), true)
		_emit_event("ring.picked_up", entity_id, 0, {"position": ring_position, "eva": _ring_eva("pickedUp")})
		_emit_event("ring.carrier_revealed", entity_id, 0, {"teams": _roster_team_ids()})
		break


func _ring_pickup_eligible(candidate: Dictionary) -> bool:
	## Narrow consumer for the converter's compiled ObjectFilter projection.
	## Unknown optional fields do not broaden admission; the mandatory retail
	## ground/Gollum exclusions are always applied.
	if int(candidate.get("health", 0)) <= 0 or bool(candidate.get("flying", false)) \
			or _is_ring_gollum(candidate):
		return false
	var filter: Dictionary = _ring_contract.get("attachFilter", {}) as Dictionary
	var object_id := String(candidate.get("object_id", ""))
	var category := String(candidate.get("category", ""))
	if (filter.get("excludedObjectIds", []) as Array).has(object_id):
		return false
	if (filter.get("excludedCategories", []) as Array).has(category):
		return false
	return true


func _configure_faction_manifest() -> bool:
	## Every faction-scoped table flows through the manifest. The 8 core
	## tables are REQUIRED (Q80): a rules dictionary without them is refused
	## by name — invented defaults were removed and never fall back silently.
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
	# Manifest keys that are now required (no fallback defaults permitted)
	var required_keys := ["unit_production_rules", "ai_production_plan", "structure_kinds", 
		"structure_max_health", "structure_build_rules", "unit_damage_types", 
		"structure_armor", "spawn_roster"]
	for req_key in required_keys:
		if not manifest.has(req_key):
			configuration_error = "Faction manifest is missing required field '%s' (pack must carry it; invented defaults were removed)" % req_key
			return false
	_unit_production_rules = (manifest.get("unit_production_rules") as Dictionary).duplicate(true)
	var plan: Array = Array(manifest.get("ai_production_plan"))
	var kinds: Array = Array(manifest.get("structure_kinds"))
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
	_structure_max_health = (manifest.get("structure_max_health") as Dictionary).duplicate(true)
	_structure_bounty_values = (manifest.get("structure_bounty_values", {}) as Dictionary).duplicate(true)
	_structure_build_rules = (manifest.get("structure_build_rules") as Dictionary).duplicate(true)
	_unit_damage_types = (manifest.get("unit_damage_types") as Dictionary).duplicate(true)
	# Repopulated from the loaded documents' compiled combat blocks; no manifest
	# constant mirrors it, so it must not survive a previous configure.
	_unit_damage_components = {}
	# Compiled per-kind structure armor (armor.ini via each structure document).
	# Legacy manifests without it keep the FortressArmor mirror with per-line
	# armor.ini provenance; kinds outside the mirror are recorded provisionals.
	_structure_armor = (manifest.get("structure_armor") as Dictionary).duplicate(true)
	_spawn_roster = (manifest.get("spawn_roster") as Array).duplicate(true)
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
	## Every structure kind must have a compiled armor table. Kinds without one
	## are recorded and logged (loud failure on damage application).
	structure_armor_provisional_kinds.clear()
	for kind_value in _structure_kinds:
		var kind := String(kind_value)
		if _structure_armor.has(kind):
			continue
		structure_armor_provisional_kinds.append(kind)
		push_error("[RetailSliceSim] structure armor: kind '%s' has no compiled armor contract; structure damage will be refused for that kind" % kind)


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
	var compiled := {
		"damage_scalar": float((table.get("damageScalar", {}) as Dictionary).get("percent", 100.0)) / 100.0,
		"scalars": scalars,
	}
	## SoldierArmor authors FlankedPenalty = 50% (armor.ini:762). The compiler
	## already emits table.flankedPenalty; dropping it here forced a silent
	## 1.0. Absent stays absent.
	var flanked_value: Variant = table.get("flankedPenalty", table.get("flanked_penalty", null))
	if typeof(flanked_value) == TYPE_DICTIONARY:
		var percent := float((flanked_value as Dictionary).get("percent", 0.0))
		if is_finite(percent) and percent > 0.0:
			compiled["flanked_penalty"] = percent / 100.0
	elif typeof(flanked_value) in [TYPE_FLOAT, TYPE_INT]:
		var raw := float(flanked_value)
		if is_finite(raw) and raw > 0.0:
			compiled["flanked_penalty"] = raw if raw <= 1.0 else raw / 100.0
	return compiled


static func _scenario_structure_kind(document: Dictionary) -> String:
	## Lairs intentionally share one MonsterLair table. Other neutral structures
	## do not: CaptureFlag, Outpost, SignalFire, ruins, and towers can carry
	## different ArmorSets despite sharing the admission role `neutral-structure`.
	return StructureArmorContract.scenario_document_kind(document)


func _scenario_structure_armor_projection(document: Dictionary) -> Dictionary:
	return StructureArmorContract.normalize_registration_armor(document)


func _configure_scenario_structure_armor_projection() -> bool:
	var registry_value: Variant = _rules.get("scenario_structure_runtimes", {})
	if typeof(registry_value) != TYPE_DICTIONARY:
		configuration_error = "Scenario structure runtime registry is not a dictionary"
		return false
	var registry := registry_value as Dictionary
	var object_ids: Array = registry.keys()
	object_ids.sort_custom(func(a, b): return String(a).naturalnocasecmp_to(String(b)) < 0)
	var contracts_by_kind: Dictionary = {}
	var sources_by_kind: Dictionary = {}
	for object_id_value in object_ids:
		var document_value: Variant = registry.get(object_id_value)
		if typeof(document_value) != TYPE_DICTIONARY:
			continue
		var document := document_value as Dictionary
		var kind := _scenario_structure_kind(document)
		if kind == "":
			continue
		var projection := _scenario_structure_armor_projection(document)
		if projection.has("error"):
			configuration_error = "Scenario structure '%s' armor is invalid: %s" % [String(document.get("objectId", object_id_value)), String(projection.get("error", ""))]
			return false
		var contract: Dictionary = (projection.get("table", {}) as Dictionary).duplicate(true) if bool(projection.get("present", false)) else {}
		if contracts_by_kind.has(kind):
			if (contracts_by_kind.get(kind, {}) as Dictionary) != contract:
				configuration_error = "Scenario structure kind collision '%s': %s and %s carry unequal armor contracts" % [kind, String(sources_by_kind.get(kind, "")), String(document.get("objectId", object_id_value))]
				return false
			continue
		contracts_by_kind[kind] = contract
		sources_by_kind[kind] = String(document.get("objectId", object_id_value))
		if contract.is_empty():
			continue
		if _structure_armor.has(kind) and (_structure_armor.get(kind, {}) as Dictionary) != contract:
			configuration_error = "Scenario structure kind collision '%s': faction and %s carry unequal armor contracts" % [kind, String(document.get("objectId", object_id_value))]
			return false
		_structure_armor[kind] = contract.duplicate(true)
	return true


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
			"production-legality":
				# Compiler marker: WeaponSetUpgrade gates production legality
				# without a damage delta. Eligible for the purchase surface;
				# combat resolution treats it as a no-op effect.
				effect["semantic"] = String(upgrade.get("semantic", ""))
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


func _compiled_banner_carrier(document: Dictionary) -> Dictionary:
	## Normalize the compiler's BannerCarriersAllowed contract. A malformed
	## contract refuses registration; it never creates a placeholder flag.
	var gameplay := ((document.get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary)
	var source: Variant = gameplay.get("bannerCarrier")
	if typeof(source) != TYPE_DICTIONARY:
		return {}
	var contract := source as Dictionary
	var allowed_value: Variant = contract.get("allowedObjectIds", [])
	var positions_value: Variant = contract.get("positions", [])
	var min_level_value: Variant = contract.get("minLevel")
	if (
		typeof(allowed_value) != TYPE_ARRAY
		or typeof(positions_value) != TYPE_ARRAY
		or typeof(min_level_value) not in [TYPE_INT, TYPE_FLOAT]
		or int(min_level_value) < 0
		or not is_equal_approx(float(min_level_value), float(int(min_level_value)))
	):
		return {}
	var allowed: Array = allowed_value as Array
	var positions: Array = positions_value as Array
	if allowed.is_empty():
		return {}
	for value in allowed:
		if String(value) == "":
			return {}
	var banner_document: Dictionary = {}
	var db = _content_db_ref()
	if db != null and db.has_method("get_playable_unit_runtime"):
		banner_document = db.get_playable_unit_runtime(String(allowed[0]))
	if banner_document.is_empty():
		push_error("RetailSliceSim banner target '%s' has no converted playable-unit template" % String(allowed[0]))
		return {}
	var banner_simulation := ((banner_document.get("registration", {}) as Dictionary).get("simulation", {}) as Dictionary)
	var resolved := banner_simulation.get("resolved", {}) as Dictionary
	var member_health_value: Variant = resolved.get("memberHealth")
	var banner_max_health := 0
	if typeof(member_health_value) == TYPE_DICTIONARY:
		banner_max_health = int((member_health_value as Dictionary).get("value", 0))
	elif typeof(member_health_value) in [TYPE_INT, TYPE_FLOAT]:
		banner_max_health = int(member_health_value)
	if banner_max_health <= 0:
		push_error("RetailSliceSim banner target '%s' has no authored member health" % String(allowed[0]))
		return {}
	var respawn_ticks := _compiled_banner_respawn_ticks(banner_document)
	var offset := Vector2.ZERO
	if not positions.is_empty():
		if typeof(positions[0]) != TYPE_DICTIONARY:
			return {}
		var position: Dictionary = positions[0] as Dictionary
		if typeof(position.get("x")) not in [TYPE_INT, TYPE_FLOAT] or typeof(position.get("y")) not in [TYPE_INT, TYPE_FLOAT]:
			return {}
		offset = Vector2(float(position["x"]), float(position["y"]))
	return {
		"object_id": PlayableUnitAdapter.runtime_object_id(String(allowed[0])),
		"source_banner_object_id": String(allowed[0]),
		"min_level": int(min_level_value),
		"offset_source": offset,
		"destroy_horde_on_death": bool(contract.get("destroyHordeOnBannerDeath", false)),
		"banner_max_health": banner_max_health,
		"respawn_ticks": respawn_ticks,
	}


func _compiled_banner_respawn_ticks(document: Dictionary) -> int:
	## BannerCarrierUpdate DiedRespawnTime / MeleeFreeBannerReSpawnTime (ms).
	## Returns -1 when retail authored no death respawn timer.
	var gameplay := ((document.get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary)
	var update: Variant = gameplay.get("bannerCarrierUpdate")
	if typeof(update) != TYPE_DICTIONARY:
		return -1
	var contract := update as Dictionary
	var died: Variant = contract.get("diedRespawnTime")
	if typeof(died) != TYPE_DICTIONARY:
		return -1
	var died_ms := int((died as Dictionary).get("milliseconds", -1))
	if died_ms < 0:
		return -1
	var melee_ms := 0
	var melee: Variant = contract.get("meleeFreeBannerRespawnTime")
	if typeof(melee) == TYPE_DICTIONARY:
		melee_ms = maxi(0, int((melee as Dictionary).get("milliseconds", 0)))
	var delay_ms := maxi(died_ms, melee_ms)
	# Match C# PackTemplateLoader: ceil ms -> ticks at TICK_SECONDS.
	return maxi(0, int(ceili(float(delay_ms) / (TICK_SECONDS * 1000.0))))


func _compiled_castle_behavior(document: Dictionary) -> Dictionary:
	## Fail-closed copy of pack gameplay.castleBehavior (BSE pieces).
	var gameplay := ((document.get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary)
	var source: Variant = gameplay.get("castleBehavior")
	if typeof(source) != TYPE_DICTIONARY:
		return {}
	var contract := source as Dictionary
	var pieces_value: Variant = contract.get("pieces")
	if typeof(pieces_value) != TYPE_ARRAY:
		return {}
	var pieces := pieces_value as Array
	if pieces.is_empty() or pieces.size() > 64:
		return {}
	var normalized: Array = []
	for index in pieces.size():
		var row_value: Variant = pieces[index]
		if typeof(row_value) != TYPE_DICTIONARY:
			return {}
		var row := row_value as Dictionary
		if int(row.get("index", -1)) != index:
			return {}
		var object_id := String(row.get("objectId", ""))
		var offset_value: Variant = row.get("offset")
		if object_id == "" or typeof(offset_value) != TYPE_ARRAY or (offset_value as Array).size() != 3:
			return {}
		var offset_arr := offset_value as Array
		if (
			typeof(offset_arr[0]) not in [TYPE_INT, TYPE_FLOAT]
			or typeof(offset_arr[1]) not in [TYPE_INT, TYPE_FLOAT]
			or typeof(offset_arr[2]) not in [TYPE_INT, TYPE_FLOAT]
		):
			return {}
		var angle := float(row.get("angleRadians", 0.0))
		if typeof(row.get("angleRadians")) not in [TYPE_INT, TYPE_FLOAT] or not is_finite(angle):
			return {}
		var target_document: Dictionary = {}
		var db = _content_db_ref()
		if db != null and db.has_method("get_playable_structure_runtime"):
			target_document = db.get_playable_structure_runtime(object_id)
		if target_document.is_empty():
			push_error("RetailSliceSim CastleBehavior target '%s' has no converted structure template" % object_id)
			return {}
		var target_gameplay := ((target_document.get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary)
		var target_health := int((((target_gameplay.get("health", {}) as Dictionary).get("primary", {}) as Dictionary).get("maxHealth", {}) as Dictionary).get("value", 0))
		if target_health <= 0:
			push_error("RetailSliceSim CastleBehavior target '%s' has no authored maximum health" % object_id)
			return {}
		normalized.append({
			"index": index,
			"source_object_id": object_id,
			"object_id": PlayableUnitAdapter._runtime_id(object_id),
			"offset_source": Vector2(float(offset_arr[0]), float(offset_arr[1])),
			"elevation_source": float(offset_arr[2]),
			"angle_radians": angle,
			"maximum_health": target_health,
			"priority": int(row.get("priority", 0)),
			"phase": int(row.get("phase", 0)),
		})
	return {
		"faction": String(contract.get("faction", "")),
		"castle_template_token": String(contract.get("castleTemplateToken", "")),
		"pieces": normalized,
	}


func configure_castle_behaviors(by_source_object_id: Dictionary) -> void:
	## Vertical-slice wiring: source structure object id -> castleBehavior contract.
	_castle_behavior_by_source = by_source_object_id.duplicate(true)
	# setup() has already seeded legacy expansion slots on first boot. Replace
	# them with the selected pack's BSE contract before the first gameplay tick.
	for structure_id in structure_ids():
		var row: Dictionary = structures[structure_id]
		if _castle_behavior_for_structure(row).is_empty():
			continue
		if String(row.get("structure_kind", "")) == "fortress":
			expansion_pads.erase(structure_id)
		_unpack_castle_behavior_for_structure(structure_id)
		if String(row.get("structure_kind", "")) == "fortress":
			_seed_expansion_pads_for(structure_id)


func _retail_source_to_sim_offset(offset_source: Vector2) -> Vector2:
	## Banner formation offsets and BSE piece XY are retail source units.
	## Prefer the map transform scale; fall back to 0.1 (common SAGE->local).
	var scale := float(_rules.get("source_map_transform_scale", 0.0))
	if scale <= 0.0:
		scale = float(_rules.get("source_unit_scale", 0.1))
	if scale <= 0.0:
		scale = 0.1
	return offset_source * scale


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


func _compiled_damage_components(combat: Dictionary, source_scale: float = 1.0) -> Array:
	## Normalize the compiler's damageComponents rows. [] when the weapon has a
	## single authored damageType (the ordinary path) or authors none at all.
	var rows: Array = []
	for row_value in combat.get("damageComponents", []) as Array:
		if typeof(row_value) != TYPE_DICTIONARY:
			continue
		var row := row_value as Dictionary
		var value := float(row.get("value", 0.0))
		if value <= 0.0:
			continue
		var normalized := {
			"damage_type": String(row.get("damageType", "")).to_lower(),
			"value": value,
		}
		if row.has("radius"):
			normalized["radius"] = maxf(0.0, float(row.get("radius", 0.0)) * source_scale)
		if row.has("damageTaperOff"):
			normalized["damage_taper_off"] = clampf(float(row.get("damageTaperOff", 0.0)), 0.0, 100.0)
		if row.has("deathType"):
			normalized["death_type"] = String(row.get("deathType", "NORMAL"))
		if row.has("damageFXType"):
			normalized["damage_fx_type"] = String(row.get("damageFXType", ""))
		rows.append(normalized)
	return rows


func _damage_components_for(attacker_id: int, damage_type_override: String) -> Array:
	## The attacker's authored multi-type damage mix. An explicit override
	## (an ability or projectile that names its own type) replaces the mix
	## outright rather than blending into it.
	if damage_type_override != "" or not entities.has(attacker_id):
		return []
	return (entities[attacker_id] as Dictionary).get("damage_components", []) as Array


func _weighted_armor_scalar(scalars: Dictionary, components: Array, damage_type: String) -> float:
	## Damage-weighted mean of each component's own armor column. Because the
	## armor scalar is a linear multiplier, sum(dmg_i * scalar_i) equals
	## total_damage * this mean -- so one hit reproduces retail's per-nugget
	## resolution exactly without splitting into several hits.
	##
	## For Arwen (HERO 180 + SLASH 20) into RivendellLancerArmor (HERO 200%,
	## SLASH 40%) that is (180*2.0 + 20*0.4) / 200 = 1.84, i.e. 368 of 200 raw
	## -- the retail intent, where the untyped lump previously scored 200.
	var total := 0.0
	var weighted := 0.0
	for row_value in components:
		var row := row_value as Dictionary
		var value := float(row.get("value", 0.0))
		if value <= 0.0:
			continue
		total += value
		# An untyped component keeps falling to DEFAULT: never invent a type
		# weapon.ini does not author.
		var key := String(row.get("damage_type", ""))
		weighted += value * float(scalars.get(key, scalars.get("default", 1.0)))
	if total <= 0.0:
		return float(scalars.get(damage_type.to_lower(), scalars.get("default", 1.0)))
	return weighted / total


func _member_armor_scalar(target: Dictionary, damage_type: String, components: Array = []) -> float:
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
	if not components.is_empty():
		return float(table.get("damage_scalar", 1.0)) * _weighted_armor_scalar(scalars, components, damage_type)
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


func _compiled_ring_gollum_rule(registration: Dictionary) -> Dictionary:
	var objects_value: Variant = registration.get("objects", {})
	var system_value: Variant = registration.get("system", {})
	if typeof(objects_value) != TYPE_DICTIONARY or typeof(system_value) != TYPE_DICTIONARY:
		return {}
	var objects := objects_value as Dictionary
	var spawn_value: Variant = (system_value as Dictionary).get("spawn", {})
	if typeof(spawn_value) != TYPE_DICTIONARY:
		return {}
	var object_id := String((spawn_value as Dictionary).get("objectId", ""))
	var child_value: Variant = objects.get(object_id, {})
	if object_id == "" or typeof(child_value) != TYPE_DICTIONARY:
		return {}
	var child := child_value as Dictionary
	var parent_id := String(child.get("parentObjectId", ""))
	var parent_value: Variant = objects.get(parent_id, {})
	if parent_id == "" or typeof(parent_value) != TYPE_DICTIONARY:
		return {}
	var parent := parent_value as Dictionary
	var locomotors_value: Variant = parent.get("locomotors", {})
	var body_value: Variant = parent.get("body", {})
	if typeof(locomotors_value) != TYPE_DICTIONARY or typeof(body_value) != TYPE_DICTIONARY:
		return {}
	var speed_source := float((locomotors_value as Dictionary).get("normal", 0.0))
	var member_health := int((body_value as Dictionary).get("maxHealth", 0))
	if speed_source <= 0.0 or member_health <= 0:
		return {}
	# NeutralGollum binds HumanLocomotor for SET_NORMAL (neutralunits.ini:324),
	# which authors Acceleration/Braking 510 and TurnTime 500 -> 720 deg/s
	# (locomotor.ini:142). The importer now emits those on the ring descriptor
	# as `movement`; a pack cooked before that binding is a NAMED gap, not a
	# licence to reuse the walk speed as an acceleration.
	var movement_value: Variant = parent.get("movement", {})
	var movement: Dictionary = (movement_value as Dictionary) if typeof(movement_value) == TYPE_DICTIONARY else {}
	for authored_field in ["acceleration", "braking", "turnRateDegreesPerSecond"]:
		if not movement.has(authored_field):
			push_error(
				"unauthored locomotor field %s for %s (ring system descriptor predates the locomotor binding; recook the pack)"
				% [authored_field, object_id]
			)
			return {}
	var source_scale := maxf(0.000001, float(_rules.get("source_map_transform_scale", 1.0)))
	var speed := speed_source * source_scale
	var vision_source := float((parent.get("camouflage", {}) as Dictionary).get("detectionRange", 0.0))
	return {
		"source_object_id": object_id,
		"horde_id": object_id,
		"category": "hero" if (parent.get("kindOf", []) as Array).has("HERO") else "infantry",
		"kind_of": (parent.get("kindOf", []) as Array).duplicate(),
		"member_count": 1,
		"member_health": member_health,
		"member_damage": 0,
		"noncombatant": true,
		"speed": speed,
		"speed_source": speed_source,
		"acceleration": float(movement["acceleration"]) * source_scale,
		"acceleration_source": float(movement["acceleration"]),
		"turn_rate_degrees_per_second": float(movement["turnRateDegreesPerSecond"]),
		"braking": float(movement["braking"]) * source_scale,
		"braking_source": float(movement["braking"]),
		"attack_range": 0.0,
		"attack_range_source": 0.0,
		"minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0,
		"vision_range": vision_source * source_scale,
		"vision_range_source": vision_source,
		"delay_between_shots_ms": 1000.0,
		"pre_attack_delay_ms": 0.0,
		"firing_duration_ms": 0.0,
		"attack_period_ticks": 10,
		"pre_attack_ticks": 0,
		"firing_duration_ticks": 0,
		"formation_positions": [Vector3.ZERO],
		"provenance": {"source": "ring.system.registration.objects", "parentObjectId": parent_id},
	}


func _configure_playable_unit_runtime_contracts() -> void:
	var value: Variant = _rules.get("playable_unit_runtimes", {})
	if typeof(value) != TYPE_DICTIONARY:
		configuration_error = "Playable-unit runtime registry is not a dictionary"
		return
	var configured_unit_rules: Dictionary = _rules.get("unit_rules", {}) as Dictionary
	if ring_mechanic_enabled:
		var ring_registration_value: Variant = _ring_contract.get("_compiledRegistration", {})
		if typeof(ring_registration_value) == TYPE_DICTIONARY and not (ring_registration_value as Dictionary).is_empty():
			var compiled_gollum_rule := _compiled_ring_gollum_rule(ring_registration_value as Dictionary)
			if compiled_gollum_rule.is_empty():
				configuration_error = "Compiled ring-system Gollum has no usable simulation rule"
				return
			configured_unit_rules[String(compiled_gollum_rule.get("source_object_id", ""))] = compiled_gollum_rule
	var producer_kinds: Dictionary = _rules.get("producer_kind_by_source_object", {}) as Dictionary
	_unit_armor.clear()
	_unit_weapon_upgrades.clear()
	_created_hero_owner_teams.clear()
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
		var ring_gollum_block: Variant = ((document_value as Dictionary).get("ringMechanic", {}) as Dictionary).get("gollum", {})
		if ring_mechanic_enabled and typeof(ring_gollum_block) == TYPE_DICTIONARY \
				and not (ring_gollum_block as Dictionary).is_empty():
			_ring_contract.merge((ring_gollum_block as Dictionary), false)
			var gollum_simulation := PlayableUnitAdapter.simulation_rule(document_value as Dictionary, false)
			var gollum_rule := PlayableUnitAdapter.normalized_unit_rule(
				gollum_simulation, float(_rules.get("source_map_transform_scale", 0.0))
			)
			if gollum_rule.is_empty():
				configuration_error = "Ring Gollum runtime '%s' has no normalized unit rule" % object_id
				return
			var source_gollum_id := String((document_value as Dictionary).get("objectId", ""))
			var member_gollum_id := PlayableUnitAdapter.runtime_member_id(document_value as Dictionary)
			configured_unit_rules[source_gollum_id] = gollum_rule
			configured_unit_rules[member_gollum_id] = gollum_rule
			if not _ring_contract.has("gollumObjectId"):
				_ring_contract["gollumObjectId"] = source_gollum_id
			continue
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
		var banner_rule := _compiled_banner_carrier(document_value as Dictionary)
		var document_gameplay := (((document_value as Dictionary).get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary)
		if typeof(document_gameplay.get("bannerCarrier")) == TYPE_DICTIONARY and banner_rule.is_empty():
			configuration_error = "Playable-unit runtime '%s' has an unresolved banner carrier target" % object_id
			return
		if not banner_rule.is_empty():
			for armor_id in armor_id_space:
				_unit_banner_carriers[armor_id] = banner_rule
		# Banner unit documents carry BannerCarrierUpdate respawn timers.
		var respawn_ticks := _compiled_banner_respawn_ticks(document_value as Dictionary)
		if respawn_ticks >= 0:
			var banner_runtime_id := PlayableUnitAdapter.runtime_unit_id(document_value as Dictionary)
			var banner_member_id := PlayableUnitAdapter.runtime_member_id(document_value as Dictionary)
			if banner_runtime_id != "":
				_banner_respawn_ticks_by_object[banner_runtime_id] = respawn_ticks
			if banner_member_id != "" and banner_member_id != banner_runtime_id:
				_banner_respawn_ticks_by_object[banner_member_id] = respawn_ticks
			# Also index the retail source name and adapter slug used by horde contracts.
			var source_name := String((document_value as Dictionary).get("objectId", ""))
			if source_name != "":
				_banner_respawn_ticks_by_object[PlayableUnitAdapter.runtime_object_id(source_name)] = respawn_ticks
				_banner_respawn_ticks_by_object[source_name] = respawn_ticks
		var purchase_rows := _compiled_unit_upgrade_commands(document_value as Dictionary)
		if purchase_rows.is_empty():
			if not units_without_upgrade_commands.has(armor_member_id):
				units_without_upgrade_commands.append(armor_member_id)
		else:
			# Keep the unit fieldable when a purchase row has no compiled effect
			# yet (thrall composition swaps; forged-blade WeaponSetUpgrade not
			# emitted into combat.upgrades). Fail-closed used to refuse the
			# entire playable_unit_runtimes registry — one incomplete purchase
			# killed match configure for the whole faction. Surface only the
			# rows that already have weapon/armor/level effects; record gaps.
			var armor_upgrade_ids: Dictionary = (armor_rule.get("upgrades", {}) as Dictionary)
			var weapon_upgrade_ids: Dictionary = _unit_weapon_upgrades.get(armor_member_id, {}) as Dictionary
			var level_upgrade_ids: Dictionary = _unit_level_upgrades.get(armor_member_id, {}) as Dictionary
			var accepted_purchase_rows: Array = []
			for row_value in purchase_rows:
				var purchase := row_value as Dictionary
				var purchase_id := String(purchase.get("upgrade_id", ""))
				if purchase_id == "":
					continue
				if (
					weapon_upgrade_ids.has(purchase_id)
					or armor_upgrade_ids.has(purchase_id)
					or level_upgrade_ids.has(purchase_id)
				):
					accepted_purchase_rows.append(purchase)
				else:
					print(
						"[RetailSliceSim] unit '%s' purchase '%s' has no compiled weapon/armor/level effect; deferred from purchase surface"
						% [object_id, purchase_id]
					)
			var purchase_unit_type := String(PlayableUnitAdapter.simulation_rule(document_value as Dictionary).get("unit_type", ""))
			if purchase_unit_type != "" and not accepted_purchase_rows.is_empty():
				_unit_upgrade_commands[purchase_unit_type] = accepted_purchase_rows
			elif purchase_unit_type != "" and accepted_purchase_rows.is_empty() and not purchase_rows.is_empty():
				if not units_without_upgrade_commands.has(armor_member_id):
					units_without_upgrade_commands.append(armor_member_id)
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
		# The compiled selection surface carries the unit's authored base
		# CommandSet on every command row. Record it only when the document agrees
		# on one identity; mixed surfaces remain unresolved rather than choosing a
		# convenient first row. MonitorConditionUpdate uses this exact value to
		# restore the base palette after its condition clears.
		var authored_command_sets: Array[String] = []
		for selection_value in PlayableUnitAdapter.selection_commands(document_value as Dictionary):
			var command_set_id := String((selection_value as Dictionary).get("commandSetId", "")).strip_edges()
			if command_set_id != "" and not authored_command_sets.has(command_set_id): authored_command_sets.append(command_set_id)
		if authored_command_sets.size() == 1:
			unit_rule["default_command_set_id"] = authored_command_sets[0]
		var authored_formation_toggle := _authored_formation_toggle(document_value as Dictionary)
		if not authored_formation_toggle.is_empty():
			unit_rule["formation_toggle"] = authored_formation_toggle
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
		var doc_combat: Dictionary = simulation.get("combat", {}) as Dictionary
		var doc_damage_type := String(doc_combat.get("damageType", "")).to_lower()
		var doc_damage_components := _compiled_damage_components(
			doc_combat, float(_rules.get("source_map_transform_scale", 1.0))
		)
		if not doc_damage_components.is_empty():
			# A multi-nugget weapon whose nuggets author different types
			# (ArwenSword: HERO ARWEN_DAMAGE + SLASH 20) carries no single
			# authored damageType. Each component keeps its own type and is
			# weighted against its own armor column instead of collapsing the
			# whole hit onto DEFAULT.
			_unit_damage_components[member_id] = doc_damage_components
		if doc_damage_type != "":
			# Source-authored BFME2 damage type (SLASH/PIERCE/CAVALRY/...);
			# fortress armor consumes it through the same scalar table the
			# historical Men constants use, unknown types keep the default.
			_unit_damage_types[member_id] = doc_damage_type
		elif doc_damage_components.is_empty() and not missing_damage_type_units.has(member_id):
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
		var is_ring_hero_rule := false
		for producer_route in resolved_producers:
			if String(producer_route.get("source_field", "")) == "BuildableRingHeroesMP":
				is_ring_hero_rule = true
				break
		if is_ring_hero_rule:
			(_unit_production_rules[unit_type] as Dictionary)["is_ring_hero"] = true
		if not _production_unit_order.has(unit_type):
			_production_unit_order.append(unit_type)
		var created_hero: Dictionary = (
			(document_value as Dictionary).get("registration", {}) as Dictionary
		).get("createAHero", {}) as Dictionary
		if created_hero.has("ownerTeam"):
			_created_hero_owner_teams[unit_type] = int(created_hero["ownerTeam"])
		if not created_hero.is_empty() and created_hero.has("awardDefinitions"):
			var award_contract := created_hero.duplicate(true)
			award_contract["ownerTeam"] = int(created_hero.get("ownerTeam", -1))
			_cah_award_contracts[unit_type] = award_contract
		var prerequisites_by_producer: Dictionary = {}
		var any_groups_by_producer: Dictionary = {}
		for route in resolved_producers:
			var producer_kind := String(route["producer_kind"])
			var candidate: Array = (route.get("prerequisites", []) as Array).duplicate()
			if String(route.get("source_field", "")) == "BuildableRingHeroesMP" and candidate.is_empty():
				candidate = ["Upgrade_RingHero", "Upgrade_FortressRingHero"]
				print("[RetailSliceSim] stale-pack-ring-prereqs-synthesized unit=%s producer=%s" % [unit_type, producer_kind])
			var candidate_any: Array = (route.get("prerequisites_any_of", []) as Array).duplicate()
			if not prerequisites_by_producer.has(producer_kind):
				prerequisites_by_producer[producer_kind] = candidate
				any_groups_by_producer[producer_kind] = candidate_any
				continue
			# One authored route per producer level can list the same button with
			# different prerequisites (base/L2/L3 command sets). The unit is
			# trainable as soon as its cheapest authored variant's prerequisites
			# hold, so the effective requirement is the minimum-cardinality set;
			# ties keep deterministic document order. An ANY-of group counts as
			# one requirement, whatever its member count.
			var existing: Array = prerequisites_by_producer[producer_kind]
			var existing_any: Array = any_groups_by_producer.get(producer_kind, []) as Array
			var candidate_size := candidate.size() + (1 if not candidate_any.is_empty() else 0)
			var existing_size := existing.size() + (1 if not existing_any.is_empty() else 0)
			if candidate_size < existing_size:
				prerequisites_by_producer[producer_kind] = candidate
				any_groups_by_producer[producer_kind] = candidate_any
		_unit_prerequisites[unit_type] = prerequisites_by_producer
		_unit_prerequisite_any_groups[unit_type] = any_groups_by_producer
		# Hero SPECIAL_POWER abilities (converted doc rows) register per unit
		# type; heroes without an abilities array simply carry none.
		var ability_rows := PlayableUnitAdapter.ability_rules(document_value as Dictionary)
		if not ability_rows.is_empty():
			_unit_ability_rules[unit_type] = _scaled_ability_rules(
				ability_rows, float(_rules.get("source_map_transform_scale", 0.0))
			)
		_ensure_capture_building_ability(unit_type, document_value as Dictionary)
		# ExperienceLevel chain (converted doc rows) registers per unit type;
		# units whose chain retail never authored carry no rule and never gain
		# XP — their kill payout is the recorded default, recorded per victim.
		var experience_rule := PlayableUnitAdapter.experience_rule(document_value as Dictionary)
		if not experience_rule.is_empty():
			_unit_experience_rules[unit_type] = experience_rule
		# Converter moduleContracts (typed + opaque deferred): index for runtime
		# consumers. Deferred rows are still attached as authored evidence; only
		# executable rows drive behavior until their subsystems land.
		var contracts := PlayableUnitAdapter.module_contracts(document_value as Dictionary)
		if not contracts.is_empty():
			_unit_module_contracts[unit_type] = contracts
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
	var any_groups_by_producer: Dictionary = {}
	for route_value in resolved_producers:
		var route := route_value as Dictionary
		var producer_kind := String(route["producer_kind"])
		if not prerequisites_by_producer.has(producer_kind):
			prerequisites_by_producer[producer_kind] = (route.get("prerequisites", []) as Array).duplicate()
			any_groups_by_producer[producer_kind] = (route.get("prerequisites_any_of", []) as Array).duplicate()
	_unit_prerequisites[unit_type] = prerequisites_by_producer
	_unit_prerequisite_any_groups[unit_type] = any_groups_by_producer
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
	_register_structure_upgrade_contract(_structure_upgrade_contracts, upgrade_id, {
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


func _global_upgrade_source() -> Dictionary:
	## The three raw upgrade tables the global (player-faction) path reads,
	## resolved exactly as before: an explicit top-level rules key wins, else the
	## compiled faction manifest supplies it.
	var manifest: Dictionary = _rules.get("faction_manifest", {}) as Dictionary
	return {
		"structure_upgrade_chains": _rules.get("structure_upgrade_chains", manifest.get("structure_upgrade_chains", {})),
		"structure_research": _rules.get("structure_research", manifest.get("structure_research", {})),
		"structure_upgrade_effects": _rules.get("structure_upgrade_effects", manifest.get("structure_upgrade_effects", {})),
		"structure_castle_upgrades": _rules.get("structure_castle_upgrades", manifest.get("structure_castle_upgrades", {})),
	}


func _manifest_upgrade_source(manifest: Dictionary) -> Dictionary:
	## The same three raw tables read straight off a per-team faction manifest
	## (cross-faction path). The manifest dicts already carry these keys, so no
	## importer change is needed.
	return {
		"structure_upgrade_chains": manifest.get("structure_upgrade_chains", {}),
		"structure_research": manifest.get("structure_research", {}),
		"structure_upgrade_effects": manifest.get("structure_upgrade_effects", {}),
		"structure_castle_upgrades": manifest.get("structure_castle_upgrades", {}),
	}


func _configure_structure_upgrade_chains() -> void:
	_compile_structure_upgrade_chains(_global_upgrade_source(), _structure_upgrade_contracts)
	_compile_structure_castle_upgrades(_global_upgrade_source(), _structure_upgrade_contracts)


func _compile_structure_castle_upgrades(source: Dictionary, contracts: Dictionary) -> void:
	## Retail's fortress improvement surface (the OBJECT_UPGRADE buttons in the
	## fortress command set's upgrades page — commandset.ini
	## MordorFortressCommandSet slots 8-13 DoomPyres/LavaMoat/FireArrows/
	## MagmaCauldrons/MorgulSorcery/GorgorothSpire, and the same shape for every
	## other faction).
	##
	## Unlike a level chain these do NOT swap the command set or raise the
	## building's level: the button buys a *Trigger* OBJECT upgrade and the
	## fortress's own CastleUpgrade module hands out the real one (see
	## `_castle_upgrade_grants`). They therefore ride their own contract branch:
	## no from/to command set, no level gain, one purchase each.
	##
	## Source rows are {kind: {"upgrades": [{upgradeId, grantsUpgradeId, cost,
	## buildTimeSeconds, slot, commandId, labelId, tooltipId, buttonImageId,
	## neededUpgradeIds?, requiresUpgradeId?}]}}. Malformed rows fail closed.
	if configuration_error != "":
		return
	var value: Variant = source.get("structure_castle_upgrades", {})
	if typeof(value) != TYPE_DICTIONARY:
		configuration_error = "Structure castle upgrades are not a dictionary"
		return
	var kinds: Array[String] = []
	for kind_value in (value as Dictionary).keys():
		kinds.append(String(kind_value))
	kinds.sort()
	for kind in kinds:
		var surface_value: Variant = (value as Dictionary).get(kind)
		if typeof(surface_value) != TYPE_DICTIONARY:
			configuration_error = "Structure castle upgrade surface for '%s' is not a dictionary" % kind
			return
		var rows: Array = (surface_value as Dictionary).get("upgrades", []) as Array
		if rows.is_empty():
			configuration_error = "Structure castle upgrade surface for '%s' is malformed" % kind
			return
		for row_value in rows:
			if typeof(row_value) != TYPE_DICTIONARY:
				configuration_error = "Structure castle upgrade surface for '%s' has a malformed row" % kind
				return
			var row := row_value as Dictionary
			var upgrade_id := String(row.get("upgradeId", ""))
			var granted_id := String(row.get("grantsUpgradeId", ""))
			var cost := int(row.get("cost", -1))
			var build_seconds := float(row.get("buildTimeSeconds", 0.0))
			# Zero build time is authored evidence (see the research surface's
			# RotWK BuildTime 0 note); the duration clamps to >= 1 tick below.
			# An EMPTY grant is authored evidence, not a gap: retail's Banners /
			# Siege Kegs / Oil Casks / Mighty Catapult buttons buy an upgrade
			# that applies to the fortress itself with no CastleUpgrade pass-out
			# module behind it (commandset.ini:4107 slots 8/9/11/13).
			if upgrade_id == "" or cost < 0 or build_seconds < 0.0:
				configuration_error = "Structure castle upgrade '%s' on '%s' is malformed" % [upgrade_id, kind]
				return
			var needed: Array[String] = []
			for needed_value in Array(row.get("neededUpgradeIds", [])):
				needed.append(String(needed_value))
			var contract := {
				"structure_kind": kind,
				"cost": cost,
				"duration_ticks": maxi(1, roundi(build_seconds / TICK_SECONDS)),
				"level_cap": 99,
				"levels_to_gain": 0,
				"to_level": 0,
				"cancelable": bool(row.get("cancelable", false)),
				"from_command_set": "",
				"to_command_set": "",
				"requires_upgrade_id": String(row.get("requiresUpgradeId", "")),
				"health_add": 0,
				"production_multiplier": 1.0,
				"castle_upgrade": true,
				"grants_upgrade_id": granted_id,
				"command_id": String(row.get("commandId", "")),
				"slot": int(row.get("slot", 0)),
				"label_id": String(row.get("labelId", "")),
				"tooltip_id": String(row.get("tooltipId", "")),
				"image_id": String(row.get("buttonImageId", "")),
				"lacks_prerequisite_label_id": String(row.get("lacksPrerequisiteLabelId", "")),
				"needed_upgrade_ids": needed,
				"needed_upgrade_any": bool(row.get("neededUpgradeAny", false)),
			}
			if not _register_structure_upgrade_contract(contracts, upgrade_id, contract):
				return


func _compile_structure_upgrade_chains(source: Dictionary, contracts: Dictionary) -> void:
	## Generic doc-driven structure levels: every authored upgrade chain the
	## faction manifest projected from the playableStructure.* documents
	## (cost/time/level cap/command-set swap/per-level effects) registers into
	## `contracts` keyed by its authored upgrade id. Malformed chains fail closed
	## into configuration_error instead of registering a partial contract.
	if configuration_error != "":
		return
	var value: Variant = source.get("structure_upgrade_chains", {})
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
			if not _register_structure_upgrade_contract(contracts, upgrade_id, contract):
				return
			previous_to_level = to_level


func _register_structure_upgrade_contract(contracts: Dictionary, upgrade_id: String, contract: Dictionary) -> bool:
	## One upgrade id binds exactly one structure kind WITHIN the target table;
	## collisions fail closed instead of picking a winner. `contracts` is the
	## global dict for the player faction and a per-team dict for a cross-faction
	## team, so the fail-close scopes per team (Men/Elf ids never collide).
	if contracts.has(upgrade_id):
		var existing: Dictionary = contracts[upgrade_id]
		if String(existing.get("structure_kind", "")) != String(contract.get("structure_kind", "")):
			configuration_error = "Structure upgrade '%s' binds two structure kinds" % upgrade_id
			return false
	contracts[upgrade_id] = contract
	return true


func _configure_structure_research_contracts() -> void:
	_compile_structure_research_contracts(
		_global_upgrade_source(),
		_structure_upgrade_contracts,
		_structure_upgrade_effects,
		_compiled_research_kinds,
	)


func _compile_structure_research_contracts(source: Dictionary, contracts: Dictionary, upgrade_effects: Dictionary, research_kinds: Dictionary) -> void:
	## Doc-driven PLAYER technology sales: every authored research row the
	## faction manifest projected from the playableStructure.* documents
	## (cost/time/gate/button identity) registers into `contracts` keyed by its
	## authored upgrade id, with the per-kind effect bundles in `upgrade_effects`
	## and the compiled kinds recorded in `research_kinds`. Malformed surfaces
	## fail closed into configuration_error.
	if configuration_error != "":
		return
	var research_value: Variant = source.get("structure_research", {})
	if typeof(research_value) != TYPE_DICTIONARY:
		configuration_error = "Structure research surfaces are not a dictionary"
		return
	var effects_value: Variant = source.get("structure_upgrade_effects", {})
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
			if String(effect.get("kind", "")) == "command-set-transition":
				var normalized_command_set := _normalized_command_set_upgrade_effect(effect)
				if normalized_command_set.is_empty():
					configuration_error = "Structure '%s' has a malformed CommandSetUpgrade effect" % kind
					return
				normalized_effects.append(normalized_command_set)
				continue
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
		upgrade_effects[kind] = {
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
		research_kinds[kind] = true
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
			if not _register_structure_upgrade_contract(contracts, upgrade_id, contract):
				return


func _normalized_command_set_upgrade_effect(effect: Dictionary) -> Dictionary:
	var game := String(effect.get("game", "")).to_lower()
	var active_game := String(_rules.get("game", "")).to_lower()
	var triggers_value: Variant = effect.get("triggerUpgradeIds")
	var provenance_value: Variant = effect.get("commandSetProvenance")
	if (
		game not in ["bfme2", "rotwk"]
		or active_game not in ["bfme2", "rotwk"]
		or game != active_game
		or String(effect.get("effectId", "")).strip_edges() == ""
		or String(effect.get("upgradeId", "")).strip_edges() == ""
		or String(effect.get("triggerSemantics", "")) not in ["any", "all"]
		or String(effect.get("commandSetId", "")).strip_edges() == ""
		or String(effect.get("module", "")) != "CommandSetUpgrade"
		or String(effect.get("runtimeStatus", "")) != "executable"
		or String(effect.get("descriptorStatus", "")) != "resolved"
		or typeof(triggers_value) != TYPE_ARRAY
		or (triggers_value as Array).is_empty()
		or typeof(provenance_value) != TYPE_DICTIONARY
	):
		return {}
	var triggers: Array[String] = []
	var seen: Dictionary = {}
	for trigger_value in triggers_value as Array:
		var trigger := String(trigger_value).strip_edges()
		var folded := trigger.to_lower()
		if trigger == "" or seen.has(folded):
			return {}
		seen[folded] = true
		triggers.append(trigger)
	if not triggers.has(String(effect.get("upgradeId", ""))):
		return {}
	var provenance := provenance_value as Dictionary
	if (
		String(provenance.get("authored", "")).strip_edges() != String(effect.get("commandSetId", ""))
		or String(provenance.get("sourceIni", "")).strip_edges() == ""
		or int(provenance.get("line", 0)) <= int(effect.get("line", 0))
	):
		return {}
	if effect.has("customAnimation"):
		var animation_value: Variant = effect.get("customAnimation")
		if typeof(animation_value) != TYPE_DICTIONARY:
			return {}
		var animation := animation_value as Dictionary
		if (
			String(animation.get("animState", "")).strip_edges() == ""
			or float(animation.get("animTimeMs", -1.0)) < 0.0
			or String(animation.get("runtimeStatus", "")) != "deferred"
			or String(animation.get("deferredReason", "")) != "presentation-runtime-not-accepted"
		):
			return {}
	return effect.duplicate(true)


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
	var trebuchet_rule := {
		"horde_id": TREBUCHET_OBJECT_ID,
		"member_count": 1,
		"member_health": health,
		"member_damage": damage,
		"speed": speed_source * scale,
		"speed_source": speed_source,
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
		"provenance": {"contractSources": contract.get("sources", []).duplicate(true)},
	}
	# GondorTrebuchet binds CatapultLocomotor for SET_NORMAL (trebuchet.ini
	# LocomotorSet), and that template authors Acceleration = Braking = 1000
	# plus TurnTime (locomotor.ini:1683 in BFME2, TurnTime 3000 -> 120 deg/s in
	# RotWK 2.01). m3_pack_expansion now binds those three numbers onto the M3
	# contract. A pack cooked before that binding is a NAMED gap: the trebuchet
	# still builds and shoots, and _step_route refuses to move it while saying
	# exactly why. There is no invented ramp and no 360 deg/s placeholder.
	var trebuchet_movement_gaps: Array = []
	for authored_field in ["acceleration", "braking", "turnRateDegreesPerSecond"]:
		if movement.has(authored_field):
			continue
		trebuchet_movement_gaps.append(authored_field)
	if trebuchet_movement_gaps.is_empty():
		trebuchet_rule["acceleration"] = float(movement["acceleration"]) * scale
		trebuchet_rule["acceleration_source"] = float(movement["acceleration"])
		trebuchet_rule["braking"] = float(movement["braking"]) * scale
		trebuchet_rule["braking_source"] = float(movement["braking"])
		trebuchet_rule["turn_rate_degrees_per_second"] = float(movement["turnRateDegreesPerSecond"])
	else:
		# printerr, not push_warning: this is a lane-authored, expected-until-recook
		# data gap, and gate-m2-focused treats any engine WARNING as a defect.
		printerr(
			"NAMED GAP: %s carries no authored %s; the M3 trebuchet contract predates the CatapultLocomotor binding and the unit will not move until the pack is recooked"
			% [TREBUCHET_OBJECT_ID, ", ".join(PackedStringArray(trebuchet_movement_gaps))]
		)
		trebuchet_rule["unauthored_locomotor_fields"] = trebuchet_movement_gaps
	configured_unit_rules[TREBUCHET_OBJECT_ID] = trebuchet_rule
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
	_note_structure_table_mutation()
	# Same contract as _restore_authoritative_state: ids are about to be reused
	# by a new match, so the id-keyed footprint memo must not survive.
	_structure_footprint_radius_cache.clear()
	var layout := _home_layout if not _home_layout.is_empty() else _derive_home_layout()
	for team in _roster_team_ids():
		var team_layout: Dictionary = layout.get(team, layout.get(str(team), {}))
		var base_id := _team_structure_base(team)
		var team_seed_kinds := seed_structure_kinds_for_team(team)
		if team_seed_kinds.is_empty():
			team_seed_kinds = structure_kinds_for_team(team)
		var team_max_health := structure_max_health_for_team(team)
		var team_build_rules := structure_build_rules_for_team(team)
		var team_production_order := production_unit_order_for_team(team)
		var team_production_rules := unit_production_rules_for_team(team)
		var team_scope := production_scope_for_team(team)
		for index in range(team_seed_kinds.size()):
			var kind := String(team_seed_kinds[index])
			var position := Vector2(team_layout.get(kind, _fallback_structure_position(team, index)))
			var maximum_health := int(team_max_health[kind])
			var production: Array[String] = []
			for unit_type in team_production_order:
				if not team_scope.is_empty() and not team_scope.has(String(unit_type)):
					continue
				if created_hero_owner_team(String(unit_type)) not in [-1, team]:
					continue
				var production_rule: Dictionary = team_production_rules[unit_type]
				var producer_kinds_for_rule: Array = production_rule.get("producer_kinds", [String(production_rule.get("producer_kind", ""))])
				if producer_kinds_for_rule.has(kind):
					production.append(unit_type)
			var structure_id := base_id + index + 1
			_note_structure_table_mutation()
			structures[structure_id] = {
				"id": structure_id,
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
			if bool((team_build_rules.get(kind, {}) as Dictionary).get("highlander_body", false)):
				structures[structure_id]["highlander_body"] = true
			# Retail object identity for EVERY seeded kind, not just fortresses,
			# so this path and issue_construct stamp the same table for the same
			# kind and a building cannot have two identities depending on
			# whether the map placed it or a porter raised it.
			#
			# MEASURED, not assumed: the id is one input to
			# _structure_footprint_radius, which raised the worry that stamping
			# it would move structure attack-range and unit eviction. On the
			# mounted Men and Angmar packs it does not — the runner prints
			# with/without/seed radii for a fortress and a non-fortress kind and
			# all three agree to four decimals (FORTRESS_FOOTPRINT,
			# NON_FORTRESS_FOOTPRINT), because the resolver already falls back to
			# the same per-kind geometry. The 3000-tick state pin is unchanged.
			# The symmetry is worth having on its own terms; it is not a fix for
			# a divergence anyone has demonstrated.
			var seed_sources: Variant = structure_source_object_ids_for_team(team).get(kind, [])
			if typeof(seed_sources) == TYPE_ARRAY and not (seed_sources as Array).is_empty():
				structures[structure_id]["source_object_id"] = String((seed_sources as Array)[0])
			elif typeof(seed_sources) in [TYPE_STRING, TYPE_STRING_NAME]:
				structures[structure_id]["source_object_id"] = String(seed_sources)
			_apply_structure_create_grants(
				structures[structure_id] as Dictionary, true, true
			)
			_apply_structure_inherit_upgrades(structures[structure_id] as Dictionary)
			_initialize_structure_auto_deposit(structures[structure_id] as Dictionary)
			_unpack_castle_behavior_for_structure(structure_id)
	_seed_all_expansion_pads()
	if build_plots_only:
		_seed_all_build_plots()


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
	command_points: int = -1,
	unit_rule_override: Dictionary = {},
	cached_build_cost: int = -1
) -> void:
	var unit_rules_value: Variant = _rules.get("unit_rules", {})
	var unit_rule: Dictionary = (
		unit_rule_override
		if not unit_rule_override.is_empty()
		else (
			(unit_rules_value as Dictionary).get(object_id, {}) as Dictionary
			if typeof(unit_rules_value) == TYPE_DICTIONARY
			else {}
		)
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
	# Only the sealed scenario noncombatant contract may preserve zero damage.
	# Legacy/malformed rules that merely omit or zero damage retain the historic
	# clamp to one and cannot smuggle a new semantic through a numeric sentinel.
	var noncombatant := bool(unit_rule.get("noncombatant", false))
	var member_damage := 0 if noncombatant else maxi(1, int(unit_rule.get("member_damage", 0)))
	var fallback_weapon := {
		"name": "legacy-default",
		"weapon_slot": String(unit_rule.get("default_weapon_slot", "")),
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
	if unit_rule.has("pre_attack_type"):
		fallback_weapon["pre_attack_type"] = String(unit_rule["pre_attack_type"])
	if unit_rule.has("pre_attack_random_amount_ms"):
		fallback_weapon["pre_attack_random_amount_ms"] = float(unit_rule["pre_attack_random_amount_ms"])
	for optional_weapon_field in [
		"projectile_object_id", "projectile_speed", "projectile_speed_source",
		"radius_damage_affects", "damage_components", "damage_type",
	]:
		if unit_rule.has(optional_weapon_field):
			fallback_weapon[optional_weapon_field] = unit_rule[optional_weapon_field]
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
		# 0.0 is the UNAUTHORED sentinel, not a default: _step_route and
		# _retail_turn_rate_degrees both refuse to act on it and name the unit
		# in a push_error. A rule that omits these keys is a NAMED GAP already
		# reported at configuration time (today: the M3 trebuchet contract,
		# whose pack predates the CatapultLocomotor binding), so spawning must
		# not hard-index them and abort the whole spawn path.
		"acceleration": float(unit_rule.get("acceleration", 0.0)),
		"acceleration_source": float(unit_rule.get("acceleration_source", 0.0)),
		"turn_rate_degrees_per_second": float(unit_rule.get("turn_rate_degrees_per_second", 0.0)),
		"braking": float(unit_rule.get("braking", 0.0)),
		"braking_source": float(unit_rule.get("braking_source", 0.0)),
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
		"damage_components": (unit_rule.get("damage_components", _unit_damage_components.get(object_id, [])) as Array).duplicate(true),
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
		# LockWeaponCreate applies at build completion and remains permanent.
		# The importer currently accepts only the exact retail PRIMARY corpus.
		"permanent_weapon_locks": Array(
			unit_rule.get("permanent_weapon_locks", [])
		).duplicate(),
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
	if cached_build_cost >= 0 and _contracts_have_executable_refund_die(
		_unit_module_contracts.get(unit_type, []) as Array
	):
		# Production supplies the queue item's final charged price after every
		# authored cost modifier; death must never recompute it from current rules.
		entities[id]["cached_build_cost"] = cached_build_cost
	# Optional sealed scenario policy. False is the historical combatant default
	# and must remain absent, otherwise every ordinary unit gains a meaningless
	# state byte and moves the frozen cross-platform pin.
	if noncombatant:
		entities[id]["noncombatant"] = true
	if String(unit_rule.get("default_command_set_id", "")) != "":
		entities[id]["default_command_set_id"] = String(unit_rule.get("default_command_set_id", ""))
		entities[id]["command_set_id"] = String(unit_rule.get("default_command_set_id", ""))
	# PreAttackType / random amount ride the compiled rule. Absent on the
	# synthetic pin harness (which never authors them) so the 3000-tick pin
	# stays put; `_step_member_attacks` defaults missing type to PER_SHOT.
	if unit_rule.has("pre_attack_type"):
		entities[id]["pre_attack_type"] = String(unit_rule["pre_attack_type"])
	if unit_rule.has("pre_attack_random_amount_ms"):
		entities[id]["pre_attack_random_amount_ms"] = float(unit_rule["pre_attack_random_amount_ms"])
	# Absent-unless-authored keeps melee and no-projectile state byte-identical.
	for optional_projectile_field in [
		"projectile_object_id", "projectile_speed", "projectile_speed_source",
		"radius_damage_affects",
	]:
		if unit_rule.has(optional_projectile_field):
			entities[id][optional_projectile_field] = unit_rule[optional_projectile_field]
	# ShroudClearingRange, the deshroud radius. Absent unless the compiled rule
	# authors one, exactly like the body scalars below and for the same reason:
	# a key that appears unconditionally would change every unit's snapshot and
	# move the 3000-tick pin. Absent means the fog pass falls back to vision and
	# says so (_shroud_clearing_radius).
	if unit_rule.has("shroud_clearing_range"):
		entities[id]["shroud_clearing_range"] = maxf(
			0.0, float(unit_rule["shroud_clearing_range"])
		)
		entities[id]["shroud_clearing_range_source"] = maxf(
			0.0, float(unit_rule.get("shroud_clearing_range_source", 0.0))
		)
	# Exact effective Object BountyValue. No field means no authored bounty and
	# must remain distinguishable from an authored zero in state/save/hash.
	if unit_rule.has("bounty_value"):
		entities[id]["bounty_value"] = maxi(0, int(unit_rule["bounty_value"]))
	if unit_rule.has("max_turn_without_reform_degrees"):
		entities[id]["max_turn_without_reform_degrees"] = float(
			unit_rule["max_turn_without_reform_degrees"]
		)
	if unit_rule.has("slow_turn_radius"):
		entities[id]["slow_turn_radius"] = maxf(0.0, float(unit_rule["slow_turn_radius"]))
	if unit_rule.has("fast_turn_radius"):
		entities[id]["fast_turn_radius"] = maxf(0.0, float(unit_rule["fast_turn_radius"]))
	if unit_rule.has("min_turn_speed"):
		entities[id]["min_turn_speed"] = clampf(float(unit_rule["min_turn_speed"]), 0.0, 1.0)
	if String(unit_rule.get("turn_rate_source", "")) != "":
		entities[id]["turn_rate_source"] = String(unit_rule["turn_rate_source"])
	for crush_int_key in ["crusher_level", "crushable_level", "crush_damage", "crush_revenge_damage"]:
		if unit_rule.has(crush_int_key):
			entities[id][crush_int_key] = int(unit_rule[crush_int_key])
	if unit_rule.has("crush_weapon_id"):
		entities[id]["crush_weapon_id"] = String(unit_rule["crush_weapon_id"])
	if unit_rule.has("crush_revenge_weapon_id"):
		entities[id]["crush_revenge_weapon_id"] = String(unit_rule["crush_revenge_weapon_id"])
	for crush_float_key in [
		"min_crush_velocity_percent",
		"crush_deceleration_percent",
		"crush_knockback",
	]:
		if unit_rule.has(crush_float_key):
			entities[id][crush_float_key] = float(unit_rule[crush_float_key])
	if typeof(unit_rule.get("formation_toggle")) == TYPE_DICTIONARY and not (unit_rule.get("formation_toggle") as Dictionary).is_empty():
		# Absent unless the unit's own CommandSet authored a
		# HORDE_TOGGLE_FORMATION button (commandbutton.ini). A unit with no
		# such button gets no key, and cannot be put into a formation.
		entities[id]["formation_toggle"] = (unit_rule.get("formation_toggle") as Dictionary).duplicate(true)
	if unit_rule.has("flanking_bonus"):
		entities[id]["flanking_bonus"] = float(unit_rule["flanking_bonus"])
	if unit_rule.has("wait_for_formation"):
		entities[id]["wait_for_formation"] = bool(unit_rule["wait_for_formation"])
	if typeof(unit_rule.get("kind_of")) == TYPE_ARRAY and not (unit_rule.get("kind_of") as Array).is_empty():
		entities[id]["kind_of"] = (unit_rule.get("kind_of") as Array).duplicate()
	# Body policy is optional authoritative state. Keep the key absent for
	# ordinary ActiveBody units so their snapshots/hashes do not change.
	if unit_rule.get("highlander_body") == true:
		entities[id]["highlander_body"] = true
	# Innate body scalars (damage taken, regeneration rate). Absent unless the
	# compiled rule authors them, so no retail unit's snapshot or authoritative
	# hash gains a byte.
	if unit_rule.has("innate_armor_scalar"):
		entities[id]["innate_armor_scalar"] = maxf(0.0, float(unit_rule["innate_armor_scalar"]))
	if unit_rule.has("auto_heal_multiplier"):
		entities[id]["auto_heal_multiplier"] = maxf(0.0, float(unit_rule["auto_heal_multiplier"]))
	# Optional object lifecycle policy. Its absence contributes no entity,
	# snapshot, or authoritative-hash bytes.
	if unit_rule.has("destroy_die"):
		entities[id]["destroy_die"] = Array(
			unit_rule["destroy_die"]
		).duplicate(true)
	if unit_rule.has("slow_death_fades"):
		entities[id]["slow_death_fades"] = Array(
			unit_rule["slow_death_fades"]
		).duplicate(true)
	if unit_rule.has("keep_object_die"):
		entities[id]["keep_object_die"] = bool(unit_rule.get("keep_object_die", false))
		entities[id]["keep_object_die_policy"] = (unit_rule.get("keep_object_die_policy", {}) as Dictionary).duplicate(true)
	if unit_rule.has("summon_auras"):
		entities[id]["summon_auras"] = Array(unit_rule["summon_auras"]).duplicate(true)
	# Optional AIUpdateInterface field slice. Keep the keys absent unless the
	# compiler authored the complete contract, so legacy/missing-field entity
	# snapshots and hashes remain byte-identical.
	if (
		unit_rule.has("auto_acquire_enabled")
		and unit_rule.has("auto_acquire_attack_buildings")
		and unit_rule.has("auto_acquire_while_stealthed")
	):
		entities[id]["auto_acquire_enabled"] = bool(unit_rule["auto_acquire_enabled"])
		entities[id]["auto_acquire_attack_buildings"] = bool(
			unit_rule["auto_acquire_attack_buildings"]
		)
		entities[id]["auto_acquire_while_stealthed"] = bool(
			unit_rule["auto_acquire_while_stealthed"]
		)
	# Optional AIUpdateInterface idle-rescan cadence. The one-shot jitter flag
	# is armed at spawn/reset; the next-check tick is created only by the first
	# eligible idle scan. Absent authoring preserves legacy bytes and RNG order.
	if (
		int(unit_rule.get("mood_attack_check_rate_ticks", 0)) > 0
		and unit_rule.has("auto_acquire_enabled")
		and unit_rule.has("auto_acquire_attack_buildings")
		and unit_rule.has("auto_acquire_while_stealthed")
	):
		entities[id]["mood_attack_check_rate_ticks"] = int(
			unit_rule["mood_attack_check_rate_ticks"]
		)
		entities[id]["mood_randomize_next_check"] = true
	# File the new battalion immediately: units spawned mid-tick (production
	# exits, summons) must be acquirable by battalions stepped later in the same
	# tick, exactly as the old full scans saw them.
	_spatial_sync(entities[id])
	for tech_id_value in (team_upgrades.get(team, {}) as Dictionary).keys():
		_apply_equipment_to_horde(entities[id], _equipment_ids_for_forge_upgrade(String(tech_id_value)))
	if (
		String(unit_rule.get("category", "")) == "hero"
		or not (_unit_ability_rules.get(String(entities[id].get("unit_type", "")), []) as Array).is_empty()
	):
		_attach_hero_ability_state(entities[id])
	var has_registered_experience := not (
		_unit_experience_rules.get(String(entities[id].get("unit_type", "")), {}) as Dictionary
	).is_empty()
	_attach_experience_state(entities[id])
	_attach_module_contracts(entities[id])
	# ExperienceLevelCreate is a creation module, independent of whether this
	# bounded summon leaf carries a complete ExperienceLevel progression table.
	# Ordinary unit rules omit this key and preserve their existing XP path.
	if int(unit_rule.get("creation_experience_rank", 0)) > 0:
		entities[id]["level"] = int(unit_rule["creation_experience_rank"])
		if not has_registered_experience:
			_apply_experience_level_effects(
				entities[id],
				unit_rule.get("creation_experience_effects", {}) as Dictionary
			)
	_record_hero_rank_attainment(entities[id])
	_refresh_banner_carrier_state(entities[id])


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
	if not structure_build_rules_for_team(team).has("forge"):
		return
	if compiled_research_kinds_for_team(team).has("forge"):
		# The compiled research surface (the forge's authored PLAYER technology
		# sales) replaces the recorded provisional contracts below; research
		# completion then grants the technology and the per-battalion purchase
		# path equips it — the retail two-tier flow, not the conflation.
		return
	var contracts := structure_upgrade_contracts_for_team(team)
	for upgrade_id_value in FORGE_UPGRADE_CONTRACTS.keys():
		var upgrade_id := String(upgrade_id_value)
		if contracts.has(upgrade_id):
			continue
		var source: Dictionary = FORGE_UPGRADE_CONTRACTS[upgrade_id]
		contracts[upgrade_id] = {
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
	return _upgrades_subsystem()._sorted_upgrade_ids(applied)


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
	_upgrades_subsystem()._apply_team_upgrade_to_hordes(team, upgrade_id)


func _apply_structure_create_grants(
	building: Dictionary,
	apply_create_when_complete: bool,
	apply_build_complete: bool
) -> void:
	## GrantUpgradeCreate is idempotent in retail's upgrade sinks. Keep the
	## converted lifecycle edge explicit: an UNDER_CONSTRUCTION exemption may
	## grant on creation only for an already-complete object, while the BFME
	## foundation form grants when construction completes.
	var team := int(building.get("team", -1))
	var kind := String(building.get("structure_kind", ""))
	var grants: Array = structure_create_grants_for_team(team).get(kind, [])
	for grant_value in grants:
		var grant := grant_value as Dictionary
		if (
			apply_create_when_complete
			and bool(grant.get("onCreateWhenComplete", false))
			and float(building.get("construction_progress", 0.0)) >= 1.0
		):
			_apply_structure_granted_upgrade(building, grant)
		if apply_build_complete and bool(grant.get("onBuildComplete", false)):
			_apply_structure_granted_upgrade(building, grant)


func _apply_structure_granted_upgrade(building: Dictionary, grant: Dictionary) -> void:
	_upgrades_subsystem()._apply_structure_granted_upgrade(building, grant)


func _apply_structure_inherit_upgrades(building: Dictionary) -> void:
	_upgrades_subsystem()._apply_structure_inherit_upgrades(building)


func queue_structure_upgrade(team: int, structure_id: int, upgrade_id: String) -> Dictionary:
	return _upgrades_subsystem().queue_structure_upgrade(team, structure_id, upgrade_id)


func structure_upgrade_queue_state(structure_id: int) -> Array[Dictionary]:
	return _upgrades_subsystem().structure_upgrade_queue_state(structure_id)


var _named_wall_slot_types: Dictionary = {}


static func _document_is_wall_upgrade_slot(document: Dictionary) -> bool:
	## A map-placed wall piece retail authors weaponless: it offers upgrade
	## commands (Upgrade_TrebuchetTurret / OpenGarrison / PosternGate) on its
	## trained command sets, or grants Upgrade_TrebuchetTurret on creation.
	var gameplay := ((document.get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary)
	for grant_value in gameplay.get("createGrants", []) as Array:
		if String(JSON.stringify(grant_value)).to_lower().contains("upgrade_trebuchetturret"):
			return true
	for set_value in gameplay.get("trainedCommandSets", []) as Array:
		for slot_value in (set_value as Dictionary).get("slots", []) as Array:
			var command_id := String((slot_value as Dictionary).get("commandId", "")).to_lower()
			if command_id.contains("trebuchetturret") or command_id.contains("opengarrison") or command_id.contains("posterngate"):
				return true
	return false


func structure_upgrade_commands(structure_id: int) -> Array[Dictionary]:
	return _upgrades_subsystem().structure_upgrade_commands(structure_id)


func _sorted_unit_upgrade_commands(unit_type: String) -> Array:
	return _upgrades_subsystem()._sorted_unit_upgrade_commands(unit_type)


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
	return _upgrades_subsystem()._discounted_battalion_upgrade_cost(team, command)


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
	return _upgrades_subsystem().queue_battalion_upgrade(team, entity_id, upgrade_id)


func battalion_upgrade_queue_state(entity_id: int) -> Array[Dictionary]:
	return _upgrades_subsystem().battalion_upgrade_queue_state(entity_id)


func _step_battalion_upgrades() -> void:
	_upgrades_subsystem()._step_battalion_upgrades()


func _apply_structure_death_refund(building: Dictionary) -> void:
	## Compatibility entry point retained for focused runners and old callers.
	## RefundDie is an Object DieMux, not a structure-only behavior.
	if not bool(building.get("structure_module_contracts_attached", false)):
		_attach_structure_module_contracts(building)
	_apply_refund_die_on_death(building)


func _apply_refund_die_on_death(owner: Dictionary) -> void:
	## Retail game.dat RefundDie::onDie: the death edge is delivered once by the
	## DieMux, then UNDER_CONSTRUCTION/SOLD, current owner, prerequisites and the
	## object's cached build cost are evaluated in that order.  A failed check is
	## not a deferred opportunity: there is no second death callback later.
	var typed_policies := owner.get("refund_die", []) as Array
	if not typed_policies.is_empty():
		if bool(owner.get("refund_die_death_dispatched", false)):
			return
		owner["refund_die_death_dispatched"] = true
		var statuses := owner.get("object_status", {}) as Dictionary
		var death_blocker := ""
		if (
			bool(statuses.get("UNDER_CONSTRUCTION", false))
			or (
				owner.has("construction_progress")
				and float(owner.get("construction_progress", 1.0)) < 1.0
			)
		):
			death_blocker = "UNDER_CONSTRUCTION"
		elif bool(statuses.get("SOLD", false)):
			death_blocker = "SOLD"
		if death_blocker != "":
			for policy_index in typed_policies.size():
				var blocked_policy := typed_policies[policy_index] as Dictionary
				blocked_policy["death_blocker"] = death_blocker
				typed_policies[policy_index] = blocked_policy
			owner["refund_die"] = typed_policies
			return
		var team := int(owner.get("team", -1))
		if not team_resources.has(team):
			return
		var build_cost_value: Variant = owner.get("cached_build_cost")
		for policy_index in typed_policies.size():
			var policy := typed_policies[policy_index] as Dictionary
			var upgrade_required := String(policy.get("upgrade_required", ""))
			if upgrade_required != "" and not (team_upgrades.get(team, {}) as Dictionary).has(upgrade_required): continue
			var building_filter := policy.get("building_required", []) as Array
			if not building_filter.is_empty() and not _team_has_required_building_filter(team, building_filter): continue
			if typeof(build_cost_value) not in [TYPE_INT, TYPE_FLOAT] or float(build_cost_value) < 0.0:
				var unsupported := policy.get("unsupported_semantics", []) as Array
				if not unsupported.has("structure-build-cost-unresolved"): unsupported.append("structure-build-cost-unresolved")
				policy["unsupported_semantics"] = unsupported
				typed_policies[policy_index] = policy
				continue
			var amount := ceili(float(build_cost_value) * float(policy.get("fraction", 0.0)))
			policy["refund_amount"] = amount
			typed_policies[policy_index] = policy
			if amount <= 0: continue
			team_resources[team] = resources_for_team(team) + amount
			_emit_event("economy.refund", int(owner.get("id", 0)), 0, {"team": team, "amount": amount, "upgrade_id": upgrade_required, "building_required": building_filter, "module": "RefundDie"})
		owner["refund_die"] = typed_policies
		return
	var team := int(owner.get("team", -1))
	var kind := String(owner.get("structure_kind", ""))
	# Compatibility only for stale structure documents predating typed
	# moduleContracts. A typed row never falls through and cannot double-refund.
	var bundle: Dictionary = structure_upgrade_effects_for_team(team).get(kind, {})
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
		_emit_event("economy.refund", int(owner.get("id", 0)), 0, {"team": team, "amount": refund, "upgrade_id": upgrade_id})


func _team_has_required_building_filter(team: int, filter: Array) -> bool:
	## BuildingRequired is a SAGE object filter. Preserve every token and test
	## it against living authored structure identities, not display names.
	if filter.is_empty(): return true
	var registry: Dictionary = _rules.get("producer_kind_registry", {}) as Dictionary
	if registry.is_empty(): registry = (_rules.get("faction_manifest", {}) as Dictionary).get("producer_kind_registry", {}) as Dictionary
	for structure_id in structure_ids(team):
		var candidate := structures[structure_id] as Dictionary
		if int(candidate.get("health", 0)) <= 0: continue
		var candidate_status := candidate.get("object_status", {}) as Dictionary
		if (
			bool(candidate_status.get("EFFECTIVELY_DEAD", false))
			or bool(candidate_status.get("DESTROYED", false))
		):
			continue
		var traits := {"STRUCTURE": true}
		for value in [candidate.get("source_object_id", ""), candidate.get("object_id", ""), candidate.get("structure_kind", ""), candidate.get("category", "")]:
			if String(value) != "": traits[String(value).to_upper()] = true
		var inert := false
		for kind_value in candidate.get("kind_of", []) as Array:
			var kind_token := String(kind_value).to_upper()
			traits[kind_token] = true
			if kind_token == "INERT": inert = true
		if inert: continue
		for object_id_value in registry.keys():
			if String(registry[object_id_value]).to_lower() == String(candidate.get("structure_kind", "")).to_lower(): traits[String(object_id_value).to_upper()] = true
		var positive: Array[String] = []
		var excluded := false
		for token_value in filter:
			var token := String(token_value).to_upper()
			if token.begins_with("+"): positive.append(token.substr(1))
			elif token.begins_with("-") and traits.has(token.substr(1)): excluded = true
		if excluded: continue
		if not positive.is_empty():
			for required_trait in positive:
				if traits.has(required_trait): return true
		elif filter.has("ANY") or filter.has("ALL"):
			return true
	return false


func _income_with_upgrade_bonus(team: int, building: Dictionary, base_income: int) -> int:
	return _economy_subsystem().income_with_upgrade_bonus(team, building, base_income)


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
var _spellbook_command_points_upgrade: Dictionary = {}
var _team_sciences := {PLAYER_TEAM: [], ENEMY_TEAM: []}
var _power_cooldown_until := {PLAYER_TEAM: {}, ENEMY_TEAM: {}}
## NONPRESSABLE passive/one-shot activations and their live Scavenger scale.
## Both are authoritative match state and therefore snapshot/hash state below.
var _consumed_nonpressable_powers: Dictionary = {PLAYER_TEAM: {}, ENEMY_TEAM: {}}
var _scavenger_bounty_percent: Dictionary = {PLAYER_TEAM: 0.0, ENEMY_TEAM: 0.0}
## Picks made since the last ACCEPT: RESET refunds exactly these ("unspent
## picks" — casting a staged pick spends it and it can no longer be refunded).
var _staged_purchases := {PLAYER_TEAM: [], ENEMY_TEAM: []}
## Per-team spellbook TREE overrides for cross-faction matches (two different
## factions in one sim). Empty by default: every team then resolves against the
## single global tree above, so the default same-faction match is byte-identical
## and the signature does not move. A team present here plays its OWN faction's
## powers/costs/prereqs/reloads — team ownership overlays (points, purchased,
## sciences, cooldowns, staged) stay in the per-team maps already declared.
## team:int -> {ready, powers, order, sciences, science_to_power, intrinsic, document}
var _team_spellbooks: Dictionary = {}
## Single-player pause seam for the spellbook orb: while the palantir is open
## the sim clock halts (ticks, production, AI — everything in tick()). The
## slice drives this through set_spellbook_orb_open; the escape-menu pause in
## the slice composes independently (either one halts the clock).
var clock_paused := false


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
	return _powers_subsystem()._spellbook_effect_support(power_row, fields, references, modifier_leaves, object_leaves, ocl_leaves, weapon_leaves)


func _spellbook_field_float(fields: Dictionary, key: String, fallback: float) -> float:
	return _powers_subsystem()._spellbook_field_float(fields, key, fallback)


func _parse_modifier_row(value: String) -> Dictionary:
	return _powers_subsystem()._parse_modifier_row(value)


func _spellbook_ocl_support(power_row: Dictionary, references: Dictionary, modifier_leaves: Dictionary, object_leaves: Dictionary, ocl_leaves: Dictionary, weapon_leaves: Dictionary, create_location: String = "", secondary_object_filter: String = "") -> Dictionary:
	return _powers_subsystem()._spellbook_ocl_support(power_row, references, modifier_leaves, object_leaves, ocl_leaves, weapon_leaves, create_location, secondary_object_filter)


func _spellbook_has_unconverted_hatch_payload(leaf: Dictionary, object_leaves: Dictionary, ocl_leaves: Dictionary) -> bool:
	return _powers_subsystem()._spellbook_has_unconverted_hatch_payload(leaf, object_leaves, ocl_leaves)


func _spellbook_hatch_payload_leaves(leaf: Dictionary, object_leaves: Dictionary, ocl_leaves: Dictionary) -> Array:
	return _powers_subsystem()._spellbook_hatch_payload_leaves(leaf, object_leaves, ocl_leaves)


func _spellbook_ocl_named_gap(spawns: Array, object_leaves: Dictionary, ocl_leaves: Dictionary, create_location: String, secondary_object_filter: String) -> Dictionary:
	return _powers_subsystem()._spellbook_ocl_named_gap(spawns, object_leaves, ocl_leaves, create_location, secondary_object_filter)


var _weather_effects: Array[Dictionary] = []


func _spellbook_weather_modifier_support(field_values: Dictionary, field_resolved: Dictionary, modifier_leaves: Dictionary) -> Dictionary:
	return _powers_subsystem()._spellbook_weather_modifier_support(field_values, field_resolved, modifier_leaves)


func _spellbook_weather_anticategory_support(field_values: Dictionary, field_resolved: Dictionary) -> Dictionary:
	return _powers_subsystem()._spellbook_weather_anticategory_support(field_values, field_resolved)


func _spellbook_untamed_allegiance_support(field_values: Dictionary, field_resolved: Dictionary) -> Dictionary:
	return _powers_subsystem()._spellbook_untamed_allegiance_support(field_values, field_resolved)


func _spellbook_fire_weapon_support(spawns: Array, weapon_leaves: Dictionary) -> Dictionary:
	return _powers_subsystem()._spellbook_fire_weapon_support(spawns, weapon_leaves)


func _spellbook_weapon_damage_nuggets(weapon: Dictionary, weapon_leaves: Dictionary) -> Array:
	return _powers_subsystem()._spellbook_weapon_damage_nuggets(weapon, weapon_leaves)


func _spellbook_weapon_field(weapon: Dictionary, key: String) -> float:
	return _powers_subsystem()._spellbook_weapon_field(weapon, key)


func _spellbook_structure_summon_support(spawn: Dictionary, weapon_leaves: Dictionary) -> Dictionary:
	return _powers_subsystem()._spellbook_structure_summon_support(spawn, weapon_leaves)


func _spellbook_direct_summon_support(spawns: Array, modifier_leaves: Dictionary, object_leaves: Dictionary, weapon_leaves: Dictionary) -> Dictionary:
	return _powers_subsystem()._spellbook_direct_summon_support(spawns, modifier_leaves, object_leaves, weapon_leaves)


func _spellbook_create_pick_count(create: Dictionary, multiplier: int = 1) -> int:
	return _powers_subsystem()._spellbook_create_pick_count(create, multiplier)


func _spellbook_create_enabled(create: Dictionary, owned_upgrades: Dictionary = {}) -> bool:
	return _powers_subsystem()._spellbook_create_enabled(create, owned_upgrades)


func _spellbook_summon_support(spawns: Array, modifier_leaves: Dictionary, object_leaves: Dictionary, ocl_leaves: Dictionary, weapon_leaves: Dictionary) -> Dictionary:
	return _powers_subsystem()._spellbook_summon_support(spawns, modifier_leaves, object_leaves, ocl_leaves, weapon_leaves)


func _spellbook_summon_literal_preview(spawns: Array, object_leaves: Dictionary, ocl_leaves: Dictionary) -> Dictionary:
	return _powers_subsystem()._spellbook_summon_literal_preview(spawns, object_leaves, ocl_leaves)


func _spellbook_summon_rule(target_leaf: Dictionary, modifier_leaves: Dictionary, object_leaves: Dictionary, weapon_leaves: Dictionary) -> Dictionary:
	return _powers_subsystem()._spellbook_summon_rule(target_leaf, modifier_leaves, object_leaves, weapon_leaves)


func _spellbook_summon_aura_rules(member: Dictionary, modifier_leaves: Dictionary) -> Dictionary:
	return _powers_subsystem()._spellbook_summon_aura_rules(member, modifier_leaves)


func _spellbook_one_summon_aura_rule(member: Dictionary, aura: Dictionary, modifier_leaves: Dictionary, allow_marker_modifiers: bool = false) -> Dictionary:
	return _powers_subsystem()._spellbook_one_summon_aura_rule(member, aura, modifier_leaves, allow_marker_modifiers)


func _spellbook_grove_chain(references: Dictionary, object_leaves: Dictionary, ocl_leaves: Dictionary) -> Dictionary:
	return _powers_subsystem()._spellbook_grove_chain(references, object_leaves, ocl_leaves)


func _spellbook_grove_support(field_values: Dictionary, field_resolved: Dictionary, references: Dictionary, modifier_leaves: Dictionary, object_leaves: Dictionary, ocl_leaves: Dictionary = {}, object_field: String = "ElvenGroveObject") -> Dictionary:
	return _powers_subsystem()._spellbook_grove_support(field_values, field_resolved, references, modifier_leaves, object_leaves, ocl_leaves, object_field)


func _spellbook_field_ping_support(spawns: Array, modifier_leaves: Dictionary) -> Dictionary:
	return _powers_subsystem()._spellbook_field_ping_support(spawns, modifier_leaves)


func _spellbook_ping_invisibility_rules(leaf: Dictionary) -> Dictionary:
	return _powers_subsystem()._spellbook_ping_invisibility_rules(leaf)


func _spellbook_cloudbreak_support(field_values: Dictionary, field_resolved: Dictionary) -> Dictionary:
	return _powers_subsystem()._spellbook_cloudbreak_support(field_values, field_resolved)


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
	return _powers_subsystem()._cast_spellbook_scavenger(team, effect)


func _award_scavenger_bounty(attacker_id: int, victim: Dictionary, victim_kind: String) -> int:
	return _economy_subsystem().award_scavenger_bounty(attacker_id, victim, victim_kind)


func cast_power(team: int, power_id: String, point: Vector2) -> Dictionary:
	return _powers_subsystem().cast_power(team, power_id, point)


func cast_heal(team: int, point: Vector2) -> Dictionary:
	return _powers_subsystem().cast_heal(team, point)


func cast_rally(team: int, point: Vector2) -> Dictionary:
	return _powers_subsystem().cast_rally(team, point)


func _spellbook_object_kinds(row: Dictionary) -> Array:
	return _powers_subsystem()._spellbook_object_kinds(row)


func _spellbook_affects(row: Dictionary, filter_text: String) -> bool:
	return _powers_subsystem()._spellbook_affects(row, filter_text)


func _spellbook_filter_has_kind_terms(filter_text: String) -> bool:
	return _powers_subsystem()._spellbook_filter_has_kind_terms(filter_text)


func _cast_spellbook_heal(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	return _powers_subsystem()._cast_spellbook_heal(team, effect, point)


func _cast_spellbook_structure_heal(team: int, effect: Dictionary, point: Vector2, radius: float) -> Dictionary:
	return _powers_subsystem()._cast_spellbook_structure_heal(team, effect, point, radius)


func _cast_spellbook_attribute_modifier(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	return _powers_subsystem()._cast_spellbook_attribute_modifier(team, effect, point)


func _cast_spellbook_fire_weapon(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	return _powers_subsystem()._cast_spellbook_fire_weapon(team, effect, point)


func _cast_spellbook_summon(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	return _powers_subsystem()._cast_spellbook_summon(team, effect, point)


func _terminal_visible_summon_count(targets: Array) -> int:
	return _powers_subsystem()._terminal_visible_summon_count(targets)


func _spellbook_resolve_summon_targets(target_groups: Array) -> Array:
	return _powers_subsystem()._spellbook_resolve_summon_targets(target_groups)


func _cast_spellbook_structure_summon(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	return _powers_subsystem()._cast_spellbook_structure_summon(team, effect, point)


func _cast_spellbook_grove(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	return _powers_subsystem()._cast_spellbook_grove(team, effect, point)


func _cast_spellbook_field_ping(team: int, power_id: String, effect: Dictionary, point: Vector2) -> Dictionary:
	return _powers_subsystem()._cast_spellbook_field_ping(team, power_id, effect, point)


func team_revealed_regions(team: int) -> Array:
	return _powers_subsystem().team_revealed_regions(team)


func field_ping_count() -> int:
	return _powers_subsystem().field_ping_count()


func _step_field_pings() -> void:
	_powers_subsystem()._step_field_pings()


func _revoke_field_ping_invisibility(ping: Dictionary) -> void:
	_powers_subsystem()._revoke_field_ping_invisibility(ping)


func _cast_spellbook_cloudbreak(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	return _powers_subsystem()._cast_spellbook_cloudbreak(team, effect, point)


func _revoke_opposing_weather_for_cloudbreak(team: int) -> void:
	_powers_subsystem()._revoke_opposing_weather_for_cloudbreak(team)


func _cast_spellbook_weather_modifier(team: int, power_id: String, effect: Dictionary) -> Dictionary:
	return _powers_subsystem()._cast_spellbook_weather_modifier(team, power_id, effect)


func _cast_spellbook_weather_anticategory(team: int, power_id: String, effect: Dictionary) -> Dictionary:
	return _powers_subsystem()._cast_spellbook_weather_anticategory(team, power_id, effect)


func _apply_weather_modifier(entry: Dictionary) -> int:
	return _powers_subsystem()._apply_weather_modifier(entry)


func _set_leadership_suppression_source(row: Dictionary, source_key: String, expire_tick: int) -> void:
	_powers_subsystem()._set_leadership_suppression_source(row, source_key, expire_tick)


func _erase_leadership_suppression_source(row: Dictionary, source_key: String) -> void:
	_powers_subsystem()._erase_leadership_suppression_source(row, source_key)


func _refresh_leadership_suppression(row: Dictionary) -> int:
	return _powers_subsystem()._refresh_leadership_suppression(row)


func _apply_weather_anticategory(entry: Dictionary) -> int:
	return _powers_subsystem()._apply_weather_anticategory(entry)


func _step_weather_effects() -> void:
	_powers_subsystem()._step_weather_effects()


func active_weather_effects() -> Array:
	return _powers_subsystem().active_weather_effects()


func _migrate_restored_weather_sources() -> void:
	_powers_subsystem()._migrate_restored_weather_sources()


func _cast_spellbook_creep_allegiance(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	return _powers_subsystem()._cast_spellbook_creep_allegiance(team, effect, point)


func _apply_scenario_structure_faction_command_set(row: Dictionary, team: int) -> Dictionary:
	## Resolve the owning team's exact PLAYER upgrade, then let the generic typed
	## CommandSetUpgrade effect graph choose the authored set. The trained-set
	## scan remains compatibility-only for selected packs cooked before that graph
	## was accepted; a recook removes this branch without changing gameplay.
	var sets := row.get("scenario_trained_command_sets", []) as Array
	if sets.is_empty():
		return {"ok": false, "reason": "no-trained-command-sets"}
	var side_result := team_retail_side(team)
	if side_result.has("reason"):
		return {"ok": false, "reason": String(side_result["reason"])}
	var side := String(side_result.get("side", ""))
	var upgrade_stem := {"Dwarves": "Dwarf", "Elves": "Elf"}.get(side, side) as String
	var faction_upgrade := "Upgrade_%sFaction" % upgrade_stem
	var completed := row.get("completed_upgrades", []) as Array
	if not completed.has(faction_upgrade):
		completed.append(faction_upgrade)
		completed.sort()
		row["completed_upgrades"] = completed
	var graph_result := _reconcile_structure_command_set_upgrades(row)
	if bool(graph_result.get("accepted_graph", false)):
		if not bool(graph_result.get("ok", false)):
			return graph_result
		return {
			"ok": true,
			"reason": "",
			"upgrade_id": faction_upgrade,
			"command_set_id": String(row.get("command_set_id", "")),
			"effect_id": String(graph_result.get("effect_id", "")),
		}
	# Compatibility for the currently selected pre-recook neutral artifacts.
	var selected: Dictionary = {}
	for set_value in sets:
		if typeof(set_value) != TYPE_DICTIONARY:
			continue
		var candidate := set_value as Dictionary
		if String(candidate.get("kind", "")) == "upgraded" and (candidate.get("triggeredBy", []) as Array).has(faction_upgrade):
			selected = candidate
			break
	if selected.is_empty():
		return {"ok": false, "reason": "faction-upgrade-not-authored", "upgrade_id": faction_upgrade}
	var prior := String(row.get("command_set_id", row.get("default_command_set_id", "")))
	var selected_id := String(selected.get("id", ""))
	if selected_id == "":
		return {"ok": false, "reason": "authored-command-set-id-empty"}
	row["command_set_id"] = selected_id
	if prior != selected_id:
		_emit_event("upgrade.scenario_command_set", int(row.get("id", 0)), 0, {
			"team": team,
			"upgrade_id": faction_upgrade,
			"from": prior,
			"to": selected_id,
		})
	return {"ok": true, "reason": "", "upgrade_id": faction_upgrade, "command_set_id": selected_id}


func _structure_command_set_upgrade_effects(row: Dictionary) -> Array[Dictionary]:
	var source: Array = []
	if row.has("scenario_command_set_upgrade_effects"):
		source = row.get("scenario_command_set_upgrade_effects", []) as Array
	else:
		var team := int(row.get("team", -1))
		var kind := String(row.get("structure_kind", ""))
		var bundle := structure_upgrade_effects_for_team(team).get(kind, {}) as Dictionary
		source = bundle.get("effects", []) as Array
	var by_id: Dictionary = {}
	var edge_ids: Dictionary = {}
	for effect_value in source:
		if typeof(effect_value) != TYPE_DICTIONARY:
			continue
		var effect := effect_value as Dictionary
		if String(effect.get("kind", "")) != "command-set-transition":
			continue
		var effect_id := String(effect.get("effectId", ""))
		if effect_id == "":
			continue
		if by_id.has(effect_id):
			var existing := by_id[effect_id] as Dictionary
			for field in ["game", "triggerUpgradeIds", "triggerSemantics", "commandSetId", "moduleTag", "moduleOrdinal", "commandSetProvenance"]:
				if existing.get(field) != effect.get(field):
					return []
		else:
			by_id[effect_id] = effect
			edge_ids[effect_id] = {}
		var edge_key := String(effect.get("upgradeId", "")).to_lower()
		if edge_key == "" or (edge_ids[effect_id] as Dictionary).has(edge_key):
			return []
		(edge_ids[effect_id] as Dictionary)[edge_key] = true
	var result: Array[Dictionary] = []
	for effect_id_value in by_id.keys():
		var effect := by_id[effect_id_value] as Dictionary
		var expected_edges: Dictionary = {}
		for trigger_value in effect.get("triggerUpgradeIds", []) as Array:
			expected_edges[String(trigger_value).to_lower()] = true
		if (edge_ids[effect_id_value] as Dictionary).keys().size() != expected_edges.keys().size():
			return []
		for expected_key in expected_edges.keys():
			if not (edge_ids[effect_id_value] as Dictionary).has(expected_key):
				return []
		result.append(effect.duplicate(true))
	result.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		if int(a.get("moduleOrdinal", 0)) != int(b.get("moduleOrdinal", 0)):
			return int(a.get("moduleOrdinal", 0)) < int(b.get("moduleOrdinal", 0))
		if String(a.get("sourceIni", "")) != String(b.get("sourceIni", "")):
			return String(a.get("sourceIni", "")).naturalnocasecmp_to(String(b.get("sourceIni", ""))) < 0
		if int(a.get("line", 0)) != int(b.get("line", 0)):
			return int(a.get("line", 0)) < int(b.get("line", 0))
		return String(a.get("effectId", "")).naturalnocasecmp_to(String(b.get("effectId", ""))) < 0
	)
	return result


func _reconcile_structure_command_set_upgrades(row: Dictionary) -> Dictionary:
	var effects := _structure_command_set_upgrade_effects(row)
	if effects.is_empty():
		var declared_graph := false
		var declared_source: Array = row.get("scenario_command_set_upgrade_effects", []) as Array
		if not row.has("scenario_command_set_upgrade_effects"):
			var bundle := structure_upgrade_effects_for_team(int(row.get("team", -1))).get(String(row.get("structure_kind", "")), {}) as Dictionary
			declared_source = bundle.get("effects", []) as Array
		for effect_value in declared_source:
			if typeof(effect_value) == TYPE_DICTIONARY and String((effect_value as Dictionary).get("kind", "")) == "command-set-transition":
				declared_graph = true
				break
		return {"ok": false, "reason": "malformed-command-set-upgrade-graph" if declared_graph else "no-accepted-command-set-upgrade", "accepted_graph": declared_graph}
	var active_game := String(_rules.get("game", "")).to_lower()
	var team_owned := team_upgrades.get(int(row.get("team", -1)), {}) as Dictionary
	var object_owned := row.get("completed_upgrades", []) as Array
	var selected: Dictionary = {}
	for effect in effects:
		if String(effect.get("game", "")).to_lower() != active_game:
			return {"ok": false, "reason": "command-set-upgrade-edition-mismatch", "accepted_graph": true}
		var matched := 0
		var triggers := effect.get("triggerUpgradeIds", []) as Array
		for trigger_value in triggers:
			var trigger := String(trigger_value)
			if _dictionary_has_casefolded_key(team_owned, trigger) or _array_has_casefolded_string(object_owned, trigger):
				matched += 1
		var eligible := matched == triggers.size() if String(effect.get("triggerSemantics", "")) == "all" else matched > 0
		if eligible:
			selected = effect
	var command_field := "command_set_id" if row.has("scenario_source_object_id") else "command_set"
	if not row.has("command_set_upgrade_base"):
		row["command_set_upgrade_base"] = String(row.get(command_field, row.get("default_command_set_id", "")))
	var next_set := String(row.get("command_set_upgrade_base", ""))
	var next_effect := ""
	if not selected.is_empty():
		next_set = String(selected.get("commandSetId", ""))
		next_effect = String(selected.get("effectId", ""))
	var prior_set := String(row.get(command_field, ""))
	var prior_effect := String(row.get("command_set_upgrade_active_effect", ""))
	row[command_field] = next_set
	row["command_set_upgrade_active_effect"] = next_effect
	row["command_set_upgrade_receipt"] = {
		"game": active_game,
		"effectId": next_effect,
		"commandSetId": next_set,
		"triggerUpgradeIds": Array(selected.get("triggerUpgradeIds", [])).duplicate() if not selected.is_empty() else [],
		"triggerSemantics": String(selected.get("triggerSemantics", "")),
		"commandSetProvenance": (selected.get("commandSetProvenance", {}) as Dictionary).duplicate(true) if not selected.is_empty() else {},
		"customAnimationStatus": "deferred" if selected.has("customAnimation") else "absent",
	}
	if prior_set != next_set or prior_effect != next_effect:
		_emit_event("upgrade.command_set", int(row.get("id", 0)), 0, {
			"team": int(row.get("team", -1)), "game": active_game,
			"effect_id": next_effect, "from": prior_set, "to": next_set,
			"presentation_custom_anim": "deferred" if selected.has("customAnimation") else "absent",
		})
	return {"ok": not selected.is_empty(), "reason": "" if not selected.is_empty() else "triggers-unsatisfied", "accepted_graph": true, "effect_id": next_effect, "command_set_id": next_set}


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
	_powers_subsystem()._step_pending_power_effects()


func _fire_death_weapon(effect: Dictionary) -> void:
	_powers_subsystem()._fire_death_weapon(effect)


func _fire_power_strike(effect: Dictionary) -> void:
	_powers_subsystem()._fire_power_strike(effect)


func _apply_area_damage_to_battalion(id: int, amount: float, damage_type: String) -> void:
	_powers_subsystem()._apply_area_damage_to_battalion(id, amount, damage_type)


func _apply_area_damage_to_structure(structure_id: int, amount: float, damage_type: String) -> void:
	_powers_subsystem()._apply_area_damage_to_structure(structure_id, amount, damage_type)


func _fire_power_summon(effect: Dictionary) -> void:
	_powers_subsystem()._fire_power_summon(effect)


func _spawn_summon_targets(team: int, point: Vector2, targets: Array) -> Array:
	return _powers_subsystem()._spawn_summon_targets(team, point, targets)


func spawn_script_object(object_type: String, team: int, at: Vector2, ring_fallback := false, scenario_surface: String = "script-spawn") -> int:
	return _powers_subsystem().spawn_script_object(object_type, team, at, ring_fallback, scenario_surface)


func _step_summon_despawns() -> void:
	_powers_subsystem()._step_summon_despawns()


func _step_summon_auras() -> void:
	_powers_subsystem()._step_summon_auras()


func _refresh_one_summon_aura(source_id: int, source: Dictionary, aura: Dictionary) -> void:
	_powers_subsystem()._refresh_one_summon_aura(source_id, source, aura)


func _summon_aura_allows_relation(aura: Dictionary, same_team: bool) -> bool:
	return _powers_subsystem()._summon_aura_allows_relation(aura, same_team)


func _step_grove_auras() -> void:
	_powers_subsystem()._step_grove_auras()


func _spellbook_member_affects(
	row: Dictionary, filter_text: String, same_team: Variant = null
) -> bool:
	return _powers_subsystem()._spellbook_member_affects(row, filter_text, same_team)


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


func configure_map_named_object_namespace(names: Array) -> void:
	## Declare the installed map's complete named-object table. Sorted before
	## insertion so byte-equal configuration hashes identically on every peer
	## regardless of caller iteration order (the unpackable_bases discipline).
	var sorted := names.duplicate()
	sorted.sort()
	var table: Dictionary = {}
	for name_value in sorted:
		table[String(name_value)] = true
	map_named_object_namespace = {"names": table}


func map_named_object_namespace_declared() -> bool:
	return not map_named_object_namespace.is_empty()


func map_declares_named_object(name: String) -> bool:
	## Case-sensitive, like retail's strcmp over the name table.
	return (map_named_object_namespace.get("names", {}) as Dictionary).has(name)


# --- Script unit references (the WP16 shared namespace, SIM-owned) ---------
#
# team:int -> {reference_name: structure_id}. Owned by the SIM rather than
# the script-world adapter, DELIBERATELY: a bound reference changes what a
# later script action does (build_base_building at "AI_REF" succeeds or
# refuses on whether AI_REF is bound), which makes it sim-outcome-bearing
# state - and no such state may live outside the snapshot/hash boundary that
# save/load and late-join reproduce. A peer that adopts a snapshot must
# resolve every reference exactly as the peer that minted it, or byte-equal
# sims diverge on the very next scripted action.
#
# Keyed by TEAM because retail executes each AI player's script libraries in
# that player's own context: one world instance per script player, each with
# its own reference namespace. The team id is already authoritative state, so
# the key introduces nothing platform- or load-order-dependent.
#
# HASH INERTNESS: participates in the authoritative state ONLY when non-empty
# (empty-is-absent, the unpackable_bases discipline), so a match whose
# scripts never bind a reference contributes zero bytes to state_hash() and
# the frozen cross-platform pin stands untouched.

## See the block comment above. setup() clears it (match state, not config).
## Values are int (a structure id) or String (a base-flag name) - the two
## kinds of entry the shared namespace carries; see
## bind_script_unit_reference_to_base for why the flag kind exists.
var script_unit_references: Dictionary = {}
## team -> reference name -> entity id. Companion to script_unit_references
## (structure/base-flag store). Entity nearest-ref binds land here so AI
## SET_REF_TO_NEREST_* is not bag-only.
var script_entity_references: Dictionary = {}
## Pending CreateObjectDie spawns: {team, position, creation_list, source_entity, tick}.
## Observable sim state when an executable CreateObjectDie fires on death.
var create_object_die_pending: Array = []
## Match-scoped script construction permissions:
## team -> exact retail object type -> false. Absence means retail's default
## allowed state; an allow write erases the override and returns to pristine.
## Stored in the authoritative snapshot only while nonempty.
var building_permissions_by_team: Dictionary = {}


func set_building_allowed(team: int, object_type: String, allowed: bool) -> bool:
	if not _team_roster.has(team) or object_type == "":
		return false
	var permissions: Dictionary = building_permissions_by_team.get(team, {})
	var matching_key := ""
	for authored_value in permissions.keys():
		if String(authored_value).to_lower() == object_type.to_lower():
			matching_key = String(authored_value)
			break
	if matching_key != "":
		permissions.erase(matching_key)
	if not allowed:
		permissions[object_type] = false
	if permissions.is_empty():
		building_permissions_by_team.erase(team)
	else:
		building_permissions_by_team[team] = permissions
	return true


func _building_object_identity_key(value: String) -> String:
	## Source names are case-insensitive SAGE identifiers; converted runtime
	## ids are their punctuation-separated slugs. Collapse both to the same
	## alphanumeric key so MENFORTRESS, MenFortress, and
	## bfme2.object.men-fortress retain one identity.
	var folded := value.to_lower()
	var runtime_separator := folded.find(".object.")
	if runtime_separator >= 0:
		folded = folded.substr(runtime_separator + 8)
	var key := ""
	for index in folded.length():
		var code := folded.unicode_at(index)
		if (code >= 97 and code <= 122) or (code >= 48 and code <= 57):
			key += String.chr(code)
	return key


func building_permission_for_kind(team: int, structure_kind: String) -> Dictionary:
	var permissions: Dictionary = building_permissions_by_team.get(team, {})
	if permissions.is_empty():
		return {"known": true, "allowed": true}
	var source_types: Dictionary = {}
	var runtime_types: Dictionary = {}
	var identity_keys: Dictionary = {}
	var registry := _structure_source_registry(team_manifest_for(team))
	for source_value in registry.keys():
		if String(registry[source_value]) == structure_kind:
			source_types[String(source_value).to_lower()] = true
			identity_keys[_building_object_identity_key(String(source_value))] = true
	var structure_ids: Dictionary = (
		team_manifest_for(team).get("structure_object_ids", {}) as Dictionary
	)
	if structure_ids.has(structure_kind):
		var structure_object_id := String(structure_ids[structure_kind])
		runtime_types[structure_object_id] = true
		identity_keys[_building_object_identity_key(structure_object_id)] = true
	var expansion_rule: Dictionary = _expansion_build_rules.get(structure_kind, {})
	var expansion_object_id := String(expansion_rule.get("object_id", ""))
	if expansion_object_id != "":
		runtime_types[expansion_object_id] = true
		identity_keys[_building_object_identity_key(expansion_object_id)] = true
	if source_types.is_empty() and runtime_types.is_empty():
		return {
			"known": false,
			"allowed": false,
			"reason": "no retail object identity is registered for structure kind '%s'" % structure_kind,
		}
	var authored_types: Array = permissions.keys()
	authored_types.sort()
	for authored_value in authored_types:
		var authored_type := String(authored_value)
		if (
			source_types.has(authored_type.to_lower())
			or runtime_types.has(authored_type)
			or runtime_types.has(PlayableUnitAdapter.runtime_object_id(authored_type))
			or identity_keys.has(_building_object_identity_key(authored_type))
		):
			return {
				"known": true,
				"allowed": false,
				"object_type": authored_type,
			}
	return {"known": true, "allowed": true}


func bind_script_entity_reference(team: int, reference: String, entity_id: int) -> bool:
	if team < 0 or reference == "" or entity_id <= 0:
		return false
	if not entities.has(entity_id):
		return false
	if not script_entity_references.has(team):
		script_entity_references[team] = {}
	(script_entity_references[team] as Dictionary)[reference] = entity_id
	return true


func script_entity_reference(team: int, reference: String) -> int:
	return int((script_entity_references.get(team, {}) as Dictionary).get(reference, 0))


func bind_script_unit_reference(team: int, reference: String, structure_id: int) -> bool:
	## Bind (or re-point) `reference` for `team` to a concrete structure id.
	## References are mutable by design (retail re-points
	## AI_CURRENT_CONSTRUCTION_SITE constantly). An empty reference binds
	## nothing (vacuous true). A base-flag name is refused loudly - callers
	## are expected to have cleared the shadow check BEFORE mutating the sim,
	## so tripping this backstop means a caller skipped it.
	if reference == "":
		return true
	if unpackable_bases.has(reference):
		push_error(
			"bind_script_unit_reference refused: '%s' names a base flag; " % reference
			+ "flag names are owned by the unpackable-base table (callers must "
			+ "check the shadow rejection before mutating the sim)"
		)
		return false
	if not script_unit_references.has(team):
		script_unit_references[team] = {}
	(script_unit_references[team] as Dictionary)[reference] = structure_id
	return true


func bind_script_unit_reference_to_base(team: int, reference: String, base_name: String) -> bool:
	## Bind (or re-point) `reference` for `team` to a BASE FLAG by name.
	##
	## WHY A SECOND BINDING KIND, ON RETAIL EVIDENCE. SET_UNIT_REFERENCE's
	## subject slot in the shipped AI libraries is a base-flag name at 32 of
	## its 40 call sites (BASE_FLAG_1..16; the other 8 are BASE_SPAWN_1..8,
	## which this simulation does not model) - the AI aims
	## AI_CURRENT_CONSTRUCTION_SITE / AI_CURRENT_DEF_CONSTRUCTION_SITE at a
	## flag it has NOT yet unpacked and then builds through the reference. A
	## packed flag has no structure id (structure_id is 0 until unpack), so a
	## structure-id-only store could only refuse those 32 sites. Storing the
	## FLAG NAME is not "storing the source string" in the aliasing sense the
	## class comment forbids: a flag name is match configuration, immutable
	## for the match, so it cannot be re-aimed under the reference the way a
	## rebindable reference name could.
	##
	## Same store, same key, same empty-is-absent discipline - the VALUE is a
	## String here and an int for structure bindings, which is exactly the
	## "two kinds of entry" the shared object / unit-reference namespace is
	## documented to carry. Refuses (loudly) a reference that would shadow a
	## flag, and refuses an unknown base name rather than binding a dangling
	## handle.
	if reference == "":
		return true
	if unpackable_bases.has(reference):
		push_error(
			"bind_script_unit_reference_to_base refused: '%s' names a base flag; " % reference
			+ "flag names are owned by the unpackable-base table (callers must "
			+ "check the shadow rejection before mutating the sim)"
		)
		return false
	if not unpackable_bases.has(base_name):
		push_error(
			"bind_script_unit_reference_to_base refused: '%s' is not a base flag " % base_name
			+ "this simulation models; binding it would leave a dangling handle"
		)
		return false
	if not script_unit_references.has(team):
		script_unit_references[team] = {}
	(script_unit_references[team] as Dictionary)[reference] = base_name
	return true


func script_unit_reference(team: int, reference: String) -> int:
	## The structure id bound to `reference` for `team`; 0 when unbound OR
	## when the binding is a base-flag name (structure ids are never 0, and a
	## flag-valued binding is not a structure id - callers that need it must
	## read script_unit_reference_handle).
	var bound: Variant = (script_unit_references.get(team, {}) as Dictionary).get(reference, 0)
	return int(bound) if typeof(bound) == TYPE_INT else 0


func script_unit_reference_base(team: int, reference: String) -> String:
	## The base-flag name bound to `reference` for `team`; "" when unbound or
	## when the binding is a structure id.
	var bound: Variant = (script_unit_references.get(team, {}) as Dictionary).get(reference, 0)
	return String(bound) if typeof(bound) == TYPE_STRING else ""


# --- Script object-type lists (OBJECT_TYPE_LIST stores, SIM-owned) ----------
#
# Named, script-mutable SETS of retail object-type names: the subjects of
# OBJECTLIST_ADDOBJECTTYPE / OBJECTLIST_REMOVEOBJECTTYPE and the resolution
# target of every OBJECT_TYPE_LIST-typed script argument. Retail authors BOTH
# spellings in those slots - a declared list name ("Offensive_Units") and a
# plain object type ("IsengardUrukPit") - and resolves list-first with a
# single-type fallback; resolve_object_type_names below mirrors that exactly.
#
# STATE, NOT CONFIGURATION - decided on retail evidence, not assumption. The
# retail engine's ScriptEngine owns ONE global ObjectTypeList table per match,
# mutated mid-match by script actions and PERSISTED IN SAVE GAMES (OpenSAGE's
# ScriptingSystem.Persist serializes _objectTypeLists; each list is a
# HashSet<string> keyed by name). Mutable-by-actions plus save-file membership
# puts the table inside the snapshot/hash boundary, exactly like
# script_unit_references. UNLIKE the references it is NOT keyed by team: the
# retail store is engine-global (one namespace per match, however many script
# players run), and every peer executes the same lockstep script stream, so
# the converged table is identical on every peer by construction.
#
# CANONICAL FORM: member arrays are kept SORTED and UNIQUE (retail's HashSet
# has no order; a sorted array is the canonical serialization of a set), and
# a list whose last member is removed loses its KEY too, so an emptied table
# returns to the exact pristine hash.
#
# HASH INERTNESS: participates in the authoritative state ONLY when non-empty
# (empty-is-absent, the unpackable_bases discipline), so a match whose
# scripts never build a list contributes zero bytes to state_hash() and the
# frozen cross-platform pin stands untouched. setup() clears it (match state).

## list name -> sorted unique Array of retail object-type name Strings.
## See the block comment above. setup() clears it; hashed only when non-empty.
var script_object_type_lists: Dictionary = {}


func change_object_type_list(list_name: String, object_type: String, add: bool) -> Dictionary:
	## OBJECTLIST_ADDOBJECTTYPE (`add` true) / OBJECTLIST_REMOVEOBJECTTYPE
	## (`add` false). Set semantics, matching retail's HashSet store: a
	## duplicate add and an absent remove are successful no-ops. Empty names
	## refuse - "" is neither a list nor a type in the retail vocabulary, and
	## admitting it would mint an unreachable store entry.
	if list_name == "":
		return {"ok": false, "reason": "empty-list-name"}
	if object_type == "":
		return {"ok": false, "reason": "empty-object-type"}
	if add:
		var members: Array = script_object_type_lists.get(list_name, [])
		if not members.has(object_type):
			members.append(object_type)
			members.sort()
		script_object_type_lists[list_name] = members
		return {"ok": true, "reason": ""}
	if script_object_type_lists.has(list_name):
		var members: Array = script_object_type_lists[list_name]
		members.erase(object_type)
		if members.is_empty():
			# Empty-is-absent inside the container too: no empty list may
			# linger as a hash-visible key.
			script_object_type_lists.erase(list_name)
	return {"ok": true, "reason": ""}


func object_type_list_names() -> Array[String]:
	var names: Array[String] = []
	for name_value in script_object_type_lists.keys():
		names.append(String(name_value))
	names.sort()
	return names


func has_object_type_list(list_name: String) -> bool:
	return script_object_type_lists.has(list_name)


func resolve_object_type_names(object_type_list: String) -> Array:
	## Retail's OBJECT_TYPE_LIST argument resolution: a declared list answers
	## its members; any other name IS a single object type (the retail engine
	## looks the name up in the list table and falls back to reading the
	## string as one type - the authored corpus uses both spellings). This is
	## also the correct answer BEFORE list-building scripts have run: retail
	## in that state has no list either and reads the single type. Read-only.
	if script_object_type_lists.has(object_type_list):
		return (script_object_type_lists[object_type_list] as Array).duplicate()
	return [object_type_list]


# --- Script-team registry (named sub-player teams, SIM-owned) ---------------
#
# SAGE teams are independent identities even when they share a player owner.
# Definitions come from decoded map configuration, but membership and flags
# affect gameplay and therefore live inside snapshots/state_hash. Handles are
# typed because entity and structure ids occupy overlapping id spaces.
# Imported objectCount is evidence only: it can never mint a phantom member.
var script_teams: Dictionary = {}


func _script_owner_exists(owner: int) -> bool:
	return _is_combatant_team(owner) or owner == NEUTRAL_TEAM or owner == CREEP_TEAM


func register_script_team(
	team_name: String,
	owner: int,
	default_team: bool = false,
	handles: Array = [],
	membership_complete: bool = true,
	unresolved_members: Array = [],
	unmodeled_object_count: int = 0,
	dynamic_default_roster: bool = true,
	marker_only: bool = false
) -> Dictionary:
	if team_name == "":
		return {"ok": false, "reason": "script-team name is empty"}
	if not _script_owner_exists(owner):
		return {"ok": false, "reason": "script-team owner %d is unavailable" % owner}
	if script_teams.has(team_name):
		var existing := script_teams[team_name] as Dictionary
		if (
			int(existing.get("configured_owner", existing.get("owner", -1))) != owner
			or bool(existing.get("default", false)) != default_team
			or bool(existing.get("membership_incomplete", false)) == membership_complete
			or (existing.get("unresolved_members", []) as Array) != unresolved_members
			or int(existing.get("unmodeled_object_count", 0)) != unmodeled_object_count
			or bool(existing.get("explicit_default_membership", false))
			== dynamic_default_roster
			or bool(existing.get("marker_only", false)) != marker_only
		):
			return {"ok": false, "reason": "script team '%s' was rebound" % team_name}
		return {"ok": true, "reason": ""}
	if unmodeled_object_count < 0:
		return {"ok": false, "reason": "script team '%s' has a negative unmodeled count" % team_name}
	var canonical_unresolved: Array[String] = []
	for unresolved_value in unresolved_members:
		if typeof(unresolved_value) != TYPE_STRING or String(unresolved_value) == "":
			return {"ok": false, "reason": "script team '%s' has a malformed unresolved member name" % team_name}
		var unresolved_name := String(unresolved_value)
		canonical_unresolved.append(unresolved_name)
	canonical_unresolved.sort()
	var members: Array = []
	for handle_value in handles:
		if typeof(handle_value) != TYPE_DICTIONARY:
			return {"ok": false, "reason": "script team '%s' has a malformed member handle" % team_name}
		var handle := handle_value as Dictionary
		if (
			typeof(handle.get("kind")) != TYPE_STRING
			or not ["entity", "structure"].has(handle.get("kind"))
			or typeof(handle.get("id")) != TYPE_INT
			or typeof(handle.get("id")) == TYPE_BOOL
		):
			return {"ok": false, "reason": "script team '%s' has a malformed typed member handle" % team_name}
		var kind := String(handle["kind"])
		var object_id := int(handle["id"])
		var row: Dictionary
		if kind == "entity" and entities.has(object_id):
			row = entities[object_id] as Dictionary
		elif kind == "structure" and structures.has(object_id):
			row = structures[object_id] as Dictionary
		else:
			return {"ok": false, "reason": "script team '%s' references an unavailable %s %d" % [team_name, kind, object_id]}
		if int(row.get("team", -1)) != owner:
			return {"ok": false, "reason": "script team '%s' member owner disagrees" % team_name}
		var canonical := {"kind": kind, "id": object_id}
		if not members.has(canonical):
			members.append(canonical)
	members.sort_custom(_script_member_less)
	var record := {
		"owner": owner,
		# Match configuration is immutable even when a retail script later
		# changes the team's controlling player. Keeping the original owner
		# lets setup() reset the match and lets the snapshot view omit teams
		# whose controlling owner was never changed.
		"configured_owner": owner,
	}
	if default_team:
		record["default"] = true
		if not dynamic_default_roster:
			record["explicit_default_membership"] = true
	if not membership_complete:
		record["membership_incomplete"] = true
		if not canonical_unresolved.is_empty():
			record["unresolved_members"] = canonical_unresolved
		if unmodeled_object_count > 0:
			record["unmodeled_object_count"] = unmodeled_object_count
	if not members.is_empty():
		record["members"] = members
	if marker_only:
		if default_team or not members.is_empty():
			return {
				"ok": false,
				"reason": "marker-only team '%s' cannot be default or materialized" % team_name,
			}
		record["marker_only"] = true
	script_teams[team_name] = record
	mark_team_created(team_name)
	return {"ok": true, "reason": ""}


func _script_member_less(a: Dictionary, b: Dictionary) -> bool:
	var a_kind := String(a.get("kind", ""))
	var b_kind := String(b.get("kind", ""))
	return a_kind < b_kind or (a_kind == b_kind and int(a.get("id", -1)) < int(b.get("id", -1)))


func script_team_owner(team_name: String) -> Dictionary:
	if not script_teams.has(team_name):
		return {"ok": false, "reason": "script team '%s' is not registered" % team_name}
	return {"ok": true, "owner": int((script_teams[team_name] as Dictionary).get("owner", -1))}


func transfer_script_team_controlling_player(
	team_name: String, destination_owner: int
) -> Dictionary:
	## Retail TEAM_TRANSFER_TO_PLAYER calls Team::setControllingPlayer. It
	## changes the Team identity's controlling player; it is not Player asset
	## transfer and it does not merge/capture the Team's Object list.
	##
	## The measured BFME2/RotWK AI sites address civilian inheritance teams
	## containing tactical markers, which this sim deliberately does not
	## materialize. Restrict this surface to that exact safe shape. A later
	## combat-team transfer needs entity ownership, CP, upgrade, queue and
	## spatial invariants and must be a separate packet.
	if winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if not script_teams.has(team_name):
		return {"ok": false, "reason": "script team '%s' is not registered" % team_name}
	if not _script_owner_exists(destination_owner):
		return {
			"ok": false,
			"reason": "destination player %d is unavailable" % destination_owner,
		}
	if not _is_combatant_team(destination_owner):
		return {
			"ok": false,
			"reason": "destination player %d is not a combatant" % destination_owner,
		}
	var record := script_teams[team_name] as Dictionary
	if bool(record.get("default", false)):
		return {
			"ok": false,
			"reason": "default team '%s' is outside the retail inheritance-team scope" % team_name,
		}
	if not bool(record.get("marker_only", false)):
		return {
			"ok": false,
			"reason": "team '%s' lacks source-attested marker-only evidence" % team_name,
		}
	if int(record.get("configured_owner", -1)) != NEUTRAL_TEAM:
		return {
			"ok": false,
			"reason": "marker-only team '%s' was not configured under the civilian owner" % team_name,
		}
	if not (record.get("members", []) as Array).is_empty():
		return {
			"ok": false,
			"reason": "team '%s' has materialized members and requires entity transfer" % team_name,
		}
	record["owner"] = destination_owner
	script_teams[team_name] = record
	return {"ok": true, "reason": ""}


func script_team_members(team_name: String, living_only: bool = true) -> Dictionary:
	if not script_teams.has(team_name):
		return {"ok": false, "reason": "script team '%s' is not registered" % team_name}
	var record := script_teams[team_name] as Dictionary
	var owner := int(record.get("owner", -1))
	var source: Array = []
	if (
		bool(record.get("default", false))
		and not bool(record.get("explicit_default_membership", false))
	):
		for id_value in entity_ids():
			var entity_id := int(id_value)
			if int((entities[entity_id] as Dictionary).get("team", -1)) == owner:
				source.append({"kind": "entity", "id": entity_id})
		for id_value in structure_ids():
			var structure_id := int(id_value)
			if int((structures[structure_id] as Dictionary).get("team", -1)) == owner:
				source.append({"kind": "structure", "id": structure_id})
	else:
		source = (record.get("members", []) as Array).duplicate(true)
	var answer: Array = []
	for handle_value in source:
		var handle := handle_value as Dictionary
		var kind := String(handle.get("kind", ""))
		var object_id := int(handle.get("id", -1))
		var row: Dictionary
		if kind == "entity" and entities.has(object_id):
			row = entities[object_id] as Dictionary
		elif kind == "structure" and structures.has(object_id):
			row = structures[object_id] as Dictionary
		else:
			continue
		if int(row.get("team", -1)) != owner:
			continue
		if living_only and int(row.get("health", 0)) <= 0:
			continue
		answer.append({"kind": kind, "id": object_id})
	answer.sort_custom(_script_member_less)
	return {
		"ok": true,
		"members": answer,
		"complete": not bool(record.get("membership_incomplete", false)),
		"unresolved_members": (record.get("unresolved_members", []) as Array).duplicate(),
		"unmodeled_object_count": int(record.get("unmodeled_object_count", 0)),
	}


func _script_team_definition(record: Dictionary) -> Dictionary:
	var configured_owner := int(record.get("configured_owner", record.get("owner", -1)))
	var definition := {
		"owner": configured_owner,
		"configured_owner": configured_owner,
	}
	for key in [
		"default",
		"explicit_default_membership",
		"membership_incomplete",
		"unresolved_members",
		"unmodeled_object_count",
		"marker_only",
	]:
		if record.has(key):
			var value: Variant = record[key]
			definition[key] = value.duplicate(true) if value is Array or value is Dictionary else value
	return definition


func set_script_team_recruitable(team_name: String, enabled: bool) -> Dictionary:
	if not script_teams.has(team_name):
		return {"ok": false, "reason": "script team '%s' is not registered" % team_name}
	var record := script_teams[team_name] as Dictionary
	# This is a tri-state override in retail: never-set, explicitly true, and
	# explicitly false are distinct. Team::tryToRecruit first checks whether
	# setRecruitable() was called, then lets that value override the default
	# team / prototype setting. Erasing false would silently turn a scripted
	# refusal back into the prototype default.
	record["recruitable"] = enabled
	script_teams[team_name] = record
	return {"ok": true, "reason": ""}


func _script_team_state_view() -> Dictionary:
	## Team definitions are match configuration and are rebuilt when worlds are
	## installed. Only mutable membership/recruitable state crosses the dynamic
	## snapshot boundary. A changed controlling owner is mutable too. Default-
	## team aliases with none of these contribute zero
	## bytes, preserving the pristine-hash contract of merely binding a world.
	var view: Dictionary = {}
	var names := script_teams.keys()
	names.sort()
	for name_value in names:
		var name := String(name_value)
		var record := script_teams[name] as Dictionary
		if (
			record.has("members")
			or record.has("recruitable")
			or int(record.get("owner", -1))
			!= int(record.get("configured_owner", record.get("owner", -1)))
		):
			view[name] = record
	return view


# --- Team behavior state (TEAM_STATE + custom-state tokens, SIM-owned) ------
#
# The retail AI's own blackboard: the per-team state token TEAM_SET_STATE
# writes and TEAM_STATE_IS/_IS_NOT read, plus the custom-state token set
# TEAM_SET_CUSTOM_STATE toggles and TEAM_HAS_CUSTOM_STATE tests. The attack
# loops gate on these constantly (112 AI call sites across the six members).
#
# RETAIL SEMANTICS, SOURCED:
#   * TEAM_STATE is one plain string per team - Team.h:200 in the GPL
#     Generals/ZH reference declares `AsciiString m_state`, the BFME1
#     decompilation's Team.cpp matches it field-for-field, and
#     ScriptActions.cpp doSetTeamState is a bare setState(). No enum, no
#     validation, no case folding (AsciiString::operator== is strcmp), and no
#     engine consumer besides the two conditions - STORAGE IS THE ENTIRE
#     SEMANTIC. The default is the empty string (m_state is absent from the
#     Team constructor's initializer list), so a team never set IS in state ""
#     and TEAM_STATE_IS against any non-empty token is a truthful false.
#   * TEAM_STATE IS SAVE-PERSISTED: Team::xfer writes m_state right after the
#     member-id list (BFME decomp Team.cpp:2677, identical in ZH). Mutable by
#     script action plus save-file membership puts it inside the
#     snapshot/hash boundary, the script_object_type_lists rule.
#   * CUSTOM STATES: both engine source trees carry only the metadata (action
#     id 490, parameter types [TEAM, TEAM_STATE, BOOLEAN]; condition id 143);
#     the BFME implementation is not decompiled. The SET reading below -
#     enabled inserts the token, disabled removes it, HAS is membership, a
#     never-set token is false - is the inference the signature forces
#     (retail authors the same token with both booleans: AI_ADVANCING is
#     authored 34x enabled AND 34x disabled, independently of AI_ASSAULTING),
#     recorded as an ASSUMPTION. What would falsify it: the custom-state
#     handler in the retail BFME1 binary storing something other than a
#     per-team token set. Custom-state save persistence is likewise
#     unevidenced; outcome-bearing mutability alone puts it inside the
#     boundary here regardless.
#
# The token vocabulary is CONTENT-DEFINED (AI_ATTACKING, AI_DEFENDING,
# READY_TO_AMBUSH, ... and 46 distinct custom tokens in the retail AI
# libraries); nothing here validates tokens against a table, exactly like
# retail. Comparisons are exact and case-sensitive.
#
# CANONICAL FORM: a team's record holds "state" only when non-empty (setting
# "" returns to the default and drops the key - retail's default IS "") and
# "custom" only when tokens are enabled (sorted unique Array; disabling the
# last token drops the key). A team whose record empties loses its team key,
# so state returned to pristine values returns to the pristine hash exactly.
#
# HASH INERTNESS: participates in the authoritative state ONLY when non-empty
# (empty-is-absent, the unpackable_bases discipline), so a match whose
# scripts never touch team state contributes zero bytes to state_hash() and
# the frozen cross-platform pin stands untouched. setup() clears it (match
# state, exactly like the OBJECT_TYPE_LIST stores).

## script team name -> {"state": String (present iff != ""),
## "custom": sorted unique Array[String] (present iff non-empty)}.
## See the block comment above. setup() clears it; hashed only when non-empty.
var team_behavior_states: Dictionary = {}


func set_team_behavior_state(team: String, token: String) -> Dictionary:
	## TEAM_SET_STATE: overwrite the team's single state string. Any token is
	## admitted, including ones no condition ever reads (retail validates
	## nothing). Setting "" IS meaningful - it returns the team to the retail
	## default - and canonically drops the key rather than storing "".
	if not script_teams.has(team):
		return {"ok": false, "reason": "script team '%s' is not registered" % team}
	if token == "":
		_prune_team_behavior_key(team, "state")
		return {"ok": true, "reason": ""}
	var record: Dictionary = team_behavior_states.get(team, {})
	record["state"] = token
	team_behavior_states[team] = record
	return {"ok": true, "reason": ""}


func team_behavior_state(team: String) -> Dictionary:
	## The team's current state string. {"ok": true, "state": String} - "" for
	## a rostered team never set, which is retail's default, not a dodge.
	if not script_teams.has(team):
		return {"ok": false, "reason": "script team '%s' is not registered" % team}
	return {
		"ok": true,
		"state": String((team_behavior_states.get(team, {}) as Dictionary).get("state", "")),
	}


func set_team_custom_state(team: String, token: String, enabled: bool) -> Dictionary:
	## TEAM_SET_CUSTOM_STATE: enable inserts `token` into the team's set,
	## disable removes it. Duplicate enables and absent disables are
	## successful no-ops (set semantics - the assumption block above). An
	## empty token refuses: "" names nothing in the retail vocabulary and
	## would mint an unreachable membership entry.
	if not script_teams.has(team):
		return {"ok": false, "reason": "script team '%s' is not registered" % team}
	if token == "":
		return {"ok": false, "reason": "empty custom-state token names nothing"}
	if enabled:
		var record: Dictionary = team_behavior_states.get(team, {})
		var tokens: Array = record.get("custom", [])
		if not tokens.has(token):
			tokens.append(token)
			tokens.sort()
		record["custom"] = tokens
		team_behavior_states[team] = record
		return {"ok": true, "reason": ""}
	if team_behavior_states.has(team):
		var record: Dictionary = team_behavior_states[team]
		var tokens: Array = record.get("custom", [])
		tokens.erase(token)
		if tokens.is_empty():
			_prune_team_behavior_key(team, "custom")
		else:
			record["custom"] = tokens
	return {"ok": true, "reason": ""}


func team_custom_states(team: String) -> Dictionary:
	## The team's enabled custom-state tokens, sorted, as a defensive copy.
	## {"ok": true, "tokens": Array} - empty for a team never toggled.
	if not script_teams.has(team):
		return {"ok": false, "reason": "script team '%s' is not registered" % team}
	return {
		"ok": true,
		"tokens": ((team_behavior_states.get(team, {}) as Dictionary).get("custom", []) as Array).duplicate(),
	}


func _prune_team_behavior_key(team: String, key: String) -> void:
	## Drop `key` from the team's record, and the record itself when it
	## empties - the canonical form the hash discipline requires (a lingering
	## empty record would be a hash-visible phantom, the e56a0d4 class).
	if not team_behavior_states.has(team):
		return
	var record: Dictionary = team_behavior_states[team]
	record.erase(key)
	if record.is_empty():
		team_behavior_states.erase(team)


# --- Sequential scripts (SIM-owned, ScriptEngine m_sequentialScripts) ------
#
# Retail ScriptEngine holds a global vector of SequentialScript heads, one
# active head per team/object, with further scripts chained via
# m_nextScriptInSequence. TEAM_EXECUTE_SEQUENTIAL_SCRIPT appends;
# evaluateAndProgressAllSequentialScripts steps idle heads one action at a
# time with m_conditionTeam latched for <This Team>.
#
# Mapping:
#   * times_to_loop: -1 forever, 0 run once (no re-append), matching the
#     boolean facet (looping true/false). Finite counts >1 stay refused at
#     the handler (retail AI sites pass 0).
#   * current_instruction starts at -1; progress increments before execute.
#   * frames_to_wait: -1 none, 0 force progress, >0 countdown each tick.
#   * idle: set true at queue (doTeamStartSequentialScript groupIdle); order
#     verbs that take control of members call mark_team_sequential_busy.
#   * empty-is-absent for hash inertness on scriptless matches.
#
# Unit sequential scripts are not stored here (separate object-id key space).

## script team name -> Array of sequential entries (head + chained nexts).
var sequential_script_queues: Dictionary = {}

const _SEQUENTIAL_SPIN_LIMIT := 32


func queue_team_sequential_script(
	script_team: String, script_name: String, times_to_loop: int
) -> Dictionary:
	## ScriptActions::doTeamStartSequentialScript. Requires the named script
	## to be loaded on at least one registered executor (loud refuse rather
	## than retail's silent no-op when findScriptByName fails).
	if not script_teams.has(script_team):
		return {"ok": false, "reason": "script team '%s' is not registered" % script_team}
	if winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if script_name.strip_edges() == "":
		return {"ok": false, "reason": "sequential script name is empty"}
	if _executor_for_script(script_name) == null:
		return {
			"ok": false,
			"reason": "script '%s' is not loaded on any registered executor" % script_name,
		}
	var owner := int((script_teams[script_team] as Dictionary).get("owner", -1))
	if _script_owner_exists(owner):
		# Retail groupIdle so the sequential head can start immediately.
		issue_stop(living_ids(owner), owner)
	var entry := {
		"script_name": script_name,
		"times_to_loop": times_to_loop,
		"current_instruction": -1,
		"frames_to_wait": -1,
		"dont_advance": false,
		"idle": true,
	}
	var chain: Array = sequential_script_queues.get(script_team, []) as Array
	chain.append(entry)
	sequential_script_queues[script_team] = chain
	return {"ok": true, "reason": ""}


func clear_team_sequential_scripts(script_team: String) -> Dictionary:
	if not script_teams.has(script_team):
		return {"ok": false, "reason": "script team '%s' is not registered" % script_team}
	sequential_script_queues.erase(script_team)
	return {"ok": true, "reason": ""}


func mark_team_sequential_busy(script_team: String) -> void:
	## Order verbs that assign AI work clear sequential idle so further
	## instructions wait (retail ai/aigroup isIdle gate).
	if not sequential_script_queues.has(script_team):
		return
	var chain: Array = sequential_script_queues[script_team]
	if chain.is_empty():
		return
	var head := chain[0] as Dictionary
	head["idle"] = false
	chain[0] = head
	sequential_script_queues[script_team] = chain


func mark_team_sequential_idle(script_team: String) -> void:
	if not sequential_script_queues.has(script_team):
		return
	var chain: Array = sequential_script_queues[script_team]
	if chain.is_empty():
		return
	var head := chain[0] as Dictionary
	head["idle"] = true
	chain[0] = head
	sequential_script_queues[script_team] = chain


# --- Object status bits (script Object_STATUS vocabulary, SIM-owned) --------
#
# Retail ObjectStatus bits ride the object; scripts set/clear named bits via
# UNIT/TEAM_CHANGE_OBJECT_STATUS and read them via UNIT_HAS_OBJECT_STATUS /
# TEAM_ALL/SOME_HAVE_OBJECT_STATUS. Storage is a per-entity map of exact
# authored status names -> true. Absent name is false. Empty map is erased
# (empty-is-absent on the entity row) so scriptless matches keep frozen
# hashes. The bit vocabulary is content-defined; unknown names are still
# stored (retail admits authoring any token).


func set_entity_object_status(
	entity_id: int, status: String, enabled: bool
) -> Dictionary:
	if winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if status.strip_edges() == "":
		return {"ok": false, "reason": "empty object status names nothing"}
	if not entities.has(entity_id):
		return {"ok": false, "reason": "entity %d is not in the simulation" % entity_id}
	var row := entities[entity_id] as Dictionary
	if int(row.get("health", 0)) <= 0:
		return {"ok": false, "reason": "entity %d is not living" % entity_id}
	var flags: Dictionary = row.get("object_status", {}) as Dictionary
	if enabled:
		flags[status] = true
		row["object_status"] = flags
	else:
		flags.erase(status)
		if flags.is_empty():
			row.erase("object_status")
		else:
			row["object_status"] = flags
	entities[entity_id] = row
	return {"ok": true, "reason": ""}


func entity_has_object_status(entity_id: int, status: String) -> bool:
	if not entities.has(entity_id) or status.strip_edges() == "":
		return false
	var row := entities[entity_id] as Dictionary
	if int(row.get("health", 0)) <= 0:
		return false
	var flags: Dictionary = row.get("object_status", {}) as Dictionary
	return bool(flags.get(status, false))


func set_entities_object_status(
	entity_ids: Array, status: String, enabled: bool
) -> Dictionary:
	if winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if status.strip_edges() == "":
		return {"ok": false, "reason": "empty object status names nothing"}
	for id_value in entity_ids:
		var result := set_entity_object_status(int(id_value), status, enabled)
		if not bool(result.get("ok", false)):
			return result
	return {"ok": true, "reason": ""}


# --- Production / AI build-loop control flags (script surface) -------------
#
# Retail AI toggles base construction, factories, auto-build, and per-type
# unit construction. Absence of an override means retail's default (enabled /
# speed 1.0). Non-default values are match state and hash-visible.

var production_controls_by_team: Dictionary = {}
## object_type -> buildability enum int (content-defined). Empty = untouched.
var tech_buildability: Dictionary = {}
## owner team id -> reference name -> script team name (TEAM_REF store).
var script_team_references: Dictionary = {}


func _production_controls_for(team: int) -> Dictionary:
	return production_controls_by_team.get(team, {}) as Dictionary


func _set_production_control_flag(team: int, key: String, enabled: bool) -> Dictionary:
	if winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if not _script_owner_exists(team):
		return {"ok": false, "reason": "team %d is unavailable" % team}
	var row: Dictionary = _production_controls_for(team).duplicate(true)
	# Default is enabled; store only explicit disables, erase on re-enable.
	if enabled:
		row.erase(key)
	else:
		row[key] = false
	if row.is_empty():
		production_controls_by_team.erase(team)
	else:
		production_controls_by_team[team] = row
	return {"ok": true, "reason": ""}


func set_auto_build_enabled(team: int, enabled: bool) -> Dictionary:
	return _set_production_control_flag(team, "auto_build", enabled)


func set_base_construction_enabled(team: int, enabled: bool) -> Dictionary:
	return _set_production_control_flag(team, "base_construction", enabled)


func set_factories_enabled(team: int, enabled: bool) -> Dictionary:
	return _set_production_control_flag(team, "factories", enabled)


func set_base_construction_speed(team: int, factor: float) -> Dictionary:
	if winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if not _script_owner_exists(team):
		return {"ok": false, "reason": "team %d is unavailable" % team}
	if factor < 0.0:
		return {"ok": false, "reason": "construction speed factor cannot be negative"}
	var row: Dictionary = _production_controls_for(team).duplicate(true)
	if is_equal_approx(factor, 1.0):
		row.erase("base_construction_speed")
	else:
		row["base_construction_speed"] = factor
	if row.is_empty():
		production_controls_by_team.erase(team)
	else:
		production_controls_by_team[team] = row
	return {"ok": true, "reason": ""}


func set_unit_construction_enabled(
	team: int, object_type: String, enabled: bool
) -> Dictionary:
	if winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if not _script_owner_exists(team):
		return {"ok": false, "reason": "team %d is unavailable" % team}
	if object_type.strip_edges() == "":
		return {"ok": false, "reason": "empty object type names nothing"}
	var row: Dictionary = _production_controls_for(team).duplicate(true)
	var unit_map: Dictionary = row.get("unit_construction", {}) as Dictionary
	var match_key := ""
	for key_value in unit_map.keys():
		if String(key_value).to_lower() == object_type.to_lower():
			match_key = String(key_value)
			break
	if match_key != "":
		unit_map.erase(match_key)
	if not enabled:
		unit_map[object_type] = false
	if unit_map.is_empty():
		row.erase("unit_construction")
	else:
		row["unit_construction"] = unit_map
	if row.is_empty():
		production_controls_by_team.erase(team)
	else:
		production_controls_by_team[team] = row
	return {"ok": true, "reason": ""}


func production_control_enabled(team: int, key: String) -> bool:
	## Default true when no override is stored.
	var row := _production_controls_for(team)
	if not row.has(key):
		return true
	return bool(row.get(key, true))


func unit_construction_enabled(team: int, object_type: String) -> bool:
	var row := _production_controls_for(team)
	var unit_map: Dictionary = row.get("unit_construction", {}) as Dictionary
	for key_value in unit_map.keys():
		if String(key_value).to_lower() == object_type.to_lower():
			return bool(unit_map[key_value])
	return true


func set_tech_buildability(object_type: String, buildability: int) -> Dictionary:
	if winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if object_type.strip_edges() == "":
		return {"ok": false, "reason": "empty object type names nothing"}
	tech_buildability[object_type] = buildability
	return {"ok": true, "reason": ""}


func has_prerequisite_to_build(team: int, object_type: String) -> Dictionary:
	## True when every authored prerequisite upgrade for the unit type is
	## completed for the team, or the type has no prerequisite map (fieldable
	## without tech). Unknown/unmapped types refuse rather than guessing.
	if not _script_owner_exists(team):
		return {"ok": false, "reason": "team %d is unavailable" % team}
	if object_type.strip_edges() == "":
		return {"ok": false, "reason": "empty object type names nothing"}
	var unit_type := trainable_unit_type_for(team, object_type)
	if unit_type == "":
		# Fall back to casefold match against production-rule keys.
		for key_value in _unit_production_rules.keys():
			if String(key_value).to_lower() == object_type.to_lower():
				unit_type = String(key_value)
				break
	if unit_type == "" and not _unit_production_rules.has(object_type):
		return {
			"ok": false,
			"reason": (
				"object type '%s' is not a production rule this simulation models"
				% object_type
			),
		}
	if unit_type == "":
		unit_type = object_type
	var prereq_value: Variant = _unit_prerequisites.get(unit_type, {})
	var completed: Dictionary = team_upgrades.get(team, {}) as Dictionary
	if typeof(prereq_value) == TYPE_STRING:
		var upgrade_id := String(prereq_value)
		if upgrade_id == "":
			return {"ok": true, "value": true}
		return {"ok": true, "value": completed.has(upgrade_id)}
	if typeof(prereq_value) != TYPE_DICTIONARY:
		return {"ok": true, "value": true}
	var by_producer: Dictionary = prereq_value
	if by_producer.is_empty():
		return {"ok": true, "value": true}
	# Trainable if ANY producer path has all its prerequisites completed.
	for producer_key in by_producer.keys():
		var reqs: Array = by_producer[producer_key] as Array
		var ok_path := true
		for req_value in reqs:
			if not completed.has(String(req_value)):
				ok_path = false
				break
		if ok_path:
			return {"ok": true, "value": true}
	return {"ok": true, "value": false}


func bind_script_team_reference(
	owner_team: int, reference: String, script_team: String
) -> Dictionary:
	if winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if reference.strip_edges() == "":
		return {"ok": false, "reason": "empty team reference names nothing"}
	if not script_teams.has(script_team):
		return {
			"ok": false,
			"reason": "script team '%s' is not registered" % script_team,
		}
	var table: Dictionary = script_team_references.get(owner_team, {}) as Dictionary
	table[reference] = script_team
	script_team_references[owner_team] = table
	return {"ok": true, "reason": ""}


func script_team_reference(owner_team: int, reference: String) -> String:
	var table: Dictionary = script_team_references.get(owner_team, {}) as Dictionary
	return String(table.get(reference, ""))


func set_entity_stopping_distance(entity_id: int, distance: float) -> Dictionary:
	if winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if not entities.has(entity_id):
		return {"ok": false, "reason": "entity %d is not in the simulation" % entity_id}
	if distance < 0.0:
		return {"ok": false, "reason": "stopping distance cannot be negative"}
	var row := entities[entity_id] as Dictionary
	if is_equal_approx(distance, 0.0):
		row.erase("stopping_distance")
	else:
		row["stopping_distance"] = distance
	entities[entity_id] = row
	return {"ok": true, "reason": ""}


func set_entities_idle_until(entity_ids: Array, until_tick: int) -> Dictionary:
	if winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	for id_value in entity_ids:
		var entity_id := int(id_value)
		if not entities.has(entity_id):
			continue
		var row := entities[entity_id] as Dictionary
		if int(row.get("health", 0)) <= 0:
			continue
		row["script_idle_until"] = until_tick
		row["state"] = "idle"
		row.erase("target_id")
		_clear_pending_route(row, true)
		entities[entity_id] = row
	return {"ok": true, "reason": ""}


func set_entities_spin_until(entity_ids: Array, until_tick: int) -> Dictionary:
	if winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	for id_value in entity_ids:
		var entity_id := int(id_value)
		if not entities.has(entity_id):
			continue
		var row := entities[entity_id] as Dictionary
		if int(row.get("health", 0)) <= 0:
			continue
		row["script_spin_until"] = until_tick
		row["state"] = "idle"
		entities[entity_id] = row
	return {"ok": true, "reason": ""}


func issue_hunt(ids: Array[int], team: int = PLAYER_TEAM) -> int:
	## TEAM/NAMED/PLAYER_HUNT with empty command button: aggressive stance and
	## clear hold so the existing acquire loop hunts hostiles.
	var count := 0
	for id_value in ids:
		var entity_id := int(id_value)
		if not entities.has(entity_id):
			continue
		var row := entities[entity_id] as Dictionary
		if int(row.get("team", -1)) != team or int(row.get("health", 0)) <= 0:
			continue
		row["stance"] = "Aggressive"
		row.erase("script_idle_until")
		entities[entity_id] = row
		count += 1
	issue_set_stance(ids, "Aggressive", team)
	return count



# --- Bulk script surface stores (flags, progression, containment, events) ---
#
# These back high-volume script facet methods with exact authored keys and
# empty-is-absent defaults. They do not invent pathfinding, area geometry, or
# presentation. Combat effects of flags (stealth vision, etc.) remain for
# later subsystems; storage and script readback are truthful.

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


func _player_prog(team: int) -> Dictionary:
	return player_progression.get(team, {}) as Dictionary


func _set_player_prog_value(team: int, key: String, value: Variant) -> Dictionary:
	if winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if not _script_owner_exists(team):
		return {"ok": false, "reason": "team %d is unavailable" % team}
	var row: Dictionary = _player_prog(team).duplicate(true)
	row[key] = value
	player_progression[team] = row
	return {"ok": true, "reason": ""}


func set_diplomacy_override(from_team: int, to_team: int, relation: int) -> Dictionary:
	## Hash-backed relation override (player/team script diplomacy).
	if winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if not _script_owner_exists(from_team):
		return {"ok": false, "reason": "from team unavailable"}
	var table: Dictionary = player_diplomacy_overrides.get(from_team, {}) as Dictionary
	table[to_team] = relation
	player_diplomacy_overrides[from_team] = table
	return {"ok": true, "reason": ""}


func clear_diplomacy_override(from_team: int, to_team: int) -> Dictionary:
	if not player_diplomacy_overrides.has(from_team):
		return {"ok": true, "reason": ""}
	var table: Dictionary = player_diplomacy_overrides[from_team]
	table.erase(to_team)
	if table.is_empty():
		player_diplomacy_overrides.erase(from_team)
	else:
		player_diplomacy_overrides[from_team] = table
	return {"ok": true, "reason": ""}


func clear_all_diplomacy_overrides(from_team: int) -> Dictionary:
	player_diplomacy_overrides.erase(from_team)
	return {"ok": true, "reason": ""}


func set_attack_priority_entry(
	set_name: String, target_kind: String, target_name: String, priority: int
) -> void:
	## Hash-backed attack-priority table (script AI vocabulary).
	if not match_script_flags.has("attack_priority_sets"):
		match_script_flags["attack_priority_sets"] = {}
	var sets: Dictionary = match_script_flags["attack_priority_sets"]
	var set_row: Dictionary = sets.get(set_name, {}) as Dictionary
	set_row["%s:%s" % [target_kind, target_name]] = priority
	sets[set_name] = set_row
	match_script_flags["attack_priority_sets"] = sets


func set_default_attack_priority_entry(set_name: String, priority: int) -> void:
	if not match_script_flags.has("attack_priority_defaults"):
		match_script_flags["attack_priority_defaults"] = {}
	var defaults: Dictionary = match_script_flags["attack_priority_defaults"]
	defaults[set_name] = priority
	match_script_flags["attack_priority_defaults"] = defaults


func set_team_ai_priority(team: int, priority: int) -> Dictionary:
	if winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if not _script_owner_exists(team):
		return {"ok": false, "reason": "team unavailable"}
	if not match_script_flags.has("team_ai_priority"):
		match_script_flags["team_ai_priority"] = {}
	var table: Dictionary = match_script_flags["team_ai_priority"]
	table[team] = priority
	match_script_flags["team_ai_priority"] = table
	return {"ok": true, "reason": ""}


func adjust_team_ai_priority(team: int, delta: int) -> Dictionary:
	var table: Dictionary = match_script_flags.get("team_ai_priority", {}) as Dictionary
	var current := int(table.get(team, 0))
	return set_team_ai_priority(team, current + delta)


func _add_player_prog_value(team: int, key: String, delta: int) -> Dictionary:
	var row := _player_prog(team)
	var current := int(row.get(key, 0))
	return _set_player_prog_value(team, key, current + delta)


func set_entity_bool_flag(entity_id: int, flag: String, enabled: bool) -> Dictionary:
	if winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if not entities.has(entity_id):
		return {"ok": false, "reason": "entity %d missing" % entity_id}
	var row := entities[entity_id] as Dictionary
	var flags: Dictionary = row.get("script_bool_flags", {}) as Dictionary
	if enabled:
		flags[flag] = true
		row["script_bool_flags"] = flags
	else:
		flags.erase(flag)
		if flags.is_empty():
			row.erase("script_bool_flags")
		else:
			row["script_bool_flags"] = flags
	entities[entity_id] = row
	return {"ok": true, "reason": ""}


func entity_bool_flag(entity_id: int, flag: String) -> bool:
	if not entities.has(entity_id):
		return false
	var flags: Dictionary = (entities[entity_id] as Dictionary).get("script_bool_flags", {})
	return bool(flags.get(flag, false))


func set_entities_bool_flag(entity_ids: Array, flag: String, enabled: bool) -> Dictionary:
	for id_value in entity_ids:
		var result := set_entity_bool_flag(int(id_value), flag, enabled)
		if not bool(result.get("ok", false)):
			return result
	return {"ok": true, "reason": ""}


func set_entity_string_state(entity_id: int, key: String, value: String) -> Dictionary:
	if winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if not entities.has(entity_id):
		return {"ok": false, "reason": "entity %d missing" % entity_id}
	var row := entities[entity_id] as Dictionary
	var store: Dictionary = row.get("script_string_state", {}) as Dictionary
	if value == "":
		store.erase(key)
	else:
		store[key] = value
	if store.is_empty():
		row.erase("script_string_state")
	else:
		row["script_string_state"] = store
	entities[entity_id] = row
	return {"ok": true, "reason": ""}


func entity_string_state(entity_id: int, key: String) -> String:
	if not entities.has(entity_id):
		return ""
	var store: Dictionary = (entities[entity_id] as Dictionary).get("script_string_state", {})
	return String(store.get(key, ""))


func set_entity_timed_flag(entity_id: int, flag: String, until_tick: int) -> Dictionary:
	if winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if not entities.has(entity_id):
		return {"ok": false, "reason": "entity %d missing" % entity_id}
	var row := entities[entity_id] as Dictionary
	var store: Dictionary = row.get("script_timed_flags", {}) as Dictionary
	if until_tick < 0:
		store.erase(flag)
	else:
		store[flag] = until_tick
	if store.is_empty():
		row.erase("script_timed_flags")
	else:
		row["script_timed_flags"] = store
	entities[entity_id] = row
	return {"ok": true, "reason": ""}


func entity_timed_flag_active(entity_id: int, flag: String) -> bool:
	if not entities.has(entity_id):
		return false
	var store: Dictionary = (entities[entity_id] as Dictionary).get("script_timed_flags", {})
	if not store.has(flag):
		return false
	return tick_index <= int(store[flag])


func script_set_health_percent(entity_id: int, percent: float) -> Dictionary:
	if winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if not entities.has(entity_id):
		return {"ok": false, "reason": "entity %d missing" % entity_id}
	var row := entities[entity_id] as Dictionary
	var maximum := maxi(1, int(row.get("maximum_health", row.get("health", 1))))
	var clamped := clampf(percent, 0.0, 100.0)
	var new_health := int(round(maximum * clamped / 100.0))
	row["health"] = new_health
	var defeated_members: Array[int] = []
	if row.has("member_health") and row["member_health"] is Array:
		var members: Array = row["member_health"]
		if not members.is_empty():
			var each := int(new_health / members.size())
			var rebuilt: Array = []
			for member_index in members.size():
				if int(members[member_index]) > 0 and each <= 0:
					defeated_members.append(member_index)
				rebuilt.append(each)
			row["member_health"] = rebuilt
	entities[entity_id] = row
	if new_health <= 0:
		var death_policy := _bookkeep_battalion_death(
			entity_id, row, "NORMAL", defeated_members
		)
		if bool(row.get("is_banner_carrier", false)):
			_on_banner_carrier_defeated(row)
		_emit_event("battalion.defeated", 0, entity_id, {"reason": "script-kill"})
		if bool(death_policy.get("destroy_object", false)) or bool(row.get("is_banner_carrier", false)):
			entities.erase(entity_id)
	return {"ok": true, "reason": ""}


func script_kill_entity(entity_id: int) -> Dictionary:
	return script_set_health_percent(entity_id, 0.0)


func script_damage_entity(entity_id: int, amount: float) -> Dictionary:
	if not entities.has(entity_id):
		return {"ok": false, "reason": "entity %d missing" % entity_id}
	var row := entities[entity_id] as Dictionary
	var maximum := maxi(1, int(row.get("maximum_health", 1)))
	var health := int(row.get("health", 0))
	var pct := 100.0 * float(health) / float(maximum)
	var damage_pct := 100.0 * amount / float(maximum)
	return script_set_health_percent(entity_id, pct - damage_pct)


func contain_entity(structure_id: int, entity_id: int) -> Dictionary:
	if winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if not structures.has(structure_id) and not entities.has(structure_id):
		return {"ok": false, "reason": "container %d missing" % structure_id}
	if not entities.has(entity_id):
		return {"ok": false, "reason": "entity %d missing" % entity_id}
	if entity_container.has(entity_id):
		return {"ok": false, "reason": "entity already contained"}
	var passengers: Array = containment.get(structure_id, []) as Array
	passengers.append(entity_id)
	containment[structure_id] = passengers
	entity_container[entity_id] = structure_id
	return {"ok": true, "reason": ""}


func exit_entity_container(entity_id: int) -> Dictionary:
	if not entity_container.has(entity_id):
		return {"ok": true, "reason": ""}  # vacuous
	var structure_id := int(entity_container[entity_id])
	var passengers: Array = containment.get(structure_id, []) as Array
	passengers.erase(entity_id)
	if passengers.is_empty():
		containment.erase(structure_id)
	else:
		containment[structure_id] = passengers
	entity_container.erase(entity_id)
	return {"ok": true, "reason": ""}


func passenger_count(structure_id: int) -> int:
	return (containment.get(structure_id, []) as Array).size()


func register_script_area(name: String, center: Vector2, radius: float) -> void:
	script_areas[name] = {"center": center, "radius": radius, "impassable": false}


func register_script_waypoint(name: String, position: Vector2) -> void:
	script_waypoints[name] = position


func register_script_waypoint_path(name: String, points: Array) -> void:
	script_waypoint_paths[name] = points.duplicate()


func area_contains(name: String, position: Vector2) -> Dictionary:
	if not script_areas.has(name):
		return {"ok": false, "reason": "area '%s' is not registered" % name}
	var area: Dictionary = script_areas[name]
	var center: Vector2 = area.get("center", Vector2.ZERO)
	var radius := float(area.get("radius", 0.0))
	return {"ok": true, "value": center.distance_to(position) <= radius}


func bump_script_event(key: String, amount: int = 1) -> void:
	script_event_counts[key] = int(script_event_counts.get(key, 0)) + amount


func script_event_count(key: String) -> int:
	return int(script_event_counts.get(key, 0))


func mark_team_created(script_team: String) -> void:
	team_created_edge[script_team] = true


func team_created_is_set(script_team: String) -> bool:
	## Retail Team::isCreated is cleared by Team::updateState once per frame,
	## not by the condition read itself. Probe/hash-safe: reading does not
	## mutate. Clear edges in _step_script_executors after scripts run.
	return bool(team_created_edge.get(script_team, false))


func clear_team_created_edges() -> void:
	team_created_edge.clear()


func set_entity_team(entity_id: int, new_team: int) -> Dictionary:
	if winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if not entities.has(entity_id):
		return {"ok": false, "reason": "entity %d missing" % entity_id}
	if not _script_owner_exists(new_team):
		return {"ok": false, "reason": "team %d unavailable" % new_team}
	var row := entities[entity_id] as Dictionary
	var old_team := int(row.get("team", -1))
	if old_team == new_team:
		return {"ok": true, "reason": ""}
	if base_loop_enabled and not bool(row.get("command_points_released", false)):
		var commitment := _entity_command_point_commitment(row)
		team_command_points[old_team] = maxi(0, command_points_for_team(old_team) - commitment)
		team_command_points[new_team] = command_points_for_team(new_team) + commitment
	row["team"] = new_team
	entities[entity_id] = row
	return {"ok": true, "reason": ""}


func delete_entity(entity_id: int) -> Dictionary:
	if winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if not entities.has(entity_id):
		return {"ok": false, "reason": "entity %d missing" % entity_id}
	exit_entity_container(entity_id)
	var row := entities[entity_id] as Dictionary
	_summon_despawn_ticks.erase(entity_id)
	_summon_aura_source_ids.erase(entity_id)
	selected_ids.erase(entity_id)
	_release_command_points_once(row)
	entities.erase(entity_id)
	prune_control_groups()
	return {"ok": true, "reason": ""}



func _executor_for_script(script_name: String) -> SageScriptExecutor:
	for team_key in _sorted_dictionary_keys(_script_executors):
		var executor: SageScriptExecutor = (
			_script_executors[team_key] as WeakRef
		).get_ref()
		if executor != null and executor.has_script(script_name):
			return executor
	return null


func _step_sequential_scripts() -> void:
	## ScriptEngine::evaluateAndProgressAllSequentialScripts, bounded to team
	## heads. Runs after ordinary executor.tick() so newly queued scripts from
	## this frame's AI scripts can start the same logic frame (retail steps
	## sequential scripts in the same ScriptEngine::update).
	if sequential_script_queues.is_empty() or winner != -1:
		return
	var team_names := sequential_script_queues.keys()
	team_names.sort()
	for team_name_value in team_names:
		var script_team := String(team_name_value)
		var spin := 0
		while spin < _SEQUENTIAL_SPIN_LIMIT:
			spin += 1
			if not sequential_script_queues.has(script_team):
				break
			var chain: Array = sequential_script_queues[script_team]
			if chain.is_empty():
				sequential_script_queues.erase(script_team)
				break
			var head := (chain[0] as Dictionary).duplicate(true)
			var frames := int(head.get("frames_to_wait", -1))
			if frames > 0:
				head["frames_to_wait"] = frames - 1
				chain[0] = head
				sequential_script_queues[script_team] = chain
				break
			var can_progress := bool(head.get("idle", false)) or frames == 0
			if not can_progress:
				break
			if bool(head.get("dont_advance", false)):
				head["dont_advance"] = false
			else:
				head["current_instruction"] = int(head.get("current_instruction", -1)) + 1
			var script_name := String(head.get("script_name", ""))
			var executor := _executor_for_script(script_name)
			if executor == null:
				# Script unloaded or executors dropped; fail closed by clearing.
				sequential_script_queues.erase(script_team)
				break
			var actions_result: Dictionary = executor.true_actions_for_script(script_name)
			if not bool(actions_result.get("ok", false)):
				sequential_script_queues.erase(script_team)
				break
			var actions: Array = actions_result.get("actions", []) as Array
			var instruction := int(head.get("current_instruction", 0))
			if instruction < 0 or instruction >= actions.size():
				# Finished the action list. Re-append when looping.
				var times := int(head.get("times_to_loop", 0))
				chain.remove_at(0)
				if times != 0:
					var requeue := {
						"script_name": script_name,
						"times_to_loop": times if times < 0 else times - 1,
						"current_instruction": -1,
						"frames_to_wait": -1,
						"dont_advance": false,
						"idle": true,
					}
					chain.append(requeue)
				if chain.is_empty():
					sequential_script_queues.erase(script_team)
				else:
					sequential_script_queues[script_team] = chain
				# Allow the next chained script to start this frame.
				continue
			var action: Dictionary = actions[instruction]
			head["frames_to_wait"] = -1
			chain[0] = head
			sequential_script_queues[script_team] = chain
			var world: RetailSliceScriptWorld = executor.world as RetailSliceScriptWorld
			if world != null:
				world.latch_script_team_context(script_team, true)
			executor.execute_action_record(action, script_name)
			if world != null:
				world.clear_script_team_context()
			# Re-read head: the action may have stopped/queued/busy-marked.
			if not sequential_script_queues.has(script_team):
				break
			chain = sequential_script_queues[script_team]
			if chain.is_empty():
				sequential_script_queues.erase(script_team)
				break
			head = chain[0] as Dictionary
			if bool(head.get("dont_advance", false)):
				break
			if not bool(head.get("idle", false)):
				break
			# Still idle: retail allows another instruction this frame.


# --- Logic random stream (SIM-owned, retail GameLogic generator) ------------
#
# The deterministic random stream retail script actions draw from
# (SET_RANDOM_COUNTER / SET_RANDOM_TIMER / SET_RANDOM_MSEC_TIMER /
# SET_RANDOM_COUNTER_IN_SECONDS). This is retail's LOGIC stream, not its
# client stream: RandomValue.cpp keeps three independent generators
# (theGameLogicSeed / theGameClientSeed / theGameAudioSeed) precisely so the
# GameLogic "remains deterministic, regardless of the effects displayed on
# the GameClient" (RandomValue.cpp:138-142), and the script engine's random
# actions call GameLogicRandomValue (ScriptEngine.cpp setTimer, lines
# 6746-6760 in the GPL Zero Hour source) - the logic stream. The client
# stream stays refused here (SET_COUNTER_TO_CLIENT_RANDOM_VALUE is a
# DELIBERATE gap): it is desync-prone by design.
#
# ONE GLOBAL STREAM, NOT PER-PLAYER - retail's shape. theGameLogicSeed is a
# single static array; every logic draw by every subsystem and every script
# player advances the same sequence, and the script engine passes no player
# context into GameLogicRandomValue. Draw ORDER is therefore part of the
# contract: draws happen only inside script/handler execution, which the sim
# steps in ascending team order (register_script_executor's guarantee), so
# every peer interleaves draws identically.
#
# THE GENERATOR IS RETAIL'S, TRANSCRIBED, NOT APPROXIMATED: the Michael
# Booth (Jan 1998) lagged add-with-carry over six 32-bit words from the GPL
# Generals/Zero Hour RandomValue.cpp (randomValue/seedRandom), which the
# BFME1 binary still exports (GetGameLogicRandomValueReal thunk in the
# decompilation's masm dumps, same signature). Two deliberate fidelity
# points, verbatim from retail even where a clean-room design would differ:
#   * the ADC carry is retail's `C = (SUM < A) || (SUM < B)` on the WRAPPED
#     sum - which misses a true carry in the a=b=0xFFFFFFFF,c=1 edge. Bit
#     identity with retail beats mathematical tidiness.
#   * the range map is retail's biased modulo (delta = hi-lo+1 as uint32;
#     delta==0 answers hi WITHOUT consuming a draw; otherwise one draw,
#     `draw % delta + lo`, inclusive of BOTH bounds). The modulo bias is
#     <= delta/2^32 - immaterial for script ranges like [1..3], and matching
#     retail's mapping exactly matters more than uniformity.
#
# CROSS-PLATFORM BIT-IDENTITY: integer arithmetic only, every value masked
# to 32 bits, all intermediates far below 2^63 - GDScript's int is 64-bit
# signed on every platform, so no operation here can overflow or vary.
# Deliberately NOT Godot's RandomNumberGenerator/randi(): their algorithm is
# an engine implementation detail with no cross-version output guarantee.
# This section IS the specification - it can be re-implemented identically
# from this file alone (and was, in Python, to mint the pinned vectors).
#
# SEEDING is match configuration: _rules["logic_random_seed"] (absent means
# 0), read at first draw. Rules are agreed match configuration on every peer
# and a hashed static key, so a disagreeing seed diverges the state hash
# immediately. Retail seeds the same way - InitGameLogicRandom(getSeed())
# from the lobby-shared game seed (LANAPICallbacks.cpp:267,
# SkirmishGameOptionsMenu.cpp:434); a lobby-varied seed is a follow-up that
# only needs to set this rules key.
#
# STATE AND HASH INERTNESS: the six words ARE the entire stream state (the
# draw count is not needed to continue the sequence). They live in
# _logic_random_state, empty until the first draw (lazy seeding), hashed and
# snapshotted empty-is-absent - a match that never draws contributes ZERO
# bytes, so the frozen cross-platform pin stands untouched. setup() clears
# the stream (match state); a peer adopting a mid-match snapshot receives
# the words and continues the identical sequence.

const _U32 := 0xFFFFFFFF

## The six 32-bit words of the logic stream; [] until the first draw (the
## empty-is-absent form). See the block comment above. setup() clears it.
var _logic_random_state: Array = []

## Process-local draw tally, DIAGNOSTIC only (like frame_conversions): an
## adopting peer reports its own draws, not the minter's. Never hashed.
var logic_random_draws: int = 0


static func _logic_random_seed_words(seed_value: int) -> Array:
	## Retail seedRandom() with the incremental constant additions telescoped:
	## after step k the accumulator is exactly SEED + constant_k (mod 2^32).
	var seed32 := seed_value & _U32
	return [
		(seed32 + 0xF22D0E56) & _U32,
		(seed32 + 0x883126E9) & _U32,
		(seed32 + 0xC624DD2F) & _U32,
		(seed32 + 0x0702C49C) & _U32,
		(seed32 + 0x9E353F7D) & _U32,
		(seed32 + 0x6FDF3B64) & _U32,
	]


static func _logic_random_draw32(words: Array) -> int:
	## One raw 32-bit draw, mutating `words` in place: retail randomValue() -
	## five chained ADCs from words[5] down to words[0] (each ADC uses
	## retail's carry rule on the wrapped sum), then the increment cascade
	## that bubbles a +1 up from words[5], bumping the RETURN VALUE too when
	## it reaches words[0].
	var w0 := int(words[0])
	var w1 := int(words[1])
	var w2 := int(words[2])
	var w3 := int(words[3])
	var w4 := int(words[4])
	var w5 := int(words[5])
	var carry := 0
	var ax := (w5 + w4 + carry) & _U32
	carry = 1 if (ax < w5 or ax < w4) else 0
	w4 = ax
	var prev := ax
	ax = (prev + w3 + carry) & _U32
	carry = 1 if (ax < prev or ax < w3) else 0
	w3 = ax
	prev = ax
	ax = (prev + w2 + carry) & _U32
	carry = 1 if (ax < prev or ax < w2) else 0
	w2 = ax
	prev = ax
	ax = (prev + w1 + carry) & _U32
	carry = 1 if (ax < prev or ax < w1) else 0
	w1 = ax
	prev = ax
	ax = (prev + w0 + carry) & _U32
	w0 = ax
	w5 = (w5 + 1) & _U32
	if w5 == 0:
		w4 = (w4 + 1) & _U32
		if w4 == 0:
			w3 = (w3 + 1) & _U32
			if w3 == 0:
				w2 = (w2 + 1) & _U32
				if w2 == 0:
					w1 = (w1 + 1) & _U32
					if w1 == 0:
						w0 = (w0 + 1) & _U32
						ax = (ax + 1) & _U32
	words[0] = w0
	words[1] = w1
	words[2] = w2
	words[3] = w3
	words[4] = w4
	words[5] = w5
	return ax


func logic_random_int(low: int, high: int) -> int:
	## Retail GetGameLogicRandomValue(lo, hi): inclusive of BOTH bounds.
	## delta = hi - lo + 1 as uint32; delta == 0 (hi == lo - 1 mod 2^32)
	## answers hi without consuming a draw; low == high consumes a draw and
	## answers low (delta 1) - retail does both, and stream POSITION is
	## contract, so neither shortcut may be "optimized". The unsigned draw is
	## reinterpreted as int32 and the final sum wrapped to int32, matching
	## retail's x86 Int arithmetic on the (unreachable-by-authored-scripts)
	## degenerate ranges too.
	if _logic_random_state.is_empty():
		_logic_random_state = _logic_random_seed_words(int(_rules.get("logic_random_seed", 0)))
	var delta := (high - low + 1) & _U32
	if delta == 0:
		return high
	logic_random_draws += 1
	var drawn := _logic_random_draw32(_logic_random_state) % delta
	if drawn >= 0x80000000:
		drawn -= 0x100000000
	return ((drawn + low + 0x80000000) & _U32) - 0x80000000


func logic_random_real(low: float, high: float) -> float:
	## Retail GetGameLogicRandomValueReal: unlike the integer helper this always
	## consumes one logic draw, including a 0..1 roll tested against 100%.
	if _logic_random_state.is_empty():
		_logic_random_state = _logic_random_seed_words(int(_rules.get("logic_random_seed", 0)))
	logic_random_draws += 1
	var unit := float(_logic_random_draw32(_logic_random_state)) / 4294967295.0
	return low + (high - low) * unit


# --- Script-engine environment state (SageScriptEnv, SIM-owned) -------------
#
# The script interpreter's own mutable memory - counters, flags, timers,
# per-script enable bits and its tick counter. This is the LARGEST instance of
# the e56a0d4 defect class: FLAG/SET_FLAG/COUNTER/ENABLE_SCRIPT account for
# 64.7% of all retail-AI call sites, every one a read or write of exactly this
# state, mutated mid-match by script actions. Two peers whose counters diverge
# run completely different AI while their sim hashes agree - unless the state
# lives HERE, inside the snapshot/hash boundary.
#
# Keyed by TEAM (the script player's), like script_unit_references: retail
# runs each AI player's script libraries in that player's own environment
# (SageScriptEnv's own doc: "Counter and flag namespaces are global across all
# loaded scripts, which matches the retail per-player script environment").
#
# HOW THE ENV REACHES IT: attach_script_env(env, team) hands the env this
# Dictionary BY REFERENCE (see SageScriptEnv.attach_state_store); the env then
# reads and writes script_env_state[team] directly. No object reference in
# either direction, so no RefCounted cycle, and the env keeps working with no
# sim at all (its standalone backing) - tests and the bare executor construct
# it that way. BECAUSE the reference is shared, setup() and restore() must
# mutate this dictionary IN PLACE (clear()/merge()), never rebind the
# variable, or every attached env would silently keep writing to an orphan.
#
# WHAT EACH FIELD IS, decided deliberately:
#   * counters/flags/timers/script_enabled - STATE (script actions write them,
#     later conditions read them; the retail save persists the script engine).
#   * "tick" - the interpreter's tick counter, STATE. It anchors every timer
#     (a timer is {"remaining" ticks, decremented once per env.advance()}) and
#     phases interval-gated scripts (tick % interval). It is deliberately NOT
#     aliased to the sim's own tick_index: the executor<->sim tick cadence is
#     not production-wired yet, and aliasing would bake in an unenforced
#     "sim.step() exactly once before executor.tick()" contract whose
#     violation would silently double- or zero-advance timers. Both clocks are
#     authoritative state in the same snapshot, so they cannot drift APART
#     between peers - a mid-match adopter inherits the interpreter tick with
#     the timers anchored to it and expires them on the same absolute tick as
#     the peer that armed them.
#   * env.ticks_per_second / retail_frames_per_second - CONFIGURATION (never
#     mutated mid-match; every tick count they produce lands in hashed timer
#     state, so a misconfigured peer diverges visibly on first use).
#   * env.frame_conversions - DIAGNOSTIC, process-local (nothing in script
#     logic reads it; hashing observability would let a non-outcome counter
#     desync a match).
#
# CANONICAL FORM AND HASH INERTNESS: the raw store is the env's working
# memory; _script_env_state_view() below is what gets hashed and serialized.
# The view prunes zero counters and false flags (indistinguishable from
# absent by every read: counter() defaults 0, flag() defaults false), keeps
# every explicit script_enabled bit (absent means "authored default", so
# false is NOT absent), keeps every timer (an unset timer answers
# timer_expired false; a set one answers from "remaining"), drops tick 0,
# empty collections, and empty team entries, and sorts every level - so an
# untouched match contributes ZERO bytes (the frozen pin stands), and state
# that returns to pristine values returns to the pristine hash EXACTLY.
# Enforcing this at the boundary (one choke point) rather than in every
# mutator also keeps direct dictionary writes - which tests use - canonical.
# Pruning applies ONLY to fields the view understands: an unrecognised field
# is carried verbatim and reported loudly, never silently dropped (see
# _script_env_state_view).

## team id -> {"tick": int, "counters": {}, "flags": {}, "timers": {},
## "script_enabled": {}}. See the block comment above. setup() clears it;
## hashed/serialized through _script_env_state_view (empty-is-absent).
var script_env_state: Dictionary = {}

## The team-entry fields (and timer-row fields) _script_env_state_view()
## understands. Anything else in the store is a boundary violation: it is
## reported loudly (once per field) and carried VERBATIM into the hash and
## snapshot, so state added without teaching the view can never silently
## escape the boundary (0dce37e review: it used to be invisible to the hash
## and dropped on peer adoption).
const SCRIPT_ENV_VIEW_FIELDS: Array[String] = [
	"tick", "counters", "flags", "timers", "script_enabled",
]
const SCRIPT_ENV_TIMER_FIELDS: Array[String] = ["remaining", "running"]

## Diagnostic count of unrecognised script-env fields the view has carried.
## Process-local observability like script_wiring_faults, never hashed.
var script_env_view_faults: int = 0
## "team|path" keys already reported, so a persistent stray field is loud
## once instead of once per hash. Cleared by setup() with the store.
var _script_env_view_reported: Dictionary = {}


func attach_script_env(env: SageScriptEnv, team: int) -> bool:
	## Route `env`'s state through script_env_state[team] (see above). Refuses
	## loudly for a null env or an unrostered team; SageScriptEnv itself
	## refuses an env that is already attached or already holds local state.
	if env == null:
		push_error("attach_script_env refused: null env")
		return false
	if not _script_owner_exists(team):
		push_error("attach_script_env refused: team %d is not a script-capable owner" % team)
		return false
	# This sim is the store's lifetime witness: if it is freed while the env
	# lives on, the env's store becomes an orphan outside every hash and
	# snapshot, and the env must refuse loudly instead of writing into it.
	return env.attach_state_store(script_env_state, team, self)


func _script_env_state_view() -> Dictionary:
	## Canonical, pruned, sorted copy of script_env_state for state_hash() and
	## snapshot() - the boundary choke point described in the block comment.
	##
	## FAIL LOUD, NEVER PRUNE THE UNKNOWN: pruning applies only to the fields
	## this view UNDERSTANDS (SCRIPT_ENV_VIEW_FIELDS / the timer-row pair). A
	## field it does not recognise is carried VERBATIM into the view - so it
	## reaches the hash, the snapshot and every adopting peer - and reported
	## loudly once (_report_script_env_view_fault). The 0dce37e review proved
	## the previous whitelist silently dropped such a field from both the hash
	## and the snapshot: a collection added to the env without updating this
	## view would have been invisible to the desync barrier and lost on peer
	## adoption, the exact silent-fallback class e56a0d4 closed.
	var view := {}
	var team_keys := script_env_state.keys()
	team_keys.sort()
	for team_key in team_keys:
		var entry: Dictionary = script_env_state[team_key]
		var entry_view := {}
		var tick := int(entry.get("tick", 0))
		if tick != 0:
			entry_view["tick"] = tick
		var counters: Dictionary = entry.get("counters", {})
		var counters_view := {}
		for name in _sorted_dictionary_keys(counters):
			var count := int(counters[name])
			if count != 0:
				counters_view[name] = count
		if not counters_view.is_empty():
			entry_view["counters"] = counters_view
		var flags: Dictionary = entry.get("flags", {})
		var flags_view := {}
		for name in _sorted_dictionary_keys(flags):
			if bool(flags[name]):
				flags_view[name] = true
		if not flags_view.is_empty():
			entry_view["flags"] = flags_view
		var timers: Dictionary = entry.get("timers", {})
		var timers_view := {}
		for name in _sorted_dictionary_keys(timers):
			var timer: Dictionary = timers[name]
			var timer_view := {
				"remaining": float(timer.get("remaining", 0.0)),
				"running": bool(timer.get("running", false)),
			}
			for field in _sorted_dictionary_keys(timer):
				if SCRIPT_ENV_TIMER_FIELDS.has(field):
					continue
				_report_script_env_view_fault(
					team_key, "timers/%s/%s" % [str(name), str(field)]
				)
				timer_view[field] = timer[field]
			timers_view[name] = timer_view
		if not timers_view.is_empty():
			entry_view["timers"] = timers_view
		var enabled: Dictionary = entry.get("script_enabled", {})
		var enabled_view := {}
		for name in _sorted_dictionary_keys(enabled):
			enabled_view[name] = bool(enabled[name])
		if not enabled_view.is_empty():
			entry_view["script_enabled"] = enabled_view
		for field in _sorted_dictionary_keys(entry):
			if SCRIPT_ENV_VIEW_FIELDS.has(field):
				continue
			_report_script_env_view_fault(team_key, str(field))
			entry_view[field] = entry[field]
		if not entry_view.is_empty():
			view[team_key] = entry_view
	return view


func _report_script_env_view_fault(team_key: Variant, path: String) -> void:
	## Loud once per (team, field), like SageScriptEnv._report_stale: the first
	## sighting is the defect report; one per hash call would bury the log.
	var report_key := "%s|%s" % [str(team_key), path]
	if _script_env_view_reported.has(report_key):
		return
	_script_env_view_reported[report_key] = true
	script_env_view_faults += 1
	push_error(
		(
			"script env state: team %s carries unrecognised field '%s'; "
			+ "_script_env_state_view does not understand it, so it is carried "
			+ "VERBATIM into the hash and snapshot rather than silently dropped - "
			+ "teach the view (SCRIPT_ENV_VIEW_FIELDS) about it"
		) % [str(team_key), path]
	)


func _sorted_dictionary_keys(source: Dictionary) -> Array:
	var keys := source.keys()
	keys.sort()
	return keys


# --- Script executors wired into the match loop (the production seam) -------
#
# THE SEAM. Registered SageScriptExecutors are stepped by tick() itself -
# _step_script_executors() below - NOT by the vertical slice's frame loop or
# the lockstep session. The sim is the only object every driving path (single
# player _process, lockstep advance_if_ready, control-server save/load, every
# test runner) already funnels through, so putting the step inside tick()
# makes the cadence contract STRUCTURAL: no caller can double-step or skip
# the script engine without also double-stepping or skipping the simulation.
#
# THE TICK-ORDERING CONTRACT, exact and enforced:
#
#   Each registered executor ticks EXACTLY ONCE per gameplay-advancing sim
#   tick, in ascending team order, after that tick's commands are applied
#   and before any gameplay subsystem (economy, production, AI controllers,
#   entity stepping) runs. Ticks in which gameplay is frozen (clock paused,
#   match decided) step NO scripts.
#
# "After commands, before gameplay" means a script evaluating on tick N sees
# the world exactly as tick N-1 left it plus tick N's player commands, and
# every mutation it makes is visible to all of tick N's gameplay - the same
# slot the SAGE script engine occupies at the top of the logic frame.
# 87cf636 deliberately refused to alias the env clock to the sim clock
# because this contract was unenforced then; it is enforced now, two ways:
#
#   * STRUCTURALLY: the only production call site of executor.tick() is
#     inside sim.tick(), behind the same pause/winner gates as gameplay.
#   * MECHANICALLY: the sim tracks, per executor, the interpreter-tick value
#     it last left that executor's env at (seeded from the env's hashed
#     clock at registration, so an adopting peer derives the minter's
#     value). Every step checks the env clock against that expectation
#     BEFORE ticking (catches an out-of-band executor.tick() by any other
#     caller) and re-checks AFTER (catches an executor whose tick advanced
#     the clock by anything but 1). NOTE the expectation is a tracked value,
#     not a constant offset from tick_index: decided-match and lockstep-
#     paused ticks advance the sim clock while deliberately stepping no
#     scripts, so the two clocks legitimately drift APART across frozen
#     ticks - what must never happen is the ENV clock moving except under
#     this function. A violation quarantines the executor loudly -
#     push_error naming team, expected and actual, script_wiring_faults
#     incremented, no further steps - rather than silently re-syncing,
#     because by then the hashed env tick has already diverged from every
#     correct peer and hiding it would be a silent desync. Both clocks ride
#     the snapshot, so the hash barrier catches whatever the quarantine
#     reports.
#
# WIRING, NOT STATE. The registration table is process-local plumbing like
# frame_conversions: script BODIES are match configuration (identical bytes
# on every peer, from the content pack), and everything the scripts DO lands
# in script_env_state / the sim's own hashed rows. Registrations are held by
# WEAKREF - the match owner (vertical slice, test runner) keeps the executor
# alive - so sim -> executor -> world -> sim never forms a RefCounted cycle.
# A registration whose executor was freed is reported loudly and dropped,
# never skipped silently.
#
# INERT BY DEFAULT. With no registered executor _step_script_executors()
# returns before touching anything, no env is attached, and no script state
# key exists - a scriptless match is bit-identical to one built before this
# seam existed, which the frozen b177804c pin proves on every run.

## team id -> WeakRef of the SageScriptExecutor running that team's scripts.
var _script_executors: Dictionary = {}
## team id -> the env interpreter tick _step_script_executors last left that
## executor at. Seeded from the env's hashed clock at registration and rebased
## by setup()/restore() from the same hashed values every peer holds, so the
## expectation is derived, deterministic, and identical on every peer.
var _script_executor_expected_ticks: Dictionary = {}
## team id -> true once quarantined by a cadence fault. Cleared by setup().
var _script_executor_faults: Dictionary = {}
## Diagnostic count of wiring faults (freed executor, stale env, cadence
## violation). Process-local observability, never hashed.
var script_wiring_faults: int = 0


func register_script_executor(executor: SageScriptExecutor, team: int) -> bool:
	## Wire `executor` to run team `team`'s scripts inside tick(). Refuses
	## loudly rather than guessing: null executors, unrostered teams, a team
	## that already has a live executor, and - the choke point that makes env
	## lifetime detection airtight - an executor whose env is not attached to
	## THIS sim's store UNDER THIS TEAM (attach_script_env(env, team) first).
	## Both halves of that check are load-bearing: an env attached to another
	## sim would run scripts against state this sim never hashes, and an env
	## attached to this sim under a DIFFERENT team would run in team `team`'s
	## step slot while writing the other team's state key - the 0dce37e review
	## registered a team-0 env under team 1 (and a swapped PAIR) and both were
	## accepted, silently inverting the ascending-team-order guarantee with
	## zero faults.
	if executor == null:
		push_error("register_script_executor refused: null executor")
		return false
	if not _script_owner_exists(team):
		push_error("register_script_executor refused: team %d is not a script-capable owner" % team)
		return false
	if _script_executors.has(team) and (_script_executors[team] as WeakRef).get_ref() != null:
		push_error("register_script_executor refused: team %d already has a registered executor" % team)
		return false
	if executor.env == null or not executor.env.attached_to(self):
		push_error(
			"register_script_executor refused: the executor's env is not attached "
			+ "to this sim's state store (call attach_script_env(executor.env, %d) first)" % team
		)
		return false
	var env_key: Variant = executor.env.attachment_key()
	if typeof(env_key) != TYPE_INT or int(env_key) != team:
		push_error(
			(
				"register_script_executor refused: the executor's env is attached "
				+ "under state-store key %s, not registration team %d - stepping it "
				+ "in team %d's slot would run its scripts against another team's "
				+ "hashed state (attach_script_env(executor.env, %d) first)"
			) % [str(env_key), team, team, team]
		)
		return false
	_script_executors[team] = weakref(executor)
	_script_executor_expected_ticks[team] = executor.env.tick_index
	_script_executor_faults.erase(team)
	return true


func unregister_script_executor(team: int) -> bool:
	if not _script_executors.has(team):
		return false
	_script_executors.erase(team)
	_script_executor_expected_ticks.erase(team)
	_script_executor_faults.erase(team)
	return true


func registered_script_executor_teams() -> Array:
	return _sorted_dictionary_keys(_script_executors)


func _step_script_executors() -> void:
	## The contract's enforcement point - see the block comment above.
	if _script_executors.is_empty():
		return
	for team_key in _sorted_dictionary_keys(_script_executors):
		var executor_ref: WeakRef = _script_executors[team_key]
		var executor: SageScriptExecutor = executor_ref.get_ref()
		if executor == null:
			script_wiring_faults += 1
			push_error(
				"script wiring: the executor registered for team %s was freed while "
				% str(team_key)
				+ "registered; dropping the registration - its scripts stop HERE, loudly"
			)
			_script_executors.erase(team_key)
			_script_executor_expected_ticks.erase(team_key)
			_script_executor_faults.erase(team_key)
			continue
		if _script_executor_faults.get(team_key, false):
			continue  # quarantined; the fault was reported once when it happened
		if executor.env.attachment_stale():
			_quarantine_script_executor(team_key, "its env's backing store owner was freed")
			continue
		var expected := int(_script_executor_expected_ticks[team_key])
		var before := executor.env.tick_index
		if before != expected:
			_quarantine_script_executor(
				team_key,
				"cadence violation before the step: env tick %d, expected %d - something ticked this executor outside sim.tick()"
				% [before, expected]
			)
			continue
		executor.tick()
		if executor.env.tick_index != before + 1:
			_quarantine_script_executor(
				team_key,
				"cadence violation during the step: one executor tick moved the env clock %d -> %d (must be exactly +1)"
				% [before, executor.env.tick_index]
			)
			continue
		_script_executor_expected_ticks[team_key] = before + 1
	# Sequential heads progress after ordinary script evaluation so the same
	# logic frame can both queue (TEAM_EXECUTE_SEQUENTIAL_*) and advance.
	_step_sequential_scripts()
	# Retail clears Team::m_created after the script pass (updateState).
	clear_team_created_edges()


func _quarantine_script_executor(team_key: Variant, reason: String) -> void:
	script_wiring_faults += 1
	_script_executor_faults[team_key] = true
	push_error(
		"script wiring: quarantining team %s's executor - %s. Its scripts no "
		% [str(team_key), reason]
		+ "longer run; the hashed interpreter clock already carries the divergence."
	)


func _rebase_script_executor_offsets() -> void:
	## The expected env clock is DERIVED wiring: the interpreter tick is
	## hashed state, so whenever it moves out-of-band-but-legitimately
	## (setup() zeroes it, restore() sets it from a snapshot) the expectation
	## is recomputed from the same value every peer holds.
	for team_key in _script_executors.keys():
		var executor: SageScriptExecutor = (_script_executors[team_key] as WeakRef).get_ref()
		if executor != null and not executor.env.attachment_stale():
			_script_executor_expected_ticks[team_key] = executor.env.tick_index


# --- Retail object-type identity (derived reads over existing hashed rows) --
#
# Counting and nearest-object queries by RETAIL object-type name. No new sim
# state: every identity consulted here already lives inside the hash/snapshot
# boundary - entity rows carry their compiled rule's provenance
# (retail_rule_provenance.source_object_id, the document's retail objectId)
# and their runtime ids (unit_type, a deterministic slug of the retail
# container name); structure rows carry structure_kind, resolved through the
# team manifest's producer_kind_registry (retail source object id -> kind,
# part of the hashed rules/config) plus the expansion build rules; creep camps
# carry their retail type_name verbatim. Matching is therefore EXACT string
# identity over recorded facts, never a heuristic: retail names fold case
# (SAGE INI object lookups are case-insensitive), runtime ids compare exactly.
#
# KNOWN LIMIT, recorded rather than papered over: rows whose identity was
# never recorded cannot be matched. That is (a) the legacy tiny-pack's
# hand-written synthetic ids where the id was not derived from the retail
# name by the standard slug (the tower guard: "gondor-tower-guard" vs retail
# GondorTowerShieldGuardHorde), and (b) creep GUARD battalions, whose ids are
# synthetic creep-family keys. Pack-driven content - the shipping path -
# records provenance on every unit rule, so its censuses are exact.


func count_objects_of_types(team: int, type_names: Array, include_dead: bool) -> int:
	## Census of `team`'s objects (battalion rows AND structure rows) whose
	## retail type matches any name in `type_names`. STRICTLY READ-ONLY: this
	## backs the retail AI's highest-traffic condition
	## (PLAYER_HAS_OBJECT_COMPARISON) and conditions are evaluated an
	## unpredictable number of times.
	##
	## `include_dead` counts rows regardless of health - rows that still
	## EXIST. Structure rows persist after razing; battalion rows persist
	## until corpse expiry (CORPSE_LIFETIME_TICKS), after which retail has
	## deleted the object too. Living-only is the default reading.
	##
	## The count is exact over the enumerable object census: every countable
	## row's identity is recorded (see the block comment), so a name matching
	## zero rows is a true zero about THIS match, not a guess - a type the
	## simulation cannot field has no instances here by construction.
	var probe := _object_type_probe(type_names)
	var total := 0
	for id in entity_ids():
		var row: Dictionary = entities[id]
		if int(row.get("team", -1)) != team:
			continue
		if not include_dead and int(row.get("health", 0)) <= 0:
			continue
		if _entity_matches_types(row, probe):
			total += 1
	for structure_id in structure_ids(team):
		var row: Dictionary = structures[structure_id]
		if not include_dead and int(row.get("health", 0)) <= 0:
			continue
		if _structure_matches_types(row, probe):
			total += 1
	return total


func living_object_levels_of_types(team: int, type_names: Array) -> Array[int]:
	## Current rank of every LIVING battalion or structure owned by `team`
	## whose recorded retail identity matches `type_names`. This is a derived,
	## read-only view over the same authoritative rows and exact matcher used
	## by count_objects_of_types; it adds no veterancy cache or history.
	var probe := _object_type_probe(type_names)
	var levels: Array[int] = []
	for id in entity_ids():
		var row: Dictionary = entities[id]
		if (
			int(row.get("team", -1)) == team
			and int(row.get("health", 0)) > 0
			and _entity_matches_types(row, probe)
		):
			levels.append(int(row.get("level", 1)))
	for structure_id in structure_ids(team):
		var row: Dictionary = structures[structure_id]
		if int(row.get("health", 0)) > 0 and _structure_matches_types(row, probe):
			levels.append(int(row.get("level", 1)))
	return levels


func nearest_object_of_types(origin: Vector2, type_names: Array, owner_teams: Array) -> Dictionary:
	## Nearest LIVING object (battalion or structure) whose retail type
	## matches any of `type_names`, owned by any team in `owner_teams` (empty
	## = any owner, creep and neutral rows included). Read-only.
	##
	## DETERMINISM: candidates are visited in sorted id order and the winner
	## is the minimum under an exact TOTAL order - strictly-less squared
	## distance, ties to battalions before structures (the two id spaces may
	## overlap numerically), then to the LOWEST id. Never is_equal_approx: a
	## tolerance comparison is not transitive and cannot define a total order.
	## Answers {"found": false} or {"found": true, "kind": "battalion"|
	## "structure", "id": int, "position": Vector2}.
	var probe := _object_type_probe(type_names)
	var owner_filter := {}
	for team_value in owner_teams:
		owner_filter[int(team_value)] = true
	var found := false
	var best_id := 0
	var best_rank := 0
	var best_distance := 0.0
	var best_position := Vector2.ZERO
	for id in entity_ids():
		var row: Dictionary = entities[id]
		if int(row.get("health", 0)) <= 0:
			continue
		if not owner_filter.is_empty() and not owner_filter.has(int(row.get("team", -1))):
			continue
		if not _entity_matches_types(row, probe):
			continue
		var distance := origin.distance_squared_to(Vector2(row.get("position", Vector2.ZERO)))
		var wins := not found or distance < best_distance
		if not wins and distance == best_distance:
			wins = 0 < best_rank or (best_rank == 0 and id < best_id)
		if wins:
			found = true
			best_id = id
			best_rank = 0
			best_distance = distance
			best_position = Vector2(row.get("position", Vector2.ZERO))
	for structure_id in structure_ids():
		var row: Dictionary = structures[structure_id]
		if int(row.get("health", 0)) <= 0:
			continue
		if not owner_filter.is_empty() and not owner_filter.has(int(row.get("team", -1))):
			continue
		if not _structure_matches_types(row, probe):
			continue
		var distance := origin.distance_squared_to(Vector2(row.get("position", Vector2.ZERO)))
		var wins := not found or distance < best_distance
		if not wins and distance == best_distance:
			wins = 1 < best_rank or (best_rank == 1 and structure_id < best_id)
		if wins:
			found = true
			best_id = structure_id
			best_rank = 1
			best_distance = distance
			best_position = Vector2(row.get("position", Vector2.ZERO))
	if not found:
		return {"found": false}
	return {
		"found": true,
		"kind": "battalion" if best_rank == 0 else "structure",
		"id": best_id,
		"position": best_position,
	}


func fieldable_object_type(name: String) -> bool:
	## Whether THIS simulation could ever field an object of the retail type
	## `name` - derived from match configuration only (unit rules, per-team
	## manifests, expansion rules, creep families), so it is identical on
	## every peer and never moves with match state. Callers use it to
	## distinguish "zero of a type this match can express" (a truthful no-op)
	## from "a type outside this simulation's model entirely" (a refusal that
	## keeps the modeling gap visible - the retail AI's tactical-marker moves
	## land there). Order-independent: a pure any() over configuration sets.
	if name == "":
		return false
	var folded := name.to_lower()
	var runtime_id := PlayableUnitAdapter.runtime_object_id(name)
	var unit_rules: Dictionary = _rules.get("unit_rules", {}) as Dictionary
	for object_id_value in unit_rules.keys():
		var rule: Dictionary = unit_rules[object_id_value] as Dictionary
		var source := String((rule.get("provenance", {}) as Dictionary).get("source_object_id", ""))
		if source != "" and source.to_lower() == folded:
			return true
		if String(object_id_value) == runtime_id or String(rule.get("horde_id", "")) == runtime_id:
			return true
	for team_value in _roster_team_ids():
		var team := int(team_value)
		var manifest := team_manifest_for(team)
		var registry: Dictionary = _structure_source_registry(manifest)
		for source_value in registry.keys():
			if String(source_value).to_lower() == folded:
				return true
		for object_id_value in (manifest.get("structure_object_ids", {}) as Dictionary).values():
			if String(object_id_value) == runtime_id:
				return true
		if unit_production_rules_for_team(team).has(runtime_id):
			return true
	for kind_value in _expansion_build_rules.keys():
		var expansion_object_id := String((_expansion_build_rules[kind_value] as Dictionary).get("object_id", ""))
		if expansion_object_id == runtime_id or expansion_object_id.to_lower() == folded:
			return true
	for registry_key in ["scenario_unit_runtimes", "scenario_structure_runtimes"]:
		for object_id_value in (_rules.get(registry_key, {}) as Dictionary).keys():
			if String(object_id_value).to_lower() == folded:
				return true
	return false


func _object_type_probe(type_names: Array) -> Dictionary:
	## Per-query matching keys: case-folded retail names, their derived
	## runtime ids, and a lazily filled per-team structure-kind cache.
	var folded := {}
	var runtime_ids := {}
	for name_value in type_names:
		var name := String(name_value)
		if name == "":
			continue
		folded[name.to_lower()] = true
		runtime_ids[PlayableUnitAdapter.runtime_object_id(name)] = true
	return {"folded": folded, "runtime_ids": runtime_ids, "kinds_by_team": {}}


func _entity_matches_types(row: Dictionary, probe: Dictionary) -> bool:
	## A battalion row matches on its recorded provenance (the retail source
	## object id, authoritative) or on its runtime container id (unit_type is
	## the deterministic slug of the retail container name for every
	## pack-driven rule). The MEMBER id (row.object_id) deliberately does not
	## match: a row is ONE retail horde object, and counting the member name
	## as the horde would answer 1 where retail counts 15 members - the exact
	## granularity lie the class comment forbids.
	var provenance: Dictionary = row.get("retail_rule_provenance", {}) as Dictionary
	var source := String(provenance.get("source_object_id", ""))
	if source != "" and (probe["folded"] as Dictionary).has(source.to_lower()):
		return true
	return (probe["runtime_ids"] as Dictionary).has(String(row.get("unit_type", "")))


func _structure_matches_types(row: Dictionary, probe: Dictionary) -> bool:
	## A structure row matches through its team's kind registry (retail source
	## object id -> structure kind), the manifest/expansion runtime ids, or its
	## descriptor-backed scenario source identity.
	var scenario_type := String(row.get("scenario_source_object_id", row.get("source_object_id", "")))
	if scenario_type != "" and (probe["folded"] as Dictionary).has(scenario_type.to_lower()):
		return true
	var team := int(row.get("team", -1))
	var kinds_by_team: Dictionary = probe["kinds_by_team"]
	if not kinds_by_team.has(team):
		kinds_by_team[team] = _structure_kinds_matching_probe(team, probe)
	return (kinds_by_team[team] as Dictionary).has(String(row.get("structure_kind", "")))


func _structure_kinds_matching_probe(team: int, probe: Dictionary) -> Dictionary:
	## The set of structure kinds (for `team`'s manifest) that the probe's
	## names denote. Built as a SET, so source-dictionary iteration order
	## cannot affect any answer.
	var kinds := {}
	var folded: Dictionary = probe["folded"]
	var runtime_ids: Dictionary = probe["runtime_ids"]
	var manifest := team_manifest_for(team)
	var registry := _structure_source_registry(manifest)
	for source_value in registry.keys():
		if folded.has(String(source_value).to_lower()):
			kinds[String(registry[source_value])] = true
	for kind_value in (manifest.get("structure_object_ids", {}) as Dictionary).keys():
		if runtime_ids.has(String((manifest.get("structure_object_ids", {}) as Dictionary)[kind_value])):
			kinds[String(kind_value)] = true
	for kind_value in _expansion_build_rules.keys():
		# Expansion rules record either the runtime id (the vertical slice's
		# doc-driven path) or a plain source-style name (synthetic fixtures);
		# both compare exactly against their own key form.
		var expansion_object_id := String((_expansion_build_rules[kind_value] as Dictionary).get("object_id", ""))
		if runtime_ids.has(expansion_object_id) or folded.has(expansion_object_id.to_lower()):
			kinds[String(kind_value)] = true
	return kinds


func _structure_source_registry(manifest: Dictionary) -> Dictionary:
	## retail structure source object id -> structure kind, from the team's
	## manifest; the vertical slice's global registry is the fallback for the
	## legacy manifest-free rules shape.
	var registry: Variant = manifest.get("producer_kind_registry")
	if typeof(registry) == TYPE_DICTIONARY and not (registry as Dictionary).is_empty():
		return registry as Dictionary
	return _rules.get("producer_kind_by_source_object", {}) as Dictionary


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
		_apply_group_speed_cap(accepted_ids)
		_stamp_order_sequence(accepted_ids)
		last_route_rejection = ""
		_emit_event(ack_kind, accepted_ids[0], 0, _voice_event_identity(accepted_ids[0]))
	return accepted_ids.size()


func _apply_group_speed_cap(accepted_ids: Array[int]) -> void:
	## WaitForFormation (locomotor.ini:713, on the melee / charge-melee / ranged
	## horde locomotors): "When moving into formations, these guys stop & wait for
	## others." Retail's observable consequence is that a mixed selection advances
	## at the pace of its slowest member and arrives roughly together, rather than
	## the cavalry landing a lap ahead of the pikes.
	##
	## The cap is the minimum AUTHORED speed across the group, so per-row stance,
	## formation, and ability multipliers still apply on top of it in _step_route.
	## A single-battalion order caps at its own speed, i.e. no change.
	##
	## Deterministic: min over the accepted set is order-independent, and the
	## accepted set is already built in caller-supplied id order.
	## Per-row WaitForFormation, not the global retail_formation_movement flag.
	## Pin fixtures do not author the field, so they stay absent-unless-set.
	## A mixed group that includes at least one waiter coheres at the slowest
	## authored speed; only waiters receive the key.
	var waiters: Array[int] = []
	if retail_formation_movement:
		waiters = accepted_ids.duplicate()
	else:
		for id in accepted_ids:
			if bool((entities[id] as Dictionary).get("wait_for_formation", false)):
				waiters.append(id)
	if waiters.is_empty():
		return
	var slowest := INF
	for id in accepted_ids:
		var row: Dictionary = entities[id]
		slowest = minf(slowest, maxf(0.0, float(row.get("speed", 0.0))))
	if slowest == INF:
		return
	for id in waiters:
		(entities[id] as Dictionary)["group_speed_cap"] = slowest


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
		if bool(row.get("noncombatant", false)):
			continue
		if int(row["team"]) == int(target["team"]):
			continue
		if target_kind == "battalion" and not _can_engage_battalion(row, target):
			continue
		if not _assign_target_route(row, Vector2(target["position"])):
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
		# A group attack coheres exactly like a group move; refreshing here also
		# stops a cap left over from an earlier escort order throttling the
		# charge.
		_apply_group_speed_cap(accepted_ids)
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
		_rearm_mood_idle_cadence(row)
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


func _authored_formation_toggle(document: Dictionary) -> Dictionary:
	## The unit's own HORDE_TOGGLE_FORMATION button, read off the compiled
	## selection surface so the sim gate and the palantir gate answer from the
	## SAME authored data (commandbutton.ini / commandset.ini).
	##
	var compiled_toggle := PlayableUnitAdapter.formation_toggle_contract(document)
	var modifier_lists: Array = compiled_toggle.get("modifierLists", []) as Array
	var effects: Array = []
	var modifier_ids: Array[String] = []
	var unsupported_receipts: Array[String] = []
	for list_value in modifier_lists:
		var modifier_list := list_value as Dictionary
		var modifier_id := String(modifier_list.get("id", ""))
		if modifier_id != "":
			modifier_ids.append(modifier_id)
		for modifier_value in modifier_list.get("modifiers", []) as Array:
			var modifier := modifier_value as Dictionary
			if String(modifier.get("runtimeSupport", "receipt-only")) == "supported":
				effects.append(modifier.duplicate(true))
			else:
				unsupported_receipts.append(
					"formation_modifier_unsupported:%s:%s" % [
						modifier_id, String(modifier.get("kind", "UNKNOWN"))
					]
				)
	for selection_value in PlayableUnitAdapter.selection_commands(document):
		var selection := selection_value as Dictionary
		for kind_value in selection.get("commandKinds", []) as Array:
			if String(kind_value).strip_edges().to_upper() != "HORDE_TOGGLE_FORMATION":
				continue
			return {
				"command_id": String(selection.get("commandId", "")),
				"command_set_id": String(selection.get("commandSetId", "")),
				"source_ini": String(selection.get("sourceIni", "")),
				"modifier": {
					"id": modifier_ids[0] if modifier_ids.size() == 1 else "+".join(modifier_ids),
					"modifier_ids": modifier_ids,
					"category": "FORMATION",
					"modifiers": effects,
					"unsupported_receipts": unsupported_receipts,
				},
			}
	return {}


func horde_formation_toggle(row: Dictionary) -> Dictionary:
	## The unit's authored HORDE_TOGGLE_FORMATION button, or {} when its command
	## set carries none.
	##
	## RETAIL ORACLE: a formation toggle is authored per command set, not given
	## to every unit. data/ini/commandbutton.ini declares 18 live
	## HORDE_TOGGLE_FORMATION buttons (Command_TowerGuardPorcupineFormation
	## :664, Command_ToggleFormationGondorFighter :196, Rohan :676, Isengard
	## pikeman :690, Mithlond :702, Dwarven :714, Wild :726, Mordor Easterling
	## :738, Angmar :9678, ...), each with
	## `Options = TOGGLE_IMAGE_ON_FORMATION OK_FOR_MULTI_SELECT` and a TWO-image
	## ButtonImage; only 13 command sets reference one. A Gondor archer horde
	## has no such button and cannot be put into a formation at all.
	return row.get("formation_toggle", {}) as Dictionary


func _formation_order_admitted(row: Dictionary) -> bool:
	if not horde_formation_toggle(row).is_empty():
		return true
	# LEGACY FIXTURE CARVE-OUT, deliberate and narrow: synthetic runner rows
	# that predate the compiled command surface carry no module contracts and
	# no authored toggle, and the determinism pins script a formation order on
	# exactly such a row. Descriptor-backed rows -- everything a real pack
	# produces -- are held to the authored data.
	return not row.has("module_contracts") and not row.has("command_surface")


func issue_toggle_formation(ids: Array[int], team: int = PLAYER_TEAM) -> int:
	var accepted_ids: Array[int] = []
	for id in ids:
		if accepted_ids.has(id) or not _is_commandable_for_team(id, team):
			continue
		var row: Dictionary = entities[id]
		if bool(row.get("is_builder", false)):
			continue
		if not _formation_order_admitted(row):
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
		if not _formation_order_admitted(row):
			continue
		row["formation_mode"] = formation
		_apply_formation_mode(row)
		accepted_ids.append(id)
	if not accepted_ids.is_empty():
		_stamp_order_sequence(accepted_ids)
		_emit_event("order.formation", accepted_ids[0], 0, {"formation": formation})
	return accepted_ids.size()


const FORMATION_MODIFIER_KEY := "formation"


func _apply_formation_attribute_modifier(row: Dictionary) -> void:
	## RETAIL ORACLE: the toggle swaps the horde to its authored
	## `AlternateFormation` ChildObject, whose HordeContain carries
	## `AttributeModifiers = <ModifierList>` and `IsPorcupineFormation = Yes`
	## (object/evilfaction/hordes/isengard/isengardhordes.ini:531-548, and the
	## file's own note: "for alternate formations, all info outside of the
	## Contain Behavior module is ignored. Any modifications need to be done via
	## the Attribute Modifiers in the contain module").
	##
	## That ModifierList is attributemodifier.ini:756-764
	## `ModifierList GondorTowerShieldGuardHordePorcupine / Category = FORMATION
	## / Modifier = CRUSHED_DECELERATE 1000% / Duration = 0` -- and the same
	## shape at :766-806 for Isengard, Mithlond, Dwarven, Wild and Mordor.
	## The SPEED / ARMOR / DAMAGE_ADD / CRUSHABLE_LEVEL rows in those blocks are
	## COMMENTED OUT in retail and are deliberately NOT applied here.
	##
	## Duration = 0 is "forever" (the file says so at :764), so the entry never
	## expires; it is erased when the horde leaves the formation.
	var toggle := horde_formation_toggle(row)
	var table: Dictionary = row.get("timed_modifiers", {}) as Dictionary
	var was_active: bool = table.has(FORMATION_MODIFIER_KEY)
	var modifier := toggle.get("modifier", {}) as Dictionary
	var effects: Array = modifier.get("modifiers", []) as Array
	var active: bool = (
		String(row.get("formation_mode", "Line")) != "Line" and not effects.is_empty()
	)
	if not active and not was_active:
		# Nothing authored and nothing to drop: leave the row byte-identical.
		# Rows carry an empty `timed_modifiers` dict by construction, so an
		# unconditional erase-if-empty here would change every hashed row.
		return
	table.erase(FORMATION_MODIFIER_KEY)
	if active:
		table[FORMATION_MODIFIER_KEY] = {
			"modifiers": effects.duplicate(true),
			"expires_tick": -1,
			"persistent": true,
			"category": String(modifier.get("category", "FORMATION")),
			"modifier_id": String(modifier.get("id", "")),
			"modifier_ids": Array(modifier.get("modifier_ids", [])).duplicate(),
			"unsupported_modifier_receipts": Array(
				modifier.get("unsupported_receipts", [])
			).duplicate(),
			"source_id": int(row.get("id", 0)),
		}
	row["timed_modifiers"] = table


func _apply_formation_mode(row: Dictionary) -> void:
	_apply_formation_attribute_modifier(row)
	var base: Array = row.get("formation_positions_base", row.get("formation_positions", [])) as Array
	if base.is_empty():
		return
	var mode := String(row.get("formation_mode", "Line"))
	var isotropic := float(FORMATION_SPACING.get(mode, 1.0))
	# x = lateral, z = depth (see FORMATION_SPACING_RETAIL).
	var lateral_scale := isotropic
	var depth_scale := isotropic
	if retail_formation_movement:
		var authored: Dictionary = FORMATION_SPACING_RETAIL.get(mode, {}) as Dictionary
		lateral_scale = float(authored.get("lateral", 1.0))
		depth_scale = float(authored.get("depth", 1.0))
	var scaled: Array = []
	for slot_value in base:
		if typeof(slot_value) != TYPE_VECTOR3:
			scaled.append(Vector3.ZERO)
			continue
		var slot: Vector3 = slot_value
		scaled.append(Vector3(slot.x * lateral_scale, slot.y, slot.z * depth_scale))
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
	var ids := entity_ids()
	for index in range(ids.size()):
		var a: Dictionary = entities[ids[index]]
		# Only settled battalions get nudged apart: pushing marching columns
		# around mid-route disrupts ford crossings and formation moves.
		if int(a.get("health", 0)) <= 0 or String(a.get("state", "")) != "idle" or bool(a.get("flying", false)) or bool(a.get("presentation_hidden", false)):
			continue
		# Only battalions overlapping `a` can be pushed by it, so the old
		# all-pairs inner sweep is a neighbourhood query over ids above `a`.
		#
		# `a` also moves as it separates. The gather covers everything within
		# BATTALION_SEPARATION_QUERY_SLACK of where `a` started, so while its
		# drift stays inside that slack the candidate set is a superset of what
		# the all-pairs loop would have tested. If a pile-up ever pushes `a`
		# further than that, the sweep falls back to the full id list for the
		# rest of this battalion rather than silently skipping a partner.
		# Partners are consumed in ascending id order from either list, so the
		# fallback resumes by id and every pair is still visited exactly once.
		var gather_origin := Vector2(a["position"])
		var neighbours := _spatial_gather_sorted(
			gather_origin, BATTALION_SEPARATION_RADIUS + BATTALION_SEPARATION_QUERY_SLACK
		)
		var widened := false
		var previous_id: int = ids[index]
		var cursor := 0
		while true:
			# Validate the candidate list before it is used to pick the next
			# partner, so a drifted `a` never selects from a set that no longer
			# covers its neighbourhood.
			if not widened \
					and gather_origin.distance_to(Vector2(a["position"])) > BATTALION_SEPARATION_QUERY_SLACK:
				widened = true
				neighbours = ids
				cursor = 0
			while cursor < neighbours.size() and neighbours[cursor] <= previous_id:
				cursor += 1
			if cursor >= neighbours.size():
				break
			var other_id: int = neighbours[cursor]
			previous_id = other_id
			if not entities.has(other_id):
				continue
			var b: Dictionary = entities[other_id]
			if int(b.get("health", 0)) <= 0 or String(b.get("state", "")) != "idle" or bool(b.get("flying", false)) or bool(b.get("presentation_hidden", false)):
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
				_spatial_sync(a)
			if _position_walkable(b_target):
				b["position"] = b_target
				_spatial_sync(b)


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
		if row.has("auto_heal_behavior"):
			# An authored AutoHealBehavior contract is the real regeneration
			# rate; the provisional percentage would stack a second heal on top.
			continue
		var health := int(row.get("health", 0))
		var maximum := int(row.get("maximum_health", 0))
		if health <= 0 or health >= maximum:
			continue
		var ticks_since_damage := tick_index - int(row.get("last_damage_tick", -1000000))
		if float(ticks_since_damage) * TICK_SECONDS < HERO_REGEN_OUT_OF_COMBAT_SECONDS:
			continue
		# AUTO_HEAL scales the object's own regeneration rate, which is what
		# retail's AutoHeal attribute ladder does: it multiplies an authored
		# AutoHealBehavior, it does not author one. A unit that declares no
		# multiplier regenerates at exactly the historical rate.
		var heal_rate := HERO_REGEN_PERCENT_PER_SECOND * float(row.get("auto_heal_multiplier", 1.0))
		var amount := maxi(1, roundi(float(maximum) * heal_rate * TICK_SECONDS))
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


# --- AutoHealBehavior (authored self-heal cadence) ---
# Retail's AutoHealBehavior is a flat HealingAmount applied every HealingDelay
# milliseconds, restarted StartHealingDelay after the object was last damaged
# when HealOnlyIfNotInCombat or HealOnlyIfNotUnderAttack is authored. Heroes
# carry HERO_HEAL_AMOUNT (30) on
# a 1000ms pulse behind a HERO_HEAL_DELAY (15000ms) restart, which is nothing
# like the provisional percentage above. Only the importer's closed executable
# subset arms here: area heals, button bursts, upgrade-triggered activation and
# containment heals stay authored evidence with no timer.


func _attach_auto_heal_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_auto_heal_contract(row, contract)


func _step_auto_heal_updates() -> void:
	_contracts_subsystem()._step_auto_heal_updates()


func _apply_auto_heal_pulse(row: Dictionary, battalion: bool) -> void:
	_contracts_subsystem()._apply_auto_heal_pulse(row, battalion)


var _unit_ability_rules: Dictionary = {}
var _shared_ability_cooldowns: Dictionary = {} # team:special-power -> ready tick

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
	return _contracts_subsystem()._scaled_ability_rules(rules, source_scale)


func _attach_hero_ability_state(row: Dictionary) -> void:
	_contracts_subsystem()._attach_hero_ability_state(row)


var _unit_experience_rules: Dictionary = {}
var _experience_unauthored_victims: Dictionary = {}
## unit_type -> Array of projected moduleContracts (typed + opaque deferred).
var _unit_module_contracts: Dictionary = {}
## structure object_id / structure_kind -> projected moduleContracts.
var _structure_module_contracts: Dictionary = {}
## Retail's CastleUpgrade indirection, projected from the fortress documents'
## own moduleContracts rows.
##
## A fortress improvement button (Command_PurchaseUpgradeMordorFortressLavaMoat,
## Command_PurchaseUpgradeAngmarFortressIceWalls, ...) does NOT buy the upgrade
## it names. It buys a *Trigger* upgrade (Upgrade_AngmarFortressIceWallsTrigger,
## upgrade.ini), and a CastleUpgrade behavior on the fortress converts that
## trigger into the real upgrade and hands it to the castle
## (angmarfortress.ini:1282-1286 "Behavior = CastleUpgrade
## ModuleTag_PassOutAngmarStoneworkUpgrade / TriggeredBy =
## Upgrade_AngmarFortressIceWallsTrigger / Upgrade = Upgrade_AngmarFortressIceWalls
## / WallUpgradeRadius = ..."). Every downstream module — AttributeModifierUpgrade,
## SubObjectsUpgrade, WeaponSetUpgrade — is triggered by the REAL upgrade, so
## without this hop a purchased fortress improvement does nothing at all.
##
## trigger upgrade id (folded) -> Array[{upgrade_id, wall_upgrade_radius,
## source_object_id, tag}].
var _castle_upgrade_grants: Dictionary = {}
## Match-scoped objective history: team -> authored hero unit type -> peak
## rank reached. A value of -1 records that the hero existed but had no
## authored ExperienceLevel chain, so historical negative answers refuse
## instead of silently forgetting uncertainty after death. Hero unit type is
## the revival-stable identity used by production and the experience rules.
var _hero_peak_ranks_by_team: Dictionary = {}


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


func _structure_contracts_with_passive_area_resolution(
	document: Dictionary, contracts: Array
) -> Array:
	return _contracts_subsystem()._structure_contracts_with_passive_area_resolution(document, contracts)


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


func _passive_area_effect_field(fields: Dictionary, key: String) -> String:
	return _contracts_subsystem()._passive_area_effect_field(fields, key)


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
	_contracts_subsystem()._attach_pickup_stuff_update_contract(row, contract)


func register_pickup_object(kind_of: Array, position: Vector2, object_id: String = "") -> int:
	return _contracts_subsystem().register_pickup_object(kind_of, position, object_id)


func remove_pickup_object(pickup_id: int) -> void:
	_contracts_subsystem().remove_pickup_object(pickup_id)


func _step_pickup_stuff_updates() -> void:
	_contracts_subsystem()._step_pickup_stuff_updates()


func _nearest_pickup_for(row: Dictionary, policy: Dictionary) -> int:
	return _contracts_subsystem()._nearest_pickup_for(row, policy)


func _attach_auto_ability_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_auto_ability_contract(row, contract)


func _resolve_auto_ability_range(value: Variant) -> float:
	return _contracts_subsystem()._resolve_auto_ability_range(value)


func set_auto_ability_active(entity_id: int, special_ability: String, active: bool) -> Dictionary:
	return _contracts_subsystem().set_auto_ability_active(entity_id, special_ability, active)


func _step_auto_abilities() -> void:
	_contracts_subsystem()._step_auto_abilities()


func _ability_id_for_special_power(row: Dictionary, special_power: String) -> String:
	return _contracts_subsystem()._ability_id_for_special_power(row, special_power)


func _auto_ability_query_target(source: Dictionary, behavior: Dictionary, minimum: float, maximum: float) -> int:
	return _contracts_subsystem()._auto_ability_query_target(source, behavior, minimum, maximum)


func _auto_ability_filter_accepts(source: Dictionary, candidate: Dictionary, tokens: Array) -> bool:
	return _contracts_subsystem()._auto_ability_filter_accepts(source, candidate, tokens)


func _attach_ai_special_power_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_ai_special_power_contract(row, contract)


func _resolve_ai_special_power_expression(value: Variant) -> Dictionary:
	return _contracts_subsystem()._resolve_ai_special_power_expression(value)


func _ai_special_power_type_supported(ai_type: String) -> bool:
	return _contracts_subsystem()._ai_special_power_type_supported(ai_type)


func _step_ai_special_power_updates() -> void:
	_contracts_subsystem()._step_ai_special_power_updates()


func _ability_auto_blocked_model_condition(row: Dictionary, rule: Dictionary) -> String:
	return _contracts_subsystem()._ability_auto_blocked_model_condition(row, rule)


func _ai_special_power_cast(entity_id: int, row: Dictionary, policy: Dictionary, point: Vector2) -> Dictionary:
	return _contracts_subsystem()._ai_special_power_cast(entity_id, row, policy, point)


func _ai_special_power_stance(ai_type: String) -> String:
	return _contracts_subsystem()._ai_special_power_stance(ai_type)


func _ai_special_power_desired_stance(row: Dictionary) -> String:
	return _contracts_subsystem()._ai_special_power_desired_stance(row)


func _ai_special_power_target(source: Dictionary, policy: Dictionary) -> Dictionary:
	return _contracts_subsystem()._ai_special_power_target(source, policy)


func _ai_special_power_ability_rule(source: Dictionary, command: String) -> Dictionary:
	return _contracts_subsystem()._ai_special_power_ability_rule(source, command)


func _ai_special_power_is_self(ai_type: String) -> bool:
	return _contracts_subsystem()._ai_special_power_is_self(ai_type)


func _ai_special_power_targets_allies(ai_type: String) -> bool:
	return _contracts_subsystem()._ai_special_power_targets_allies(ai_type)


func _ai_special_power_targets_structure(ai_type: String) -> bool:
	return _contracts_subsystem()._ai_special_power_targets_structure(ai_type)


func _ai_special_power_cluster_score(source: Dictionary, point: Vector2, radius: float, allies: bool) -> int:
	return _contracts_subsystem()._ai_special_power_cluster_score(source, point, radius, allies)


func _ai_special_power_structure_site_clear(point: Vector2, radius: float) -> bool:
	return _contracts_subsystem()._ai_special_power_structure_site_clear(point, radius)


func _attach_respawn_update_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_respawn_update_contract(row, contract)


func _schedule_respawn_update(entity_id: int, row: Dictionary, death_type:String="NORMAL", attacker_id:int=0) -> void:
	_contracts_subsystem()._schedule_respawn_update(entity_id, row, death_type, attacker_id)


func request_respawn(entity_id: int) -> Dictionary:
	return _contracts_subsystem().request_respawn(entity_id)


func _step_respawn_updates() -> void:
	_contracts_subsystem()._step_respawn_updates()


func _respawn_anchor(team: int, filter: Array) -> int:
	return _contracts_subsystem()._respawn_anchor(team, filter)


func _attach_fire_weapon_update_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_fire_weapon_update_contract(row, contract)


func _step_fire_weapon_updates() -> void:
	_contracts_subsystem()._step_fire_weapon_updates()


func _attach_deletion_update_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_deletion_update_contract(row, contract)


func _resolve_deletion_bound(value: Variant) -> Dictionary:
	return _contracts_subsystem()._resolve_deletion_bound(value)


func _step_deletion_updates() -> void:
	_contracts_subsystem()._step_deletion_updates()


func _attach_production_update_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_production_update_contract(row, contract)


func _attach_getting_built_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_getting_built_contract(row, contract)


func _resolve_build_seconds(value:Variant)->Dictionary:
	return _contracts_subsystem()._resolve_build_seconds(value)


func _attach_building_behavior_contract(row:Dictionary,contract:Dictionary)->void:
	_contracts_subsystem()._attach_building_behavior_contract(row, contract)


func _attach_queue_production_exit_contract(row:Dictionary,contract:Dictionary)->void:
	_contracts_subsystem()._attach_queue_production_exit_contract(row, contract)


func _attach_rebuild_hole_expose_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_rebuild_hole_expose_contract(row, contract)


func _attach_rebuild_hole_behavior_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_rebuild_hole_behavior_contract(row, contract)


func _expose_rebuild_hole(owner_id: int, owner: Dictionary, attacker_id: int) -> int:
	return _contracts_subsystem()._expose_rebuild_hole(owner_id, owner, attacker_id)


func _step_rebuild_holes() -> void:
	_contracts_subsystem()._step_rebuild_holes()


func _typed_contract_numbers(fields:Dictionary,key:String)->Array[float]:
	return _contracts_subsystem()._typed_contract_numbers(fields, key)


func _attach_banner_carrier_update_contract(row:Dictionary,contract:Dictionary)->void:
	_contracts_subsystem()._attach_banner_carrier_update_contract(row, contract)


func _resolve_respawn_body_expression(field:Variant)->Dictionary:
	return _contracts_subsystem()._resolve_respawn_body_expression(field)


func _attach_respawn_body_contract(row:Dictionary,contract:Dictionary)->void:
	_contracts_subsystem()._attach_respawn_body_contract(row, contract)


func _resolve_contract_milliseconds(field:Variant,define_key:String)->Dictionary:
	return _contracts_subsystem()._resolve_contract_milliseconds(field, define_key)


func _attach_give_upgrade_contract(row:Dictionary,contract:Dictionary)->void:
	_contracts_subsystem()._attach_give_upgrade_contract(row, contract)


func request_give_upgrade(source_id:int,target_id:int,upgrade_id:String,special_power:String="")->Dictionary:
	return _contracts_subsystem().request_give_upgrade(source_id, target_id, upgrade_id, special_power)


func _step_give_upgrade_updates()->void:
	_contracts_subsystem()._step_give_upgrade_updates()


func _attach_gate_open_close_contract(row:Dictionary,contract:Dictionary)->void:
	_contracts_subsystem()._attach_gate_open_close_contract(row, contract)


func _attach_ai_gate_contract(row:Dictionary,contract:Dictionary)->void:
	_contracts_subsystem()._attach_ai_gate_contract(row, contract)


func _attach_fake_pathfind_portal_contract(row:Dictionary,contract:Dictionary)->void:
	_contracts_subsystem()._attach_fake_pathfind_portal_contract(row, contract)


func request_gate_open(structure_id:int,requester_id:int=0)->Dictionary:
	return _contracts_subsystem().request_gate_open(structure_id, requester_id)


func _sync_gate_passage(structure_id: int) -> void:
	_contracts_subsystem()._sync_gate_passage(structure_id)


func gate_portal_allows(structure_id:int,requester_id:int)->bool:
	return _contracts_subsystem().gate_portal_allows(structure_id, requester_id)


func _step_gate_updates()->void:
	_contracts_subsystem()._step_gate_updates()


func _typed_effect_graph(contract: Dictionary, kind: String, target_mode: String) -> Dictionary:
	return _contracts_subsystem()._typed_effect_graph(contract, kind, target_mode)


func _attach_stop_special_power_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_stop_special_power_contract(row, contract)


func _active_special_power_channel_key(row: Dictionary, target_template: String) -> String:
	return _contracts_subsystem()._active_special_power_channel_key(row, target_template)


func activate_stop_special_power(entity_id: int, special_power_template: String, team: int = -1) -> Dictionary:
	return _contracts_subsystem().activate_stop_special_power(entity_id, special_power_template, team)


func _attach_unleash_special_power_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_unleash_special_power_contract(row, contract)


func _unleash_owned_slave(owner_id: int, owner: Dictionary, policy: Dictionary) -> int:
	return _contracts_subsystem()._unleash_owned_slave(owner_id, owner, policy)


func activate_unleash_special_power(owner_id: int, special_power_template: String, team: int = -1) -> Dictionary:
	return _contracts_subsystem().activate_unleash_special_power(owner_id, special_power_template, team)


func _attach_special_enemy_sense_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_special_enemy_sense_contract(row, contract)


func _special_enemy_sense_filter_accepts(target: Dictionary, filter: Array) -> bool:
	return _contracts_subsystem()._special_enemy_sense_filter_accepts(target, filter)


func _step_special_enemy_sense_updates() -> void:
	_contracts_subsystem()._step_special_enemy_sense_updates()


func _resolve_invisibility_expression(field: Variant, key: String) -> Dictionary:
	return _contracts_subsystem()._resolve_invisibility_expression(field, key)


func _attach_invisibility_update_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_invisibility_update_contract(row, contract)


func set_invisibility_update_active(object_id: int, enabled: bool, tag: String = "") -> Dictionary:
	return _contracts_subsystem().set_invisibility_update_active(object_id, enabled, tag)


func _invisibility_upgrade_gate(row: Dictionary, policy: Dictionary) -> bool:
	return _contracts_subsystem()._invisibility_upgrade_gate(row, policy)


func _invisibility_condition_set(row: Dictionary) -> Dictionary:
	return _contracts_subsystem()._invisibility_condition_set(row)


func _invisibility_source_key(object_id: int, policy: Dictionary, prefix: String = "module") -> String:
	return _contracts_subsystem()._invisibility_source_key(object_id, policy, prefix)


func _invisibility_source_active(row: Dictionary, source_key: String) -> bool:
	return _contracts_subsystem()._invisibility_source_active(row, source_key)


func _set_invisibility_source(target: Dictionary, source_key: String, policy: Dictionary, enabled: bool, source_id: int) -> void:
	_contracts_subsystem()._set_invisibility_source(target, source_key, policy, enabled, source_id)


func _revoke_invisibility_policy_sources(object_id: int, row: Dictionary, policy: Dictionary) -> void:
	_contracts_subsystem()._revoke_invisibility_policy_sources(object_id, row, policy)


func _step_invisibility_updates() -> void:
	_contracts_subsystem()._step_invisibility_updates()


func _step_invisibility_broadcast(source_id: int, source: Dictionary, policy: Dictionary, blocked: bool) -> void:
	_contracts_subsystem()._step_invisibility_broadcast(source_id, source, policy, blocked)


func _attach_stealth_detector_contract(row:Dictionary,contract:Dictionary)->void:
	_contracts_subsystem()._attach_stealth_detector_contract(row, contract)


func _step_stealth_detectors()->void:
	_contracts_subsystem()._step_stealth_detectors()


func _attach_slaved_update_contract(row:Dictionary,contract:Dictionary)->void:
	_contracts_subsystem()._attach_slaved_update_contract(row, contract)


func bind_slave(slave_id:int,master_id:int)->Dictionary:
	return _contracts_subsystem().bind_slave(slave_id, master_id)


func _step_slaved_updates()->void:
	_contracts_subsystem()._step_slaved_updates()


func _kill_slave_for_master_death(slave_id:int,slave:Dictionary)->void:
	_contracts_subsystem()._kill_slave_for_master_death(slave_id, slave)


func _attach_castle_upgrade_contract(row:Dictionary,contract:Dictionary)->void:
	_contracts_subsystem()._attach_castle_upgrade_contract(row, contract)


func apply_castle_upgrade_trigger(structure_id:int,trigger_upgrade_id:String)->Dictionary:
	return _contracts_subsystem().apply_castle_upgrade_trigger(structure_id, trigger_upgrade_id)


func _attach_spawn_behavior_contract(row:Dictionary,contract:Dictionary)->void:
	_contracts_subsystem()._attach_spawn_behavior_contract(row, contract)


func _step_spawn_behaviors()->void:
	_contracts_subsystem()._step_spawn_behaviors()


func _spawn_behavior_member(owner_id:int,owner:Dictionary,policy:Dictionary)->int:
	return _contracts_subsystem()._spawn_behavior_member(owner_id, owner, policy)


func _spawn_behavior_reclaim_orphan(owner_id:int,owner:Dictionary,policy:Dictionary)->int:
	return _contracts_subsystem()._spawn_behavior_reclaim_orphan(owner_id, owner, policy)


func _spawn_behavior_closest_orphan(owner:Dictionary,template:String)->int:
	return _contracts_subsystem()._spawn_behavior_closest_orphan(owner, template)


func _spawn_behavior_template_equivalent(candidate:Dictionary,template:String)->bool:
	return _contracts_subsystem()._spawn_behavior_template_equivalent(candidate, template)


func _attach_stealth_update_contract(row:Dictionary,contract:Dictionary)->void:
	_contracts_subsystem()._attach_stealth_update_contract(row, contract)


func set_stealth_update_active(entity_id:int,enabled:bool)->Dictionary:
	return _contracts_subsystem().set_stealth_update_active(entity_id, enabled)


func _step_stealth_updates()->void:
	_contracts_subsystem()._step_stealth_updates()


func _attach_object_creation_upgrade_contract(row:Dictionary,contract:Dictionary)->void:
	_contracts_subsystem()._attach_object_creation_upgrade_contract(row, contract)


func _attach_attribute_modifier_upgrade_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_attribute_modifier_upgrade_contract(row, contract)


func _attribute_modifier_upgrade_owned(row: Dictionary, upgrade_id: String) -> bool:
	return _contracts_subsystem()._attribute_modifier_upgrade_owned(row, upgrade_id)


func _attribute_modifier_upgrade_should_activate(row: Dictionary, policy: Dictionary) -> bool:
	return _contracts_subsystem()._attribute_modifier_upgrade_should_activate(row, policy)


func _attribute_modifier_upgrade_key(policy: Dictionary) -> String:
	return _contracts_subsystem()._attribute_modifier_upgrade_key(policy)


func _reconcile_attribute_modifier_upgrades(row: Dictionary) -> void:
	_contracts_subsystem()._reconcile_attribute_modifier_upgrades(row)


func _step_attribute_modifier_upgrades() -> void:
	_contracts_subsystem()._step_attribute_modifier_upgrades()


func _attach_geometry_upgrade_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_geometry_upgrade_contract(row, contract)


func _geometry_upgrade_should_activate(row: Dictionary, policy: Dictionary) -> bool:
	return _contracts_subsystem()._geometry_upgrade_should_activate(row, policy)


func _reconcile_geometry_upgrades(row: Dictionary) -> void:
	_contracts_subsystem()._reconcile_geometry_upgrades(row)


func _step_geometry_upgrades() -> void:
	_contracts_subsystem()._step_geometry_upgrades()


func _emotion_expression_value(fields: Dictionary, key: String, unsupported: Array[String]) -> float:
	return _contracts_subsystem()._emotion_expression_value(fields, key, unsupported)


func _attach_emotion_tracker_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_emotion_tracker_contract(row, contract)


func trigger_entity_emotion(entity_id: int, emotion_name: String, duration_ticks: int = -1) -> Dictionary:
	return _contracts_subsystem().trigger_entity_emotion(entity_id, emotion_name, duration_ticks)


func _step_emotion_trackers() -> void:
	_contracts_subsystem()._step_emotion_trackers()


func _attach_castle_member_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_castle_member_contract(row, contract)


func _dispatch_castle_member_destroyed(structure_id: int, member: Dictionary, attacker_id: int, reason: String) -> void:
	_contracts_subsystem()._dispatch_castle_member_destroyed(structure_id, member, attacker_id, reason)


func _attach_inactive_body_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_inactive_body_contract(row, contract)


func _attach_squish_collide_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_squish_collide_contract(row, contract)


func _squish_collision_admitted(victim: Dictionary) -> bool:
	return _contracts_subsystem()._squish_collision_admitted(victim)


func _attach_horde_member_collide_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_horde_member_collide_contract(row, contract)


func _attach_notify_crushing_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_notify_crushing_contract(row, contract)


func _attach_flammable_update_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_flammable_update_contract(row, contract)


func record_flame_damage(object_id: int, amount: float) -> Dictionary:
	return _contracts_subsystem().record_flame_damage(object_id, amount)


func _step_flammable_updates() -> void:
	_contracts_subsystem()._step_flammable_updates()


func _set_row_object_status(row: Dictionary, status: String, enabled: bool) -> void:
	_contracts_subsystem()._set_row_object_status(row, status, enabled)


func _attach_dynamic_portal_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_dynamic_portal_contract(row, contract)


func _step_dynamic_portals() -> void:
	_contracts_subsystem()._step_dynamic_portals()


func request_dynamic_portal_route(portal_id: int, entity_id: int, from_index: int, to_index: int) -> Dictionary:
	return _contracts_subsystem().request_dynamic_portal_route(portal_id, entity_id, from_index, to_index)


func _attach_foundation_ai_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_foundation_ai_contract(row, contract)


func foundation_build_variation(structure_id: int) -> Dictionary:
	return _contracts_subsystem().foundation_build_variation(structure_id)


func _attach_dual_weapon_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_dual_weapon_contract(row, contract)


func _attach_refund_die_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_refund_die_contract(row, contract)


func _cache_refund_die_build_cost(row: Dictionary) -> void:
	_contracts_subsystem()._cache_refund_die_build_cost(row)


func _attach_wall_hub_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_wall_hub_contract(row, contract)


func _wall_hub_distance(field: Dictionary, defines: Dictionary) -> Dictionary:
	return _contracts_subsystem()._wall_hub_distance(field, defines)


func request_wall_hub_plan(structure_id: int, option: String, endpoint: Vector2) -> Dictionary:
	return _contracts_subsystem().request_wall_hub_plan(structure_id, option, endpoint)


func _attach_attach_update_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_attach_update_contract(row, contract)


func request_attach_update(child_id: int, parent_id: int, parent_kind: String = "entity") -> Dictionary:
	return _contracts_subsystem().request_attach_update(child_id, parent_id, parent_kind)


func _step_attach_updates() -> void:
	_contracts_subsystem()._step_attach_updates()


func _detach_attach_update(child: Dictionary, reason: String) -> void:
	_contracts_subsystem()._detach_attach_update(child, reason)


func _attach_filter_probe(row: Dictionary, kind: String) -> Dictionary:
	return _contracts_subsystem()._attach_filter_probe(row, kind)


func _set_attach_parent_status(parent: Dictionary, child_id: int, statuses: Array, enabled: bool) -> void:
	_contracts_subsystem()._set_attach_parent_status(parent, child_id, statuses, enabled)


func _attach_monitor_condition_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_monitor_condition_contract(row, contract)


func _monitor_condition_route(value: Variant) -> Dictionary:
	return _contracts_subsystem()._monitor_condition_route(value)


func _step_monitor_condition_updates() -> void:
	_contracts_subsystem()._step_monitor_condition_updates()


func _step_monitor_condition_row(object_id: int, row: Dictionary) -> void:
	_contracts_subsystem()._step_monitor_condition_row(object_id, row)


func _upper_token_set(value: Variant) -> Dictionary:
	return _contracts_subsystem()._upper_token_set(value)


func _monitor_flags_match(active: Dictionary, required: Array) -> bool:
	return _contracts_subsystem()._monitor_flags_match(active, required)


func set_entity_upgrade_state(entity_id: int, upgrade_id: String, installed: bool) -> Dictionary:
	return _contracts_subsystem().set_entity_upgrade_state(entity_id, upgrade_id, installed)


func set_team_upgrade_state(team: int, upgrade_id: String, installed: bool) -> Dictionary:
	return _contracts_subsystem().set_team_upgrade_state(team, upgrade_id, installed)


func _step_object_creation_upgrades()->void:
	_contracts_subsystem()._step_object_creation_upgrades()


func _consume_object_creation_upgrade(owner_id:int,owner:Dictionary,policy:Dictionary)->void:
	_contracts_subsystem()._consume_object_creation_upgrade(owner_id, owner, policy)


func _attach_replace_self_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_replace_self_contract(row, contract)


func apply_replace_self_upgrade(structure_id: int, trigger_upgrade_id: String) -> Dictionary:
	return _contracts_subsystem().apply_replace_self_upgrade(structure_id, trigger_upgrade_id)


func _step_replace_self_upgrades() -> void:
	_contracts_subsystem()._step_replace_self_upgrades()


func _apply_due_replace_self_policy(structure_id: int, row: Dictionary, trigger_upgrade_id: String) -> Dictionary:
	return _contracts_subsystem()._apply_due_replace_self_policy(structure_id, row, trigger_upgrade_id)


func _replace_self_structure_spec(team: int, object_id: String) -> Dictionary:
	return _contracts_subsystem()._replace_self_structure_spec(team, object_id)


func _playable_structure_runtime_document(object_id: String) -> Dictionary:
	return _contracts_subsystem()._playable_structure_runtime_document(object_id)


func _structure_attack_from_combat(combat: Dictionary) -> Dictionary:
	return _contracts_subsystem()._structure_attack_from_combat(combat)


func _replacement_structure_row(structure_id: int, previous: Dictionary, object_id: String, spec: Dictionary, preserve_state: bool) -> Dictionary:
	return _contracts_subsystem()._replacement_structure_row(structure_id, previous, object_id, spec, preserve_state)


func _reconcile_replacement_containment(structure_id: int, replacement: Dictionary) -> void:
	_contracts_subsystem()._reconcile_replacement_containment(structure_id, replacement)


func _replace_self_receipt(policy: Dictionary, receipt: String) -> void:
	_contracts_subsystem()._replace_self_receipt(policy, receipt)


func _attach_citadel_slaughter_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_citadel_slaughter_contract(row, contract)


func enter_citadel_slaughter(structure_id: int, entity_id: int) -> Dictionary:
	return _contracts_subsystem().enter_citadel_slaughter(structure_id, entity_id)


func _consume_citadel_ring_entry(structure_id: int, entity_id: int, citadel: Dictionary, passenger: Dictionary, policy: Dictionary) -> Dictionary:
	return _contracts_subsystem()._consume_citadel_ring_entry(structure_id, entity_id, citadel, passenger, policy)


func _resolve_citadel_slaughter_death(structure_id: int, citadel: Dictionary) -> void:
	_contracts_subsystem()._resolve_citadel_slaughter_death(structure_id, citadel)


func _attach_ocl_update_contract(row:Dictionary,contract:Dictionary)->void:
	_contracts_subsystem()._attach_ocl_update_contract(row, contract)


func _step_ocl_updates()->void:
	_contracts_subsystem()._step_ocl_updates()


func _attach_stances_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_stances_contract(row, contract)


func set_entity_stance(entity_id: int, stance: String) -> Dictionary:
	return _contracts_subsystem().set_entity_stance(entity_id, stance)


func toggle_entity_stance(entity_id: int) -> Dictionary:
	return _contracts_subsystem().toggle_entity_stance(entity_id)


func _normalize_stance_name(stance: String) -> String:
	return _contracts_subsystem()._normalize_stance_name(stance)


func _apply_stance_modifier(row: Dictionary) -> void:
	_contracts_subsystem()._apply_stance_modifier(row)


func _attach_attribute_modifier_aura_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_attribute_modifier_aura_contract(row, contract)


func _resolve_lifetime_bound(field: Variant) -> Dictionary:
	return _contracts_subsystem()._resolve_lifetime_bound(field)


func _attach_lifetime_update_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_lifetime_update_contract(row, contract)


func _arm_lifetime(row: Dictionary) -> bool:
	return _contracts_subsystem()._arm_lifetime(row)


func wake_lifetime(object_id: int, object_kind: String = "entity") -> Dictionary:
	return _contracts_subsystem().wake_lifetime(object_id, object_kind)


func _step_lifetime_updates() -> void:
	_contracts_subsystem()._step_lifetime_updates()


func _expire_lifetime_entity(entity_id: int, row: Dictionary, death_type: String) -> void:
	_contracts_subsystem()._expire_lifetime_entity(entity_id, row, death_type)


func _expire_lifetime_structure(structure_id: int, row: Dictionary, death_type: String) -> void:
	_contracts_subsystem()._expire_lifetime_structure(structure_id, row, death_type)


func _step_attribute_modifier_auras() -> void:
	_contracts_subsystem()._step_attribute_modifier_auras()


func _step_attribute_modifier_aura_source(source: Dictionary) -> void:
	_contracts_subsystem()._step_attribute_modifier_aura_source(source)


func _aura_source_active(source: Dictionary, rule: Dictionary) -> bool:
	return _contracts_subsystem()._aura_source_active(source, rule)


func _aura_has_upgrade(upgrades: Array, applied: Dictionary, sought: String) -> bool:
	return _contracts_subsystem()._aura_has_upgrade(upgrades, applied, sought)


func _apply_typed_aura_to_target(source_id: int, target: Dictionary, rule: Dictionary, modifier: Dictionary, refresh_ticks: int) -> void:
	_contracts_subsystem()._apply_typed_aura_to_target(source_id, target, rule, modifier, refresh_ticks)


func _attach_model_condition_sound_selector(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_model_condition_sound_selector(row, contract)


func _attach_random_sound_selector(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_random_sound_selector(row, contract)


func _attach_upgrade_sound_selector(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_upgrade_sound_selector(row, contract)


func _attach_large_group_audio_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_large_group_audio_contract(row, contract)


func emit_typed_audio_intent(entity_id: int, sound_role: String) -> Dictionary:
	return _contracts_subsystem().emit_typed_audio_intent(entity_id, sound_role)


func emit_large_group_audio_intent(entity_ids_value: Array, sound_role: String) -> Dictionary:
	return _contracts_subsystem().emit_large_group_audio_intent(entity_ids_value, sound_role)


func _audio_sequence_roll(entity_id: int, role: String) -> float:
	return _contracts_subsystem()._audio_sequence_roll(entity_id, role)


func _attach_fire_spread_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_fire_spread_contract(row, contract)


func set_fire_spread_active(object_id: int, active: bool) -> Dictionary:
	return _contracts_subsystem().set_fire_spread_active(object_id, active)


func _fire_spread_delay(object_id: int, policy: Dictionary) -> int:
	return _contracts_subsystem()._fire_spread_delay(object_id, policy)


func _step_fire_spread_updates() -> void:
	_contracts_subsystem()._step_fire_spread_updates()


func _audio_active_conditions(row: Dictionary) -> Dictionary:
	return _contracts_subsystem()._audio_active_conditions(row)


func _audio_sound_field(role: String) -> String:
	return _contracts_subsystem()._audio_sound_field(role)


func _attach_radiate_fear_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_radiate_fear_contract(row, contract)


func activate_radiate_fear(entity_id: int, special_power: int) -> Dictionary:
	return _contracts_subsystem().activate_radiate_fear(entity_id, special_power)


func _step_radiate_fear_updates() -> void:
	_contracts_subsystem()._step_radiate_fear_updates()


func _fear_victim_filter_accepts(target: Dictionary, tokens: Array) -> bool:
	return _contracts_subsystem()._fear_victim_filter_accepts(target, tokens)


func _attach_poisoned_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_poisoned_contract(row, contract)


func apply_poison(entity_id: int, damage_per_pulse: float) -> Dictionary:
	return _contracts_subsystem().apply_poison(entity_id, damage_per_pulse)


func _step_poisoned_behaviors() -> void:
	_contracts_subsystem()._step_poisoned_behaviors()


func _attach_damage_field_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_damage_field_contract(row, contract)


func _step_damage_fields() -> void:
	_contracts_subsystem()._step_damage_fields()


func _attach_spawn_unit_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_spawn_unit_contract(row, contract)


func request_spawn_unit_command(owner_id: int, command: String) -> Dictionary:
	return _contracts_subsystem().request_spawn_unit_command(owner_id, command)


func _step_spawn_unit_behaviors() -> void:
	_contracts_subsystem()._step_spawn_unit_behaviors()


func _attach_hit_reaction_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_hit_reaction_contract(row, contract)


func record_hit_reaction(entity_id: int, damage: float) -> Dictionary:
	return _contracts_subsystem().record_hit_reaction(entity_id, damage)


func _step_hit_reactions() -> void:
	_contracts_subsystem()._step_hit_reactions()


func _attach_animal_ai_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_animal_ai_contract(row, contract)


func _step_animal_ai_updates() -> void:
	_contracts_subsystem()._step_animal_ai_updates()


func _attach_threat_finder_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_threat_finder_contract(row, contract)


func _step_threat_finders() -> void:
	_contracts_subsystem()._step_threat_finders()


func _nearest_hostile_entity(source_id: int, radius_source: float) -> int:
	return _contracts_subsystem()._nearest_hostile_entity(source_id, radius_source)


func _attach_large_group_bonus_contract(row: Dictionary, contract: Dictionary) -> void:
	_contracts_subsystem()._attach_large_group_bonus_contract(row, contract)


func _step_large_group_bonus_updates() -> void:
	_contracts_subsystem()._step_large_group_bonus_updates()


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


func spawn_physics_object(
	source_object_id: String,
	position: Vector2,
	height_source: float,
	horizontal_velocity: Vector2,
	vertical_velocity_source: float,
	contract: Dictionary
) -> int:
	## Materialize one explicitly thrown/knocked-back body from the typed
	## importer contract. Opaque legacy rows fail closed; this runtime must not
	## reinterpret authored strings or silently invent fields.
	if String(contract.get("module", "")) != "PhysicsBehavior":
		return -1
	if String(contract.get("extraction", "")) != "typed":
		return -1
	var fields: Dictionary = contract.get("fields", {}) as Dictionary
	var gravity_value: Variant = _module_contract_value(fields, "GravityMult", 1.0)
	var first_height_value: Variant = _module_contract_value(fields, "FirstHeight", 0.0)
	var second_height_value: Variant = _module_contract_value(fields, "SecondHeight", 0.0)
	var bounce_value: Variant = _module_contract_value(fields, "AllowBouncing", false)
	var orient_value: Variant = _module_contract_value(fields, "OrientToFlightPath", false)
	var kill_value: Variant = _module_contract_value(fields, "KillWhenRestingOnGround", false)
	var low_value: Variant = _module_contract_value(fields, "ShockStunnedTimeLow", 0.0)
	var high_value: Variant = _module_contract_value(fields, "ShockStunnedTimeHigh", 0.0)
	var standing_value: Variant = _module_contract_value(fields, "ShockStandingTime", 0.0)
	for number_value in [gravity_value, first_height_value, second_height_value, low_value, high_value, standing_value]:
		if typeof(number_value) not in [TYPE_INT, TYPE_FLOAT]:
			return -1
	if typeof(bounce_value) != TYPE_BOOL or typeof(orient_value) != TYPE_BOOL or typeof(kill_value) != TYPE_BOOL:
		return -1
	if float(gravity_value) < 0.0 or float(first_height_value) < 0.0 or float(second_height_value) < 0.0:
		return -1
	if float(low_value) < 0.0 or float(high_value) < 0.0 or float(standing_value) < 0.0:
		return -1
	var low_ms := mini(roundi(float(low_value)), roundi(float(high_value)))
	var high_ms := maxi(roundi(float(low_value)), roundi(float(high_value)))
	var unsupported: Array[String] = []
	if bool(bounce_value) and float(first_height_value) <= 0.0 and float(second_height_value) <= 0.0:
		# The typed descriptor does not carry collision restitution. Retail rows
		# that only say AllowBouncing therefore remain explicitly unresolved at
		# the impact boundary instead of acquiring an invented coefficient.
		unsupported.append("bounce_restitution_without_authored_heights")
	var id := _next_physics_object_id
	_next_physics_object_id += 1
	physics_objects[id] = {
		"id": id,
		"source_object_id": source_object_id,
		"position": position,
		"height_source": maxf(0.0, height_source),
		"horizontal_velocity": horizontal_velocity,
		"vertical_velocity_source": vertical_velocity_source,
		"gravity_multiplier": float(gravity_value),
		"allow_bouncing": bool(bounce_value),
		"orient_to_flight_path": bool(orient_value),
		"kill_when_resting_on_ground": bool(kill_value),
		"first_height_source": float(first_height_value),
		"second_height_source": float(second_height_value),
		"shock_stunned_low_ms": low_ms,
		"shock_stunned_high_ms": high_ms,
		"shock_standing_ms": roundi(float(standing_value)),
		"bounce_count": 0,
		"phase": "airborne",
		"phase_ticks_remaining": 0,
		"yaw_radians": 0.0,
		"pitch_radians": 0.0,
		"unsupported_semantics": unsupported,
		"contract_tag": String(contract.get("tag", "")),
		"contract_line": int(contract.get("line", 0)),
	}
	return id


func _step_physics_objects() -> void:
	if physics_objects.is_empty():
		return
	var ids := physics_objects.keys()
	ids.sort()
	var gravity_source := maxf(0.0, float(_rules.get("physics_gravity_source_per_second_squared", 0.0)))
	for id_value in ids:
		var id := int(id_value)
		if not physics_objects.has(id):
			continue
		var row := physics_objects[id] as Dictionary
		match String(row.get("phase", "airborne")):
			"airborne":
				_step_airborne_physics_object(id, row, gravity_source)
			"shock_stunned":
				_step_physics_recovery_phase(row, "shock_standing")
			"shock_standing":
				_step_physics_recovery_phase(row, "recovered")


func _step_projectiles() -> void:
	_projectiles_subsystem().step()


func _resolve_member_projectile_impact(projectile_id: int, projectile: Dictionary) -> void:
	_projectiles_subsystem().resolve_member_projectile_impact(projectile_id, projectile)


func _radius_relation_allowed(attacker_team: int, victim_team: int, affects: String) -> bool:
	return _projectiles_subsystem().radius_relation_allowed(attacker_team, victim_team, affects)


func _tapered_radius_amount(amount: float, distance: float, radius: float, taper_off: float) -> int:
	return _projectiles_subsystem().tapered_radius_amount(amount, distance, radius, taper_off)


func _apply_radius_damage(
	attacker_id: int,
	origin: Vector2,
	radius: float,
	amount: float,
	damage_type: String,
	taper_off: float,
	affects: String,
	exclude_target_id: int,
	death_type: String = "NORMAL"
) -> void:
	_projectiles_subsystem().apply_radius_damage(attacker_id, origin, radius, amount, damage_type, taper_off, affects, exclude_target_id, death_type)


func _step_airborne_physics_object(id: int, row: Dictionary, gravity_source: float) -> void:
	row["position"] = Vector2(row.get("position", Vector2.ZERO)) + Vector2(row.get("horizontal_velocity", Vector2.ZERO)) * TICK_SECONDS
	var vertical_velocity := float(row.get("vertical_velocity_source", 0.0))
	vertical_velocity -= gravity_source * float(row.get("gravity_multiplier", 1.0)) * TICK_SECONDS
	row["vertical_velocity_source"] = vertical_velocity
	row["height_source"] = float(row.get("height_source", 0.0)) + vertical_velocity * TICK_SECONDS
	if bool(row.get("orient_to_flight_path", false)):
		var horizontal := Vector2(row.get("horizontal_velocity", Vector2.ZERO))
		if not horizontal.is_zero_approx():
			row["yaw_radians"] = horizontal.angle()
		row["pitch_radians"] = atan2(vertical_velocity, horizontal.length())
	if float(row.get("height_source", 0.0)) > 0.0:
		return
	row["height_source"] = 0.0
	var bounce_count := int(row.get("bounce_count", 0))
	var rebound_height := 0.0
	if bool(row.get("allow_bouncing", false)):
		if bounce_count == 0:
			rebound_height = float(row.get("first_height_source", 0.0))
		elif bounce_count == 1:
			rebound_height = float(row.get("second_height_source", 0.0))
	if rebound_height > 0.0 and gravity_source * float(row.get("gravity_multiplier", 1.0)) > 0.0:
		row["bounce_count"] = bounce_count + 1
		row["vertical_velocity_source"] = sqrt(2.0 * gravity_source * float(row.get("gravity_multiplier", 1.0)) * rebound_height)
		return
	row["horizontal_velocity"] = Vector2.ZERO
	row["vertical_velocity_source"] = 0.0
	if not (row.get("landing_warhead", {}) as Dictionary).is_empty():
		_resolve_fling_landing(row)
	if bool(row.get("kill_when_resting_on_ground", false)):
		physics_objects.erase(id)
		return
	_begin_physics_recovery(row)


func _resolve_fling_landing(projectile: Dictionary) -> void:
	var warhead := projectile.get("landing_warhead", {}) as Dictionary
	var point := Vector2(projectile.get("position", Vector2.ZERO))
	var radius := float(warhead.get("radius_scaled", 0.0))
	var force_filter: Array = String(warhead.get("forceKillObjectFilter", "")).split(" ", false)
	var affected: Array[int] = []
	for target_id in entity_ids():
		var target := entities[target_id] as Dictionary
		if int(target.get("health", 0)) <= 0 or Vector2(target.get("position", Vector2.ZERO)).distance_to(point) > radius + 0.000001:
			continue
		if not _transport_filter_accepts(target, force_filter):
			continue
		_apply_damage(int(projectile.get("fling_attacker_id", 0)), target_id, 2147483647, "battalion", String(warhead.get("deathType", "NORMAL")), String(warhead.get("damageType", "")))
		affected.append(target_id)
	_emit_event("ability.fling_landed", int(projectile.get("fling_attacker_id", 0)), 0, {"warhead_id": String(warhead.get("id", "")), "damage_type": String(warhead.get("damageType", "")), "death_type": String(warhead.get("deathType", "")), "affected_ids": affected, "special_filter_without_damage_amount": String(warhead.get("specialObjectFilter", ""))})


func _begin_physics_recovery(row: Dictionary) -> void:
	var low_ms := int(row.get("shock_stunned_low_ms", 0))
	var high_ms := int(row.get("shock_stunned_high_ms", 0))
	if high_ms > 0:
		var stunned_ms := logic_random_int(low_ms, high_ms)
		row["phase"] = "shock_stunned"
		row["phase_ticks_remaining"] = maxi(1, ceili(float(stunned_ms) / (TICK_SECONDS * 1000.0)))
		return
	var standing_ms := int(row.get("shock_standing_ms", 0))
	if standing_ms > 0:
		row["phase"] = "shock_standing"
		row["phase_ticks_remaining"] = maxi(1, ceili(float(standing_ms) / (TICK_SECONDS * 1000.0)))
		return
	row["phase"] = "recovered"
	row["phase_ticks_remaining"] = 0


func _step_physics_recovery_phase(row: Dictionary, next_phase: String) -> void:
	var remaining := int(row.get("phase_ticks_remaining", 0)) - 1
	if remaining > 0:
		row["phase_ticks_remaining"] = remaining
		return
	if next_phase == "shock_standing":
		var standing_ms := int(row.get("shock_standing_ms", 0))
		if standing_ms > 0:
			row["phase"] = "shock_standing"
			row["phase_ticks_remaining"] = maxi(1, ceili(float(standing_ms) / (TICK_SECONDS * 1000.0)))
			return
	row["phase"] = "recovered"
	row["phase_ticks_remaining"] = 0


func _attach_fire_weapon_when_dead_contract(row: Dictionary, contract: Dictionary) -> void:
	## Normalize the typed importer row once at materialization. Opaque rows from
	## older packs fail closed because their timer/offset/status values are not
	## typed; they remain in module_contracts as evidence.
	if String(contract.get("extraction", "")) != "typed":
		return
	var fields: Dictionary = contract.get("fields", {}) as Dictionary
	var starts_value: Variant = _module_contract_value(fields, "StartsActive", null)
	if typeof(starts_value) != TYPE_BOOL or not bool(starts_value):
		return
	var weapon_value: Variant = _module_contract_value(fields, "DeathWeapon", "")
	var weapon_id := String(weapon_value).strip_edges()
	if weapon_id == "":
		return
	var delay_value: Variant = _module_contract_value(fields, "DelayTime", 0.0)
	if typeof(delay_value) not in [TYPE_INT, TYPE_FLOAT]:
		return
	var offset := Vector2.ZERO
	var offset_z := 0.0
	var offset_value: Variant = _module_contract_value(fields, "WeaponOffset", {})
	if typeof(offset_value) == TYPE_DICTIONARY:
		var coordinates := offset_value as Dictionary
		if (
			typeof(coordinates.get("x", 0.0)) not in [TYPE_INT, TYPE_FLOAT]
			or typeof(coordinates.get("y", 0.0)) not in [TYPE_INT, TYPE_FLOAT]
		):
			return
		offset = Vector2(float(coordinates.get("x", 0.0)), float(coordinates.get("y", 0.0)))
		if typeof(coordinates.get("z", 0.0)) not in [TYPE_INT, TYPE_FLOAT]:
			return
		offset_z = float(coordinates.get("z", 0.0))
	var rows: Array = row.get("fire_weapon_when_dead", []) as Array
	rows.append({
		"death_types": String(fields.get("deathTypes", "ALL")).to_upper(),
		"included_death_types": Array(fields.get("includedDeathTypes", [])).duplicate(),
		"excluded_death_types": Array(fields.get("excludedDeathTypes", [])).duplicate(),
		"required_status": Array(_module_contract_value(fields, "RequiredStatus", [])).duplicate(),
		"exempt_status": Array(_module_contract_value(fields, "ExemptStatus", [])).duplicate(),
		"active_during_construction": bool(_module_contract_value(fields, "ActiveDuringConstruction", false)),
		"delay_ticks": maxi(0, roundi(float(delay_value) / (TICK_SECONDS * 1000.0))),
		"death_weapon": weapon_id,
		"weapon_offset_source": offset,
		"weapon_offset_z_source": offset_z,
		"tag": String(contract.get("tag", "")),
		"source_ini": String(contract.get("source_ini", contract.get("sourceIni", ""))),
		"line": int(contract.get("line", 0)),
	})
	row["fire_weapon_when_dead"] = rows


func register_death_weapon_rule(weapon_id: String, rule: Dictionary) -> bool:
	## Closed payload consumed when a scheduled DeathWeapon fires. This does not
	## invent data for a named-but-unconverted weapon: absent ids still produce a
	## deterministic unresolved event and no damage.
	var id := weapon_id.strip_edges()
	if id == "":
		return false
	for key in ["damage", "radius_source"]:
		if typeof(rule.get(key)) not in [TYPE_INT, TYPE_FLOAT] or float(rule.get(key)) < 0.0:
			return false
	var normalized := rule.duplicate(true)
	normalized["damage"] = float(rule.get("damage"))
	normalized["radius_source"] = float(rule.get("radius_source"))
	normalized["damage_type"] = String(rule.get("damage_type", ""))
	normalized["affects"] = String(rule.get("affects", "ENEMIES"))
	_death_weapon_rules[id] = normalized
	return true


func _configure_death_weapon_rules_from_rules() -> void:
	_death_weapon_rules.clear()
	var configured: Variant = _rules.get("death_weapon_rules", {})
	if typeof(configured) != TYPE_DICTIONARY:
		return
	var ids := (configured as Dictionary).keys()
	ids.sort()
	for id_value in ids:
		var rule_value: Variant = (configured as Dictionary).get(id_value)
		if typeof(rule_value) == TYPE_DICTIONARY:
			register_death_weapon_rule(String(id_value), rule_value as Dictionary)


func _passive_area_effect_number(fields: Dictionary, key: String) -> float:
	return _contracts_subsystem()._passive_area_effect_number(fields, key)


func _passive_area_effect_percent(text: String) -> float:
	return _contracts_subsystem()._passive_area_effect_percent(text)


func _passive_area_effect_yes(fields: Dictionary, key: String) -> bool:
	return _contracts_subsystem()._passive_area_effect_yes(fields, key)


func _step_passive_area_effect_heals() -> void:
	## PassiveAreaEffectBehavior's healing branch. Retail wells/fortress healing
	## author a periodic percent-of-member-max heal, a radius, an object filter,
	## optional upgrade gate, and NonStackable. Dead members are not revived.
	## ModifierName leadership rows are deliberately not handled here: they need
	## their referenced ModifierList resolved into the shared modifier core.
	var candidates: Dictionary = {}
	var scale := float(_rules.get("source_map_transform_scale", 0.0))
	if scale <= 0.0:
		scale = 1.0
	for structure_id in structure_ids():
		var structure: Dictionary = structures[structure_id]
		if not bool(structure.get("structure_module_contracts_attached", false)):
			_attach_structure_module_contracts(structure)
		if (
			int(structure.get("health", 0)) <= 0
			or float(structure.get("construction_progress", 1.0)) < 1.0
		):
			continue
		var team := int(structure.get("team", -1))
		if team < 0:
			continue
		var rules: Array = structure.get("passive_area_effect_heals", []) as Array
		for rule_index in rules.size():
			var rule: Dictionary = rules[rule_index]
			var upgrade_required := String(rule.get("upgrade_required", ""))
			if upgrade_required != "" and not _structure_has_completed_upgrade(
				structure, upgrade_required
			):
				continue
			var next_ping := int(rule.get("next_ping_tick", tick_index + 1))
			var ping_ticks := maxi(1, int(rule.get("ping_ticks", 1)))
			var due := tick_index >= next_ping
			if due:
				# Preserve cadence even if a test/operator advances this subsystem after
				# its deadline; normal gameplay calls it exactly once per sim tick.
				while next_ping <= tick_index:
					next_ping += ping_ticks
				rule["next_ping_tick"] = next_ping
				rules[rule_index] = rule
			var radius := float(rule.get("radius_source", 0.0)) * scale
			var rate := float(rule.get("heal_fraction_per_second", 0.0))
			if radius <= 0.0 or rate <= 0.0:
				continue
			var origin := Vector2(structure.get("position", Vector2.ZERO))
			var filter_text := String(rule.get("allow_filter", ""))
			for entity_id in living_ids(team):
				var target: Dictionary = entities[entity_id]
				if not _ability_filter_accepts(target, filter_text):
					continue
				if Vector2(target.get("position", Vector2.ZERO)).distance_to(origin) > radius:
					continue
				if int(target.get("health", 0)) >= int(target.get("maximum_health", 0)):
					continue
				var raw_amount := (
					float(target.get("member_maximum_health", 0))
					* rate
					* float(ping_ticks)
					* TICK_SECONDS
				)
				if raw_amount <= 0.0:
					continue
				var target_candidates: Array = candidates.get(entity_id, []) as Array
				target_candidates.append({
					"raw_amount": raw_amount,
					"strength": rate,
					"source_id": structure_id,
					"due": due,
					"remainder_key": (
						"nonstackable"
						if bool(rule.get("non_stackable", false))
						else "%d:%s" % [structure_id, String(rule.get("tag", rule_index))]
					),
					"non_stackable": bool(rule.get("non_stackable", false)),
				})
				candidates[entity_id] = target_candidates
		if rules.is_empty():
			structure.erase("passive_area_effect_heals")
		else:
			structure["passive_area_effect_heals"] = rules
	for entity_id_value in candidates.keys():
		var entity_id := int(entity_id_value)
		if not entities.has(entity_id):
			continue
		var stackable: Array = []
		var best_nonstackable: Dictionary = {}
		for candidate_value in candidates[entity_id] as Array:
			var candidate := candidate_value as Dictionary
			if bool(candidate.get("non_stackable", false)):
				if (
					best_nonstackable.is_empty()
					or float(candidate.get("strength", 0.0))
						> float(best_nonstackable.get("strength", 0.0))
					or (
						is_equal_approx(
							float(candidate.get("strength", 0.0)),
							float(best_nonstackable.get("strength", 0.0))
						)
						and int(candidate.get("source_id", 0))
							< int(best_nonstackable.get("source_id", 0))
					)
				):
					best_nonstackable = candidate
			elif bool(candidate.get("due", false)):
				stackable.append(candidate)
		if not best_nonstackable.is_empty() and bool(best_nonstackable.get("due", false)):
			stackable.append(best_nonstackable)
		for candidate_value in stackable:
			_apply_passive_area_effect_heal(
				entities[entity_id] as Dictionary, candidate_value as Dictionary
			)


func _step_passive_area_effect_modifiers() -> void:
	## Statue/heroic-statue leadership branch. The importer resolves ModifierName
	## into typed ModifierList effects, duration, category and stacking policy;
	## refresh those effects through the shared timed-modifier core.
	var scale := float(_rules.get("source_map_transform_scale", 0.0))
	if scale <= 0.0:
		scale = 1.0
	for structure_id in structure_ids():
		var structure: Dictionary = structures[structure_id]
		if not bool(structure.get("structure_module_contracts_attached", false)):
			_attach_structure_module_contracts(structure)
		if int(structure.get("health", 0)) <= 0 or float(structure.get("construction_progress", 1.0)) < 1.0:
			continue
		var team := int(structure.get("team", -1))
		if team < 0:
			continue
		var rules: Array = structure.get("passive_area_effect_modifiers", []) as Array
		for rule_index in rules.size():
			var rule: Dictionary = rules[rule_index]
			var upgrade_required := String(rule.get("upgrade_required", ""))
			if upgrade_required != "" and not _structure_has_completed_upgrade(structure, upgrade_required):
				continue
			var next_ping := int(rule.get("next_ping_tick", tick_index))
			if tick_index < next_ping:
				continue
			var ping_ticks := maxi(1, int(rule.get("ping_ticks", 1)))
			while next_ping <= tick_index:
				next_ping += ping_ticks
			rule["next_ping_tick"] = next_ping
			rules[rule_index] = rule
			var radius := float(rule.get("radius_source", 0.0)) * scale
			var duration_ticks := maxi(1, int(rule.get("duration_ticks", 1)))
			var category := String(rule.get("category", ""))
			var modifier_id := String(rule.get("id", ""))
			var stacking: Dictionary = rule.get("stacking", {}) as Dictionary
			var origin := Vector2(structure.get("position", Vector2.ZERO))
			for entity_id in living_ids(team):
				var target: Dictionary = entities[entity_id]
				if not _ability_filter_accepts(target, String(rule.get("allow_filter", ""))):
					continue
				if Vector2(target.get("position", Vector2.ZERO)).distance_to(origin) > radius:
					continue
				if bool(stacking.get("ignoreIfAnticategoryActive", false)) and category == "LEADERSHIP" and _refresh_leadership_suppression(target) > tick_index:
					continue
				var key := "passive:%s:%s" % [category, modifier_id]
				if bool(stacking.get("replaceInCategoryIfLongest", false)) or bool(rule.get("non_stackable", false)):
					key = "passive-category:%s" % category
					var current: Dictionary = (target.get("timed_modifiers", {}) as Dictionary).get(key, {}) as Dictionary
					if int(current.get("expires_tick", -1)) > tick_index + duration_ticks:
						continue
				_set_timed_modifier(target, key, rule.get("effects", []) as Array, tick_index + duration_ticks)
				_emit_event("module.passive_area_effect_modifier", structure_id, entity_id, {"modifier_id": modifier_id, "category": category})
		if rules.is_empty():
			structure.erase("passive_area_effect_modifiers")
		else:
			structure["passive_area_effect_modifiers"] = rules


static func _structure_has_completed_upgrade(structure: Dictionary, upgrade_id: String) -> bool:
	if (structure.get("completed_upgrades", []) as Array).has(upgrade_id):
		return true
	var applied: Variant = structure.get("applied_upgrades", {})
	return typeof(applied) == TYPE_DICTIONARY and (applied as Dictionary).has(upgrade_id)


func _apply_passive_area_effect_heal(target: Dictionary, candidate: Dictionary) -> void:
	var remainders: Dictionary = target.get("passive_area_heal_remainders", {}) as Dictionary
	var key := String(candidate.get("remainder_key", ""))
	var accumulated := float(remainders.get(key, 0.0)) + float(candidate.get("raw_amount", 0.0))
	# Percent text such as 2% and a 300 ms cadence has the exact rational
	# result 0.6, but binary float accumulation can land at 5.999999... on the
	# tenth ping. A tiny deterministic epsilon preserves the authored rational
	# boundary without ever promoting a materially sub-integer value.
	var amount := floori(accumulated + 0.000001)
	remainders[key] = maxf(0.0, accumulated - float(amount))
	target["passive_area_heal_remainders"] = remainders
	if amount <= 0:
		return
	var health_values: Array = target.get("member_health", []) as Array
	var member_maximum := int(target.get("member_maximum_health", 0))
	var remaining := amount
	for member_index in health_values.size():
		if remaining <= 0:
			break
		var current := int(health_values[member_index])
		if current <= 0 or current >= member_maximum:
			continue
		var healed := mini(remaining, member_maximum - current)
		health_values[member_index] = current + healed
		remaining -= healed
	target["member_health"] = health_values
	var aggregate := 0
	for value in health_values:
		aggregate += int(value)
	target["health"] = aggregate
	_emit_event("module.passive_area_effect_heal", int(candidate.get("source_id", 0)), int(target.get("id", 0)), {
		"amount": amount - remaining,
		"team": int(target.get("team", -1)),
	})


func _attach_experience_state(row: Dictionary) -> void:
	_experience_subsystem().attach_experience_state(row)


func _refresh_banner_carrier_state(row: Dictionary) -> void:
	## Retail BannerCarriersAllowed: once the horde reaches minLevel, keep one
	## linked banner entity (or re-spawn after authored BannerCarrierUpdate
	## timers). Presentation reads banner_carrier_spawned / object_id / offset.
	var rule: Dictionary = _unit_banner_carriers.get(String(row.get("unit_type", "")), {}) as Dictionary
	if rule.is_empty():
		rule = _unit_banner_carriers.get(String(row.get("object_id", "")), {}) as Dictionary
	if rule.is_empty():
		return
	row["banner_carrier_object_id"] = String(rule.get("object_id", ""))
	row["banner_carrier_offset_source"] = rule.get("offset_source", Vector2.ZERO)
	row["banner_carrier_destroy_horde_on_death"] = bool(rule.get("destroy_horde_on_death", false))
	if int(row.get("level", 1)) < int(rule.get("min_level", 2)):
		return
	# AllowBannerSpawnUpgrade is authored on fortress garrison expansions. It
	# gates only a horde currently contained by that expansion; uncontained
	# hordes and containers without the module retain their normal banner path.
	if entity_container.has(int(row.get("id", 0))):
		var container_id := int(entity_container[int(row.get("id", 0))])
		if structures.has(container_id) and not structure_allows_banner_spawn(container_id):
			return
	var banner_id := int(row.get("banner_entity_id", 0))
	if banner_id != 0 and entities.has(banner_id) and int((entities[banner_id] as Dictionary).get("health", 0)) > 0:
		_sync_banner_entity_transform(row, banner_id, rule)
		row["banner_carrier_spawned"] = true
		return
	var respawn_remaining := int(row.get("banner_respawn_ticks_remaining", -1))
	if respawn_remaining > 0:
		return
	_spawn_banner_carrier_entity(row, rule)


func _spawn_banner_carrier_entity(parent: Dictionary, rule: Dictionary) -> void:
	var team := int(parent.get("team", -1))
	if team < 0:
		return
	if not _next_dynamic_id.has(team):
		_next_dynamic_id[team] = 900000 + team * 1000
	var banner_object_id := String(rule.get("object_id", ""))
	var banner_max_health := int(rule.get("banner_max_health", 0))
	if banner_object_id == "" or banner_max_health <= 0:
		parent["banner_carrier_spawned"] = false
		parent["banner_carrier_error"] = "selected banner template is incomplete"
		return
	var entity_id := int(_next_dynamic_id[team])
	_next_dynamic_id[team] = entity_id + 1
	var offset_source: Vector2 = rule.get("offset_source", Vector2.ZERO)
	var at := Vector2(parent.get("position", Vector2.ZERO)) + _retail_source_to_sim_offset(offset_source)
	var unit_rules_value: Variant = _rules.get("unit_rules", {})
	var has_rule := (
		typeof(unit_rules_value) == TYPE_DICTIONARY
		and not ((unit_rules_value as Dictionary).get(banner_object_id, {}) as Dictionary).is_empty()
	)
	if has_rule:
		_add_battalion(
			entity_id,
			team,
			at,
			banner_object_id,
			banner_object_id,
			banner_object_id,
			0
		)
	else:
		# Banner runtimes may be non-producible (no UNIT_BUILD rule). Spawn a
		# minimal 1-member entity so death/respawn still execute fail-closed.
		entities[entity_id] = {
			"id": entity_id,
			"team": team,
			"name": banner_object_id,
			"object_id": banner_object_id,
			"unit_type": banner_object_id,
			"position": at,
			"facing": Vector2(parent.get("facing", Vector2.RIGHT)),
			"destination": at,
			"route": [],
			"route_cells": [],
			"state": "idle",
			"target_id": 0,
			"target_kind": "battalion",
			"health": banner_max_health,
			"maximum_health": banner_max_health,
			"member_maximum_health": banner_max_health,
			"member_health": [banner_max_health],
			"damage": 0,
			"member_damage": 0,
			"speed": 0.0,
			"speed_source": 0.0,
			"current_speed": 0.0,
			"command_points": 0,
			"level": 1,
			"order_kind": "",
			"is_builder": false,
			"formation_positions": [Vector3.ZERO],
			"formation_positions_base": [Vector3.ZERO],
			"timed_modifiers": {},
			"applied_upgrades": {},
		}
	if not entities.has(entity_id):
		return
	var banner: Dictionary = entities[entity_id]
	if not banner.has("module_contracts"):
		_attach_module_contracts(banner)
	banner["is_banner_carrier"] = true
	banner["banner_parent_entity_id"] = int(parent.get("id", 0))
	banner["command_points"] = 0
	banner["banner_carrier_object_id"] = banner_object_id
	# Linked carriers are not free-command battalions.
	banner["ignores_select_all"] = true
	_spatial_sync(banner)
	parent["banner_entity_id"] = entity_id
	parent["banner_carrier_spawned"] = true
	parent["banner_respawn_ticks_remaining"] = -1
	parent.erase("banner_respawn_armed_tick")
	_emit_event("battalion.banner_spawned", int(parent.get("id", 0)), entity_id, {
		"team": team,
		"banner_object_id": banner_object_id,
		"parent_unit_type": String(parent.get("unit_type", "")),
	})


func _sync_banner_entity_transform(parent: Dictionary, banner_id: int, rule: Dictionary) -> void:
	if not entities.has(banner_id):
		return
	var banner: Dictionary = entities[banner_id]
	var offset_source: Vector2 = rule.get("offset_source", Vector2.ZERO)
	banner["position"] = Vector2(parent.get("position", Vector2.ZERO)) + _retail_source_to_sim_offset(offset_source)
	banner["destination"] = banner["position"]
	banner["facing"] = Vector2(parent.get("facing", banner.get("facing", Vector2.RIGHT)))
	banner["team"] = int(parent.get("team", banner.get("team", -1)))
	_spatial_sync(banner)


func _step_banner_carriers() -> void:
	## Follow parents, count respawn timers, re-spawn when due.
	for id in entity_ids():
		var row: Dictionary = entities[id]
		if bool(row.get("is_banner_carrier", false)):
			_step_banner_replenishment(row)
			var parent_id := int(row.get("banner_parent_entity_id", 0))
			if int(row.get("health", 0)) <= 0:
				_on_banner_carrier_defeated(row)
				entities.erase(id)
				continue
			if parent_id == 0 or not entities.has(parent_id) or int((entities[parent_id] as Dictionary).get("health", 0)) <= 0:
				entities.erase(id)
				continue
			if parent_id != 0 and entities.has(parent_id):
				var parent: Dictionary = entities[parent_id]
				var rule: Dictionary = _unit_banner_carriers.get(String(parent.get("unit_type", "")), {}) as Dictionary
				if rule.is_empty():
					rule = _unit_banner_carriers.get(String(parent.get("object_id", "")), {}) as Dictionary
				if not rule.is_empty():
					_sync_banner_entity_transform(parent, id, rule)
			continue
		var remaining := int(row.get("banner_respawn_ticks_remaining", -1))
		if remaining < 0:
			# Keep living banner glued even when not a banner carrier itself.
			if int(row.get("banner_entity_id", 0)) != 0:
				_refresh_banner_carrier_state(row)
			continue
		if remaining > 0:
			if int(row.get("banner_respawn_armed_tick", -1)) == tick_index:
				continue
			row["banner_respawn_ticks_remaining"] = remaining - 1
			if int(row["banner_respawn_ticks_remaining"]) > 0:
				continue
		# remaining hit 0 — attempt respawn if level still qualifies.
		_refresh_banner_carrier_state(row)


func _on_banner_carrier_defeated(banner: Dictionary) -> void:
	## Mirror C# BannerCarrierModule: destroy horde when authored, else arm
	## authored respawn ticks (never invent a default timer).
	var parent_id := int(banner.get("banner_parent_entity_id", 0))
	if parent_id == 0 or not entities.has(parent_id):
		return
	var parent: Dictionary = entities[parent_id]
	if int(parent.get("banner_entity_id", 0)) == int(banner.get("id", 0)):
		parent["banner_entity_id"] = 0
	parent["banner_carrier_spawned"] = false
	if bool(parent.get("banner_carrier_destroy_horde_on_death", false)):
		# Lethal destroy of the owning horde (retail thrall-style contracts).
		parent["health"] = 0
		var member_health: Array = parent.get("member_health", []) as Array
		var defeated_members: Array[int] = []
		for member_index in member_health.size():
			if int(member_health[member_index]) > 0:
				defeated_members.append(member_index)
			member_health[member_index] = 0
		parent["member_health"] = member_health
		var death_policy := _bookkeep_battalion_death(
			parent_id, parent, "NORMAL", defeated_members
		)
		_emit_event("battalion.defeated", int(banner.get("id", 0)), parent_id, {
			"object_id": String(parent.get("object_id", "")),
			"team": int(parent.get("team", -1)),
			"category": String(parent.get("category", "")),
			"reason": "banner-carrier-destroy-horde-on-death",
		})
		if bool(death_policy.get("destroy_object", false)):
			entities.erase(parent_id)
		return
	var banner_object_id := String(banner.get("object_id", parent.get("banner_carrier_object_id", "")))
	var rule: Dictionary = _unit_banner_carriers.get(String(parent.get("unit_type", "")), {}) as Dictionary
	if rule.is_empty():
		rule = _unit_banner_carriers.get(String(parent.get("object_id", "")), {}) as Dictionary
	var respawn_ticks := int(rule.get("respawn_ticks", -1))
	var typed_update:=banner.get("banner_carrier_update",{}) as Dictionary
	if typed_update.is_empty():
		_attach_module_contracts(banner);typed_update=banner.get("banner_carrier_update",{}) as Dictionary
	if bool(typed_update.get("has_respawn_timer",false)):respawn_ticks=maxi(int(typed_update.get("died_respawn_ticks",0)),int(typed_update.get("melee_banner_respawn_ticks",0)))
	if _banner_respawn_ticks_by_object.has(banner_object_id):
		respawn_ticks = int(_banner_respawn_ticks_by_object[banner_object_id])
	# Fall back through retail source name on the rule.
	if respawn_ticks < 0 and not rule.is_empty():
		var source_name := String(rule.get("source_banner_object_id", ""))
		if source_name != "" and _banner_respawn_ticks_by_object.has(source_name):
			respawn_ticks = int(_banner_respawn_ticks_by_object[source_name])
		var rule_oid := String(rule.get("object_id", ""))
		if respawn_ticks < 0 and rule_oid != "" and _banner_respawn_ticks_by_object.has(rule_oid):
			respawn_ticks = int(_banner_respawn_ticks_by_object[rule_oid])
	if respawn_ticks >= 0:
		parent["banner_respawn_ticks_remaining"] = respawn_ticks
		parent["banner_respawn_armed_tick"] = tick_index
		_emit_event("battalion.banner_respawn_armed", parent_id, int(banner.get("id", 0)), {
			"respawn_ticks": respawn_ticks,
			"banner_object_id": banner_object_id,
		})
	else:
		parent["banner_respawn_ticks_remaining"] = -1
		parent.erase("banner_respawn_armed_tick")


func _step_banner_replenishment(banner:Dictionary)->void:
	var update:=banner.get("banner_carrier_update",{}) as Dictionary
	if update.is_empty():return
	if tick_index<int(update.get("next_replenish_tick",0)):return
	update["next_replenish_tick"]=tick_index+maxi(1,int(update.get("idle_spawn_ticks",1)));banner["banner_carrier_update"]=update
	var parent_id:=int(banner.get("banner_parent_entity_id",0));var required:=String(update.get("upgrade_required",""))
	if required!="" and (parent_id==0 or not entities.has(parent_id) or not _structure_has_completed_upgrade(entities[parent_id] as Dictionary,required)):return
	var candidates:Array[int]=[]
	if bool(update.get("replenish_nearby",false)) and parent_id!=0:candidates.append(parent_id)
	if bool(update.get("replenish_all",false)):
		var radius:=float(update.get("scan_range_source",0.0))*float(_rules.get("source_unit_scale",0.1));var origin:=Vector2(banner.get("position",Vector2.ZERO))
		for id in entity_ids():
			if id==int(banner.get("id",0)) or candidates.has(id):continue
			if int((entities[id] as Dictionary).get("team",-1))!=int(banner.get("team",-1)):continue
			if radius<=0.0 or origin.distance_to(Vector2((entities[id] as Dictionary).get("position",Vector2.ZERO)))<=radius:candidates.append(id)
	for id in candidates:
		if not entities.has(id):continue
		var target:=entities[id] as Dictionary;var members:=target.get("member_health",[]) as Array;var replenished:=-1
		for index in members.size():
			if int(members[index])<=0:members[index]=maxi(1,int(target.get("member_maximum_health",1)));replenished=index;break
		if replenished<0:continue
		target["member_health"]=members;var total:=0;for health in members:total+=int(health);target["health"]=total
		_emit_event("battalion.banner_replenished",int(banner.get("id",0)),id,{"member_index":replenished})


func _record_hero_rank_attainment(row: Dictionary) -> void:
	if String(row.get("category", "")) != "hero":
		return
	var team := int(row.get("team", -1))
	var identity := String(row.get("unit_type", ""))
	if team < 0 or identity == "":
		return
	var team_history: Dictionary = _hero_peak_ranks_by_team.get(team, {})
	var rule: Dictionary = _unit_experience_rules.get(identity, {})
	if rule.is_empty():
		if not team_history.has(identity):
			team_history[identity] = -1
	else:
		var previous := int(team_history.get(identity, -1))
		team_history[identity] = maxi(previous, int(row.get("level", rule.get("initial_rank", 1))))
	_hero_peak_ranks_by_team[team] = team_history


func hero_rank_attainment(team: int, rank: int) -> Dictionary:
	## Returns only historical facts the simulation can vouch for. `known`
	## counts distinct revival-stable hero identities whose authored peak met
	## the threshold; `unknown` preserves every identity whose rank was never
	## authored, including after death.
	var known := 0
	var unknown := 0
	var team_history: Dictionary = _hero_peak_ranks_by_team.get(team, {})
	for peak_value in team_history.values():
		var peak := int(peak_value)
		if peak < 0:
			unknown += 1
		elif peak >= rank:
			known += 1
	return {"known": known, "unknown": unknown}


func debug_force_max_level(entity_ids: Array) -> int:
	## Playtest aid: walk each entity up its own authored ExperienceLevel chain
	## by awarding the XP the chain itself demands, so every rank's authored
	## level effects apply exactly as they would in a real match. It never
	## fabricates a rank the source does not author.
	var levelled := 0
	for id_value in entity_ids:
		var entity_id := int(id_value)
		if not entities.has(entity_id):
			continue
		var row: Dictionary = entities[entity_id]
		if int(row.get("health", 0)) <= 0:
			continue
		var rule: Dictionary = _unit_experience_rules.get(String(row.get("unit_type", "")), {})
		if rule.is_empty():
			continue
		var before := int(row.get("level", 1))
		var required := 0
		for level_value in Array(rule.get("levels", [])):
			required = maxi(required, int((level_value as Dictionary).get("required_experience", 0)))
		var deficit := required - int(row.get("experience_xp", 0))
		if deficit > 0:
			_award_experience(row, deficit)
		if int(row.get("level", 1)) != before:
			levelled += 1
	return levelled


func debug_restore_health(entity_ids: Array) -> int:
	## Playtest aid: refill an entity and every living horde member. Dead
	## members stay dead — resurrecting them would change horde size, which is
	## a simulation fact rather than a convenience.
	var healed := 0
	for id_value in entity_ids:
		var entity_id := int(id_value)
		if not entities.has(entity_id):
			continue
		var row: Dictionary = entities[entity_id]
		if int(row.get("health", 0)) <= 0:
			continue
		var member_max := int(row.get("member_maximum_health", 0))
		var members: Array = Array(row.get("member_health", []))
		if member_max > 0 and not members.is_empty():
			for index in range(members.size()):
				if int(members[index]) > 0:
					members[index] = member_max
			row["member_health"] = members
			var total := 0
			for value in members:
				total += int(value)
			row["health"] = total
		else:
			row["health"] = int(row.get("maximum_health", row.get("health", 0)))
		healed += 1
	return healed


func experience_rule_for_unit(unit_type: String) -> Dictionary:
	return (_unit_experience_rules.get(unit_type, {}) as Dictionary).duplicate(true)


func _experience_level_row(rule: Dictionary, rank: int) -> Dictionary:
	return _experience_subsystem().experience_level_row(rule, rank)


func experience_state(entity_id: int) -> Dictionary:
	return _experience_subsystem().experience_state(entity_id)


func experience_unauthored_victims() -> Array[String]:
	return _experience_subsystem().experience_unauthored_victims()


func _award_member_kill_experience(attacker_id: int, target: Dictionary) -> void:
	_experience_subsystem().award_member_kill_experience(attacker_id, target)


func _cah_tally_for(unit_type: String) -> Dictionary:
	if not _cah_award_contracts.has(unit_type):
		return {}
	if not _cah_award_tallies.has(unit_type):
		_cah_award_tallies[unit_type] = (
			(_cah_award_contracts[unit_type] as Dictionary).get("trackingStats", {})
			as Dictionary
		).duplicate(true)
	return _cah_award_tallies[unit_type] as Dictionary


func _record_cah_member_kill(attacker_id: int, target: Dictionary) -> void:
	if not entities.has(attacker_id):
		return
	var attacker := entities[attacker_id] as Dictionary
	if int(attacker.get("health", 0)) <= 0:
		return
	if not _is_hostile(int(attacker.get("team", -1)), int(target.get("team", -1))):
		return
	var tally := _cah_tally_for(String(attacker.get("unit_type", "")))
	if tally.is_empty() and not _cah_award_contracts.has(String(attacker.get("unit_type", ""))):
		return
	tally["ENEMIES_KILLED"] = int(tally.get("ENEMIES_KILLED", 0)) + 1
	# Retail spells this identifier HEROS_KILLED in awardsystem.ini.
	if String(target.get("category", "")) == "hero":
		tally["HEROS_KILLED"] = int(tally.get("HEROS_KILLED", 0)) + 1
	if _cah_openplay_multiplayer() and String(target.get("unit_type", "")).begins_with("CreateAHero__"):
		tally["MP_CREATE_A_HEROES_KILLED"] = int(tally.get("MP_CREATE_A_HEROES_KILLED", 0)) + 1


func _record_cah_structure_kill(attacker_id: int, target: Dictionary) -> void:
	if not entities.has(attacker_id):
		return
	var attacker := entities[attacker_id] as Dictionary
	if not _is_hostile(int(attacker.get("team", -1)), int(target.get("team", -1))):
		return
	var unit_type := String(attacker.get("unit_type", ""))
	var tally := _cah_tally_for(unit_type)
	if tally.is_empty() and not _cah_award_contracts.has(unit_type):
		return
	# The only structure-derived ThingStat retail defines is keeps destroyed;
	# v1 maps the authoritative fortress death path and names all other building
	# categories as gaps instead of inventing a BUILDINGS_DESTROYED counter.
	if _cah_openplay_multiplayer() and String(target.get("structure_kind", "")) == "fortress":
		tally["MP_KEEPS_DESTROYED"] = int(tally.get("MP_KEEPS_DESTROYED", 0)) + 1


func _cah_openplay_multiplayer() -> bool:
	## Awardsystem.ini separates skirmish and open-play multiplayer counters.
	## The explicit session rule is agreed before setup on every lockstep peer;
	## absent remains the historical solo/skirmish path.
	return String(_rules.get("session_mode", "skirmish")) == "openplay-mp"


func _award_experience(row: Dictionary, amount: int) -> void:
	_experience_subsystem().award_experience(row, amount)


func _apply_experience_level_effects(row: Dictionary, level_row: Dictionary) -> void:
	_experience_subsystem().apply_experience_level_effects(row, level_row)


func ability_rules_for_unit(unit_type: String) -> Array:
	return (_unit_ability_rules.get(unit_type, []) as Array).duplicate(true)


func _ensure_capture_building_ability(unit_type: String, document: Dictionary) -> void:
	## Hordes author Command_CaptureBuilding on the CommandSet but do not
	## compile CaptureBuilding.inc as an ability row (AISpecialPowerUpdate is
	## unsupported). Bind the shared include so infantry can capture flags.
	var existing: Array = _unit_ability_rules.get(unit_type, []) as Array
	for rule_value in existing:
		var effect: Dictionary = (rule_value as Dictionary).get("effect", {})
		if String(effect.get("kind", "")) == "capture-building":
			return
	var has_command := false
	for command_value in PlayableUnitAdapter.selection_commands(document):
		if String((command_value as Dictionary).get("commandId", "")) == "Command_CaptureBuilding":
			has_command = true
			break
	if not has_command:
		return
	var capture_rows: Array[Dictionary] = [{
		"ability_id": "Command_CaptureBuilding",
		"slot": 12,
		"special_power_id": "SpecialAbilityCaptureBuilding",
		"targeting": "enemy-object",
		"cooldown_ticks": 0,
		"required_level": 1,
		"level_gate_resolved": true,
		"castable": true,
		"availability_reason": "",
		"limitations": ["capture uses CaptureBuilding.inc StartAbilityRange/PreparationTime"],
		"effect": {
			"kind": "capture-building",
			"startAbilityRange": CAPTURE_BUILDING_RANGE_SOURCE,
			"unpackMs": CAPTURE_BUILDING_UNPACK_MS,
			"preparationMs": CAPTURE_BUILDING_PREPARATION_MS,
			"packMs": CAPTURE_BUILDING_PACK_MS,
			"doCaptureFx": true,
			"sourceIni": "data/ini/object/includes/CaptureBuilding.inc",
		},
		"icon_id": "UPBeacon",
		"label_id": "CONTROLBAR:CaptureBuilding",
		"tooltip_id": "CONTROLBAR:ToolTipCaptureBuilding",
		"fallback_label": "Capture Building",
		"fallback_tooltip": "Take control of targeted structure",
	}]
	var scaled := _scaled_ability_rules(
		capture_rows, float(_rules.get("source_map_transform_scale", 0.0))
	)
	var combined: Array = existing.duplicate()
	combined.append_array(scaled)
	_unit_ability_rules[unit_type] = combined


func ability_states_for(hero_id: int) -> Dictionary:
	if not entities.has(hero_id):
		return {}
	return ((entities[hero_id] as Dictionary).get("ability_states", {}) as Dictionary).duplicate(true)


func _ability_object_kind_tokens(row: Dictionary) -> Array[String]:
	var tokens: Array[String] = ["ANY"]
	for token_value in row.get("kind_of", []) as Array:
		var token := String(token_value).to_upper()
		if token != "" and not tokens.has(token):
			tokens.append(token)
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


func cast_ability(hero_id: int, ability_id: String, target_point: Vector2, team: int = -1) -> Dictionary:
	## Cast one converted hero ability, validating ownership, evidence,
	## cooldown, and the authored level gate before applying the bound effect.
	## `team` >= 0 is the issuing seat (the lockstep command path always passes
	## it): a peer can only cast ITS OWN hero's abilities — fail-closed.
	if not entities.has(hero_id):
		return {"ok": false, "reason": "unknown-hero"}
	var row: Dictionary = entities[hero_id]
	if team >= 0 and int(row.get("team", -1)) != team:
		return {"ok": false, "reason": "wrong-owner"}
	if int(row.get("health", 0)) <= 0:
		return {"ok": false, "reason": "unit-defeated"}
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
	var power_contract := rule.get("special_power_contract", {}) as Dictionary
	var shared_key := "%d:%s" % [int(row.get("team", -1)), String(rule.get("special_power_id", ability_id))]
	if bool(power_contract.get("sharedSyncedTimer", false)):
		ready_tick = maxi(ready_tick, int(_shared_ability_cooldowns.get(shared_key, 0)))
	if tick_index < ready_tick:
		return {"ok": false, "reason": "cooldown-active", "ready_tick": ready_tick}
	var effect: Dictionary = rule.get("effect", {}) as Dictionary
	var targeting := String(rule.get("targeting", "self"))
	var hero_position := Vector2(row.get("position", Vector2.ZERO))
	if targeting != "self":
		var range_limit := float(effect.get("range", 0.0))
		var max_cast_range := float(power_contract.get("maxCastRangeScaled", 0.0))
		if max_cast_range > 0.0:
			range_limit = max_cast_range if range_limit <= 0.0 else minf(range_limit, max_cast_range)
		if range_limit > 0.0 and hero_position.distance_to(target_point) > range_limit:
			return {"ok": false, "reason": "out-of-range"}
	var activation_gate := _validate_special_power_activation(row, power_contract, targeting, target_point)
	if not bool(activation_gate.get("ok", false)):
		return activation_gate
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
		"experience-grant":
			result = _apply_ability_experience_grant(row, effect, target_point if targeting == "point" else hero_position)
		"arrow-storm":
			result = _apply_ability_arrow_storm(row, effect, target_point if targeting == "point" else hero_position)
		"stealth-toggle":
			result = _apply_ability_stealth_toggle(row, effect)
		"teleport":
			result = _apply_ability_teleport(row, effect, target_point)
		"curse":
			result = _apply_ability_curse(row, effect, target_point)
		"leadership-strip":
			result = _apply_ability_leadership_strip(row, effect)
		"activate-module-graph":
			result = _apply_ability_activate_module_graph(row, ability_id, effect, target_point, targeting)
		"weapon-mode-special-power":
			result = _apply_ability_weapon_mode_special_power(row, effect)
		"dominate-enemy":
			result = _apply_ability_dominate_enemy(row, ability_id, effect, target_point, targeting)
		"grab-passenger":
			result = _apply_ability_grab_passenger(row, ability_id, effect, target_point)
		"fling-passenger":
			result = _apply_ability_fling_passenger(row, ability_id, effect)
		"repair-structure":
			result = _apply_ability_repair_structure(row, ability_id, effect, target_point)
		"stop-special-power":
			result = activate_stop_special_power(hero_id, String(effect.get("specialPowerTemplateId", "")), int(row.get("team", -1)))
		"siege-deploy":
			result = _apply_ability_siege_deploy(row, effect, target_point, power_contract)
		"toggle-deploy":
			result = _apply_ability_toggle_deploy(row, effect)
		"special-disguise":
			result = _apply_ability_special_disguise(row, effect)
		"unleash-special-power":
			result = activate_unleash_special_power(hero_id, String(effect.get("specialPowerTemplateId", "")), int(row.get("team", -1)))
		_:
			return {"ok": false, "reason": "no-effect"}
	if not bool(result.get("ok", false)):
		return result
	if effect_kind != "stealth-toggle":
		# Retail InvisibilityNugget ForbiddenConditions: casting another
		# ability while cloaked drops a USING_ABILITY-forbidden stealth.
		_break_stealth(row, "USING_ABILITY")
	state["cooldown_ready_tick"] = tick_index + int(rule.get("cooldown_ticks", 0))
	if bool(power_contract.get("sharedSyncedTimer", false)):
		_shared_ability_cooldowns[shared_key] = state["cooldown_ready_tick"]
	states[ability_id] = state
	row["ability_states"] = states
	_apply_special_power_unit_cost(row, power_contract)
	_emit_event("ability.cast", hero_id, 0, {
		"team": int(row.get("team", -1)),
		"ability_id": ability_id,
		"special_power_id": String(rule.get("special_power_id", "")),
		"effect_kind": effect_kind,
		"affected": int(result.get("affected", 0)),
		"summoned": result.get("summoned", []),
		"sound_id": String(rule.get("initiate_sound_id", rule.get("unit_specific_sound_id", ""))),
		# Authored FX identity and the map-scaled radii the converted leaf gave
		# this cast. The presentation layer had no way to tell one ability's
		# cue from another's before this; spellbook casts already carried their
		# fx_lists on power.cast, hero abilities carried nothing.
		"fx_lists": _ability_fx_list_ids(effect),
		"fx_radius": snappedf(_ability_fx_radius(effect), 0.001),
		"damage_type": String(effect.get("damageType", "")),
		"point": [snappedf(target_point.x, 0.001), snappedf(target_point.y, 0.001)],
	})
	return result


func _apply_ability_activate_module_graph(row: Dictionary, ability_id: String, effect: Dictionary, target_point: Vector2, targeting: String) -> Dictionary:
	return _abilities_subsystem()._apply_ability_activate_module_graph(row, ability_id, effect, target_point, targeting)


func _activate_module_target_identity(row: Dictionary, target_point: Vector2, targeting: String) -> Dictionary:
	if targeting == "self":
		return {"id": int(row.get("id", 0)), "kind": "battalion"}
	var best: Dictionary = {}
	var best_distance := 2.0
	for entity_id in entity_ids():
		var distance := Vector2((entities[entity_id] as Dictionary).get("position", target_point)).distance_to(target_point)
		if distance <= best_distance:
			best_distance = distance; best = {"id": entity_id, "kind": "battalion"}
	for structure_id in structure_ids():
		var distance := Vector2((structures[structure_id] as Dictionary).get("position", target_point)).distance_to(target_point)
		if distance <= best_distance:
			best_distance = distance; best = {"id": structure_id, "kind": "structure"}
	if not best.is_empty():
		return best
	# Command APIs currently carry an object selection as a point. Retain the
	# live attack target only as a fallback when no object occupies that point;
	# this prevents a stale combat target hijacking a newly clicked power target.
	var target_id := int(row.get("target_id", 0))
	var target_kind := String(row.get("target_kind", "battalion"))
	if target_kind == "battalion" and entities.has(target_id):
		return {"id": target_id, "kind": target_kind}
	if target_kind == "structure" and structures.has(target_id):
		return {"id": target_id, "kind": target_kind}
	return best


func _step_activate_module_graph(row: Dictionary) -> void:
	var channel := row.get("activate_module_channel", {}) as Dictionary
	if channel.is_empty():
		return
	var unavoidable_interrupt := int(row.get("health", 0)) <= 0 or bool(row.get("knocked_down", false)) or tick_index < int(row.get("stun_until_tick", -1)) or tick_index < int(row.get("cower_until_tick", -1))
	var voluntary_interrupt := not bool(channel.get("must_finish", false)) and int(row.get("order_sequence", 0)) != int(channel.get("order_sequence_at_start", 0))
	if unavoidable_interrupt or voluntary_interrupt:
		row.erase("activate_module_channel")
		_emit_event("ability.graph_interrupted", int(row.get("id", 0)), int(channel.get("current_target_id", 0)), {"ability_id": channel.get("ability_id"), "unavoidable": unavoidable_interrupt, "must_finish": channel.get("must_finish")})
		return
	if not bool(channel.get("dispatched", false)) and tick_index >= int(channel.get("activation_tick", 0)):
		var results: Array = []
		for route_value in channel.get("routes", []) as Array:
			var route := route_value as Dictionary
			var target := _activate_module_route_target(row, channel, String(route.get("targetMode", "")))
			var result := {"ok": false, "reason": "activate-module-target-lost"}
			if bool(target.get("ok", false)):
				var leaf := _activate_module_effect_range(route.get("effect", {}) as Dictionary, float(channel.get("effect_range_scaled", 0.0)))
				leaf["moduleTag"] = String(route.get("moduleTag", ""))
				result = _dispatch_activate_module_leaf(row, String(channel.get("ability_id", "")), leaf, Vector2(target.get("point", row.get("position", Vector2.ZERO))))
			var receipt := {"module_tag": String(route.get("moduleTag", "")), "target_mode": String(route.get("targetMode", "")), "ok": bool(result.get("ok", false)), "reason": String(result.get("reason", ""))}
			results.append(receipt)
			_emit_event("ability.graph_route", int(row.get("id", 0)), int(target.get("id", 0)), receipt)
		channel["route_results"] = results
		channel["dispatched"] = true
		row["activate_module_channel"] = channel
	if tick_index >= int(channel.get("finish_tick", 0)):
		row.erase("activate_module_channel")
		row["state"] = "idle"
		_emit_event("ability.graph_finished", int(row.get("id", 0)), int(channel.get("current_target_id", 0)), {"ability_id": channel.get("ability_id"), "route_results": channel.get("route_results", [])})


func _activate_module_route_target(row: Dictionary, channel: Dictionary, mode: String) -> Dictionary:
	if mode == "SELF":
		return {"ok": true, "id": int(row.get("id", 0)), "kind": "battalion", "point": Vector2(row.get("position", Vector2.ZERO))}
	if mode == "LOCATION":
		return {"ok": true, "id": 0, "kind": "point", "point": Vector2(channel.get("location", row.get("position", Vector2.ZERO)))}
	if mode == "CURRENT_TARGET":
		var target_id := int(channel.get("current_target_id", 0)); var kind := String(channel.get("current_target_kind", ""))
		if kind == "battalion" and entities.has(target_id) and int((entities[target_id] as Dictionary).get("health", 0)) > 0:
			return {"ok": true, "id": target_id, "kind": kind, "point": Vector2((entities[target_id] as Dictionary).get("position", Vector2.ZERO))}
		if kind == "structure" and structures.has(target_id) and int((structures[target_id] as Dictionary).get("health", 0)) > 0:
			return {"ok": true, "id": target_id, "kind": kind, "point": Vector2((structures[target_id] as Dictionary).get("position", Vector2.ZERO))}
	return {"ok": false, "id": 0, "kind": mode, "point": Vector2.ZERO}


func _activate_module_effect_range(effect: Dictionary, effect_range: float) -> Dictionary:
	var leaf := effect.duplicate(true)
	if effect_range <= 0.0:
		return leaf
	match String(leaf.get("kind", "")):
		"weapon-blast": if float(leaf.get("damage_radius", 0.0)) <= 0.0: leaf["damage_radius"] = effect_range
		"heal", "curse", "leadership-strip": if float(leaf.get("radius_scaled", 0.0)) <= 0.0: leaf["radius_scaled"] = effect_range
		"attribute-modifier": if float(leaf.get("range_scaled", 0.0)) <= 0.0: leaf["range_scaled"] = effect_range
		"experience-grant": if float(leaf.get("radius_scaled", 0.0)) <= 0.0: leaf["radius_scaled"] = effect_range
		"arrow-storm": if float(leaf.get("target_radius_scaled", 0.0)) <= 0.0: leaf["target_radius_scaled"] = effect_range
	return leaf


func _dispatch_activate_module_leaf(row: Dictionary, ability_id: String, effect: Dictionary, point: Vector2) -> Dictionary:
	match String(effect.get("kind", "")):
		"weapon-blast": return _apply_ability_weapon_blast(row, effect, point)
		"heal": return _apply_ability_heal(row, effect, point)
		"attribute-modifier": return _apply_ability_modifier(row, "%s:%s" % [ability_id, String(effect.get("moduleTag", "route"))], effect)
		"summon": return _apply_ability_summon(row, effect, point)
		"weapon-toggle": return _apply_ability_weapon_toggle(row, effect)
		"terror": return _apply_ability_terror(row, ability_id, effect)
		"mount-toggle": return _apply_ability_mount_toggle(row, effect)
		"experience-grant": return _apply_ability_experience_grant(row, effect, point)
		"arrow-storm": return _apply_ability_arrow_storm(row, effect, point)
		"stealth-toggle": return _apply_ability_stealth_toggle(row, effect)
		"teleport": return _apply_ability_teleport(row, effect, point)
		"curse": return _apply_ability_curse(row, effect, point)
		"leadership-strip": return _apply_ability_leadership_strip(row, effect)
		"trigger-fx":
			_emit_event("ability.graph_fx", int(row.get("id", 0)), 0, {"fx_id": String(effect.get("fxId", "")), "point": point})
			return {"ok": true, "reason": "", "affected": 0, "presentation_only": true}
	return {"ok": false, "reason": "activate-module-leaf-unsupported:%s" % String(effect.get("kind", ""))}


func _validate_special_power_activation(row: Dictionary, contract: Dictionary, targeting: String, target_point: Vector2) -> Dictionary:
	for condition_value in contract.get("preventActivationConditions", []) as Array:
		var condition := String(condition_value).to_upper()
		if condition == "MOVING" and (not (row.get("route", []) as Array).is_empty() or float(row.get("current_speed", 0.0)) > 0.0):
			return {"ok": false, "reason": "activation-condition:MOVING"}
		if condition.begins_with("FIRING") and String(row.get("state", "")) == "attack":
			return {"ok": false, "reason": "activation-condition:%s" % condition}
		if bool((row.get("object_status", {}) as Dictionary).get(condition, false)):
			return {"ok": false, "reason": "activation-condition:%s" % condition}
	# Retail's SpecialPower validator passes the candidate destination to the
	# NO_FORBIDDEN_OBJECTS partition query. Targeted casts therefore scan around
	# their destination, not around the caster.
	var origin := Vector2(row.get("position", Vector2.ZERO)) if targeting == "self" else target_point
	var flags: Array = contract.get("flags", []) as Array
	if flags.has("PATHABLE_ONLY"):
		# Retail's PATHABLE_ONLY is a target-location admission rule.  Unlike
		# ordinary movement helpers, this must fail closed when no map navigation
		# authority is attached: silently treating an unknown cell as walkable
		# would consume the power on a target retail refuses.
		if (
			route_provider == null
			or not route_provider.has_method("is_local_inside_navigation")
			or not route_provider.has_method("local_to_grid_cell")
			or not route_provider.has_method("is_navigation_walkable")
			or not bool(route_provider.call("is_local_inside_navigation", target_point))
			or not bool(route_provider.call("is_navigation_walkable", route_provider.call("local_to_grid_cell", target_point)))
		):
			return {"ok": false, "reason": "target-unpathable"}
	elif targeting == "point" and not flags.has("WATER_OK"):
		# WATER_OK is PATHABLE_ONLY's complement and retail never authors the
		# two together: it is the permission to land a point power on a water
		# cell. RotWK authors it on 24 SpecialPowers — Drogoth's Incinerate and
		# the spellbook powers dropped across rivers — so a point power that
		# does NOT carry it refuses the same cell. The refusal only fires on a
		# cell a map authority calls water; with no authority attached there is
		# no water to refuse, and nothing is substituted for the answer.
		if (
			route_provider != null
			and route_provider.has_method("is_local_inside_navigation")
			and route_provider.has_method("local_to_grid_cell")
			and route_provider.has_method("is_water_cell")
			and bool(route_provider.call("is_local_inside_navigation", target_point))
			and bool(route_provider.call("is_water_cell", route_provider.call("local_to_grid_cell", target_point)))
		):
			return {"ok": false, "reason": "target-over-water"}
	var forbidden_range := float(contract.get("forbiddenObjectRangeScaled", 0.0))
	if forbidden_range > 0.0:
		for candidate_id in entity_ids():
			if candidate_id == int(row.get("id", 0)):
				continue
			var candidate := entities[candidate_id] as Dictionary
			if origin.distance_to(Vector2(candidate.get("position", origin))) <= forbidden_range and _ability_token_filter_accepts(candidate, contract.get("forbiddenObjectFilter", []) as Array):
				return {"ok": false, "reason": "forbidden-object-nearby", "object_id": candidate_id}
		for structure_id in structure_ids():
			var candidate := structures[structure_id] as Dictionary
			if int(candidate.get("health", 0)) <= 0:
				continue
			if origin.distance_to(Vector2(candidate.get("position", origin))) <= forbidden_range and _ability_token_filter_accepts(candidate, contract.get("forbiddenObjectFilter", []) as Array):
				return {"ok": false, "reason": "forbidden-object-nearby", "object_id": structure_id, "object_kind": "structure"}
	if targeting in ["enemy-object", "object"] and not (contract.get("objectFilter", []) as Array).is_empty():
		var matched := false
		for candidate_id in entity_ids():
			var candidate := entities[candidate_id] as Dictionary
			if targeting == "enemy-object" and int(candidate.get("team", -1)) == int(row.get("team", -1)):
				continue
			if Vector2(candidate.get("position", Vector2.ZERO)).distance_to(target_point) <= 1.5 and _ability_token_filter_accepts(candidate, contract.get("objectFilter", []) as Array):
				matched = true
				break
		if not matched:
			for structure_id in structure_ids():
				var candidate := structures[structure_id] as Dictionary
				if int(candidate.get("health", 0)) <= 0:
					continue
				if targeting == "enemy-object" and int(candidate.get("team", -1)) == int(row.get("team", -1)):
					continue
				if Vector2(candidate.get("position", Vector2.ZERO)).distance_to(target_point) <= 1.5 and _ability_token_filter_accepts(candidate, contract.get("objectFilter", []) as Array):
					matched = true
					break
		if not matched:
			return {"ok": false, "reason": "object-filter-refused"}
	return {"ok": true, "reason": ""}


func _ability_token_filter_accepts(row: Dictionary, tokens: Array) -> bool:
	if tokens.is_empty():
		return true
	var probe := {"category": String(row.get("category", "")), "kind_of": _ability_object_kind_tokens(row)}
	return _transport_filter_accepts(probe, tokens)


func _apply_special_power_unit_cost(row: Dictionary, contract: Dictionary) -> void:
	var cost := maxi(0, int(contract.get("unitCost", 0)))
	if cost <= 0:
		return
	var health_values: Array = row.get("member_health", []) as Array
	var consumed := 0
	for index in range(health_values.size() - 1, -1, -1):
		if consumed >= cost:
			break
		if int(health_values[index]) > 0:
			health_values[index] = 0
			consumed += 1
	row["member_health"] = health_values
	var aggregate := 0
	for health_value in health_values:
		aggregate += int(health_value)
	row["health"] = aggregate
	_emit_event("ability.unit_cost", int(row.get("id", 0)), 0, {"count": consumed, "death_types": contract.get("unitCostDeathTypes", [])})


func _ability_fx_list_ids(effect: Dictionary) -> Array:
	## Authored FXList ids on a converted ability leaf, in a stable order. The
	## converter emits these under kind-specific keys (fireFxId on weapon
	## leaves, healFxId on heals, levelFxId on level grants); nothing is
	## synthesised when a leaf authors none.
	var ids: Array = []
	for key in ["fireFxId", "healFxId", "levelFxId", "fxId", "dominatedFxId", "triggerFxId"]:
		var value := String(effect.get(key, ""))
		if value != "" and not ids.has(value):
			ids.append(value)
	return ids


func _ability_fx_radius(effect: Dictionary) -> float:
	## The largest map-scaled radius this ability actually acts over, so the
	## presentation cue covers exactly the ground the sim affected. Ability
	## kinds that author no radius return 0 and get no radial cue.
	var radius := 0.0
	for key in ["knockback_radius", "damage_radius", "radius_scaled", "target_radius_scaled", "dominate_radius_scaled"]:
		radius = maxf(radius, float(effect.get(key, 0.0)))
	return radius


func _ability_enemies_near(team: int, point: Vector2, radius: float) -> Array[int]:
	var result: Array[int] = []
	# Ability blasts cover a bounded disc, so this is a neighbourhood query.
	# Sorted, because callers apply damage in the returned order.
	for id in _spatial_gather_sorted(point, radius):
		if not entities.has(id):
			continue
		var row: Dictionary = entities[id]
		if int(row.get("team", -1)) == team or int(row.get("health", 0)) <= 0:
			continue
		if Vector2(row.get("position", Vector2.ZERO)).distance_to(point) <= radius:
			result.append(id)
	return result


func _apply_ability_weapon_blast(hero_row: Dictionary, effect: Dictionary, point: Vector2) -> Dictionary:
	return _abilities_subsystem()._apply_ability_weapon_blast(hero_row, effect, point)


func _apply_ability_heal(hero_row: Dictionary, effect: Dictionary, epicenter: Vector2) -> Dictionary:
	return _abilities_subsystem()._apply_ability_heal(hero_row, effect, epicenter)


func _apply_ability_modifier(hero_row: Dictionary, ability_id: String, effect: Dictionary) -> Dictionary:
	return _abilities_subsystem()._apply_ability_modifier(hero_row, ability_id, effect)


func _apply_ability_weapon_toggle(hero_row: Dictionary, effect: Dictionary) -> Dictionary:
	return _abilities_subsystem()._apply_ability_weapon_toggle(hero_row, effect)


func _siege_deploy_target(source: Dictionary, effect: Dictionary, point: Vector2, contract: Dictionary) -> int:
	if String(effect.get("targetMode", "")) != "TARGET_STRUCTURE":
		return 0
	var chosen_id := 0
	var chosen_distance := 1.5
	for structure_id in structure_ids():
		var candidate := structures[structure_id] as Dictionary
		if int(candidate.get("health", 0)) <= 0:
			continue
		var distance := Vector2(candidate.get("position", Vector2.ZERO)).distance_to(point)
		if distance > chosen_distance:
			continue
		if not _ability_token_filter_accepts(candidate, contract.get("objectFilter", []) as Array):
			continue
		if chosen_id == 0 or distance < chosen_distance or (is_equal_approx(distance, chosen_distance) and structure_id < chosen_id):
			chosen_id = structure_id
			chosen_distance = distance
	return chosen_id


func _apply_ability_siege_deploy(row: Dictionary, effect: Dictionary, point: Vector2, contract: Dictionary) -> Dictionary:
	return _abilities_subsystem()._apply_ability_siege_deploy(row, effect, point, contract)


func _toggle_deploy_set_model_condition(row: Dictionary, condition: String) -> void:
	var conditions: Array = (row.get("model_conditions", []) as Array).duplicate()
	for deploy_condition in ["UNPACKING", "PACKING", "DEPLOYED"]:
		conditions.erase(deploy_condition)
	if condition != "" and not conditions.has(condition):
		conditions.append(condition)
	conditions.sort()
	if conditions.is_empty():
		row.erase("model_conditions")
	else:
		row["model_conditions"] = conditions


func _toggle_deploy_set_modifier(row: Dictionary, modifiers: Array, active: bool) -> void:
	const KEY := "ability:toggle-deploy"
	var table := row.get("timed_modifiers", {}) as Dictionary
	if active:
		table[KEY] = {
			"modifiers": modifiers.duplicate(true),
			"expires_tick": -1,
			"persistent": true,
		}
	else:
		table.erase(KEY)
	if table.is_empty():
		row.erase("timed_modifiers")
	else:
		row["timed_modifiers"] = table


func _apply_ability_toggle_deploy(row: Dictionary, effect: Dictionary) -> Dictionary:
	return _abilities_subsystem()._apply_ability_toggle_deploy(row, effect)


func _step_toggle_deploy(row: Dictionary) -> void:
	var channel := row.get("toggle_deploy_channel", {}) as Dictionary
	if channel.is_empty() or tick_index < int(channel.get("phase_end_tick", 0)):
		return
	var phase := String(channel.get("phase", ""))
	var statuses := row.get("object_status", {}) as Dictionary
	if phase == "unpacking":
		statuses["DEPLOYED"] = true
		row["object_status"] = statuses
		_toggle_deploy_set_model_condition(row, "DEPLOYED")
		_toggle_deploy_set_modifier(row, channel.get("modifiers", []) as Array, true)
		row["toggle_deployed"] = true
		row.erase("toggle_deploy_channel")
		row["state"] = "idle"
		_emit_event("ability.toggle_deployed", int(row.get("id", 0)), 0, {
			"modifier_id": channel.get("modifier_id", ""),
			"presentation_receipt": "model-condition:DEPLOYED",
		})
	elif phase == "packing":
		statuses.erase("DEPLOYED")
		if statuses.is_empty():
			row.erase("object_status")
		else:
			row["object_status"] = statuses
		_toggle_deploy_set_model_condition(row, "")
		_toggle_deploy_set_modifier(row, [], false)
		row.erase("toggle_deployed")
		row.erase("toggle_deploy_channel")
		row["state"] = "idle"
		_emit_event("ability.toggle_undeployed", int(row.get("id", 0)), 0, {
			"modifier_id": channel.get("modifier_id", ""),
			"presentation_receipt": "model-condition:CLEAR_DEPLOYED",
		})


func _step_siege_deploy(row: Dictionary) -> void:
	var channel := row.get("siege_deploy_channel", {}) as Dictionary
	if channel.is_empty():
		return
	var phase := String(channel.get("phase", ""))
	if phase == "lowering" and tick_index >= int(channel.get("phase_end_tick", 0)):
		var statuses := row.get("object_status", {}) as Dictionary
		statuses["DEPLOYED"] = true
		row["object_status"] = statuses
		row["siege_deployed"] = true
		channel["phase"] = "deployed"
		channel["phase_end_tick"] = -1
		row["siege_deploy_channel"] = channel
		var evacuated: Array[int] = []
		if bool(channel.get("evacuate_passengers", false)):
			var passenger_ids := (containment.get(int(row.get("id", 0)), []) as Array).duplicate()
			passenger_ids.sort()
			for passenger_value in passenger_ids:
				var passenger_id := int(passenger_value)
				_finish_transport_exit(int(row.get("id", 0)), passenger_id)
				evacuated.append(passenger_id)
		_emit_event("ability.siege_deployed", int(row.get("id", 0)), int(channel.get("target_id", 0)), {"evacuated_ids": evacuated, "model_receipts": channel.get("model_receipts", [])})
		return
	if phase == "retracting" and tick_index >= int(channel.get("phase_end_tick", 0)):
		var statuses := row.get("object_status", {}) as Dictionary
		statuses.erase("DEPLOYED")
		if statuses.is_empty():
			row.erase("object_status")
		else:
			row["object_status"] = statuses
		row.erase("siege_deployed")
		row.erase("siege_deploy_channel")
		row["state"] = "idle"
		_emit_event("ability.siege_retracted", int(row.get("id", 0)), int(channel.get("target_id", 0)), {"model_receipts": channel.get("model_receipts", [])})


func _apply_ability_weapon_mode_special_power(row: Dictionary, effect: Dictionary) -> Dictionary:
	return _abilities_subsystem()._apply_ability_weapon_mode_special_power(row, effect)


func _attach_weapon_mode_special_power_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed":
		return
	var fields := contract.get("fields", {}) as Dictionary
	var template := String(_module_contract_value(fields, "SpecialPowerTemplate", "")).strip_edges()
	if template == "":
		return
	var policies: Array = row.get("weapon_mode_special_powers", []) as Array
	var key := "%s:%d" % [String(contract.get("tag", "")), int(contract.get("line", 0))]
	for existing_value in policies:
		if String((existing_value as Dictionary).get("key", "")) == key:
			return
	var duration_field := fields.get("Duration", {}) as Dictionary
	var duration_ms := -1.0
	var unsupported: Array[String] = []
	if duration_field.has("milliseconds"):
		duration_ms = float(duration_field.get("milliseconds", -1.0))
	else:
		var define := String(duration_field.get("define", ""))
		var defines := _rules.get("weapon_mode_duration_defines", {}) as Dictionary
		if define != "" and typeof(defines.get(define)) in [TYPE_INT, TYPE_FLOAT] and float(defines[define]) >= 0.0:
			duration_ms = float(defines[define])
		else:
			unsupported.append("unresolved_duration_define:%s" % define)
	var modifier_name := String(_module_contract_value(fields, "AttributeModifier", ""))
	var modifier := ((_rules.get("attribute_modifier_rules", {}) as Dictionary).get(modifier_name, {}) as Dictionary).duplicate(true)
	if modifier_name != "" and (modifier.is_empty() or (modifier.get("effects", []) as Array).is_empty()):
		unsupported.append("unresolved_modifier_list:%s" % modifier_name)
	var flags := _typed_contract_tokens(fields, "WeaponSetFlags")
	var lock_slot := String(_module_contract_value(fields, "LockWeaponSlot", "")).to_lower()
	var mode := _resolve_weapon_mode_special_power_profile(row, flags, lock_slot)
	if (not flags.is_empty() or lock_slot != "") and mode == "":
		unsupported.append("weapon_mode_profile_unavailable:%s" % (lock_slot if lock_slot != "" else " ".join(flags)))
	policies.append({
		"key": key, "special_power_template": template,
		"duration_ticks": _ship_contract_delay_ticks(duration_ms) if duration_ms >= 0.0 else 0,
		"starts_paused": bool(_module_contract_value(fields, "StartsPaused", false)),
		"paused": bool(_module_contract_value(fields, "StartsPaused", false)),
		"modifier_name": modifier_name, "modifier": modifier,
		"weapon_set_flags": flags, "lock_weapon_slot": lock_slot, "mode": mode,
		"active": false, "expires_tick": -1, "prior_mode": "", "prior_toggle_mode": "", "prior_weapon_set_flags": [],
		"activation_count": 0, "unsupported_semantics": unsupported,
	})
	row["weapon_mode_special_powers"] = policies


func _resolve_weapon_mode_special_power_profile(row: Dictionary, flags: Array, lock_slot: String) -> String:
	var modes := row.get("weapon_modes", {}) as Dictionary
	var requested: Array[String] = []
	for flag in flags:
		requested.append(String(flag).to_lower())
	if lock_slot != "":
		for mode_key in modes.keys():
			if String((modes[mode_key] as Dictionary).get("weapon_slot", "")) == lock_slot:
				requested.append(String(mode_key))
	for candidate in requested:
		if modes.has(candidate):
			return candidate
	return ""


func set_weapon_mode_special_power_paused(entity_id: int, special_power_template: String, paused: bool) -> Dictionary:
	if not entities.has(entity_id):
		return {"ok": false, "reason": "entity-missing"}
	var row := entities[entity_id] as Dictionary
	if not row.has("weapon_mode_special_powers"):
		_attach_module_contracts(row)
	var policies := row.get("weapon_mode_special_powers", []) as Array
	for index in policies.size():
		var policy := policies[index] as Dictionary
		if String(policy.get("special_power_template", "")) == special_power_template:
			policy["paused"] = paused
			policies[index] = policy; row["weapon_mode_special_powers"] = policies
			return {"ok": true, "reason": "", "paused": paused}
	return {"ok": false, "reason": "weapon-mode-special-power-missing"}


func activate_weapon_mode_special_power(entity_id: int, special_power_template: String, team: int = -1) -> Dictionary:
	if not entities.has(entity_id):
		return {"ok": false, "reason": "entity-missing"}
	var row := entities[entity_id] as Dictionary
	if team >= 0 and int(row.get("team", -1)) != team:
		return {"ok": false, "reason": "wrong-owner"}
	if int(row.get("health", 0)) <= 0:
		return {"ok": false, "reason": "unit-defeated"}
	if not row.has("weapon_mode_special_powers"):
		_attach_module_contracts(row)
	var policies := row.get("weapon_mode_special_powers", []) as Array
	for index in policies.size():
		var policy := policies[index] as Dictionary
		if String(policy.get("special_power_template", "")) != special_power_template:
			continue
		if bool(policy.get("paused", false)):
			return {"ok": false, "reason": "special-power-paused"}
		if not (policy.get("unsupported_semantics", []) as Array).is_empty():
			return {"ok": false, "reason": String((policy.get("unsupported_semantics", []) as Array)[0])}
		if bool(policy.get("active", false)):
			_end_weapon_mode_special_power(row, policy)
		policy["prior_mode"] = String(row.get("active_weapon_mode", row.get("default_weapon_mode", "default")))
		policy["prior_toggle_mode"] = String(row.get("weapon_toggle_mode", ""))
		policy["prior_weapon_set_flags"] = (row.get("weapon_set_flags", []) as Array).duplicate()
		var mode := String(policy.get("mode", ""))
		if mode != "" and not _apply_weapon_mode(row, mode):
			return {"ok": false, "reason": "weapon-mode-unavailable:%s" % mode}
		if mode != "":
			row["weapon_toggle_mode"] = mode
		if not (policy.get("weapon_set_flags", []) as Array).is_empty():
			row["weapon_set_flags"] = (policy.get("weapon_set_flags", []) as Array).duplicate()
		var modifier := policy.get("modifier", {}) as Dictionary
		var expiry := tick_index + int(policy.get("duration_ticks", 0))
		if not modifier.is_empty():
			var modifier_key := "weapon-mode-special:%s" % special_power_template
			_set_timed_modifier(row, modifier_key, modifier.get("effects", []) as Array, expiry)
			(row.get("timed_modifiers", {}) as Dictionary)[modifier_key]["category"] = String(modifier.get("category", ""))
		policy["active"] = true; policy["expires_tick"] = expiry; policy["activation_count"] = int(policy.get("activation_count", 0)) + 1
		policies[index] = policy; row["weapon_mode_special_powers"] = policies
		_emit_event("ability.weapon_mode_started", entity_id, 0, {"special_power_template":special_power_template,"mode":mode,"expires_tick":expiry,"weapon_set_flags":policy.get("weapon_set_flags"),"lock_weapon_slot":policy.get("lock_weapon_slot")})
		return {"ok": true, "reason": "", "mode": mode, "expires_tick": expiry}
	return {"ok": false, "reason": "weapon-mode-special-power-missing"}


func _step_weapon_mode_special_powers(row: Dictionary) -> void:
	var policies := row.get("weapon_mode_special_powers", []) as Array
	for index in policies.size():
		var policy := policies[index] as Dictionary
		if bool(policy.get("active", false)) and tick_index >= int(policy.get("expires_tick", 0)):
			_end_weapon_mode_special_power(row, policy)
			policies[index] = policy
	if policies.is_empty():
		row.erase("weapon_mode_special_powers")
	else:
		row["weapon_mode_special_powers"] = policies


func _end_weapon_mode_special_power(row: Dictionary, policy: Dictionary) -> void:
	var prior := String(policy.get("prior_mode", row.get("default_weapon_mode", "default")))
	if prior != "":
		_apply_weapon_mode(row, prior)
	row["weapon_toggle_mode"] = String(policy.get("prior_toggle_mode", ""))
	row["weapon_set_flags"] = (policy.get("prior_weapon_set_flags", []) as Array).duplicate()
	var table := row.get("timed_modifiers", {}) as Dictionary
	table.erase("weapon-mode-special:%s" % String(policy.get("special_power_template", "")))
	if table.is_empty(): row.erase("timed_modifiers")
	else: row["timed_modifiers"] = table
	policy["active"] = false; policy["expires_tick"] = -1
	_emit_event("ability.weapon_mode_finished", int(row.get("id", 0)), 0, {"special_power_template":policy.get("special_power_template"),"restored_mode":prior})


func _apply_ability_dominate_enemy(row: Dictionary, ability_id: String, effect: Dictionary, target_point: Vector2, targeting: String) -> Dictionary:
	return _abilities_subsystem()._apply_ability_dominate_enemy(row, ability_id, effect, target_point, targeting)


func _apply_ability_grab_passenger(row: Dictionary, ability_id: String, effect: Dictionary, target_point: Vector2) -> Dictionary:
	return _abilities_subsystem()._apply_ability_grab_passenger(row, ability_id, effect, target_point)


func _apply_ability_repair_structure(row: Dictionary, ability_id: String, effect: Dictionary, target_point: Vector2) -> Dictionary:
	return _abilities_subsystem()._apply_ability_repair_structure(row, ability_id, effect, target_point)


func _step_repair_structure(row: Dictionary) -> void:
	var channel := row.get("repair_structure_channel", {}) as Dictionary
	if channel.is_empty():
		return
	var target_id := int(channel.get("target_id", 0))
	if int(row.get("health", 0)) <= 0 or int(row.get("order_sequence", 0)) != int(channel.get("order_sequence_at_start", 0)) or not structures.has(target_id):
		row.erase("repair_structure_channel")
		_emit_event("ability.repair_interrupted", int(row.get("id", 0)), target_id)
		return
	var structure := structures[target_id] as Dictionary
	if int(structure.get("team", -1)) != int(row.get("team", -2)) or int(structure.get("health", 0)) <= 0:
		row.erase("repair_structure_channel"); return
	var maximum := maxi(1, int(structure.get("maximum_health", structure.get("health", 1))))
	if int(structure.get("health", 0)) >= maximum:
		row.erase("repair_structure_channel"); row["state"] = "idle"
		_emit_event("ability.repair_finished", int(row.get("id", 0)), target_id, {"health": maximum})
		return
	var amount := float(channel.get("fractional_health", 0.0)) + maximum * float(channel.get("max_health_fraction_per_second", 0.0)) * TICK_SECONDS
	var whole := floori(amount)
	channel["fractional_health"] = amount - whole
	if whole > 0:
		structure["health"] = mini(maximum, int(structure.get("health", 0)) + whole)
	row["repair_structure_channel"] = channel
	_emit_event("ability.repair_tick", int(row.get("id", 0)), target_id, {"applied": whole, "health": int(structure.get("health", 0)), "fractional_health": channel.get("fractional_health")})


func _grab_passenger_target(source: Dictionary, effect: Dictionary, point: Vector2) -> int:
	var best_id := 0
	var best_distance := 1.5
	var admission := effect.get("targetAdmission", {}) as Dictionary
	var contain := effect.get("containment", {}) as Dictionary
	var tree_ids := admission.get("treeObjectIds", []) as Array
	var manual_filter: Array = String(admission.get("passengerFilter", "")).split(" ", false)
	for target_id in entity_ids():
		if target_id == int(source.get("id", 0)) or entity_container.has(target_id):
			continue
		var target := entities[target_id] as Dictionary
		if int(target.get("health", 0)) <= 0:
			continue
		var distance := Vector2(target.get("position", Vector2.ZERO)).distance_to(point)
		if distance > best_distance:
			continue
		var source_object_id := String(target.get("source_object_id", target.get("object_id", "")))
		var exact_tree := bool(effect.get("allowTree", false)) and tree_ids.has(source_object_id)
		if bool(effect.get("allowTree", false)) and not exact_tree:
			continue
		if not _transport_filter_accepts(target, manual_filter):
			continue
		var relation := team_relationship(int(source.get("team", -1)), int(target.get("team", -2)))
		var allowed := (
			(relation == "local" and bool(contain.get("allowAlliesInside", false)))
			or (relation == "allied" and bool(contain.get("allowAlliesInside", false)))
			or (relation == "enemy" and bool(contain.get("allowEnemiesInside", false)))
			or (relation == "unavailable" and bool(contain.get("allowNeutralInside", false)))
		)
		if not allowed:
			continue
		best_id = target_id; best_distance = distance
	return best_id


func _step_grab_passenger(row: Dictionary) -> void:
	var channel := row.get("grab_passenger_channel", {}) as Dictionary
	if channel.is_empty():
		return
	if int(row.get("health", 0)) <= 0:
		row.erase("grab_passenger_channel")
		_eject_grabbed_passengers(int(row.get("id", 0)), row)
		_emit_event("ability.grab_interrupted", int(row.get("id", 0)), int(channel.get("target_id", 0)), {"reason": "carrier-dead"})
		return
	if not bool(channel.get("triggered", false)) and int(row.get("order_sequence", 0)) != int(channel.get("order_sequence_at_start", 0)):
		row.erase("grab_passenger_channel")
		_emit_event("ability.grab_interrupted", int(row.get("id", 0)), int(channel.get("target_id", 0)), {"reason": "order-interrupt"})
		return
	if not bool(channel.get("triggered", false)) and tick_index >= int(channel.get("trigger_tick", 0)):
		var target_id := int(channel.get("target_id", 0))
		if not entities.has(target_id) or entity_container.has(target_id):
			row.erase("grab_passenger_channel")
			_emit_event("ability.grab_interrupted", int(row.get("id", 0)), target_id, {"reason": "target-unavailable"})
			return
		var target := entities[target_id] as Dictionary
		var contained := contain_entity(int(row.get("id", 0)), target_id)
		if not bool(contained.get("ok", false)):
			row.erase("grab_passenger_channel")
			return
		var effect := channel.get("effect", {}) as Dictionary
		var contain := effect.get("containment", {}) as Dictionary
		target["grab_prior_status"] = (target.get("object_status", {}) as Dictionary).duplicate(true)
		var statuses := target.get("object_status", {}) as Dictionary
		for status_value in contain.get("objectStatusOfContained", []) as Array:
			statuses[String(status_value)] = true
		target["object_status"] = statuses; target["state"] = "contained"; target["position"] = row.get("position", Vector2.ZERO)
		row["grab_weapon_set_types"] = (contain.get("weaponSetTypes", []) as Array).duplicate(true)
		row["grab_weapon_state_types"] = (contain.get("weaponStateTypes", []) as Array).duplicate(true)
		var acquire := effect.get("acquire", {}) as Dictionary
		var heal_percent := float(acquire.get("healGainPercent", 0.0))
		if heal_percent > 0.0:
			var maximum := maxi(1, int(row.get("maximum_health", row.get("health", 1))))
			row["health"] = mini(maximum, int(row.get("health", 0)) + roundi(maximum * heal_percent / 100.0))
		var award_xp := int(acquire.get("awardXp", 0))
		if award_xp > 0:
			_award_experience(row, award_xp)
		channel["triggered"] = true; row["grab_passenger_channel"] = channel
		_emit_event("ability.passenger_grabbed", int(row.get("id", 0)), target_id, {"heal_gain_percent": heal_percent, "award_xp": award_xp, "statuses": contain.get("objectStatusOfContained", []), "weapon_sets": contain.get("weaponSetTypes", []), "weapon_states": contain.get("weaponStateTypes", [])})
	if tick_index >= int(channel.get("finish_tick", 0)):
		row.erase("grab_passenger_channel"); row["state"] = "idle"
		_emit_event("ability.grab_finished", int(row.get("id", 0)), int(channel.get("target_id", 0)))


func _eject_grabbed_passengers(carrier_id: int, carrier: Dictionary) -> void:
	for target_value in (containment.get(carrier_id, []) as Array).duplicate():
		var target_id := int(target_value)
		exit_entity_container(target_id)
		if entities.has(target_id):
			var target := entities[target_id] as Dictionary
			target["object_status"] = (target.get("grab_prior_status", {}) as Dictionary).duplicate(true)
			target.erase("grab_prior_status"); target["state"] = "idle"; target["position"] = carrier.get("position", Vector2.ZERO)


func _apply_ability_fling_passenger(row: Dictionary, ability_id: String, effect: Dictionary) -> Dictionary:
	return _abilities_subsystem()._apply_ability_fling_passenger(row, ability_id, effect)


func release_grabbed_passenger(carrier_id: int, release_index: int = 0) -> Dictionary:
	if not entities.has(carrier_id):
		return {"ok": false, "reason": "carrier-missing"}
	var carrier := entities[carrier_id] as Dictionary
	var policies: Array = []
	for rule_value in _unit_ability_rules.get(String(carrier.get("unit_type", "")), []) as Array:
		var effect := (rule_value as Dictionary).get("effect", {}) as Dictionary
		if String(effect.get("kind", "")) == "grab-passenger":
			policies = effect.get("releaseAbilities", []) as Array; break
	if release_index < 0 or release_index >= policies.size():
		return {"ok": false, "reason": "release-ability-missing"}
	return _apply_ability_fling_passenger(carrier, "nested-release:%d" % release_index, policies[release_index] as Dictionary)


func _step_fling_passenger(row: Dictionary) -> void:
	var channel := row.get("fling_passenger_channel", {}) as Dictionary
	if channel.is_empty():
		return
	var effect := channel.get("effect", {}) as Dictionary
	if int(row.get("health", 0)) <= 0:
		row.erase("fling_passenger_channel"); _eject_grabbed_passengers(int(row.get("id", 0)), row); return
	if not bool(effect.get("mustFinishAbility", false)) and not bool(channel.get("triggered", false)) and int(row.get("order_sequence", 0)) != int(channel.get("order_sequence_at_start", 0)):
		row.erase("fling_passenger_channel"); _emit_event("ability.fling_interrupted", int(row.get("id", 0)), int(channel.get("passenger_id", 0))); return
	if not bool(channel.get("triggered", false)) and tick_index >= int(channel.get("trigger_tick", 0)):
		var passenger_id := int(channel.get("passenger_id", 0))
		if not entities.has(passenger_id) or int(entity_container.get(passenger_id, -1)) != int(row.get("id", 0)):
			row.erase("fling_passenger_channel"); return
		var passenger := entities[passenger_id] as Dictionary
		exit_entity_container(passenger_id)
		var physics_id := _spawn_fling_physics_object(row, passenger, effect)
		entities.erase(passenger_id)
		channel["triggered"] = true; channel["physics_object_id"] = physics_id; row["fling_passenger_channel"] = channel
		_emit_event("ability.passenger_flung", int(row.get("id", 0)), passenger_id, {"physics_object_id": physics_id, "velocity": effect.get("velocity", {}), "custom_animation": effect.get("customAnimation", {})})
	if tick_index >= int(channel.get("finish_tick", 0)):
		row.erase("fling_passenger_channel"); row["state"] = "idle"
		_emit_event("ability.fling_finished", int(row.get("id", 0)), int(channel.get("passenger_id", 0)))


func _spawn_fling_physics_object(carrier: Dictionary, passenger: Dictionary, effect: Dictionary) -> int:
	var id := _next_physics_object_id; _next_physics_object_id += 1
	physics_objects[id] = {
		"id": id, "source_object_id": String(passenger.get("source_object_id", passenger.get("object_id", ""))),
		"position": Vector2(carrier.get("position", Vector2.ZERO)), "height_source": 0.001,
		"horizontal_velocity": Vector2(effect.get("horizontal_velocity_scaled", Vector2.ZERO)),
		"vertical_velocity_source": float(effect.get("vertical_velocity_source", 0.0)),
		"gravity_multiplier": 1.0, "allow_bouncing": false, "orient_to_flight_path": true,
		"kill_when_resting_on_ground": true, "first_height_source": 0.0, "second_height_source": 0.0,
		"shock_stunned_low_ms": 0, "shock_stunned_high_ms": 0, "shock_standing_ms": 0,
		"bounce_count": 0, "phase": "airborne", "phase_ticks_remaining": 0,
		"yaw_radians": 0.0, "pitch_radians": 0.0, "landing_warhead": (effect.get("landingWarhead", {}) as Dictionary).duplicate(true),
		"fling_attacker_id": int(carrier.get("id", 0)), "unsupported_semantics": [],
	}
	return id


func _step_dominate_enemy(row: Dictionary) -> void:
	var channel := row.get("dominate_enemy_channel", {}) as Dictionary
	if channel.is_empty():
		return
	var interrupted := (
		int(row.get("health", 0)) <= 0
		or bool(row.get("knocked_down", false))
		or tick_index < int(row.get("stun_until_tick", -1))
		or tick_index < int(row.get("cower_until_tick", -1))
		or (not bool(channel.get("triggered", false)) and int(row.get("order_sequence", 0)) != int(channel.get("order_sequence_at_start", 0)))
	)
	if interrupted:
		row.erase("dominate_enemy_channel")
		_emit_event("ability.dominate_interrupted", int(row.get("id", 0)), 0, {"ability_id": channel.get("ability_id"), "triggered": channel.get("triggered")})
		return
	if not bool(channel.get("triggered", false)) and tick_index >= int(channel.get("activation_tick", 0)):
		var effect := {
			"affectsFilter": channel.get("affects_filter", ""),
			"dominate_radius_scaled": channel.get("dominate_radius_scaled", 0.0),
		}
		var affected := _dominate_enemy_candidates(row, effect, Vector2(channel.get("target_point", Vector2.ZERO)), String(channel.get("targeting", "point")))
		for target_id in affected:
			_dominate_enemy_convert(
				row, entities[int(target_id)] as Dictionary,
				bool(channel.get("permanently_convert", false)),
				int(channel.get("temporary_defect_duration_ticks", 0))
			)
		channel["affected_ids"] = affected
		channel["triggered"] = true
		row["dominate_enemy_channel"] = channel
		_emit_event("ability.dominate_triggered", int(row.get("id", 0)), int(affected[0]) if not affected.is_empty() else 0, {
			"ability_id": channel.get("ability_id"), "affected_ids": affected,
			"presentation": channel.get("presentation"),
		})
	if tick_index >= int(channel.get("finish_tick", 0)):
		row.erase("dominate_enemy_channel")
		row["state"] = "idle"
		_emit_event("ability.dominate_finished", int(row.get("id", 0)), 0, {"ability_id": channel.get("ability_id"), "affected_ids": channel.get("affected_ids", [])})


func _dominate_enemy_candidates(source: Dictionary, effect: Dictionary, point: Vector2, targeting: String) -> Array[int]:
	var radius := float(effect.get("dominate_radius_scaled", 0.0))
	if radius <= 0.0:
		radius = 1.5
	if targeting == "enemy-object":
		var chosen_id := 0
		var chosen_distance := 1.5
		for target_id in entity_ids():
			if target_id == int(source.get("id", 0)):
				continue
			var target := entities[target_id] as Dictionary
			var distance := Vector2(target.get("position", Vector2.ZERO)).distance_to(point)
			if int(target.get("health", 0)) > 0 and (chosen_id == 0 and distance <= chosen_distance or chosen_id != 0 and distance < chosen_distance) and _dominate_enemy_filter_accepts(source, target, String(effect.get("affectsFilter", ""))):
				chosen_distance = distance
				chosen_id = target_id
		var single: Array[int] = []
		if chosen_id != 0:
			single.append(chosen_id)
		return single
	var result: Array[int] = []
	for target_id in entity_ids():
		if target_id == int(source.get("id", 0)):
			continue
		var target := entities[target_id] as Dictionary
		if int(target.get("health", 0)) <= 0 or Vector2(target.get("position", Vector2.ZERO)).distance_to(point) > radius:
			continue
		if not _dominate_enemy_filter_accepts(source, target, String(effect.get("affectsFilter", ""))):
			continue
		result.append(target_id)
	return result


func _dominate_enemy_filter_accepts(source: Dictionary, target: Dictionary, filter_text: String) -> bool:
	var tokens: Array = filter_text.split(" ", false)
	if tokens.is_empty():
		return false
	var source_team := int(source.get("team", -1))
	var target_team := int(target.get("team", -1))
	var relation_allowed := false
	var has_relation := false
	var trait_tokens: Array = []
	for token_value in tokens:
		var token := String(token_value).to_upper()
		if token == "ENEMIES":
			has_relation = true; relation_allowed = relation_allowed or _is_hostile(source_team, target_team)
		elif token == "NEUTRAL":
			has_relation = true; relation_allowed = relation_allowed or target_team == NEUTRAL_TEAM or target_team < 0
		elif token == "ALLIES":
			has_relation = true; relation_allowed = relation_allowed or (target_team == source_team or (not _is_hostile(source_team, target_team) and target_team != NEUTRAL_TEAM and target_team >= 0))
		else:
			trait_tokens.append(token)
	if has_relation and not relation_allowed:
		return false
	var traits: Dictionary = {}
	for trait_value in _ability_object_kind_tokens(target):
		traits[String(trait_value).to_upper()] = true
	var source_rule := ((_rules.get("unit_rules", {}) as Dictionary).get(String(target.get("object_id", "")), {}) as Dictionary)
	if source_rule.is_empty():
		source_rule = ((_rules.get("unit_rules", {}) as Dictionary).get(String(target.get("unit_type", "")), {}) as Dictionary)
	for identity in [source_rule.get("source_object_id", ""), target.get("source_object_id", ""), target.get("object_id", ""), target.get("unit_type", "")]:
		if String(identity) != "":
			traits[String(identity).to_upper()] = true
	var accepted := trait_tokens.has("ALL") or trait_tokens.has("ANY")
	for token_value in trait_tokens:
		var token := String(token_value)
		if token.begins_with("-") and traits.has(token.substr(1)):
			return false
		if token.begins_with("+") and traits.has(token.substr(1)):
			accepted = true
	return accepted


func _dominate_enemy_convert(source: Dictionary, target: Dictionary, permanent: bool = true, temporary_duration_ticks: int = 0) -> void:
	var prior_team := int(target.get("team", -1))
	var new_team := int(source.get("team", -1))
	if prior_team == new_team:
		return
	if not target.has("dominated_from_team"):
		target["dominated_from_team"] = prior_team
	if permanent:
		target.erase("temporary_defect")
	else:
		if temporary_duration_ticks <= 0:
			return
		var original_team := int(target.get("dominated_from_team", prior_team))
		var existing := target.get("temporary_defect", {}) as Dictionary
		if not existing.is_empty():
			original_team = int(existing.get("original_team", original_team))
		target["temporary_defect"] = {
			"original_team": original_team,
			"defecting_team": new_team,
			"source_id": int(source.get("id", 0)),
			"started_tick": tick_index,
			"expires_tick": tick_index + temporary_duration_ticks,
			"duration_ticks": temporary_duration_ticks,
		}
	target["team"] = new_team
	target["target_id"] = 0
	target["target_kind"] = "battalion"
	target["destination"] = Vector2(target.get("position", Vector2.ZERO))
	target["route"] = []
	target["route_cells"] = []
	target["state"] = "idle"
	_clear_member_attack_schedule(target)
	_clear_member_targets(target)
	_emit_event("ability.unit_dominated", int(source.get("id", 0)), int(target.get("id", 0)), {"prior_team": prior_team, "team": new_team, "permanent": permanent, "restore_tick": tick_index + temporary_duration_ticks if not permanent else -1})


func _step_temporary_defect(row: Dictionary) -> void:
	var defect := row.get("temporary_defect", {}) as Dictionary
	if defect.is_empty():
		return
	if int(row.get("health", 0)) <= 0:
		row.erase("temporary_defect")
		_emit_event("ability.temporary_defect_ended", int(defect.get("source_id", 0)), int(row.get("id", 0)), {"reason": "target-dead", "restored": false})
		return
	if tick_index < int(defect.get("expires_tick", 0)):
		return
	var restored := int(row.get("team", -1)) == int(defect.get("defecting_team", -1))
	if restored:
		row["team"] = int(defect.get("original_team", -1))
		row["target_id"] = 0
		row["destination"] = Vector2(row.get("position", Vector2.ZERO))
		row["route"] = []
		row["route_cells"] = []
		row["state"] = "idle"
		_clear_member_attack_schedule(row)
		_clear_member_targets(row)
	row.erase("temporary_defect")
	row.erase("dominated_from_team")
	_emit_event("ability.temporary_defect_ended", int(defect.get("source_id", 0)), int(row.get("id", 0)), {"reason": "expired", "restored": restored, "team": int(row.get("team", -1))})


func _apply_ability_mount_toggle(hero_row: Dictionary, effect: Dictionary) -> Dictionary:
	return _abilities_subsystem()._apply_ability_mount_toggle(hero_row, effect)


func _apply_ability_special_disguise(row: Dictionary, effect: Dictionary) -> Dictionary:
	return _abilities_subsystem()._apply_ability_special_disguise(row, effect)


func special_disguise_opacity(row: Dictionary) -> float:
	var channel := row.get("special_disguise_channel", {}) as Dictionary
	if channel.is_empty():
		return 1.0
	var target := float(channel.get("opacity_target", 1.0))
	var phase := String(channel.get("phase", ""))
	if phase in ["preparation", "persistent-hold"]:
		return target
	if phase not in ["unpacking", "packing"]:
		return 1.0
	var start_tick := int(channel.get("phase_start_tick", tick_index))
	var end_tick := int(channel.get("phase_end_tick", start_tick))
	var progress := clampf(float(tick_index - start_tick) / float(maxi(1, end_tick - start_tick)), 0.0, 1.0)
	return lerpf(1.0, target, progress) if phase == "unpacking" else lerpf(target, 1.0, progress)


func _step_special_disguise(row: Dictionary) -> void:
	var channel := row.get("special_disguise_channel", {}) as Dictionary
	if channel.is_empty():
		return
	if int(row.get("health", 0)) <= 0:
		# The binary packet does not close death/respawn/reset abort ordering.
		# Freeze the channel and label the boundary instead of inventing a reset.
		row["special_disguise_deferred_boundary"] = "death-reset-ordering"
		return
	var phase := String(channel.get("phase", ""))
	if phase == "persistent-hold" or tick_index < int(channel.get("phase_end_tick", 0)):
		return
	match phase:
		"unpacking":
			channel["phase"] = "preparation"
			channel["phase_start_tick"] = tick_index
			channel["phase_end_tick"] = tick_index + int(channel.get("preparation_ticks", 1))
			row["special_disguise_channel"] = channel
		"preparation":
			_set_row_object_status(row, "DISGUISED", true)
			channel["phase"] = "persistent-hold"
			channel["phase_start_tick"] = tick_index
			# PersistentPrepTime is an authored hold cadence, not permission to
			# retrigger the disguise body every 250ms. The shipped override fires
			# once and remains held until cancel.
			channel["phase_end_tick"] = tick_index + int(channel.get("persistent_prep_ticks", 1))
			row["special_disguise_channel"] = channel
			_emit_special_disguise_presentation(row, "owner-disguised-presentation", String(channel.get("owner_disguise_template_id", "")), true)
			_emit_event("ability.special_disguise_triggered", int(row.get("id", 0)), 0, {
				"template_id": String(channel.get("owner_disguise_template_id", "")),
				"authoritative_object_id": String(row.get("unit_type", "")),
				"viewer_perspective": "deferred-owner-only",
			})
		"packing":
			row.erase("special_disguise_channel")
			row.erase("special_disguise_deferred_boundary")
			_emit_event("ability.special_disguise_packed", int(row.get("id", 0)), 0, {"opacity": 1.0})


func cancel_special_disguise(entity_id: int, reason: String = "explicit", suppress_exit_fx: bool = false) -> Dictionary:
	if not entities.has(entity_id):
		return {"ok": false, "reason": "unknown-entity"}
	return _cancel_special_disguise_row(entities[entity_id] as Dictionary, reason, suppress_exit_fx)


func _cancel_special_disguise_row(row: Dictionary, reason: String, suppress_exit_fx: bool) -> Dictionary:
	var channel := row.get("special_disguise_channel", {}) as Dictionary
	if channel.is_empty():
		return {"ok": false, "reason": "special-disguise-inactive"}
	if String(channel.get("phase", "")) == "packing":
		return {"ok": false, "reason": "special-disguise-already-packing"}
	_set_row_object_status(row, "DISGUISED", false)
	channel["phase"] = "packing"
	channel["phase_start_tick"] = tick_index
	channel["phase_end_tick"] = tick_index + int(channel.get("pack_ticks", 1))
	row["special_disguise_channel"] = channel
	_emit_special_disguise_presentation(row, "owner-mounted-presentation", String(channel.get("owner_object_id", "")), false)
	var exit_fx := "" if suppress_exit_fx else String(channel.get("disguise_fx_id", ""))
	_emit_event("ability.special_disguise_cancelled", int(row.get("id", 0)), 0, {
		"reason": reason, "suppress_exit_fx": suppress_exit_fx,
		"exit_fx_id": exit_fx, "phase_end_tick": int(channel.get("phase_end_tick", 0)),
	})
	return {"ok": true, "reason": "", "effect": "special-disguise-cancel", "phase": "packing", "exit_fx_id": exit_fx}


func _emit_special_disguise_presentation(row: Dictionary, role: String, template_id: String, force_mounted: bool) -> void:
	_emit_event("ability.special_disguise_presentation", int(row.get("id", 0)), 0, {
		"role": role, "template_id": template_id,
		"authoritative_object_id": String(row.get("unit_type", "")),
		"opacity": special_disguise_opacity(row),
		"force_mounted": force_mounted,
		"viewer_perspective": "deferred-owner-only",
		"presentation_prerequisite_sha256": String((row.get("special_disguise_channel", {}) as Dictionary).get("presentation_prerequisite_sha256", "")),
	})


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
	return _abilities_subsystem()._apply_ability_capture_building(hero_row, effect, target_point)


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
		_award_auto_deposit_capture(structure, team)
		_transfer_linked_capture(structure, team)
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
	return _abilities_subsystem()._apply_ability_terror(hero_row, ability_id, effect)


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
	_spatial_sync(row)
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
	return _abilities_subsystem()._apply_ability_summon(hero_row, effect, point)


func _apply_ability_summon_chain(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	return _abilities_subsystem()._apply_ability_summon_chain(team, effect, point)


func _summon_unit_type_for(source_object_id: String) -> String:
	var member_id := PlayableUnitAdapter.runtime_object_id(source_object_id)
	for unit_type_value in _unit_production_rules.keys():
		if String((_unit_production_rules[unit_type_value] as Dictionary).get("object_id", "")) == member_id:
			return String(unit_type_value)
	return ""


func _apply_ability_experience_grant(hero_row: Dictionary, effect: Dictionary, point: Vector2) -> Dictionary:
	return _experience_subsystem().apply_ability_experience_grant(hero_row, effect, point)


func _apply_ability_arrow_storm(hero_row: Dictionary, effect: Dictionary, point: Vector2) -> Dictionary:
	return _abilities_subsystem()._apply_ability_arrow_storm(hero_row, effect, point)


func _step_volley_channel(row: Dictionary) -> bool:
	## Advance one entity's arrow-storm volley. Returns true while the channel
	## holds the hero in place (the entity step then goes no further). Any
	## later order (route or target) cancels the volley outright.
	var channel: Dictionary = row.get("volley_channel", {}) as Dictionary
	if channel.is_empty():
		return false
	if not (row["route"] as Array).is_empty() or int(row.get("target_id", 0)) != 0:
		row.erase("volley_channel")
		_emit_event("ability.volley_cancelled", int(row.get("id", 0)), 0, {"team": int(row.get("team", -1))})
		return false
	if tick_index >= int(channel.get("next_shot_tick", 0)):
		var team := int(row.get("team", -1))
		var point := Vector2(channel.get("point", Vector2.ZERO))
		var enemy_ids := _ability_enemies_near(team, point, float(channel.get("radius", 0.0)))
		if enemy_ids.is_empty() and not bool(channel.get("can_shoot_empty_ground", false)):
			row.erase("volley_channel")
			row["state"] = "idle"
			_emit_event("ability.volley_complete", int(row.get("id", 0)), 0, {"team": team, "shots": int(channel.get("shots_fired", 0))})
			return false
		var shots := mini(int(channel.get("shots_per_burst", 1)), int(channel.get("shots_left", 0)))
		var damage := int(channel.get("damage", 0))
		var fired := int(channel.get("shots_fired", 0))
		for shot_index in range(shots):
			if enemy_ids.is_empty():
				continue
			var target_id := int(enemy_ids[(fired + shot_index) % enemy_ids.size()])
			if entities.has(target_id) and int((entities[target_id] as Dictionary).get("health", 0)) > 0:
				_apply_damage(int(row.get("id", 0)), target_id, damage, "battalion")
		channel["shots_fired"] = fired + shots
		channel["shots_left"] = int(channel.get("shots_left", 0)) - shots
		channel["next_shot_tick"] = tick_index + int(channel.get("interval_ticks", 1))
		if int(channel.get("shots_left", 0)) <= 0:
			row.erase("volley_channel")
			row["state"] = "idle"
			_emit_event("ability.volley_complete", int(row.get("id", 0)), 0, {"team": team, "shots": fired + shots})
			return false
		row["volley_channel"] = channel
	row["state"] = "volley"
	row["current_speed"] = 0.0
	return true


func _apply_ability_stealth_toggle(hero_row: Dictionary, effect: Dictionary) -> Dictionary:
	return _abilities_subsystem()._apply_ability_stealth_toggle(hero_row, effect)


func _stealth_active(row: Dictionary) -> bool:
	return tick_index < int(row.get("stealth_until_tick", -1)) and tick_index >= int(row.get("detected_until_tick", -1))


func _grant_stealth(row: Dictionary, until_tick: int, forbidden: Array) -> void:
	row["stealth_until_tick"] = until_tick
	row["stealth_forbidden"] = forbidden.duplicate()
	_emit_event("ability.stealth", int(row.get("id", 0)), 0, {"engaged": true, "until_tick": until_tick})


func _clear_stealth(row: Dictionary) -> void:
	if not row.has("stealth_until_tick"):
		return
	row.erase("stealth_until_tick")
	row.erase("stealth_forbidden")
	_emit_event("ability.stealth", int(row.get("id", 0)), 0, {"engaged": false})


func _break_stealth(row: Dictionary, condition: String) -> void:
	## Break an active cloak when its authored ForbiddenConditions include the
	## triggering condition (TAKING_DAMAGE / FIRING_ANY / USING_ABILITY).
	if not _stealth_active(row):
		return
	if not (row.get("stealth_forbidden", []) as Array).has(condition):
		return
	var policy := row.get("invisibility_update", {}) as Dictionary
	if (
		not policy.is_empty()
		and (policy.get("forbidden_conditions", []) as Array).has(condition)
		and (policy.get("options", []) as Array).has("UNTOGGLE_HIDDEN_WHEN_LEAVING_STEALTH")
	):
		policy["enabled"] = false
		var object_id := int(row.get("id", 0))
		_revoke_invisibility_policy_sources(object_id, row, policy)
		row["invisibility_update"] = policy
	_clear_stealth(row)


func _apply_ability_teleport(hero_row: Dictionary, effect: Dictionary, point: Vector2) -> Dictionary:
	return _abilities_subsystem()._apply_ability_teleport(hero_row, effect, point)


func _apply_ability_curse(hero_row: Dictionary, effect: Dictionary, point: Vector2) -> Dictionary:
	return _abilities_subsystem()._apply_ability_curse(hero_row, effect, point)


func _apply_ability_leadership_strip(hero_row: Dictionary, effect: Dictionary) -> Dictionary:
	return _abilities_subsystem()._apply_ability_leadership_strip(hero_row, effect)


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
	## COVERAGE, DECIDED AND RECORDED 2026-08-04 (round 18) — do not "fix" this
	## silently in either direction.
	##
	## This multiplier is consulted at exactly ONE site: the per-member melee/
	## ranged swing in _step_attacks. It therefore does NOT ride
	##   - upgrade-gated bonus nuggets (fire-arrow flame components),
	##   - hero cleave splash,
	##   - trample,
	##   - ability/power direct damage.
	## That is consistent with SpellBookDarkness and every hero leadership aura,
	## which enter through the same accumulator, and it is narrower than the
	## Rallying Call `rally_factor`, which rides inside _apply_damage and so
	## scales everything.
	##
	## What retail means: attributemodifier.ini authors DAMAGE_MULT with the
	## comment "Multiplicitive. Damage multiplied by this, will compound in
	## multiple bonuses", i.e. the object's damage OUTPUT — all of it. So the
	## correct model is the wide one, and this narrow model is a known gap.
	## OpenSAGE is NOT an oracle here: its AttributeModifier.Apply
	## (Logic/ModifierList.cs:39-52) implements only Production and Health and
	## drops DAMAGE_MULT on the floor, so it cannot arbitrate the scope.
	##
	## NOT widened in this round, deliberately: moving it into _apply_damage
	## moves every hero-leadership damage number in the game (leadership auras
	## write DAMAGE_MULT into this same table and are live throughout the pinned
	## slice scenarios), so it needs its own failing-first evidence and a
	## conscious pin re-mint rather than riding along with a parser fix. Round 16
	## reached the current narrow coverage as a side effect of moving taint off
	## its private row fields; this comment is the record that the narrowing was
	## noticed and accepted, not that it was chosen as correct.
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
			# An aura reaches a bounded radius, so only that neighbourhood of the
			# owning team can receive a grant. The hero itself always lands in
			# the gathered set (distance zero), preserving the AffectsSelf seam.
			for ally_id in _spatial_gather_sorted(origin, range_limit):
				if not entities.has(ally_id):
					continue
				var ally: Dictionary = entities[ally_id]
				if int(ally.get("team", -1)) != team or int(ally.get("health", 0)) <= 0:
					continue
				if ally_id == id:
					if not bool(effect.get("affectsSelf", false)):
						continue
				elif Vector2(ally.get("position", Vector2.ZERO)).distance_to(origin) > range_limit:
					continue
				if not _ability_filter_accepts(ally, filter_text):
					continue
				if tick_index < _refresh_leadership_suppression(ally):
					# An anti-category strip (Horn of Gondor) suppresses new
					# leadership grants for its authored duration.
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
		_step_temporary_defect(row)
		_step_activate_module_graph(row)
		_step_weapon_mode_special_powers(row)
		_step_dominate_enemy(row)
		_step_grab_passenger(row)
		_step_fling_passenger(row)
		_step_repair_structure(row)
		_step_toggle_deploy(row)
		_step_siege_deploy(row)
		_step_special_disguise(row)
		# Expiry sweeps for the per-row ability fields (exact tick, then the
		# field leaves the row so default rows never carry it).
		if row.has("stealth_until_tick") and tick_index >= int(row["stealth_until_tick"]):
			_clear_stealth(row)
		_refresh_leadership_suppression(row)
		if row.has("ability_hold_until_tick") and tick_index >= int(row["ability_hold_until_tick"]):
			row.erase("ability_hold_until_tick")
		var table: Dictionary = row.get("timed_modifiers", {}) as Dictionary
		if table.is_empty():
			continue
		var expired: Array[String] = []
		for key_value in table.keys():
			if bool((table[key_value] as Dictionary).get("persistent", false)):
				continue
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


func _step_entity(id: int) -> void:
	var row: Dictionary = entities[id]
	# The carrier is a real damageable simulation entity, but its movement and
	# presentation are owned by its horde; it never runs independent orders/AI.
	if bool(row.get("is_banner_carrier", false)):
		return
	if int(row["health"]) <= 0:
		row["state"] = "death"
		_clear_pending_route(row, true)
		_rearm_mood_idle_cadence(row)
		return
	if row.has("grab_passenger_channel") or row.has("fling_passenger_channel") or row.has("repair_structure_channel"):
		row["current_speed"] = 0.0
		row["state"] = "ability"
		_rearm_mood_idle_cadence(row)
		return
	if not (row.get("dominate_enemy_channel", {}) as Dictionary).is_empty():
		# DominateEnemySpecialPower owns locomotion for unpack/preparation and
		# the authored post-trigger AI freeze. The ability step resolves its
		# interrupt/trigger/finish boundary deterministically after this pass.
		row["current_speed"] = 0.0
		row["state"] = "ability"
		_rearm_mood_idle_cadence(row)
		return
	if tick_index < int(row.get("cower_until_tick", -1)):
		row["current_speed"] = 0.0
		row["state"] = "cower"
		_rearm_mood_idle_cadence(row)
		return
	elif row.has("cower_until_tick"):
		row.erase("cower_until_tick")
		row["state"] = "idle"
	if entity_container.has(id):
		var horde_ai := row.get("horde_ai_update", {}) as Dictionary
		if horde_ai.is_empty():
			_attach_module_contracts(row)
			horde_ai = row.get("horde_ai_update", {}) as Dictionary
		# Contained hordes are position-owned by the carrier. They may execute
		# weapon logic only when retail authors the opt-in; otherwise they are
		# inert. Route clearing prevents independent movement in either case.
		_clear_pending_route(row, true)
		row["attack_move"] = false
		row["current_speed"] = 0.0
		var carrier_id := int(entity_container.get(id, 0))
		if structures.has(carrier_id):
			row["position"] = Vector2((structures[carrier_id] as Dictionary).get("position", row.get("position", Vector2.ZERO)))
		if not bool(row.get("contained_can_attack", false)) and not bool(horde_ai.get("can_attack_while_contained", false)):
			row["state"] = "contained"
			return
	if tick_index < int(row.get("stun_until_tick", -1)):
		# Cloud Break disruption: the battalion holds position and cannot act
		# until the authored weather duration elapses.
		row["current_speed"] = 0.0
		_rearm_mood_idle_cadence(row)
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
			_emit_event("combat.stand_up", id, 0)
			# Standing back up resumes the order the charge interrupted, from
			# the spot the battalion was thrown to. Without this a trampled
			# battalion stands up ownerless and idle, so the player's attack
			# order is destroyed by the first hoof that touches it.
			if not _resume_order_after_knockdown(row):
				row["state"] = "idle"
		else:
			row["state"] = "knocked_down"
		_rearm_mood_idle_cadence(row)
		return
	row["attack_cooldown"] = maxi(0, int(row["attack_cooldown"]) - 1)
	if not (row.get("activate_module_channel", {}) as Dictionary).is_empty():
		# ActivateModuleSpecialPower owns locomotion through its authored
		# unpack/prep/duration/pack envelope. A voluntary order is observed and
		# resolved by _step_activate_module_graph after this entity pass; a
		# MustFinish graph keeps the order queued and resumes it after finish.
		row["current_speed"] = 0.0
		row["state"] = "ability"
		_rearm_mood_idle_cadence(row)
		return
	if not (row.get("siege_deploy_channel", {}) as Dictionary).is_empty():
		row["current_speed"] = 0.0
		row["state"] = "deployed" if String((row.get("siege_deploy_channel", {}) as Dictionary).get("phase", "")) == "deployed" else "ability"
		_rearm_mood_idle_cadence(row)
		return
	if _step_capture_channel(row):
		_rearm_mood_idle_cadence(row)
		return
	if _step_volley_channel(row):
		_rearm_mood_idle_cadence(row)
		return
	if tick_index < int(row.get("ability_hold_until_tick", -1)):
		# Authored post-ability busy envelope (teleport BusyForDuration): the
		# hero holds this tick; accepted orders resume once the hold expires.
		row["current_speed"] = 0.0
		row["state"] = "idle"
		_rearm_mood_idle_cadence(row)
		return
	if _step_production_exit(row):
		_rearm_mood_idle_cadence(row)
		return
	if bool(row.get("is_builder", false)):
		_rearm_mood_idle_cadence(row)
		var construction_id := int(row.get("construction_id", 0))
		if construction_id != 0 and structures.has(construction_id):
			var site: Dictionary = structures[construction_id]
			if float(site.get("construction_progress", 1.0)) < 1.0:
				var site_position := Vector2(site.get("position", row["position"]))
				# A successful navigation query may end at the closest walkable
				# perimeter cell rather than the structure's obstructed center.
				# Exhausting that accepted route is arrival; requiring another
				# invented center-distance strands real-map construction sites.
				# Arrival is the FOOTPRINT EDGE, not a flat 2.0 from the centre: a
				# route that ends at the centre (no nav-grid obstruction for the
				# fresh site) leaves the porter pressed against the site's own
				# collision disc - placement radius plus its body - and a
				# centre-distance rule strands it there forever with the order
				# still armed (Angmar porter, second fortress: stuck 5.2 out).
				# Retail porters build from the foundation's edge.
				var footprint := _structure_placement_radius(String(site.get("structure_kind", "")))
				var arrival_reach := maxf(2.0, footprint + 2.0)
				if String(row.get("order_kind", "")) == "construct" and ((row["route"] as Array).is_empty() or Vector2(row["position"]).distance_to(site_position) <= arrival_reach):
					_clear_pending_route(row, true)
					row["state"] = "construct"
					return
		if not (row["route"] as Array).is_empty():
			_step_route(row)
		else:
			row["state"] = "idle"
		return
	var target_id := int(row["target_id"])
	if (
		target_id != 0
		or not (row["route"] as Array).is_empty()
		or bool(row.get("attack_move", false))
	):
		_rearm_mood_idle_cadence(row)
	if target_id == 0 and (row["route"] as Array).is_empty() and not bool(row.get("attack_move", false)):
		var scan_now := true
		var mood_rate := int(row.get("mood_attack_check_rate_ticks", 0))
		if mood_rate > 0:
			var policy_allows_idle_scan := (
				bool(row.get("auto_acquire_enabled", true))
				and (
					not _stealth_active(row)
					or bool(row.get("auto_acquire_while_stealthed", true))
				)
			)
			scan_now = (
				policy_allows_idle_scan
				and tick_index >= int(row.get("mood_next_check_tick", -1))
			)
		if scan_now:
			var auto_target := _nearest_auto_target(row)
			if mood_rate > 0:
				var interval := mood_rate
				if bool(row.get("mood_randomize_next_check", true)):
					var half_rate := mood_rate >> 1
					interval += logic_random_int(-half_rate, half_rate)
					row["mood_randomize_next_check"] = false
				row["mood_next_check_tick"] = tick_index + maxi(1, interval)
			if not auto_target.is_empty():
				target_id = int(auto_target["id"])
				row["target_id"] = target_id
				row["target_kind"] = String(auto_target["kind"])
				row["order_kind"] = "auto_attack"
				row["auto_attack_origin"] = row.get("position", Vector2.ZERO)
	if target_id == 0 and bool(row.get("attack_move", false)) and not (row["route"] as Array).is_empty():
		var acquired := _nearest_attack_move_target(row)
		if acquired != 0:
			row["target_id"] = acquired
			row["target_kind"] = "battalion"
			target_id = acquired
	if target_id != 0:
		var target_kind := String(row.get("target_kind", "battalion"))
		if String(row.get("order_kind", "")) == "auto_attack":
			var ai_policy := row.get("ai_update_interface", {}) as Dictionary
			var stop_source := float(ai_policy.get("stop_chase_distance_source", 0.0))
			var source_scale := float(_rules.get("source_map_transform_scale", 1.0))
			var stop_distance := stop_source * (source_scale if source_scale > 0.0 else 1.0)
			if stop_distance > 0.0 and Vector2(row.get("position", Vector2.ZERO)).distance_to(Vector2(row.get("auto_attack_origin", row.get("position", Vector2.ZERO)))) > stop_distance:
				row["target_id"] = 0
				row["target_kind"] = "battalion"
				row["order_kind"] = ""
				row.erase("auto_attack_origin")
				_clear_pending_route(row, true)
				row["state"] = "idle"
				return
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
		var auto_target_invalid := (
			String(row.get("order_kind", "")) == "auto_attack"
			and (
				not bool(row.get("auto_acquire_enabled", true))
				or (
					_stealth_active(row)
					and not bool(row.get("auto_acquire_while_stealthed", true))
				)
				or (
					target_kind == "battalion"
					and _stealth_active(entities[target_id] as Dictionary)
				)
				or (
					target_kind == "structure"
					and not bool(row.get("auto_acquire_attack_buildings", true))
				)
			)
		)
		if auto_target_invalid:
			# Re-evaluate an AUTO target for temporal policy changes (source or
			# target cloaking) without touching explicit attack/retaliation
			# orders. A later tick may reacquire after the condition clears.
			row["target_id"] = 0
			row["target_kind"] = "battalion"
			row["attack_windup"] = 0
			_clear_member_attack_schedule(row)
			_clear_member_targets(row)
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
		# SURFACE-TO-SURFACE against a structure; centre-to-centre otherwise.
		#
		# This used to be centre-to-centre unconditionally, copied from a partial
		# open-source reimplementation that compares raw translation distance
		# against AttackRange with no bounding-radius expansion. The original
		# engine instead asks its partition manager for a bounding-sphere-to-
		# bounding-sphere distance, i.e. the GAP between the two footprints.
		#
		# MEASURED consequence of the simplification, in the live slice: a melee
		# horde ordered onto the enemy fortress ended in state `attack` at d=0.24
		# and then d=0.00 — standing ON the fortress centre — because a 0.305
		# AttackRange against a 1.96-radius footprint is only satisfiable at the
		# centre. Melee is supposed to stand at the wall and swing. Bracketed
		# from both sides by _test_structure_surface_range in
		# game/tests/banner_castle_sim_runner.gd.
		#
		# See _target_footprint_radius for why the attacker's own radius is NOT
		# subtracted (it is a horde centre, not a soldier).
		var distance := maxf(
			0.0,
			Vector2(row["position"]).distance_to(target_position)
			- _target_footprint_radius(target_id, target_kind)
		)
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
			if entity_container.has(id):
				row["state"] = "contained"
				return
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
			if entity_container.has(id):
				row["state"] = "contained"
				return
			if not _assign_target_route(row, target_position):
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


func structure_sell_command(structure_id: int) -> Dictionary:
	## Compiled Command_Sell row for this structure, or {} if the mounted docs
	## do not author a sell slot. Command_Sell is slot 6 of every Men production
	## set and the whole of SellableCommandSet (commandset.ini:5771).
	if not structures.has(structure_id):
		return {}
	var building: Dictionary = structures[structure_id]
	if int(building.get("health", 0)) <= 0:
		return {}
	# farm.ini:34 authors CommandSet = SellableCommandSet on the object with
	# no under-construction override. SAGE exposes SELL for the building's
	# whole life; refund is still SellPercentage of the already-paid cost.
	var slot := _compiled_sell_slot_for(building)
	if slot.is_empty():
		return {}
	var kind := String(building.get("structure_kind", ""))
	var team := int(building.get("team", -1))
	var cost := int((structure_build_rules_for_team(team).get(kind, {}) as Dictionary).get("cost", 0))
	if cost <= 0:
		cost = int((_expansion_build_rules.get(kind, {}) as Dictionary).get("cost", 0))
	# gamedata.ini:8973 SellPercentage = 50%.
	var refund := int(cost / 2)
	return {
		"command_id": String(slot.get("commandId", "Command_Sell")),
		"slot": int(slot.get("slot", 6)),
		"refund": refund,
	}


func sell_structure(team: int, structure_id: int) -> Dictionary:
	## Retail SELL (commandbutton.ini:3554): raze the building and refund
	## SellPercentage of its authored build cost. Presentation follows the
	## ordinary health=0 / structure.destroyed path.
	if not base_loop_enabled or winner != -1:
		return {"ok": false, "reason": "match-unavailable"}
	if not structures.has(structure_id):
		return {"ok": false, "reason": "unknown-structure"}
	var building: Dictionary = structures[structure_id]
	if int(building.get("team", -1)) != team:
		return {"ok": false, "reason": "wrong-owner"}
	if int(building.get("health", 0)) <= 0:
		return {"ok": false, "reason": "structure-unavailable"}
	var sell: Dictionary = structure_sell_command(structure_id)
	if sell.is_empty():
		return {"ok": false, "reason": "no-sell-command"}
	var refund := int(sell.get("refund", 0))
	team_resources[team] = resources_for_team(team) + refund
	building["health"] = 0
	building["queue"] = []
	building["upgrade_queue"] = []
	# Detach the porter so a later construct does not treat this husk as a
	# cancellable site (that path refunds the full build cost).
	var builder_id := int(building.get("builder_id", 0))
	if builder_id != 0 and entities.has(builder_id):
		var builder: Dictionary = entities[builder_id]
		if int(builder.get("construction_id", 0)) == structure_id:
			builder["construction_id"] = 0
			if String(builder.get("order_kind", "")) == "construct":
				builder["order_kind"] = ""
			if String(builder.get("state", "")) == "construct":
				builder["state"] = "idle"
	building["builder_id"] = 0
	_clear_expansion_pad_occupant(structure_id)
	var structure_kind := String(building.get("structure_kind", ""))
	_emit_event("structure.sold", 0, structure_id, {
		"team": team,
		"refund": refund,
		"structure_kind": structure_kind,
	})
	_emit_event("structure.destroyed", 0, structure_id, {
		"reason": "sold",
		"structure_kind": structure_kind,
		"team": team,
	})
	return {"ok": true, "refund": refund, "structure_id": structure_id}


func structure_command_slot(structure_id: int, command_id: String) -> int:
	## Authored palantir slot for a command on this building's current compiled
	## command set, or 0 when the docs do not place it.
	if command_id == "" or not structures.has(structure_id):
		return 0
	for slot_value in _compiled_command_slots_for(structures[structure_id]):
		if typeof(slot_value) != TYPE_DICTIONARY:
			continue
		var slot: Dictionary = slot_value
		if String(slot.get("commandId", "")) == command_id:
			return int(slot.get("slot", 0))
	return 0


func _compiled_sell_slot_for(building: Dictionary) -> Dictionary:
	for slot_value in _compiled_command_slots_for(building):
		if typeof(slot_value) != TYPE_DICTIONARY:
			continue
		var slot: Dictionary = slot_value
		if String(slot.get("commandId", "")) == "Command_Sell":
			return slot
	return {}


func _compiled_command_slots_for(building: Dictionary) -> Array:
	# Scenario structures carry the exact selected-pack command-set projection on
	# the instance. It stays outside the faction structure registry, but ownership
	# transitions still expose the authored defected-lair command surface.
	var scenario_sets := building.get("scenario_trained_command_sets", []) as Array
	if not scenario_sets.is_empty():
		var active_id := String(building.get("command_set_id", building.get("default_command_set_id", "")))
		for set_value in scenario_sets:
			if typeof(set_value) != TYPE_DICTIONARY:
				continue
			var command_set := set_value as Dictionary
			if String(command_set.get("id", "")) == active_id:
				return command_set.get("slots", []) as Array
		return []
	var candidates: Array[String] = []
	var stamped := String(building.get("source_object_id", ""))
	if stamped != "":
		candidates.append(stamped)
	var kind := String(building.get("structure_kind", ""))
	var aliases: Variant = structure_source_object_ids_for_team(int(building.get("team", -1))).get(kind, [])
	if typeof(aliases) == TYPE_ARRAY:
		for alias_value in aliases as Array:
			var alias_id := String(alias_value)
			if alias_id != "" and not candidates.has(alias_id):
				candidates.append(alias_id)
	elif typeof(aliases) in [TYPE_STRING, TYPE_STRING_NAME]:
		var alias_id := String(aliases)
		if alias_id != "" and not candidates.has(alias_id):
			candidates.append(alias_id)
	var db = _content_db_ref()
	if db == null or not db.has_method("get_playable_structure_runtime"):
		return []
	for object_id in candidates:
		var document: Dictionary = db.get_playable_structure_runtime(object_id)
		if document.is_empty():
			continue
		var sets: Array = (
			((document.get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary)
			.get("trainedCommandSets", [])
		) as Array
		var slots := _direct_or_first_command_set_slots(sets)
		if not slots.is_empty():
			return slots
	return []


func _direct_or_first_command_set_slots(sets: Array) -> Array:
	for set_value in sets:
		if typeof(set_value) != TYPE_DICTIONARY:
			continue
		var row: Dictionary = set_value
		if String(row.get("kind", "")) == "direct":
			return row.get("slots", []) as Array
	if sets.is_empty() or typeof(sets[0]) != TYPE_DICTIONARY:
		return []
	return (sets[0] as Dictionary).get("slots", []) as Array


func _clear_expansion_pad_occupant(structure_id: int) -> void:
	for fortress_id_value in expansion_pads.keys():
		var pads: Array = expansion_pads[fortress_id_value] as Array
		for pad_value in pads:
			if typeof(pad_value) != TYPE_DICTIONARY:
				continue
			var pad: Dictionary = pad_value
			if int(pad.get("expansion_structure_id", 0)) == structure_id:
				pad["expansion_structure_id"] = 0


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
const MAX_STRUCTURE_PLACEMENT_RADIUS := 4.0


func _structure_placement_radius(structure_kind: String) -> float:
	return float(STRUCTURE_PLACEMENT_RADII.get(structure_kind, 2.4))


func _authored_structure_placement_radius(team: int, structure_kind: String) -> float:
	var sources: Variant = structure_source_object_ids_for_team(team).get(structure_kind, [])
	var source_object_id := ""
	if typeof(sources) == TYPE_ARRAY and not (sources as Array).is_empty():
		source_object_id = String((sources as Array)[0])
	elif typeof(sources) in [TYPE_STRING, TYPE_STRING_NAME]:
		source_object_id = String(sources)
	if source_object_id != "":
		var authored_radius := _structure_footprint_radius({
			"source_object_id": source_object_id,
			"structure_kind": structure_kind,
		})
		if authored_radius > 0.0:
			return authored_radius
	return _structure_placement_radius(structure_kind)


func _authored_site_foundation_fixture_contains(fixture: Dictionary, site: Vector2) -> bool:
	# An authored site may coincide with its own foundation/build-plot marker,
	# but never gets a blanket exemption from the surrounding keep. Both the
	# fixture identity and its actual compiled footprint must prove occupancy.
	var role := String(fixture.get("castle_fixture_role", "")).to_lower()
	var fixture_type := String(fixture.get("castle_fixture_type", "")).to_lower()
	var is_foundation := (
		role.contains("foundation")
		or role.contains("build-plot")
		or role.contains("build_plot")
		or fixture_type.contains("foundation")
		or fixture_type.contains("buildplot")
		or fixture_type.contains("build_plot")
	)
	if not is_foundation:
		return false
	var footprint := _structure_footprint_radius(fixture)
	return footprint > 0.0 and Vector2(fixture.get("position", Vector2.ZERO)).distance_to(site) <= footprint


func _issue_construct_for_team(
	team: int,
	ids: Array[int],
	structure_kind: String,
	position: Vector2,
	dry_run: bool = false,
	authored_castle_site: bool = false
) -> Dictionary:
	if not base_loop_enabled or winner != -1:
		return {"ok": false, "reason": "match-unavailable"}
	if team != PLAYER_TEAM and team != ENEMY_TEAM:
		return {"ok": false, "reason": "invalid-team"}
	# The constructing team's OWN faction tables (identical to the globals in
	# the default single-manifest match; a cross-faction guest builds its own
	# faction's structures at its own costs).
	var team_structure_build_rules := structure_build_rules_for_team(team)
	var team_structure_max_health := structure_max_health_for_team(team)
	if not team_structure_build_rules.has(structure_kind):
		return {"ok": false, "reason": "unsupported-structure"}
	var permission := building_permission_for_kind(team, structure_kind)
	if not bool(permission.get("known", false)):
		return {
			"ok": false,
			"reason": "building-permission-identity-unresolved",
			"detail": String(permission.get("reason", "")),
		}
	if not bool(permission.get("allowed", false)):
		return {
			"ok": false,
			"reason": "building-disallowed",
			"object_type": String(permission.get("object_type", "")),
		}
	# BFME1 build-plots-only: construction is restricted to designated empty
	# plots. The click must land on a free plot; the build then snaps to the
	# plot's center and skips the freeform geometry checks (plot positions are
	# predetermined and valid). Occupancy is claimed after the site is created.
	var build_plot_index := -1
	if build_plots_only:
		build_plot_index = _free_build_plot_index_near(team, position)
		if build_plot_index < 0:
			return {"ok": false, "reason": "build-plots-only: pick an empty plot"}
		position = Vector2((build_plots[team] as Array)[build_plot_index].get("position", position))
	else:
		if playable_outline.size() >= 3 and not Geometry2D.is_point_in_polygon(position, playable_outline):
			return {"ok": false, "reason": "outside-playable-area"}
		var new_radius := (
			_authored_structure_placement_radius(team, structure_kind)
			if authored_castle_site
			else _structure_placement_radius(structure_kind)
		)
		# The spatial query is exact: no existing footprint farther away than the
		# two maximum authored radii plus the authored clearance can overlap this
		# site. This matters on castle maps, whose hundreds of wall fixtures made
		# every fallback candidate repeat a full structure-table scan.
		var gather_radius := new_radius + MAX_STRUCTURE_PLACEMENT_RADIUS + PLACEMENT_CLEARANCE_MARGIN
		var exempted_foundation_id := 0
		for existing_id in _structure_ids_within_gather_radius(position, gather_radius):
			var existing_row: Dictionary = structures[existing_id] as Dictionary
			# Exempt at most the one foundation marker whose own footprint contains
			# the authored point. Walls, gates, towers, other foundations, and every
			# live/dynamic structure retain normal clearance.
			if (
				authored_castle_site
				and exempted_foundation_id == 0
				and existing_id >= CASTLE_FIXTURE_FIRST_ID
				and _authored_site_foundation_fixture_contains(existing_row, position)
			):
				exempted_foundation_id = existing_id
				continue
			var existing_position := Vector2(existing_row.get("position", Vector2.ZERO))
			var existing_radius := _structure_placement_radius(String(existing_row.get("structure_kind", "")))
			var clearance_margin := PLACEMENT_CLEARANCE_MARGIN
			if existing_id >= CASTLE_FIXTURE_FIRST_ID:
				var fixture_radius := _structure_footprint_radius(existing_row)
				if fixture_radius > 0.0:
					existing_radius = fixture_radius
				# Imported fixtures have exact authored footprints and no builder
				# working apron. The margin remains for live/dynamic structures.
				clearance_margin = 0.0
			var clearance := new_radius + existing_radius + clearance_margin
			if existing_position.distance_to(position) < clearance:
				return {
					"ok": false,
					"reason": "site-obstructed",
					"obstruction_id": existing_id,
					"obstruction_type": String(existing_row.get("castle_fixture_type", existing_row.get("structure_kind", ""))),
					"obstruction_role": String(existing_row.get("castle_fixture_role", "")),
					"obstruction_distance": existing_position.distance_to(position),
					"required_clearance": clearance,
				}
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
	var build_rule: Dictionary = team_structure_build_rules[structure_kind]
	var cost := int(build_rule["cost"])
	if resources_for_team(team) < cost:
		return {"ok": false, "reason": "insufficient-resources", "cost": cost}
	if dry_run:
		return {"ok": true, "reason": "", "dry_run": true, "cost": cost}
	var structure_id := _next_dynamic_structure_id
	_next_dynamic_structure_id += 1
	var maximum_health := int(team_structure_max_health[structure_kind])
	var production: Array[String] = []
	var construct_production_order := production_unit_order_for_team(team)
	var construct_production_rules := unit_production_rules_for_team(team)
	var construct_scope := production_scope_for_team(team)
	for unit_type in construct_production_order:
		if not construct_scope.is_empty() and not construct_scope.has(String(unit_type)):
			continue
		var production_rule: Dictionary = construct_production_rules.get(unit_type, {}) as Dictionary
		var producer_kinds_for_rule: Array = production_rule.get("producer_kinds", [String(production_rule.get("producer_kind", ""))])
		if producer_kinds_for_rule.has(structure_kind):
			production.append(unit_type)
	var build_ticks := maxi(1, roundi(float(build_rule["seconds"]) / TICK_SECONDS))
	_note_structure_table_mutation()
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
	# A building the player RAISES is the same retail object as the one the map
	# seeds (_seed_structures) or a flag unpacks (unpack_base): both of those
	# stamp the faction's authored source id and this path did not, so a
	# constructed structure came up with no retail identity at all -- which is
	# what left a built fortress unable to unpack its castle and therefore
	# showing an empty command wheel.
	#
	# Snapshot-inert (state_signature carries no source id), so the
	# cross-platform pin is untouched. It is NOT inert in general -- the id also
	# feeds _structure_footprint_radius and object-id script queries -- but the
	# footprint half is measured to be a no-op on the mounted packs; see the
	# note in _seed_structures.
	var constructed_sources: Variant = structure_source_object_ids_for_team(team).get(structure_kind, [])
	if typeof(constructed_sources) == TYPE_ARRAY and not (constructed_sources as Array).is_empty():
		structures[structure_id]["source_object_id"] = String((constructed_sources as Array)[0])
	elif typeof(constructed_sources) in [TYPE_STRING, TYPE_STRING_NAME]:
		structures[structure_id]["source_object_id"] = String(constructed_sources)
	_stamp_refund_die_creation_cost(structures[structure_id] as Dictionary, cost)
	_mark_ring_delivery_structure(structures[structure_id] as Dictionary)
	if bool(build_rule.get("highlander_body", false)):
		structures[structure_id]["highlander_body"] = true
	team_resources[team] = resources_for_team(team) - cost
	var builder: Dictionary = entities[builder_id]
	var previous_site_id := int(builder.get("construction_id", 0))
	if previous_site_id != 0 and structures.has(previous_site_id):
		# Redirecting a busy builder cancels its unfinished site with a full
		# refund; otherwise the site would linger as unfinishable scaffolding.
		var previous_site: Dictionary = structures[previous_site_id]
		if float(previous_site.get("construction_progress", 1.0)) < 1.0:
			var previous_team := int(previous_site.get("team", team))
			team_resources[previous_team] = resources_for_team(previous_team) + int(structure_build_rules_for_team(previous_team).get(String(previous_site.get("structure_kind", "")), {}).get("cost", 0))
			structures.erase(previous_site_id)
			_note_structure_table_mutation()
			_emit_event("construction.cancelled", builder_id, previous_site_id, {"team": previous_team})
	builder["construction_id"] = structure_id
	builder["order_kind"] = "construct"
	builder["target_id"] = 0
	_clear_member_targets(builder)
	if not _assign_route(builder, position):
		structures.erase(structure_id)
		_note_structure_table_mutation()
		team_resources[team] = resources_for_team(team) + cost
		builder["construction_id"] = 0
		return {"ok": false, "reason": last_route_rejection if last_route_rejection != "" else "route-rejected"}
	_apply_structure_inherit_upgrades(structures[structure_id] as Dictionary)
	_initialize_structure_auto_deposit(structures[structure_id] as Dictionary)
	_unpack_castle_behavior_for_structure(structure_id)
	if build_plots_only and build_plot_index >= 0:
		(build_plots[team] as Array)[build_plot_index]["occupant_structure_id"] = structure_id
	_emit_event("construction.started", builder_id, structure_id, {"team": team, "structure_kind": structure_kind, "cost": cost, "build_ticks": build_ticks, "object_id": String(builder.get("object_id", ""))})
	return {"ok": true, "builder_id": builder_id, "structure_id": structure_id, "cost": cost, "build_ticks": build_ticks}


# --- Dev playtest cheats -----------------------------------------------------
# Direct state mutation for the dev HUD (OPENBFME_DEV_HUD). Never routed
# through the lockstep command codec — the presentation layer blocks these in
# multiplayer, where a one-sided mutation would desync the peers.


func debug_finish_team_work(team: int) -> Dictionary:
	## Fast-forwards every in-progress job the team owns: construction sites,
	## production queues, structure/battalion upgrade queues, spellbook power
	## cooldowns, and hero ability cooldowns. Jobs are only rescheduled to
	## complete now — the REAL step paths still run, so every authored side
	## effect (pad seeding, events, upgrade effects) fires normally.
	var constructions := 0
	var jobs := 0
	for structure_id in structure_ids():
		var building: Dictionary = structures[structure_id]
		if int(building.get("team", -1)) != team:
			continue
		if float(building.get("construction_progress", 1.0)) < 1.0:
			building["construction_elapsed_ticks"] = maxi(0, int(building.get("construction_build_ticks", 1)) - 1)
			constructions += 1
		for item_value in building.get("queue", []) as Array:
			(item_value as Dictionary)["complete_tick"] = tick_index
			jobs += 1
		for item_value in building.get("upgrade_queue", []) as Array:
			(item_value as Dictionary)["complete_tick"] = tick_index
			jobs += 1
	_power_cooldown_until[team] = {}
	for id in entity_ids():
		var row: Dictionary = entities[id]
		if int(row.get("team", -1)) != team:
			continue
		for item_value in row.get("upgrade_queue", []) as Array:
			(item_value as Dictionary)["complete_tick"] = tick_index
			jobs += 1
		var states: Dictionary = row.get("ability_states", {}) as Dictionary
		for ability_id in states:
			(states[ability_id] as Dictionary)["cooldown_ready_tick"] = 0
	return {"constructions": constructions, "jobs": jobs}


func debug_level_up_battalions(ids: Array) -> Dictionary:
	## +1 authored rank per selected battalion/hero: awards exactly the XP
	## delta to the next authored threshold through the real experience
	## pipeline, so every authored level effect applies at true magnitudes.
	var leveled := 0
	var capped := 0
	var unauthored := 0
	for id_value in ids:
		var id := int(id_value)
		if not entities.has(id):
			continue
		var row: Dictionary = entities[id]
		if int(row.get("health", 0)) <= 0:
			continue
		var rule: Dictionary = _unit_experience_rules.get(String(row.get("unit_type", "")), {})
		if rule.is_empty():
			unauthored += 1
			continue
		var level := int(row.get("level", 1))
		var next_row: Dictionary = {}
		for row_value in Array(rule.get("levels", [])):
			var candidate := row_value as Dictionary
			if int(candidate.get("rank", 0)) > level:
				next_row = candidate
				break
		if level >= int(rule.get("max_level", 1)) or next_row.is_empty():
			capped += 1
			continue
		var needed := int(next_row.get("required_experience", 0)) - int(row.get("experience_xp", 0))
		_award_experience(row, maxi(1, needed))
		leveled += 1
	return {"leveled": leveled, "capped": capped, "unauthored": unauthored}


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
				var foundation_team := int(site.get("team", -1))
				if _team_ai_state.has(foundation_team):
					(_team_ai_state[foundation_team] as Dictionary)["construction_resolved"] = true
				_apply_structure_create_grants(site, false, true)
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
			_apply_structure_create_grants(site, false, true)
			_emit_event("construction.completed", builder_id, structure_id, {"team": int(site.get("team", -1)), "structure_kind": String(site.get("structure_kind", ""))})
func _nearest_attack_move_target(row: Dictionary) -> int:
	var origin := Vector2(row.get("position", Vector2.ZERO))
	var limit := maxf(float(row.get("attack_range", 1.0)), float(row.get("vision_range", 17.5)) * _ability_vision_multiplier(row))
	return _spatial_nearest_hostile(
		row, int(row.get("team", PLAYER_TEAM)), origin, limit,
		SPATIAL_FILTER_ENGAGE | SPATIAL_FILTER_STEALTH
	)


func _nearest_auto_target(row: Dictionary) -> Dictionary:
	if not bool(row.get("auto_acquire_enabled", true)):
		return {}
	if _stealth_active(row) and not bool(row.get("auto_acquire_while_stealthed", true)):
		return {}
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
	# InvisibilityUpdate: a cloaked battalion is never auto-acquired, so the
	# stealth filter travels with the neighbourhood query.
	var nearest_battalion := _spatial_nearest_hostile(
		row, self_team, origin, limit, SPATIAL_FILTER_ENGAGE | SPATIAL_FILTER_STEALTH
	)
	if nearest_battalion != 0:
		best_distance = origin.distance_to(
			Vector2((entities[nearest_battalion] as Dictionary).get("position", Vector2.ZERO))
		)
		best_id = nearest_battalion
		best_kind = "battalion"
	# Structures are not indexed: their count does not grow with army size, so
	# this loop is linear in map furniture rather than in units. It still runs
	# against the battalion result above, preserving the original precedence
	# where an equidistant structure examined later wins the tie.
	if not bool(row.get("auto_acquire_attack_buildings", true)):
		return {"id": best_id, "kind": best_kind} if best_id != 0 else {}
	for candidate in _hostile_living_structure_ids(self_team):
		if bool((structures[candidate] as Dictionary).get("not_auto_acquirable", false)):
			# holes.ini NOT_AUTOACQUIRABLE: an exposed rebuild hole is only ever
			# destroyed by an explicit attack order, never by idle acquisition.
			continue
		# SURFACE-TO-SURFACE, the same semantic the RANGE gate uses. Round 20
		# made firing at a structure subtract the target's authored bounding
		# circle (SAGE getDistanceSquared(..., FROM_BOUNDINGSPHERE_2D); see
		# _target_footprint_radius and the range test in _step_attacks) but left
		# ACQUISITION centre-to-centre. The two halves then disagreed, and the
		# disagreement was not academic:
		#
		#   A HoldGround melee horde standing at a fortress wall clamps `limit`
		#   to its own AttackRange (~0.305 sim units, see the HoldGround branch
		#   above). The Men fortress footprint is 1.9604. Centre-to-centre, that
		#   horde is ~2.0 units from the fortress centre — SIX TIMES its
		#   acquisition limit — so it never acquired a building it was already
		#   in weapon range of and could hit the instant it was ordered to.
		#
		#   The same subtraction also decides ties. `distance <= best_distance`
		#   lets a structure win an equal-distance comparison against a
		#   battalion; measured centre-to-centre a structure's distance is
		#   inflated by its whole footprint, so a structure lost every tie it
		#   should have won.
		#
		# Only the CANDIDATE's radius is subtracted, never the acquirer's, for
		# exactly the reason spelled out at _target_footprint_radius: this sim's
		# unit position is the horde centre, not a soldier bounding sphere.
		var distance := maxf(
			0.0,
			origin.distance_to(Vector2((structures[candidate] as Dictionary).get("position", Vector2.ZERO)))
			- _target_footprint_radius(candidate, "structure")
		)
		if distance <= best_distance:
			best_distance = distance
			best_id = candidate
			best_kind = "structure"
	return {"id": best_id, "kind": best_kind} if best_id != 0 else {}


func _rearm_mood_idle_cadence(row: Dictionary) -> void:
	if int(row.get("mood_attack_check_rate_ticks", 0)) <= 0:
		return
	row.erase("mood_next_check_tick")
	row["mood_randomize_next_check"] = true


func _step_production_exit(row: Dictionary) -> bool:
	return _production_subsystem()._step_production_exit(row)


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
		# PreAttackType (weapon.ini:4233 GondorArcherBow = PER_POSITION;
		# :10808 HaradrimBow = PER_SHOT). PER_POSITION charges PreAttackDelay
		# only when the attacker acquires a NEW target/attack position (and
		# on the first shot of an engagement). Sustained fire on a stationary
		# target cycles at firing + clip reload only. PER_SHOT (and PER_ATTACK
		# until it has its own rule) keeps the every-shot windup.
		# PreAttackRandomAmount is compiled but not applied (deferred).
		var pre_attack_type := String(row.get("pre_attack_type", "PER_SHOT")).to_upper()
		var last_target_id := int(row.get("pre_attack_last_target_id", 0))
		var last_target_kind := String(row.get("pre_attack_last_target_kind", ""))
		var same_engagement := (
			last_target_id == target_id
			and last_target_kind == target_kind
			and last_target_id != 0
		)
		var charge_pre_attack := pre_attack_type != "PER_POSITION" or not same_engagement
		var windup_ticks := pre_attack_ticks if charge_pre_attack else 0
		var windup_ms := float(row.get("pre_attack_delay_ms", 0.0)) if charge_pre_attack else 0.0
		row["attack_sequence"] = int(row.get("attack_sequence", 0)) + 1
		row["continuous_fire_count"] = int(row.get("continuous_fire_count", 0)) + 1
		# Persist last-target only for PER_POSITION. Writing these keys on the
		# pin harness (PER_SHOT default, synthetic rules) would move the
		# 3000-tick state hash.
		if pre_attack_type == "PER_POSITION":
			row["pre_attack_last_target_id"] = target_id
			row["pre_attack_last_target_kind"] = target_kind
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
			hit_ticks[member_index] = tick_index + stagger + windup_ticks
		var reload_or_delay_ms := base_reload_or_delay_ms
		if continuous_threshold > 0 and int(row["continuous_fire_count"]) > continuous_threshold:
			reload_or_delay_ms = floorf(reload_or_delay_ms / rate_multiplier)
		var cadence_ms := (
			windup_ms
			+ float(row.get("firing_duration_ms", 0.0))
			+ reload_or_delay_ms
		)
		var cadence_ticks := maxi(1, roundi(cadence_ms / (TICK_SECONDS * 1000.0)))
		var coast_anchor_ticks := maxi(1, roundi(coast_anchor_ms / (TICK_SECONDS * 1000.0)))
		row["continuous_fire_expiration_tick"] = tick_index + coast_anchor_ticks + coast_ticks
		row["attack_cooldown"] = maxi(
			cadence_ticks,
			windup_ticks + maximum_stagger + 1
		)
		row["attack_windup"] = windup_ticks + maximum_stagger
		_emit_event("combat.swing", attacker_id, target_id, {
			"attack_sequence": attack_sequence,
			"living_members": _living_member_count(row),
			"object_id": String(row.get("object_id", "")),
			"charged_pre_attack": charge_pre_attack,
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
			# The weapon-release instant for EVERY mode (a melee swing releases
			# too); the projectile-only bookkeeping below is a separate question.
			_mark_member_release(attacker_id, member_index)
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
				if target_kind == "battalion" and entities.has(target_id):
					swing_damage = maxi(
						1,
						roundi(float(swing_damage) * _flanking_outgoing_multiplier(row, entities[target_id]))
					)
				if _member_weapon_has_projectile(row):
					_launch_member_projectile(
						attacker_id,
						member_index,
						row,
						target_id,
						target_kind,
						forced_target,
						swing_damage,
						weapon_effect,
						int(release_tokens[member_index])
					)
				else:
					_apply_member_damage(
						attacker_id,
						member_index,
						target_id,
						swing_damage,
						target_kind,
						int(row.get("attack_sequence", 0)),
						forced_target
					)
					# Upgrade-gated bonus nuggets remain instant only on an instant
					# weapon; projectile-capable weapons carry them to impact below.
					_apply_member_bonus_nuggets(
						attacker_id, member_index, row, target_id, target_kind,
						forced_target, weapon_effect
					)
	row["member_attack_start_ticks"] = start_ticks
	row["member_attack_hit_ticks"] = hit_ticks
	row["member_attack_tokens"] = tokens
	row["member_target_indices"] = target_indices
	row["member_weapon_modes"] = weapon_modes
	row["member_attack_release_tokens"] = release_tokens
	row["attack_windup"] = maxi(0, int(row.get("attack_windup", 0)) - 1)


func _member_weapon_has_projectile(row: Dictionary) -> bool:
	return _projectiles_subsystem().member_weapon_has_projectile(row)


func _scaled_projectile_components(components: Array, outgoing_amount: int) -> Array:
	return _projectiles_subsystem().scaled_projectile_components(components, outgoing_amount)


func _launch_member_projectile(
	attacker_id: int,
	member_index: int,
	row: Dictionary,
	target_id: int,
	target_kind: String,
	forced_target: int,
	swing_damage: int,
	weapon_effect: Dictionary,
	release_token: int
) -> void:
	_projectiles_subsystem().launch_member_projectile(attacker_id, member_index, row, target_id, target_kind, forced_target, swing_damage, weapon_effect, release_token)


func _apply_member_bonus_nuggets(
	attacker_id: int,
	member_index: int,
	row: Dictionary,
	target_id: int,
	target_kind: String,
	forced_target: int,
	weapon_effect: Dictionary
) -> void:
	_combat_subsystem()._apply_member_bonus_nuggets(attacker_id, member_index, row, target_id, target_kind, forced_target, weapon_effect)


func _clear_member_attack_schedule(row: Dictionary) -> void:
	var start_ticks: Array = row.get("member_attack_start_ticks", [])
	var hit_ticks: Array = row.get("member_attack_hit_ticks", [])
	for index in range(start_ticks.size()):
		start_ticks[index] = -1
	for index in range(hit_ticks.size()):
		hit_ticks[index] = -1
	row["member_attack_start_ticks"] = start_ticks
	row["member_attack_hit_ticks"] = hit_ticks
	# Leaving the engagement (or swapping weapon sets) ends the firing span, so
	# the derived FIRING_*/RELOADING window goes with the schedule.
	_member_fire_ticks.erase(int(row.get("id", 0)))
	# Leaving the firing engagement drops the PER_POSITION last-target so the
	# next acquire (including the same unit after an idle) charges windup.
	# Only touch the keys if they already exist — adding them on the pin
	# harness (which never fires) would move the state hash.
	if row.has("pre_attack_last_target_id"):
		row["pre_attack_last_target_id"] = 0
		row["pre_attack_last_target_kind"] = ""


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


func _apply_weapon_mode(row: Dictionary, mode: String) -> bool:
	var modes: Dictionary = row.get("weapon_modes", {}) as Dictionary
	var selected: Dictionary = modes.get(mode, {}) as Dictionary
	if selected.is_empty():
		return false
	var prior := String(row.get("active_weapon_mode", ""))
	if (
		prior != ""
		and prior != mode
		and not (row.get("permanent_weapon_locks", []) as Array).is_empty()
	):
		# WeaponSet::updateWeaponSet implicitly releases even a permanent lock
		# before installing a different template set unless the incoming set
		# authors WeaponLockSharedAcrossSets. Neither BFME2 nor RotWK retail
		# authors that field, so every current-corpus mode transition releases.
		row["permanent_weapon_locks"] = []
	row["active_weapon_mode"] = mode
	for optional_projectile_field in [
		"projectile_object_id", "projectile_speed", "projectile_speed_source",
		"radius_damage_affects",
	]:
		if not selected.has(optional_projectile_field):
			row.erase(optional_projectile_field)
	# damage_components stay on the row unless the selected mode compiles
	# its own mix. Blanking here wiped the unit-rule mix on every attack tick.
	for field in [
		"attack_range", "attack_range_source", "minimum_attack_range",
		"minimum_attack_range_source", "delay_between_shots_ms",
		"pre_attack_delay_ms", "pre_attack_type", "pre_attack_random_amount_ms",
		"firing_duration_ms", "attack_period_ticks",
		"pre_attack_ticks", "firing_duration_ticks", "member_damage", "clip_size",
		"clip_reload_time_ms", "continuous_fire_one", "continuous_fire_coast_ticks",
		"continuous_fire_rate_multiplier", "projectile_object_id", "projectile_speed",
		"projectile_speed_source", "radius_damage_affects", "damage_components",
		"damage_type",
	]:
		if selected.has(field):
			row[field] = selected[field]
	if prior != "" and prior != mode:
		_clear_member_attack_schedule(row)
	return true


## Retail WeaponSlot -> the letter SAGE suffixes onto the weapon-cycle model
## conditions (PREATTACK_A, FIRING_B, ...). The retail corpus authors exactly
## three slots (playable_unit_compiler._WEAPON_SLOT_NAMES: PRIMARY, SECONDARY,
## TERTIARY), so the `_D` family has no source in this game's data and is never
## produced here — see `weapon_condition_deferred_reasons`.
const WEAPON_SLOT_CONDITION_LETTERS := {
	"primary": "A",
	"secondary": "B",
	"tertiary": "C",
}

## Live WeaponSet condition -> its model-condition token. The compiled weapon
## mode keys ARE the authored WeaponSet condition, lower-cased
## (playable_unit_compiler._conditional_weapon_modes), so this table only
## restates which of them retail also raises as a model condition. A live mode
## that is not listed is receipted, never uppercased into an invented token.
const LIVE_WEAPON_SET_CONDITION_MODES := {
	"weaponset_toggle_1": "WEAPONSET_TOGGLE_1",
	"weaponset_toggle_2": "WEAPONSET_TOGGLE_2",
	"weaponset_toggle_3": "WEAPONSET_TOGGLE_3",
	"weaponset_toggle_4": "WEAPONSET_TOGGLE_4",
	"mounted": "MOUNTED",
	"close_range": "CLOSE_RANGE",
}

## Tick each member last RELEASED its weapon: entity id -> member index -> tick.
##
## Everything else the weapon cycle needs is already authoritative —
## `member_attack_start_ticks` / `member_attack_hit_ticks` bracket the windup —
## but the release tick is destroyed the moment it is used: `_step_member_attacks`
## sets `member_attack_hit_ticks[i] = -1` on the firing tick, so afterwards
## "firing" and "idle" look identical on the row.
##
## That one fact is recorded HERE and not on the entity row on purpose: every row
## key is walked by `_authoritative_state()`, so a new per-member array would move
## the pinned 3000-tick hash (`tests/retail_state_pin_runner.gd`) for a value no
## rule reads. This table is tick-derived observation, exactly like `events`.
##
## Stated rather than hidden (AGENTS.md rule 5): `restore()` clears it, so a
## member that was inside its FiringDuration when the snapshot was taken reports
## no FIRING_* until its next release. PREATTACK_*, the pre-attack half of the
## composites and every weapon-set condition read authoritative keys and survive.
var _member_fire_ticks: Dictionary = {}


func member_weapon_condition_tokens(entity_id: int) -> Array:
	## Live SAGE weapon-cycle model conditions, one token Array per battalion
	## member, index-aligned with `member_health`. The presenter unions these into
	## the condition set it hands `AnimationStateSelect.select()`; retail binds
	## PREATTACK_A -> ATKF1 and FIRING_OR_RELOADING_A -> ATKF2 on the archer
	## (gondorarcher.ini:236-288).
	##
	## Derived on demand from the authoritative combat schedule — nothing here is
	## stored on the entity row. A member with no resolvable weapon slot, or a
	## battalion that is not attacking, yields the weapon-set conditions only;
	## `weapon_condition_deferred_reasons` says why.
	var out: Array = []
	if not entities.has(entity_id):
		return out
	var row := entities[entity_id] as Dictionary
	var member_health_values: Array = row.get("member_health", [])
	var start_ticks: Array = row.get("member_attack_start_ticks", [])
	var hit_ticks: Array = row.get("member_attack_hit_ticks", [])
	var member_modes: Array = row.get("member_weapon_modes", [])
	var modes := row.get("weapon_modes", {}) as Dictionary
	var attacking := String(row.get("state", "")) == "attack"
	var set_tokens := _live_weapon_set_condition_tokens(row)
	var marks := _member_fire_ticks.get(entity_id, {}) as Dictionary
	for member_index in range(member_health_values.size()):
		var tokens: Array = []
		if int(member_health_values[member_index]) <= 0:
			out.append(tokens)
			continue
		for token in set_tokens:
			tokens.append(token)
		if not attacking:
			out.append(tokens)
			continue
		var mode_key := String(row.get("active_weapon_mode", ""))
		if member_index < member_modes.size():
			# The mode this member's in-flight shot was scheduled with, which is
			# what its slot letter must name.
			mode_key = String(member_modes[member_index])
		var mode := modes.get(mode_key, {}) as Dictionary
		var letter := String(WEAPON_SLOT_CONDITION_LETTERS.get(String(mode.get("weapon_slot", "")), ""))
		if letter == "":
			out.append(tokens)
			continue
		var start := int(start_ticks[member_index]) if member_index < start_ticks.size() else -1
		var hit := int(hit_ticks[member_index]) if member_index < hit_ticks.size() else -1
		var firing_ticks := maxi(0, int(mode.get("firing_duration_ticks", row.get("firing_duration_ticks", 0))))
		var fire_tick := int(marks.get(member_index, -1))
		var since_release := tick_index - fire_tick if fire_tick >= 0 else -1
		# The swing has begun (its start tick was consumed) and the release tick
		# is still ahead: PreAttackDelay is running for this member.
		var preattack := start < 0 and hit > tick_index
		var firing := since_release >= 0 and since_release < firing_ticks
		# The third segment of the sim's cadence (windup + FiringDuration +
		# DelayBetweenShots-or-ClipReloadTime): released, done firing, waiting for
		# the next swing. Retail folds it into FIRING_OR_RELOADING.
		var reloading := since_release >= firing_ticks and fire_tick >= 0 and not preattack
		if preattack:
			tokens.append("PREATTACK_%s" % letter)
		if firing:
			tokens.append("FIRING_%s" % letter)
		if preattack or firing:
			tokens.append("FIRING_OR_PREATTACK_%s" % letter)
		if firing or reloading:
			tokens.append("FIRING_OR_RELOADING_%s" % letter)
		out.append(tokens)
	return out


func weapon_condition_deferred_reasons(entity_id: int) -> Array:
	## Why a weapon-cycle model condition is NOT being raised. Fail-loud
	## companion to `member_weapon_condition_tokens`: a consumer that sees no
	## PREATTACK_A must be able to tell "not winding up" from "this unit's data
	## cannot name a slot".
	var out: Array = []
	if not entities.has(entity_id):
		return ["entity-missing"]
	var row := entities[entity_id] as Dictionary
	var active := String(row.get("active_weapon_mode", ""))
	var mode := (row.get("weapon_modes", {}) as Dictionary).get(active, {}) as Dictionary
	if not WEAPON_SLOT_CONDITION_LETTERS.has(String(mode.get("weapon_slot", ""))):
		# No authored WeaponSlot means no letter, and a guessed PRIMARY would be
		# an invented animation state.
		out.append("weapon-slot-unauthored:%s" % active)
	if maxi(0, int(mode.get("firing_duration_ticks", row.get("firing_duration_ticks", 0)))) <= 0:
		out.append("firing-duration-zero:%s" % active)
	if (
		active != ""
		and active != String(row.get("default_weapon_mode", ""))
		and active != String(row.get("close_weapon_mode", ""))
		and not LIVE_WEAPON_SET_CONDITION_MODES.has(active)
	):
		out.append("weapon-set-condition-unmapped:%s" % active)
	# Structural, not per-unit: `_apply_weapon_mode` installs a set in one tick
	# and clears the member schedule, so there is no swap-in-progress span for
	# SWAPPING_TO_WEAPONSET_* to describe.
	out.append("swapping-to-weaponset-not-modelled")
	# PRIMARY/SECONDARY/TERTIARY is the whole retail slot corpus.
	out.append("weapon-slot-d-absent-from-retail-corpus")
	return out


func _live_weapon_set_condition_tokens(row: Dictionary) -> Array:
	var out: Array = []
	var active := String(row.get("active_weapon_mode", ""))
	var close := String(row.get("close_weapon_mode", ""))
	if active != "" and active == close:
		# The close profile is built from the WeaponSet conditioned on
		# CLOSE_RANGE (retail_vertical_slice._retail_unit_rule), whatever the
		# rule chose to key it under.
		out.append("CLOSE_RANGE")
	elif LIVE_WEAPON_SET_CONDITION_MODES.has(active):
		out.append(String(LIVE_WEAPON_SET_CONDITION_MODES[active]))
	for flag_value in row.get("weapon_set_flags", []) as Array:
		var flag := String(flag_value).to_upper()
		if flag != "" and not out.has(flag):
			out.append(flag)
	return out


func _mark_member_release(attacker_id: int, member_index: int) -> void:
	var marks := _member_fire_ticks.get(attacker_id, {}) as Dictionary
	marks[member_index] = tick_index
	_member_fire_ticks[attacker_id] = marks
	if _member_fire_ticks.size() > entities.size():
		# Entities are removed from a dozen places with no shared hook, so the
		# observation table is pruned here instead. Self-limiting: after one pass
		# it cannot exceed the live entity count again until another id dies.
		for key in _member_fire_ticks.keys():
			if not entities.has(int(key)):
				_member_fire_ticks.erase(key)


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


## Source-object-id -> authored footprint radius in retail SOURCE units. Pack
## documents never change inside a match, so this is a pure memo of a read-only
## lookup: identical on every lockstep peer, order-independent, never part of the
## hashed state.
var _structure_footprint_source_cache: Dictionary = {}
## Structure id -> resolved footprint radius in SIM units. Same memo contract as
## the table above (pure function of read-only inputs, never hashed), one level
## further down so the per-tick attack path allocates no key string at all.
## Cleared wherever the structure table is replaced wholesale.
var _structure_footprint_radius_cache: Dictionary = {}
## Source object ids whose missing geometry has already been reported, so the
## fallback warning fires once per id instead of once per call.
var _footprint_fallback_reported: Dictionary = {}


func _structure_footprint_radius(structure_row: Dictionary) -> float:
	## The structure's BOUNDING-CIRCLE radius in sim units — SAGE's
	## `FROM_BOUNDINGSPHERE_2D` radius, not the movement block radius.
	##
	## THE TWO RADII ARE DIFFERENT NUMBERS AND BOTH ARE CORRECT.
	## STRUCTURE_BLOCK_RADIUS is a MOVEMENT footprint: placement radius plus a
	## step of walkway, so units path politely around finished buildings (a
	## fortress is 4.6). This is the AUTHORED Geometry block: MenFortress is
	## `Geometry = BOX / GeometryMajorRadius = 64` plus four AdditionalGeometry
	## plot pieces of radius 10 at GeometryOffset 64/-64
	## (object/goodfaction/structures/men/fortress.ini:1254-1265), which the
	## importer projects into a union `footprint.radius` of 74 source units —
	## 1.9604 sim at the Fords of Isen II transform 0.02649232738129. Using the
	## movement radius here would hand every weapon in the game 2.6 extra units of
	## reach against a fortress.
	##
	## RESOLUTION ORDER: an explicit row value (fixtures, and any future seeding
	## that wants to pin a footprint) -> the selected pack's compiled geometry via
	## ContentDB -> a fallback, see COMBAT_FALLBACK_STRUCTURE_SOURCE_RADIUS.
	##
	## Returns 0.0 (no expansion, i.e. the old centre-to-centre behaviour) when
	## the map carries no source transform, so a fixture that never set one is
	## never handed a 74-SIM-unit disc.
	##
	## MEMOISED PER STRUCTURE ID. This is on the per-tick attack path — the range
	## gate calls it for every attacker against every structure target, every
	## tick — and the resolution underneath it built a formatted string key on
	## every call. The inputs (the row's authored value, its source object id,
	## its kind, and the map transform) are all fixed for a structure's lifetime,
	## so the result is cached against the integer structure id, which allocates
	## nothing. Cleared by setup() and by _restore_authoritative_state(), the two
	## places the structure table is replaced wholesale.
	if structure_row.is_empty():
		return 0.0
	var structure_id := int(structure_row.get("id", 0))
	if structure_id != 0 and _structure_footprint_radius_cache.has(structure_id):
		return float(_structure_footprint_radius_cache[structure_id])
	var scale := float(_rules.get("source_map_transform_scale", 0.0))
	if not is_finite(scale) or scale <= 0.0:
		return 0.0
	var source_radius := float(structure_row.get("footprint_radius_source", 0.0))
	if not is_finite(source_radius) or source_radius <= 0.0:
		source_radius = _resolved_footprint_source_radius(
			String(structure_row.get("source_object_id", "")),
			String(structure_row.get("structure_kind", "")),
		)
	if not is_finite(source_radius) or source_radius <= 0.0:
		return 0.0
	var radius := source_radius * scale
	if structure_id != 0:
		_structure_footprint_radius_cache[structure_id] = radius
	return radius


## Source-unit footprint used when a structure document carries no compiled
## geometry at all. NON-FORTRESS ONLY; a fortress keeps
## SelectionPick.DEFAULT_FORTRESS_SOURCE_RADIUS (64.0), which is MenFortress's
## authored GeometryMajorRadius verbatim.
##
## WHY IT IS NOT 50.0 ANY MORE. The selection round published 50.0 "roughly a
## Gondor barracks", and for SELECTION that direction is forgiving: an oversized
## pick radius makes a building easier to click. On the COMBAT path the same
## number is a gift of free weapon reach and free acquisition range against
## exactly the structures whose real size is unknown. Censused across every
## playable-structure document in every pack on disk
## (workspace/scratch/opus29-footprint-census.txt, 196 documents that carry
## geometry): the median authored radius is 48, the 10th percentile is 15, and
## 104 of the 196 are BELOW 50. A 50.0 fallback over-expands most of them.
##
## 5.0 IS A FLOOR WITH A DERIVATION, not a smaller guess. The fallback must
## never exceed a structure's true footprint, or it hands out reach the geometry
## does not support; the greatest value that satisfies that for every structure
## the packs ship is the SMALLEST authored radius, and that is 5.0 — the
## fortress expansion pads (Dwarven/Isengard/Men/Mordor/Wild
## FortressExpansionPad{Corner,Side}, same census). Erring small degrades toward
## the pre-round-20 centre-to-centre behaviour, which is the safe direction.
##
## HOW OFTEN IT FIRES, measured rather than assumed: 6 of the 182 structure
## documents in the current workspace selection carry no geometry — MenWallGate,
## DwarvenCastleWallGate, Isengard/Mordor/Wild LumberMill. The stale
## bfme2-men-vslice supplemental has 22 more, all superseded by rotwk-men-vslice.
const COMBAT_FALLBACK_STRUCTURE_SOURCE_RADIUS := 5.0


func _resolved_footprint_source_radius(source_object_id: String, structure_kind: String) -> float:
	## MEMO KEY IS THE EXACT ID, not a lowered one. ContentDB's registry lookup
	## is an exact Dictionary hit (see get_playable_structure_runtime), so a
	## lowered memo key answered for a DIFFERENT string than the one the lookup
	## would have used: two ids differing only in case shared one memo entry
	## while resolving differently — one hitting the document, one missing it and
	## taking the fallback. Keying on the same string the lookup uses makes the
	## memo incapable of disagreeing with the thing it memoises.
	var key := "%s|%s" % [source_object_id, structure_kind]
	if _structure_footprint_source_cache.has(key):
		return float(_structure_footprint_source_cache[key])
	var resolved := 0.0
	if source_object_id != "":
		var db = _content_db_ref()
		if db != null and db.has_method("get_playable_structure_runtime"):
			var document: Variant = db.get_playable_structure_runtime(source_object_id)
			if typeof(document) == TYPE_DICTIONARY:
				var gameplay: Dictionary = (
					((document as Dictionary).get("registration", {}) as Dictionary)
					.get("gameplay", {}) as Dictionary
				)
				var geometry: Variant = gameplay.get("geometry", {})
				if typeof(geometry) == TYPE_DICTIONARY:
					resolved = SelectionPick.source_footprint_radius(geometry as Dictionary)
	if not is_finite(resolved) or resolved <= 0.0:
		# NAMED, ONCE PER OBJECT ID. The fallback used to fire in total silence,
		# so a pack shipped without geometry looked exactly like a pack with it.
		# Once per id (not per call) keeps a per-tick path from flooding the log.
		if source_object_id != "" and not _footprint_fallback_reported.has(source_object_id):
			_footprint_fallback_reported[source_object_id] = true
			push_warning(
				"structure footprint: '%s' (kind=%s) carries no compiled geometry; using the %s source-unit fallback"
				% [
					source_object_id,
					structure_kind,
					"fortress" if structure_kind == "fortress" else "non-fortress",
				]
			)
		resolved = (
			SelectionPick.DEFAULT_FORTRESS_SOURCE_RADIUS
			if structure_kind == "fortress"
			else COMBAT_FALLBACK_STRUCTURE_SOURCE_RADIUS
		)
	_structure_footprint_source_cache[key] = resolved
	return resolved


func _target_footprint_radius(target_id: int, target_kind: String) -> float:
	## The radius to subtract from a centre-to-centre distance before comparing it
	## with a weapon range.
	##
	## STRUCTURES ONLY, DELIBERATELY. Real SAGE subtracts BOTH objects' bounding
	## radii, and every horde member in the selected pack authors
	## `GeometryMajorRadius = 8.0` (measured across every
	## data/playable-units/*.json in the men pack). It is not applied here, for a
	## reason that is about this sim's model rather than about SAGE:
	##
	##   The sim's authoritative unit position is the HORDE CENTRE, and the range
	##   gate is horde-centre to horde-centre. SAGE's bounding-sphere test is
	##   between two individual SOLDIER objects. Subtracting 2 x 8 source units
	##   from a horde-centre distance applies a soldier-scale correction to a
	##   horde-scale measurement — it would move every engagement 0.42 sim units
	##   earlier without matching anything retail does. `_step_member_attacks`
	##   (this file) never range-tests a member at all; members exist in the
	##   combat path only through `_member_world_position`, and only to assign
	##   victims. Doing this properly means moving the gate itself down to the
	##   member level, which is its own change with its own failing-first evidence
	##   and its own re-derivation of the member-combat suite's 98 authored
	##   expectations.
	##
	## A structure has no such gap: it is a single object, its authoritative
	## position IS its centre, and its authored Geometry IS the bounding circle
	## SAGE measures from. The correction applies exactly.
	if target_kind != "structure":
		return 0.0
	return _structure_footprint_radius(structures.get(target_id, {}) as Dictionary)


## Hysteresis width added to a castle member's own block radius when deciding
## whether the walking line crosses it. The corridor is recomputed every tick
## from the unit's current position, so a zero-width test would let a member
## flip to BLOCKING while the unit is still inside its disc — it would close on
## top of the unit rather than behind it.
##
## OWN CONSTANT, WITH ITS OWN DERIVATION. Round 18 wrote this as an alias of
## BATTALION_SEPARATION_PUSH, which made the two move together for no reason:
## the separation push is a per-tick displacement between two battalions, this
## is a geometric tolerance on a segment/disc test. They are numerically equal
## by coincidence, and the alias hid the actual bound.
##
## DERIVED from the one castle the pack ships (MenFortress on Fords of Isen II,
## measured in workspace/scratch/opus24-probe1.out.log): every castle piece
## carries the default 2.8 STRUCTURE_BLOCK_RADIUS; attacking the fortress centre
## from outside puts the furthest corner pad 2.398 off the walking line, and
## attacking an east corner pad from the east puts the two west pads 3.392 off
## it (recomputed to full precision this round: 3.391 and the corner spacing
## 4.796 — see _test_castle_corridor_is_bounded). The margin must therefore
## satisfy
##     2.398 - 2.8 < margin < 3.391 - 2.8   i.e.   (negative) < margin < 0.591
## — the lower bound is already met by any non-negative value because 2.398 is
## inside the bare radius, so the binding constraint is the upper one. It must
## also exceed one tick of travel or the hysteresis buys nothing.
##
## ONE TICK OF TRAVEL, RECONCILED (round 21). This file carried two different
## answers to that question — "~0.03" here and "0.55" at the transit budget in
## _deflect_around_structures — and neither was right. 0.55 was the castle
## fixture's mis-scaled step; 0.03 is a factor of ten under the real figure, and
## reads like a per-tick value derived from an already-per-tick speed. The
## measured answer, from every playable-unit document in the workspace selection
## (159 rows with a resolved speed): median 55 source units/second = 0.1457 sim
## per tick, ceiling 115 source = 0.3047 sim per tick. Both derivations now cite
## this same census.
##
## 0.35 still holds, and now for a stated reason: it clears the 0.3047 ceiling
## (so the corridor cannot close on a unit mid-step even at the game's top
## authored speed) while sitting 0.24 under the 0.591 geometric ceiling above.
const CASTLE_CORRIDOR_MARGIN := 0.35


func _point_segment_distance(point: Vector2, a: Vector2, b: Vector2) -> float:
	var ab := b - a
	var length_squared := ab.length_squared()
	if length_squared <= 0.000001:
		return point.distance_to(a)
	var t := clampf((point - a).dot(ab) / length_squared, 0.0, 1.0)
	return point.distance_to(a + ab * t)


func _castle_footprint_pass_through(position: Vector2, attack_target_id: int, attack_target_kind: String) -> Dictionary:
	## The set of structure ids a battalion standing at `position` and attacking
	## `attack_target_id` may walk through: the target itself, plus exactly those
	## members of the target's castle whose footprint the WALKING LINE from the
	## unit to the target actually crosses.
	##
	## BOUNDED, not blanket. Round 17 opened the ENTIRE castle group on any
	## attack order onto any member of it, which made a far-side attack dissolve
	## the near wall as well and left the group open for as long as the order
	## lasted. This model opens only what is in the way, is recomputed every tick
	## from the unit's current position, and closes behind it.
	##
	## WHY THE WHOLE CASTLE, NOT JUST THE TARGET. CastleBehavior authors its
	## pieces INSIDE the fortress footprint, not around it: MenFortressCitadel
	## sits on the fortress origin (offset_source 0,0) and the six expansion
	## pads within 64 source units of it. At the Fords of Isen II transform
	## (0.02649232738129) that is a citadel exactly on the fortress centre and
	## pads 1.64-2.40 sim units out — measured live, every enemy castle piece in
	## workspace/scratch/opus24-probe1.out.log. Each piece carries the default
	## 2.8 STRUCTURE_BLOCK_RADIUS, so exempting only the ordered target left the
	## fortress ringed by a ~5.2-unit wall of its own sub-structures. Retail melee
	## ranges are ~11.5 source units = 0.305 sim, and the MOVEMENT ring is 5.2, so
	## no melee horde could ever reach a fortress or a pad — that gap is far wider
	## than the 1.9604 footprint the range gate now subtracts (round 20), so the
	## corridor is still required: they parked on the ring at distance 4.4-5.1 in state
	## `run` and only ranged units ever landed a blow
	## (workspace/scratch/opus09-live1.out.log:35,52 — 17,521 ticks to kill a
	## fortress, all of it archer damage).
	##
	## THE RULE: a member is passable only if the segment [unit -> target centre]
	## comes within `member block radius + CASTLE_CORRIDOR_MARGIN` of that
	## member's centre — i.e. the walking line actually crosses its footprint.
	## The target itself is always passable (that is the order). Everything
	## outside the group deflects normally, and a battalion with no STRUCTURE
	## attack target (every plain move order, friendly castles included) gets an
	## empty set on the first line, so that path stays byte-identical.
	##
	## MEASURED against the one castle the pack ships (MenFortress on Fords of
	## Isen II, workspace/scratch/opus24-probe1.out.log: fortress radius 4.6 at
	## the origin, citadel radius 2.8 exactly on it, two side pads 1.643 out and
	## four corner pads 2.398 out, all radius 2.8):
	##   attacking the fortress centre from outside  -> every piece is on the
	##     line (0.000-2.398 <= 2.8 + 0.35), so the whole group opens, which is
	##     correct: they genuinely overlap the target.
	##   attacking the EAST corner pad from the east -> the two WEST pads sit
	##     3.391 and 4.796 off the line, above the 3.15 threshold, and stay
	##     BLOCKING. Under round 17 they opened too.
	## Those numbers are the bound: 2.398 passes, 3.391 does not.
	##
	## CORRECTION (round 19): round 18 reported BOTH west pads at "3.392". They
	## are not equidistant and neither figure was exact. At the retail transform
	## 0.02649232738129 a 64-source pad offset is 1.6955090 sim units, so a corner
	## pad sits 2.3978 out along its diagonal. With the attacker on the
	## fortress->east-corner ray, the SW pad's nearest point on the SEGMENT is the
	## east pad endpoint at 2 * 1.6955090 = 3.3910179; the NW pad lies on the
	## infinite line but on the far side of the fortress, so the segment clamps to
	## the same endpoint and it measures 3.3910179 * sqrt(2) = 4.7956. Both are
	## now asserted to 0.001 in _test_castle_corridor_is_bounded instead of being
	## quoted from a report.
	##
	## ID-ALIAS GUARD: battalion ids and structure ids come from the same counter
	## space but are separate tables, so a battalion target whose id happens to
	## match a structure id would have opened that structure. The caller passes
	## the row's `target_kind` and anything but "structure" returns empty.
	var passable: Dictionary = {}
	if attack_target_kind != "structure" or attack_target_id == 0 or not structures.has(attack_target_id):
		return passable
	passable[attack_target_id] = true
	var target_row: Dictionary = structures[attack_target_id]
	var target_center := Vector2(target_row.get("position", Vector2.ZERO))
	# Three ways into the same group: the castle owner itself, one of its
	# CastleBehavior pieces, or an expansion raised on one of its pads.
	var castle_id := int(target_row.get("castle_piece_of_fortress", 0))
	if castle_id == 0:
		castle_id = int(target_row.get("expansion_of_fortress", 0))
	if castle_id == 0 and (
		target_row.has("castle_piece_structure_ids")
		or String(target_row.get("structure_kind", "")) == "fortress"
	):
		castle_id = attack_target_id
	if castle_id == 0 or not structures.has(castle_id):
		return passable
	var members: Array[int] = [castle_id]
	for piece_value in (structures[castle_id] as Dictionary).get("castle_piece_structure_ids", []) as Array:
		members.append(int(piece_value))
	for pad_value in expansion_pads.get(castle_id, []) as Array:
		var expansion_structure_id := int((pad_value as Dictionary).get("expansion_structure_id", 0))
		if expansion_structure_id != 0:
			members.append(expansion_structure_id)
	for member_id in members:
		if passable.has(member_id) or not structures.has(member_id):
			continue
		var member_row: Dictionary = structures[member_id]
		var member_radius := float(STRUCTURE_BLOCK_RADIUS.get(
			String(member_row.get("structure_kind", "")), 2.8
		))
		var member_center := Vector2(member_row.get("position", Vector2.ZERO))
		if _point_segment_distance(member_center, position, target_center) <= member_radius + CASTLE_CORRIDOR_MARGIN:
			passable[member_id] = true
	return passable


## The furthest a footprint may move a unit in one tick. Deflection used to
## SNAP: a unit sitting on a fortress centre was projected 4.6 units in a single
## step, which is a teleport, and a unit sitting EXACTLY on the centre was
## skipped entirely by the `distance > 0.001` guard and stayed clipped forever.
## Both are reachable now that an attack order can put a melee horde inside a
## castle footprint and then end (stop, retarget, target death), at which point
## the exemption disappears and the unit has to be evicted. Bounding the
## displacement makes that eviction a walk, not a jump; the unit keeps being
## pushed every tick until it is clear.
const STRUCTURE_EVICTION_STEP := BATTALION_SEPARATION_PUSH


func _castle_gate_blocking_discs(structure_row: Dictionary, mover: Dictionary) -> Array[Dictionary]:
	var policy: Dictionary = structure_row.get("gate_behavior", {})
	var geometries: Dictionary = structure_row.get("gate_geometries", {})
	if policy.is_empty() or geometries.is_empty():
		return []
	var use_open_geometry := bool(policy.get("pathing_open", false))
	var same_team := int(mover.get("team", -1)) == int(structure_row.get("team", -2))
	if structure_row.has("fake_pathfind_portal"):
		var portal: Dictionary = structure_row.get("fake_pathfind_portal", {})
		if use_open_geometry and not same_team and not bool(portal.get("allow_enemies", false)):
			# FakePathfindPortalBehaviour AllowEnemies=No: hostiles never get
			# the passage even while open; they must destroy the gate (L3 pin).
			use_open_geometry = false
		elif not use_open_geometry:
			# Retail's fake pathfind PORTAL is a shortcut THROUGH a closed
			# gate: qualified movers path as if it were open and AIGateUpdate
			# swings it before they arrive. AllowNonSkirmishAIUnits=No
			# (helmsdeepbuildings.ini:6288) reserves that shortcut for
			# skirmish-AI-controlled friendlies; a human owner's troops wait
			# for the doors like retail. An OPEN gate is never impassable for
			# its owner - the rule only widens closed-gate passage.
			if same_team and (bool(portal.get("allow_non_skirmish_ai", false)) or (ai_enabled and team_is_ai(int(mover.get("team", -1))))):
				use_open_geometry = true
			elif not same_team and bool(portal.get("allow_enemies", false)):
				use_open_geometry = true
	var geometry_names: Array[String] = []
	if use_open_geometry:
		geometry_names.assign(["OpenLeft", "OpenRight"])
	else:
		geometry_names.append("Closed")
	var scale := float(_rules.get("source_map_transform_scale", 0.1))
	var facing := float(structure_row.get("facing_radians", 0.0))
	var origin := Vector2(structure_row.get("position", Vector2.ZERO))
	var discs: Array[Dictionary] = []
	for geometry_name in geometry_names:
		var geometry: Dictionary = geometries.get(geometry_name, {})
		if geometry.is_empty() or String(geometry.get("shape", "")).to_upper() != "BOX":
			continue
		var major := float(geometry.get("majorRadius", 0.0)) * scale
		var minor := float(geometry.get("minorRadius", 0.0)) * scale
		if major <= 0.0 or minor <= 0.0:
			continue
		var long_radius := maxf(major, minor)
		var authored_short_radius := minf(major, minor)
		# Minkowski-expand the authored box by the battalion's collision body;
		# otherwise a centre-point test lets the formation clip through the leaf.
		var disc_radius := authored_short_radius + BATTALION_SEPARATION_PUSH
		var local_axis := Vector2.RIGHT if major >= minor else Vector2.DOWN
		var axis := local_axis.rotated(facing)
		var offset_value: Array = geometry.get("offset", [])
		var local_offset := Vector2.ZERO
		if offset_value.size() == 3:
			local_offset = Vector2(float(offset_value[0]), float(offset_value[1])) * scale
		var center := origin + local_offset.rotated(facing)
		# A long authored BOX is represented by a deterministic chain of discs,
		# never by the old single tiny structure disc. Adjacent discs overlap.
		var span := maxf(0.0, long_radius - authored_short_radius)
		var disc_count := maxi(2, int(ceili(span / maxf(disc_radius, 0.001))) + 1)
		for disc_index in range(disc_count):
			var along := 0.0 if disc_count == 1 else lerpf(-span, span, float(disc_index) / float(disc_count - 1))
			discs.append({"center": center + axis * along, "radius": disc_radius})
	return discs


func _deflect_around_structures(
	position: Vector2,
	row: Dictionary,
	travel_step: Vector2 = Vector2.ZERO,
	structure_id_list: Array[int] = []
) -> Vector2:
	# Battalions slide around building footprints instead of clipping through
	# them. The battalion's own attack target — and, when that target is part of
	# a castle, the members of that castle the walking line actually crosses —
	# is exempt so melee can close in. See _castle_footprint_pass_through.
	#
	# `travel_step` is the displacement this tick ALREADY applied to `position`
	# by _step_route. A non-zero value selects the TANGENTIAL SLIDE below; the
	# stationary eviction pass passes zero and keeps the radial push.
	#
	# `structure_id_list` used to let a caller hoist structure_ids() out of a
	# multi-entity pass. The spatial-hash gather is a complete, cheaper
	# substitute for that full list (a centre outside the gathered box cannot
	# overlap any blocking disc), so a provided list is ignored — honouring
	# it would let the eviction hoist bypass the broad-phase.
	var attack_target_id := int(row.get("target_id", 0))
	var attack_target_kind := String(row.get("target_kind", "battalion"))
	var passable := _castle_footprint_pass_through(
		position,
		attack_target_id,
		attack_target_kind,
	)
	# BROAD-PHASE (lane L2b item 6): only structures whose blocking disc can
	# overlap `position` are visited, in the same ascending id order the full
	# scan used. A centre outside the gathered box is further than the maximum
	# block radius away, and the loop below skips such rows with no side
	# effects, so this is byte-identical to scanning structure_ids().
	var ids: Array[int] = _structure_ids_near(position)
	# TOTAL push bound. Round 18 clamped each structure's push to
	# STRUCTURE_EVICTION_STEP separately, so N overlapping discs could compound
	# into an N * step jump in one tick — and overlapping discs are the NORMAL
	# case inside a castle, where the citadel sits exactly on the fortress centre
	# and six pads sit 1.64-2.40 out with 2.8 radii. A unit on the fortress
	# origin is inside four of them at once. The budget below is spent across all
	# of them, so eviction is one step per tick however many footprints claim it.
	#
	# The budget bounds the OUTWARD (radial) component only. A tangential slide
	# is not displacement the sim is inventing — it is the unit's own travel
	# step redirected along the disc — and charging it to the eviction budget
	# left a unit that walked in at full speed unable to recover its clearance in
	# the same tick.
	#
	# A TRANSIT step raises the budget to its own length. STRUCTURE_EVICTION_STEP
	# exists so a unit that is already deep inside a footprint WALKS out instead
	# of teleporting; it was never meant to stop a moving unit from undoing the
	# penetration it just created. Recovering at most exactly what this tick
	# moved is still not a teleport.
	#
	# RE-DERIVED FROM A CORRECTED MEASUREMENT (round 21). Round 19 justified this
	# with "a slice melee steps 0.55 per tick at the retail transform". That
	# number was WRONG, and wrong by 3.8x: it came from banner_castle_sim_runner's
	# castle fixture, which swapped the map transform to retail but left its unit
	# rules at the 0.1 scale they were authored for (see _rescale_unit_rules
	# there, fixed in the same round). The AUTHORED ceiling, censused across every
	# playable-unit document in every pack in the workspace selection (159 rows
	# with a resolved speed): the fastest unit in the game authors 115 source
	# units/second — Rivendell Lancers, Haradrim Riders, Warg riders, Knights of
	# Dol Amroth — which at 0.02649232738129 and TICK_SECONDS 0.1 is 0.3047 sim
	# units per tick. The median is 55 source = 0.1457. NOT ONE authored base
	# speed exceeds STRUCTURE_EVICTION_STEP (0.35).
	#
	# SO WHY KEEP IT. Because `travel_step` is not a base speed: _step_route
	# composes it as base * stance speedMultiplier * formation speed_multiplier *
	# ability SPEED modifiers (see the max_speed line there). A mounted or
	# leadership-boosted lancer at any multiplier above 1.149 clears 0.35, and
	# those multipliers are authored data, not a hypothetical. The rule is
	# therefore inert for every unit at base speed and binding exactly where a
	# boosted one would otherwise be left clipped inside a wall it walked into.
	# Deleting it would trade a measured no-op for an unmeasured regression.
	var push_budget := maxf(STRUCTURE_EVICTION_STEP, travel_step.length())
	for structure_id in ids:
		if not structures.has(structure_id):
			continue
		var structure_row: Dictionary = structures[structure_id]
		if int(structure_row.get("health", 0)) <= 0:
			continue
		# Construction sites do not block movement: builders must reach their
		# own site, and scaffolding is passable until the structure completes.
		if float(structure_row.get("construction_progress", 1.0)) < 1.0:
			continue
		var gate_discs := _castle_gate_blocking_discs(structure_row, row)
		if not gate_discs.is_empty():
			for disc in gate_discs:
				var gate_center := Vector2(disc.get("center", Vector2.ZERO))
				var gate_radius := float(disc.get("radius", 0.0))
				var gate_offset := position - gate_center
				var gate_distance := gate_offset.length()
				# Sweep the just-applied travel segment as well as testing its end;
				# fast battalions must not tunnel through a thin Closed door slab.
				if gate_distance >= gate_radius and travel_step.length_squared() > 0.000001:
					var gate_start := position - travel_step
					var gate_t := clampf((gate_center - gate_start).dot(travel_step) / travel_step.length_squared(), 0.0, 1.0)
					var gate_closest := gate_start + travel_step * gate_t
					var closest_offset := gate_closest - gate_center
					if closest_offset.length() < gate_radius:
						position = gate_closest
						gate_offset = closest_offset
						gate_distance = closest_offset.length()
				if gate_distance >= gate_radius:
					continue
				var incoming_offset := position - travel_step - gate_center
				var gate_direction := incoming_offset.normalized() if incoming_offset.length_squared() > 0.000001 else (gate_offset / gate_distance if gate_distance > 0.001 else _eviction_fallback_direction(row))
				var gate_applied := minf(gate_radius - gate_distance, push_budget)
				push_budget -= gate_applied
				var gate_seated_radius := gate_distance + gate_applied
				position = gate_center + gate_direction * gate_seated_radius
				if push_budget <= 0.0:
					break
			continue
		var radius := float(STRUCTURE_BLOCK_RADIUS.get(String(structure_row.get("structure_kind", "")), 2.8))
		if String(row.get("order_kind", "")) == "construct" and structure_id >= CASTLE_FIXTURE_FIRST_ID:
			# Castle props sit densely around their authored build plots. A porter
			# travelling to one uses each fixture's compiled footprint, not the
			# generic 2.8-unit dynamic-building walkway disc that seals the keep.
			var fixture_radius := _structure_footprint_radius(structure_row)
			if fixture_radius > 0.0:
				radius = fixture_radius
		if passable.has(structure_id):
			# THE TARGET'S OWN FOOTPRINT STILL STOPS THE ATTACKER AT ITS WALL.
			#
			# The corridor exists because the MOVEMENT block radius is an inflated
			# walkway ring (a fortress is 4.6 against an authored footprint of
			# 1.9604) and a castle's pieces are authored INSIDE it, so leaving it
			# up walled melee out of every fortress it was ordered onto. But
			# opening it completely let the attacker walk to the target's CENTRE —
			# measured d=0.24 then d=0.00 in the live slice
			# (workspace/scratch/opus24-probe2.out.log).
			#
			# Now that the range gate is surface-to-surface the wall is reachable
			# and standing off it is correct, so the target reasserts its OWN
			# authored footprint. `minf` keeps this a strict relaxation of the
			# ordinary rule: the corridor can never block harder than the plain
			# movement disc would have (relevant for wall towers, whose 98-source
			# geometry projects wider than their 2.2 block radius).
			#
			# CROSSED CASTLE MEMBERS STAY FULLY OPEN. Only the ordered target is
			# re-blocked. A pad or citadel the walking line happens to cross is
			# not what the unit is trying to hit, and re-blocking those would put
			# the fortress's own 1.96 disc between an attacker and a pad standing
			# 1.64 from the fortress centre — unreachable from outside.
			if structure_id != attack_target_id or attack_target_kind != "structure":
				continue
			radius = minf(radius, _structure_footprint_radius(structure_row))
			if radius <= 0.0:
				continue
		var center := Vector2(structure_row.get("position", Vector2.ZERO))
		var offset := position - center
		var distance := offset.length()
		if distance >= radius:
			continue
		var direction := offset / distance if distance > 0.001 else _eviction_fallback_direction(row)
		# Bounded: keep walking out, one step per tick, instead of teleporting to
		# the ring. Ordinary deflection never reaches the clamp — a unit moving at
		# slice speeds penetrates far less than one step per tick — so it only
		# bites on the eviction case it exists for.
		var applied := minf(radius - distance, push_budget)
		push_budget -= applied
		var seated_radius := distance + applied
		position = center + direction * seated_radius
		if travel_step.length_squared() > 0.000001:
			var slide_dest := Vector2(row.get("destination", position))
			var live_route: Array = row.get("route", [])
			if not live_route.is_empty():
				slide_dest = Vector2(live_route[0])
			position = _tangential_slide_point(
				center, seated_radius, direction, travel_step, slide_dest
			)
		if push_budget <= 0.0:
			break
	return position


func _tangential_slide_point(
	center: Vector2,
	radius: float,
	radial_direction: Vector2,
	travel_step: Vector2,
	destination: Vector2 = Vector2.INF
) -> Vector2:
	## TANGENTIAL SLIDE. The radial push alone deadlocks whenever a blocking
	## structure's centre sits on the line of travel: the push
	## `center + offset/|offset| * radius` is then exactly ANTI-PARALLEL to the
	## step, so every tick moves the unit forward by one step and shoves it back
	## onto the same ring point. Measured on the seeded fixture: an attacker at
	## (80, 200) ordered onto a fortress at (100, 200) with a barracks at
	## (90, 200) parks at (87.2, 200.0) — exactly barracks + (-2.8, 0) — with its
	## route still length 1 after 600 ticks. _step_route's 3-tick stall escape
	## never fires because the attack state re-assigns the route every tick,
	## which resets route_stall_ticks before it can reach the threshold.
	##
	## The fix walks the unit AROUND the disc instead of standing it off:
	## project the step onto the tangent at the unit's current bearing and
	## re-seat the result on the ring, so the unit keeps its clearance while
	## making angular progress toward the far side.
	##
	## DETERMINISTIC SIDE CHOICE. Which way round is decided by the sign of the
	## 2-D cross product of the OBSTACLE OFFSET (centre -> unit) with the travel
	## direction. Off-axis that sign is the side the unit is already drifting
	## toward, so the slide never fights the approach. ON-AXIS — the deadlock
	## case — the cross product is zero and a fixed fallback side takes over.
	##
	## WHY A FIXED FALLBACK IS THE RIGHT ANSWER, stated correctly. An earlier
	## version of this comment justified it as "any position-derived tie-break
	## would make two lockstep peers disagree". That is wrong on its face:
	## position IS replicated state, every peer holds the same value, and a
	## position-derived choice would replicate fine. The real reason is
	## NUMERICAL: near the axis the cross product is a difference of two nearly
	## equal products, so its SIGN is the least trustworthy bit in the whole
	## computation — it is exactly the quantity that a fused multiply-add
	## contracts differently on different CPUs, and that ordinary rounding flips
	## from tick to tick on a single CPU. A fixed side is stable under both.
	##
	## THE NEAR-ZERO BAND IS PART OF THE FIX, not padding. Snapping only the
	## exact 0.0 left a band around the axis where |cross| is nonzero but its
	## sign is FMA-dependent: two peers on different microarchitectures compute
	## the same inputs, contract `x1*y2 - y1*x2` differently, and pick opposite
	## sides — the unit walks round the disc clockwise on one peer and
	## counter-clockwise on the other, and the sim desyncs. The lockstep runners
	## cannot catch this: both peers in those tests are the SAME binary on the
	## SAME machine, so they always contract identically and always agree. The
	## band is therefore a hazard that only a heterogeneous match exposes, which
	## is why it is closed by construction rather than by test.
	##
	## 1e-6 is the same tolerance this file already uses for "this vector is
	## degenerate" (`length_squared() <= 0.000001`, both in this function and in
	## _deflect_around_structures), so the geometry has one epsilon, not two.
	## Inside the band the fixed side (+1, counter-clockwise in the sim's X/Z
	## frame) applies; it is a pure function of the two vectors, so every peer
	## computes it identically.
	## Dest-side when a remaining waypoint is known: both lockstep peers hold
	## the same destination, so this does not re-open the FMA sign hazard that
	## forced a fixed +1 on-axis. Walking a 1-point LOS through a building
	## is exactly on-axis; +1 then orbits the long way. The shorter remaining
	## distance is the short side of the disc.
	var cross := radial_direction.cross(travel_step)
	var side := signf(cross) if absf(cross) > 0.000001 else 1.0
	if destination != Vector2.INF:
		var perp := Vector2(-radial_direction.y, radial_direction.x)
		var left := center + radial_direction * radius + perp * travel_step.length()
		var right := center + radial_direction * radius - perp * travel_step.length()
		var left_gap := left.distance_squared_to(destination)
		var right_gap := right.distance_squared_to(destination)
		if absf(left_gap - right_gap) > 0.000001:
			side = 1.0 if left_gap < right_gap else -1.0
	var tangent := Vector2(-radial_direction.y, radial_direction.x) * side
	var slid := center + radial_direction * radius + tangent * travel_step.length()
	var seated := slid - center
	if seated.length_squared() <= 0.000001:
		return center + radial_direction * radius
	return center + seated.normalized() * radius


func _step_structure_eviction() -> void:
	## Standing still is exactly when a footprint has to reassert itself.
	##
	## The castle pass-through ends with the ORDER, not with the route, and
	## `_step_entity` only reaches `_step_route` while a unit is moving: an
	## attacking, idle, stopped, or freshly-untargeted battalion never touches the
	## deflection at all. A melee horde that walked inside a castle to attack it
	## and then stopped, retargeted, or outlived its target therefore sat clipped
	## inside the wall for the rest of the match. This pass runs every tick for
	## every living battalion and pushes it back out at
	## STRUCTURE_EVICTION_STEP per tick.
	##
	## It consults the SAME pass-through set as movement, so a battalion whose
	## order still opens a corridor is not fought against while it is attacking —
	## only once the order ends does the set empty and the walk-out begin.
	##
	## Deterministic: ascending entity id, fixed step, no wall clock, no RNG.
	##
	## HOISTED. structure_ids() allocates a new array and SORTS it on every call;
	## calling it once per entity made this pass O(entities * structures) with a
	## sort inside the entity loop. Structures are neither added nor removed by
	## this pass (it only moves entities), so the id list is stable for its whole
	## duration and _deflect_around_structures re-checks structures.has() anyway.
	var ids: Array[int] = structure_ids()
	if ids.is_empty():
		return
	for id in entity_ids():
		var row: Dictionary = entities[id]
		if int(row.get("health", 0)) <= 0 or bool(row.get("flying", false)) or entity_container.has(id):
			continue
		if bool(row.get("is_banner_carrier", false)):
			# A BANNER CARRIER HAS NO POSITION OF ITS OWN. It is glued to its
			# parent horde by _sync_banner_entity_transform, which runs in
			# _step_banner_carriers — the pass IMMEDIATELY BEFORE this one, in
			# the same tick (see the tick order at _step_banner_carriers /
			# _step_structure_eviction). Evicting it therefore fights a value
			# that is not the eviction pass's to own: the nudge is overwritten
			# by the next tick's glue, and in the window between the two the
			# authoritative banner position is wrong for the spatial index and
			# for presentation.
			#
			# It is not hypothetical. A horde parked against its own castle wall
			# — the ordinary defensive posture — puts its banner inside a
			# structure footprint, so this pass nudged it EVERY TICK for as long
			# as the horde stood there, and every one of those nudges was
			# discarded by the following tick's glue.
			#
			# The parent horde is still evicted normally; the banner follows it
			# out because it follows it everywhere.
			continue
		if not (row.get("route", []) as Array).is_empty():
			continue  # already deflected this tick inside _step_route
		if int(row.get("production_exit_start_tick", -1)) >= 0:
			# THE DOORWAY IS INSIDE THE FOOTPRINT, BY CONTRACT. QueueProductionExit
			# creates a horde at production_origin + PRODUCTION_DOOR_INSET_RADIUS
			# (0.9) along the exit direction and walks it out to
			# PRODUCTION_EXIT_RADIUS (4.25) — see _step_production. A producer's
			# block radius is 2.6-3.0, so the authored create point is 1.7-2.1
			# units INSIDE its own disc and _step_production_exit owns the unit's
			# position for the whole animation (it lerps origin -> destination
			# every tick and leaves route empty with state "run"). Round 18's pass
			# therefore fought the doorway on every single unit the game produced.
			#
			# MEASURED, and measured PRECISELY — the effect is real but bounded,
			# and overstating it would be as wrong as missing it. In the
			# retail_state_pin fixture the produced horde's AUTHORITATIVE
			# end-of-tick position is exactly STRUCTURE_EVICTION_STEP (0.35) off
			# the authored lerp for the whole time it is inside the producer's
			# disc, and then the two reconverge byte for byte
			# (workspace/scratch/opus26-pin-positions-{new,revert-prodexit}.json.tick*):
			#   t=211  penetration 1.7851 vs 1.4351   (0.3500 apart)
			#   t=214  penetration 1.2681 vs 0.9181   (0.3500 apart)
			#   t=217  penetration 0.5030 vs 0.1530   (0.3500 apart)
			#   t=220  identical, and identical at every later sample
			# It does NOT accumulate, because the next tick's lerp overwrites the
			# nudge — which is exactly why neither pinned hash moves. What it does
			# corrupt is the end-of-tick state everything else reads inside that
			# window: the spatial index, presentation, and any query against the
			# authoritative position while a unit is emerging.
			#
			# The exit is a bounded, self-terminating animation that ends OUTSIDE
			# the footprint; eviction resumes the tick it completes.
			continue
		if _is_engaged_in_range(row):
			# A unit that is attacking something it can actually hit must not be
			# shoved out of its own weapon range. The castle corridor only exempts
			# STRUCTURE targets in the target's own castle group, so a melee horde
			# fighting an enemy BATTALION that happens to stand on a footprint —
			# defenders backed against their own barracks, a fight spilling onto a
			# wall — was evicted out of contact and had to walk back in, every tick.
			continue
		var position := Vector2(row.get("position", Vector2.ZERO))
		# An IDLE battalion is not executing its order, whatever `target_id` still
		# says, so it gets no corridor. Measured need: the live slice parks a unit
		# with attack_range 0.0 in state `idle` at d=0.00 on the enemy fortress
		# CENTRE while still holding it as a target
		# (workspace/scratch/opus25-probe1.out.log:`3:idle:t2001:d0.00`) — the
		# weapon-mode gate returns "unsupported-close", drops the route and idles,
		# but never clears the target, so a permanent corridor kept a unit clipped
		# inside the wall for the whole match. Gating on state, not on target,
		# is what makes "the exemption ends with the order" actually true.
		var executing := String(row.get("state", "")) in ["run", "attack"]
		var evicted := _deflect_around_structures(
			position,
			row if executing else {"facing": row.get("facing", Vector2.ZERO)},
			Vector2.ZERO,
			ids
		)
		if evicted == position:
			continue
		row["position"] = evicted
		_spatial_sync(row)


func _is_engaged_in_range(row: Dictionary) -> bool:
	## True when this battalion is in the attack state against a LIVING target it
	## is currently within weapon range of — of ANY kind, battalion or structure.
	## It must use EXACTLY the test _step_attacks uses to enter the state, or a
	## unit that is legitimately engaged gets evicted out of its own weapon range
	## every tick: surface-to-surface against a structure (the target's authored
	## bounding circle subtracted), centre-to-centre against a battalion. See the
	## citation block at that range test.
	if String(row.get("state", "")) != "attack":
		return false
	var target_id := int(row.get("target_id", 0))
	if target_id == 0:
		return false
	var target_kind := String(row.get("target_kind", "battalion"))
	var target_row: Dictionary = (
		structures.get(target_id, {}) if target_kind == "structure" else entities.get(target_id, {})
	)
	if target_row.is_empty() or int(target_row.get("health", 0)) <= 0:
		return false
	var attack_range := float(row.get("attack_range", 0.0))
	if attack_range <= 0.0:
		return false
	var distance := maxf(
		0.0,
		Vector2(row.get("position", Vector2.ZERO)).distance_to(
			Vector2(target_row.get("position", Vector2.ZERO))
		) - _target_footprint_radius(target_id, target_kind)
	)
	return distance <= attack_range


func _eviction_fallback_direction(row: Dictionary) -> Vector2:
	## A unit standing EXACTLY on a footprint centre has no radial direction to
	## be pushed along, and the old code left it there permanently (the
	## `distance > 0.001` guard skipped it). Push it along its own facing, which
	## is deterministic state already replicated in lockstep; a zero facing falls
	## back to a fixed axis so the result never depends on iteration order, wall
	## clock, or RNG.
	var facing := Vector2(row.get("facing", Vector2.ZERO))
	if facing.length_squared() > 0.000001:
		return facing.normalized()
	return Vector2.RIGHT


## Locomotion has no invented constants. Every number below comes from the
## authored Locomotor template the object's LocomotorSet binds, compiled by
## importer/openbfme_importer/locomotor_compiler.py and carried on the row.


func _should_honor_turn_rate(row: Dictionary) -> bool:
	## Authored TurnTime reaches the row as a positive
	## turn_rate_degrees_per_second. Old/synthetic fixtures also carry the
	## positive field explicitly, so they exercise the same arithmetic. A zero or
	## absent value is the only missing-data sentinel and retains snap/direct
	## movement; no provenance label or category can fabricate a rate.
	if float(row.get("turn_rate_degrees_per_second", 0.0)) <= 0.0:
		_report_turn_rate_fallback(row)
		return false
	return true


func _report_turn_rate_fallback(row: Dictionary) -> void:
	var unit_type := String(row.get(
		"unit_type", row.get("source_object_id", row.get("horde_id", "<unknown>"))
	))
	if _turn_rate_fallback_unit_types.has(unit_type):
		return
	_turn_rate_fallback_unit_types[unit_type] = true
	print(
		"RETAIL_TURN_MODEL missing_authored_turn_rate unit_type=%s fallback=pre_change_snap_direct"
		% unit_type
	)


func _should_reform(row: Dictionary) -> bool:
	## The reform gate exists only where retail authors MaxTurnWithoutReform.
	## Absence means "no reform gate", not a guessed arc.
	if retail_formation_movement:
		return true
	return float(row.get("max_turn_without_reform_degrees", 0.0)) > 0.0


func _retail_reform_threshold_degrees(row: Dictionary) -> float:
	return _movement_subsystem()._retail_reform_threshold_degrees(row)


func _retail_turn_rate_degrees(row: Dictionary) -> float:
	return _movement_subsystem()._retail_turn_rate_degrees(row)


func _step_retail_heading(
	row: Dictionary,
	movement_direction: Vector2,
	braking: float,
	effective_turn_rate_degrees_per_second: float
) -> bool:
	return _movement_subsystem()._step_retail_heading(row, movement_direction, braking, effective_turn_rate_degrees_per_second)


func _step_route(row: Dictionary) -> void:
	_movement_subsystem()._step_route(row)


func _consume_route_point_layer(row: Dictionary) -> void:
	if not row.has("route_point_layers"):
		return
	var layers: Array = row.get("route_point_layers", []) as Array
	if layers.is_empty():
		return
	var layer := String(layers.pop_front())
	row["route_point_layers"] = layers
	var elevations: Array = row.get("route_point_elevations", []) as Array
	var elevation := 0.0
	if not elevations.is_empty():
		elevation = float(elevations.pop_front())
		row["route_point_elevations"] = elevations
	if layer in ["ground", "ramp", "deck"]:
		row["pathing_layer"] = layer
		if layer == "ground":
			row.erase("pathing_elevation")
		else:
			row["pathing_elevation"] = elevation


func _should_attempt_crush(row: Dictionary, translation_speed: float, max_speed: float) -> bool:
	if max_speed <= 0.0:
		return false
	if row.has("crush_damage") and int(row.get("crush_damage", 0)) > 0:
		var min_percent := float(row.get("min_crush_velocity_percent", 40.0))
		return translation_speed + 0.0001 >= max_speed * (min_percent / 100.0)
	# Descriptor-backed units must supply their retail CrusherLevel/CrushWeapon
	# inputs. The category fallback exists only for old synthetic fixtures whose
	# rules predate the compiled crush fields.
	if row.has("module_contracts"):
		return false
	return String(row.get("category", "")) == "cavalry" and translation_speed > max_speed * 0.4


func _try_cavalry_trample(row: Dictionary) -> void:
	## Authored crush when crush_damage/crusher_level are present. Otherwise
	## the legacy cavalry 0.5 pulse so the pin and slice trample checks stay
	## put on packs that predate the fields.
	var cooldown := int(row.get("trample_cooldown", 0))
	if cooldown > 0:
		row["trample_cooldown"] = cooldown - 1
		return
	var authored_damage := int(row.get("crush_damage", 0))
	var has_authored := row.has("crush_damage") and authored_damage > 0
	if has_authored:
		var max_speed := float(row.get("speed", 0.0))
		var min_percent := float(row.get("min_crush_velocity_percent", 40.0))
		if max_speed > 0.0 and float(row.get("current_speed", 0.0)) + 0.0001 < max_speed * (min_percent / 100.0):
			return
	elif row.has("module_contracts") or String(row.get("category", "")) != "cavalry":
		return
	var team := int(row.get("team", PLAYER_TEAM))
	var origin := Vector2(row.get("position", Vector2.ZERO))
	var best_id := _spatial_nearest_hostile(
		row, team, origin, TRAMPLE_COLLISION_RADIUS, SPATIAL_FILTER_NOT_FLYING
	)
	if best_id == 0:
		return
	var victim: Dictionary = entities[best_id] as Dictionary
	if not _squish_collision_admitted(victim):
		_emit_event("combat.crush_refused", int(row.get("id", 0)), best_id, {"reason": "victim-missing-squish-collide"})
		return
	if has_authored:
		var crusher_level := int(row.get("crusher_level", 0))
		var victim_level := int(victim.get("crushable_level", 0))
		if crusher_level <= victim_level:
			return
	var damage := 0
	if has_authored:
		damage = maxi(
			1,
			int(round(float(authored_damage) * _timed_modifier_product(row, "CRUSH")))
		)
	else:
		damage = maxi(1, int(round(float(row.get("member_damage", 1)) * float(row.get("member_count", 1)) * TRAMPLE_DAMAGE_FACTOR * _timed_modifier_product(row, "CRUSH"))))
	# A braced shield wall blunts the charge (retail pike/shield counterplay).
	damage = maxi(1, roundi(float(damage) * float(_formation_effects(victim).get("trample_damage_multiplier", 1.0))))
	_apply_damage(int(row.get("id", 0)), best_id, damage, "battalion")
	row["trample_cooldown"] = TRAMPLE_COOLDOWN_TICKS
	var payload := {
		"amount": damage,
		"category": String(row.get("category", "cavalry")),
	}
	if has_authored:
		payload["weapon"] = String(row.get("crush_weapon_id", ""))
		_emit_event("combat.crush", int(row.get("id", 0)), best_id, payload)
	# Alias kept so existing slice/knockback listeners still see a pulse.
	_emit_event("combat.trample", int(row.get("id", 0)), best_id, payload)
	_apply_crush_deceleration(row, victim)
	# CrushRevengeWeapon: victim reflects authored nugget damage at the
	# crusher. Weapon id without authored damage is fail-closed (no invent).
	if victim.has("crush_revenge_damage"):
		var revenge := int(victim.get("crush_revenge_damage", 0))
		if revenge > 0 and int(row.get("health", 0)) > 0:
			_apply_damage(best_id, int(row.get("id", 0)), revenge, "battalion")
			_emit_event("combat.crush_revenge", best_id, int(row.get("id", 0)), {
				"amount": revenge,
				"weapon": String(victim.get("crush_revenge_weapon_id", "")),
			})
	var knockback_strength := TRAMPLE_KNOCKBACK_STRENGTH
	if has_authored and row.has("crush_knockback"):
		knockback_strength = maxf(0.0, float(row.get("crush_knockback", TRAMPLE_KNOCKBACK_STRENGTH)))
	_apply_knockback(origin, TRAMPLE_COLLISION_RADIUS, knockback_strength, team, 0, "trample", int(row.get("id", 0)))


func _apply_crush_deceleration(crusher: Dictionary, victim: Dictionary) -> void:
	## The crusher pays for the crush in speed, and a braced formation makes it
	## pay much more.
	##
	## RETAIL ORACLE, two halves:
	##  * the crusher's locomotor authors `CrushDecelerationPercent`, and retail
	##    annotates the number itself:
	##    object/cinematic/cinematicobjects.ini:2264
	##    `CrushDecelerationPercent = 20 ; Lose 80 percent of max velocity when
	##    crushing.` -- so the authored percent is the fraction of speed KEPT
	##    and (100 - percent) is the loss.
	##  * the victim's FORMATION ModifierList authors `CRUSHED_DECELERATE`, and
	##    attributemodifier.ini:49 documents it as "Multiplicitive. The
	##    percentage that things crushing you slow" -- it scales that loss. A
	##    porcupine horde authors `CRUSHED_DECELERATE 1000%`
	##    (attributemodifier.ini:762), i.e. ten times the loss, which stops the
	##    charge dead.
	##
	## No authored CrushDecelerationPercent means no deceleration term at all:
	## absent stays absent, never a default. Until this landed, the compiled
	## `crush_deceleration_percent` was stored on the row and read by nothing.
	if not crusher.has("crush_deceleration_percent"):
		return
	var kept := clampf(float(crusher.get("crush_deceleration_percent", 100.0)) / 100.0, 0.0, 1.0)
	var scale := maxf(0.0, _timed_modifier_product(victim, "CRUSHED_DECELERATE"))
	var loss := clampf((1.0 - kept) * scale, 0.0, 1.0)
	if loss <= 0.0:
		return
	crusher["current_speed"] = maxf(0.0, float(crusher.get("current_speed", 0.0)) * (1.0 - loss))


func _resume_order_after_knockdown(row: Dictionary) -> bool:
	## Re-path a battalion that just stood up back onto the order it was
	## carrying when it was knocked down: its live attack target first, then a
	## pending move destination. Returns false when there is nothing left to
	## resume (order complete, target dead, or the route is now unreachable),
	## in which case the caller settles it into idle.
	var target_id := int(row.get("target_id", 0))
	if target_id != 0:
		var target: Dictionary = {}
		if String(row.get("target_kind", "battalion")) == "structure":
			target = structures.get(target_id, {}) as Dictionary
		else:
			target = entities.get(target_id, {}) as Dictionary
		if not target.is_empty() and int(target.get("health", 0)) > 0:
			if _assign_target_route(row, Vector2(target["position"])):
				row["state"] = "run"
				return true
		row["target_id"] = 0
		row["target_kind"] = "battalion"
	var destination := Vector2(row.get("destination", row["position"]))
	if destination.distance_to(Vector2(row["position"])) > 0.001 and _assign_route(row, destination):
		row["state"] = "run"
		return true
	_clear_pending_route(row, true)
	return false


func _apply_knockback(center: Vector2, radius: float, strength: float, source_team: int, damage: int, damage_reason: String, source_id: int = 0, taper_off: float = 0.0, z_mult: float = 1.0) -> int:
	## Deterministic radial knockback: sweep enemy battalions in ascending id
	## order, throw each away from the center (clamped to walkable ground),
	## knock them down for KNOCKDOWN_DURATION_TICKS, and apply the optional
	## damage through the existing damage path. Allies and flyers are immune.
	var affected := 0
	# Only battalions inside the blast disc can be thrown, so the old full sweep
	# is a neighbourhood query. Sorted to keep the documented ascending-id order,
	# which damage application and event emission both observe.
	for id in _spatial_gather_sorted(center, radius):
		if not entities.has(id):
			continue
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
		if int(row.get("knockdown_ticks", 0)) > 0:
			# Already sprawled on the ground: a charge cannot fling a battalion
			# that is still lying there, and it cannot refresh the timer either.
			# KNOCKDOWN_DURATION_TICKS(25) outlives TRAMPLE_COOLDOWN_TICKS(10)
			# and TRAMPLE_KNOCKBACK_STRENGTH(2.0) throws the victim less far
			# than TRAMPLE_COLLISION_RADIUS(2.5), so without this guard a single
			# cavalry battalion re-downs the same clump every 10 ticks forever:
			# the victims never stand, never retaliate, and are ground to dust
			# for free. Damage still lands on a prone target.
			if damage > 0:
				_apply_damage(source_id, id, damage, "battalion")
			continue
		var direction := (position - center) / distance if distance > 0.001 else Vector2.RIGHT
		# The generic deterministic MetaImpact representation applies the proven
		# radial amount. ShockWaveTaperOff and ShockWaveZMult are retained in the
		# event receipt, but their retail force curve is not projected into this
		# 2D sim until the remaining engine helper semantics are proven.
		var applied_strength := strength
		# Try the full throw first, then shorter deterministic fractions so a
		# victim near water/cliff lands on the nearest walkable spot instead
		# of being stranded on unwalkable cells.
		var landed := position
		for fraction in [1.0, 0.5, 0.25]:
			var candidate := position + direction * applied_strength * float(fraction)
			if _position_walkable(candidate):
				landed = candidate
				break
		row["position"] = landed
		_spatial_sync(row)
		row["knockdown_ticks"] = KNOCKDOWN_DURATION_TICKS
		row["knocked_down"] = true
		row["current_speed"] = 0.0
		row["attack_windup"] = 0
		# The order survives the fall. Being bowled over interrupts a battalion,
		# it does not make it forget what it was told to do; the route is
		# dropped (the victim was displaced) and re-pathed on stand-up. Wiping
		# target_id/destination here made every knockdown a permanent
		# disarm, because nothing ever re-issues the player's order.
		_clear_member_attack_schedule(row)
		_clear_member_targets(row)
		_clear_pending_route(row, false)
		row["state"] = "knocked_down"
		if damage > 0:
			_apply_damage(source_id, id, damage, "battalion")
		var knockback_event := {
			"reason": damage_reason,
			"center": [snappedf(center.x, 0.001), snappedf(center.y, 0.001)],
			"landed": [snappedf(landed.x, 0.001), snappedf(landed.y, 0.001)],
			"knockdown_ticks": KNOCKDOWN_DURATION_TICKS,
		}
		if taper_off > 0.0:
			knockback_event["shockwave_taper_off"] = taper_off
			knockback_event["shockwave_z_mult"] = z_mult
			knockback_event["generic_metaimpact_projection"] = true
		_emit_event("combat.knockback", source_id, id, knockback_event)
		affected += 1
	return affected


func _apply_damage(attacker_id: int, target_id: int, amount: int, target_kind: String = "battalion", death_type: String = "NORMAL", damage_type_override: String = "") -> void:
	_combat_subsystem()._apply_damage(attacker_id, target_id, amount, target_kind, death_type, damage_type_override)


func _incoming_damage_factor(attacker_id: int, target: Dictionary, target_kind: String, damage_type: String, components: Array = []) -> float:
	## The full compiled multiplier an incoming hit applies before rounding:
	## weapon DamageScalar target filters, the armor.ini set (with recorded
	## applied armor upgrades), stance, formation, and ability/aura factors.
	var weapon_factor := 1.0
	if entities.has(attacker_id):
		var attacker_effect := _applied_weapon_effect(entities[attacker_id] as Dictionary)
		if not attacker_effect.is_empty():
			weapon_factor = _damage_scalar_factor(attacker_effect.get("scalars", []) as Array, target, target_kind)
	var factor := weapon_factor * _member_body_damage_factor(
		target, damage_type, components
	)
	if target_kind == "battalion" and _is_flanking_hit(attacker_id, target):
		factor *= _flanked_penalty_multiplier(target)
	return factor


func _active_armor_table(target: Dictionary) -> Dictionary:
	var rule: Dictionary = _unit_armor.get(String(target.get("object_id", "")), {})
	if rule.is_empty() or bool(rule.get("passthrough", false)):
		return {}
	var table := rule
	var active := String(target.get("active_armor_upgrade", ""))
	if active != "":
		var upgrades: Dictionary = rule.get("upgrades", {})
		if (target.get("applied_upgrades", {}) as Dictionary).has(active) and upgrades.has(active):
			table = upgrades[active]
	return table


func _flanked_penalty_multiplier(target: Dictionary) -> float:
	## armor.ini FlankedPenalty is extra incoming damage when hit from behind
	## the facing hemisphere. SoldierArmor authors 50% → 1.5x. No field → 1.0.
	var table := _active_armor_table(target)
	var penalty := float(table.get("flanked_penalty", 0.0))
	if not is_finite(penalty) or penalty <= 0.0:
		return 1.0
	return 1.0 + penalty


func _flanking_outgoing_multiplier(attacker: Dictionary, target: Dictionary) -> float:
	## weapon.ini FlankingBonus is extra outgoing damage when the hit is
	## behind the target facing. GondorSword 50% → 1.5x. Absent → 1.0.
	var bonus := float(attacker.get("flanking_bonus", 0.0))
	if bonus <= 0.0 or not is_finite(bonus):
		return 1.0
	if not _is_flanking_hit(int(attacker.get("id", 0)), target):
		return 1.0
	return 1.0 + bonus / 100.0


func _is_flanking_hit(attacker_id: int, target: Dictionary) -> bool:
	if not entities.has(attacker_id):
		return false
	var facing := Vector2(target.get("facing", Vector2.ZERO))
	if facing.length_squared() <= 0.000001:
		return false
	var origin := Vector2(target.get("position", Vector2.ZERO))
	var attacker_at := Vector2((entities[attacker_id] as Dictionary).get("position", Vector2.ZERO))
	var from_target := attacker_at - origin
	if from_target.length_squared() <= 0.000001:
		return false
	return facing.normalized().dot(from_target.normalized()) < 0.0


func _member_body_damage_factor(
	target: Dictionary, damage_type: String, components: Array = []
) -> float:
	## ActiveBody-side factors only. Outgoing weapon/ability scalars have
	## already formed DamageInfo.in.m_amount before Body::attemptDamage.
	return (
		_member_armor_scalar(target, damage_type, components)
		# INNATE_ARMOR: a damage-TAKEN scalar the object carries for its whole
		# life, as against the timed ability/aura grants above it. Retail's
		# Create-a-Hero armour ladder is authored exactly this way, so a HIGHER
		# step means MORE damage taken and the number arrives already inverted.
		# Absent on every retail unit, which keeps this factor exactly 1.0 there.
		* float(target.get("innate_armor_scalar", 1.0))
		* float(_stance_state(target).get("incomingDamageMultiplier", 1.0))
		* float(_formation_effects(target).get("incoming_damage_multiplier", 1.0))
		* _ability_incoming_multiplier(target)
	)


func _highlander_raw_damage_amount(
	raw_amount: float,
	current_health: int,
	damage_type: String,
	damage_components: Array
) -> float:
	## HighlanderBody mutates raw DamageInfo before ActiveBody applies armor.
	## Only exact DAMAGE_UNRESISTABLE bypasses the clamp. A mixed aggregate
	## cannot preserve the source engine's per-DamageInfo ordering, so refuse
	## it explicitly rather than silently choosing immortal or lethal.
	if damage_type.to_lower() == "unresistable":
		return maxf(0.0, raw_amount)
	var has_unresistable := false
	var has_other := false
	for component_value in damage_components:
		if typeof(component_value) != TYPE_DICTIONARY:
			continue
		var component_type := String(
			(component_value as Dictionary).get("damage_type", "")
		).to_lower()
		if component_type == "unresistable":
			has_unresistable = true
		else:
			has_other = true
	if has_unresistable and has_other:
		return -1
	if has_unresistable:
		return maxf(0.0, raw_amount)
	return minf(maxf(0.0, raw_amount), float(maxi(0, current_health - 1)))


func _structure_uses_highlander_body(target: Dictionary) -> bool:
	return bool(target.get("highlander_body", false))


func _apply_member_damage(
	attacker_id: int,
	attacker_member_index: int,
	target_id: int,
	amount: int,
	target_kind: String,
	attack_sequence: int,
	forced_target_member: int = -1,
	damage_type_override: String = "",
	death_type: String = "NORMAL",
	damage_components_override: Variant = null
) -> void:
	_combat_subsystem()._apply_member_damage(attacker_id, attacker_member_index, target_id, amount, target_kind, attack_sequence, forced_target_member, damage_type_override, death_type, damage_components_override)


func _bookkeep_battalion_death(
	entity_id: int, row: Dictionary, death_type: String, defeated_members: Array[int],
	attacker_id: int = 0
) -> Dictionary:
	## Shared authoritative bookkeeping for every battalion lethal path. Callers
	## retain path-specific kill credit/events, but lifetime, corpse policy,
	## selection, routing, and command-point release cannot drift apart.
	_schedule_respawn_update(entity_id, row, death_type, attacker_id)
	_on_ring_entity_death(entity_id, row)
	_summon_despawn_ticks.erase(entity_id)
	_summon_aura_source_ids.erase(entity_id)
	# A produced hero's death releases its identity on every lethal path, not
	# only ordinary member combat. Spawn-roster heroes were never registered.
	var defeated_identity := "%d:%s" % [
		int(row.get("team", -1)), String(row.get("unit_type", ""))
	]
	if _completed_hero_identities.has(defeated_identity):
		_completed_hero_identities.erase(defeated_identity)
		_emit_event(
			"hero.identity_released", entity_id, 0,
			{"unit_type": String(row.get("unit_type", ""))}
		)
	if entities.has(attacker_id) and not bool(row.get("is_banner_carrier", false)):
		award_power_kill(int((entities[attacker_id] as Dictionary).get("team", -1)))
	var death_policy := _apply_playable_unit_death_policy(
		row, death_type, defeated_members
	)
	row["state"] = "death"
	row["target_id"] = 0
	_clear_pending_route(row, true)
	selected_ids.erase(entity_id)
	_release_command_points_once(row)
	prune_control_groups()
	return death_policy


func _release_command_points_once(row: Dictionary) -> void:
	_production_subsystem()._release_command_points_once(row)


func _entity_command_point_commitment(row: Dictionary) -> int:
	return _production_subsystem()._entity_command_point_commitment(row)


func _apply_playable_unit_death_policy(
	row: Dictionary, death_type: String, defeated_members: Array[int]
) -> Dictionary:
	## The single policy boundary for every materialized playable-unit lethal
	## path. Executable container/object DestroyDie removes the authoritative
	## battalion object. primaryMember remains descriptor evidence only because
	## this simulation has no independently materialized member-corpse object.
	## Callers retain their path-specific kill credit and event bookkeeping.
	var member_ticks: Array = row.get("member_corpse_expire_ticks", [])
	for member_index in defeated_members:
		if member_index >= 0 and member_index < member_ticks.size():
			member_ticks[member_index] = tick_index + CORPSE_LIFETIME_TICKS
	if not defeated_members.is_empty():
		row["member_corpse_expire_ticks"] = member_ticks
		_award_own_guys_die_experience(row, defeated_members.size())
	var destroy_object := (
		int(row.get("health", 0)) <= 0
		and (
			_destroy_die_matches(row, "container", death_type)
			or _destroy_die_matches(row, "object", death_type)
		)
	)
	# KeepObjectDie module contract: destroyOnDeath=false keeps a readable
	# corpse when the death type matches the attached policy (excluded types
	# do not keep — TOPPLED under ALL -TOPPLED still erases if DestroyDie says so).
	if _keep_object_die_matches(row, death_type):
		destroy_object = false
	if int(row.get("health", 0)) <= 0:
		# RefundDie is a generic Object DieMux behavior (MordorTributeCart is the
		# retail non-structure carrier), so battalion/object deaths share the same
		# once-only dispatch as structures.
		_apply_refund_die_on_death(row)
		_schedule_fire_weapon_when_dead(row, death_type, "entity")
		var slow_death_started := _begin_slow_death_core(row, death_type)
		# A matched DestroyDie used to erase the object on the SAME tick, which
		# handed the simulation an effective fade window of 0. Retail authors
		# that window as SlowDeathBehavior DestructionDelay on a
		# `DeathTypes = NONE +FADED` module (pure-retail range 1000..10000 ms).
		# When the pack carries the authored delay for THIS death type, the
		# object stays until it elapses; with no authored delay the immediate
		# removal is unchanged, so a pack that predates the emission behaves
		# exactly as before.
		if slow_death_started:
			# SlowDeath owns destruction even when DestroyDie also matched; do not
			# let the caller erase the object in the death callback.
			destroy_object = false
		else:
			var fade_ticks := _slow_death_fade_ticks(row, death_type) if destroy_object else 0
			row["corpse_expire_tick"] = (
				tick_index + fade_ticks if destroy_object else tick_index + CORPSE_LIFETIME_TICKS
			)
		_consume_create_object_die(row, death_type)
	return {
		"destroy_object": destroy_object,
	}


func _slow_death_core_matches(policy: Dictionary, death_type: String) -> bool:
	var folded := death_type.strip_edges().to_upper()
	if folded == "":
		folded = "NORMAL"
	var mode := String(policy.get("death_types", "")).to_upper()
	if mode == "ALL":
		for excluded_value in policy.get("excluded_death_types", []) as Array:
			if String(excluded_value).to_upper() == folded:
				return false
		return true
	if mode == "NONE":
		for included_value in policy.get("included_death_types", []) as Array:
			if String(included_value).to_upper() == folded:
				return true
	return false


func _slow_death_delay_ticks(milliseconds: float) -> int:
	if milliseconds <= 0.0:
		return 0
	# Retail parseDurationUnsignedInt rounds partial logic frames upward.
	return maxi(1, ceili(milliseconds / 1000.0 / TICK_SECONDS))


func _begin_slow_death_core(row: Dictionary, death_type: String) -> bool:
	if row.has("slow_death_state"):
		return true
	if not row.has("slow_death_core_contracts"):
		_attach_module_contracts(row)
	var applicable: Array[Dictionary] = []
	var total_weight := 0
	for policy_value in row.get("slow_death_core_contracts", []) as Array:
		if typeof(policy_value) != TYPE_DICTIONARY:
			continue
		var policy := policy_value as Dictionary
		if not _slow_death_core_matches(policy, death_type):
			continue
		var weight := maxi(0, int(policy.get("probability_weight", 0)))
		if weight <= 0:
			continue
		applicable.append(policy)
		total_weight += weight
	if applicable.is_empty() or total_weight <= 0:
		return false
	# Retail always draws 1..total, including a single applicable row.
	var roll := logic_random_int(1, total_weight)
	var selected: Dictionary = applicable[-1]
	for policy_value in applicable:
		var policy := policy_value as Dictionary
		roll -= int(policy.get("probability_weight", 0))
		if roll <= 0:
			selected = policy
			break
	# Retail also executes both zero-variance draws before its midpoint draw.
	var sink_delay_ms := float(selected.get("sink_delay_ms", 0.0))
	var destruction_delay_ms := float(selected.get("destruction_delay_ms", 0.0))
	logic_random_int(0, 0)
	logic_random_int(0, 0)
	var sink_ticks := _slow_death_delay_ticks(sink_delay_ms)
	var destruction_ticks := _slow_death_delay_ticks(destruction_delay_ms)
	var midpoint_offset := int(logic_random_real(
		0.35 * float(destruction_ticks),
		0.65 * float(destruction_ticks)
	))
	var state := {
		"death_type": death_type.strip_edges().to_upper(),
		"selected_tag": String(selected.get("tag", "")),
		"selected_source_ini": String(selected.get("source_ini", "")),
		"selected_line": int(selected.get("line", 0)),
		"initial_tick": tick_index,
		"sink_start_tick": tick_index + sink_ticks,
		"midpoint_tick": tick_index + midpoint_offset,
		"destruction_tick": tick_index + destruction_ticks,
		"sink_rate_source_per_second": float(
			selected.get("sink_rate_source_per_second", 0.0)
		),
		"sink_depth_source": 0.0,
		"executed_phases": [],
		"presentation_receipts": (
			selected.get("presentation_receipts", []) as Array
		).duplicate(true),
	}
	row["slow_death_state"] = state
	row["corpse_expire_tick"] = tick_index + destruction_ticks
	_record_slow_death_phase(row, "INITIAL")
	return true


func _record_slow_death_phase(row: Dictionary, phase: String) -> void:
	var state := row.get("slow_death_state", {}) as Dictionary
	if state.is_empty():
		return
	var phases := state.get("executed_phases", []) as Array
	if phases.has(phase):
		return
	phases.append(phase)
	state["executed_phases"] = phases
	# Retail chooses one FX vector entry with the logic stream at each phase.
	# Preserve that deterministic choice as a receipt, but do not emit an FX.
	var fx_candidates: Array[String] = []
	for receipt_value in state.get("presentation_receipts", []) as Array:
		if typeof(receipt_value) != TYPE_DICTIONARY:
			continue
		var receipt := receipt_value as Dictionary
		if (
			String(receipt.get("kind", "")) == "FX"
			and String(receipt.get("phase", "")) == phase
		):
			for reference_value in receipt.get("references", []) as Array:
				fx_candidates.append(String(reference_value))
	if not fx_candidates.is_empty():
		var choice_index := logic_random_int(0, fx_candidates.size() - 1)
		var choices := state.get("presentation_choices", []) as Array
		choices.append({
			"kind": "FX",
			"phase": phase,
			"selected_reference": fx_candidates[choice_index],
			"runtime_status": "deferred-presentation",
		})
		state["presentation_choices"] = choices
	row["slow_death_state"] = state
	_emit_event("slow_death.phase_receipt", int(row.get("id", 0)), 0, {
		"phase": phase,
		"presentation_status": "deferred",
		"selected_tag": String(state.get("selected_tag", "")),
	})


func _step_slow_death_core() -> void:
	for entity_id in entity_ids():
		if not entities.has(entity_id):
			continue
		var row := entities[entity_id] as Dictionary
		var state := row.get("slow_death_state", {}) as Dictionary
		if state.is_empty() or int(row.get("health", 0)) > 0:
			continue
		var rate := float(state.get("sink_rate_source_per_second", 0.0))
		if rate > 0.0 and tick_index >= int(state.get("sink_start_tick", 0)):
			state["sink_depth_source"] = (
				float(state.get("sink_depth_source", 0.0)) + rate * TICK_SECONDS
			)
			row["slow_death_state"] = state
		if tick_index >= int(state.get("midpoint_tick", 0)):
			_record_slow_death_phase(row, "MIDPOINT")
		if tick_index >= int(state.get("destruction_tick", 0)):
			_record_slow_death_phase(row, "FINAL")


func _award_own_guys_die_experience(row: Dictionary, defeated_count: int) -> void:
	_experience_subsystem().award_own_guys_die_experience(row, defeated_count)


func _slow_death_fade_ticks(row: Dictionary, death_type: String) -> int:
	## Authored DestructionDelay for this death type, in ticks, or 0.
	##
	## Fail-closed by construction: the adapter only projects rows whose delay
	## retail actually authored and whose expression resolved to a number, so an
	## absent or unresolvable delay reaches here as no row at all and the
	## removal stays immediate. The longest matching authored window wins when
	## an object stacks several modules on one death type.
	var rows: Array = row.get("slow_death_fades", []) as Array
	if rows.is_empty():
		return 0
	var death_folded := death_type.strip_edges().to_upper()
	if death_folded == "":
		death_folded = "NORMAL"
	var delay_ms := 0.0
	for policy_value in rows:
		if typeof(policy_value) != TYPE_DICTIONARY:
			continue
		var policy := policy_value as Dictionary
		if String(policy.get("death_types", "")).to_upper() != "NONE":
			continue
		for included_value in policy.get("included_death_types", []) as Array:
			if String(included_value).to_upper() == death_folded:
				delay_ms = maxf(delay_ms, float(policy.get("destruction_delay_ms", 0.0)))
				break
	if delay_ms <= 0.0:
		return 0
	return maxi(1, roundi(delay_ms / 1000.0 / TICK_SECONDS))


func _schedule_fire_weapon_when_dead(row: Dictionary, death_type: String, source_kind: String) -> void:
	var policies: Array = row.get("fire_weapon_when_dead", []) as Array
	if policies.is_empty():
		return
	var scheduled: Dictionary = row.get("fire_weapon_when_dead_scheduled", {}) as Dictionary
	for policy_value in policies:
		if typeof(policy_value) != TYPE_DICTIONARY:
			continue
		var policy := policy_value as Dictionary
		var once_key := "%s:%d" % [String(policy.get("tag", "")), int(policy.get("line", 0))]
		if scheduled.has(once_key):
			continue
		if not _death_mux_matches(policy, death_type):
			continue
		if not _death_status_mux_matches(row, policy):
			continue
		if (
			source_kind == "structure"
			and float(row.get("construction_progress", 1.0)) < 1.0
			and not bool(policy.get("active_during_construction", false))
		):
			continue
		var facing := Vector2(row.get("facing", Vector2.RIGHT))
		if facing.length_squared() <= 0.000001:
			facing = Vector2.RIGHT
		var local_offset := _retail_source_to_sim_offset(
			Vector2(policy.get("weapon_offset_source", Vector2.ZERO))
		)
		var point := Vector2(row.get("position", Vector2.ZERO)) + local_offset.rotated(facing.angle())
		var weapon_id := String(policy.get("death_weapon", ""))
		_pending_power_effects.append({
			"kind": "death_weapon",
			"fire_tick": tick_index + maxi(0, int(policy.get("delay_ticks", 0))),
			"team": int(row.get("team", -1)),
			"source_id": int(row.get("id", 0)),
			"source_kind": source_kind,
			"death_type": death_type.to_upper(),
			"weapon_id": weapon_id,
			"point": point,
			"height_source": float(policy.get("weapon_offset_z_source", 0.0)),
			"weapon_rule": (_death_weapon_rules.get(weapon_id, {}) as Dictionary).duplicate(true),
		})
		scheduled[once_key] = true
		_emit_event("module.death_weapon_scheduled", int(row.get("id", 0)), 0, {
			"weapon_id": weapon_id,
			"fire_tick": tick_index + maxi(0, int(policy.get("delay_ticks", 0))),
			"point": [snappedf(point.x, 0.001), snappedf(point.y, 0.001)],
			"height_source": float(policy.get("weapon_offset_z_source", 0.0)),
			"death_type": death_type.to_upper(),
		})
	row["fire_weapon_when_dead_scheduled"] = scheduled


static func _death_mux_matches(policy: Dictionary, death_type: String) -> bool:
	var folded := death_type.strip_edges().to_upper()
	if folded == "":
		folded = "NORMAL"
	var mode := String(policy.get("death_types", "ALL")).to_upper()
	if mode == "ALL":
		for excluded_value in policy.get("excluded_death_types", []) as Array:
			if String(excluded_value).to_upper() == folded:
				return false
		return true
	if mode == "NONE":
		for included_value in policy.get("included_death_types", []) as Array:
			if String(included_value).to_upper() == folded:
				return true
	return false


static func _death_status_mux_matches(row: Dictionary, policy: Dictionary) -> bool:
	var statuses: Dictionary = row.get("object_status", {}) as Dictionary
	for required_value in policy.get("required_status", []) as Array:
		if not bool(statuses.get(String(required_value), false)):
			return false
	for exempt_value in policy.get("exempt_status", []) as Array:
		if bool(statuses.get(String(exempt_value), false)):
			return false
	return true


func _create_object_die_matches(row: Dictionary, death_type: String) -> bool:
	if not bool(row.get("create_object_die", false)):
		return false
	var policy: Dictionary = row.get("create_object_die_policy", {}) as Dictionary
	if policy.is_empty():
		return false
	var death_folded := death_type.strip_edges().to_upper()
	if death_folded == "":
		death_folded = "NORMAL"
	var mode := String(policy.get("death_types", "ALL")).to_upper()
	if mode == "ALL":
		for excluded_value in policy.get("excluded_death_types", []) as Array:
			if String(excluded_value).to_upper() == death_folded:
				return false
	elif mode == "NONE":
		var included: Array = policy.get("included_death_types", []) as Array
		var hit := false
		for included_value in included:
			if String(included_value).to_upper() == death_folded:
				hit = true
				break
		if not hit:
			return false
	else:
		return false
	return String(policy.get("creation_list", "")) != ""


func _consume_create_object_die(row: Dictionary, death_type: String) -> void:
	## Queue CreationList spawn intent, then hatch through registered OCL leaves
	## when present (same machinery as spellbook summon chains).
	if not _create_object_die_matches(row, death_type):
		return
	var policy: Dictionary = row.get("create_object_die_policy", {}) as Dictionary
	var entry := {
		"team": int(row.get("team", -1)),
		"position": row.get("position", Vector2.ZERO),
		"creation_list": String(policy.get("creation_list", "")),
		"source_entity": int(row.get("id", 0)),
		"source_unit_type": String(row.get("unit_type", "")),
		"death_type": death_type,
		"tick": tick_index,
	}
	create_object_die_pending.append(entry)
	_emit_event(
		"module.create_object_die",
		int(row.get("id", 0)),
		0,
		{
			"creation_list": entry["creation_list"],
			"team": entry["team"],
		}
	)
	var hatch := hatch_create_object_die_entry(entry)
	entry["hatch"] = hatch
	create_object_die_pending[create_object_die_pending.size() - 1] = entry


func hatch_create_object_die_entry(entry: Dictionary) -> Dictionary:
	## Resolve OCL_id -> createObjects -> script_spawn_entity for each converted
	## object type. Unconverted leaves stay pending with reason (fail-closed).
	var ocl_id := String(entry.get("creation_list", ""))
	var team := int(entry.get("team", -1))
	var at: Vector2 = entry.get("position", Vector2.ZERO)
	if ocl_id == "" or team < 0:
		return {"ok": false, "reason": "invalid-entry"}
	var ocl: Dictionary = {}
	if parity != null:
		ocl = (parity.ocl_leaves as Dictionary).get(ocl_id, {}) as Dictionary
	if ocl.is_empty():
		# Try spellbook leaf tables already loaded on this sim.
		ocl = _ocl_leaf_lookup(ocl_id)
	if ocl.is_empty():
		return {"ok": false, "reason": "ocl-not-registered:%s" % ocl_id}
	var spawned: Array = []
	var ordinal := 0
	for create_value in Array(ocl.get("createObjects", [])):
		if typeof(create_value) != TYPE_DICTIONARY:
			continue
		var create := create_value as Dictionary
		var count := 1
		for field_value in Array(create.get("fields", [])):
			if typeof(field_value) == TYPE_DICTIONARY and String(
				(field_value as Dictionary).get("key", "")
			) == "Count":
				count = maxi(1, int((field_value as Dictionary).get("resolved", 1)))
		for object_name_value in Array(create.get("objects", [])):
			var object_name := String(object_name_value)
			for _i in range(count):
				ordinal += 1
				var offset := Vector2(8.0 * float(ordinal), 0.0)
				var result: Dictionary = spawn_scenario_object(
					object_name, team, at + offset, "object-creation-list"
				)
				if not bool(result.get("ok", false)):
					result = script_spawn_entity(
						object_name, team, at + offset, "object-creation-list"
					)
				if bool(result.get("ok", false)):
					var spawned_id := int(result.get("id", result.get("entity_id", 0)))
					spawned.append(spawned_id)
					_emit_event(
						"module.create_object_die.hatch",
						spawned_id,
						int(entry.get("source_entity", 0)),
						{
							"ocl": ocl_id,
							"object": object_name,
							"kind": String(result.get("kind", "unit")),
						}
					)
				else:
					# Fail-closed for this object; continue other creates.
					_emit_event(
						"module.create_object_die.hatch_failed",
						int(entry.get("source_entity", 0)),
						0,
						{
							"ocl": ocl_id,
							"object": object_name,
							"reason": String(result.get("reason", "")),
						}
					)
	if spawned.is_empty():
		return {"ok": false, "reason": "no-spawns", "ocl": ocl_id}
	return {"ok": true, "spawned": spawned, "ocl": ocl_id}


func _ocl_leaf_lookup(ocl_id: String) -> Dictionary:
	if parity != null and (parity.ocl_leaves as Dictionary).has(ocl_id):
		return (parity.ocl_leaves as Dictionary)[ocl_id]
	return {}


func register_ocl_leaf(ocl_id: String, leaf: Dictionary) -> void:
	_ensure_parity()
	parity.register_ocl_leaf(ocl_id, leaf)


func register_object_leaf(object_id: String, leaf: Dictionary) -> void:
	_ensure_parity()
	parity.register_object_leaf(object_id, leaf)


func ingest_ocl_leaves_from_document(document: Dictionary) -> int:
	## Register ObjectCreationList leaves from a spellbook/summon/pack document
	## so CreateObjectDie hatch can resolve CreationList ids without inventing
	## createObjects. Returns the number of OCL ids registered.
	_ensure_parity()
	var registered := 0
	var leaves: Dictionary = document.get("leaves", {}) as Dictionary
	for ocl_value in Array(leaves.get("objectCreationLists", [])):
		if typeof(ocl_value) != TYPE_DICTIONARY:
			continue
		var ocl := ocl_value as Dictionary
		var ocl_id := String(ocl.get("id", ""))
		if ocl_id == "":
			continue
		register_ocl_leaf(ocl_id, ocl)
		registered += 1
	# Top-level array form used by some converters.
	for ocl_value2 in Array(document.get("objectCreationLists", [])):
		if typeof(ocl_value2) != TYPE_DICTIONARY:
			continue
		var ocl2 := ocl_value2 as Dictionary
		var ocl_id2 := String(ocl2.get("id", ""))
		if ocl_id2 == "":
			continue
		register_ocl_leaf(ocl_id2, ocl2)
		registered += 1
	return registered


func fog_of_war():
	## The retail shroud grid for this match. Always returns a model - a match
	## with fog off gets one whose `enabled` is false and which no tick ever
	## stamps, so a presentation query is a cheap "everything is visible" rather
	## than a null check at every call site.
	if _fog_of_war == null:
		_fog_of_war = FogOfWarScript.new()
		_fog_of_war.enabled = fog_of_war_enabled
		var scale := float(_rules.get("source_map_transform_scale", 0.0))
		if scale <= 0.0:
			# No map transform in the rules (bare harness sims). One sim unit per
			# source unit is the only honest fallback; the cell then spans
			# PartitionCellSize directly.
			scale = 1.0
		if playable_outline.size() >= 3:
			var bounds_min := playable_outline[0]
			var bounds_max := playable_outline[0]
			for point in playable_outline:
				bounds_min = Vector2(minf(bounds_min.x, point.x), minf(bounds_min.y, point.y))
				bounds_max = Vector2(maxf(bounds_max.x, point.x), maxf(bounds_max.y, point.y))
			# One shroud cell of margin so a unit nudged onto the border by
			# eviction still has a cell of its own.
			var margin := Vector2.ONE * FogOfWarScript.cell_size_for_scale(scale)
			_fog_of_war.configure(bounds_min - margin, bounds_max + margin, scale)
		else:
			_fog_of_war.configure_default(scale)
	return _fog_of_war


func _shroud_clearing_radius(row: Dictionary) -> float:
	## Retail authors TWO independent ranges and this is the deshroud one.
	##   gamedata.ini:38-64 - SHROUD_CLEAR_* and VISION_* are separate macro
	##   families, and the shipped objects disagree constantly: MenFortressCitadel
	##   is VisionRange 400 / ShroudClearingRange 800, GondorSentryTower is
	##   600 / 500. Deriving one from the other would be wrong in both directions.
	## Falls back to vision_range only when the pack carries no deshroud value,
	## which is every unit compiled before this lane's importer change rides a
	## republish - named as a live gap in the report rather than hidden here.
	var shroud_range := float(row.get("shroud_clearing_range", 0.0))
	if shroud_range > 0.0:
		return shroud_range
	return float(row.get("vision_range", 0.0))


func source_transform_scale() -> float:
	## Source (retail) units to sim units for the loaded map. 1.0 when no map
	## transform is configured, which is what a bare harness sim gets.
	var scale := float(_rules.get("source_map_transform_scale", 0.0))
	return scale if scale > 0.0 else 1.0


func refresh_fog_of_war() -> void:
	## Stamp the shroud grid once, outside the tick. The presentation calls this
	## when a match is bound so the first frame already shows the player's own
	## base cleared; without it a match opens on a fully black screen that pops
	## clear one tick later.
	_step_shroud_grid()


func _step_shroud_grid() -> void:
	## The retail look pass: rebuild every team's clear set from scratch from the
	## authoritative entity and structure rows. A full rebuild rather than
	## retail's incremental refcount because the result is identical and a
	## rebuild cannot accumulate error across 3000 ticks; the one behavioural
	## difference is UnlookPersistDuration (gamedata.ini:9024, retail 1), the
	## delay before a vacated cell falls back to fog, which this model does not
	## implement. Named in the report.
	##
	## Iteration order follows entity_ids()/structure_ids(), the same sorted
	## order every other step uses, so two peers stamp in the same order.
	if not fog_of_war_enabled:
		return
	var fog = fog_of_war()
	fog.begin_look_pass()
	for eid in entity_ids():
		if not entities.has(eid):
			continue
		var row: Dictionary = entities[eid]
		if int(row.get("health", 0)) <= 0:
			continue
		var team := int(row.get("team", -1))
		if team < 0:
			continue
		var radius := _shroud_clearing_radius(row)
		if radius <= 0.0:
			continue
		# The entity id IS the looker key, which is what makes the pass
		# incremental: a battalion that has not left its shroud cell since last
		# tick costs one dictionary lookup here and touches no cell at all.
		fog.add_look(team, row.get("position", Vector2.ZERO), radius, eid)
	for sid in structure_ids():
		if not structures.has(sid):
			continue
		var srow: Dictionary = structures[sid]
		if int(srow.get("health", 0)) <= 0:
			continue
		var steam := int(srow.get("team", -1))
		if steam < 0:
			continue
		var sradius := _structure_shroud_clearing_radius(srow)
		if sradius <= 0.0:
			continue
		# Structure ids and entity ids share a numbering space in places, so the
		# structure key is negated to keep the two looker sets disjoint.
		fog.add_look(steam, srow.get("position", Vector2.ZERO), sradius, -sid)
	fog.commit_look_pass()


func _structure_shroud_clearing_radius(srow: Dictionary) -> float:
	## THE BUG THIS EXISTS TO FIX. Structure rows are built at eight separate
	## sites in this file and NOT ONE of them ever wrote `vision_range`,
	## `shroud_clearing_range` or `footprint_radius`, so the generic
	## `_shroud_clearing_radius` read 0 from every building ever placed and the
	## structure half of the look pass was dead code. The owner's first fog-on
	## playtest found it immediately: "I built a fortress and it gives no fog
	## visibility."
	##
	## Rather than teach eight construction sites the same field, the radius is
	## resolved from the row's kind against the team's compiled build rules -
	## which is where the manifest already puts every other authored number - so
	## a map-seeded fortress, a porter-built farm, an expansion pad tower, an
	## unpacked base and a summoned Lone Tower all get their vision by the same
	## path, the moment they exist.
	var direct := float(srow.get("shroud_clearing_range", 0.0))
	if direct > 0.0:
		return direct
	var rule: Dictionary = structure_build_rules_for_team(
		int(srow.get("team", -1))
	).get(String(srow.get("structure_kind", "")), {}) as Dictionary
	var source := float(rule.get("shroud_clearing_range_source", 0.0))
	if source <= 0.0:
		return 0.0
	return source * source_transform_scale()


func _step_fog_from_vision() -> void:
	## FoW consumer: living entities reveal fog for their team using vision_range.
	_ensure_parity()
	for eid in entity_ids():
		if not entities.has(eid):
			continue
		var row: Dictionary = entities[eid]
		if int(row.get("health", 0)) <= 0:
			continue
		var team := int(row.get("team", -1))
		if team < 0:
			continue
		var vision := float(row.get("vision_range", 0.0))
		if vision <= 0.0:
			continue
		parity.fog_reveal(team, row.get("position", Vector2.ZERO), vision, false)


func _keep_object_die_matches(row: Dictionary, death_type: String) -> bool:
	if not bool(row.get("keep_object_die", false)):
		return false
	var policy: Dictionary = row.get("keep_object_die_policy", {}) as Dictionary
	if policy.is_empty():
		return true
	var death_folded := death_type.strip_edges().to_upper()
	var mode := String(policy.get("death_types", "ALL")).to_upper()
	if mode == "ALL":
		for excluded_value in policy.get("excluded_death_types", []) as Array:
			if String(excluded_value).to_upper() == death_folded:
				return false
		return true
	if mode == "NONE":
		for included_value in policy.get("included_death_types", []) as Array:
			if String(included_value).to_upper() == death_folded:
				return true
	return false


func _destroy_die_matches(
	row: Dictionary, owner_role: String, death_type: String
) -> bool:
	for policy_value in row.get("destroy_die", []) as Array:
		if typeof(policy_value) != TYPE_DICTIONARY:
			continue
		var policy := policy_value as Dictionary
		if String(policy.get("owner_role", "")) != owner_role:
			continue
		var mode := String(policy.get("death_types", "")).to_upper()
		var death_folded := death_type.strip_edges().to_upper()
		var excluded: Array = policy.get("excluded_death_types", []) as Array
		if mode == "ALL" and not excluded.any(
			func(value: Variant) -> bool: return String(value).to_upper() == death_folded
		):
			return true
		if mode == "NONE":
			for included_value in policy.get("included_death_types", []) as Array:
				if String(included_value).to_upper() == death_folded:
					return true
	return false


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


func _apply_structure_damage(
	attacker_id: int,
	target_id: int,
	amount: int,
	damage_type_override: String = "",
	damage_components_override: Variant = null
) -> void:
	_combat_subsystem()._apply_structure_damage(attacker_id, target_id, amount, damage_type_override, damage_components_override)


func _target_alive(target_id: int, target_kind: String) -> bool:
	if target_kind == "structure":
		return structures.has(target_id) and int((structures[target_id] as Dictionary).get("health", 0)) > 0
	return entities.has(target_id) and not entity_container.has(target_id) and int((entities[target_id] as Dictionary).get("health", 0)) > 0


func _target_position(target_id: int, target_kind: String) -> Vector2:
	var row: Dictionary = structures.get(target_id, {}) if target_kind == "structure" else entities.get(target_id, {})
	return Vector2(row.get("position", Vector2.ZERO))


func _seed_scenario_map_placements() -> void:
	## Resolve every authored map type against the selected registries. Unknown
	## decoration remains visual-only; exact-one-domain admission is enforced by
	## scenario_spawn_contract(). Source-index order fixes identity allocation.
	if _scenario_map_placements.is_empty() or not _scenario_runtime_tables_present():
		return
	var placements := _scenario_map_placements.duplicate(true)
	placements.sort_custom(
		func(a, b):
			var ai := int((a as Dictionary).get("source_index", -1))
			var bi := int((b as Dictionary).get("source_index", -1))
			if ai != bi:
				return ai < bi
			return String((a as Dictionary).get("type_name", "")) < String((b as Dictionary).get("type_name", ""))
	)
	# Capturable rows carry richer gameplay than the generic scenario structure
	# contract (neutral ownership, capturable/link fields, paired transfer). When
	# that lane is enabled it owns its authored source indices; generic registry
	# admission must not steal them merely because CaptureFlag/Outpost now also
	# have descriptor-backed scenario documents.
	var capturable_source_indices: Dictionary = {}
	if capturable_neutrals_enabled:
		for capturable_value in _capturable_placements:
			if typeof(capturable_value) != TYPE_DICTIONARY:
				continue
			var capturable := capturable_value as Dictionary
			var capturable_index := int(capturable.get("source_index", -1))
			if capturable_index >= 0:
				capturable_source_indices[capturable_index] = true
	var seen_source_indices: Dictionary = {}
	for placement_value in placements:
		var placement := placement_value as Dictionary
		var source_index := int(placement.get("source_index", -1))
		if source_index < 0 or seen_source_indices.has(source_index):
			continue
		seen_source_indices[source_index] = true
		var object_id := String(placement.get("type_name", ""))
		var contract := scenario_spawn_contract(object_id, "map-placement")
		var kind := String(contract.get("kind", ""))
		if kind == "":
			continue
		# Unit/prop admission still outranks a malformed cross-domain capturable
		# row at the same source index. Only the generic STRUCTURE path yields to
		# the richer capturable structure contract.
		if kind == "structure" and capturable_source_indices.has(source_index):
			continue
		var properties := placement.get("properties", {}) as Dictionary
		var team := _castle_fixture_team(String(properties.get("originalOwner", "")))
		var at := Vector2(placement.get("position", Vector2.ZERO))
		var spawned_id := -1
		match kind:
			"unit":
				spawned_id = spawn_scenario_unit(object_id, team, at, "map-placement", _next_scenario_unit_id)
				if spawned_id > 0:
					_next_scenario_unit_id += 1
			"structure":
				spawned_id = spawn_scenario_structure(object_id, team, at, "map-placement", _next_scenario_structure_id)
				if spawned_id > 0:
					_next_scenario_structure_id += 1
			"prop":
				spawned_id = spawn_scenario_prop(object_id, at, "map-placement")
		if spawned_id <= 0:
			_emit_event("scenario.map_placement_refused", 0, 0, {
				"object_id": object_id, "kind": kind, "source_index": source_index,
			})
			continue
		var row: Dictionary = {}
		match kind:
			"unit": row = entities[spawned_id] as Dictionary
			"structure": row = structures[spawned_id] as Dictionary
			"prop": row = scenario_props[spawned_id] as Dictionary
		row["yaw"] = float(placement.get("yaw", 0.0))
		row["scenario_source_index"] = source_index
		row["scenario_source_position"] = Vector3(placement.get("source_position", Vector3.ZERO))
		row["scenario_source_properties"] = properties.duplicate(true)
		_scenario_map_seeded_source_indices[source_index] = true
		_emit_event("scenario.map_placement_seeded", spawned_id if kind == "unit" else 0, spawned_id if kind == "structure" else 0, {
			"object_id": String((contract.get("document", {}) as Dictionary).get("objectId", object_id)),
			"kind": kind, "source_index": source_index, "team": team,
			"yaw": float(placement.get("yaw", 0.0)),
		})
	# SpawnBehavior children consume this normal allocator; continue immediately
	# after map-assigned CREEP unit IDs without collision.
	_next_dynamic_id[CREEP_TEAM] = _next_scenario_unit_id

func _castle_fixture_team(owner: String) -> int:
	## Map a fixture's authored originalOwner ("Player_N/teamPlayer_N",
	## "PlyrCivilian/teamPlyrCivilian", …) onto a sim team. A player owner maps
	## to the roster team seated at that start index (team_start_indices:
	## team -> zero-based start index); PlyrCreeps maps to the creep team;
	## everything else — civilian/neutral owners, retail's malformed "/team",
	## and players with no roster seat on this match — maps to the
	## non-combatant civilian team, never silently to team 0.
	if owner.begins_with("PlyrCreeps"):
		return CREEP_TEAM
	if owner.begins_with("Player_"):
		var digits := owner.trim_prefix("Player_").split("/", true, 1)[0]
		if digits.is_valid_int():
			var seat := int(digits) - 1
			for team_value in _team_descriptors.keys():
				var descriptor: Dictionary = _team_descriptors[team_value]
				if int(descriptor.get("start_index", -1)) == seat:
					return int(team_value)
			# Roster rows without a start_index (any lobby launch where no one
			# picked a start position): the team still spawns at the map's
			# configured seat (_ai_start_waypoint_name falls back to
			# _configured_team_start_indices), so the castle's authored
			# Player_N owner must resolve through the same table instead of
			# dropping to the civilian team (review 2026-08-19: the player's
			# own open gate blocked him on every injected-roster launch).
			var seat_teams: Array = _configured_team_start_indices.keys()
			seat_teams.sort()
			for team_value in seat_teams:
				var descriptor: Dictionary = _team_descriptors.get(int(team_value), {}) as Dictionary
				if descriptor.has("start_index"):
					continue
				if int(_configured_team_start_indices[team_value]) == seat:
					return int(team_value)
	return CASTLE_CIVILIAN_TEAM


func _seed_capturable_neutrals() -> void:
	## Map-authored Inn / Outpost / SignalFire / CaptureFlag become live
	## NEUTRAL structures. The flag is CAPTURABLE; LINKED_TO_FLAG buildings
	## follow the nearest flag. Visuals stay on the bound map props.
	if _capturable_placements.is_empty():
		return
	## Inn has no selected neutral descriptor yet. Keep that one gap explicit;
	## descriptor-backed CaptureFlag / Outpost / SignalFire must never fall back.
	if not _structure_armor.has("inn"):
		_structure_armor["inn"] = {"set_id": "NeutralInn-provisional", "damage_scalar": 1.0, "scalars": {"default": 1.0}}
	var placements := _capturable_placements.duplicate(true)
	placements.sort_custom(
		func(a, b): return int((a as Dictionary).get("source_index", 0)) < int((b as Dictionary).get("source_index", 0))
	)
	var seeded: Array[int] = []
	for placement_value in placements:
		var placement: Dictionary = placement_value
		if _scenario_map_seeded_source_indices.has(int(placement.get("source_index", -1))):
			continue
		var kind := StructureArmorContract.scenario_runtime_kind(String(placement.get("type_name", placement.get("structure_kind", ""))))
		var authored_kind := StructureArmorContract.scenario_runtime_kind(String(placement.get("structure_kind", "")))
		if kind == "" or authored_kind != kind:
			configuration_error = "Capturable placement '%s' has inconsistent runtime kind '%s'" % [String(placement.get("type_name", "")), String(placement.get("structure_kind", ""))]
			return
		if not _structure_armor.has(kind):
			configuration_error = "Capturable placement '%s' has no compiled armor contract" % String(placement.get("type_name", ""))
			return
		var structure_id := _next_capturable_structure_id
		_next_capturable_structure_id += 1
		var max_health := maxi(1, int(placement.get("maximum_health", 1)))
		_note_structure_table_mutation()
		var row := {
			"id": structure_id,
			"team": NEUTRAL_TEAM,
			"structure_kind": kind,
			"name": String(placement.get("type_name", kind)),
			"type_name": String(placement.get("type_name", "")),
			"source_index": int(placement.get("source_index", -1)),
			"position": Vector2(placement.get("position", Vector2.ZERO)),
			"yaw": float(placement.get("yaw", 0.0)),
			"rally": Vector2(placement.get("position", Vector2.ZERO)),
			"health": max_health,
			"maximum_health": max_health,
			"construction_progress": 1.0,
			"level": 1,
			"completed_upgrades": [],
			"damage_remainders": {},
			"queue": [],
			"upgrade_queue": [],
			"capturable": bool(placement.get("capturable", false)),
			"linked_to_flag": bool(placement.get("linked_to_flag", false)),
			"unattackable": bool(placement.get("unattackable", false)),
			"presentation": "bound-map-prop",
			"linked_structure_id": 0,
		}
		if kind == "outpost":
			row["auto_deposit_amount"] = OUTPOST_DEPOSIT_AMOUNT
			row["auto_deposit_interval_ms"] = OUTPOST_DEPOSIT_MS
			row["auto_deposit_capture_bonus"] = OUTPOST_CAPTURE_BONUS
			_initialize_structure_auto_deposit(row)
		structures[structure_id] = row
		seeded.append(structure_id)
		_emit_event("structure.neutral_seeded", 0, structure_id, {
			"type_name": String(placement.get("type_name", "")),
			"kind": kind,
			"source_index": int(placement.get("source_index", -1)),
		})
	_link_capture_flags(seeded)


func _link_capture_flags(seeded_ids: Array[int]) -> void:
	## Pair each CAPTURABLE flag with the nearest LINKED_TO_FLAG building.
	## The map does not encode the link; retail places the pair together.
	var flags: Array[int] = []
	var buildings: Array[int] = []
	for structure_id in seeded_ids:
		var row: Dictionary = structures.get(structure_id, {})
		if row.is_empty():
			continue
		if bool(row.get("capturable", false)):
			flags.append(structure_id)
		elif bool(row.get("linked_to_flag", false)):
			buildings.append(structure_id)
	for flag_id in flags:
		var flag: Dictionary = structures[flag_id]
		var origin := Vector2(flag.get("position", Vector2.ZERO))
		var best_id := 0
		var best_distance := INF
		for building_id in buildings:
			var building: Dictionary = structures[building_id]
			if int(building.get("linked_structure_id", 0)) != 0:
				continue
			var distance := origin.distance_to(Vector2(building.get("position", Vector2.ZERO)))
			if distance < best_distance:
				best_distance = distance
				best_id = building_id
		if best_id == 0:
			continue
		flag["linked_structure_id"] = best_id
		(structures[best_id] as Dictionary)["linked_structure_id"] = flag_id


func _transfer_linked_capture(flag_row: Dictionary, team: int) -> void:
	var linked_id := int(flag_row.get("linked_structure_id", 0))
	if linked_id == 0 or not structures.has(linked_id):
		return
	var linked: Dictionary = structures[linked_id]
	linked["team"] = team
	_award_auto_deposit_capture(linked, team)
	_emit_event("structure.linked_captured", 0, linked_id, {
		"team": team,
		"flag_id": int(flag_row.get("id", 0)),
		"structure_kind": String(linked.get("structure_kind", "")),
	})


func _seed_castle_fixtures() -> void:
	## Lane L2b: map-authored castle structures become live sim structures,
	## following the creep-lair seeding precedent (deterministic source_index
	## order, authored health/owner, recorded provisional armor). Only the
	## sim-seeded subset arrives here — creep lairs, INERT scenery and
	## capturable flags were deferred upstream with named reasons.
	if _castle_fixture_placements.is_empty():
		return
	if not _structure_armor.has("castle_fixture"):
		# No castle map-fixture armor table is compiled into the packs yet, so
		# register a recorded neutral 1.0 provisional (the creep-lair
		# precedent) instead of falling to the unrelated 0.25 default. The
		# authored set name rides each row ("castle_fixture_armor") for the
		# lane that compiles those tables.
		_structure_armor["castle_fixture"] = {"set_id": "MapFixture-provisional", "damage_scalar": 1.0, "scalars": {"default": 1.0}}
	var placements := _castle_fixture_placements.duplicate(true)
	placements.sort_custom(
		func(a, b): return int((a as Dictionary).get("source_index", 0)) < int((b as Dictionary).get("source_index", 0))
	)
	for placement_value in placements:
		var placement: Dictionary = placement_value
		if _scenario_map_seeded_source_indices.has(int(placement.get("source_index", -1))):
			continue
		var structure_id := _next_castle_fixture_id
		_next_castle_fixture_id += 1
		var position := Vector2(placement.get("position", Vector2.ZERO))
		var maximum_health := maxi(1, roundi(float(placement.get("maximum_health", 1.0))))
		var health := maximum_health
		if placement.has("initial_health"):
			# objectInitialHealth is an authored PERCENT of MaxHealth: Carn Dum
			# authors 99/75/50 on seven rows, everything measured else is 100.
			health = clampi(
				maxi(1, roundi(float(maximum_health) * float(placement.get("initial_health", 100.0)) / 100.0)),
				1,
				maximum_health
			)
		_note_structure_table_mutation()
		var row := {
			"id": structure_id,
			"team": _castle_fixture_team(String(placement.get("owner", ""))),
			"kind": "structure",
			"structure_kind": "castle_fixture",
			"name": String(placement.get("type_name", "")),
			"castle_fixture_type": String(placement.get("type_name", "")),
			"castle_fixture_role": String(placement.get("role", "")),
			"kind_of": (placement.get("kind_of", []) as Array).duplicate(),
			"castle_fixture_owner": String(placement.get("owner", "")),
			"castle_fixture_armor": String(placement.get("armor", "")),
			"castle_fixture_kind_of": Array(placement.get("kind_of", [])).duplicate(),
			"castle_fixture_enabled": bool(placement.get("enabled", true)),
			"castle_fixture_targetable": bool(placement.get("targetable", true)),
			"source_index": int(placement.get("source_index", -1)),
			"position": position,
			"elevation": float(placement.get("elevation", 0.0)),
			"facing_radians": float(placement.get("yaw", 0.0)),
			"rally": position,
			"health": health,
			"maximum_health": maximum_health,
			# SAGE objectIndestructible, carried verbatim (Erebor authors it on
			# 501 of 609 fixture rows); _apply_structure_damage refuses it.
			"indestructible": bool(placement.get("indestructible", false)),
			"construction_progress": 1.0,
			"level": 1,
			"completed_upgrades": [],
			"upgrade_queue": [],
			"production": [],
			"queue": [],
			"damage_remainders": {},
			"income_per_payout": 0,
			# The battlefield already draws the map's bound prop for this
			# placement; the exact playable-structure document is optional for
			# presentation but mandatory for wall-defense behavior.
			"presentation": "bound-map-prop",
		}
		var type_name := String(placement.get("type_name", ""))
		var document := _playable_structure_runtime_document(type_name)
		if not document.is_empty():
			row["source_object_id"] = type_name
			row["object_id"] = type_name
			var gameplay := ((document.get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary)
			var attack := _structure_attack_from_combat(gameplay.get("combat", {}) as Dictionary)
			if not attack.is_empty():
				row["attack"] = attack
				row["wall_defense_status"] = "compiled"
			elif String(placement.get("role", "")) == "wall-mounted":
				if _document_is_wall_upgrade_slot(document):
					# Retail authors these WITHOUT a weapon: the slot gains one
					# through the player's Trebuchet/Postern/Garrison purchase
					# (GondorCastleWallCommandSet, WeaponSet PLAYER_UPGRADE) or,
					# for catapult mounts, a slaved trebuchet spawned on
					# creation. Neither purchase flow exists in this runtime
					# yet, so the slot is inert BY NAME - not a stale pack.
					row["wall_defense_status"] = "upgrade-slot-purchase-unimplemented"
					if not _named_wall_slot_types.has(type_name):
						_named_wall_slot_types[type_name] = true
						print("[RetailSliceSim] CASTLE_WALL_UPGRADE_SLOT type=%s reason=map-placed slot; Trebuchet/Postern/Garrison purchase on fixtures not implemented (named gap)" % type_name)
				else:
					# Document present but no compiled combat: the exact shape a
					# half-recooked pack takes. Loud, never inert-and-silent.
					row["wall_defense_status"] = "stale-pack-document-without-combat"
					print("[RetailSliceSim] CASTLE_WALL_DEFENSE_STALE type=%s source_index=%d reason=document-without-compiled-combat" % [type_name, int(placement.get("source_index", -1))])
			_attach_structure_module_contracts(row)
		elif String(placement.get("role", "")) == "wall-mounted":
			row["wall_defense_status"] = "stale-pack-missing-structure-document"
			print("[RetailSliceSim] CASTLE_WALL_DEFENSE_STALE type=%s source_index=%d reason=missing-playable-structure-document" % [type_name, int(placement.get("source_index", -1))])
		structures[structure_id] = row
		var garrison := placement.get("garrison", {}) as Dictionary
		if not garrison.is_empty():
			_attach_castle_fixture_garrison(structures[structure_id] as Dictionary, garrison)
		var stale_status := String(row.get("wall_defense_status", ""))
		if stale_status.begins_with("stale-pack-"):
			_emit_event("castle.fixture_wall_defense_stale", 0, structure_id, {
				"type_name": type_name,
				"source_index": int(placement.get("source_index", -1)),
				"reason": "missing-playable-structure-document" if stale_status == "stale-pack-missing-structure-document" else "document-without-compiled-combat",
			})
		var fixture_row: Dictionary = structures[structure_id]
		var gate_block_value: Variant = placement.get("gate", null)
		if String(placement.get("role", "")) == "gate" and typeof(gate_block_value) == TYPE_DICTIONARY:
			var gate_block := gate_block_value as Dictionary
			var opened := bool(gate_block.get("openByDefault", false))
			fixture_row["gate_behavior"] = {
				"open": opened,
				"pathing_open": opened,
				"open_fraction": 1.0 if opened else 0.0,
				"reset_ticks": _ship_contract_delay_ticks(float(gate_block.get("resetMilliseconds", 0.0))),
				"pathing_threshold": float(gate_block.get("percentOpenForPathing", 100.0)) / 100.0,
				"repel_colliding": false,
				"close_tick": -1,
				"unsupported_semantics": [],
			}
			fixture_row["gate_geometries"] = (gate_block.get("geometries", {}) as Dictionary).duplicate(true)
			if gate_block.has("commandSet"):
				fixture_row["gate_command_set"] = String(gate_block.get("commandSet", ""))
			if gate_block.has("commandSetRows"):
				fixture_row["gate_command_rows"] = (gate_block.get("commandSetRows", []) as Array).duplicate(true)
			var ai_gate_value: Variant = gate_block.get("aiGateUpdate", null)
			if typeof(ai_gate_value) == TYPE_DICTIONARY:
				var ai_gate := ai_gate_value as Dictionary
				fixture_row["ai_gate_update"] = {"trigger_width_source": Vector2(float(ai_gate.get("triggerWidthX", 0.0)), float(ai_gate.get("triggerWidthY", 0.0)))}
			var portal_value: Variant = gate_block.get("fakePathfindPortal", null)
			if typeof(portal_value) == TYPE_DICTIONARY:
				var portal := portal_value as Dictionary
				fixture_row["fake_pathfind_portal"] = {"allow_enemies": bool(portal.get("allowEnemies", false)), "allow_non_skirmish_ai": bool(portal.get("allowNonSkirmishAIUnits", false))}
			structures[structure_id] = fixture_row
			_sync_gate_passage(structure_id)
		_emit_event("castle.fixture_seeded", 0, structure_id, {
			"type_name": String(placement.get("type_name", "")),
			"role": String(placement.get("role", "")),
			"team": int(structures[structure_id].get("team", -1)),
			"source_index": int(placement.get("source_index", -1)),
		})


func _attach_castle_fixture_garrison(row: Dictionary, garrison: Dictionary) -> void:
	var capacity := int(garrison.get("containMax", 0))
	if capacity <= 0:
		return
	var statuses: Array = (garrison.get("objectStatusOfContained", []) as Array).duplicate()
	var filter: Array = (garrison.get("passengerFilter", []) as Array).duplicate()
	# Compatibility for the selected v0.2.6 maps pack: its fixture schema
	# predates L4 and carries capacity/ownership/death but not these two fields.
	# Exact tower identity keeps this authored stopgap narrow and loud; the next
	# maps recook emits the same values from INI and retires this branch.
	if statuses.is_empty() or filter.is_empty():
		var tower_type := String(row.get("castle_fixture_type", ""))
		if tower_type in ["EBGarrisonableTower", "GHGarrisonableTower"]:
			statuses = ["UNSELECTABLE", "CAN_ATTACK", "ENCLOSED"]
			filter = ["ANY", "+INFANTRY", "+BANNER", "-CAVALRY", "-SUMMONED", "-WildSpiderling", "-WildSpiderlingHorde", "-COMBO_HORDE", "-IsengardSharku", "-AngmarThrallMaster"]
			row["garrison_contract_gap"] = "selected-map-pack-predates-L4-fields"
			push_warning("Castle garrison %s uses the explicit pre-L4 fixture compatibility contract; maps recook owed" % tower_type)
	var exit_values: Array = garrison.get("exitOffset", []) as Array
	var exit_offset := Vector2.ZERO
	if exit_values.size() == 3:
		exit_offset = Vector2(float(exit_values[0]), float(exit_values[1]))
	elif String(row.get("castle_fixture_type", "")) == "EBGarrisonableTower":
		# ereborbuildings.ini:5279 HordeGarrisonContain ExitOffset X:50 (pre-L4 pack)
		exit_offset = Vector2(50.0, 0.0)
		row["garrison_contract_gap"] = "selected-map-pack-predates-L4-fields"
		push_warning("Castle garrison EBGarrisonableTower exitOffset/visionRange from the explicit pre-L4 compatibility contract; maps recook owed")
	elif String(row.get("castle_fixture_type", "")) == "GHGarrisonableTower":
		# greyhavenbuildings.ini:2171 ExitOffset X:0 Y:-45 (pre-L4 pack)
		exit_offset = Vector2(0.0, -45.0)
		row["garrison_contract_gap"] = "selected-map-pack-predates-L4-fields"
		push_warning("Castle garrison GHGarrisonableTower exitOffset/visionRange from the explicit pre-L4 compatibility contract; maps recook owed")
	elif not garrison.has("exitOffset"):
		push_warning("Castle garrison %s has no authored exitOffset in the fixture row and no compatibility contract; occupants exit at the tower centre" % String(row.get("castle_fixture_type", "")))
	var kill_on_death := bool(garrison.get("killPassengersOnDeath", false))
	row["transport_capacity"] = capacity
	row["horde_transport"] = {
		"module": "HordeGarrisonContain",
		"contained_statuses": statuses,
		"passenger_filter": filter,
		"damage_ratio": float(garrison.get("damagePercentToUnits", 0.0)),
		"allow_own": bool(garrison.get("allowOwnPlayerInsideOverride", false)),
		"allow_allies": bool(garrison.get("allowAlliesInside", false)),
		"allow_enemies": bool(garrison.get("allowEnemiesInside", false)),
		"allow_neutral": bool(garrison.get("allowNeutralInside", false)),
		"exit_delay_ticks": 0,
		"kill_passengers_on_death": kill_on_death,
		"eject_passengers_on_death": not kill_on_death,
		"exit_offset_source": exit_offset,
		"tower_vision_range_source": float(garrison.get("visionRange", 600.0 if String(row.get("castle_fixture_type", "")) == "EBGarrisonableTower" else 160.0 if String(row.get("castle_fixture_type", "")) == "GHGarrisonableTower" else 0.0)),
		"unsupported_semantics": [],
		"source": "map-fixtures.garrison",
	}

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
## (workspace/retail-extract/data/ini/default/aidata.ini:145-202) places eight
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


func _evaluate_cah_match_awards() -> void:
	var unit_types: Array = _cah_award_contracts.keys()
	unit_types.sort_custom(func(a, b): return String(a) < String(b))
	for unit_type_value in unit_types:
		var unit_type := String(unit_type_value)
		if cah_award_results.has(unit_type):
			continue
		var contract := _cah_award_contracts[unit_type] as Dictionary
		var tally := _cah_tally_for(unit_type)
		var owner_team := int(contract.get("ownerTeam", _created_hero_owner_teams.get(unit_type, -1)))
		var mode_suffix := "OPENPLAY_MP" if _cah_openplay_multiplayer() else "SKIRMISH"
		var result_stat := "HERO_%s_COUNT_%s" % ["VICTORY" if owner_team == winner else "DEFEAT", mode_suffix]
		tally[result_stat] = int(tally.get(result_stat, 0)) + 1
		var eligible: Dictionary = {}
		for award_value in contract.get("eligibleAwards", []) as Array:
			eligible[String(award_value)] = true
		var awards: Array = (contract.get("ownedAwards", []) as Array).duplicate()
		var new_awards: Array = []
		for definition_value in contract.get("awardDefinitions", []) as Array:
			var definition := definition_value as Dictionary
			var award_id := String(definition.get("awardId", ""))
			if award_id == "" or not eligible.has(award_id) or awards.has(award_id):
				continue
			var earned := true
			for trigger_value in definition.get("triggers", []) as Array:
				var trigger := trigger_value as Dictionary
				var total := 0
				for stat_value in trigger.get("statIds", []) as Array:
					total += int(tally.get(String(stat_value), 0))
				if total < int(trigger.get("threshold", 0)):
					earned = false
					break
			if earned:
				awards.append(award_id)
				new_awards.append(award_id)
		var result := {
			"heroId": String(contract.get("heroId", unit_type.trim_prefix("CreateAHero__"))),
			"objectId": unit_type,
			"ownerTeam": owner_team,
			"trackingStats": tally.duplicate(true),
			"awards": awards,
			"newAwards": new_awards,
		}
		cah_award_results[unit_type] = result
		_emit_event("cah.awards_resolved", 0, 0, result)


func _resolve_victory() -> void:
	## Last-alliance-standing over the whole roster. The match ends when no two
	## surviving teams are mutually hostile (a single alliance, or one team, or
	## none remain). `winner` stays a single int for snapshot compat: the LOWEST
	## surviving team id (documented tie-break); with no survivors it is the
	## lowest rostered team so a mutual wipe still terminates deterministically.
	## For the historical {0,1} roster this reduces to the old team0-vs-team1
	## comparison and emits the identical observer-frame events, so the pinned
	## battle signature does not move.
	# A delayed FireWeaponWhenDeadBehavior is part of the lethal callback that
	# produced it. Do not freeze the match before that authored consequence has
	# fired; otherwise killing the last carrier would strand the schedule forever
	# behind tick()'s winner gate.
	for effect in _pending_power_effects:
		if String(effect.get("kind", "")) == "death_weapon":
			return
	# A thrown/knocked-back body must finish its deterministic impact/recovery
	# consequence before the winner gate freezes gameplay. Recovered bodies are
	# inert evidence and do not hold the match open.
	for physics_value in physics_objects.values():
		if String((physics_value as Dictionary).get("phase", "")) != "recovered":
			return
	for entity_value in entities.values():
		var lifetime := (entity_value as Dictionary).get("lifetime_update", {}) as Dictionary
		if int(lifetime.get("expire_tick", -1)) > tick_index:
			return
	for structure_value in structures.values():
		var structure := structure_value as Dictionary
		var lifetime := structure.get("lifetime_update", {}) as Dictionary
		if int(lifetime.get("expire_tick", -1)) > tick_index:
			return
		if String(structure.get("ship_death_phase", "")) == "sinking":
			return
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
	_evaluate_cah_match_awards()
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


## PRESENTATION-ONLY: the LOCAL machine's seat for selection/control-group
## gating. Never part of the authoritative state or the lockstep hash — each
## peer's selection is local. Defaults to PLAYER_TEAM (solo and the host seat);
## the slice sets the lockstep guest's real seat (team 1) so the guest selects
## and controls its OWN army instead of a hardcoded team 0.
var local_seat_team: int = PLAYER_TEAM


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

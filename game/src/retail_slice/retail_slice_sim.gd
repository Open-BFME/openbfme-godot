class_name RetailSliceSim
extends RefCounted

const PlayableUnitAdapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const ParitySystems = preload("res://src/retail_slice/retail_slice_parity.gd")
const CommandScript = preload("res://src/retail_slice/retail_command.gd")
const SelectionPick = preload("res://src/retail_slice/retail_selection_pick.gd")

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
## Dedicated creep owner (retail PlyrCreeps): hostile to every rostered team,
## never rostered itself — never a victory participant and excluded from
## spellbook/economy/AI production. Distinct from NEUTRAL_TEAM, which only
## owns capturable structures. High sentinel so no N-team roster id collides.
const CREEP_TEAM := 9999
const CREEP_VISION_SOURCE := 200.0  # gamedata.ini line 61 CREEP_VISION
const CREEP_LAIR_MAX_HEALTH := 2000  # StructureBody MaxHealth, all six lairs
const CREEP_LAIR_DAMAGED_HEALTH := 1000  # authored damage tiers 1000/500
const CREEP_LAIR_REALLY_DAMAGED_HEALTH := 500
const CREEP_HOLE_MAX_HEALTH := 500  # RebuildHoleExposeDie HoleMaxHealth
const CREEP_HOLE_REBUILD_TICKS := 1200  # RebuildHoleBehavior WorkerRespawnDelay 120000 ms
const CREEP_TREASURE_MIN_RESOURCE := 160  # crate.ini TreasureChest1 MinResource
const CREEP_TREASURE_MAX_RESOURCE := 200  # crate.ini TreasureChest1 MaxResource
const CREEP_GUARD_WANDER_INTERVAL_TICKS := 40
const CREEP_GUARD_EXIT_RADIUS := 2.0
## Per-family lair contract (BFME2 1.06 INI corpus, measured in
## .private/retail-work/reports/creep-contract/creep_contract.json):
## SpawnBehavior burst/replace cadence and the hole-death treasure OCL.
const CREEP_LAIR_FAMILIES := {
	"CaveTrollLair": {"spawn_number": 1, "replace_delay_ms": 120000.0, "treasure_chests": 4, "levelup_chest": false, "guards": ["bfme2.object.creep-cave-troll"]},
	"WargLair": {"spawn_number": 2, "replace_delay_ms": 45000.0, "treasure_chests": 3, "levelup_chest": false, "guards": ["bfme2.object.creep-warg"]},
	"MoriarGoblinLair": {"spawn_number": 8, "replace_delay_ms": 60000.0, "treasure_chests": 2, "levelup_chest": false, "guards": ["bfme2.object.creep-goblin-swordsman", "bfme2.object.creep-goblin-archer"]},
	"SpiderLair": {"spawn_number": 7, "replace_delay_ms": 45000.0, "treasure_chests": 4, "levelup_chest": false, "guards": ["bfme2.object.creep-minor-spider"]},
	"BarrowWightLair": {"spawn_number": 1, "replace_delay_ms": 300000.0, "treasure_chests": 1, "levelup_chest": true, "guards": ["bfme2.object.creep-barrow-wight"]},
	"FireDrakeLair": {"spawn_number": 1, "replace_delay_ms": 120000.0, "treasure_chests": 1, "levelup_chest": true, "guards": ["bfme2.object.creep-fire-drake"]},
}
const CREEP_LAIR_FAMILY_ALIASES := {
	"CaveTrollLairSnow": "CaveTrollLair",
	"MoriarGoblinLairSnow": "MoriarGoblinLair",
}
## Guard chassis: health / GuardMaxRange / GuardWanderRange / CREEP_VISION are
## measured (creep_contract.json guards table); weapon damage, cadence, and
## locomotor speeds are recorded provisionals pending INI weapon extraction.
const CREEP_GUARD_STATS := {
	"bfme2.object.creep-cave-troll": {"name": "Cave Troll", "health": 3000, "damage": 120, "damage_type": "crush", "speed": 50.0, "attack_range": 20.0, "guard_max_range": 250.0, "guard_wander_range": 80.0, "delay_ms": 2500.0, "art_status": "converted-wild-goblincavetroll"},
	"bfme2.object.creep-warg": {"name": "Neutral Warg", "health": 800, "damage": 45, "damage_type": "slash", "speed": 90.0, "attack_range": 20.0, "guard_max_range": 250.0, "guard_wander_range": 80.0, "delay_ms": 1500.0, "art_status": "provisional-riderless-iuwarg-unvalidated"},
	"bfme2.object.creep-goblin-swordsman": {"name": "Goblin Swordsman", "health": 30, "damage": 15, "damage_type": "slash", "speed": 60.0, "attack_range": 20.0, "guard_max_range": 250.0, "guard_wander_range": 40.0, "delay_ms": 1500.0, "art_status": "converted-wild-goblinfighterhorde-member"},
	"bfme2.object.creep-goblin-archer": {"name": "Goblin Archer", "health": 30, "damage": 12, "damage_type": "pierce", "speed": 60.0, "attack_range": 160.0, "guard_max_range": 250.0, "guard_wander_range": 40.0, "delay_ms": 2000.0, "art_status": "converted-wild-goblinarcherhorde-member"},
	"bfme2.object.creep-minor-spider": {"name": "Minor Spider", "health": 500, "damage": 40, "damage_type": "slash", "speed": 70.0, "attack_range": 20.0, "guard_max_range": 350.0, "guard_wander_range": 75.0, "delay_ms": 1500.0, "art_status": "converted-wild-wildspiderlinghorde-member"},
	"bfme2.object.creep-barrow-wight": {"name": "Barrow Wight", "health": 250, "damage": 60, "damage_type": "magic", "speed": 40.0, "attack_range": 20.0, "guard_max_range": 250.0, "guard_wander_range": 75.0, "delay_ms": 2500.0, "art_status": "provisional-cuwight-unconverted"},
	"bfme2.object.creep-fire-drake": {"name": "Fire Drake", "health": 4000, "damage": 150, "damage_type": "flame", "speed": 80.0, "attack_range": 100.0, "guard_max_range": 420.0, "guard_wander_range": 80.0, "delay_ms": 3000.0, "art_status": "provisional-wufiredrk-unconverted"},
}
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
		_spatial_sync(entities[key] as Dictionary)


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
					if check_stealth and tick_index < int(candidate_dict.get("stealth_until_tick", -1)):
						continue
					var distance := origin.distance_to(Vector2(candidate_dict.get("position", Vector2.ZERO)))
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
var _spawn_positions: Dictionary = {}
## Team -> spawn-anchor Vector2 for rostered teams beyond 0/1 (N-team spawn
## geometry). Populated from the map layer's team_start_centers; teams 0/1 keep
## deriving their anchors from spawn ids 1/2 and 101/102, so this is empty and
## inert for every 2-team match.
var _extra_team_centers: Dictionary = {}
var _home_layout: Dictionary = {}
var _map_script_waypoints: Dictionary = {}
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
## Neutral creep lairs (opt-in gameplay rule "enable_creep_lairs"; default off
## keeps every legacy runner byte-identical). Placements arrive with the map
## configuration and stay inert until the rule enables seeding.
var creep_lairs_enabled := false
## Retail horde movement semantics (opt-in gameplay rule
## "retail_formation_movement"; default off keeps every legacy runner and the
## owner-signed 3000-tick behaviour pin byte-identical).
##
## When enabled the sim honours three things the retail data authors and this
## sim previously ignored outright:
##
##   1. Turn rate. locomotor.ini authors TurnTime per horde locomotor
##      (NormalMeleeHordeLocomotor TurnTime=2000 -> 180 deg/s;
##      NormalCavalryHordeLocomotor TurnTime=1000 -> 360 deg/s). The importer
##      already lands it on the row as turn_rate_degrees_per_second; without
##      this flag the sim throws it away and snaps facing in one tick.
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
## See .private/scratch/opus17-formation-extraction.md for the full citation set.
var retail_formation_movement := false
var _creep_lair_placements: Array = []
var _next_creep_guard_id := 70001
var _next_creep_structure_id := 60001
var ring_mechanic_enabled := false
var _ring_contract: Dictionary = {}
var _ring_delivery_kinds: Dictionary = {}
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
	for waypoint_name in _map_script_waypoints.keys():
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
	# Base-flag CONFIG (position/cost/health) survives setup like the expansion
	# rules. The dynamic unpack state is reset EXPLICITLY here (rows back to
	# packed) and above (expansion pads, the expansion id counter and script
	# unit references cleared with the structures they pointed at) - none of
	# it resets by itself.
	for base_name in unpackable_base_names():
		var flag_row: Dictionary = unpackable_bases[base_name]
		flag_row["unpacked_by"] = -1
		flag_row["structure_id"] = 0
	_next_creep_guard_id = 70001
	_next_creep_structure_id = 60001
	if creep_lairs_enabled:
		_seed_creep_lairs()
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
	_map_script_waypoints.clear()
	var configured_waypoints: Variant = configuration.get("script_waypoints", {})
	if typeof(configured_waypoints) == TYPE_DICTIONARY:
		for waypoint_name in (configured_waypoints as Dictionary).keys():
			var waypoint_position: Variant = (configured_waypoints as Dictionary)[waypoint_name]
			if typeof(waypoint_position) == TYPE_VECTOR2:
				_map_script_waypoints[String(waypoint_name)] = waypoint_position
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
	# Optional authored creep-lair placements (PlyrCreeps camps). Inert until
	# the opt-in creep rule enables seeding; malformed rows are dropped here so
	# seeding never has to guess.
	_creep_lair_placements = []
	var configured_lairs: Variant = configuration.get("creep_lair_placements", [])
	if typeof(configured_lairs) == TYPE_ARRAY:
		for lair_value in configured_lairs as Array:
			if typeof(lair_value) != TYPE_DICTIONARY:
				continue
			var lair := lair_value as Dictionary
			if (
				typeof(lair.get("position")) != TYPE_VECTOR2
				or String(lair.get("type_name", "")) == ""
				or typeof(lair.get("source_index")) != TYPE_INT
			):
				continue
			_creep_lair_placements.append({
				"type_name": String(lair.get("type_name", "")),
				"source_index": int(lair.get("source_index", -1)),
				"position": Vector2(lair.get("position")),
				"yaw": float(lair.get("yaw", 0.0)),
				"binding_status": String(lair.get("binding_status", "unresolved")),
			})


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
	_extra_team_centers = {}
	source_map_configured = false
	_creep_lair_placements = []


func _apply_gameplay_rules(gameplay_rules: Dictionary) -> void:
	_rules = gameplay_rules.duplicate(true)
	configuration_error = ""
	ring_mechanic_enabled = bool(_rules.get("allow_ring_heroes", false))
	_configure_ring_mechanic_contract()
	if not _configure_faction_manifest():
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
	creep_lairs_enabled = bool(_rules.get("enable_creep_lairs", false))
	# Retail turn-rate / wheel-vs-reform / group-cohesion opt-in. Absent (every
	# legacy runner and the untouched menu) resolves false, so the pinned
	# behaviour signature stays byte-identical.
	retail_formation_movement = bool(_rules.get("retail_formation_movement", false))
	command_point_cap = maxi(60, int(_rules.get("command_point_cap", 200)))
	var starting_resources := maxi(0, int(_rules.get("starting_resources", 1200 if base_loop_enabled else 0)))
	team_resources = _seed_team_map(starting_resources)
	team_command_points = _seed_team_map(120)
	_seed_team_manifest_tables()


func _configure_ring_mechanic_contract() -> void:
	_ring_contract = (_rules.get("ring_system", {}) as Dictionary).duplicate(true)
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


func _ring_state() -> Dictionary:
	return script_surface_bag.get("ring_mechanic", {}) as Dictionary


func _mark_ring_delivery_structures() -> void:
	for structure_id in structure_ids():
		var row: Dictionary = structures[structure_id]
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


func _is_ring_gollum(row: Dictionary) -> bool:
	var wanted := _ring_gollum_object_id()
	return String(row.get("object_id", "")) == wanted \
		or String(row.get("unit_type", "")) == wanted \
		or String(row.get("source_object_id", "")) == wanted


func _spawn_ring_gollum_fallback() -> void:
	var state := _ring_state()
	if bool(state.get("gollum_spawned", false)):
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
		push_warning("RING_GOLLUM_FALLBACK missing scripted spawn; deterministic sim fallback waypoint=%s roll=%d" % [waypoint, rolled])
	var gollum_id := spawn_script_object(
		_ring_gollum_object_id(), _ring_spawn_team(), at
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
	row["ring_gollum"] = true
	row["ring_wander_percentage"] = int(_ring_contract.get("wanderPercentage", 80))
	row["ring_detection_range"] = float(_ring_contract.get("detectionRange", 120.0))
	row["ring_flee_enemy_range"] = float(_ring_contract.get("fleeEnemyRange", 300.0))
	row["ring_flee_distance"] = float(_ring_contract.get("fleeDistance", 800.0))
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
	var detector := _spatial_nearest_hostile(row, CREEP_TEAM, position, detect_range, 0, true)
	if detector != 0:
		if _stealth_active(row):
			_clear_stealth(row)
	else:
		if not _stealth_active(row):
			_grant_stealth(row, 0x3FFFFFFF, [])
	var flee_range := float(row.get("ring_flee_enemy_range", 300.0))
	var threat := _spatial_nearest_hostile(row, CREEP_TEAM, position, flee_range, 0, true)
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
			var range := float(delivery.get("scanRange", _ring_contract.get("deliveryRange", 10.0)))
			if Vector2(structure.get("position", Vector2.ZERO)).distance_to(Vector2(carrier.get("position", Vector2.ZERO))) <= range:
				var team := int(carrier.get("team", -1))
				var owned: Dictionary = team_upgrades.get(team, {}) as Dictionary
				owned["Upgrade_RingHero"] = true
				team_upgrades[team] = owned
				var completed: Array = structure.get("completed_upgrades", [])
				if not completed.has("Upgrade_FortressRingHero"):
					completed.append("Upgrade_FortressRingHero")
				structure["completed_upgrades"] = completed
				set_entity_object_status(carrier_id, String(_ring_contract.get("status", "HOLDING_THE_RING")), false)
				state["ring_active"] = false
				state["carrier_id"] = 0
				_emit_event("ring.delivered", carrier_id, structure_id, {"team": team, "eva": _ring_eva("delivered")})
				return
	if not bool(state.get("ring_active", false)) or int(state.get("carrier_id", 0)) != 0:
		return
	var ring_position := Vector2(state.get("ring_position", Vector2.ZERO))
	var pickup_range := float(_ring_contract.get("pickupRange", 10.0))
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
	# Repopulated from the loaded documents' compiled combat blocks; no manifest
	# constant mirrors it, so it must not survive a previous configure.
	_unit_damage_components = {}
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


func _compiled_damage_components(combat: Dictionary) -> Array:
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
		rows.append({
			"damage_type": String(row.get("damageType", "")).to_lower(),
			"value": value,
		})
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


func _configure_playable_unit_runtime_contracts() -> void:
	var value: Variant = _rules.get("playable_unit_runtimes", {})
	if typeof(value) != TYPE_DICTIONARY:
		configuration_error = "Playable-unit runtime registry is not a dictionary"
		return
	var configured_unit_rules: Dictionary = _rules.get("unit_rules", {}) as Dictionary
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
		if PlayableUnitAdapter.is_ring_hero_summon(document_value as Dictionary) \
				and not ring_mechanic_enabled:
			continue
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
		var doc_damage_components := _compiled_damage_components(doc_combat)
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
		if String(primary_producer.get("source_field", "")) == "BuildableRingHeroesMP":
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
			if kind == "fortress":
				var fortress_sources: Dictionary = structure_source_object_ids_for_team(team)
				var fortress_aliases: Array = fortress_sources.get("fortress", []) as Array
				if not fortress_aliases.is_empty():
					structures[structure_id]["source_object_id"] = String(fortress_aliases[0])
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
	unit_rule_override: Dictionary = {}
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
	var member_damage := maxi(1, int(unit_rule.get("member_damage", 0)))
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
		"damage_components": (_unit_damage_components.get(object_id, []) as Array).duplicate(true),
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
	if String(unit_rule.get("category", "")) == "hero":
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
	var damage_type := String((_unit_damage_types if not _unit_damage_types.is_empty() else UNIT_DAMAGE_TYPES).get(object_id, ""))
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
	for id in structure_ids(team):
		var row: Dictionary = structures[id]
		if String(row.get("structure_kind", "")) == kind and int(row.get("health", 0)) > 0:
			return id
	return 0


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
	## Returns the upgrade id the gate is still waiting on, or "" when it holds.
	## The ALL-of set must be owned entirely; the ANY-of group (when authored)
	## needs any single member. An absent group is never a gate.
	for required_value in required_upgrades_for_unit(unit_type, producer_kind):
		if not completed_upgrades.has(String(required_value)) \
				and not team_completed_upgrades.has(String(required_value)):
			return String(required_value)
	var any_group := required_upgrade_any_group_for_unit(unit_type, producer_kind)
	if any_group.is_empty():
		return ""
	for candidate_value in any_group:
		if completed_upgrades.has(String(candidate_value)) \
				or team_completed_upgrades.has(String(candidate_value)):
			return ""
	return String(any_group[0])


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
	var upgrade_id := String(grant.get("upgradeId", ""))
	var team := int(building.get("team", -1))
	if upgrade_id == "" or team < 0:
		return
	if String(grant.get("upgradeType", "")) == "PLAYER":
		var owned: Dictionary = team_upgrades.get(team, {}) as Dictionary
		owned[upgrade_id] = true
		team_upgrades[team] = owned
		return
	var completed: Array = building.get("completed_upgrades", [])
	if not completed.has(upgrade_id):
		completed.append(upgrade_id)
		completed.sort()
		building["completed_upgrades"] = completed


func _apply_structure_inherit_upgrades(building: Dictionary) -> void:
	## InheritUpgradeCreate is a one-shot create-module query, not an aura.
	## Search stable structure ids and require the exact authored source Object
	## identity. ObjectFilter = ANY +Type does not author owner, health, or
	## completion restrictions, so this path must not invent them.
	var team := int(building.get("team", -1))
	var kind := String(building.get("structure_kind", ""))
	var rules: Array = structure_inherit_upgrades_for_team(team).get(kind, [])
	if team < 0 or rules.is_empty():
		return
	var carrier_id := int(building.get("id", 0))
	var carrier_position := Vector2(building.get("position", Vector2.ZERO))
	var scale := maxf(
		0.000001, float(_rules.get("source_map_transform_scale", 1.0))
	)
	var completed: Array = building.get("completed_upgrades", [])
	for rule_value in rules:
		var rule := rule_value as Dictionary
		var upgrade_id := String(rule.get("upgradeId", ""))
		var source_id := String(rule.get("sourceObjectId", ""))
		var source_kind := String(rule.get("sourceKind", ""))
		var radius_source := float(
			(rule.get("radius", {}) as Dictionary).get("value", 0.0)
		)
		if upgrade_id == "" or source_id == "" or radius_source <= 0.0:
			continue
		var limit_squared := pow(radius_source * scale, 2.0)
		for donor_id in structure_ids():
			if donor_id == carrier_id:
				continue
			var donor: Dictionary = structures[donor_id]
			if not Array(donor.get("completed_upgrades", [])).has(upgrade_id):
				continue
			var donor_kind := String(donor.get("structure_kind", ""))
			if source_kind != "" and donor_kind != source_kind:
				continue
			# Runtime kinds such as "fortress" are deliberately shared across
			# factions. Resolve the exact retail identity through the donor's
			# own faction manifest, never through the carrier's aliases: a
			# DwarvenFortress must not satisfy +MenFortressCitadel merely
			# because both rows use the generic fortress runtime kind.
			var donor_team := int(donor.get("team", -1))
			var donor_source_ids := structure_source_object_ids_for_team(donor_team)
			var aliases: Array = donor_source_ids.get(donor_kind, [])
			var exact_source_match := false
			for alias_value in aliases:
				if String(alias_value).nocasecmp_to(source_id) == 0:
					exact_source_match = true
					break
			if not exact_source_match:
				continue
			var donor_position := Vector2(donor.get("position", Vector2.ZERO))
			if carrier_position.distance_squared_to(donor_position) > limit_squared:
				continue
			if not completed.has(upgrade_id):
				completed.append(upgrade_id)
				completed.sort()
				building["completed_upgrades"] = completed
			break


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
	var contract: Dictionary = structure_upgrade_contracts_for_team(team).get(upgrade_id, {})
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
	var contracts := structure_upgrade_contracts_for_team(int(building.get("team", -1)))
	var upgrade_ids: Array[String] = []
	for upgrade_id_value in contracts.keys():
		upgrade_ids.append(String(upgrade_id_value))
	upgrade_ids.sort()
	for upgrade_id in upgrade_ids:
		var contract: Dictionary = contracts[upgrade_id]
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
		if bool(contract.get("castle_upgrade", false)):
			# Fortress improvements ride the fortress's upgrades page on EVERY
			# command set it can be on (they never swap it), so no from-set match
			# applies — exactly like compiled research, but bought per building.
			result.append({
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
				"castle_upgrade": true,
				"grants_upgrade_id": String(contract.get("grants_upgrade_id", "")),
				"lacks_prerequisite_label_id": String(contract.get("lacks_prerequisite_label_id", "")),
				"needed_upgrade_ids": Array(contract.get("needed_upgrade_ids", [])).duplicate(),
				"gate_satisfied": _research_gate_unsatisfied(int(building.get("team", -1)), building, contract) == "",
			})
			continue
		var from_set := String(contract.get("from_command_set", ""))
		if current_set == "":
			# Before any purchase the building sits on its base command set: the
			# chain step whose from-set is not itself another step's to-set.
			var is_downstream := false
			for other_id in upgrade_ids:
				var other: Dictionary = contracts[other_id]
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
		var bundle: Dictionary = structure_upgrade_effects_for_team(team).get(String(building.get("structure_kind", "")), {})
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
			_refresh_banner_carrier_state(row)
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
		_emit_event("economy.refund", int(building.get("id", 0)), 0, {"team": team, "amount": refund, "upgrade_id": upgrade_id})


func _income_with_upgrade_bonus(team: int, building: Dictionary, base_income: int) -> int:
	## Authored TerrainResourceBehavior upgrade bonuses (Grand Harvest): the
	## resource structure's own document declares the percent while the team
	## owns the technology and maintains the required building.
	var bundle: Dictionary = structure_upgrade_effects_for_team(team).get(String(building.get("structure_kind", "")), {})
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
var _spellbook_command_points_upgrade: Dictionary = {}
var _team_sciences := {PLAYER_TEAM: [], ENEMY_TEAM: []}
var _power_cooldown_until := {PLAYER_TEAM: {}, ENEMY_TEAM: {}}
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
	_spellbook_command_points_upgrade.clear()
	if typeof(document) != TYPE_DICTIONARY or String(document.get("schema", "")) != SPELLBOOK_SCHEMA:
		_spellbook_error = "spellbook document is missing or not an %s" % SPELLBOOK_SCHEMA
		return false
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var power_tree: Dictionary = registration.get("powerTree", {}) as Dictionary
	var spell_book_object: Dictionary = registration.get("spellBook", {}) as Dictionary
	var command_points_upgrade_value: Variant = spell_book_object.get("commandPointsUpgrade")
	if command_points_upgrade_value != null:
		if typeof(command_points_upgrade_value) != TYPE_DICTIONARY:
			_spellbook_error = "spellbook CommandPointsUpgrade is malformed"
			return false
		var command_points_upgrade := command_points_upgrade_value as Dictionary
		# Retail authors one CommandPointsUpgrade on the shared spellbook system
		# object (Marketplace Grand Harvest → +100 CP). Every faction's compiled
		# spellbook-runtime carries that same evidence. JSON may deliver the
		# numeric fields as int or float — accept exact retail identity with
		# integral-float tolerance only (not arbitrary amounts/objects).
		var command_points_upgrade_keys := [
			"triggeredBy", "commandPoints", "requiredObject",
			"module", "sourceIni", "line",
		]
		if command_points_upgrade.size() != command_points_upgrade_keys.size():
			_spellbook_error = "spellbook CommandPointsUpgrade is unsupported"
			return false
		for key in command_points_upgrade_keys:
			if not command_points_upgrade.has(key):
				_spellbook_error = "spellbook CommandPointsUpgrade is unsupported"
				return false
		var command_points_value: Variant = command_points_upgrade.get("commandPoints")
		var line_value: Variant = command_points_upgrade.get("line")
		var command_points_ok := (
			typeof(command_points_value) in [TYPE_INT, TYPE_FLOAT]
			and is_equal_approx(float(command_points_value), 100.0)
		)
		var line_ok := (
			typeof(line_value) in [TYPE_INT, TYPE_FLOAT]
			and is_equal_approx(float(line_value), float(int(line_value)))
			and int(line_value) > 0
		)
		if (
			String(command_points_upgrade.get("triggeredBy", "")) != "Upgrade_MarketplaceUpgradeGrandHarvest"
			or not command_points_ok
			or String(command_points_upgrade.get("requiredObject", "")) != "NONE +GondorMarketPlace"
			or String(command_points_upgrade.get("module", "")) != "CommandPointsUpgrade"
			or typeof(command_points_upgrade.get("sourceIni")) != TYPE_STRING
			or String(command_points_upgrade.get("sourceIni", "")).strip_edges() == ""
			or not line_ok
		):
			_spellbook_error = "spellbook CommandPointsUpgrade is unsupported"
			return false
		_spellbook_command_points_upgrade = command_points_upgrade.duplicate(true)
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
	# Production path: register converted OCL leaves for CreateObjectDie hatch.
	ingest_ocl_leaves_from_document({"leaves": leaves})
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
		var reload_row: Dictionary = power.get("reloadTimeMs", {}) as Dictionary
		var reload_ms := float(reload_row.get("value", 0.0))
		# Retail authors `ReloadTime = 0` DELIBERATELY on the one-shot passive
		# spells (specialpower.ini:1341-1346 SpellBookScavenger,
		# :1492-1498 SpellBookFueltheFires): they have no recharge at all. A
		# resolved-zero reload is therefore authored truth, not a conversion
		# gap, and must not be reported as one — the effect resolver owns the
		# real verdict for those powers. An ABSENT reloadTimeMs stays locked.
		#
		# TODO (one-shot gate, unreachable today): retail keeps these from being
		# recast by marking the BUTTON, not the power - commandbutton.ini:9364-9371
		# Command_SpellBookScavenger and :9457-9465 Command_SpellBookFueltheFires
		# both author `Options = NONPRESSABLE`. This sim reads reload time only, so
		# a resolved-zero reload would let a once-per-match spell fire every frame.
		# No gate is implemented because both powers are BLOCKED at the effect
		# resolver (no supply-dock economy, no kill-bounty economy), so nothing can
		# cast them. Any future power that resolves with an authored-zero reload
		# MUST carry a once-per-match gate keyed off NONPRESSABLE before it ships.
		var reload_authored_zero := (
			reload_ms == 0.0
			and String(reload_row.get("expression", "")).strip_edges() == "0"
		)
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
			"reload_ticks": 0 if reload_authored_zero else maxi(1, roundi(reload_ms / 1000.0 / TICK_SECONDS)),
			"sound_id": String(power.get("initiateSoundId", "")),
			"fx_lists": Array(references.get("fxLists", [])),
			"ocls": Array(references.get("objectCreationLists", [])),
		}
		if science_id == "":
			row["castable"] = false
			row["locked_reason"] = "power has no purchasable tree science in the document"
		elif reload_ms <= 0.0 and not reload_authored_zero:
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
	_state_hash_static_digest.clear()
	return true


## Cross-faction seam: give ONE team its own faction spellbook tree while other
## teams keep the global (player-faction) tree. Reuses the exact parser above by
## parsing into the globals and lifting the result into the per-team store, then
## restoring the globals and match-state untouched — so configuring team B never
## perturbs team A's already-configured tree or the default signature. Fails
## closed: a missing/incomplete doc leaves the team without an override (it will
## fall back to the global tree only if one exists) and returns false with the
## parse error surfaced through team_spellbook_error().
var _team_spellbook_errors: Dictionary = {}


func configure_team_spellbook_runtime(team: int, document: Dictionary) -> bool:
	# Deep copies: configure_spellbook_runtime() clears the global tree dicts in
	# place, so a reference-only capture would be wiped mid-parse.
	var saved := _spellbook_global_bundle_copy()
	var saved_error := _spellbook_error
	var saved_document := _spellbook_document
	var saved_sciences := _team_sciences.duplicate(true)
	var saved_cooldowns := _power_cooldown_until.duplicate(true)
	var saved_staged := _staged_purchases.duplicate(true)
	var ok := configure_spellbook_runtime(document)
	var parsed := _spellbook_global_bundle_copy() if ok else {}
	var parse_error := _spellbook_error
	# Restore the globals + match state exactly as they were before this call.
	_apply_spellbook_bundle(saved)
	_spellbook_document = saved_document
	_spellbook_error = saved_error
	_team_sciences = saved_sciences
	_power_cooldown_until = saved_cooldowns
	_staged_purchases = saved_staged
	if not ok:
		_team_spellbooks.erase(team)
		_team_spellbook_errors[team] = parse_error
		return false
	parsed["document"] = document.duplicate(true)
	_team_spellbooks[team] = parsed
	_team_spellbook_errors[team] = ""
	# Seed this team's ownership overlays from ITS OWN intrinsic sciences.
	_team_sciences[team] = (parsed.get("intrinsic", []) as Array).duplicate(true)
	_power_cooldown_until[team] = {}
	_staged_purchases[team] = []
	purchased_powers[team] = []
	return true


func team_spellbook_error(team: int) -> String:
	return String(_team_spellbook_errors.get(team, ""))


func team_has_spellbook_override(team: int) -> bool:
	return _team_spellbooks.has(team)


func _spellbook_global_bundle() -> Dictionary:
	## A shallow view of the current global tree fields. Used to lift a freshly
	## parsed tree into the per-team store and to save/restore around that parse.
	return {
		"ready": _spellbook_ready,
		"powers": _spellbook_powers,
		"order": _spellbook_order,
		"sciences": _spellbook_sciences,
		"science_to_power": _science_to_power,
		"intrinsic": _spellbook_intrinsic,
		"command_points_upgrade": _spellbook_command_points_upgrade,
	}


func _spellbook_global_bundle_copy() -> Dictionary:
	## A DEEP copy of the global tree, detached from the live global dicts so a
	## subsequent in-place clear/refill of those dicts cannot mutate it.
	var order_copy: Array[String] = []
	for power_id in _spellbook_order:
		order_copy.append(String(power_id))
	return {
		"ready": _spellbook_ready,
		"powers": _spellbook_powers.duplicate(true),
		"order": order_copy,
		"sciences": _spellbook_sciences.duplicate(true),
		"science_to_power": _science_to_power.duplicate(true),
		"intrinsic": (_spellbook_intrinsic as Array).duplicate(true),
		"command_points_upgrade": _spellbook_command_points_upgrade.duplicate(true),
	}


func _apply_spellbook_bundle(bundle: Dictionary) -> void:
	_spellbook_ready = bool(bundle.get("ready", false))
	_spellbook_powers = bundle.get("powers", {}) as Dictionary
	_spellbook_order = bundle.get("order", []) as Array[String]
	_spellbook_sciences = bundle.get("sciences", {}) as Dictionary
	_science_to_power = bundle.get("science_to_power", {}) as Dictionary
	_spellbook_intrinsic = bundle.get("intrinsic", []) as Array
	_spellbook_command_points_upgrade = bundle.get("command_points_upgrade", {}) as Dictionary


func _team_tree(team: int) -> Dictionary:
	## The tree a team resolves powers against: its own override when present,
	## otherwise a view of the global (default same-faction) tree. Read-only —
	## team ownership overlays live in the per-team maps, not in the tree.
	if _team_spellbooks.has(team):
		return _team_spellbooks[team]
	return _spellbook_global_bundle()


func _reset_spellbook_match_state() -> void:
	_team_sciences = _seed_team_map(_spellbook_intrinsic)
	# A team with its own faction tree starts from ITS intrinsic sciences, not
	# the global player-faction ones.
	for team_value in _team_spellbooks.keys():
		var override_tree: Dictionary = _team_spellbooks[team_value]
		_team_sciences[team_value] = (override_tree.get("intrinsic", []) as Array).duplicate(true)
	_power_cooldown_until = _seed_team_map({})
	_staged_purchases = _seed_team_map([])
	_pending_power_effects.clear()
	_active_groves.clear()
	_field_pings.clear()
	_weather_effects.clear()
	_summon_despawn_ticks.clear()
	_summon_aura_source_ids.clear()


## Timed spellbook effect state (volley strikes, summon hatches, groves).
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
			if not field_values.has("AttributeModifier"):
				# Retail sometimes ships a SpecialPowerModule whose whole payload is
				# COMMENTED OUT upstream and re-homed on another object. Fuel the Fires
				# is the case in this corpus: object/system/system.ini:369-378 comments
				# out AttributeModifier/Range/Affects with the note "Done in science
				# check on the lumber mill", and the live bonus is
				# SupplyCenterDockUpdate BonusScience = SCIENCE_FueltheFires /
				# BonusScienceMultiplier = 200% on Object LumberMill
				# (object/civilian/civilianbuildings.ini:18831-18836). The sim has no
				# worker/supply-dock economy for that multiplier to act on.
				return {"ok": false, "reason": "SpecialPowerModule authors no AttributeModifier: the payload is commented out on the module and re-homed on a SupplyCenterDockUpdate BonusScience, which the sim does not model"}
			if modifier_id == "" or not modifier_leaves.has(modifier_id):
				return {"ok": false, "reason": "attribute modifier '%s' is not a converted leaf" % modifier_id}
			# Retail AttributeModifier leaves author repeated Modifier lines
			# (DAMAGE_MULT, ARMOR, PRODUCTION, …). Collapsing by key kept only
			# the last row and falsely rejected powers that still carried a
			# resolved DAMAGE_MULT earlier in the list.
			var modifier_fields: Dictionary = {}
			var modifier_rows: Array = []
			for field_value in Array((modifier_leaves[modifier_id] as Dictionary).get("fields", [])):
				if typeof(field_value) != TYPE_DICTIONARY:
					continue
				var field_row := field_value as Dictionary
				var field_key := String(field_row.get("key", ""))
				var field_text := String(field_row.get("value", ""))
				if field_key == "Modifier":
					modifier_rows.append(field_text)
				elif field_key != "":
					modifier_fields[field_key] = field_text
			var damage_mult := 0.0
			var armor_mult := 0.0
			var production_mult := 0.0
			var invulnerable := false
			var unsupported_rows: Array = []
			# WHICH ROWS THE LEAF ACTUALLY AUTHORED. The three multipliers below
			# all default to the neutral 1.0 when their row is absent, so a
			# consumer reading only the numbers cannot tell an AUTHORED 100% from
			# a row that was never written. That distinction is not academic: the
			# spellbook matrix classifies a power as "castable but inert" when its
			# damage and armor multipliers are neutral and its production
			# multiplier is not, and "neutral" meant "absent OR authored-100%".
			# The grove-aura lane already publishes exactly this table for the
			# same reason (see the `authored_rows` key on the grove_aura effect).
			var authored_rows: Dictionary = {
				"DAMAGE_MULT": false, "ARMOR": false, "PRODUCTION": false
			}
			for modifier_text_value in modifier_rows:
				var parsed := _parse_modifier_row(String(modifier_text_value))
				# Fail-closed on an UNREADABLE row (see _parse_modifier_row). An
				# unconsumed KIND still falls through: this probe only asks whether the
				# leaf carries an effect the sim can apply, and `has_effect` below is
				# the verdict for that.
				if not parsed.get("ok", false):
					return {"ok": false, "reason": "attribute modifier '%s': %s" % [
						modifier_id, String(parsed.get("reason", "")),
					]}
				if not bool(parsed.get("supported", false)):
					# READ but not modelled — named and counted, never a silent drop and
					# never a shape error that takes the readable rows beside it down
					# with it. angmar/SpellBookSnowbind is exactly this case: its
					# `INVULNERABLE 0% SLASH PIERCE …` damage-type scope list has no
					# runtime here, while the `PRODUCTION 1%` row on the same leaf is
					# perfectly readable and used to be lost with it.
					unsupported_rows.append({
						"row": String(modifier_text_value),
						"shape": String(parsed.get("shape", "")),
						"reason": String(parsed.get("reason", "")),
					})
					continue
				var kind := String(parsed.get("kind", ""))
				# Only the percent shape is a multiplier; KIND_PLAIN is an absolute
				# magnitude (HEALTH 400) and must not be read as one.
				if String(parsed.get("shape", "")) != "percent":
					unsupported_rows.append({
						"row": String(modifier_text_value),
						"shape": String(parsed.get("shape", "")),
						"reason": "absolute-magnitude '%s' row has no multiplier runtime here" % kind,
					})
					continue
				var percent := float(parsed.get("value", 0.0))
				if kind == "DAMAGE_MULT":
					damage_mult = percent
					authored_rows["DAMAGE_MULT"] = true
				elif kind == "ARMOR":
					armor_mult = percent
					authored_rows["ARMOR"] = true
				elif kind == "PRODUCTION":
					production_mult = percent
					authored_rows["PRODUCTION"] = true
			var duration_ms := _spellbook_field_float(modifier_fields, "Duration", 0.0)
			# Duration may be an unresolved define name on the leaf; power-level
			# field_resolved already carries numeric AttributeModifierRange.
			if duration_ms <= 0.0 and modifier_fields.has("Duration"):
				var duration_text := String(modifier_fields.get("Duration", ""))
				# Common retail define pattern: NAME_EFFECT_DURATION — resolve via
				# power field table when the leaf only stores the define token.
				if duration_text != "" and field_resolved.has(duration_text):
					duration_ms = float(field_resolved.get(duration_text, 0.0))
			var range_source := _spellbook_field_float(field_values, "AttributeModifierRange", 0.0)
			if range_source <= 0.0 and field_resolved.has("AttributeModifierRange"):
				range_source = float(field_resolved.get("AttributeModifierRange", 0.0))
			# `invulnerable` can no longer be set from a row: retail authors ZERO bare
			# `INVULNERABLE` flags (census in _parse_modifier_row) — every authored
			# INVULNERABLE carries `0%` plus a damage-type scope list, which is
			# read-but-not-supported above. The field is kept (always false) so the
			# effect shape and its consumers do not move; blanket invulnerability was
			# never actually authored in this corpus.
			var has_effect := damage_mult > 0.0 or armor_mult > 0.0 or production_mult > 0.0 or invulnerable
			# Economy PRODUCTION auras (Industry / Dwarven Riches) are permanent
			# while the model condition holds — no Duration row. Combat auras
			# require a positive duration.
			var duration_ok := duration_ms > 0.0 or production_mult > 0.0
			if not has_effect or not duration_ok or range_source <= 0.0:
				return {"ok": false, "reason": "attribute modifier leaf lacks a resolved damage mult, duration, or range"}
			var duration_ticks := 0
			if duration_ms > 0.0:
				duration_ticks = maxi(1, roundi(duration_ms / 1000.0 / TICK_SECONDS))
			return {"ok": true, "effect": {
				"kind": "attribute_modifier",
				"damage_mult": damage_mult if damage_mult > 0.0 else 1.0,
				"armor_mult": armor_mult if armor_mult > 0.0 else 1.0,
				"production_mult": production_mult if production_mult > 0.0 else 1.0,
				"invulnerable": invulnerable,
				"duration_ticks": duration_ticks,
				"permanent": duration_ticks == 0 and production_mult > 0.0,
				"range_source": range_source,
				"affects": String(field_values.get("AttributeModifierAffects", "")),
				# Which of the three multiplier rows the leaf actually authored, so
				# an authored-neutral 100% is distinguishable from an absent row.
				"authored_rows": authored_rows,
				# Named residual rows carried onto the effect so the runner and the
				# report can COUNT them instead of losing them.
				"unsupported_modifier_rows": unsupported_rows,
			}}
		"OCLSpecialPower":
			return _spellbook_ocl_support(
				power_row, references, modifier_leaves, object_leaves, ocl_leaves, weapon_leaves,
				String(field_values.get("CreateLocation", "")),
				String(field_values.get("NearestSecondaryObjectFilter", ""))
			)
		"DarknessSpecialPower":
			return _spellbook_weather_modifier_support(field_values, field_resolved, modifier_leaves)
		"FreezingRainSpecialPower":
			return _spellbook_weather_anticategory_support(field_values, field_resolved)
		"UntamedAllegianceSpecialPower":
			return _spellbook_untamed_allegiance_support(field_values, field_resolved)
		"DevastateSpecialPower":
			# Retail's Devastation squeezes resources out of the map's TREE objects
			# (TreeValueMultiplier 50%, TreeValueTotalCap 1500 -
			# object/system/system.ini:241-252) and fires DevastationEntWeapon, whose
			# only damage nugget is filtered to `NONE +RohanGenericEnt +RohanTreeBerd
			# ENEMIES` (weapon.ini DevastationEntWeapon). The sim models neither trees
			# as harvestable objects nor Ent units, so BOTH halves have no target.
			return {"ok": false, "reason": "DevastateSpecialPower has no target in the sim: its resource half converts TREE objects (TreeValueMultiplier %s, TreeValueTotalCap %d), which the sim does not model, and its damage half (FireWeapon '%s') is filtered to Ents only" % [
				String(field_values.get("TreeValueMultiplier", "")),
				int(field_resolved.get("TreeValueTotalCap", 0)),
				String(field_values.get("FireWeapon", "")),
			]}
		"ScavengerSpecialPower":
			# BountyPercent scales the per-kill BountyValue award. The sim has no
			# kill-bounty economy at all (resources come from structure payouts and
			# AutoDepositUpdate), so there is nothing for the percent to scale.
			return {"ok": false, "reason": "ScavengerSpecialPower scales kill bounty (BountyPercent %s) but the sim has no BountyValue kill-award economy to scale" % String(field_values.get("BountyPercent", ""))}
		"ElvenWoodSpecialPower":
			return _spellbook_grove_support(field_values, field_resolved, references, modifier_leaves, object_leaves, ocl_leaves, "ElvenGroveObject")
		"TaintSpecialPower":
			# Retail's TaintSpecialPower and ElvenWoodSpecialPower are the SAME
			# module shape with a different planted object: `TaintObject/TaintRadius/
			# TaintFX/TaintOCL` against `ElvenGroveObject/ElvenWoodRadius/
			# ElvenWoodFX/ElvenWoodOCL` (data/ini/object/system/system.ini:31-38 vs
			# :829-837). TaintLand and ElvenGrove are byte-identical objects apart
			# from Side and the RequiredConditions cell type
			# (object/evilfaction/structures/taintland.ini:3-46 vs
			# object/goodfaction/structures/elven/grove.ini:3-47), so one resolver
			# serves both.
			return _spellbook_grove_support(field_values, field_resolved, references, modifier_leaves, object_leaves, ocl_leaves, "TaintObject")
		"CloudBreakSpecialPower":
			return _spellbook_cloudbreak_support(field_values, field_resolved)
		_:
			return {"ok": false, "reason": "unsupported effect module '%s'" % module}


func _spellbook_field_float(fields: Dictionary, key: String, fallback: float) -> float:
	var raw := String(fields.get(key, ""))
	if raw == "" or not raw.is_valid_float():
		return fallback
	return float(raw)


func _parse_modifier_row(value: String) -> Dictionary:
	## THE one reader of an AttributeModifier leaf's `Modifier =` row. Every
	## resolver that used to split the text itself is routed through here.
	##
	## Retail authors these rows TAB-separated as often as space-separated
	## (attributemodifier.ini:74 `Modifier = ARMOR<TAB>50%` against :75
	## `Modifier = DAMAGE_MULT 150%`) and the converter preserves the tab
	## verbatim, so a space-only split silently dropped every tabbed row. That
	## is exactly what kept Elven Wood reading as "modifier is not converted",
	## and duplicating the normalization at four call sites is how one of them
	## kept missing it.
	##
	## SHAPES ARE A CENSUS, NOT A GUESS. Round 18 counted every `Modifier =` row
	## in the PURE RETAIL oracle tree
	## (.private/retail-work/editions/rotwk/cache/effective-assets, all .ini/.inc,
	## comments stripped): 894 rows total, and they fall into exactly three
	## authored shapes —
	##
	##   KIND_PCT   386  `ARMOR<TAB>50%`, `DAMAGE_MULT 150%`
	##                     -> shape "percent", value = n / 100.0
	##   KIND_PLAIN 363  `HEALTH 400`, `CRUSHABLE_LEVEL 3`, and the far commoner
	##                   `ARMOR <DEFINE_TOKEN>` / `HEALTH <DEFINE_TOKEN>` form
	##                     -> shape "plain", value = n (NOT divided); an
	##                        unresolved define token is unreadable, ok=false
	##   MULTI      145  `INVULNERABLE 0% SLASH PIERCE …` (a damage-type scope
	##                   list), `ARMOR <TOKEN> CRUSH`, `DAMAGE_MULT
	##                   #MULTIPLY( X 0.60 )`, `PRODUCTION <TOKEN> %` (the
	##                   percent sign detached by whitespace)
	##                     -> shape "percent_scoped"/"plain_scoped"/"expression",
	##                        ok=true, supported=false, with a NAMED reason
	##
	##   BARE_FLAG    0  ZERO rows in the corpus are a single bare token. The old
	##                   `parts.size() == 1 -> flag row` branch was dead code
	##                   invented from the *appearance* of `INVULNERABLE`, whose
	##                   real authored form always carries `0%` plus a scope list
	##                   (attributemodifier.ini:909, :1079). It is deleted: one
	##                   token is now unreadable and fails closed.
	##
	## THREE-WAY VERDICT, because two different mistakes are possible:
	##   ok=false                -> UNREADABLE. Fail closed at the call site: a
	##                              row nobody can read is a buff that would go
	##                              missing in silence.
	##   ok=true, supported=false -> READ, but its runtime is not modelled. The
	##                              caller may explicitly not-support it by name
	##                              (`reason`) and carry on with the rows it can
	##                              apply. A shape error here used to lock whole
	##                              powers (angmar/SpellBookSnowbind lost its
	##                              readable `PRODUCTION 1%` because the
	##                              INVULNERABLE row beside it was called a shape
	##                              error).
	##   ok=true, supported=true  -> a plain scalar the generic consumers apply.
	##
	## `flag` is retained on every result (always false) so callers written
	## against the old contract keep compiling; nothing sets it any more.
	var parts_raw := value.replace("\t", " ").split(" ", false)
	var parts: Array[String] = []
	for part_value in parts_raw:
		parts.append(String(part_value))
	if parts.is_empty():
		return {"ok": false, "reason": "modifier row is empty"}
	var kind: String = parts[0]
	var rest: Array[String] = parts.slice(1)
	if rest.size() >= 2 and rest[rest.size() - 1] == "%":
		# `PRODUCTION ROHAN_FARM_LVL2_PRODUCTION  %` (attributemodifier.ini:1406):
		# retail lets the percent sign drift off its magnitude. Reattach it before
		# anything else so the row is judged on its real shape.
		rest = rest.slice(0, rest.size() - 1)
		rest[rest.size() - 1] = rest[rest.size() - 1] + "%"
	if rest.is_empty():
		return {"ok": false, "reason": "modifier row '%s' is a single token with no magnitude; retail authors none (census: 0 of 894)" % value}
	var head: String = rest[0]
	var scope: Array = rest.slice(1)
	if head.begins_with("#"):
		# `DAMAGE_MULT #MULTIPLY( CREATE_A_HERO_ATTRIBUTE_MULTIPLIER 0.60 )`.
		# The pack ships the expression verbatim; the sim has no INI expression
		# evaluator, so this is read-but-not-supported rather than a shape error.
		return {
			"ok": true, "supported": false, "flag": false, "shape": "expression",
			"kind": kind, "value": 0.0, "scope": rest,
			"reason": "modifier row '%s' is an authored INI expression; the pack ships it unevaluated and the sim has no expression evaluator" % value,
		}
	var magnitude := 0.0
	var shape := ""
	if head.ends_with("%"):
		var percent_text: String = head.trim_suffix("%")
		if not percent_text.is_valid_float():
			return {"ok": false, "reason": "modifier row '%s' has a non-numeric percent" % value}
		magnitude = float(percent_text) / 100.0
		shape = "percent"
	elif head.is_valid_float():
		# KIND_PLAIN carries an ABSOLUTE magnitude (HEALTH 400), not a percent.
		# Callers must read `shape` before treating `value` as a multiplier.
		magnitude = float(head)
		shape = "plain"
	else:
		return {"ok": false, "reason": "modifier row '%s' magnitude '%s' is an unresolved define token" % [value, head]}
	if scope.is_empty():
		return {
			"ok": true, "supported": true, "flag": false, "shape": shape,
			"kind": kind, "value": magnitude, "scope": [],
		}
	return {
		"ok": true, "supported": false, "flag": false, "shape": shape + "_scoped",
		"kind": kind, "value": magnitude, "scope": scope,
		"reason": "modifier row '%s' scopes %s to the damage-type list %s; the sim applies modifiers globally and does not model per-damage-type scoping" % [
			value, kind, " ".join(scope),
		],
	}


func _spellbook_ocl_support(power_row: Dictionary, references: Dictionary, modifier_leaves: Dictionary, object_leaves: Dictionary, ocl_leaves: Dictionary, weapon_leaves: Dictionary, create_location: String = "", secondary_object_filter: String = "") -> Dictionary:
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
	var creates: Array = ocl.get("createObjects", []) as Array
	for create_index in range(creates.size()):
		var create_value: Variant = creates[create_index]
		if typeof(create_value) != TYPE_DICTIONARY:
			continue
		var create := create_value as Dictionary
		for object_name_value in Array(create.get("objects", [])):
			var object_name := String(object_name_value)
			var leaf: Dictionary = object_leaves.get(object_name, {}) as Dictionary
			if leaf.is_empty():
				missing.append(object_name)
			else:
				spawns.append({"create": create, "create_index": create_index, "leaf": leaf})
	if not missing.is_empty():
		return {"ok": false, "reason": "spawned object(s) %s are not converted leaves" % ", ".join(missing)}
	if spawns.is_empty():
		return {"ok": false, "reason": "object-creation list '%s' creates no objects" % ocl_id}
	var first_leaf: Dictionary = (spawns[0] as Dictionary)["leaf"]
	if not Array(first_leaf.get("fireWeapons", [])).is_empty():
		return _spellbook_fire_weapon_support(spawns, weapon_leaves)
	# Reveal/field "ping" objects are not units and must not be routed through the
	# summon resolver, which rejected them for the irrelevant reason that an
	# IMMOBILE object authors no locomotor. Returns {} for anything else.
	var ping_verdict := _spellbook_field_ping_support(spawns, modifier_leaves)
	if not ping_verdict.is_empty():
		return ping_verdict
	# Shape detectors that own a MORE PRECISE reason than the generic summon or
	# structure paths would produce. Each names the single authored module that
	# carries the whole power, so a future converter change can be aimed at it.
	var shaped := _spellbook_ocl_named_gap(spawns, object_leaves, ocl_leaves, create_location, secondary_object_filter)
	if not shaped.is_empty():
		return shaped
	for spawn_value in spawns:
		var hatch_leaf: Dictionary = (spawn_value as Dictionary).get("leaf", {}) as Dictionary
		if typeof(hatch_leaf.get("hatch", null)) == TYPE_DICTIONARY:
			var summon_verdict := _spellbook_summon_support(spawns, modifier_leaves, object_leaves, ocl_leaves, weapon_leaves)
			if bool(summon_verdict.get("ok", false)):
				return summon_verdict
			var preview := _spellbook_summon_literal_preview(spawns, object_leaves, ocl_leaves)
			if bool(preview.get("ok", false)):
				summon_verdict["effect"] = preview.get("effect", {})
			return summon_verdict
	var body_kinds: Array = first_leaf.get("bodyKinds", []) as Array
	if body_kinds.has("StructureBody"):
		return _spellbook_structure_summon_support(spawns[0], weapon_leaves)
	# Direct summon OCLs (Ents, Eagles, Crebain/Cave Bats) create their live
	# units without an egg. Dragon Strike is identified by the spawned leaf's
	# authored-but-unconverted StrafeAreaUpdate, never by an unrelated OCL flag.
	for spawn_value in spawns:
		var leaf: Dictionary = (spawn_value as Dictionary).get("leaf", {}) as Dictionary
		if Array(leaf.get("unconvertedBehaviors", [])).has("StrafeAreaUpdate"):
			var preview := _spellbook_direct_summon_support(spawns, modifier_leaves, object_leaves, weapon_leaves)
			var verdict := {"ok": false, "reason": "spawned object '%s' requires StrafeAreaUpdate attack-run runtime" % String(leaf.get("id", ""))}
			if bool(preview.get("ok", false)):
				verdict["effect"] = preview.get("effect", {})
			return verdict
	# A direct summoned unit may author a SlowDeath corpse-effect OCL. Only an
	# actual egg is blocked here: its unconverted SlowDeath OCL creates the live
	# summon payload. Scan every leaf so a later egg cannot evade the gate.
	for spawn_value in spawns:
		var leaf: Dictionary = (spawn_value as Dictionary).get("leaf", {}) as Dictionary
		if _spellbook_has_unconverted_hatch_payload(leaf, object_leaves, ocl_leaves):
			return {"ok": false, "reason": "spawned object '%s' has an unconverted SlowDeathBehavior hatch OCL" % String(leaf.get("id", ""))}
	return _spellbook_direct_summon_support(spawns, modifier_leaves, object_leaves, weapon_leaves)


func _spellbook_has_unconverted_hatch_payload(leaf: Dictionary, object_leaves: Dictionary, ocl_leaves: Dictionary) -> bool:
	if not Array(leaf.get("unconvertedBehaviors", [])).has("SlowDeathBehavior"):
		return false
	for ocl_id_value in Array(leaf.get("unconvertedSlowDeathOcls", [])):
		var ocl: Dictionary = ocl_leaves.get(String(ocl_id_value), {}) as Dictionary
		for create_value in Array(ocl.get("createObjects", [])):
			if typeof(create_value) != TYPE_DICTIONARY:
				continue
			for object_id_value in Array((create_value as Dictionary).get("objects", [])):
				var payload: Dictionary = object_leaves.get(String(object_id_value), {}) as Dictionary
				if payload.is_empty():
					continue
				if payload.has("horde") or payload.has("weaponId") or int(payload.get("maxHealth", 0)) > 1:
					return true
	return false


func _spellbook_hatch_payload_leaves(leaf: Dictionary, object_leaves: Dictionary, ocl_leaves: Dictionary) -> Array:
	## One hatch level down: the objects an egg's CreateObjectDie OCL creates.
	## Several powers park their entire runtime behind the egg (Avalanche's
	## FloodUpdate wave, the Citadel's CastleBehavior), so a named-gap scan that
	## only looked at the power OCL's direct spawns would miss them.
	var payload: Array = []
	var hatch_value: Variant = leaf.get("hatch", null)
	if typeof(hatch_value) != TYPE_DICTIONARY:
		return payload
	var ocl: Dictionary = ocl_leaves.get(String((hatch_value as Dictionary).get("ocl", "")), {}) as Dictionary
	for create_value in Array(ocl.get("createObjects", [])):
		if typeof(create_value) != TYPE_DICTIONARY:
			continue
		for object_id_value in Array((create_value as Dictionary).get("objects", [])):
			var child: Dictionary = object_leaves.get(String(object_id_value), {}) as Dictionary
			if not child.is_empty():
				payload.append(child)
	return payload


func _spellbook_ocl_named_gap(spawns: Array, object_leaves: Dictionary, ocl_leaves: Dictionary, create_location: String, secondary_object_filter: String) -> Dictionary:
	## Precise fail-closed verdicts for OCL powers whose blocker is a single
	## authored module, reported BEFORE the generic summon/structure resolvers
	## produce an incidental (and misleading) reason such as "locomotion is not
	## converted" for an object that is deliberately IMMOBILE.
	## Returns {} when no named shape matches.
	var chain: Array = []
	for spawn_value in spawns:
		var spawn_leaf: Dictionary = (spawn_value as Dictionary).get("leaf", {}) as Dictionary
		if spawn_leaf.is_empty():
			continue
		chain.append(spawn_leaf)
		chain.append_array(_spellbook_hatch_payload_leaves(spawn_leaf, object_leaves, ocl_leaves))
	for leaf_value in chain:
		var leaf: Dictionary = leaf_value as Dictionary
		var unconverted: Array = leaf.get("unconvertedBehaviors", []) as Array
		if unconverted.has("FloodUpdate"):
			# Avalanche / Flood: the spawned object is an IMMOBILE INERT NO_COLLIDE
			# marker with an ImmortalBody, maxHealth 1 and a DeletionUpdate. Every
			# gameplay value of the power - the sweep path, its damage, the units it
			# throws - lives in FloodUpdate, which the compiler does not emit.
			return {"ok": false, "reason": "spawned object '%s' carries the whole power in FloodUpdate (wave path, damage and throw), which is not converted; the converted leaf is an immortal maxHealth-1 marker with only a %d ms DeletionUpdate" % [
				String(leaf.get("id", "")),
				int((leaf.get("deletion", {}) as Dictionary).get("maxMs", 0)),
			]}
		if unconverted.has("CastleBehavior"):
			# Citadel: the summoned object is a castle FOUNDATION. Its value is the
			# plot layout CastleBehavior declares (which expansion sites exist, what
			# may be built on them); without it the leaf is an UNATTACKABLE,
			# maxHealth-1 immortal marker with nothing to build on.
			return {"ok": false, "reason": "summoned object '%s' is a castle foundation whose CastleBehavior (plot layout and expansion sites) is not converted; the leaf converts to an UNATTACKABLE immortal maxHealth-%d marker with no plots" % [
				String(leaf.get("id", "")), int(leaf.get("maxHealth", 0)),
			]}
	if create_location == "USE_SECONDARY_OBJECT_LOCATION":
		# Bombard / Evil Bombard. The payload IS fully converted (20 seeds,
		# SpreadFormation, each seed hatching a projectile that fires
		# BombardProjectileWeapon at +600 ms for 400 SIEGE over radius 100), but the
		# barrage FOOTPRINT is the power, and where the seeds are laid out is not
		# determinable: CreateLocation names the secondary object while the same
		# CreateObject block also authors OrientInSecondaryDirection, so "at the
		# keep" and "at the cast point, oriented away from the keep" both read
		# consistently. The OpenSAGE oracle does not settle it either -
		# OCLSpecialPower.cs:35 throws NotImplementedException for this enum.
		# Guessing would move every impact, so the power stays locked.
		# TWO further runtime blockers ride the same payload, both verified against
		# the converted dwarves pack's BombardPhaseInitialWeapon /
		# BombardProjectileWeapon leaves:
		#   - the fear half is a LuaEventNugget (`LuaEvent = BeUncontrollablyAfraid`,
		#     Radius 200, SendToEnemies/SendToNeutral Yes) - retail routes it through
		#     the Lua event bus, which this sim does not have at all;
		#   - the impact half carries a MetaImpactNugget (ShockWaveAmount 75.0,
		#     ShockWaveTaperOff 1.0, ShockWaveRadius 50) - physics knockback with no
		#     model here.
		# So even a settled spread origin would leave the power partly unmodelled;
		# recording only the footprint ambiguity understated the gap.
		return {"ok": false, "reason": "OCLSpecialPower CreateLocation = USE_SECONDARY_OBJECT_LOCATION (NearestSecondaryObjectFilter '%s') is not modelled: the spread origin of the seed formation - caster keep or cast point - is not determinable from the converted pack, and the barrage footprint is the whole power. Two further halves have no runtime here either: the LuaEventNugget fear pulse (LuaEvent BeUncontrollablyAfraid, radius 200) needs retail's Lua event bus, and the MetaImpactNugget shockwave (amount 75.0, taper 1.0, radius 50) needs physics knockback" % secondary_object_filter}
	return {}


## --- Weather-based global spells (Darkness, Freezing Rain) -------------------
## Retail models these as a WEATHER change plus a global, range-less effect that
## holds for WeatherDuration. Neither power authors an AttributeModifierRange or
## a NEED_TARGET_POS cast option: the scope is the whole map. The sim therefore
## keeps a live window rather than doing a one-shot sweep, and re-applies it on
## the shared aura cadence so units that appear during the window are covered
## exactly as they are in retail (AttributeModifierWeatherBased = Yes).
## EMPTY-IS-ABSENT in the serialized state (see _serialize_state).
var _weather_effects: Array[Dictionary] = []


func _spellbook_weather_modifier_support(field_values: Dictionary, field_resolved: Dictionary, modifier_leaves: Dictionary) -> Dictionary:
	## RECORDED, redundant-but-unconsumed: SpellBookDarkness also authors a
	## SpecialPower-LEVEL filter (specialpower.ini:1446-1455 `ObjectFilter = ANY
	## -STRUCTURE -DwarvenZerker -NoldorWarrior -GondorKnightsofDol
	## -WildBabyDrake -IsengardFanatic -MordorBlackRider`). The converter does not
	## carry it onto the power row, and it changes nothing: the MODULE filter read
	## below (AttributeModifierAffects) is strictly narrower - it is ALLIES-only,
	## admits only +INFANTRY +CAVALRY +MONSTER (so -STRUCTURE is already implied),
	## and repeats every one of those unit exclusions. Noted here so the absence
	## reads as verified-inert rather than overlooked.
	if String(field_values.get("AttributeModifierWeatherBased", "")) != "Yes":
		return {"ok": false, "reason": "weather power does not author AttributeModifierWeatherBased = Yes"}
	var weather_ms := float(field_resolved.get("WeatherDuration", 0.0))
	if weather_ms <= 0.0:
		return {"ok": false, "reason": "WeatherDuration did not resolve in the document"}
	var modifier_id := String(field_values.get("AttributeModifier", ""))
	var modifier: Dictionary = modifier_leaves.get(modifier_id, {}) as Dictionary
	if modifier.is_empty():
		return {"ok": false, "reason": "weather attribute modifier '%s' is not a converted leaf" % modifier_id}
	var modifiers: Array = []
	var category := ""
	var leaf_duration_ms := 0.0
	for field_value in Array(modifier.get("fields", [])):
		if typeof(field_value) != TYPE_DICTIONARY:
			continue
		var field := field_value as Dictionary
		var key := String(field.get("key", ""))
		var value := String(field.get("value", ""))
		if key == "Category":
			category = value
		elif key == "Duration" and value.is_valid_float():
			leaf_duration_ms = float(value)
		elif key == "Modifier":
			var parsed := _parse_modifier_row(value)
			if not parsed.get("ok", false):
				return {"ok": false, "reason": "weather modifier '%s' has an unreadable modifier row: %s" % [modifier_id, String(parsed.get("reason", ""))]}
			var kind := String(parsed.get("kind", ""))
			# STRICT lane: a weather modifier is a whole-army buff and every authored
			# row of it has to land, so a read-but-not-modelled row still locks the
			# power — under its own name, not as a shape error.
			if not bool(parsed.get("supported", false)):
				return {"ok": false, "reason": "weather modifier '%s' has a row with no runtime: %s" % [modifier_id, String(parsed.get("reason", ""))]}
			if String(parsed.get("shape", "")) != "percent" or kind not in ["ARMOR", "DAMAGE_MULT", "EXPERIENCE"]:
				return {"ok": false, "reason": "weather modifier '%s' requires unsupported '%s' runtime" % [modifier_id, kind]}
			modifiers.append({"kind": kind, "value": float(parsed.get("value", 0.0))})
	if modifiers.is_empty():
		return {"ok": false, "reason": "weather modifier '%s' carries no converted stat rows" % modifier_id}
	# The authored leaf is INFINITE (Duration = 0): the weather window is what
	# bounds it. A positive leaf duration would be a different, shorter contract
	# than the one implemented here, so it fails closed rather than being
	# silently widened to the weather window.
	if leaf_duration_ms > 0.0:
		return {"ok": false, "reason": "weather modifier '%s' authors its own Duration %.0f ms; only the infinite (Duration = 0) weather-bounded form is modelled" % [modifier_id, leaf_duration_ms]}
	return {"ok": true, "effect": {
		"kind": "weather_modifier",
		"modifier_id": modifier_id,
		"category": category,
		"modifiers": modifiers,
		"duration_ticks": maxi(1, roundi(weather_ms / 1000.0 / TICK_SECONDS)),
		"weather": String(field_values.get("ChangeWeather", "")),
		"affects": String(field_values.get("AttributeModifierAffects", "")),
	}}


func _spellbook_weather_anticategory_support(field_values: Dictionary, field_resolved: Dictionary) -> Dictionary:
	if String(field_values.get("AttributeModifierWeatherBased", "")) != "Yes":
		return {"ok": false, "reason": "weather power does not author AttributeModifierWeatherBased = Yes"}
	var weather_ms := float(field_resolved.get("WeatherDuration", 0.0))
	if weather_ms <= 0.0:
		return {"ok": false, "reason": "WeatherDuration did not resolve in the document"}
	var anti_category := String(field_values.get("AntiCategory", ""))
	if anti_category != "LEADERSHIP":
		# LEADERSHIP is the one modifier category the sim can suppress
		# (leadership_suppressed_until_tick, shared with Horn of Gondor).
		return {"ok": false, "reason": "AntiCategory '%s' has no suppression runtime in the sim" % anti_category}
	var affects := String(field_values.get("AttributeModifierAffects", ""))
	if affects == "":
		return {"ok": false, "reason": "AttributeModifierAffects is absent from the document"}
	# The burn-rate half of Freezing Rain (BurnRateModifier / BurnDecayModifier)
	# acts on retail's FireLogicSystem, which the sim does not model. It is
	# carried as evidence on the effect and named, never silently dropped.
	var unconverted: Array = []
	if field_resolved.has("BurnRateModifier") or field_resolved.has("BurnDecayModifier"):
		unconverted.append("FireLogicSystem burn rate/decay")
	return {"ok": true, "effect": {
		"kind": "weather_anticategory",
		"anti_category": anti_category,
		"duration_ticks": maxi(1, roundi(weather_ms / 1000.0 / TICK_SECONDS)),
		"weather": String(field_values.get("ChangeWeather", "")),
		"affects": affects,
		"burn_rate_modifier": float(field_resolved.get("BurnRateModifier", 0.0)),
		"burn_decay_modifier": float(field_resolved.get("BurnDecayModifier", 0.0)),
		"unconverted_behaviors": unconverted,
	}}


func _spellbook_untamed_allegiance_support(field_values: Dictionary, field_resolved: Dictionary) -> Dictionary:
	## Lair conversion. The authored payload is exactly three things: TargetEnemy,
	## an object filter naming every creep lair and slaved creep, and a radius.
	## There is no AttributeModifier and no duration - the allegiance is
	## permanent, which is why the module carries neither.
	var range_source := float(field_resolved.get("AttributeModifierRange", 0.0))
	if range_source <= 0.0:
		return {"ok": false, "reason": "AttributeModifierRange did not resolve in the document"}
	var filter := String(field_values.get("AttributeModifierAffects", ""))
	if filter == "" or filter.ends_with("_OBJECTFILTER") or filter.ends_with("_OBJECT_FILTER"):
		return {"ok": false, "reason": "creep object filter '%s' did not resolve to its member list in the document" % filter}
	var lair_types: Array = []
	for term_value in filter.split(" ", false):
		var term := String(term_value)
		if term.begins_with("+") and term.contains("Lair"):
			lair_types.append(term.trim_prefix("+"))
	if lair_types.is_empty():
		return {"ok": false, "reason": "resolved creep filter names no lair objects"}
	lair_types.sort()
	return {"ok": true, "effect": {
		"kind": "creep_allegiance",
		"range_source": range_source,
		"filter": filter,
		"lair_types": lair_types,
		"target_enemy": String(field_values.get("TargetEnemy", "")) == "Yes",
	}}


func _spellbook_fire_weapon_support(spawns: Array, weapon_leaves: Dictionary) -> Dictionary:
	var strikes: Array = []
	var seen_weapons: Array = []
	for spawn_value in spawns:
		var spawn := spawn_value as Dictionary
		for fw_value in Array((spawn["leaf"] as Dictionary).get("fireWeapons", [])):
			var fw := fw_value as Dictionary
			var weapon_id := String(fw.get("weapon", ""))
			var weapon: Dictionary = weapon_leaves.get(weapon_id, {}) as Dictionary
			if weapon.is_empty():
				return {"ok": false, "reason": "fire-weapon '%s' is not a converted leaf" % weapon_id}
			if not seen_weapons.has(weapon_id):
				seen_weapons.append(weapon_id)
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
		# Undermine is this case: DwarvenUndermineSpawnWeapon has no DamageNugget
		# at all, only a MetaImpactNugget whose payload is an instant-death filter
		# plus a shock wave, and whose ShockWaveRadius is still the unresolved
		# define SPELL_UNDERMINE_SPAWN_DAMAGE_RADIUS in the pack. Naming the nugget
		# kinds points the fix at the compiler rather than at "no damage".
		var nugget_kinds: Array = []
		for weapon_id_value in seen_weapons:
			var seen_weapon: Dictionary = weapon_leaves.get(String(weapon_id_value), {}) as Dictionary
			for nugget_value in Array(seen_weapon.get("nuggets", [])):
				var nugget_kind := String((nugget_value as Dictionary).get("kind", ""))
				if nugget_kind != "" and not nugget_kinds.has(nugget_kind):
					nugget_kinds.append(nugget_kind)
		if nugget_kinds.is_empty():
			return {"ok": false, "reason": "fire-weapon chain (%s) carries no resolved damage nuggets" % ", ".join(seen_weapons)}
		return {"ok": false, "reason": "fire-weapon chain (%s) authors no DamageNugget; its only payload is %s, which has no sim runtime" % [", ".join(seen_weapons), ", ".join(nugget_kinds)]}
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
	if weapon_id == "":
		# The Barricade is this case. It is SPAWNS_ARE_THE_WEAPONS: the structure
		# itself never shoots, its garrison does, and that garrison is the
		# unconverted SpawnBehavior (barricade.ini:175-184 - SpawnNumber 4,
		# InitialBurst 4, SpawnTemplateName MordorArcherBarricade_Slaved,
		# SpawnedRequireSpawner Yes). Summoning a silent 3000 HP wall would ship
		# half a power as if it were whole.
		var kind_of: Array = leaf.get("kindOf", []) as Array
		var unconverted: Array = leaf.get("unconvertedBehaviors", []) as Array
		if kind_of.has("SPAWNS_ARE_THE_WEAPONS") and unconverted.has("SpawnBehavior"):
			return {"ok": false, "reason": "summoned structure '%s' is SPAWNS_ARE_THE_WEAPONS: its entire combat payload is the unconverted SpawnBehavior garrison and the leaf authors no weapon of its own" % String(leaf.get("id", ""))}
		return {"ok": false, "reason": "summoned structure '%s' authors no weapon" % String(leaf.get("id", ""))}
	if weapon.is_empty() or nuggets.is_empty() or attack_range <= 0.0:
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


func _spellbook_direct_summon_support(spawns: Array, modifier_leaves: Dictionary, object_leaves: Dictionary, weapon_leaves: Dictionary) -> Dictionary:
	## Direct OCL summon: validate every authored object and apply the same
	## Count/ObjectNames choice-pool semantics used by egg hatch OCLs. Choice
	## groups remain declarative here; the shared logic RNG is touched per cast.
	var groups_by_index: Dictionary = {}
	for spawn_value in spawns:
		var spawn := spawn_value as Dictionary
		var create: Dictionary = spawn.get("create", {}) as Dictionary
		var leaf: Dictionary = spawn.get("leaf", {}) as Dictionary
		var create_index := int(spawn.get("create_index", -1))
		var object_id := String(leaf.get("id", ""))
		if not Array(create.get("objects", [])).has(object_id):
			return {"ok": false, "reason": "direct summon object '%s' is absent from its CreateObject choice list" % String(leaf.get("id", ""))}
		var verdict := _spellbook_summon_rule(leaf, modifier_leaves, object_leaves, weapon_leaves)
		if not bool(verdict.get("ok", false)):
			return {"ok": false, "reason": String(verdict.get("reason", "direct summon stats unresolved"))}
		if not groups_by_index.has(create_index):
			groups_by_index[create_index] = {
				"block_index": create_index,
				"pick_count": _spellbook_create_pick_count(create),
				"choices": [],
			}
		var group: Dictionary = groups_by_index[create_index]
		(group["choices"] as Array).append({
				"object_id": object_id,
				"rule": verdict["rule"],
				"lifetime_ticks": int(verdict.get("lifetime_ticks", 0)),
				"lifetime_death_type": String(verdict.get("lifetime_death_type", "")),
			})
	var target_groups: Array = []
	var group_indices: Array = groups_by_index.keys()
	group_indices.sort()
	for group_index in group_indices:
		target_groups.append(groups_by_index[group_index])
	if target_groups.is_empty():
		return {"ok": false, "reason": "direct summon OCL resolves to no live targets"}
	return {"ok": true, "effect": {
		"kind": "summon",
		"hatch_delay_ticks": 0,
		"target_groups": target_groups,
	}}


func _spellbook_create_pick_count(create: Dictionary, multiplier: int = 1) -> int:
	var count := 1
	for field_value in Array(create.get("fields", [])):
		if (
			typeof(field_value) == TYPE_DICTIONARY
			and String((field_value as Dictionary).get("key", "")) == "Count"
		):
			count = maxi(1, int((field_value as Dictionary).get("resolved", 1)))
	return count * maxi(1, multiplier)


func _spellbook_create_enabled(create: Dictionary, owned_upgrades: Dictionary = {}) -> bool:
	## CreateObject rows can be mutually exclusive presentation variants. The
	## default spellbook cast owns no CE graphics upgrades, so a RequiredUpgrades
	## row is not an additional summon while its ForbiddenUpgrades sibling is.
	for field_value in Array(create.get("fields", [])):
		if typeof(field_value) != TYPE_DICTIONARY:
			continue
		var field := field_value as Dictionary
		var key := String(field.get("key", ""))
		var upgrades := String(field.get("value", "")).split(" ", false)
		if key == "RequiredUpgrades":
			for upgrade in upgrades:
				if not owned_upgrades.has(String(upgrade)):
					return false
		elif key == "ForbiddenUpgrades":
			for upgrade in upgrades:
				if owned_upgrades.has(String(upgrade)):
					return false
	return true


func _spellbook_summon_support(spawns: Array, modifier_leaves: Dictionary, object_leaves: Dictionary, ocl_leaves: Dictionary, weapon_leaves: Dictionary) -> Dictionary:
	## Egg powers: the power OCL creates eggs; each egg hatches an OCL of
	## summoned battalions. The full chain must convert: egg hatch, hatch OCL,
	## target horde/member stats, member weapon, locomotion, summon lifetime.
	var hatch_spawn: Dictionary = {}
	for spawn_value in spawns:
		var candidate := spawn_value as Dictionary
		if typeof((candidate.get("leaf", {}) as Dictionary).get("hatch", null)) == TYPE_DICTIONARY:
			hatch_spawn = candidate
			break
	if hatch_spawn.is_empty():
		return {"ok": false, "reason": "summon OCL has no converted hatch leaf"}
	var hatch: Dictionary = (hatch_spawn.get("leaf", {}) as Dictionary).get("hatch", {}) as Dictionary
	var hatch_ocl_id := String(hatch.get("ocl", ""))
	var hatch_ocl: Dictionary = ocl_leaves.get(hatch_ocl_id, {}) as Dictionary
	if hatch_ocl.is_empty():
		return {"ok": false, "reason": "hatch OCL '%s' is not a converted leaf" % hatch_ocl_id}
	var egg_count := 1
	for field_value in Array((hatch_spawn.get("create", {}) as Dictionary).get("fields", [])):
		if typeof(field_value) == TYPE_DICTIONARY and String((field_value as Dictionary).get("key", "")) == "Count":
			egg_count = maxi(1, int((field_value as Dictionary).get("resolved", 1)))
	var hatch_delay_ms := float(hatch.get("destructionDelayMs", 0.0))
	var target_groups: Array = []
	var hatch_creates: Array = hatch_ocl.get("createObjects", []) as Array
	for create_index in range(hatch_creates.size()):
		var create_value: Variant = hatch_creates[create_index]
		if typeof(create_value) != TYPE_DICTIONARY:
			continue
		var create := create_value as Dictionary
		if not _spellbook_create_enabled(create):
			continue
		if not _spellbook_create_enabled(create):
			continue
		var choices: Array = []
		for object_name_value in Array(create.get("objects", [])):
			var object_name := String(object_name_value)
			var target_leaf: Dictionary = object_leaves.get(object_name, {}) as Dictionary
			if target_leaf.is_empty():
				return {"ok": false, "reason": "summon target '%s' is not a converted leaf" % object_name}
			var verdict := _spellbook_summon_rule(target_leaf, modifier_leaves, object_leaves, weapon_leaves)
			if not bool(verdict.get("ok", false)):
				return {"ok": false, "reason": String(verdict.get("reason", "summon stats unresolved"))}
			choices.append({
				"object_id": object_name,
				"rule": verdict["rule"],
				"lifetime_ticks": int(verdict.get("lifetime_ticks", 0)),
				"lifetime_death_type": String(verdict.get("lifetime_death_type", "")),
			})
		if not choices.is_empty():
			target_groups.append({
				"block_index": create_index,
				"pick_count": _spellbook_create_pick_count(create, egg_count),
				"choices": choices,
			})
	if target_groups.is_empty():
		return {"ok": false, "reason": "hatch OCL '%s' spawns no converted targets" % hatch_ocl_id}
	return {"ok": true, "effect": {
		"kind": "summon",
		"hatch_delay_ticks": maxi(0, roundi(hatch_delay_ms / 1000.0 / TICK_SECONDS)),
		"target_groups": target_groups,
	}}


func _spellbook_summon_literal_preview(spawns: Array, object_leaves: Dictionary, ocl_leaves: Dictionary) -> Dictionary:
	## Preserve the exact hatch payload and LifetimeUpdate literals when a live
	## summon remains unsupported for an orthogonal reason (for example, the
	## Watcher's unresolved weapon runtime). This is inspection data only: the
	## original failed verdict remains authoritative and cannot be cast.
	var hatch_spawn: Dictionary = {}
	for spawn_value in spawns:
		var candidate := spawn_value as Dictionary
		if typeof((candidate.get("leaf", {}) as Dictionary).get("hatch", null)) == TYPE_DICTIONARY:
			hatch_spawn = candidate
			break
	if hatch_spawn.is_empty():
		return {"ok": false}
	var hatch: Dictionary = (hatch_spawn.get("leaf", {}) as Dictionary).get("hatch", {}) as Dictionary
	var hatch_ocl: Dictionary = ocl_leaves.get(String(hatch.get("ocl", "")), {}) as Dictionary
	if hatch_ocl.is_empty():
		return {"ok": false}
	var egg_count := _spellbook_create_pick_count(hatch_spawn.get("create", {}) as Dictionary)
	var target_groups: Array = []
	var hatch_creates: Array = hatch_ocl.get("createObjects", []) as Array
	for create_index in range(hatch_creates.size()):
		var create: Dictionary = hatch_creates[create_index] as Dictionary
		if not _spellbook_create_enabled(create):
			continue
		var choices: Array = []
		for object_name_value in Array(create.get("objects", [])):
			var object_name := String(object_name_value)
			var target_leaf: Dictionary = object_leaves.get(object_name, {}) as Dictionary
			if target_leaf.is_empty():
				return {"ok": false}
			var lifetime: Dictionary = target_leaf.get("lifetime", {}) as Dictionary
			if lifetime.is_empty():
				var horde: Dictionary = target_leaf.get("horde", {}) as Dictionary
				var member: Dictionary = object_leaves.get(String(horde.get("memberObject", "")), {}) as Dictionary
				lifetime = member.get("lifetime", {}) as Dictionary
			var lifetime_ms := float(lifetime.get("maxMs", 0.0))
			choices.append({
				"object_id": object_name,
				"rule": {},
				"lifetime_ticks": maxi(0, roundi(lifetime_ms / 1000.0 / TICK_SECONDS)),
				"lifetime_death_type": String(lifetime.get("deathType", "")).to_upper(),
			})
		if not choices.is_empty():
			target_groups.append({
				"block_index": create_index,
				"pick_count": _spellbook_create_pick_count(create, egg_count),
				"choices": choices,
			})
	if target_groups.is_empty():
		return {"ok": false}
	return {"ok": true, "effect": {
		"kind": "summon",
		"hatch_delay_ticks": maxi(0, roundi(float(hatch.get("destructionDelayMs", 0.0)) / 1000.0 / TICK_SECONDS)),
		"target_groups": target_groups,
	}}


func _spellbook_summon_rule(target_leaf: Dictionary, modifier_leaves: Dictionary, object_leaves: Dictionary, weapon_leaves: Dictionary) -> Dictionary:
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
	# Wyrm is intentionally stationary (WyrmLocomotor Speed = 0) but still has
	# a fully authored locomotor and ranged fire-breath runtime.
	if locomotor.is_empty() or speed < 0.0:
		return {"ok": false, "reason": "summoned member '%s' locomotion is not converted" % member_id}
	var weapon_id := String(member.get("weaponId", ""))
	var weapon: Dictionary = weapon_leaves.get(weapon_id, {}) as Dictionary
	var kind_of: Array = member.get("kindOf", []) as Array
	var move_only := kind_of.has("MOVE_ONLY")
	if weapon_id == "" and not move_only:
		# Distinct from "the weapon leaf did not convert": the object authors NO
		# WeaponSet at all. The Watcher is this case - its attack runtime is
		# GrabPassengerSpecialPower + SpecialAbilityUpdate + TransportContain
		# (grab-and-devour), none of which the compiler emits, so reporting a
		# missing weapon leaf would aim a fix at the wrong module.
		var attack_behaviors: Array = []
		for behavior_value in Array(member.get("unconvertedBehaviors", [])):
			var behavior := String(behavior_value)
			if behavior in ["GrabPassengerSpecialPower", "SpecialAbilityUpdate", "TransportContain", "AutoPickUpUpdate"]:
				attack_behaviors.append(behavior)
		if attack_behaviors.is_empty():
			return {"ok": false, "reason": "summoned member '%s' authors no weapon and no unconverted attack runtime" % member_id}
		return {"ok": false, "reason": "summoned member '%s' authors no weapon: its attack runtime is %s, which is not converted" % [member_id, ", ".join(attack_behaviors)]}
	if weapon.is_empty() and not move_only:
		return {"ok": false, "reason": "summoned member '%s' weapon '%s' is not a converted leaf" % [member_id, weapon_id]}
	var nuggets := _spellbook_weapon_damage_nuggets(weapon, weapon_leaves) if not weapon.is_empty() else []
	if nuggets.is_empty() and not move_only:
		return {"ok": false, "reason": "summoned member weapon '%s' has no resolved damage" % weapon_id}
	var attack_range := _spellbook_weapon_field(weapon, "AttackRange") if not weapon.is_empty() else 0.0
	if attack_range <= 0.0 and not move_only:
		return {"ok": false, "reason": "summoned member weapon '%s' range is not converted" % weapon_id}
	var lifetime_ms := 0.0
	var lifetime_death_type := ""
	var lifetime_row: Dictionary = target_leaf.get("lifetime", {}) as Dictionary
	if not lifetime_row.is_empty():
		lifetime_ms = float(lifetime_row.get("maxMs", 0.0))
		lifetime_death_type = String(lifetime_row.get("deathType", ""))
	if lifetime_ms <= 0.0:
		var member_lifetime: Dictionary = member.get("lifetime", {}) as Dictionary
		lifetime_ms = float(member_lifetime.get("maxMs", 0.0))
		lifetime_death_type = String(member_lifetime.get("deathType", ""))
	if lifetime_ms <= 0.0:
		return {"ok": false, "reason": "summon target '%s' is missing converted LifetimeUpdate" % String(target_leaf.get("id", ""))}
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
	var damage_nugget: Dictionary = nuggets[0] if not nuggets.is_empty() else {}
	var delay_ms := _spellbook_weapon_field(weapon, "DelayBetweenShots")
	var clip_reload_ms := _spellbook_weapon_field(weapon, "ClipReloadTime")
	var period_ms := delay_ms if delay_ms > 0.0 else clip_reload_ms
	var pre_attack_ms := _spellbook_weapon_field(weapon, "PreAttackDelay")
	var firing_ms := _spellbook_weapon_field(weapon, "FiringDuration")
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
		"member_damage": 0 if move_only else maxi(1, int(damage_nugget.get("damage", 0))),
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
		"default_weapon_slot": String(member.get("weaponSlot", "")).to_lower(),
		"damage_type": String(damage_nugget.get("damagetype", "")).to_lower(),
		"provenance": {"source": "spellbook-summon", "object_id": String(target_leaf.get("id", ""))},
	}
	var aura_verdict := _spellbook_summon_aura_rules(member, modifier_leaves)
	if not bool(aura_verdict.get("ok", false)):
		return {"ok": false, "reason": String(aura_verdict.get("reason", "summon aura is not converted"))}
	var summon_auras: Array = aura_verdict.get("auras", []) as Array
	var summon_aura_skips: Array = aura_verdict.get("skipped_auras", []) as Array
	if move_only and summon_auras.is_empty() and summon_aura_skips.is_empty():
		return {"ok": false, "reason": "move-only summoned member '%s' has no converted aura payload" % member_id}
	if not summon_auras.is_empty():
		rule["summon_auras"] = summon_auras
	if not summon_aura_skips.is_empty():
		rule["summon_aura_skips"] = summon_aura_skips
	# Carry authored lifecycle policy into the synthesized unit rule. FADED is a
	# lifetime-only removal: combat death still follows the ordinary corpse rule.
	var destroy_die: Array = []
	for policy_value in Array(member.get("destroyDie", [])):
		var policy := policy_value as Dictionary
		destroy_die.append({
			"owner_role": "object",
			"death_types": String(policy.get("deathTypes", "ALL")).to_upper(),
			"excluded_death_types": Array(policy.get("excludedDeathTypes", [])).duplicate(),
			"included_death_types": Array(policy.get("includedDeathTypes", [])).duplicate(),
		})
	if lifetime_death_type.to_upper() == "FADED":
		destroy_die.append({
			"owner_role": "object",
			"death_types": "NONE",
			"excluded_death_types": [],
			"included_death_types": ["FADED"],
		})
	if not destroy_die.is_empty():
		rule["destroy_die"] = destroy_die
	var keep_rows: Array = member.get("keepObjectDie", []) as Array
	if not keep_rows.is_empty():
		var keep := keep_rows[0] as Dictionary
		rule["keep_object_die"] = true
		rule["keep_object_die_policy"] = {
			"death_types": String(keep.get("deathTypes", "ALL")).to_upper(),
			"excluded_death_types": Array(keep.get("excludedDeathTypes", [])).duplicate(),
			"included_death_types": Array(keep.get("includedDeathTypes", [])).duplicate(),
		}
	var permanent_locks: Array[String] = []
	for lock_value in Array(member.get("permanentWeaponLocks", [])):
		if typeof(lock_value) != TYPE_DICTIONARY:
			return {"ok": false, "reason": "summoned member '%s' has a malformed permanent weapon lock" % member_id}
		var lock_row := lock_value as Dictionary
		var slot := String(lock_row.get("slot", "")).to_lower()
		var lock_line: Variant = lock_row.get("line")
		if (
			slot != "primary"
			or String(lock_row.get("state", "")) != "LOCKED_PERMANENTLY"
			or String(lock_row.get("module", "")) != "LockWeaponCreate"
			or String(lock_row.get("sourceIni", "")).strip_edges() == ""
			or typeof(lock_line) not in [TYPE_INT, TYPE_FLOAT]
			or not is_equal_approx(float(lock_line), float(int(lock_line)))
			or int(lock_line) <= 0
			or permanent_locks.has(slot)
			or slot != String(rule.get("default_weapon_slot", ""))
		):
			return {"ok": false, "reason": "summoned member '%s' permanent weapon lock is unsupported" % member_id}
		permanent_locks.append(slot)
	if not permanent_locks.is_empty():
		rule["permanent_weapon_locks"] = permanent_locks
	var creation_leaf: Dictionary = (
		target_leaf
		if target_leaf.has("experienceLevelCreate")
		else member
	)
	var creation_grant: Variant = creation_leaf.get("experienceLevelCreate")
	if creation_grant != null:
		if typeof(creation_grant) != TYPE_DICTIONARY:
			return {"ok": false, "reason": "summoned member '%s' has malformed ExperienceLevelCreate evidence" % member_id}
		var grant := creation_grant as Dictionary
		var grant_rank: Variant = grant.get("rank")
		var grant_line: Variant = grant.get("line")
		if (
			String(grant.get("module", "")) != "ExperienceLevelCreate"
			or grant.get("mpOnly") != false
			or typeof(grant_rank) not in [TYPE_INT, TYPE_FLOAT]
			or not is_equal_approx(float(grant_rank), float(int(grant_rank)))
			or int(grant_rank) < 1
			or String(grant.get("sourceIni", "")).strip_edges() == ""
			or typeof(grant_line) not in [TYPE_INT, TYPE_FLOAT]
			or not is_equal_approx(float(grant_line), float(int(grant_line)))
			or int(grant_line) <= 0
		):
			return {"ok": false, "reason": "summoned member '%s' ExperienceLevelCreate evidence is unsupported" % member_id}
		# JSON numbers enter Godot as floats; the shared adapter deliberately
		# expects source line metadata as an integer. Normalize only the proven
		# integral creation line at this boundary.
		var experience_contract: Dictionary = (creation_leaf.get("experience", {}) as Dictionary).duplicate(true)
		var normalized_grant: Dictionary = (experience_contract.get("experienceLevelCreate", {}) as Dictionary).duplicate(true)
		if not normalized_grant.is_empty():
			normalized_grant["line"] = int(normalized_grant.get("line", 0))
			experience_contract["experienceLevelCreate"] = normalized_grant
		var creation_experience_rule := PlayableUnitAdapter.experience_rule_from_contract(
			experience_contract
		)
		if (
			creation_experience_rule.is_empty()
			or int(creation_experience_rule.get("initial_rank", 0)) != int(grant_rank)
		):
			return {"ok": false, "reason": "summoned member '%s' creation experience chain is unsupported" % member_id}
		var creation_effects: Dictionary = {}
		for level_value in Array(creation_experience_rule.get("levels", [])):
			var level_row := level_value as Dictionary
			if int(level_row.get("rank", 0)) == int(grant_rank):
				creation_effects = level_row
				break
		if (
			creation_effects.is_empty()
			or not Array(creation_effects.get("unsupported_modifiers", [])).is_empty()
			or float(creation_effects.get("production_multiplier", 1.0)) != 1.0
		):
			return {"ok": false, "reason": "summoned member '%s' creation experience effects are unsupported" % member_id}
		rule["creation_experience_rank"] = int(grant_rank)
		rule["creation_experience_effects"] = creation_effects.duplicate(true)
	return {
		"ok": true,
		"rule": rule,
		"lifetime_ticks": maxi(1, roundi(lifetime_ms / 1000.0 / TICK_SECONDS)),
		"lifetime_death_type": lifetime_death_type.to_upper(),
	}


func _spellbook_summon_aura_rules(member: Dictionary, modifier_leaves: Dictionary) -> Dictionary:
	var source_auras: Array = member.get("auras", []) as Array
	if source_auras.is_empty() and typeof(member.get("aura", null)) == TYPE_DICTIONARY:
		source_auras.append(member.get("aura", {}))
	var compiled: Array = []
	var skipped: Array = []
	for aura_value in source_auras:
		var verdict := _spellbook_one_summon_aura_rule(
			member, aura_value as Dictionary, modifier_leaves
		)
		if not bool(verdict.get("ok", false)):
			return verdict
		if bool(verdict.get("skip", false)):
			skipped.append({
				"modifier": String((aura_value as Dictionary).get("modifier", "")),
				"reason": String(verdict.get("reason", "summon-aura-skipped")),
			})
			continue
		compiled.append(verdict.get("aura", {}))
	return {"ok": true, "auras": compiled, "skipped_auras": skipped}


func _spellbook_one_summon_aura_rule(member: Dictionary, aura: Dictionary, modifier_leaves: Dictionary, allow_marker_modifiers: bool = false) -> Dictionary:
	## allow_marker_modifiers: retail authors at least one ModifierList whose stat
	## rows are all commented out and only its Duration survives — PalantirVision
	## (attributemodifier.ini:1139-1146). On a summon that is evidence of an
	## unconverted payload and stays fail-closed; on a reveal ping it is the
	## authored truth (the aura genuinely changes no stat), so the ping lane opts
	## in and the row is kept as a zero-modifier marker.
	var modifier_id := String(aura.get("modifier", ""))
	var modifier: Dictionary = modifier_leaves.get(modifier_id, {}) as Dictionary
	if modifier.is_empty():
		return {"ok": false, "reason": "summon aura modifier '%s' is not a converted leaf" % modifier_id}
	var modifiers: Array = []
	var duration_ms := 0.0
	var category := ""
	for field_value in Array(modifier.get("fields", [])):
		if typeof(field_value) != TYPE_DICTIONARY:
			continue
		var field := field_value as Dictionary
		var key := String(field.get("key", ""))
		var value := String(field.get("value", ""))
		if key == "Category":
			category = value
		elif key == "Duration" and value.is_valid_float():
			duration_ms = float(value)
		elif key == "Modifier":
			var parsed := _parse_modifier_row(value)
			if not parsed.get("ok", false):
				return {"ok": false, "reason": "summon aura modifier '%s' has an unreadable modifier row: %s" % [modifier_id, String(parsed.get("reason", ""))]}
			var kind := String(parsed.get("kind", ""))
			# STRICT lane, same reasoning as the weather modifier above: a summon's
			# aura is the whole reason the summon is worth casting.
			if not bool(parsed.get("supported", false)):
				return {"ok": false, "reason": "summon aura modifier '%s' has a row with no runtime: %s" % [modifier_id, String(parsed.get("reason", ""))]}
			if String(parsed.get("shape", "")) != "percent" or kind not in ["ARMOR", "DAMAGE_MULT", "EXPERIENCE"]:
				return {"ok": false, "reason": "summon aura modifier '%s' requires unsupported '%s' runtime" % [modifier_id, kind]}
			modifiers.append({"kind": kind, "value": float(parsed.get("value", 0.0))})
		elif key == "ModelCondition" and value.strip_edges() != "":
			modifiers.append({"kind": "MODEL_CONDITION", "value": value.strip_edges()})
	var range_source := float(aura.get("range", 0.0))
	var refresh_ms := float(aura.get("refreshDelayMs", 0.0))
	var filter := String(aura.get("objectFilter", ""))
	var modifiers_missing := modifiers.is_empty() and not allow_marker_modifiers
	if category not in ["LEADERSHIP", "SPELL", "DEBUFF"] or modifiers_missing or duration_ms <= 0.0 or range_source <= 0.0 or refresh_ms <= 0.0 or filter == "":
		return {"ok": false, "reason": "summon aura '%s' is incomplete (category=%s modifiers=%d duration_ms=%.1f range=%.1f refresh_ms=%.1f filter=%s)" % [modifier_id, category, modifiers.size(), duration_ms, range_source, refresh_ms, "present" if filter != "" else "missing"]}
	var starts_active := String(aura.get("startsActive", "Yes")).to_lower() != "no"
	var enabled_on_create := starts_active
	if not enabled_on_create:
		var creation_rank := int((member.get("experienceLevelCreate", {}) as Dictionary).get("rank", 0))
		for trigger_value in Array(aura.get("triggeredBy", [])):
			var trigger := String(trigger_value)
			if trigger.begins_with("Upgrade_ObjectLevel"):
				var level_text := trigger.trim_prefix("Upgrade_ObjectLevel")
				if level_text.is_valid_int() and creation_rank >= int(level_text):
					enabled_on_create = true
					break
	if not enabled_on_create:
		return {
			"ok": true,
			"skip": true,
			"reason": "upgrade-gated-aura-inert-at-summon-creation:%s" % modifier_id,
		}
	return {"ok": true, "aura": {
		"id": modifier_id,
		"category": category,
		"modifiers": modifiers,
		"duration_ticks": maxi(1, roundi(duration_ms / (TICK_SECONDS * 1000.0))),
		"refresh_ticks": maxi(1, roundi(refresh_ms / (TICK_SECONDS * 1000.0))),
		"range_source": range_source,
		"filter": filter,
		"target_enemy": String(aura.get("targetEnemy", "")).to_lower() == "yes" if aura.has("targetEnemy") else null,
		"target_allies": String(aura.get("targetAllies", "")).to_lower() == "yes" if aura.has("targetAllies") else null,
		"starts_active": starts_active,
		"triggered_by": Array(aura.get("triggeredBy", [])).duplicate(),
	}}


func _spellbook_grove_chain(references: Dictionary, object_leaves: Dictionary, ocl_leaves: Dictionary) -> Dictionary:
	## Resolve Elven Wood's authored geometry chain, or return {} for "none".
	##
	## Retail plants the grove through an ObjectCreationList, not through the
	## ElvenGrove object (which authors `Model = None` and is pure particle):
	## OCL_ElvenWoodSeed creates N ElvenWoodTreeSeed at an authored spread
	## radius, each seed's SlowDeath hatches OCL_ElvenWoodTree -> ElvenWoodTree,
	## which itself hatches OCL_ElvenWoodTreeSpawn -> ElvenWoodTreeOpt (the tree
	## that stays). Every number below is read off that chain; nothing is
	## assumed, and an unresolvable link simply yields no trees rather than an
	## invented grove.
	var ocl_ids: Array = references.get("objectCreationLists", []) as Array
	if ocl_ids.is_empty():
		return {}
	var ocl: Dictionary = ocl_leaves.get(String(ocl_ids[0]), {}) as Dictionary
	if ocl.is_empty():
		return {}
	var creates: Array = ocl.get("createObjects", []) as Array
	if creates.is_empty() or typeof(creates[0]) != TYPE_DICTIONARY:
		return {}
	var create := creates[0] as Dictionary
	var names: Array = create.get("objects", []) as Array
	if names.is_empty():
		return {}
	var count := 1
	var min_radius := 0.0
	var max_radius := 0.0
	for field_value in Array(create.get("fields", [])):
		if typeof(field_value) != TYPE_DICTIONARY:
			continue
		var field_row := field_value as Dictionary
		match String(field_row.get("key", "")):
			"Count":
				count = maxi(1, int(field_row.get("resolved", 1)))
			"MinDistanceAFormation":
				min_radius = float(field_row.get("resolved", 0.0))
			"MaxDistanceFormation":
				max_radius = float(field_row.get("resolved", 0.0))
	# Follow every authored hatch to the object that actually remains standing.
	var object_id := String(names[0])
	var guard := 0
	while guard < 4:
		guard += 1
		var leaf: Dictionary = object_leaves.get(object_id, {}) as Dictionary
		if leaf.is_empty():
			return {}
		var hatch: Dictionary = leaf.get("hatch", {}) as Dictionary
		var hatch_ocl_id := String(hatch.get("ocl", ""))
		if hatch_ocl_id == "":
			break
		var hatch_ocl: Dictionary = ocl_leaves.get(hatch_ocl_id, {}) as Dictionary
		var hatch_creates: Array = hatch_ocl.get("createObjects", []) as Array
		if hatch_creates.is_empty() or typeof(hatch_creates[0]) != TYPE_DICTIONARY:
			break
		var hatch_names: Array = (hatch_creates[0] as Dictionary).get("objects", []) as Array
		if hatch_names.is_empty():
			break
		object_id = String(hatch_names[0])
	if object_id == "" or not object_leaves.has(object_id):
		return {}
	if max_radius <= 0.0:
		max_radius = min_radius
	return {
		"object_id": object_id,
		"count": count,
		"min_radius_source": min_radius,
		"max_radius_source": max_radius,
	}


func _spellbook_grove_support(field_values: Dictionary, field_resolved: Dictionary, references: Dictionary, modifier_leaves: Dictionary, object_leaves: Dictionary, ocl_leaves: Dictionary = {}, object_field: String = "ElvenGroveObject") -> Dictionary:
	## Terrain-taint family (Elven Wood, Taint, Isengard Taint): the converted
	## grove/taint-land leaf carries the whole effect — modifier leaf, refresh,
	## range, filter, the authored terrain cell type, and the object lifetime.
	var grove_id := String(field_values.get(object_field, ""))
	var grove: Dictionary = object_leaves.get(grove_id, {}) as Dictionary
	if grove.is_empty():
		return {"ok": false, "reason": "terrain-taint object '%s' is not a converted leaf" % grove_id}
	var aura: Dictionary = grove.get("aura", {}) as Dictionary
	var deletion: Dictionary = grove.get("deletion", {}) as Dictionary
	if aura.is_empty() or deletion.is_empty():
		return {"ok": false, "reason": "terrain-taint object '%s' aura or lifetime is not converted" % grove_id}
	var modifier_id := String(aura.get("modifier", ""))
	var modifier: Dictionary = modifier_leaves.get(modifier_id, {}) as Dictionary
	if modifier.is_empty():
		return {"ok": false, "reason": "grove aura modifier '%s' is not a converted leaf" % modifier_id}
	# ROW ABSENT is not ROW UNREADABLE. A modifier list that never authors a
	# DAMAGE_MULT row means the neutral 1.0 (BFME2 `ModifierList
	# GenericArmorLeadership` is `ARMOR 50%` and nothing else,
	# attributemodifier.ini:159-166, and it is what the BFME2 ElvenGrove actually
	# binds: grove.ini:31 `BonusName = GenericArmorLeadership`). A row that IS
	# authored and cannot be read is still fail-closed, because that is the case
	# where half an authored buff goes missing in silence.
	var armor_mult := 1.0
	var damage_mult := 1.0
	var saw_armor := false
	var saw_damage := false
	var buff_duration_ms := 0.0
	# READABLE BUT UNMATCHED KIND. A row this resolver parses cleanly and whose
	# shape is a plain percent, but whose KIND is neither ARMOR nor DAMAGE_MULT
	# (retail authors e.g. `EXPERIENCE 150%` on buff leaves), used to fall
	# straight through the `match` below and vanish. That is the same silent drop
	# the SpecialPowerModule lane was fixed for in round 18 — it just took a
	# different route to it, so the fix did not reach here. Named and counted on
	# the effect instead, in the SAME shape that lane uses.
	var unsupported_rows: Array = []
	for field_value in Array(modifier.get("fields", [])):
		if typeof(field_value) != TYPE_DICTIONARY:
			continue
		var field_row := field_value as Dictionary
		if String(field_row.get("key", "")) == "Modifier":
			# FAIL-CLOSED, not `continue`: a row this resolver cannot read is a row
			# whose buff would silently go missing, and the grove would still plant.
			var parsed := _parse_modifier_row(String(field_row.get("value", "")))
			if (
				not parsed.get("ok", false)
				or not bool(parsed.get("supported", false))
				or String(parsed.get("shape", "")) != "percent"
			):
				return {"ok": false, "reason": "grove aura modifier '%s' has an unsupported modifier row '%s' (%s)" % [
					modifier_id, String(field_row.get("value", "")),
					String(parsed.get("reason", "shape is not a plain percent")),
				]}
			var percent := float(parsed.get("value", 0.0))
			match String(parsed.get("kind", "")):
				"ARMOR":
					armor_mult = percent
					saw_armor = true
				"DAMAGE_MULT":
					damage_mult = percent
					saw_damage = true
				_:
					unsupported_rows.append({
						"row": String(field_row.get("value", "")),
						"shape": String(parsed.get("shape", "")),
						"reason": "kind '%s' has no grove-aura runtime here" % String(parsed.get("kind", "")),
					})
		elif String(field_row.get("key", "")) == "Duration":
			buff_duration_ms = float(field_row.get("resolved", field_row.get("value", 0.0)))
	var aura_range := float(aura.get("range", 0.0))
	var lifetime_ms := float(deletion.get("maxMs", 0.0))
	var filter := String(aura.get("objectFilter", ""))
	# AT LEAST ONE stat row is required, not both. Round 16 required both and it
	# was wrong twice over: it locked men/SpellBookElvenWoodMP, whose authored
	# leaf (GenericArmorLeadership) legitimately carries only ARMOR 50%, and it
	# conflated "absent" with "unreadable" — the case the round-16 note was
	# actually written about (a row that IS authored and cannot be parsed) is
	# still fail-closed, in the loop above. A leaf with NO readable stat row at
	# all still fails here: that is a grove with nothing to apply.
	if not (saw_armor or saw_damage) or armor_mult <= 0.0 or damage_mult <= 0.0 or buff_duration_ms <= 0.0 or aura_range <= 0.0 or lifetime_ms <= 0.0:
		return {"ok": false, "reason": "grove aura modifier '%s' carries no readable stat row (ARMOR seen=%s %.3f, DAMAGE_MULT seen=%s %.3f), range, or lifetime is not converted" % [
			modifier_id, saw_armor, armor_mult, saw_damage, damage_mult,
		]}
	if filter == "" or (not filter.contains("ANY") and not filter.contains("+")):
		return {"ok": false, "reason": "grove aura object filter is not converted"}
	return {"ok": true, "effect": {
		"kind": "grove_aura",
		"armor_mult": armor_mult,
		# Usually authored alongside ARMOR on the same modifier leaf (RotWK
		# GenericBuff: ARMOR 50% + DAMAGE_MULT 150%). When the leaf authors no
		# DAMAGE_MULT row at all (BFME2 GenericArmorLeadership) this stays at the
		# neutral 1.0, which is what "absent" means — a row that is present and
		# unreadable never reaches here.
		"damage_mult": damage_mult,
		# Which rows the leaf actually authored, so a consumer can tell an
		# authored-neutral 1.0 from a defaulted one.
		"authored_rows": {"ARMOR": saw_armor, "DAMAGE_MULT": saw_damage},
		# Named residual rows carried onto the effect so the runner and the report
		# can COUNT them instead of losing them — same key and same shape as the
		# SpecialPowerModule lane's.
		"unsupported_modifier_rows": unsupported_rows,
		"buff_duration_ticks": maxi(1, roundi(buff_duration_ms / 1000.0 / TICK_SECONDS)),
		"range_source": aura_range,
		"lifetime_ticks": maxi(1, roundi(lifetime_ms / 1000.0 / TICK_SECONDS)),
		"filter": filter,
		"modifier": modifier_id,
		"terrain_object_id": grove_id,
		# Retail's RequiredConditions cell type (TAINT / ELVEN_WOOD). Presentation
		# reads this to pick the ground decal; "" when the leaf does not author it.
		"terrain_condition": String(aura.get("requiredConditions", "")),
		# Presentation-only: the authored tree chain behind the grove. Absent
		# when the chain does not fully convert, and then no geometry is placed.
		"trees": _spellbook_grove_chain(references, object_leaves, ocl_leaves),
	}}


func _spellbook_field_ping_support(spawns: Array, modifier_leaves: Dictionary) -> Dictionary:
	## Reveal/field family (Farsight, Palantir Vision, Frozen Land, and the
	## Enshrouding Mist gap). Retail spawns a "ping": an object that is not a unit
	## at all — IMMOBILE, UNATTACKABLE, no weapon, no hatch — whose entire runtime
	## is a bounded VisionRange reveal plus optional AttributeModifierAuraUpdate
	## rows, ended by its authored LifetimeUpdate
	## (data/ini/object/system/system.ini:1905-1955, :1997-2062;
	## FrozenLandPing lifetime FROZEN_LAND_EFFECT_DURATION = gamedata.ini:3595).
	##
	## Returns {} when the spawn is not this shape, so the caller falls through to
	## the summon/structure resolvers unchanged.
	if spawns.is_empty():
		return {}
	var leaf: Dictionary = (spawns[0] as Dictionary).get("leaf", {}) as Dictionary
	if leaf.is_empty():
		return {}
	var kind_of: Array = leaf.get("kindOf", []) as Array
	var lifetime: Dictionary = leaf.get("lifetime", {}) as Dictionary
	var auras: Array = leaf.get("auras", []) as Array
	if auras.is_empty() and typeof(leaf.get("aura", null)) == TYPE_DICTIONARY:
		auras = [leaf.get("aura", {})]
	var vision_range := float(leaf.get("visionRange", 0.0))
	var is_ping := (
		kind_of.has("IMMOBILE")
		and kind_of.has("UNATTACKABLE")
		and (leaf.get("locomotor", {}) as Dictionary).is_empty()
		and Array(leaf.get("fireWeapons", [])).is_empty()
		and String(leaf.get("weaponId", "")) == ""
		and typeof(leaf.get("hatch", null)) != TYPE_DICTIONARY
		and (leaf.get("horde", {}) as Dictionary).is_empty()
		and float(lifetime.get("maxMs", 0.0)) > 0.0
		and (vision_range > 0.0 or not auras.is_empty())
	)
	if not is_ping:
		return {}
	var object_id := String(leaf.get("id", ""))
	var unconverted: Array = Array(leaf.get("unconvertedBehaviors", [])).duplicate()
	if unconverted.has("InvisibilityUpdate") or not Array(leaf.get("invisibilityUpdates", [])).is_empty():
		# Enshrouding Mist. The mist's headline effect IS the camouflage broadcast
		# (system.ini:2020-2029: InvisibilityNugget CAMOUFLAGE, DetectionRange
		# ELVEN_MIST_CAMOUFLAGE_DETECTION_RANGE, Broadcast Yes, BroadcastRange
		# ENSHROUDING_MIST_EFFECT_RADIUS, BroadcastObjectFilter
		# ELVEN_MIST_OBJECT_FILTER). The 2026-08-05 cook converts all of it into
		# the leaf's invisibilityUpdates rows, but NOTHING in this sim consumes
		# that data yet, so the lock keys on the data's presence rather than the
		# old unconverted marker: unlocking on marker disappearance alone would
		# ship a "concealment" power whose only live effect is its secondary
		# debuff aura. Unlock by consuming invisibilityUpdates, not by editing
		# this check.
		return {"ok": false, "reason": "ping '%s' camouflage broadcast is converted but not consumed: the sim does not yet apply invisibilityUpdates (CAMOUFLAGE nugget, detection range, broadcast range and filter ship in the pack unread)" % object_id}
	var compiled_auras: Array = []
	var effect_auras := 0
	for aura_value in auras:
		var verdict := _spellbook_one_summon_aura_rule(
			leaf, aura_value as Dictionary, modifier_leaves, true
		)
		if not bool(verdict.get("ok", false)):
			return {"ok": false, "reason": "ping '%s' aura is not converted: %s" % [object_id, String(verdict.get("reason", ""))]}
		if bool(verdict.get("skip", false)):
			continue
		var aura: Dictionary = verdict.get("aura", {}) as Dictionary
		compiled_auras.append(aura)
		if not Array(aura.get("modifiers", [])).is_empty():
			effect_auras += 1
	var reveal_source := vision_range
	if reveal_source <= 0.0 and effect_auras == 0:
		return {"ok": false, "reason": "ping '%s' has neither a converted VisionRange reveal nor a stat-bearing aura" % object_id}
	var radius_source := reveal_source
	for aura_value in compiled_auras:
		radius_source = maxf(radius_source, float((aura_value as Dictionary).get("range_source", 0.0)))
	return {"ok": true, "effect": {
		"kind": "field_ping",
		"object_id": object_id,
		"lifetime_ticks": maxi(1, roundi(float(lifetime.get("maxMs", 0.0)) / 1000.0 / TICK_SECONDS)),
		"reveal_radius_source": reveal_source,
		"radius_source": radius_source,
		"auras": compiled_auras,
		# Named residual gaps carried onto the effect so the runner and the report
		# can count them instead of losing them (StealthDetectorUpdate on the
		# Palantir/Farsight base object: this reveal does NOT unmask stealth).
		"unconverted_behaviors": unconverted,
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
	var sciences: Dictionary = _team_tree(team).get("sciences", {}) as Dictionary
	var science_row: Dictionary = sciences.get(science_id, {}) as Dictionary
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
	var tree := _team_tree(team)
	if not bool(tree.get("ready", false)):
		return {"ok": false, "reason": "spellbook-unavailable"}
	var row: Dictionary = (tree.get("powers", {}) as Dictionary).get(power_id, {}) as Dictionary
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
	var row: Dictionary = (_team_tree(team).get("powers", {}) as Dictionary)[power_id]
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
	if not bool(_team_tree(team).get("ready", false)):
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
	if not bool(_team_tree(team).get("ready", false)):
		return {"ok": false, "reason": "spellbook-unavailable"}
	_staged_purchases[team] = []
	return {"ok": true, "reason": ""}


func power_cooldown_state(team: int, power_id: String) -> Dictionary:
	var row: Dictionary = (_team_tree(team).get("powers", {}) as Dictionary).get(power_id, {}) as Dictionary
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
	var tree := _team_tree(team)
	var tree_powers: Dictionary = tree.get("powers", {}) as Dictionary
	var tree_sciences: Dictionary = tree.get("sciences", {}) as Dictionary
	var tree_intrinsic: Array = tree.get("intrinsic", []) as Array
	var tree_science_to_power: Dictionary = tree.get("science_to_power", {}) as Dictionary
	var powers: Dictionary = {}
	for power_id in (tree.get("order", []) as Array):
		var row: Dictionary = tree_powers[power_id]
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
		var science_row: Dictionary = tree_sciences.get(String(row.get("science_id", "")), {}) as Dictionary
		for group_value in Array(science_row.get("groups", [])):
			# Only groups this faction can ever complete draw a palantir fork:
			# every member must be intrinsic-owned or a tree science (groups
			# naming another faction's root, e.g. SCIENCE_DWARVES, are dead
			# paths the retail orb does not draw).
			var achievable := true
			for member in group_value as Array:
				var member_id := String(member)
				if not tree_intrinsic.has(member_id) and not tree_science_to_power.has(member_id):
					achievable = false
					break
			if not achievable:
				continue
			for member in group_value as Array:
				var prereq_power_id := String(tree_science_to_power.get(String(member), ""))
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
	# Creeps are excluded from the spellbook economy: a creep kill never banks
	# power points for the creep owner. Rostered killers of creeps still earn.
	if not _is_combatant_team(team):
		return
	var kills_per_point := maxi(1, int(_rules.get("power_point_kills", POWER_POINT_KILLS)))
	_kills_toward_power_point[team] = int(_kills_toward_power_point.get(team, 0)) + 1
	if int(_kills_toward_power_point[team]) >= kills_per_point:
		_kills_toward_power_point[team] = 0
		team_power_points[team] = power_points(team) + 1
		_emit_event("power.point_earned", 0, 0, {"team": team, "points": power_points(team)})


func cast_power(team: int, power_id: String, point: Vector2) -> Dictionary:
	var tree := _team_tree(team)
	if not bool(tree.get("ready", false)):
		return {"ok": false, "reason": "spellbook-unavailable"}
	var row: Dictionary = (tree.get("powers", {}) as Dictionary).get(power_id, {}) as Dictionary
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
		"field_ping":
			result = _cast_spellbook_field_ping(team, power_id, effect, point)
		"cloudbreak_stun":
			result = _cast_spellbook_cloudbreak(team, effect, point)
		"weather_modifier":
			result = _cast_spellbook_weather_modifier(team, power_id, effect)
		"weather_anticategory":
			result = _cast_spellbook_weather_anticategory(team, power_id, effect)
		"creep_allegiance":
			result = _cast_spellbook_creep_allegiance(team, effect, point)
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
		# Map-scaled twin of radius_source so the presentation cue can cover the
		# ground the power actually affected without re-deriving the scale.
		"fx_radius": snappedf(
			float(effect.get("radius_source", effect.get("range_source", 0.0))) * _spellbook_world_scale(),
			0.001
		),
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
		elif term == "ALL":
			# `ALL` is the universal set ONLY when the filter authors no including
			# kind term of its own — retail's `ALL ENEMIES` (Freezing Rain) is a
			# relation-only filter and means everyone. Every OTHER `ALL` in this
			# corpus sits beside kind terms, where the filter reads conjunctively;
			# a blanket include there would silently widen it to the whole board.
			if not _spellbook_filter_has_kind_terms(filter_text):
				included = true
		elif kinds.has(term):
			included = true
	return included


func _spellbook_filter_has_kind_terms(filter_text: String) -> bool:
	## True when the filter names at least one INCLUDING object-kind term.
	## Relation words, NONE, the universal words, and `-` exclusions do not
	## count: `ALL -WildBabyDrake ENEMIES` is still a relation-only filter with
	## one carve-out, so `ALL` there is still universal.
	for term_value in filter_text.split(" ", false):
		var term := String(term_value)
		if term in ["", "NONE", "ANY", "ALL", "ALLIES", "ENEMIES"] or term.begins_with("-"):
			continue
		return true
	return false

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
	# DOES THIS PAYLOAD ACTUALLY CARRY A COMBAT BUFF? `damage_mult` defaults to a
	# neutral 1.0 when the leaf authors no DAMAGE_MULT row, so a PRODUCTION-only
	# power (Industry, Dwarven Riches, Blight, Snowbind) reached the writes below
	# with 1.0 and STOMPED whatever rally the target already had.
	#
	# MEASURED, and it is a real cancel rather than a no-op: in the spellbook
	# matrix, casting mordor/SpellBookIndustry moved an ally from
	# rally_until_tick 3601 / rally_damage_mult 1.5 to 3001 / 1.0 — a live 150%
	# rally buff ended early by a power whose entire authored payload is an
	# economy multiplier (.private/scratch/opus29-spellbook-A.out.log).
	#
	# `authored_rows` is what makes this checkable rather than guessable: it
	# distinguishes a leaf that authored DAMAGE_MULT 100% (a deliberate neutral
	# buff, which still refreshes a window) from one that authored no DAMAGE_MULT
	# row at all. Only the latter is skipped.
	var authored_rows: Dictionary = effect.get("authored_rows", {}) as Dictionary
	# Absent table = an effect compiled before authored_rows existed; fall back to
	# the old unconditional behaviour rather than silently dropping a real buff.
	var carries_rally: bool = (
		not authored_rows.has("DAMAGE_MULT") or bool(authored_rows.get("DAMAGE_MULT", false))
	)
	var rallied := 0
	for id in living_ids(team):
		var row: Dictionary = entities[id]
		if Vector2(row.get("position", Vector2.ZERO)).distance_to(point) > range_sim:
			continue
		# Aura-style filters (GENERIC_BUFF_RECIPIENT_OBJECT_FILTER) exclude the
		# HORDE container but include its members; evaluate member kinds so the
		# battalion-as-container distinction does not reject infantry hordes.
		if not _spellbook_member_affects(
			row, String(effect.get("affects", "")), true
		):
			continue
		if carries_rally:
			row["rally_until_tick"] = tick_index + duration_ticks
			row["rally_damage_mult"] = damage_mult
		# Counted either way: the filter matched, so the cast found its targets
		# and succeeds. A production-only power stays castable-but-inert, which is
		# exactly what the spellbook matrix documents it as — it simply no longer
		# cancels an unrelated buff on its way to doing nothing.
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
	## whose stats and summon lifetime all come from the converted leaves. Pool
	## choices are made now, once per cast; configuration consumes no RNG bytes.
	var targets: Array = effect.get("targets", []) as Array
	if effect.has("target_groups"):
		targets = _spellbook_resolve_summon_targets(
			Array(effect.get("target_groups", []))
		)
	if targets.is_empty():
		return {"ok": false, "reason": "no-summon-targets"}
	_pending_power_effects.append({
		"kind": "summon",
		"fire_tick": tick_index + int(effect.get("hatch_delay_ticks", 0)),
		"team": team,
		"point": point,
		"targets": targets,
	})
	var queued_count := _terminal_visible_summon_count(targets)
	return {"ok": true, "reason": "", "battalions": 0, "summon_count": queued_count}


func _terminal_visible_summon_count(targets: Array) -> int:
	## The effect compiler has already followed converted egg/hatch chains, so
	## these rows are the terminal battalions/entities that will materialize.
	## Never count intermediary OCL payload rows or horde members as HUD units.
	var count := 0
	for target_value in targets:
		if typeof(target_value) != TYPE_DICTIONARY:
			continue
		var target := target_value as Dictionary
		if (target.get("rule", {}) as Dictionary).is_empty():
			continue
		count += maxi(1, int(target.get("count", 1)))
	return count


func _spellbook_resolve_summon_targets(target_groups: Array) -> Array:
	var resolved: Array = []
	for group_value in target_groups:
		if typeof(group_value) != TYPE_DICTIONARY:
			continue
		var group := group_value as Dictionary
		var choices: Array = group.get("choices", []) as Array
		if choices.is_empty():
			continue
		var counts: Dictionary = {}
		for _pick in range(maxi(0, int(group.get("pick_count", 0)))):
			var choice_index := 0
			if choices.size() > 1:
				choice_index = logic_random_int(0, choices.size() - 1)
			var choice := choices[choice_index] as Dictionary
			var object_id := String(choice.get("object_id", ""))
			counts[object_id] = int(counts.get(object_id, 0)) + 1
		for choice_value in choices:
			var choice := choice_value as Dictionary
			var object_id := String(choice.get("object_id", ""))
			var count := int(counts.get(object_id, 0))
			if count <= 0:
				continue
			var target := choice.duplicate(true)
			target["count"] = count
			resolved.append(target)
	return resolved


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
	_apply_structure_inherit_upgrades(structures[structure_id] as Dictionary)
	_initialize_structure_auto_deposit(structures[structure_id] as Dictionary)
	_emit_event("power.structure_summon", 0, structure_id, {"team": team, "object_id": String(effect.get("object_id", "")), "build_ticks": build_ticks, "health": health})
	return {"ok": true, "reason": "", "battalions": 0}


func _cast_spellbook_grove(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	_active_groves.append({
		"team": team,
		"point": point,
		"range_sim": float(effect.get("range_source", 0.0)) * _spellbook_world_scale(),
		"armor_mult": float(effect.get("armor_mult", 1.0)),
		"damage_mult": float(effect.get("damage_mult", 1.0)),
		"buff_duration_ticks": int(effect.get("buff_duration_ticks", 1)),
		"despawn_tick": tick_index + int(effect.get("lifetime_ticks", 1)),
		"filter": String(effect.get("filter", "")),
		"terrain_condition": String(effect.get("terrain_condition", "")),
		# The AUTHORED leaf id, so the accumulator key below is per-modifier-list.
		"modifier": String(effect.get("modifier", "")),
	})
	var grove_trees: Dictionary = effect.get("trees", {}) as Dictionary
	_emit_event("power.grove", 0, 0, {
		"team": team,
		"point": [snappedf(point.x, 0.001), snappedf(point.y, 0.001)],
		"lifetime_ticks": int(effect.get("lifetime_ticks", 0)),
		# Retail's terrain cell type (TAINT / ELVEN_WOOD) the ground is painted
		# with for the object's lifetime; "" when the leaf does not author one.
		"terrain_condition": String(effect.get("terrain_condition", "")),
		"terrain_object_id": String(effect.get("terrain_object_id", "")),
		"radius_source": float(effect.get("range_source", 0.0)),
		# The presenter plants the authored tree object; an empty block means
		# the chain did not convert and nothing is drawn.
		"trees": grove_trees.duplicate(true),
	})
	return {"ok": true, "reason": "", "battalions": 0}


func _cast_spellbook_field_ping(team: int, power_id: String, effect: Dictionary, point: Vector2) -> Dictionary:
	## Drop the authored ping at the cast point. It has no body the sim can shoot
	## and never moves, so it lives in its own registry rather than as an entity:
	## a bounded reveal region plus the authored auras, expiring on the leaf's
	## LifetimeUpdate. Deterministic — no RNG, no wall clock.
	var scale := _spellbook_world_scale()
	var reveal_source := float(effect.get("reveal_radius_source", 0.0))
	_field_pings.append({
		"team": team,
		"power_id": power_id,
		"object_id": String(effect.get("object_id", "")),
		"point": point,
		"reveal_radius_source": reveal_source,
		"reveal_radius_sim": reveal_source * scale,
		"expire_tick": tick_index + int(effect.get("lifetime_ticks", 1)),
		"auras": (effect.get("auras", []) as Array).duplicate(true),
	})
	_emit_event("power.field_ping", 0, 0, {
		"team": team,
		"power_id": power_id,
		"object_id": String(effect.get("object_id", "")),
		"point": [snappedf(point.x, 0.001), snappedf(point.y, 0.001)],
		"reveal_radius_source": reveal_source,
		"reveal_radius": snappedf(reveal_source * scale, 0.001),
		"lifetime_ticks": int(effect.get("lifetime_ticks", 0)),
		"auras": (effect.get("auras", []) as Array).size(),
	})
	return {"ok": true, "reason": "", "battalions": 0}


func team_revealed_regions(team: int) -> Array:
	## Per-team shroud-reveal registry the presentation consumes. The sim has no
	## fog-of-war model of its own, so this is the whole reveal contract: every
	## live ping owned by `team` that authors a VisionRange, in cast order, with
	## the tick it lapses. Presentation work still outstanding: drawing the
	## reveal (no fog layer exists to lift) and StealthDetectorUpdate unmasking,
	## which is a converter gap named on the effect.
	var regions: Array = []
	for ping in _field_pings:
		if int(ping.get("team", -1)) != team:
			continue
		if float(ping.get("reveal_radius_source", 0.0)) <= 0.0:
			continue
		regions.append({
			"point": Vector2(ping.get("point", Vector2.ZERO)),
			"radius_sim": float(ping.get("reveal_radius_sim", 0.0)),
			"radius_source": float(ping.get("reveal_radius_source", 0.0)),
			"expire_tick": int(ping.get("expire_tick", -1)),
			"power_id": String(ping.get("power_id", "")),
			"object_id": String(ping.get("object_id", "")),
		})
	return regions


func field_ping_count() -> int:
	return _field_pings.size()


func _step_field_pings() -> void:
	## Expire lapsed pings, then refresh each live ping's authored auras on its
	## own cadence. Iteration follows cast order and the spatial gather is
	## already sorted, so the pass is lockstep-deterministic.
	if _field_pings.is_empty():
		return
	var living: Array[Dictionary] = []
	for ping in _field_pings:
		if tick_index >= int(ping.get("expire_tick", -1)):
			continue
		living.append(ping)
		var team := int(ping.get("team", -1))
		var origin := Vector2(ping.get("point", Vector2.ZERO))
		for aura_value in Array(ping.get("auras", [])):
			var aura := aura_value as Dictionary
			if Array(aura.get("modifiers", [])).is_empty():
				# Authored marker aura (PalantirVision): no stat rows in retail,
				# so nothing is applied. Kept on the effect as evidence, not
				# silently invented into a buff.
				continue
			if tick_index % maxi(1, int(aura.get("refresh_ticks", 1))) != 0:
				continue
			var radius := float(aura.get("range_source", 0.0)) * _spellbook_world_scale()
			for target_id in _spatial_gather_sorted(origin, radius):
				if not entities.has(target_id):
					continue
				var target: Dictionary = entities[target_id]
				if int(target.get("health", 0)) <= 0:
					continue
				var same_team := int(target.get("team", -1)) == team
				if Vector2(target.get("position", Vector2.ZERO)).distance_to(origin) > radius:
					continue
				if not _summon_aura_allows_relation(aura, same_team):
					continue
				if not _spellbook_member_affects(
					target, String(aura.get("filter", "")), same_team
				):
					continue
				_set_timed_modifier(
					target,
					"field-ping:%s:%s" % [String(ping.get("object_id", "")), String(aura.get("id", ""))],
					Array(aura.get("modifiers", [])),
					tick_index + int(aura.get("duration_ticks", 1))
				)
	_field_pings = living


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


func _cast_spellbook_weather_modifier(team: int, power_id: String, effect: Dictionary) -> Dictionary:
	## Darkness. Global, no cast point: the whole map is under the weather for
	## WeatherDuration, and every unit the authored filter accepts carries the
	## modifier leaf's rows for exactly that window.
	var expire_tick := tick_index + int(effect.get("duration_ticks", 1))
	var entry := {
		"kind": "weather_modifier",
		"team": team,
		"power_id": power_id,
		"modifier_id": String(effect.get("modifier_id", "")),
		"modifiers": (effect.get("modifiers", []) as Array).duplicate(true),
		"affects": String(effect.get("affects", "")),
		"weather": String(effect.get("weather", "")),
		"expire_tick": expire_tick,
	}
	_weather_effects.append(entry)
	var affected := _apply_weather_modifier(entry)
	_emit_event("power.weather", 0, 0, {
		"team": team,
		"power_id": power_id,
		"kind": "weather_modifier",
		"weather": String(effect.get("weather", "")),
		"modifier_id": String(effect.get("modifier_id", "")),
		"duration_ticks": int(effect.get("duration_ticks", 0)),
		"expire_tick": expire_tick,
		"affected": affected,
	})
	return {"ok": true, "reason": "", "battalions": affected}


func _cast_spellbook_weather_anticategory(team: int, power_id: String, effect: Dictionary) -> Dictionary:
	## Freezing Rain. Global anti-LEADERSHIP: every enemy the authored filter
	## accepts loses its leadership grants for the weather window, reusing the
	## same suppression field the Horn of Gondor strip writes.
	var expire_tick := tick_index + int(effect.get("duration_ticks", 1))
	var entry := {
		"kind": "weather_anticategory",
		"team": team,
		"power_id": power_id,
		"anti_category": String(effect.get("anti_category", "")),
		"affects": String(effect.get("affects", "")),
		"weather": String(effect.get("weather", "")),
		"expire_tick": expire_tick,
	}
	_weather_effects.append(entry)
	var affected := _apply_weather_anticategory(entry)
	_emit_event("power.weather", 0, 0, {
		"team": team,
		"power_id": power_id,
		"kind": "weather_anticategory",
		"weather": String(effect.get("weather", "")),
		"anti_category": String(effect.get("anti_category", "")),
		"duration_ticks": int(effect.get("duration_ticks", 0)),
		"expire_tick": expire_tick,
		"affected": affected,
		# Named, not dropped: the fire half of the power has no sim model.
		"unconverted_behaviors": (effect.get("unconverted_behaviors", []) as Array).duplicate(),
	})
	return {"ok": true, "reason": "", "battalions": affected}


func _apply_weather_modifier(entry: Dictionary) -> int:
	var team := int(entry.get("team", -1))
	var affects := String(entry.get("affects", ""))
	var key := "weather:%s" % String(entry.get("modifier_id", ""))
	var expire_tick := int(entry.get("expire_tick", -1))
	var modifiers: Array = entry.get("modifiers", []) as Array
	var affected := 0
	for id in entity_ids():
		var row: Dictionary = entities[id]
		if int(row.get("health", 0)) <= 0:
			continue
		if not _spellbook_member_affects(row, affects, int(row.get("team", -1)) == team):
			continue
		_set_timed_modifier(row, key, modifiers, expire_tick)
		affected += 1
	return affected


func _apply_weather_anticategory(entry: Dictionary) -> int:
	var team := int(entry.get("team", -1))
	var affects := String(entry.get("affects", ""))
	var expire_tick := int(entry.get("expire_tick", -1))
	var affected := 0
	for id in entity_ids():
		var row: Dictionary = entities[id]
		if int(row.get("health", 0)) <= 0:
			continue
		if not _spellbook_member_affects(row, affects, int(row.get("team", -1)) == team):
			continue
		# EXTEND, never clobber: a second, shorter cast must not cut a longer
		# suppression short. Retail stacks these as overlapping windows.
		row["leadership_suppressed_until_tick"] = maxi(
			expire_tick, int(row.get("leadership_suppressed_until_tick", -1))
		)
		affected += 1
	return affected


func _step_weather_effects() -> void:
	## Expire lapsed weather windows, then re-apply the live ones on the shared
	## aura cadence so units created or converted mid-window are covered - which
	## is what AttributeModifierWeatherBased = Yes means. Deterministic:
	## ascending entity order, fixed cadence, no RNG and no wall clock.
	if _weather_effects.is_empty():
		return
	var living: Array[Dictionary] = []
	for entry in _weather_effects:
		if tick_index >= int(entry.get("expire_tick", -1)):
			continue
		living.append(entry)
		if tick_index % ABILITY_AURA_INTERVAL_TICKS != 0:
			continue
		match String(entry.get("kind", "")):
			"weather_modifier":
				_apply_weather_modifier(entry)
			"weather_anticategory":
				_apply_weather_anticategory(entry)
	_weather_effects = living


func active_weather_effects() -> Array:
	## Presentation contract for the weather lane. The sim has no sky/weather
	## renderer, so this registry (and the power.weather event) is the whole
	## contract until one exists: retail's ChangeWeather cell (CLOUDY / RAINY),
	## the owning team and the tick the window lapses, in cast order.
	var rows: Array = []
	for entry in _weather_effects:
		rows.append({
			"team": int(entry.get("team", -1)),
			"power_id": String(entry.get("power_id", "")),
			"kind": String(entry.get("kind", "")),
			"weather": String(entry.get("weather", "")),
			"expire_tick": int(entry.get("expire_tick", -1)),
		})
	return rows


func _cast_spellbook_creep_allegiance(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	## Untamed Allegiance. Every creep lair the authored filter names, inside the
	## authored radius of the cast point, changes owner to the caster - and its
	## slaved guards go with it (SpawnBehavior SpawnedRequireSpawner = Yes).
	##
	## What is deliberately NOT done: the defected lair does not keep producing.
	## Retail replaces its spawn template with the *_FromDefectedLair hordes
	## (system.ini ProductionSpeedBonus Type row names them), and those objects
	## are not in the converted pack - so inventing continued production would be
	## inventing units. The lair is marked defected and its respawn clock is
	## stopped; the gap is carried on the event as defected_production_unconverted.
	##
	## NAMED GAP - opponent-owned lairs. The sweep below walks CREEP_TEAM only, so
	## it steals lairs that are still wild. Retail authors `TargetEnemy = Yes` with
	## an ENEMIES-relative CREEP_OBJECTFILTER, which ALSO matches a lair an
	## opponent already defected to themselves with their own Untamed Allegiance -
	## i.e. retail lets the spell be counter-cast to take a lair back, and this
	## does not. Carried on the event as opponent_owned_lairs_unconverted so the
	## narrower sweep is a recorded limit, not a silent one.
	var radius := float(effect.get("range_source", 0.0)) * _spellbook_world_scale()
	var filter := String(effect.get("filter", ""))
	var converted_lairs: Array[int] = []
	var converted_guards: Array[int] = []
	for lair_id in structure_ids(CREEP_TEAM):
		var lair: Dictionary = structures[lair_id]
		if String(lair.get("structure_kind", "")) != "creep_lair":
			continue
		if int(lair.get("health", 0)) <= 0:
			continue
		if Vector2(lair.get("position", Vector2.ZERO)).distance_to(point) > radius:
			continue
		# The sim stores each lair's RETAIL type name (CaveTrollLair,
		# MoriarGoblinLairSnow, ...), which is exactly what the resolved
		# CREEP_OBJECTFILTER lists, so the authored filter is applied verbatim.
		if not Array(effect.get("lair_types", [])).has(String(lair.get("creep_type_name", ""))):
			continue
		lair["team"] = team
		lair["creep_defected_team"] = team
		lair["creep_defected_tick"] = tick_index
		lair["creep_next_respawn_tick"] = 0
		for guard_value in (lair.get("creep_guard_ids", []) as Array).duplicate():
			var guard_id := int(guard_value)
			if not entities.has(guard_id):
				continue
			var guard: Dictionary = entities[guard_id]
			if int(guard.get("health", 0)) <= 0:
				continue
			guard["team"] = team
			guard["target_id"] = 0
			guard["target_kind"] = "battalion"
			guard["order_kind"] = ""
			guard["creep_defected_team"] = team
			guard.erase("creep_lair_id")
			guard.erase("creep_home")
			guard.erase("creep_guard_max_range")
			guard.erase("creep_guard_wander_range")
			guard.erase("creep_returning")
			_clear_member_targets(guard)
			_clear_pending_route(guard, false)
			guard["state"] = "idle"
			converted_guards.append(guard_id)
		lair["creep_guard_ids"] = []
		converted_lairs.append(lair_id)
	if converted_lairs.is_empty():
		return {"ok": false, "reason": "no-valid-targets"}
	_emit_event("power.creep_allegiance", 0, 0, {
		"team": team,
		"point": [snappedf(point.x, 0.001), snappedf(point.y, 0.001)],
		"radius_source": float(effect.get("range_source", 0.0)),
		"lairs": converted_lairs,
		"guards": converted_guards,
		"filter_matches": Array(effect.get("lair_types", [])).size(),
		"defected_production_unconverted": true,
		# See the NAMED GAP note on this function: the sweep is CREEP_TEAM-only,
		# while retail's ENEMIES filter would also steal an opponent-defected lair.
		"opponent_owned_lairs_unconverted": true,
	})
	return {"ok": true, "reason": "", "battalions": converted_guards.size(), "structures": converted_lairs.size()}


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
	var defeated_members: Array[int] = []
	for member_index in health_values.size():
		if remaining <= 0.0:
			break
		var current := int(health_values[member_index])
		if current <= 0:
			continue
		var applied := mini(float(current), remaining)
		health_values[member_index] = maxi(0, current - int(round(applied)))
		if int(health_values[member_index]) == 0:
			defeated_members.append(member_index)
		remaining -= applied
	row["member_health"] = health_values
	var aggregate := 0
	for value in health_values:
		aggregate += int(value)
	row["health"] = aggregate
	row["last_damage_tick"] = tick_index
	if aggregate <= 0:
		var death_policy := _bookkeep_battalion_death(
			id, row, "NORMAL", defeated_members
		)
		_emit_event("battalion.defeated", 0, id)
		if bool(death_policy.get("destroy_object", false)):
			entities.erase(id)
	else:
		_apply_playable_unit_death_policy(row, "NORMAL", defeated_members)


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
	var spawned := _spawn_summon_targets(team, point, Array(effect.get("targets", [])))
	_emit_event("power.summon", 0, 0, {"team": team, "point": [snappedf(point.x, 0.001), snappedf(point.y, 0.001)], "spawned": spawned})


func _spawn_summon_targets(team: int, point: Vector2, targets: Array) -> Array:
	## Shared summon spawn: register each converted summon rule as a live unit
	## rule and place its battalions with the authored summon lifetime. Used by
	## BOTH the spellbook OCL powers and the hero egg-chain abilities so the two
	## lanes cannot drift apart.
	var spawned: Array = []
	for target_value in targets:
		var target := target_value as Dictionary
		var rule: Dictionary = (target.get("rule", {}) as Dictionary).duplicate(true)
		var object_id := String(target.get("object_id", ""))
		if rule.is_empty():
			continue
		var count := int(target.get("count", 1))
		for index in range(count):
			var angle := TAU * float(index) / float(maxi(1, count))
			var spawn_point := point + Vector2(cos(angle), sin(angle)) * (1.0 if count <= 1 else 2.0)
			var entity_id := int(_next_dynamic_id.get(team, 1000))
			_next_dynamic_id[team] = entity_id + 1
			_add_battalion(
				entity_id, team, spawn_point, String(rule.get("horde_id", object_id)),
				object_id, object_id, 0, rule
			)
			if not entities.has(entity_id):
				continue
			var lifetime_ticks := int(target.get("lifetime_ticks", 0))
			if lifetime_ticks > 0:
				_summon_despawn_ticks[entity_id] = tick_index + lifetime_ticks
				entities[entity_id]["summon_lifetime_death_type"] = String(
					target.get("lifetime_death_type", "")
				).to_upper()
			if not Array((entities[entity_id] as Dictionary).get("summon_auras", [])).is_empty():
				_summon_aura_source_ids[entity_id] = true
			spawned.append(entity_id)
	return spawned


func spawn_script_object(object_type: String, team: int, at: Vector2) -> int:
	## Map-script object creation (campaign B4). Instantiates one battalion of
	## the retail object type `object_type` for `team` at `at` and returns its
	## entity id.
	##
	## Returns -1, and creates nothing, when this simulation cannot honestly
	## produce the type: the loaded content carries no unit rule for it, or the
	## team is not in the roster. That is the ordinary case for a campaign map
	## object whose retail type is not in the loaded faction slice, and the
	## caller (RetailMapScripts) keeps the object as registry state only rather
	## than substituting a different unit for it.
	##
	## Nothing inside the simulation calls this, so a match whose scripts never
	## create objects is byte-identical to one compiled before it existed.
	if object_type == "":
		return -1
	var unit_rules_value: Variant = _rules.get("unit_rules", {})
	if typeof(unit_rules_value) != TYPE_DICTIONARY:
		return -1
	var rule: Dictionary = (unit_rules_value as Dictionary).get(object_type, {}) as Dictionary
	if rule.is_empty():
		return -1
	if not _next_dynamic_id.has(team):
		# Allocating outside the seeded per-team id ranges would collide with
		# another team's ids, so an unseeded team refuses rather than inventing
		# an id space.
		if team == CREEP_TEAM:
			_next_dynamic_id[team] = 80001
		else:
			return -1
	var entity_id := int(_next_dynamic_id[team])
	_next_dynamic_id[team] = entity_id + 1
	_add_battalion(
		entity_id,
		team,
		at,
		String(rule.get("horde_id", object_type)),
		object_type,
		object_type,
		0
	)
	return entity_id if entities.has(entity_id) else -1


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
		var death_type := String(
			row.get("summon_lifetime_death_type", "")
		).to_upper()
		var health_values: Array = row.get("member_health", [])
		var defeated_members: Array[int] = []
		for index in health_values.size():
			if int(health_values[index]) > 0:
				defeated_members.append(index)
				health_values[index] = 0
		row["member_health"] = health_values
		row["health"] = 0
		# Remove the lifetime registry first so a combat-dead summon can never be
		# processed again as an expiry (duplicate OCL/event/CP release).
		_summon_despawn_ticks.erase(entity_id)
		var death_policy := _bookkeep_battalion_death(
			entity_id, row, death_type, defeated_members
		)
		_emit_event("power.summon_expired", 0, entity_id, {"team": int(row.get("team", -1))})
		# Honor the converted unit death policy. The authored death type begins its
		# matching path here; an immediate-destroy verdict removes now, while a retained
		# corpse is cleaned by the ordinary corpse scheduler.
		if bool(death_policy.get("destroy_object", false)):
			entities.erase(entity_id)
		expired.append(entity_id)
	for entity_id in expired:
		_summon_despawn_ticks.erase(entity_id)


func _step_summon_auras() -> void:
	## Moving scout summons (Crebain/Cave Bats) carry GenericDebuff as part of
	## their payload. Refresh the converted timed modifier on enemy battalions;
	## its authored duration naturally lets the debuff trail the moving aura by
	## up to one refresh window after separation or expiry.
	if _summon_aura_source_ids.is_empty():
		return
	var source_ids: Array = _summon_aura_source_ids.keys()
	source_ids.sort()
	for source_id_value in source_ids:
		var source_id := int(source_id_value)
		if not entities.has(source_id):
			_summon_aura_source_ids.erase(source_id)
			continue
		var source: Dictionary = entities[source_id]
		if int(source.get("health", 0)) <= 0:
			continue
		for aura_value in Array(source.get("summon_auras", [])):
			_refresh_one_summon_aura(source_id, source, aura_value as Dictionary)


func _refresh_one_summon_aura(source_id: int, source: Dictionary, aura: Dictionary) -> void:
	if aura.is_empty():
		return
	var refresh_ticks := int(aura.get("refresh_ticks", 1))
	if tick_index % maxi(1, refresh_ticks) != 0:
		return
	var source_team := int(source.get("team", -1))
	var origin := Vector2(source.get("position", Vector2.ZERO))
	var radius := float(aura.get("range_source", 0.0)) * _spellbook_world_scale()
	for target_id in _spatial_gather_sorted(origin, radius):
		if not entities.has(target_id):
			continue
		var target: Dictionary = entities[target_id]
		var same_team := int(target.get("team", -1)) == source_team
		if int(target.get("health", 0)) <= 0:
			continue
		if Vector2(target.get("position", Vector2.ZERO)).distance_to(origin) > radius:
			continue
		if not _summon_aura_allows_relation(aura, same_team):
			continue
		if not _spellbook_member_affects(
			target, String(aura.get("filter", "")), same_team
		):
			continue
		_set_timed_modifier(
			target,
			"summon-aura:%d:%s" % [source_id, String(aura.get("id", ""))],
			Array(aura.get("modifiers", [])),
			tick_index + int(aura.get("duration_ticks", 1))
		)


func _summon_aura_allows_relation(aura: Dictionary, same_team: bool) -> bool:
	var target_enemy: Variant = aura.get("target_enemy", null)
	var target_allies: Variant = aura.get("target_allies", null)
	# Selected packs predating targetEnemy/targetAllies still carry the authored
	# AttributeModifier category and relation tokens. Resolve each missing side
	# from that evidence; an explicitly authored flag overrides only its side.
	var category := String(aura.get("category", "")).to_upper()
	var filter_terms := String(aura.get("filter", "")).to_upper().replace("\t", " ").split(" ", false)
	var authored_enemy := category == "DEBUFF" or filter_terms.has("ENEMIES")
	var authored_ally := not authored_enemy
	if filter_terms.has("ALLIES"):
		authored_ally = true
	# A positive explicit side is exclusive when the opposite side is omitted.
	# An explicit false only disables its own side; the omitted side may still
	# use legacy category/filter evidence.
	var allow_enemy := bool(target_enemy) if target_enemy != null else (false if target_allies == true else authored_enemy)
	var allow_ally := bool(target_allies) if target_allies != null else (false if target_enemy == true else authored_ally)
	return allow_ally if same_team else allow_enemy


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
		# A grove buffs a bounded disc, so this is a neighbourhood query over the
		# owning team rather than a sweep of its whole army.
		for id in _spatial_gather_sorted(point, range_sim):
			if not entities.has(id):
				continue
			var row: Dictionary = entities[id]
			if int(row.get("team", -1)) != team or int(row.get("health", 0)) <= 0:
				continue
			if Vector2(row.get("position", Vector2.ZERO)).distance_to(point) > range_sim:
				continue
			if not _spellbook_member_affects(
				row, String(grove.get("filter", "")), true
			):
				continue
			# The taint buff is an ordinary timed modifier, not a private pair of row
			# fields. Retail authors it as a ModifierList exactly like Darkness, and
			# attributemodifier.ini notes that same-kind rows from different lists ADD;
			# the private fields multiplied instead and bypassed ABILITY_ARMOR_CAP, so
			# taint + Darkness composed wrongly and could reach total immunity.
			#
			# KEYED BY THE AUTHORED LEAF ID, not a hardcoded "GenericBuff". The
			# grove/taint family binds different ModifierLists per edition and per
			# faction — RotWK ElvenGrove binds GenericBuff, BFME2 ElvenGrove binds
			# GenericArmorLeadership (grove.ini:31), TaintLand binds its own — and a
			# single hardcoded key made two different authored lists collide in one
			# accumulator slot, so the second overwrote the first instead of stacking
			# beside it. Same rule as the summon-aura and weather lanes, which are
			# already keyed by their authored id.
			_set_timed_modifier(
				row,
				"taint:%s" % String(grove.get("modifier", "")),
				[
					{"kind": "ARMOR", "value": float(grove.get("armor_mult", 0.0))},
					{"kind": "DAMAGE_MULT", "value": float(grove.get("damage_mult", 1.0))},
				],
				tick_index + int(grove.get("buff_duration_ticks", 1)),
			)
	_active_groves = living


func _spellbook_member_affects(
	row: Dictionary, filter_text: String, same_team: Variant = null
) -> bool:
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
		if term == "ALLIES":
			if same_team != null and not bool(same_team):
				return false
			continue
		if term == "ENEMIES":
			if same_team != null and bool(same_team):
				return false
			continue
		if term.begins_with("-"):
			if kinds.has(term.trim_prefix("-")):
				return false
		elif term.begins_with("+"):
			if kinds.has(term.trim_prefix("+")):
				included = true
		elif term == "ANY":
			included = true
		elif term == "ALL":
			# `ALL` is the universal set ONLY when the filter authors no including
			# kind term of its own — retail's `ALL ENEMIES` (Freezing Rain) is a
			# relation-only filter and means everyone. Every OTHER `ALL` in this
			# corpus sits beside kind terms, where the filter reads conjunctively;
			# a blanket include there would silently widen it to the whole board.
			if not _spellbook_filter_has_kind_terms(filter_text):
				included = true
		elif kinds.has(term):
			included = true
	return included



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
			if elapsed >= build_ticks:
				_apply_structure_create_grants(structure, false, true)
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
		var prior_member_health := int(health_values[member_index])
		health_values[member_index] = maxi(0, prior_member_health - amount)
		target["member_health"] = health_values
		var aggregate := 0
		for value in health_values:
			aggregate += int(value)
		target["health"] = aggregate
		target["last_damage_tick"] = tick_index
		var defeated_members: Array[int] = []
		if prior_member_health > 0 and int(health_values[member_index]) == 0:
			defeated_members.append(member_index)
		if aggregate <= 0:
			var death_policy := _bookkeep_battalion_death(
				best_id, target, "NORMAL", defeated_members
			)
			_emit_event("battalion.defeated", structure_id, best_id)
			if bool(death_policy.get("destroy_object", false)):
				entities.erase(best_id)
		else:
			_apply_playable_unit_death_policy(target, "NORMAL", defeated_members)
			_emit_event("power.structure_weapon_hit", structure_id, best_id, {"amount": amount})
		attack["cooldown"] = tick_index + int(attack.get("period_ticks", 1)) + int(attack.get("pre_attack_ticks", 0))


func validate_construct_site(builder_ids: Array[int], structure_kind: String, point: Vector2, team: int = PLAYER_TEAM) -> Dictionary:
	# Non-mutating dry run of issue_construct's admission checks so the
	# placement ghost can tint valid/invalid while the player aims. The team is
	# the REQUESTING seat's: a lockstep guest probes its own faction's tables.
	return issue_construct(builder_ids, structure_kind, point, true, team)


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
	_expansion_build_rules = rules.duplicate(true)


func _seed_expansion_pads_for(fortress_id: int) -> void:
	if expansion_pads.has(fortress_id) or not structures.has(fortress_id):
		return
	var fortress: Dictionary = structures[fortress_id]
	if String(fortress.get("structure_kind", "")) != "fortress":
		return
	var center := Vector2(fortress.get("position", Vector2.ZERO))
	var pads: Array = []
	var castle := _castle_behavior_for_structure(fortress)
	if not castle.is_empty():
		_unpack_castle_behavior_for_structure(fortress_id)
		pads = (fortress.get("castle_expansion_pads", []) as Array).duplicate(true)
	else:
		for pad_kind in EXPANSION_PAD_LAYOUT.keys():
			for slot in range((EXPANSION_PAD_LAYOUT[pad_kind] as Array).size()):
				pads.append({
					"slot": slot,
					"pad_kind": pad_kind,
					"position": center + (EXPANSION_PAD_LAYOUT[pad_kind] as Array)[slot],
					"expansion_structure_id": 0,
				})
	expansion_pads[fortress_id] = pads


func _unpack_castle_behavior_for_structure(structure_id: int) -> bool:
	## CastleBehavior executes once for every structure that carries the selected
	## pack contract. Every BSE row becomes an authoritative structure object;
	## no pad/citadel is inferred and no missing template gets substituted.
	if not structures.has(structure_id):
		return false
	var owner: Dictionary = structures[structure_id]
	if bool(owner.get("castle_behavior_unpacked", false)):
		return true
	var castle := _castle_behavior_for_structure(owner)
	if castle.is_empty():
		return false
	if castle.has("_error"):
		var message := "CastleBehavior for '%s' failed closed: %s" % [String(owner.get("structure_kind", "structure")), String(castle.get("_error", "invalid selected contract"))]
		owner["castle_behavior_error"] = message
		configuration_error = message
		push_error(message)
		return false
	var child_ids: Array[int] = []
	var pads: Array = []
	var side_slot := 0
	var corner_slot := 0
	for piece_value in castle.get("pieces", []) as Array:
		var piece: Dictionary = piece_value
		var source_object_id := String(piece.get("source_object_id", ""))
		var object_id := String(piece.get("object_id", ""))
		var maximum_health := int(piece.get("maximum_health", 0))
		if source_object_id == "" or object_id == "" or maximum_health <= 0:
			push_error("RetailSliceSim refused incomplete CastleBehavior piece %s" % str(piece))
			return false
		var offset: Vector2 = piece.get("offset_source", Vector2.ZERO)
		var at := Vector2(owner.get("position", Vector2.ZERO)) + _retail_source_to_sim_offset(offset)
		var child_id := _spawn_castle_piece_structure(
			structure_id,
			owner,
			source_object_id,
			object_id,
			at,
			float(piece.get("elevation_source", 0.0)),
			float(piece.get("angle_radians", 0.0)),
			maximum_health,
			int(piece.get("index", -1))
		)
		if child_id == 0:
			return false
		child_ids.append(child_id)
		var folded := source_object_id.to_lower()
		if folded.contains("expansionpad"):
			var pad_kind := "corner" if folded.contains("corner") else "side"
			var slot := corner_slot if pad_kind == "corner" else side_slot
			if pad_kind == "corner":
				corner_slot += 1
			else:
				side_slot += 1
			pads.append({
				"slot": slot,
				"pad_kind": pad_kind,
				"position": at,
				"angle_radians": float(piece.get("angle_radians", 0.0)),
				"source_object_id": source_object_id,
				"castle_piece_structure_id": child_id,
				"expansion_structure_id": 0,
				"castle_piece_index": int(piece.get("index", -1)),
			})
	owner["castle_behavior_unpacked"] = true
	owner["castle_template_token"] = String(castle.get("castle_template_token", ""))
	owner["castle_piece_structure_ids"] = child_ids
	owner["castle_expansion_pads"] = pads
	_emit_event("structure.castle_unpacked", 0, structure_id, {
		"team": int(owner.get("team", -1)),
		"token": String(castle.get("castle_template_token", "")),
		"piece_count": child_ids.size(),
		"pad_count": pads.size(),
	})
	return true


func _castle_behavior_for_structure(structure: Dictionary) -> Dictionary:
	var direct: Variant = structure.get("castle_behavior", {})
	if typeof(direct) == TYPE_DICTIONARY and not (direct as Dictionary).is_empty():
		return direct as Dictionary
	var team := int(structure.get("team", -1))
	var kind := String(structure.get("structure_kind", ""))
	var manifest_source := ""
	var manifest_sources: Variant = structure_source_object_ids_for_team(team).get(kind, [])
	if typeof(manifest_sources) == TYPE_ARRAY and not (manifest_sources as Array).is_empty():
		manifest_source = String((manifest_sources as Array)[0])
	elif typeof(manifest_sources) in [TYPE_STRING, TYPE_STRING_NAME]:
		manifest_source = String(manifest_sources)
	for key in [
		manifest_source,
		String(structure.get("source_object_id", "")),
		String(structure.get("object_id", "")),
		String(structure.get("name", "")),
	]:
		if key != "" and _castle_behavior_by_source.has(key):
			return (_castle_behavior_by_source[key] as Dictionary).duplicate(true)
		# Case-insensitive source map
		for source_key in _castle_behavior_by_source.keys():
			if String(source_key).to_lower() == key.to_lower():
				return (_castle_behavior_by_source[source_key] as Dictionary).duplicate(true)
	return {}


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
	var structure_id := _next_dynamic_structure_id
	_next_dynamic_structure_id += 1
	var team := int(fortress.get("team", -1))
	structures[structure_id] = {
		"id": structure_id,
		"team": team,
		"kind": "structure",
		"structure_kind": "castle_piece",
		"name": source_object_id,
		"source_object_id": source_object_id,
		"object_id": object_id,
		"position": at,
		"elevation": float(fortress.get("elevation", 0.0)) + _retail_source_to_sim_offset(Vector2(elevation_source, 0.0)).x,
		"facing_radians": angle_radians + float(fortress.get("facing_radians", 0.0)),
		"rally": at + Vector2(2.0, 0.0),
		"health": maximum_health,
		"maximum_health": maximum_health,
		"construction_progress": 1.0,
		"level": 1,
		"completed_upgrades": [],
		"upgrade_queue": [],
		"production": [],
		"queue": [],
		"damage_remainders": {},
		"income_per_payout": 0,
		"castle_piece_of_fortress": fortress_id,
		"castle_piece_index": piece_index,
	}
	_apply_structure_inherit_upgrades(structures[structure_id] as Dictionary)
	_initialize_structure_auto_deposit(structures[structure_id] as Dictionary)
	return structure_id


func _seed_all_expansion_pads() -> void:
	for structure_id in structure_ids():
		if String((structures[structure_id] as Dictionary).get("structure_kind", "")) == "fortress":
			_seed_expansion_pads_for(structure_id)


func expansion_pad_states(fortress_id: int) -> Array:
	return (expansion_pads.get(fortress_id, []) as Array).duplicate(true)


func _seed_build_plots_for_team(team: int) -> void:
	if build_plots.has(team):
		return
	# Prefer the fortress footprint as the ring center; fall back to the team's
	# spawn anchor when a team has no fortress (defensive — every rostered team
	# seeds one in _initialize_base_loop).
	var center := _team_center(team)
	var fortress := fortress_id(team)
	if fortress != 0:
		center = Vector2((structures[fortress] as Dictionary).get("position", center))
	var plots: Array = []
	for offset in BUILD_PLOT_RING_OFFSETS:
		plots.append({"position": center + (offset as Vector2), "occupant_structure_id": 0})
	build_plots[team] = plots


func _seed_all_build_plots() -> void:
	for team in _roster_team_ids():
		_seed_build_plots_for_team(int(team))


func _reconcile_build_plots(team: int) -> void:
	## A plot is free when it has no occupant, or its occupant structure no longer
	## exists / has been razed (health <= 0). Reconciling lazily keeps plot freeing
	## deterministic without hooking every structure-destruction path.
	if not build_plots.has(team):
		return
	for plot_value in build_plots[team] as Array:
		var plot: Dictionary = plot_value
		var occupant := int(plot.get("occupant_structure_id", 0))
		if occupant == 0:
			continue
		if not structures.has(occupant) or int((structures[occupant] as Dictionary).get("health", 0)) <= 0:
			plot["occupant_structure_id"] = 0


func build_plot_states(team: int) -> Array:
	## Reconciled (razed plots freed) copy of a team's build plots for the HUD and
	## tests. Empty unless build_plots_only is on.
	_reconcile_build_plots(team)
	return (build_plots.get(team, []) as Array).duplicate(true)


func _free_build_plot_index_near(team: int, position: Vector2) -> int:
	## Nearest free plot within BUILD_PLOT_PICK_RADIUS of the click, ties broken by
	## lowest index (deterministic). -1 when none is close enough.
	_reconcile_build_plots(team)
	var plots: Array = build_plots.get(team, [])
	var best := -1
	var best_dist := BUILD_PLOT_PICK_RADIUS + 1.0
	for index in plots.size():
		var plot: Dictionary = plots[index]
		if int(plot.get("occupant_structure_id", 0)) != 0:
			continue
		var dist := Vector2(plot.get("position", Vector2.ZERO)).distance_to(position)
		if dist <= BUILD_PLOT_PICK_RADIUS and dist < best_dist:
			best_dist = dist
			best = index
	return best


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
	var permission := building_permission_for_kind(team, expansion_kind)
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
	if bool(rule.get("highlander_body", false)):
		structures[structure_id]["highlander_body"] = true
	_apply_structure_inherit_upgrades(structures[structure_id] as Dictionary)
	_initialize_structure_auto_deposit(structures[structure_id] as Dictionary)
	(pads[pad_index] as Dictionary)["expansion_structure_id"] = structure_id
	_emit_event("construction.started", 0, structure_id, {"team": team, "structure_kind": expansion_kind, "cost": cost, "build_ticks": build_ticks})
	return {"ok": true, "structure_id": structure_id, "cost": cost, "build_ticks": build_ticks}


# --- Unpackable bases (script base flags) ----------------------------------
#
# The skirmish-AI base model's first piece: map-authored BASE FLAGS that a
# player can UNPACK into a base of their own (retail's castle/camp flags, the
# subjects of NAMED_BASE_UNPACK / NAMED_BASE_UNPACK_FREE and the base flags
# ai_economy_execution polls 64 times per pass). A flag is match configuration
# keyed by its SCRIPT OBJECT NAME; unpacking it creates a completed fortress
# structure with expansion pads at the flag's authored position, owned by the
# unpacking team, paid from that team's resources unless the free variant.
#
# HASH INERTNESS. `unpackable_bases` participates in the authoritative state
# ONLY when non-empty (see _authoritative_state): a match that configures no
# base flags contributes NOTHING to state_hash(), which is what keeps the
# frozen cross-platform pin (retail_state_pin_runner.gd) standing as proof the
# subsystem is inert by default. Do not add an unconditional key for it.
#
# MODELLING CHOICES, stated rather than implied:
#   * The unpacked base completes INSTANTLY (construction_progress 1.0).
#     Retail plays an unpack build-up; the sim models the outcome, not the
#     animation, and an under-construction base would make the bound result
#     reference point at a site later readers cannot build at yet.
#   * The base produces nothing and earns nothing (empty production, zero
#     income): its value is its expansion pads. Anything more would be a
#     guess at retail camp internals nobody has sourced yet.
#   * No proximity or builder requirement: the retail action is a script
#     verb, not a porter order.
#   * Placement is the authored flag position, unchecked: flags are match
#     configuration like the home layout, not a player click.
#   * Unpackability is per-flag, not per-player: the sim models no per-player
#     flag restrictions, so a packed flag is unpackable by ANY rostered team
#     while the match runs and the base loop is on.

## Script base-flag name -> {"position": Vector2, "cost": int, "health": int,
## "unpacked_by": int (team, -1 while packed), "structure_id": int (0 while
## packed)}. Hashed only when non-empty - see the block comment above.
var unpackable_bases: Dictionary = {}


func configure_unpackable_bases(bases: Dictionary) -> bool:
	## bases: {name: {"position": Vector2, "cost": int, "health": int}} - match
	## configuration, set identically on every peer before the match starts.
	## Every row resets to packed. An empty dict clears the subsystem (and its
	## hash contribution disappears entirely - empty-is-absent).
	##
	## THE FLAG-SHADOWING INVARIANT, second direction: a name may never be
	## both a base flag and a bound script unit reference. Bind-time already
	## refuses a reference that would shadow an existing flag (the adapter's
	## _unit_reference_rejection); THIS is the other edge - configuring a flag
	## whose name an existing reference already holds would let the stale
	## reference eclipse the sim-owned flag on every later resolve. Such a
	## configure is REFUSED WHOLE (false, push_error, nothing applied): a
	## half-applied table would be worse than a loud refusal, and nothing
	## enforces configure-before-bind ordering for the caller.
	var names := bases.keys()
	names.sort()
	var collisions: Array[String] = []
	var reference_teams := script_unit_references.keys()
	reference_teams.sort()
	for name_value in names:
		for team in reference_teams:
			if (script_unit_references[team] as Dictionary).has(String(name_value)):
				collisions.append(String(name_value))
				break
	if not collisions.is_empty():
		push_error(
			"configure_unpackable_bases refused: %s already bound as script unit "
			% ", ".join(collisions)
			+ "reference(s); a flag of the same name would be eclipsed on every "
			+ "resolve (the flag-shadowing invariant holds in both directions)"
		)
		return false
	unpackable_bases = {}
	for name_value in names:
		var spec: Dictionary = bases[name_value]
		unpackable_bases[String(name_value)] = {
			"position": Vector2(spec.get("position", Vector2.ZERO)),
			"cost": maxi(0, int(spec.get("cost", 0))),
			"health": maxi(1, int(spec.get("health", 1000))),
			"unpacked_by": -1,
			"structure_id": 0,
		}
	return true


func unpackable_base_names() -> Array[String]:
	var names: Array[String] = []
	for name_value in unpackable_bases.keys():
		names.append(String(name_value))
	names.sort()
	return names


func unpackable_base_state(base_name: String) -> Dictionary:
	## Deep copy of one flag row; {} for a name the sim does not model.
	return (unpackable_bases.get(base_name, {}) as Dictionary).duplicate(true)


func base_flag_unpackable(base_name: String) -> Dictionary:
	## Side-effect-free read behind NAMED_BASE_UNPACKABLE_FOR_PLAYER.
	## {"known": false} when the sim does not model this flag (the caller must
	## refuse, not answer false); otherwise {"known": true, "unpackable": bool}.
	## Unpackable = the base loop is on, the match is unresolved, and the flag
	## is still packed. Money is deliberately NOT consulted: affordability is
	## the unpack ACTION's concern, and folding it in here would make the
	## condition flicker with the treasury.
	if not unpackable_bases.has(base_name):
		return {"known": false}
	var row: Dictionary = unpackable_bases[base_name]
	return {
		"known": true,
		"unpackable": base_loop_enabled and winner == -1 and int(row.get("unpacked_by", -1)) < 0,
	}


func unpack_base(team: int, base_name: String, free: bool) -> Dictionary:
	## NAMED_BASE_UNPACK (`free` false) / NAMED_BASE_UNPACK_FREE (`free` true):
	## the flag at `base_name` unpacks into a completed fortress (with expansion
	## pads) owned by `team`. Paid unpacks charge the flag's authored cost; the
	## free variant charges nothing. Returns {"ok": true, "structure_id": id,
	## "cost": paid} or {"ok": false, "reason": ...}.
	if not base_loop_enabled or winner != -1:
		return {"ok": false, "reason": "match-unavailable"}
	if not unpackable_bases.has(base_name):
		return {"ok": false, "reason": "unknown-base"}
	if not _roster_team_ids().has(team):
		return {"ok": false, "reason": "unknown-team"}
	var row: Dictionary = unpackable_bases[base_name]
	if int(row.get("unpacked_by", -1)) >= 0:
		return {"ok": false, "reason": "base-already-unpacked"}
	var cost := 0 if free else int(row.get("cost", 0))
	if resources_for_team(team) < cost:
		return {"ok": false, "reason": "insufficient-resources", "cost": cost}
	team_resources[team] = resources_for_team(team) - cost
	var structure_id := _next_dynamic_structure_id
	_next_dynamic_structure_id += 1
	var maximum_health := int(row.get("health", 1000))
	var position := Vector2(row.get("position", Vector2.ZERO))
	structures[structure_id] = {
		"id": structure_id,
		"team": team,
		"kind": "structure",
		"structure_kind": "fortress",
		"name": "Fortress",
		"position": position,
		"rally": position + Vector2(4.0, 0.0),
		"health": maximum_health,
		"maximum_health": maximum_health,
		"construction_progress": 1.0,
		"level": 1,
		"completed_upgrades": [],
		"upgrade_queue": [],
		"production": [],
		"queue": [],
		"damage_remainders": {},
		"income_per_payout": 0,
		"unpacked_from_base": base_name,
	}
	var fortress_build_rule: Dictionary = (
		structure_build_rules_for_team(team).get("fortress", {}) as Dictionary
	)
	if bool(fortress_build_rule.get("highlander_body", false)):
		structures[structure_id]["highlander_body"] = true
	# Prefer faction fortress source id so castleBehavior BSE contracts resolve.
	var fortress_sources: Dictionary = structure_source_object_ids_for_team(team)
	var fortress_aliases: Array = fortress_sources.get("fortress", []) as Array
	if not fortress_aliases.is_empty():
		structures[structure_id]["source_object_id"] = String(fortress_aliases[0])
	_apply_structure_create_grants(
		structures[structure_id] as Dictionary, true, true
	)
	_apply_structure_inherit_upgrades(structures[structure_id] as Dictionary)
	_initialize_structure_auto_deposit(structures[structure_id] as Dictionary)
	_seed_expansion_pads_for(structure_id)
	row["unpacked_by"] = team
	row["structure_id"] = structure_id
	_emit_event("base.unpacked", 0, structure_id, {"team": team, "base": base_name, "cost": cost, "free": free})
	return {"ok": true, "structure_id": structure_id, "cost": cost}


# --- Map named-object namespace (retail's COMPLETE name table) --------------
#
# The CLOSED set of script object names the installed map authors (schema-v1
# world.namedObjects, recorded by the install seam when the importer decoded
# the map's script world - world.available true). Retail's ScriptEngine name
# table (m_namedObjects, GPL Generals ScriptEngine.h + whale reversal) is
# built from objects PLACED ON THE MAP; imported script LIBRARIES contribute
# scripts and teams, never objects, so a name a library authors but the map
# does not place resolves to NULL and every named condition answers FALSE.
# This table is what lets the script-world adapter give that retail FALSE for
# a name genuinely absent from the map (case b of the three-way split in
# SliceUnits) instead of refusing: with the namespace declared, "the sim does
# not model that name" AND "the map does not author it" together reproduce
# retail's grounded false. Names the map DOES author but the sim does not
# model keep refusing (case c) - retail would answer from a real object there.
#
# SIM-owned (not adapter state) for the script_unit_references reason: the
# answer changes script outcomes, so a peer adopting a snapshot must answer
# named conditions exactly as the peer that minted it.
#
# HASH INERTNESS: participates in the authoritative state ONLY when declared
# (empty-is-absent, the unpackable_bases discipline), so every scriptless
# scenario keeps its frozen cross-platform pin untouched.

## {} = no authoritative namespace declared (every unknown name refuses);
## {"names": {name: true, ...}} = the map's complete named-object table
## (declared even when the map authors zero names - then EVERY unknown name
## is retail-false). Match configuration: set by the install seam, kept
## across reset_match() like script-team identities.
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
	if not structures.has(structure_id):
		return {"ok": false, "reason": "structure %d missing" % structure_id}
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
	if creep_lairs_enabled:
		for family_value in CREEP_LAIR_FAMILIES.keys():
			if String(family_value).to_lower() == folded:
				return true
		for alias_value in CREEP_LAIR_FAMILY_ALIASES.keys():
			if String(alias_value).to_lower() == folded:
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
	## object id -> structure kind), the manifest/expansion runtime ids, or -
	## for creep camps - its recorded retail type_name.
	var creep_type := String(row.get("creep_type_name", ""))
	if creep_type != "" and (probe["folded"] as Dictionary).has(creep_type.to_lower()):
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
	## The command points queue_unit will commit for one production of
	## `unit_type` - the admission rule's own number (same rule/default
	## resolution), exposed so HAS_COMMAND_POINTS_TO_BUILD_UNIT can never
	## disagree with the queue that follows it. -1 for an unmodeled type.
	if not _unit_production_rules.has(unit_type):
		return -1
	return maxi(0, _production_rule_value(unit_type, "command_points_rule", "default_command_points"))


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
	if bool(production_rule.get("is_ring_hero", false)) and not ring_mechanic_enabled:
		return {"ok": false, "reason": "ring-heroes-disabled"}
	if created_hero_owner_team(unit_type) not in [-1, team]:
		# Every peer registers every seat's created heroes so the rule tables
		# match; only the seat that made one may buy it.
		return {"ok": false, "reason": "created-hero-not-owned"}
	if String(production_rule.get("category", "")) == "hero" and hero_unavailable(team, unit_type):
		return {"ok": false, "reason": "hero-unavailable"}
	var missing_production_upgrade := production_gate_unsatisfied(
		unit_type,
		String(building.get("structure_kind", "")),
		Array(building.get("completed_upgrades", [])),
		(team_upgrades.get(team, {}) as Dictionary).keys(),
	)
	if missing_production_upgrade != "":
		return {"ok": false, "reason": "missing-upgrade", "required_upgrade": missing_production_upgrade}
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
	if (
		command_points_for_team(team) + queued_command_points + command_cost
		> command_point_total_for_team(team)
	):
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
	if not retail_formation_movement:
		# Leave the key absent entirely so the row - and therefore the state hash
		# - stays byte-identical for every run that has not opted in.
		return
	var slowest := INF
	for id in accepted_ids:
		var row: Dictionary = entities[id]
		slowest = minf(slowest, maxf(0.0, float(row.get("speed", 0.0))))
	if slowest == INF:
		return
	for id in accepted_ids:
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
		_cleanup_expired_corpses()
		return
	# FoW consumer: each living unit reveals fog cells for its owner by vision.
	_step_fog_from_vision()
	if base_loop_enabled:
		_step_economy()
		_step_structure_upgrades()
		_step_battalion_upgrades()
		_step_production()
	_step_pending_power_effects()
	_step_grove_auras()
	_step_field_pings()
	_step_weather_effects()
	_step_summon_despawns()
	_step_summon_auras()
	_step_structure_weapons()
	if ai_enabled and tick_index % AI_CONTROLLER_BASE_INTERVAL == 0:
		_update_ai_controllers()
	if creep_lairs_enabled:
		# Deterministic creep step (lair respawns, hole rebuilds, guard leash
		# decisions) runs before the entity step executes the resulting orders.
		_step_creeps()
	for id in entity_ids():
		_step_entity(id)
	_step_banner_carriers()
	_step_structure_eviction()
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
		if int(a.get("health", 0)) <= 0 or String(a.get("state", "")) != "idle" or bool(a.get("flying", false)):
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
				# map scale like every other range. Gandalf's Wizard Blast is
				# the compiled Men ability that carries them today
				# (knockbackRadius 110, knockbackStrength 70); abilities whose
				# MetaImpactNugget extraction is still importer follow-up keep
				# these keys at 0 and deal damage without a shockwave —
				# fail-closed, nothing invented.
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
			"experience-grant":
				# LevelGrantSpecialPower: StartAbilityRange gates the cast like
				# an attack range; RadiusEffect is the grant circle; the
				# authored Experience amount is scale-free.
				effect["range"] = float(effect.get("startAbilityRange", 0.0)) * scale
				effect["radius_scaled"] = float(effect.get("radiusEffect", 0.0)) * scale
			"arrow-storm":
				# ArrowStormUpdate barrage: ranges/radii are source units; the
				# burst cadence is the authored PersistentPrepTime; shot counts
				# and per-shot weapon damage are scale-free.
				effect["range"] = float(effect.get("startAbilityRange", 0.0)) * scale
				effect["target_radius_scaled"] = float(effect.get("targetRadius", 0.0)) * scale
				effect["shot_interval_ticks"] = maxi(1, roundi(float(effect.get("persistentPrepMs", 0.0)) / (TICK_SECONDS * 1000.0)))
			"stealth-toggle":
				# ToggleHiddenSpecialAbilityUpdate / InvisibilitySpecialPower:
				# EffectDuration is milliseconds; an authored BroadcastRadius
				# (ally cloak) binds to map scale. Zero stays zero: an
				# unauthored duration keeps the cast fail-closed.
				var stealth_ms := float(effect.get("effectDurationMs", 0.0))
				effect["duration_ticks"] = maxi(1, roundi(stealth_ms / (TICK_SECONDS * 1000.0))) if stealth_ms > 0.0 else 0
				effect["broadcast_radius_scaled"] = float(effect.get("broadcastRadius", 0.0)) * scale
			"teleport":
				# TeleportSpecialAbilityUpdate: MaxDistance gates the cast like
				# a range; BusyForDuration holds the hero after arrival.
				effect["range"] = float(effect.get("maxDistance", 0.0)) * scale
				effect["busy_ticks"] = maxi(0, roundi(float(effect.get("busyForDurationMs", 0.0)) / (TICK_SECONDS * 1000.0)))
			"curse":
				# CurseSpecialPower: StartAbilityRange gates the cast; the
				# radius cursor bounds target selection; CursePercentage is
				# scale-free.
				effect["range"] = float(effect.get("startAbilityRange", 0.0)) * scale
				effect["radius_scaled"] = float(effect.get("radiusCursorRadius", 0.0)) * scale
			"leadership-strip":
				# SpecialPowerModule AntiCategory=LEADERSHIP: the authored
				# AttributeModifierRange binds to map scale; the paired
				# ModifierList authors only the suppression duration.
				effect["radius_scaled"] = float(effect.get("attributeModifierRange", 0.0)) * scale
				var strip_ms := float(effect.get("antiCategoryDurationMs", 0.0))
				effect["duration_ticks"] = maxi(1, roundi(strip_ms / (TICK_SECONDS * 1000.0))) if strip_ms > 0.0 else 0
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
	## Attach converter moduleContracts to a live entity for runtime consumers.
	## Deferred/opaque rows stay as authored evidence. Executable KeepObjectDie
	## contracts fold into death policy (destroyOnDeath=false keeps the object).
	var unit_type := String(row.get("unit_type", ""))
	var contracts: Array = _unit_module_contracts.get(unit_type, []) as Array
	if contracts.is_empty():
		return
	row["module_contracts"] = contracts.duplicate(true)
	for contract_value in contracts:
		if typeof(contract_value) != TYPE_DICTIONARY:
			continue
		var contract := contract_value as Dictionary
		var module_name := String(contract.get("module", ""))
		var fields: Dictionary = contract.get("fields", {}) as Dictionary
		var folded := module_name.to_lower()
		var executable := bool(contract.get("executable", false))
		if folded.contains("keepobjectdie") or folded.contains("createobjectdie"):
			if not fields.is_empty():
				var die_rows: Array = row.get("module_die_contracts", []) as Array
				die_rows.append({
					"module": module_name,
					"fields": fields.duplicate(true),
					"executable": executable,
				})
				row["module_die_contracts"] = die_rows
		# KeepObjectDie: forbids erase when death_type matches policy.
		if executable and folded.contains("keepobjectdie"):
			var destroy_on_death := true
			if fields.has("destroyOnDeath"):
				destroy_on_death = bool(fields.get("destroyOnDeath", true))
			if not destroy_on_death:
				var death_types := String(fields.get("deathTypes", "ALL"))
				var excluded: Array = []
				var excluded_raw: Variant = fields.get("excludedDeathTypes", [])
				if typeof(excluded_raw) == TYPE_ARRAY:
					for item in excluded_raw as Array:
						excluded.append(String(item).to_upper())
				# Only ALL is proven (same honesty bar as DestroyDie). Other
				# death-type subsets stay as module_die_contracts evidence.
				if death_types == "ALL":
					row["keep_object_die"] = true
					row["keep_object_die_module"] = module_name
					row["keep_object_die_policy"] = {
						"death_types": death_types,
						"excluded_death_types": excluded,
					}
		# CreateObjectDie: queue CreationList on matching death (second consumer).
		if executable and folded.contains("createobjectdie"):
			var creation_list := ""
			var cl_raw: Variant = fields.get("CreationList", fields.get("creationList", {}))
			if typeof(cl_raw) == TYPE_DICTIONARY:
				creation_list = String((cl_raw as Dictionary).get("value", ""))
			elif typeof(cl_raw) == TYPE_STRING:
				creation_list = String(cl_raw)
			var death_types2 := String(fields.get("deathTypes", "ALL"))
			var excluded2: Array = []
			var excluded_raw2: Variant = fields.get("excludedDeathTypes", [])
			if typeof(excluded_raw2) == TYPE_ARRAY:
				for item2 in excluded_raw2 as Array:
					excluded2.append(String(item2).to_upper())
			var included2: Array = []
			var included_raw2: Variant = fields.get("includedDeathTypes", [])
			if typeof(included_raw2) == TYPE_ARRAY:
				for item3 in included_raw2 as Array:
					included2.append(String(item3).to_upper())
			# ALL [-excluded] and NONE [+included] are both executable filters.
			if creation_list != "" and death_types2 in ["ALL", "NONE"]:
				row["create_object_die"] = true
				row["create_object_die_policy"] = {
					"creation_list": creation_list,
					"death_types": death_types2,
					"excluded_death_types": excluded2,
					"included_death_types": included2,
				}
		# AttributeModifierUpgrade / GeometryUpgrade ledgers for upgrade grants.
		if executable and (
			folded.contains("attributemodifierupgrade")
			or folded.contains("geometryupgrade")
		):
			var upgrade_rows: Array = row.get("module_upgrade_contracts", []) as Array
			upgrade_rows.append({
				"module": module_name,
				"fields": fields.duplicate(true),
				"executable": true,
			})
			row["module_upgrade_contracts"] = upgrade_rows


func module_contracts_for_unit_type(unit_type: String) -> Array:
	return (_unit_module_contracts.get(unit_type, []) as Array).duplicate(true)


func register_structure_module_contracts(key: String, contracts: Array) -> void:
	## Index structure-carried moduleContracts (KeepObjectDie/CreateObjectDie).
	if key.strip_edges() == "" or contracts.is_empty():
		return
	_structure_module_contracts[key] = contracts.duplicate(true)


func _configure_playable_structure_module_contracts() -> void:
	## Pull structure moduleContracts from ContentDB when the autoload is live.
	## Absent ContentDB (headless fixtures) leaves the table empty.
	var db = _content_db_ref()
	if db == null:
		return
	var runtimes_value: Variant = db.get("playable_structure_runtimes")
	if typeof(runtimes_value) != TYPE_DICTIONARY:
		return
	var runtimes: Dictionary = runtimes_value
	for object_id_value in runtimes.keys():
		var document_value: Variant = runtimes[object_id_value]
		if typeof(document_value) != TYPE_DICTIONARY:
			continue
		var document: Dictionary = document_value
		var contracts := PlayableUnitAdapter.module_contracts(document)
		if contracts.is_empty():
			continue
		register_structure_module_contracts(String(object_id_value), contracts)
		register_castle_upgrade_grants(String(document.get("objectId", object_id_value)), contracts)
		var slug := String(document.get("slug", ""))
		if slug != "":
			register_structure_module_contracts(slug, contracts)


func register_castle_upgrade_grants(source_object_id: String, contracts: Array) -> void:
	## Index every CastleUpgrade row (retail's trigger -> real upgrade hop) from
	## one structure's projected moduleContracts. Rows missing either half are
	## skipped: a half-recorded contract must never invent an upgrade id.
	for contract_value in contracts:
		if typeof(contract_value) != TYPE_DICTIONARY:
			continue
		var contract: Dictionary = contract_value
		if String(contract.get("module", "")).to_lower() != "castleupgrade":
			continue
		var fields: Dictionary = contract.get("fields", {}) as Dictionary
		var trigger := _castle_upgrade_field(fields, "TriggeredBy")
		var granted := _castle_upgrade_field(fields, "Upgrade")
		if trigger == "" or granted == "":
			continue
		var radius_text := _castle_upgrade_field(fields, "WallUpgradeRadius")
		var rows: Array = _castle_upgrade_grants.get(trigger.to_lower(), []) as Array
		var already_recorded := false
		for existing_value in rows:
			if String((existing_value as Dictionary).get("upgrade_id", "")) == granted:
				already_recorded = true
				break
		if already_recorded:
			continue
		rows.append({
			"upgrade_id": granted,
			"wall_upgrade_radius": radius_text,
			"source_object_id": source_object_id,
			"tag": String(contract.get("tag", "")),
		})
		_castle_upgrade_grants[trigger.to_lower()] = rows


static func _castle_upgrade_field(fields: Dictionary, key: String) -> String:
	## Opaque-authored moduleContracts record {authored, sourceIni, line} per
	## field; typed rows add "value". Either shape resolves to the authored id.
	var raw: Variant = fields.get(key, fields.get(key.to_lower(), null))
	if typeof(raw) == TYPE_DICTIONARY:
		var row := raw as Dictionary
		var authored := String(row.get("authored", ""))
		if authored != "":
			return authored.strip_edges()
		return String(row.get("value", "")).strip_edges()
	if typeof(raw) in [TYPE_STRING, TYPE_STRING_NAME]:
		return String(raw).strip_edges()
	return ""


func castle_upgrade_grants_for(trigger_upgrade_id: String) -> Array:
	## The real upgrade(s) a bought trigger hands out. Empty when the selected
	## packs record no CastleUpgrade module for that trigger.
	return (_castle_upgrade_grants.get(trigger_upgrade_id.to_lower(), []) as Array).duplicate(true)


func _apply_castle_upgrade_grants(building: Dictionary, trigger_upgrade_id: String) -> void:
	## Retail's CastleUpgrade pass-out: the fortress takes the real upgrade, and
	## so does every castle piece it owns (retail scopes wall improvements by
	## WallUpgradeRadius; the castle pieces ARE the fortress's own walls, so the
	## owning-fortress set is that radius exactly and needs no distance guess).
	var grants := castle_upgrade_grants_for(trigger_upgrade_id)
	if grants.is_empty():
		return
	var structure_id := int(building.get("id", 0))
	var recipients: Array[int] = [structure_id]
	for piece_id_value in building.get("castle_piece_structure_ids", []) as Array:
		recipients.append(int(piece_id_value))
	for grant_value in grants:
		var granted := String((grant_value as Dictionary).get("upgrade_id", ""))
		if granted == "":
			continue
		for recipient_id in recipients:
			if not structures.has(recipient_id):
				continue
			var recipient: Dictionary = structures[recipient_id]
			var owned: Array = recipient.get("completed_upgrades", [])
			if owned.has(granted):
				continue
			owned.append(granted)
			recipient["completed_upgrades"] = owned
		_emit_event("upgrade.castle_granted", structure_id, 0, {
			"team": int(building.get("team", -1)),
			"trigger_upgrade_id": trigger_upgrade_id,
			"upgrade_id": granted,
			"recipient_count": recipients.size(),
		})


func _content_db_ref():
	# ContentDB is a project autoload Node (not Engine.has_singleton).
	var tree := Engine.get_main_loop() as SceneTree
	if tree == null or tree.root == null:
		return null
	return tree.root.get_node_or_null("/root/ContentDB")


func _attach_structure_module_contracts(row: Dictionary) -> void:
	## Attach structure moduleContracts for death consumers (CreateObjectDie).
	## Does NOT write into _unit_module_contracts (that table is unit-scoped).
	var keys: Array = [
		String(row.get("object_id", "")),
		String(row.get("structure_kind", "")),
		String(row.get("kind", "")),
		String(row.get("building_type", "")),
	]
	var contracts: Array = []
	for key_value in keys:
		var key := String(key_value)
		if key != "" and _structure_module_contracts.has(key):
			contracts = (_structure_module_contracts[key] as Array).duplicate(true)
			break
	if contracts.is_empty():
		return
	row["module_contracts"] = contracts
	for contract_value in contracts:
		if typeof(contract_value) != TYPE_DICTIONARY:
			continue
		var contract := contract_value as Dictionary
		var module_name := String(contract.get("module", ""))
		var fields: Dictionary = contract.get("fields", {}) as Dictionary
		var folded := module_name.to_lower()
		var executable := bool(contract.get("executable", false))
		if not executable and String(contract.get("runtime_status", "")) == "executable":
			executable = true
		if executable and folded.contains("createobjectdie"):
			var creation_list := ""
			var cl_raw: Variant = fields.get("CreationList", fields.get("creationList", {}))
			if typeof(cl_raw) == TYPE_DICTIONARY:
				creation_list = String((cl_raw as Dictionary).get("value", ""))
			elif typeof(cl_raw) == TYPE_STRING:
				creation_list = String(cl_raw)
			var death_types2 := String(fields.get("deathTypes", "ALL"))
			var excluded2: Array = []
			var excluded_raw2: Variant = fields.get("excludedDeathTypes", [])
			if typeof(excluded_raw2) == TYPE_ARRAY:
				for item2 in excluded_raw2 as Array:
					excluded2.append(String(item2).to_upper())
			var included2: Array = []
			var included_raw2: Variant = fields.get("includedDeathTypes", [])
			if typeof(included_raw2) == TYPE_ARRAY:
				for item3 in included_raw2 as Array:
					included2.append(String(item3).to_upper())
			if creation_list != "" and death_types2 in ["ALL", "NONE"]:
				row["create_object_die"] = true
				row["create_object_die_policy"] = {
					"creation_list": creation_list,
					"death_types": death_types2,
					"excluded_death_types": excluded2,
					"included_death_types": included2,
				}
		if executable and folded.contains("keepobjectdie"):
			var destroy_on_death := true
			if fields.has("destroyOnDeath"):
				destroy_on_death = bool(fields.get("destroyOnDeath", true))
			if not destroy_on_death:
				row["keep_object_die"] = true
				row["keep_object_die_policy"] = {
					"death_types": String(fields.get("deathTypes", "ALL")),
					"excluded_death_types": fields.get("excludedDeathTypes", []),
				}


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
	for level_value in Array(rule.get("levels", [])):
		var level_row := level_value as Dictionary
		if int(level_row.get("rank", 0)) == int(row["level"]):
			_apply_experience_level_effects(row, level_row)


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
	# Award stats count at the lethal hook before XP's authored-evidence early
	# returns. A victim with no ExperienceLevel still counts as a retail kill.
	_record_cah_member_kill(attacker_id, target)
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
	var victim_level_row := _experience_level_row(victim_rule, victim_level)
	if victim_level_row.get("experience_award_known") == false:
		_experience_unauthored_victims[String(target.get("unit_type", ""))] = true
		return
	var award := int(victim_level_row.get("experience_award", 0))
	if award <= 0:
		return
	_award_experience(attacker, award)


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
		_refresh_banner_carrier_state(row)
		_record_hero_rank_attainment(row)
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
	var damage_multiplier := float(level_row.get("damage_multiplier", 1.0))
	var spell_damage_multiplier := float(level_row.get("spell_damage_multiplier", 1.0))
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
	if damage_add != 0 or damage_multiplier != 1.0:
		row["member_damage"] = roundi(
			float(int(row.get("member_damage", 0)) + damage_add)
			* damage_multiplier
		)
		row["damage"] = int(row["member_damage"]) * int(row.get("member_count", 1))
	if spell_damage_multiplier != 1.0:
		row["spell_damage_multiplier"] = (
			float(row.get("spell_damage_multiplier", 1.0))
			* spell_damage_multiplier
		)
	var applied: Dictionary = row.get("applied_upgrades", {}) as Dictionary
	for upgrade_value in Array(level_row.get("upgrades", [])):
		var upgrade_id := String(upgrade_value)
		if upgrade_id != "":
			applied[upgrade_id] = tick_index
	row["applied_upgrades"] = applied


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
		_:
			return {"ok": false, "reason": "no-effect"}
	if not bool(result.get("ok", false)):
		return result
	if effect_kind != "stealth-toggle":
		# Retail InvisibilityNugget ForbiddenConditions: casting another
		# ability while cloaked drops a USING_ABILITY-forbidden stealth.
		_break_stealth(row, "USING_ABILITY")
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


func _ability_fx_list_ids(effect: Dictionary) -> Array:
	## Authored FXList ids on a converted ability leaf, in a stable order. The
	## converter emits these under kind-specific keys (fireFxId on weapon
	## leaves, healFxId on heals, levelFxId on level grants); nothing is
	## synthesised when a leaf authors none.
	var ids: Array = []
	for key in ["fireFxId", "healFxId", "levelFxId", "fxId"]:
		var value := String(effect.get(key, ""))
		if value != "" and not ids.has(value):
			ids.append(value)
	return ids


func _ability_fx_radius(effect: Dictionary) -> float:
	## The largest map-scaled radius this ability actually acts over, so the
	## presentation cue covers exactly the ground the sim affected. Ability
	## kinds that author no radius return 0 and get no radial cue.
	var radius := 0.0
	for key in ["knockback_radius", "damage_radius", "radius_scaled", "target_radius_scaled"]:
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
	var resolved := toggle_mode if engaged else String(hero_row.get("default_weapon_mode", "default"))
	if not _apply_weapon_mode(hero_row, resolved):
		return {"ok": false, "reason": "weapon-mode-unavailable:%s" % toggle_mode}
	hero_row["weapon_toggle_mode"] = toggle_mode if engaged else ""
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
		if (
			String(hero_row.get("weapon_toggle_mode", "")) == "mounted"
			and not _apply_weapon_mode(
				hero_row,
				String(hero_row.get("default_weapon_mode", "default"))
			)
		):
			return {"ok": false, "reason": "weapon-mode-unavailable:default"}
		hero_row["speed"] = float(hero_row.get("dismounted_speed", hero_row.get("speed", 0.0)))
		hero_row["speed_source"] = float(hero_row.get("dismounted_speed_source", hero_row.get("speed_source", 0.0)))
		hero_row.erase("dismounted_speed")
		hero_row.erase("dismounted_speed_source")
		if String(hero_row.get("weapon_toggle_mode", "")) == "mounted":
			hero_row["weapon_toggle_mode"] = ""
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
		if not _apply_weapon_mode(hero_row, mode):
			var restored_speed := float(hero_row.get("dismounted_speed", hero_row.get("speed", 0.0)))
			var restored_speed_source := float(hero_row.get("dismounted_speed_source", hero_row.get("speed_source", 0.0)))
			hero_row.erase("dismounted_speed")
			hero_row.erase("dismounted_speed_source")
			hero_row["speed"] = restored_speed
			hero_row["speed_source"] = restored_speed_source
			return {"ok": false, "reason": "weapon-mode-unavailable:%s" % mode}
		hero_row["weapon_toggle_mode"] = mode
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
		_award_auto_deposit_capture(structure, team)
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
	## Converted ObjectCreationList summon: each created object must resolve to
	## a converted unit rule or the cast fails closed (never a stand-in).
	var team := int(hero_row.get("team", -1))
	# Retail hero summons hatch: the ability's ObjectCreationList creates a
	# model-less egg (AragornArmyofTheDeadSmallEgg) whose SlowDeathBehavior OCL
	# spawns the real battalions. `effect.objects` names only that egg, and no
	# playable-unit document ever describes it, so the legacy lookup below
	# always failed closed and the power did nothing at all. When the converter
	# published the ability's leaf closure, walk the same egg -> hatch -> horde
	# -> member chain the spellbook powers already walk.
	var chained := _apply_ability_summon_chain(team, effect, point)
	if not chained.is_empty():
		return chained
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


func _apply_ability_summon_chain(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	## Resolve a hero summon through its converted leaf closure with the proven
	## spellbook OCL machinery. Returns {} when the ability carries no closure
	## (older documents) so the caller keeps its playable-unit lookup, and a
	## fail-closed result carrying the exact leaf gap when the closure is
	## present but the chain does not convert — never a stand-in summon.
	var leaves: Dictionary = effect.get("leaves", {}) as Dictionary
	if leaves.is_empty():
		return {}
	var ocl_id := String(effect.get("oclId", ""))
	if ocl_id == "":
		return {}
	var object_leaves: Dictionary = {}
	for object_value in Array(leaves.get("objects", [])):
		if typeof(object_value) == TYPE_DICTIONARY:
			object_leaves[String((object_value as Dictionary).get("id", ""))] = object_value
	var ocl_leaves: Dictionary = {}
	for ocl_value in Array(leaves.get("objectCreationLists", [])):
		if typeof(ocl_value) == TYPE_DICTIONARY:
			ocl_leaves[String((ocl_value as Dictionary).get("id", ""))] = ocl_value
	ingest_ocl_leaves_from_document({"leaves": leaves})
	var weapon_leaves: Dictionary = {}
	for weapon_value in Array(leaves.get("weapons", [])):
		if typeof(weapon_value) == TYPE_DICTIONARY:
			weapon_leaves[String((weapon_value as Dictionary).get("id", ""))] = weapon_value
	var modifier_leaves: Dictionary = {}
	for modifier_value in Array(leaves.get("attributeModifiers", [])):
		if typeof(modifier_value) == TYPE_DICTIONARY:
			modifier_leaves[String((modifier_value as Dictionary).get("id", ""))] = modifier_value
	var support := _spellbook_ocl_support(
		{}, {"objectCreationLists": [ocl_id]}, modifier_leaves, object_leaves, ocl_leaves, weapon_leaves
	)
	if not bool(support.get("ok", false)):
		return {"ok": false, "reason": "summon-chain-unconverted:%s" % String(support.get("reason", ""))}
	var resolved: Dictionary = support.get("effect", {}) as Dictionary
	if String(resolved.get("kind", "")) != "summon":
		# Fire-weapon receptacles and structure spawns are other powers' shapes;
		# a hero summon button never presents them.
		return {"ok": false, "reason": "summon-chain-unsupported-kind:%s" % String(resolved.get("kind", ""))}
	# Resolve declarative choice groups and schedule through the exact spellbook
	# cast path. This keeps hero and spellbook summons on one RNG/timing contract.
	var cast := _cast_spellbook_summon(team, resolved, point)
	if not bool(cast.get("ok", false)):
		return {"ok": false, "reason": "summon-chain-%s" % String(cast.get("reason", "cast-failed"))}
	var pending := int(cast.get("summon_count", 0))
	return {
		"ok": true,
		"reason": "",
		"effect": "summon",
		"affected": pending,
		"summon_count": pending,
		"summoned": [],
	}


func _summon_unit_type_for(source_object_id: String) -> String:
	var member_id := PlayableUnitAdapter.runtime_object_id(source_object_id)
	for unit_type_value in _unit_production_rules.keys():
		if String((_unit_production_rules[unit_type_value] as Dictionary).get("object_id", "")) == member_id:
			return String(unit_type_value)
	return ""


func _apply_ability_experience_grant(hero_row: Dictionary, effect: Dictionary, point: Vector2) -> Dictionary:
	## LevelGrantSpecialPower (King's Favor / Train Allies): every allied
	## battalion inside the authored RadiusEffect of the target point passing
	## the authored AcceptanceFilter gains exactly the authored Experience
	## through the normal experience pipeline (EXPERIENCE modifiers included).
	## Only recipients with a converted ExperienceLevel chain count — a unit
	## retail never authored a chain for is never granted an invented award.
	var team := int(hero_row.get("team", -1))
	var amount := int(effect.get("experience", 0))
	var radius := float(effect.get("radius_scaled", 0.0))
	if amount <= 0 or radius <= 0.0:
		return {"ok": false, "reason": "experience-grant-fields-missing"}
	var filter_text := String(effect.get("affects", ""))
	var granted := 0
	for id in living_ids(team):
		var ally: Dictionary = entities[id]
		if Vector2(ally.get("position", Vector2.ZERO)).distance_to(point) > radius:
			continue
		if not _ability_filter_accepts(ally, filter_text):
			continue
		if (_unit_experience_rules.get(String(ally.get("unit_type", "")), {}) as Dictionary).is_empty():
			continue
		_award_experience(ally, amount)
		granted += 1
	if granted == 0:
		return {"ok": false, "reason": "no-eligible-allies-in-radius"}
	return {"ok": true, "reason": "", "effect": "experience-grant", "affected": granted}


func _apply_ability_arrow_storm(hero_row: Dictionary, effect: Dictionary, point: Vector2) -> Dictionary:
	## ArrowStormUpdate barrage (Legolas Arrow Storm, Gandalf Lightning Sword):
	## the hero channels MaxShots weapon shots in ShotsPerBurst volleys on the
	## authored PersistentPrepTime cadence against enemies inside the authored
	## TargetRadius of the target point (deterministic round-robin, ascending
	## entity ids). Fail-closed: every consumed magnitude must be authored,
	## and a cast onto empty ground needs the authored CanShootEmptyGround.
	if not (hero_row.get("volley_channel", {}) as Dictionary).is_empty():
		return {"ok": false, "reason": "volley-in-progress"}
	var damage := int(effect.get("weaponDamage", 0))
	var radius := float(effect.get("target_radius_scaled", 0.0))
	var max_shots := int(effect.get("maxShots", 0))
	if damage <= 0 or radius <= 0.0 or max_shots <= 0:
		return {"ok": false, "reason": "arrow-storm-fields-missing"}
	var team := int(hero_row.get("team", -1))
	var can_shoot_empty_ground := bool(effect.get("canShootEmptyGround", false))
	if not can_shoot_empty_ground and _ability_enemies_near(team, point, radius).is_empty():
		return {"ok": false, "reason": "no-target"}
	hero_row["volley_channel"] = {
		"point": point,
		"radius": radius,
		"damage": damage,
		"shots_left": max_shots,
		"shots_fired": 0,
		"shots_per_burst": maxi(1, int(effect.get("shotsPerBurst", 1))),
		"interval_ticks": maxi(1, int(effect.get("shot_interval_ticks", 1))),
		"can_shoot_empty_ground": can_shoot_empty_ground,
		"next_shot_tick": tick_index + 1,
	}
	# Channeling holds the hero (same stance as the capture envelope): drop
	# any live order/target so a later order cancels the volley cleanly.
	hero_row["target_id"] = 0
	hero_row["target_kind"] = "battalion"
	hero_row["attack_windup"] = 0
	_clear_member_attack_schedule(hero_row)
	_clear_member_targets(hero_row)
	_clear_pending_route(hero_row, true)
	hero_row["state"] = "volley"
	return {"ok": true, "reason": "", "effect": "arrow-storm", "affected": 0}


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
	## ToggleHiddenSpecialAbilityUpdate / InvisibilitySpecialPower: cloak the
	## hero (and, when the module authors a BroadcastRadius, allies inside it)
	## for the authored EffectDuration. Stealthed battalions are skipped by
	## enemy auto-acquisition; the authored ForbiddenConditions break the
	## cloak early. Recasting the toggle drops it (retail toggle-hidden).
	var duration_ticks := int(effect.get("duration_ticks", 0))
	if duration_ticks <= 0:
		return {"ok": false, "reason": "stealth-fields-missing"}
	if _stealth_active(hero_row):
		_clear_stealth(hero_row)
		return {"ok": true, "reason": "", "effect": "stealth-toggle", "affected": 1, "engaged": false}
	var forbidden: Array = (effect.get("forbiddenConditions", []) as Array).duplicate()
	var until_tick := tick_index + duration_ticks
	_grant_stealth(hero_row, until_tick, forbidden)
	var affected := 1
	var broadcast := float(effect.get("broadcast_radius_scaled", 0.0))
	if broadcast > 0.0:
		var team := int(hero_row.get("team", -1))
		var origin := Vector2(hero_row.get("position", Vector2.ZERO))
		var filter_text := String(effect.get("affects", ""))
		for id in living_ids(team):
			if id == int(hero_row.get("id", 0)):
				continue
			var ally: Dictionary = entities[id]
			if Vector2(ally.get("position", Vector2.ZERO)).distance_to(origin) > broadcast:
				continue
			if not _ability_filter_accepts(ally, filter_text):
				continue
			_grant_stealth(ally, until_tick, forbidden)
			affected += 1
	return {"ok": true, "reason": "", "effect": "stealth-toggle", "affected": affected, "engaged": true}


func _stealth_active(row: Dictionary) -> bool:
	return tick_index < int(row.get("stealth_until_tick", -1))


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
	_clear_stealth(row)


func _apply_ability_teleport(hero_row: Dictionary, effect: Dictionary, point: Vector2) -> Dictionary:
	## TeleportSpecialAbilityUpdate (Shelob Tunnel): deterministic relocation
	## to a walkable point inside the authored MaxDistance (the generic range
	## gate enforces it), then the authored BusyForDuration holds the hero.
	if float(effect.get("range", 0.0)) <= 0.0:
		return {"ok": false, "reason": "teleport-fields-missing"}
	if not _position_walkable(point):
		return {"ok": false, "reason": "destination-unwalkable"}
	hero_row["position"] = point
	_spatial_sync(hero_row)
	hero_row["target_id"] = 0
	hero_row["target_kind"] = "battalion"
	hero_row["attack_windup"] = 0
	_clear_member_attack_schedule(hero_row)
	_clear_member_targets(hero_row)
	_clear_pending_route(hero_row, true)
	hero_row["current_speed"] = 0.0
	hero_row["state"] = "idle"
	var busy_ticks := int(effect.get("busy_ticks", 0))
	if busy_ticks > 0:
		hero_row["ability_hold_until_tick"] = tick_index + busy_ticks
	return {"ok": true, "reason": "", "effect": "teleport", "affected": 1}


func _apply_ability_curse(hero_row: Dictionary, effect: Dictionary, point: Vector2) -> Dictionary:
	## CurseSpecialPower (Hour of the Witch-King): the nearest enemy hero
	## inside the authored radius cursor loses the authored CursePercentage of
	## every ability recharge — its cooldowns restart scaled by the authored
	## percentage, never beyond one full authored recharge.
	var percentage := float(effect.get("cursePercentage", 0.0))
	var radius := float(effect.get("radius_scaled", 0.0))
	if percentage <= 0.0 or radius <= 0.0:
		return {"ok": false, "reason": "curse-fields-missing"}
	var team := int(hero_row.get("team", -1))
	var best_id := -1
	var best_distance := radius
	for id in _ability_enemies_near(team, point, radius):
		var target: Dictionary = entities[id]
		if String(target.get("category", "")) != "hero":
			continue
		var distance := Vector2(target.get("position", Vector2.ZERO)).distance_to(point)
		if distance <= best_distance:
			best_distance = distance
			best_id = id
	if best_id < 0:
		return {"ok": false, "reason": "no-enemy-hero-in-radius"}
	var target_row: Dictionary = entities[best_id]
	var states: Dictionary = target_row.get("ability_states", {}) as Dictionary
	var cursed := 0
	var ability_keys := states.keys()
	ability_keys.sort()
	for ability_key in ability_keys:
		var state: Dictionary = states[ability_key] as Dictionary
		var cooldown_ticks := int(state.get("cooldown_ticks", 0))
		if cooldown_ticks <= 0:
			continue
		var setback := tick_index + roundi(float(cooldown_ticks) * minf(percentage, 100.0) / 100.0)
		state["cooldown_ready_tick"] = maxi(int(state.get("cooldown_ready_tick", 0)), setback)
		states[ability_key] = state
		cursed += 1
	target_row["ability_states"] = states
	return {"ok": true, "reason": "", "effect": "curse", "affected": 1, "target_id": best_id, "cursed_abilities": cursed}


func _apply_ability_leadership_strip(hero_row: Dictionary, effect: Dictionary) -> Dictionary:
	## SpecialPowerModule AntiCategory=LEADERSHIP (Horn of Gondor): enemies
	## inside the authored AttributeModifierRange lose their leadership aura
	## grants and cannot receive new ones for the authored anti-category
	## duration (the paired ModifierList authors only that duration).
	var radius := float(effect.get("radius_scaled", 0.0))
	var duration_ticks := int(effect.get("duration_ticks", 0))
	if radius <= 0.0 or duration_ticks <= 0:
		return {"ok": false, "reason": "leadership-strip-fields-missing"}
	var team := int(hero_row.get("team", -1))
	var origin := Vector2(hero_row.get("position", Vector2.ZERO))
	var affected := 0
	for id in _ability_enemies_near(team, origin, radius):
		var target: Dictionary = entities[id]
		var table: Dictionary = target.get("timed_modifiers", {}) as Dictionary
		var stripped: Array[String] = []
		for key_value in table.keys():
			if String(key_value).begins_with("aura:"):
				stripped.append(String(key_value))
		stripped.sort()
		for key in stripped:
			table.erase(key)
		target["timed_modifiers"] = table
		# Same EXTEND contract as the weather anti-category lane.
		target["leadership_suppressed_until_tick"] = maxi(
			tick_index + duration_ticks,
			int(target.get("leadership_suppressed_until_tick", -1)),
		)
		affected += 1
	if affected == 0:
		return {"ok": false, "reason": "no-enemies-in-radius"}
	return {"ok": true, "reason": "", "effect": "leadership-strip", "affected": affected}


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
				if tick_index < int(ally.get("leadership_suppressed_until_tick", -1)):
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
		# Expiry sweeps for the per-row ability fields (exact tick, then the
		# field leaves the row so default rows never carry it).
		if row.has("stealth_until_tick") and tick_index >= int(row["stealth_until_tick"]):
			_clear_stealth(row)
		if row.has("leadership_suppressed_until_tick") and tick_index >= int(row["leadership_suppressed_until_tick"]):
			row.erase("leadership_suppressed_until_tick")
		if row.has("ability_hold_until_tick") and tick_index >= int(row["ability_hold_until_tick"]):
			row.erase("ability_hold_until_tick")
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
	# AutoDepositUpdate shares the authoritative economy step but retains each
	# module's own source-authored cadence. This is not the legacy farm timer:
	# descriptor-backed structures carry their next-payment frame in hashed
	# structure state, just as the SAGE module xfers m_depositOnFrame.
	_step_auto_deposit_updates()
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


func _initialize_structure_auto_deposit(structure: Dictionary) -> void:
	var team := int(structure.get("team", -1))
	var kind := String(structure.get("structure_kind", ""))
	var descriptor_team := team
	if structure.has("auto_deposit_descriptor_team"):
		descriptor_team = int(structure.get("auto_deposit_descriptor_team", -1))
	elif team == NEUTRAL_TEAM:
		# A map-authored neutral object has no controlling-player manifest from
		# which module data can be reconstructed. Bind only when every roster
		# manifest that defines this kind agrees on the exact authored rules;
		# cross-faction ambiguity must be resolved by the map/loader setting the
		# immutable auto_deposit_descriptor_team contract before creation.
		var resolved_rules: Array = []
		var resolved_team := -1
		for candidate_team in _roster_team_ids():
			var candidate: Array = (
				structure_auto_deposit_updates_for_team(candidate_team)
				.get(kind, []) as Array
			)
			if candidate.is_empty():
				continue
			if resolved_team < 0:
				resolved_team = candidate_team
				resolved_rules = candidate
			elif candidate != resolved_rules:
				configuration_error = (
					"neutral structure %d AutoDepositUpdate descriptor is ambiguous"
					% int(structure.get("id", 0))
				)
				structure.erase("auto_deposit_rules")
				structure.erase("auto_deposit_state")
				return
		descriptor_team = resolved_team
	if descriptor_team >= 0 and not _roster_team_ids().has(descriptor_team):
		configuration_error = (
			"structure %d AutoDepositUpdate descriptor team %d is not rostered"
			% [int(structure.get("id", 0)), descriptor_team]
		)
		structure.erase("auto_deposit_rules")
		structure.erase("auto_deposit_state")
		return
	var rules: Array = []
	if descriptor_team >= 0:
		rules = (
			structure_auto_deposit_updates_for_team(descriptor_team)
				.get(kind, []) as Array
		)
	if rules.is_empty():
		structure.erase("auto_deposit_rules")
		structure.erase("auto_deposit_state")
		return
	structure["auto_deposit_descriptor_team"] = descriptor_team
	structure["auto_deposit_rules"] = rules.duplicate(true)
	# The historical tiny-slice farm timer is a fallback for structures with no
	# executable descriptor. Once real AutoDeposit module data is bound, leaving
	# this non-zero would pay both clocks for the same structure.
	structure["income_per_payout"] = 0
	var states: Array[Dictionary] = []
	for rule_value in structure["auto_deposit_rules"] as Array:
		var rule := rule_value as Dictionary
		var timing := rule.get("depositTiming", {}) as Dictionary
		var interval := maxi(1, int(timing.get("simulationTicks", 0)))
		states.append(
			{
				"next_tick": tick_index + interval,
				"initialized": false,
				"capture_bonus_armed": false,
			}
		)
	structure["auto_deposit_state"] = states


func _auto_deposit_upgrade_boost(team: int, rule: Dictionary) -> int:
	var completed: Dictionary = team_upgrades.get(team, {}) as Dictionary
	# The retail module returns the first authored matching pair, not a sum.
	for boost_value in rule.get("upgradedBoosts", []) as Array:
		var boost := boost_value as Dictionary
		if String(boost.get("upgradeType", "")) != "PLAYER":
			configuration_error = (
				"AutoDepositUpdate boost '%s' is not a PLAYER upgrade"
				% String(boost.get("upgradeId", ""))
			)
			return 0
		if completed.has(String(boost.get("upgradeId", ""))):
			return int(boost.get("boost", 0))
	return 0


func _step_auto_deposit_updates() -> void:
	for structure_id in structure_ids():
		var structure: Dictionary = structures[structure_id]
		var team := int(structure.get("team", -1))
		var rules: Array = structure.get("auto_deposit_rules", []) as Array
		var states_value: Variant = structure.get("auto_deposit_state")
		if rules.is_empty() or typeof(states_value) != TYPE_ARRAY:
			continue
		var states := states_value as Array
		if states.size() != rules.size():
			configuration_error = (
				"structure %d AutoDepositUpdate state/rule count drifted"
				% structure_id
			)
			continue
		for index in rules.size():
			var rule := rules[index] as Dictionary
			var state := states[index] as Dictionary
			if tick_index < int(state.get("next_tick", tick_index + 1)):
				continue
			var interval := maxi(
				1,
				int(
					(rule.get("depositTiming", {}) as Dictionary)
					.get("simulationTicks", 0)
				)
			)
			state["next_tick"] = tick_index + interval
			if not bool(state.get("initialized", false)):
				state["initialized"] = true
				state["capture_bonus_armed"] = true
			var amount := int(
				(rule.get("depositAmount", {}) as Dictionary).get("value", 0)
			)
			if (
				not team_resources.has(team)
				or amount <= 0
				or int(structure.get("health", 0)) <= 0
				or float(structure.get("construction_progress", 0.0)) < 1.0
				or not bool(
					(rule.get("actualMoney", {}) as Dictionary)
					.get("value", true)
				)
			):
				continue
			amount += _auto_deposit_upgrade_boost(team, rule)
			team_resources[team] = resources_for_team(team) + amount
			_emit_event(
				"economy.auto_deposit",
				structure_id,
				0,
				{"team": team, "amount": amount, "module_index": index}
			)


func _award_auto_deposit_capture(
	structure: Dictionary, new_team: int
) -> int:
	var rules: Array = structure.get("auto_deposit_rules", []) as Array
	var states_value: Variant = structure.get("auto_deposit_state")
	if rules.is_empty() or typeof(states_value) != TYPE_ARRAY:
		return 0
	var states := states_value as Array
	if states.size() != rules.size():
		configuration_error = "captured AutoDepositUpdate state/rule count drifted"
		return 0
	var awarded := 0
	for index in rules.size():
		var rule := rules[index] as Dictionary
		var state := states[index] as Dictionary
		var interval := maxi(
			1,
			int(
				(rule.get("depositTiming", {}) as Dictionary)
				.get("simulationTicks", 0)
			)
		)
		# SAGE awardInitialCaptureBonus always restarts the regular cadence,
		# even when the one-shot is not armed or its amount is zero.
		state["next_tick"] = tick_index + interval
		var amount := int(
			(rule.get("initialCaptureBonus", {}) as Dictionary).get("value", 0)
		)
		if (
			not bool(state.get("capture_bonus_armed", false))
			or amount <= 0
			or not team_resources.has(new_team)
		):
			continue
		state["capture_bonus_armed"] = false
		team_resources[new_team] = resources_for_team(new_team) + amount
		awarded += amount
		_emit_event(
			"economy.auto_deposit_capture_bonus",
			int(structure.get("id", 0)),
			0,
			{"team": new_team, "amount": amount, "module_index": index}
		)
	return awarded


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
		var contract: Dictionary = structure_upgrade_contracts_for_team(int(building.get("team", -1))).get(upgrade_id, {})
		if contract.is_empty():
			configuration_error = "queued structure upgrade lost its contract"
			continue
		queue.pop_front()
		building["upgrade_queue"] = queue
		var completed: Array = building.get("completed_upgrades", [])
		if not completed.has(upgrade_id):
			completed.append(upgrade_id)
		building["completed_upgrades"] = completed
		# Retail's CastleUpgrade hop: a fortress improvement button buys the
		# *Trigger* upgrade, and the fortress's own CastleUpgrade module hands
		# the real upgrade to the castle. Without this the purchase completes
		# and nothing downstream ever fires.
		_apply_castle_upgrade_grants(building, upgrade_id)
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
		if not bool(contract.get("castle_upgrade", false)):
			# A fortress improvement never swaps the command set (retail keeps
			# the fortress on its own set and only flips model/weapon state), so
			# it must not blank the building's live set on completion.
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
			"command_set": String(building.get("command_set", "")),
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
		if bool(production_rule.get("is_ring_hero", false)):
			var ring_hero: Dictionary = entities[new_id]
			ring_hero["ring_hero"] = true
			ring_hero["level"] = 10
			var owned: Dictionary = team_upgrades.get(team, {}) as Dictionary
			owned.erase("Upgrade_RingHero")
			team_upgrades[team] = owned
			for team_structure_id in structure_ids(team):
				var team_structure: Dictionary = structures[team_structure_id]
				var producer_upgrades: Array = team_structure.get("completed_upgrades", [])
				producer_upgrades.erase("Upgrade_FortressRingHero")
				team_structure["completed_upgrades"] = producer_upgrades
			_emit_event("ring.hero_created", new_id, id, {"team": team, "rank": 10})
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
	# The carrier is a real damageable simulation entity, but its movement and
	# presentation are owned by its horde; it never runs independent orders/AI.
	if bool(row.get("is_banner_carrier", false)):
		return
	if int(row["health"]) <= 0:
		row["state"] = "death"
		_clear_pending_route(row, true)
		_rearm_mood_idle_cadence(row)
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
	_spatial_sync(row)
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
	# Cleave only reaches a small disc, so the old full hostile scan is a
	# neighbourhood query. Sorted: damage lands in ascending id order.
	var team := int(row.get("team", PLAYER_TEAM))
	for candidate in _spatial_gather_sorted(origin, HERO_CLEAVE_RADIUS):
		if candidate == primary_target_id or not entities.has(candidate):
			continue
		var candidate_row: Dictionary = entities[candidate]
		if int(candidate_row.get("health", 0)) <= 0 or not _is_hostile(team, int(candidate_row.get("team", -1))):
			continue
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
	return true


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
## (.private/scratch/opus29-footprint-census.txt, 196 documents that carry
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
## measured in .private/scratch/opus24-probe1.out.log): every castle piece
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
	## .private/scratch/opus24-probe1.out.log. Each piece carries the default
	## 2.8 STRUCTURE_BLOCK_RADIUS, so exempting only the ordered target left the
	## fortress ringed by a ~5.2-unit wall of its own sub-structures. Retail melee
	## ranges are ~11.5 source units = 0.305 sim, and the MOVEMENT ring is 5.2, so
	## no melee horde could ever reach a fortress or a pad — that gap is far wider
	## than the 1.9604 footprint the range gate now subtracts (round 20), so the
	## corridor is still required: they parked on the ring at distance 4.4-5.1 in state
	## `run` and only ranged units ever landed a blow
	## (.private/scratch/opus09-live1.out.log:35,52 — 17,521 ticks to kill a
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
	## Isen II, .private/scratch/opus24-probe1.out.log: fortress radius 4.6 at
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
	# `structure_id_list` lets a caller that deflects many entities in one pass
	# hoist the sorted id list out of its own loop (structure_ids() allocates and
	# SORTS on every call). Empty means "read it here".
	var attack_target_id := int(row.get("target_id", 0))
	var attack_target_kind := String(row.get("target_kind", "battalion"))
	var passable := _castle_footprint_pass_through(
		position,
		attack_target_id,
		attack_target_kind,
	)
	var ids: Array[int] = structure_id_list if not structure_id_list.is_empty() else structure_ids()
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
		var radius := float(STRUCTURE_BLOCK_RADIUS.get(String(structure_row.get("structure_kind", "")), 2.8))
		if passable.has(structure_id):
			# THE TARGET'S OWN FOOTPRINT STILL STOPS THE ATTACKER AT ITS WALL.
			#
			# The corridor exists because the MOVEMENT block radius is an inflated
			# walkway ring (a fortress is 4.6 against an authored footprint of
			# 1.9604) and a castle's pieces are authored INSIDE it, so leaving it
			# up walled melee out of every fortress it was ordered onto. But
			# opening it completely let the attacker walk to the target's CENTRE —
			# measured d=0.24 then d=0.00 in the live slice
			# (.private/scratch/opus24-probe2.out.log).
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
			position = _tangential_slide_point(center, seated_radius, direction, travel_step)
		if push_budget <= 0.0:
			break
	return position


func _tangential_slide_point(
	center: Vector2, radius: float, radial_direction: Vector2, travel_step: Vector2
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
	var cross := radial_direction.cross(travel_step)
	var side := signf(cross) if absf(cross) > 0.000001 else 1.0
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
		if int(row.get("health", 0)) <= 0 or bool(row.get("flying", false)):
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
			# (.private/scratch/opus26-pin-positions-{new,revert-prodexit}.json.tick*):
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
		# (.private/scratch/opus25-probe1.out.log:`3:idle:t2001:d0.00`) — the
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


## Fallback turn rate when a pack predates locomotor extraction.
## NormalMeleeHordeLocomotor authors TurnTime = 2000 (locomotor.ini:709), and
## OpenSAGE's shared SAGE core converts that as 360 / (TurnTime / 1000)
## (LocomotorTemplate.cs:94) -> 180 deg/s.
const RETAIL_FALLBACK_TURN_RATE_DEGREES := 180.0
## MaxTurnWithoutReform. Infantry/ranged horde locomotors author 45
## (locomotor.ini:717, :765, :805); every cavalry-class horde locomotor authors
## 100 (:849, :871, :893) so horse hordes wheel through far wider turns.
const RETAIL_MAX_TURN_WITHOUT_REFORM_DEGREES := 45.0
const RETAIL_CAVALRY_MAX_TURN_WITHOUT_REFORM_DEGREES := 100.0


func _retail_reform_threshold_degrees(row: Dictionary) -> float:
	## Per-row override first so a pack that later extracts MaxTurnWithoutReform
	## wins without touching this code; otherwise the authored class default.
	var authored := float(row.get("max_turn_without_reform_degrees", 0.0))
	if authored > 0.0:
		return authored
	if String(row.get("category", "")) == "cavalry":
		return RETAIL_CAVALRY_MAX_TURN_WITHOUT_REFORM_DEGREES
	return RETAIL_MAX_TURN_WITHOUT_REFORM_DEGREES


func _retail_turn_rate_degrees(row: Dictionary) -> float:
	var authored := float(row.get("turn_rate_degrees_per_second", 0.0))
	if authored > 0.0:
		return authored
	return RETAIL_FALLBACK_TURN_RATE_DEGREES


func _step_retail_heading(row: Dictionary, movement_direction: Vector2, braking: float) -> bool:
	## Rotate the horde's facing toward the direction of travel at the authored
	## TurnTime-derived rate, and answer whether the horde is REFORMING.
	##
	## The authored locomotor field MaxTurnWithoutReform splits turning in two.
	##
	## Inside the arc the horde WHEELS - it keeps advancing while it turns. That
	## is why every member class is authored a slightly faster MEMBER speed than
	## its HORDE speed: members on the outside of a wheel have further to travel
	## and must catch up.
	##
	## Beyond the arc it REFORMS - the horde stops, pivots about its own centre,
	## and members re-take their slots against the new heading. Melee horde
	## locomotors also author no turning while moving, so a reform is stationary
	## by construction.
	##
	## Deterministic: fixed TICK_SECONDS step, no wall clock, no RNG draw.
	var facing_now := Vector2(row.get("facing", movement_direction))
	if facing_now.length_squared() <= 0.000001:
		facing_now = movement_direction
	var delta_angle := wrapf(movement_direction.angle() - facing_now.angle(), -PI, PI)
	var turn_step := deg_to_rad(_retail_turn_rate_degrees(row)) * TICK_SECONDS
	row["facing"] = facing_now.rotated(clampf(delta_angle, -turn_step, turn_step))
	if absf(delta_angle) <= deg_to_rad(_retail_reform_threshold_degrees(row)):
		return false
	# Reform: bleed speed off the authored braking ramp rather than snapping to a
	# standing start, and hold position while the pivot completes.
	row["current_speed"] = maxf(0.0, float(row.get("current_speed", 0.0)) - braking * TICK_SECONDS)
	row["route_stall_ticks"] = 0
	return true


func _step_route(row: Dictionary) -> void:
	var route: Array = row["route"]
	if route.is_empty():
		# Brake toward stop when no route remains.
		var idle_speed := maxf(0.0, float(row.get("current_speed", 0.0)) - float(row.get("braking", float(row.get("speed", 0.0)))) * TICK_SECONDS)
		row["current_speed"] = idle_speed
		return
	var position := Vector2(row["position"])
	var waypoint := Vector2(route[0])
	var base_speed := float(row["speed"])
	if retail_formation_movement:
		# WaitForFormation (locomotor.ini:713) - a group order advances at the
		# slowest authored speed in the group so the selection arrives together
		# instead of stringing out by unit class.
		var group_cap := float(row.get("group_speed_cap", 0.0))
		if group_cap > 0.0:
			base_speed = minf(base_speed, group_cap)
	var max_speed := base_speed * float(_stance_state(row).get("speedMultiplier", 1.0)) * float(_formation_effects(row).get("speed_multiplier", 1.0)) * _ability_speed_multiplier(row)
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
		if retail_formation_movement:
			if _step_retail_heading(row, movement_direction, braking):
				# Reforming: the horde pivots about its own centre and does not
				# translate this tick.
				return
		else:
			row["facing"] = movement_direction
	var pre_move_gap := position.distance_to(waypoint)
	var travel_step := Vector2.ZERO
	if pre_move_gap <= maxf(step_distance, 0.001):
		travel_step = waypoint - position
		position = waypoint
		route.pop_front()
	else:
		travel_step = position.direction_to(waypoint) * step_distance
		position += travel_step
	if not bool(row.get("flying", false)):
		# Flyers pass straight over building footprints. The step that produced
		# this position is threaded in so a footprint the unit is walking THROUGH
		# slides it tangentially around the disc rather than standing it off
		# radially — see _tangential_slide_point for the deadlock that fixes.
		position = _deflect_around_structures(position, row, travel_step)
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
	_spatial_sync(row)
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
	var best_id := _spatial_nearest_hostile(
		row, team, origin, TRAMPLE_COLLISION_RADIUS, SPATIAL_FILTER_NOT_FLYING
	)
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
			if _assign_route(row, Vector2(target["position"])):
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


func _apply_knockback(center: Vector2, radius: float, strength: float, source_team: int, damage: int, damage_reason: String, source_id: int = 0) -> int:
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
	if bool(target.get("highlander_body", false)) and damage_type.to_lower() != "unresistable":
		var target_member := _choose_target_member(
			target, attacker_id, 0, int(target.get("attack_sequence", 0))
		)
		if target_member >= 0:
			_apply_member_damage(
				attacker_id,
				-1,
				target_id,
				remaining,
				"battalion",
				int(target.get("attack_sequence", 0)),
				target_member,
			)
		return
	while remaining > 0 and int(target.get("health", 0)) > 0:
		var target_member := _choose_target_member(target, attacker_id, 0, int(target.get("attack_sequence", 0)))
		if target_member < 0:
			break
		var health_values: Array = target.get("member_health", [])
		# Each member must receive exactly the raw amount its compiled factors
		# reduce to a kill; capping at current member health would grind
		# geometrically against sub-1.0 armor and stall short of lethal.
		# No override on this path: the attacker's own authored mix applies.
		var factor := _incoming_damage_factor(attacker_id, target, "battalion", damage_type, _damage_components_for(attacker_id, ""))
		if factor <= 0.0:
			# A 0% armor match (e.g. LOGICAL_FIRE vs fortress) makes the hit a
			# retail no-op; the remaining raw damage has no path through.
			break
		var applied := mini(remaining, maxi(1, ceili(float(health_values[target_member]) / factor)))
		_apply_member_damage(attacker_id, -1, target_id, applied, "battalion", int(target.get("attack_sequence", 0)), target_member)
		remaining -= applied


func _incoming_damage_factor(attacker_id: int, target: Dictionary, target_kind: String, damage_type: String, components: Array = []) -> float:
	## The full compiled multiplier an incoming hit applies before rounding:
	## weapon DamageScalar target filters, the armor.ini set (with recorded
	## applied armor upgrades), stance, formation, and ability/aura factors.
	var weapon_factor := 1.0
	if entities.has(attacker_id):
		var attacker_effect := _applied_weapon_effect(entities[attacker_id] as Dictionary)
		if not attacker_effect.is_empty():
			weapon_factor = _damage_scalar_factor(attacker_effect.get("scalars", []) as Array, target, target_kind)
	return weapon_factor * _member_body_damage_factor(
		target, damage_type, components
	)


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
	death_type: String = "NORMAL"
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
	# A multi-nugget weapon resolves each component against its own armor
	# column; an explicit override replaces the mix rather than blending in.
	var damage_components := _damage_components_for(attacker_id, damage_type_override)
	var weapon_factor := 1.0
	if entities.has(attacker_id):
		var attacker: Dictionary = entities[attacker_id]
		if damage_type == "":
			damage_type = String(attacker.get("damage_type", ""))
		var attacker_effect := _applied_weapon_effect(attacker)
		if not attacker_effect.is_empty():
			weapon_factor = _damage_scalar_factor(attacker_effect.get("scalars", []) as Array, target, target_kind)
	var rally_factor := 1.0
	if (
		entities.has(attacker_id)
		and tick_index
			< int((entities[attacker_id] as Dictionary).get(
				"rally_until_tick", -1
			))
	):
		rally_factor = float(
			(entities[attacker_id] as Dictionary).get("rally_damage_mult", 1.5)
		)
	var stance_adjusted_amount := 0
	if bool(target.get("highlander_body", false)):
		var highlander_amount := _highlander_raw_damage_amount(
			float(amount) * weapon_factor * rally_factor,
			prior_health,
			damage_type,
			damage_components,
		)
		if highlander_amount < 0:
			_emit_event("combat.damage_refused", attacker_id, target_id, {
				"reason": "highlander-mixed-unresistable",
				"target": "member %d of %s" % [
					target_member, String(target.get("object_id", ""))
				],
			})
			return
		stance_adjusted_amount = maxi(0, roundi(
			highlander_amount
			* _member_body_damage_factor(target, damage_type, damage_components)
		))
	else:
		stance_adjusted_amount = maxi(0, roundi(
			float(amount)
			* _incoming_damage_factor(
				attacker_id,
				target,
				target_kind,
				damage_type,
				damage_components,
			)
		))
	if rally_factor != 1.0 and not bool(target.get("highlander_body", false)):
		# Rallying Call: the converted SpellBookRallyingCallModifier leaf
		# (DAMAGE_MULT 150%, 60s) rides the row from cast time until expiry.
		stance_adjusted_amount = int(ceil(stance_adjusted_amount * rally_factor))
	health_values[target_member] = maxi(0, prior_health - stance_adjusted_amount)
	target["member_health"] = health_values
	target["last_damage_tick"] = tick_index
	# Authored InvisibilityNugget ForbiddenConditions: the hit breaks a
	# TAKING_DAMAGE-forbidden cloak on the victim and a FIRING_ANY-forbidden
	# cloak on the attacker.
	_break_stealth(target, "TAKING_DAMAGE")
	if entities.has(attacker_id):
		_break_stealth(entities[attacker_id] as Dictionary, "FIRING_ANY")
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
		"armor_scalar": _member_armor_scalar(target, damage_type, damage_components),
		"weapon_factor": weapon_factor,
	})
	var defeated_members: Array[int] = []
	if prior_health > 0 and int(health_values[target_member]) == 0:
		defeated_members.append(target_member)
	var death_policy: Dictionary = {}
	if int(target["health"]) == 0:
		death_policy = _bookkeep_battalion_death(
			target_id, target, death_type, defeated_members, attacker_id
		)
	else:
		death_policy = _apply_playable_unit_death_policy(
			target, death_type, defeated_members
		)
	if not defeated_members.is_empty():
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
		# Banner carriers notify the owning horde before ordinary kill bookkeeping.
		if bool(target.get("is_banner_carrier", false)):
			_on_banner_carrier_defeated(target)
		_emit_event("battalion.defeated", attacker_id, target_id, {
			"object_id": String(target.get("object_id", "")),
			"team": int(target.get("team", -1)),
			"category": String(target.get("category", "")),
		})
		if bool(death_policy.get("destroy_object", false)) or bool(target.get("is_banner_carrier", false)):
			# SAGE DestroyDie::onDie calls destroyObject in the death callback;
			# it does not enter the ordinary readable-corpse lifetime.
			# Banner carriers are presentation/sim attachments without corpses.
			entities.erase(target_id)


func _bookkeep_battalion_death(
	entity_id: int, row: Dictionary, death_type: String, defeated_members: Array[int],
	attacker_id: int = 0
) -> Dictionary:
	## Shared authoritative bookkeeping for every battalion lethal path. Callers
	## retain path-specific kill credit/events, but lifetime, corpse policy,
	## selection, routing, and command-point release cannot drift apart.
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
	if not base_loop_enabled or bool(row.get("command_points_released", false)):
		return
	row["command_points_released"] = true
	var row_team := int(row.get("team", -1))
	team_command_points[row_team] = maxi(
		0,
		command_points_for_team(row_team)
		- _entity_command_point_commitment(row)
	)


func _entity_command_point_commitment(row: Dictionary) -> int:
	return maxi(
		0,
		int(row.get("command_points", _rules.get("soldier_command_points", 60)))
	)


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
		# A matched DestroyDie used to erase the object on the SAME tick, which
		# handed the simulation an effective fade window of 0. Retail authors
		# that window as SlowDeathBehavior DestructionDelay on a
		# `DeathTypes = NONE +FADED` module (pure-retail range 1000..10000 ms).
		# When the pack carries the authored delay for THIS death type, the
		# object stays until it elapses; with no authored delay the immediate
		# removal is unchanged, so a pack that predates the emission behaves
		# exactly as before.
		var fade_ticks := _slow_death_fade_ticks(row, death_type) if destroy_object else 0
		row["corpse_expire_tick"] = (
			tick_index + fade_ticks if destroy_object else tick_index + CORPSE_LIFETIME_TICKS
		)
		_consume_create_object_die(row, death_type)
	return {
		"destroy_object": destroy_object,
	}


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
				var result: Dictionary = script_spawn_entity(
					object_name, team, at + offset
				)
				if bool(result.get("ok", false)):
					spawned.append(int(result.get("entity_id", 0)))
					_emit_event(
						"module.create_object_die.hatch",
						int(result.get("entity_id", 0)),
						int(entry.get("source_entity", 0)),
						{"ocl": ocl_id, "object": object_name}
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


func _apply_structure_damage(attacker_id: int, target_id: int, amount: int, damage_type_override: String = "") -> void:
	if not structures.has(target_id):
		return
	var target: Dictionary = structures[target_id]
	if int(target.get("health", 0)) <= 0:
		return
	var damage_type := damage_type_override
	var damage_components := _damage_components_for(attacker_id, damage_type_override)
	if damage_type == "":
		damage_type = "default"
		if entities.has(attacker_id):
			damage_type = String((entities[attacker_id] as Dictionary).get("damage_type", "default"))
			if damage_type == "" and not damage_components.is_empty():
				# Typed per component, not untyped: name the mix so the
				# per-type remainder accumulator keeps it separate.
				damage_type = "mixed"
	var weapon_factor := 1.0
	if entities.has(attacker_id):
		var attacker_effect := _applied_weapon_effect(
			entities[attacker_id] as Dictionary
		)
		if not attacker_effect.is_empty():
			weapon_factor = _damage_scalar_factor(
				attacker_effect.get("scalars", []) as Array,
				target,
				"structure",
			)
	var body_amount := float(maxi(0, amount)) * weapon_factor
	if _structure_uses_highlander_body(target):
		var highlander_amount := _highlander_raw_damage_amount(
			body_amount,
			int(target.get("health", 0)),
			damage_type,
			damage_components,
		)
		if highlander_amount < 0:
			_emit_event("combat.damage_refused", attacker_id, target_id, {
				"reason": "highlander-mixed-unresistable",
				"target": "structure %s" % String(target.get("structure_kind", "")),
			})
			return
		body_amount = highlander_amount
	# Every structure kind scales by its own compiled armor.ini table; kinds
	# without one use the recorded provisional (logged once at configure).
	var kind := String(target.get("structure_kind", ""))
	var table: Dictionary = _structure_armor.get(kind, {})
	var scalar := STRUCTURE_ARMOR_PROVISIONAL_SCALAR
	if not table.is_empty():
		var scalars: Dictionary = table.get("scalars", {})
		if not damage_components.is_empty():
			scalar = float(table.get("damage_scalar", 1.0)) * _weighted_armor_scalar(scalars, damage_components, damage_type)
		else:
			scalar = float(table.get("damage_scalar", 1.0)) * float(scalars.get(damage_type.to_lower(), scalars.get("default", 1.0)))
	var remainder_by_type: Dictionary = target.get("damage_remainders", {})
	var accumulated := (
		float(remainder_by_type.get(damage_type, 0.0))
		+ body_amount * scalar
	)
	var applied := floori(accumulated)
	remainder_by_type[damage_type] = accumulated - float(applied)
	target["damage_remainders"] = remainder_by_type
	if applied <= 0:
		return
	if entities.has(attacker_id):
		# Firing on a structure breaks a FIRING_ANY-forbidden cloak too.
		_break_stealth(entities[attacker_id] as Dictionary, "FIRING_ANY")
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
		_record_cah_structure_kill(attacker_id, target)
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
		# Structure-carried CreateObjectDie (debris/refund eggs) when contracts
		# were attached at spawn or via register_structure_module_contracts.
		if not bool(target.get("create_object_die", false)):
			_attach_structure_module_contracts(target)
		_consume_create_object_die(target, "NORMAL")
		_emit_event("structure.destroyed", attacker_id, target_id, {"structure_kind": structure_kind, "team": int(target.get("team", -1))})
		if int(target.get("team", -1)) == PLAYER_TEAM:
			_emit_event("eva.building_lost", 0, target_id, {"team": PLAYER_TEAM, "structure_kind": structure_kind})
		if creep_lairs_enabled and int(target.get("team", -1)) == CREEP_TEAM:
			_on_creep_structure_destroyed(attacker_id, target_id)


func _target_alive(target_id: int, target_kind: String) -> bool:
	if target_kind == "structure":
		return structures.has(target_id) and int((structures[target_id] as Dictionary).get("health", 0)) > 0
	return entities.has(target_id) and int((entities[target_id] as Dictionary).get("health", 0)) > 0


func _target_position(target_id: int, target_kind: String) -> Vector2:
	var row: Dictionary = structures.get(target_id, {}) if target_kind == "structure" else entities.get(target_id, {})
	return Vector2(row.get("position", Vector2.ZERO))


# ---------------------------------------------------------------------------
# Neutral creep lairs (retail PlyrCreeps camps). Opt-in via the
# "enable_creep_lairs" gameplay rule; every path below is unreachable when it
# is off, keeping the default match byte-identical. Retail semantics per the
# measured creep contract: 2000 HP lair seeds SpawnBehavior guards, replaces
# dead guards on the family's SpawnReplaceDelay, exposes a 500 HP hole on
# death (RebuildHoleExposeDie), the hole regrows the lair after 120 s
# (RebuildHoleBehavior) unless destroyed, and hole death drops the family's
# treasure OCL. v0 treasure shape: the chest values are credited directly to
# the killing team on hole death (SalvageCrateCollide walk-over pickup and the
# chest crate model are follow-ups; values stay the authored 160-200 band).
# ---------------------------------------------------------------------------


func _creep_scale() -> float:
	## Source→sim scale for the measured SAGE ranges (CREEP_VISION, leashes).
	var scale := float(_rules.get("source_map_transform_scale", 0.0))
	return scale if scale > 0.0 else 1.0


func _creep_family_for(type_name: String) -> Dictionary:
	var family_name := String(CREEP_LAIR_FAMILY_ALIASES.get(type_name, type_name))
	var family: Dictionary = CREEP_LAIR_FAMILIES.get(family_name, {}) as Dictionary
	if family.is_empty():
		return {}
	var row := family.duplicate(true)
	row["family"] = family_name
	return row


func _register_creep_guard_rules() -> void:
	## Synthesized creep guard unit rules (the trebuchet-contract pattern):
	## measured chassis numbers, recorded-provisional weapon/locomotor values.
	## Registered only when creeps are enabled, so default rules never move.
	var scale := _creep_scale()
	var configured_unit_rules: Dictionary = _rules.get("unit_rules", {}) as Dictionary
	var guard_object_ids: Array = CREEP_GUARD_STATS.keys()
	guard_object_ids.sort()
	for guard_object_value in guard_object_ids:
		var guard_object_id := String(guard_object_value)
		if configured_unit_rules.has(guard_object_id):
			continue
		var stats: Dictionary = CREEP_GUARD_STATS[guard_object_id]
		var speed_source := float(stats["speed"])
		var attack_range_source := float(stats["attack_range"])
		var delay_ms := float(stats["delay_ms"])
		configured_unit_rules[guard_object_id] = {
			"horde_id": guard_object_id,
			"member_count": 1,
			"member_health": int(stats["health"]),
			"member_damage": int(stats["damage"]),
			"speed": speed_source * scale,
			"speed_source": speed_source,
			"acceleration": speed_source * scale,
			"acceleration_source": speed_source,
			"turn_rate_degrees_per_second": 360.0,
			"braking": speed_source * scale,
			"braking_source": speed_source,
			"attack_range": attack_range_source * scale,
			"attack_range_source": attack_range_source,
			"minimum_attack_range": 0.0,
			"minimum_attack_range_source": 0.0,
			"vision_range": CREEP_VISION_SOURCE * scale,
			"vision_range_source": CREEP_VISION_SOURCE,
			"delay_between_shots_ms": delay_ms,
			"pre_attack_delay_ms": 500.0,
			"firing_duration_ms": 500.0,
			"attack_period_ticks": maxi(1, roundi(delay_ms / (TICK_SECONDS * 1000.0))),
			"pre_attack_ticks": 5,
			"firing_duration_ticks": 5,
			"clip_size": 0,
			"clip_reload_time_ms": 0.0,
			"continuous_fire_one": 0,
			"continuous_fire_coast_ticks": 0,
			"continuous_fire_rate_multiplier": 1.0,
			"formation_positions": [Vector3.ZERO],
			"formation_positions_base": [Vector3.ZERO],
			"formation_mode": "Line",
			"provenance": {
				"measured": "creep-contract: MaxHealth/GuardMaxRange/GuardWanderRange/CREEP_VISION per BFME2 1.06 INIs",
				"provisional": "weapon damage, cadence, and locomotor speeds are recorded provisionals (INI weapon extraction follow-up)",
				"art_status": String(stats["art_status"]),
			},
		}
		# Only extend a populated compiled damage-type table; seeding an empty
		# legacy table would silently switch every other unit's lookup source.
		if not _unit_damage_types.is_empty() and not _unit_damage_types.has(guard_object_id):
			_unit_damage_types[guard_object_id] = String(stats["damage_type"])
	_rules["unit_rules"] = configured_unit_rules
	# MonsterLair / NeutralStructureHole armor is not compiled yet: register a
	# neutral 1.0 stand-in (recorded provisional) so lair damage is 1:1 instead
	# of falling to the unrelated 25% structure provisional scalar.
	if not _structure_armor.has("creep_lair"):
		_structure_armor["creep_lair"] = {"set_id": "MonsterLair-provisional", "damage_scalar": 1.0, "scalars": {"default": 1.0}}
	if not _structure_armor.has("creep_hole"):
		_structure_armor["creep_hole"] = {"set_id": "NeutralStructureHole-provisional", "damage_scalar": 1.0, "scalars": {"default": 1.0}}


func _seed_creep_lairs() -> void:
	if _creep_lair_placements.is_empty():
		return
	_register_creep_guard_rules()
	var placements := _creep_lair_placements.duplicate(true)
	placements.sort_custom(
		func(a, b): return int((a as Dictionary).get("source_index", 0)) < int((b as Dictionary).get("source_index", 0))
	)
	for placement_value in placements:
		var placement: Dictionary = placement_value
		var type_name := String(placement.get("type_name", ""))
		var family := _creep_family_for(type_name)
		if family.is_empty():
			# Fail closed: an unmapped lair type is recorded, never seeded blind
			# and never silently dropped from observability.
			_emit_event("creep.lair_unsupported", 0, 0, {
				"type_name": type_name,
				"source_index": int(placement.get("source_index", -1)),
			})
			continue
		var lair_id := _next_creep_structure_id
		_next_creep_structure_id += 1
		var position := Vector2(placement.get("position", Vector2.ZERO))
		structures[lair_id] = {
			"id": lair_id,
			"team": CREEP_TEAM,
			"structure_kind": "creep_lair",
			"creep_family": String(family.get("family", type_name)),
			"creep_type_name": type_name,
			"source_index": int(placement.get("source_index", -1)),
			"position": position,
			"yaw": float(placement.get("yaw", 0.0)),
			"rally": position,
			"health": CREEP_LAIR_MAX_HEALTH,
			"maximum_health": CREEP_LAIR_MAX_HEALTH,
			"construction_progress": 1.0,
			"level": 1,
			"completed_upgrades": [],
			"damage_remainders": {},
			"queue": [],
			"upgrade_queue": [],
			"creep_spawn_number": int(family.get("spawn_number", 1)),
			"creep_replace_delay_ticks": maxi(1, roundi(float(family.get("replace_delay_ms", 45000.0)) / (TICK_SECONDS * 1000.0))),
			"creep_guard_templates": (family.get("guards", []) as Array).duplicate(),
			"creep_guard_spawn_count": 0,
			"creep_guard_ids": [],
			"creep_next_respawn_tick": 0,
			"creep_hole_id": 0,
			"creep_cleared": false,
			"creep_treasure_chests": int(family.get("treasure_chests", 1)),
			"creep_levelup_chest": bool(family.get("levelup_chest", false)),
			# Unconverted lair art (goblin/spider/wight/drake families) fails
			# closed into this recorded provisional: the sim camp is fully live,
			# presentation simply has no bound lifecycle visual yet.
			"creep_art_status": String(placement.get("binding_status", "unresolved")),
		}
		_emit_event("creep.lair_seeded", 0, lair_id, {
			"family": String(family.get("family", type_name)),
			"type_name": type_name,
			"source_index": int(placement.get("source_index", -1)),
			"art_status": String(placement.get("binding_status", "unresolved")),
		})
		for _burst_index in range(int(family.get("spawn_number", 1))):
			_spawn_creep_guard(lair_id)


func _spawn_creep_guard(lair_id: int) -> int:
	var lair: Dictionary = structures.get(lair_id, {})
	if lair.is_empty():
		return 0
	var templates: Array = lair.get("creep_guard_templates", [])
	if templates.is_empty():
		return 0
	# Cumulative spawn ordinal drives template alternation (goblin lair mixes
	# swordsmen and archers) and the deterministic exit fan-out.
	var spawn_ordinal := int(lair.get("creep_guard_spawn_count", 0))
	var guard_object_id := String(templates[spawn_ordinal % templates.size()])
	var rule: Dictionary = (_rules.get("unit_rules", {}) as Dictionary).get(guard_object_id, {}) as Dictionary
	if rule.is_empty():
		return 0
	var spawn_number := maxi(1, int(lair.get("creep_spawn_number", 1)))
	var home := Vector2(lair.get("position", Vector2.ZERO))
	var exit_angle := float(lair.get("yaw", 0.0)) + TAU * float(spawn_ordinal % spawn_number) / float(spawn_number)
	var at := home + Vector2.RIGHT.rotated(exit_angle) * CREEP_GUARD_EXIT_RADIUS
	if route_provider != null and route_provider.has_method("_walkable_spawn"):
		at = Vector2(route_provider.call("_walkable_spawn", at))
	var guard_id := _next_creep_guard_id
	_next_creep_guard_id += 1
	var stats: Dictionary = CREEP_GUARD_STATS.get(guard_object_id, {}) as Dictionary
	_add_battalion(guard_id, CREEP_TEAM, at, String(stats.get("name", guard_object_id)), guard_object_id, guard_object_id, 0)
	if not entities.has(guard_id):
		return 0
	var scale := _creep_scale()
	var row: Dictionary = entities[guard_id]
	row["creep_lair_id"] = lair_id
	row["creep_home"] = home
	row["creep_guard_max_range"] = float(stats.get("guard_max_range", 250.0)) * scale
	row["creep_guard_wander_range"] = float(stats.get("guard_wander_range", 40.0)) * scale
	row["creep_returning"] = false
	lair["creep_guard_spawn_count"] = spawn_ordinal + 1
	var guard_ids: Array = lair.get("creep_guard_ids", [])
	guard_ids.append(guard_id)
	lair["creep_guard_ids"] = guard_ids
	_emit_event("creep.guard_spawned", guard_id, lair_id, {"object_id": guard_object_id})
	return guard_id


func _step_creeps() -> void:
	var lair_ids: Array[int] = []
	var hole_ids: Array[int] = []
	for id in structure_ids(CREEP_TEAM):
		match String((structures[id] as Dictionary).get("structure_kind", "")):
			"creep_lair":
				lair_ids.append(id)
			"creep_hole":
				hole_ids.append(id)
	# 1) SpawnBehavior replacement bookkeeping per living lair.
	for lair_id in lair_ids:
		var lair: Dictionary = structures[lair_id]
		if int(lair.get("health", 0)) <= 0:
			continue
		if _living_creep_guard_count(lair) >= int(lair.get("creep_spawn_number", 1)):
			lair["creep_next_respawn_tick"] = 0
			continue
		var next_tick := int(lair.get("creep_next_respawn_tick", 0))
		if next_tick <= 0:
			lair["creep_next_respawn_tick"] = tick_index + int(lair.get("creep_replace_delay_ticks", 1))
		elif tick_index >= next_tick:
			lair["creep_next_respawn_tick"] = 0
			_spawn_creep_guard(lair_id)
	# 2) RebuildHoleBehavior: a surviving hole regrows its lair.
	for hole_id in hole_ids:
		var hole: Dictionary = structures[hole_id]
		if int(hole.get("health", 0)) <= 0:
			continue
		if tick_index >= int(hole.get("creep_rebuild_tick", 0)):
			_rebuild_creep_lair(hole_id)
	# 3) Guard leash AI, ascending guard id.
	for guard_id in entity_ids():
		var row: Dictionary = entities[guard_id]
		if int(row.get("team", -1)) == CREEP_TEAM and row.has("creep_lair_id"):
			_step_creep_guard(guard_id)


func _living_creep_guard_count(lair: Dictionary) -> int:
	## Prunes expired-corpse ids from the lair roster as a side effect.
	var living := 0
	var pruned: Array = []
	for guard_value in lair.get("creep_guard_ids", []) as Array:
		var guard_id := int(guard_value)
		if not entities.has(guard_id):
			continue
		pruned.append(guard_id)
		if int((entities[guard_id] as Dictionary).get("health", 0)) > 0:
			living += 1
	lair["creep_guard_ids"] = pruned
	return living


func _step_creep_guard(guard_id: int) -> void:
	var row: Dictionary = entities[guard_id]
	if int(row.get("health", 0)) <= 0:
		return
	var home := Vector2(row.get("creep_home", row.get("position", Vector2.ZERO)))
	var leash := float(row.get("creep_guard_max_range", 0.0))
	var wander := float(row.get("creep_guard_wander_range", 0.0))
	var position := Vector2(row.get("position", Vector2.ZERO))
	var target_id := int(row.get("target_id", 0))
	if target_id != 0:
		# SlavedUpdate leash enforcement: break the chase the moment either the
		# guard or its target is beyond GuardMaxRange of the lair anchor.
		var target_kind := String(row.get("target_kind", "battalion"))
		var beyond := position.distance_to(home) > leash
		if not beyond and _target_alive(target_id, target_kind):
			beyond = _target_position(target_id, target_kind).distance_to(home) > leash
		if beyond:
			row["target_id"] = 0
			row["target_kind"] = "battalion"
			row["attack_windup"] = 0
			row["attack_move"] = false
			row["order_kind"] = ""
			_clear_member_attack_schedule(row)
			_clear_member_targets(row)
			_clear_pending_route(row, false)
			row["creep_returning"] = true
			if _assign_route(row, home):
				row["state"] = "run"
			else:
				row["creep_returning"] = false
				row["state"] = "idle"
			_emit_event("creep.guard_leash_return", guard_id, 0, {"home": home})
		return
	if bool(row.get("creep_returning", false)):
		# No acquisition on the way home (documented simplification of the
		# retail return leg); arrival re-arms ordinary aggro below.
		if (row.get("route", []) as Array).is_empty():
			row["creep_returning"] = false
		return
	# Aggro: CREEP_VISION acquisition against any rostered team's battalion.
	var vision := float(row.get("vision_range", 0.0))
	if vision > 0.0:
		var best_id := _spatial_nearest_hostile(
			row, CREEP_TEAM, position, vision, SPATIAL_FILTER_ENGAGE
		)
		if best_id != 0:
			row["target_id"] = best_id
			row["target_kind"] = "battalion"
			row["order_kind"] = "auto_attack"
			_clear_pending_route(row, false)
			_emit_event("creep.guard_aggro", guard_id, best_id, {})
			return
	# Idle wander within GuardWanderRange (deterministic tick/id hash).
	if wander <= 0.0 or not (row.get("route", []) as Array).is_empty():
		return
	if (tick_index + guard_id) % CREEP_GUARD_WANDER_INTERVAL_TICKS != 0:
		return
	var seed_value := (tick_index * 2654435761 + guard_id * 40503) & 0x7FFFFFFF
	var wander_angle := TAU * float(seed_value % 360) / 360.0
	var wander_radius := wander * (0.35 + 0.65 * float((seed_value / 360) % 100) / 100.0)
	if _assign_route(row, home + Vector2.RIGHT.rotated(wander_angle) * wander_radius):
		row["state"] = "run"


func _on_creep_structure_destroyed(attacker_id: int, target_id: int) -> void:
	var target: Dictionary = structures.get(target_id, {})
	match String(target.get("structure_kind", "")):
		"creep_lair":
			if int(target.get("creep_hole_id", 0)) != 0 or bool(target.get("creep_cleared", false)):
				return
			# RebuildHoleExposeDie: lair death exposes the family's 500 HP hole.
			var hole_id := _next_creep_structure_id
			_next_creep_structure_id += 1
			var position := Vector2(target.get("position", Vector2.ZERO))
			structures[hole_id] = {
				"id": hole_id,
				"team": CREEP_TEAM,
				"structure_kind": "creep_hole",
				"creep_family": String(target.get("creep_family", "")),
				"creep_lair_id": target_id,
				"source_index": int(target.get("source_index", -1)),
				"position": position,
				"rally": position,
				"health": CREEP_HOLE_MAX_HEALTH,
				"maximum_health": CREEP_HOLE_MAX_HEALTH,
				"construction_progress": 1.0,
				"level": 1,
				"completed_upgrades": [],
				"damage_remainders": {},
				"queue": [],
				"upgrade_queue": [],
				"not_auto_acquirable": true,
				"creep_rebuild_tick": tick_index + CREEP_HOLE_REBUILD_TICKS,
				"creep_treasure_chests": int(target.get("creep_treasure_chests", 1)),
				"creep_levelup_chest": bool(target.get("creep_levelup_chest", false)),
			}
			target["creep_hole_id"] = hole_id
			target["creep_next_respawn_tick"] = 0
			_emit_event("creep.hole_exposed", attacker_id, hole_id, {
				"lair_id": target_id,
				"family": String(target.get("creep_family", "")),
				"rebuild_tick": tick_index + CREEP_HOLE_REBUILD_TICKS,
			})
		"creep_hole":
			_award_creep_treasure(attacker_id, target_id)


func _rebuild_creep_lair(hole_id: int) -> void:
	var hole: Dictionary = structures.get(hole_id, {})
	var lair_id := int(hole.get("creep_lair_id", 0))
	structures.erase(hole_id)
	var lair: Dictionary = structures.get(lair_id, {})
	if lair.is_empty():
		return
	lair["health"] = int(lair.get("maximum_health", CREEP_LAIR_MAX_HEALTH))
	lair["damage_remainders"] = {}
	lair["creep_hole_id"] = 0
	lair["creep_next_respawn_tick"] = 0
	_emit_event("creep.lair_rebuilt", 0, lair_id, {"family": String(lair.get("creep_family", ""))})
	# The regrown camp bursts back to its full guard complement immediately;
	# subsequent losses fall back to the ordinary replacement timer.
	var deficit := int(lair.get("creep_spawn_number", 1)) - _living_creep_guard_count(lair)
	for _index in range(maxi(0, deficit)):
		_spawn_creep_guard(lair_id)


func _award_creep_treasure(attacker_id: int, hole_id: int) -> void:
	var hole: Dictionary = structures.get(hole_id, {})
	var lair_id := int(hole.get("creep_lair_id", 0))
	var lair: Dictionary = structures.get(lair_id, {})
	if not lair.is_empty():
		# Destroying the hole permanently clears the camp: no rebuild ever.
		lair["creep_cleared"] = true
		lair["creep_hole_id"] = 0
		lair["creep_next_respawn_tick"] = 0
	var chest_count := maxi(1, int(hole.get("creep_treasure_chests", 1)))
	var seed_index := maxi(0, int(hole.get("source_index", 0)))
	var chest_values: Array[int] = []
	var total := 0
	for chest_index in range(chest_count):
		# Deterministic stand-in for the retail 160-200 random roll: seeded by
		# the authored placement so twin runs and both lockstep peers agree.
		var value := CREEP_TREASURE_MIN_RESOURCE + (seed_index * 31 + chest_index * 17) % (CREEP_TREASURE_MAX_RESOURCE - CREEP_TREASURE_MIN_RESOURCE + 1)
		chest_values.append(value)
		total += value
	var killer_team := -1
	if entities.has(attacker_id):
		killer_team = int((entities[attacker_id] as Dictionary).get("team", -1))
	if _is_combatant_team(killer_team):
		team_resources[killer_team] = resources_for_team(killer_team) + total
		_emit_event("creep.treasure_collected", attacker_id, hole_id, {
			"team": killer_team,
			"amount": total,
			"chests": chest_values,
			"family": String(hole.get("creep_family", "")),
		})
	else:
		# No attributable rostered killer (e.g. a power strike): the drop is
		# recorded and forfeited rather than silently invented for anyone.
		_emit_event("creep.treasure_forfeited", attacker_id, hole_id, {
			"amount": total,
			"chests": chest_values,
			"family": String(hole.get("creep_family", "")),
		})
	if bool(hole.get("creep_levelup_chest", false)):
		# TreasureChest2 (wight/drake Special OCL) levels up a nearby battalion
		# in retail; v0 records the drop instead of inventing a recipient.
		_emit_event("creep.levelup_chest_provisional", attacker_id, hole_id, {})
	_emit_event("creep.camp_cleared", attacker_id, lair_id, {
		"family": String(hole.get("creep_family", "")),
		"source_index": int(hole.get("source_index", -1)),
	})


func _creep_state_snapshot() -> Dictionary:
	## Deterministic creep block appended to state_snapshot() only when the
	## creep rule is enabled (the OFF snapshot stays byte-identical). Lairs,
	## holes, and guards already serialize through the ordinary structure and
	## entity rows; this block adds the creep-only timers and links.
	var lair_rows: Array[Dictionary] = []
	var hole_rows: Array[Dictionary] = []
	for id in structure_ids(CREEP_TEAM):
		var row: Dictionary = structures[id]
		match String(row.get("structure_kind", "")):
			"creep_lair":
				var guard_ids: Array[int] = []
				for guard_value in row.get("creep_guard_ids", []) as Array:
					guard_ids.append(int(guard_value))
				guard_ids.sort()
				lair_rows.append({
					"id": id,
					"family": String(row.get("creep_family", "")),
					"source_index": int(row.get("source_index", -1)),
					"health": int(row.get("health", 0)),
					"spawn_count": int(row.get("creep_guard_spawn_count", 0)),
					"next_respawn_tick": int(row.get("creep_next_respawn_tick", 0)),
					"guard_ids": guard_ids,
					"hole_id": int(row.get("creep_hole_id", 0)),
					"cleared": bool(row.get("creep_cleared", false)),
				})
			"creep_hole":
				hole_rows.append({
					"id": id,
					"lair_id": int(row.get("creep_lair_id", 0)),
					"health": int(row.get("health", 0)),
					"rebuild_tick": int(row.get("creep_rebuild_tick", 0)),
				})
	var guard_rows: Array[Dictionary] = []
	for id in entity_ids():
		var row: Dictionary = entities[id]
		if int(row.get("team", -1)) != CREEP_TEAM or not row.has("creep_lair_id"):
			continue
		var creep_home := Vector2(row.get("creep_home", Vector2.ZERO))
		guard_rows.append({
			"id": id,
			"lair_id": int(row.get("creep_lair_id", 0)),
			"returning": bool(row.get("creep_returning", false)),
			"home": [snappedf(creep_home.x, 0.001), snappedf(creep_home.y, 0.001)],
		})
	return {
		"lairs": lair_rows,
		"holes": hole_rows,
		"guards": guard_rows,
		"next_creep_guard_id": _next_creep_guard_id,
		"next_creep_structure_id": _next_creep_structure_id,
	}


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
			# Nearest hostile, ties to the lowest id. This was the last quadratic
			# term in the tick: an all-pairs scan of every wave member against
			# every hostile, unbounded in range, once per team every
			# AI_CONTROLLER_BASE_INTERVAL ticks.
			#
			# Converting it required NORMALISING the tie-break first, which is a
			# deliberate behaviour change. The old rule was
			#   distance < closest or (is_equal_approx(distance, closest) and candidate < target_id)
			# and it could not be reproduced from a neighbourhood query for two
			# reasons: is_equal_approx is a tolerance comparison and therefore not
			# transitive, and on an approximate tie the rule reassigned
			# `closest_distance` to a value that could be slightly LARGER than the
			# current best, so the running minimum drifted upward. Both make the
			# winner depend on sequential visit order. The replacement keeps the
			# same intent - closest, lowest id wins - as an exact total order.
			target_id = _spatial_nearest_hostile(
				row, team, Vector2(row["position"]), SPATIAL_UNBOUNDED_RANGE, 0, true
			)
			if target_id == 0:
				# hostiles is non-empty here, so the sweep always finds one; this
				# only guards a hostile whose row moved out from under the index.
				target_id = hostiles[0]
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
	# Parity path ledger: refuse ground routes that cross impassable cells.
	_ensure_parity()
	if not parity.can_path_between(Vector2(row["position"]), destination):
		last_route_rejection = "parity-path-impassable"
		return false
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
	return entities.has(id) and not bool((entities[id] as Dictionary).get("is_banner_carrier", false)) and int((entities[id] as Dictionary)["team"]) == team and int((entities[id] as Dictionary)["health"]) > 0 and int((entities[id] as Dictionary).get("knockdown_ticks", 0)) <= 0 and winner == -1


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
	if retail_formation_movement and row.has("group_speed_cap"):
		# The group cohesion cap belongs to one order, not to the unit.
		row["group_speed_cap"] = 0.0
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
	if creep_lairs_enabled:
		# Appended only when the opt-in creep rule is on: the default snapshot
		# — and therefore the pinned battle signature — stays byte-identical.
		snapshot_row["creeps"] = _creep_state_snapshot()
	if not hero_rank_history.is_empty():
		snapshot_row["hero_peak_ranks"] = hero_rank_history
	if not building_permission_rows.is_empty():
		snapshot_row["building_permissions"] = building_permission_rows
	if not command_point_override_rows.is_empty():
		snapshot_row["command_point_overrides"] = command_point_override_rows
	return snapshot_row


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
			last_command_result = cast_ability(int(args.get("hero_id", 0)), String(args.get("ability_id", "")), Vector2(args.get("target_point", Vector2.ZERO)), team)
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
		"production_unit_order", "ai_production_plan", "unit_damage_types",
		"unit_damage_components", "unit_armor",
		"unit_weapon_upgrades", "structure_armor", "spawn_roster", "structure_kinds",
		"seed_structure_kinds", "structure_max_health", "structure_build_rules",
		"unit_prerequisites", "structure_upgrade_contracts", "structure_upgrade_effects",
		"compiled_research_kinds", "unit_upgrade_commands", "unit_level_upgrades", "unit_banner_carriers",
		"spellbook_ready", "spellbook_document", "spellbook_powers", "spellbook_order",
		"spellbook_sciences", "spellbook_intrinsic", "science_to_power",
		"expansion_build_rules", "unit_ability_rules", "unit_experience_rules",
	]


func _authoritative_state() -> Dictionary:
	var state := {
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
		"unit_damage_components": _unit_damage_components,
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
		"unit_banner_carriers": _unit_banner_carriers,
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
		"team_spellbooks": _team_spellbooks,
		"clock_paused": clock_paused,
		"pending_power_effects": _pending_power_effects,
		"active_groves": _active_groves,
		"summon_despawn_ticks": _summon_despawn_ticks,
		"expansion_pads": expansion_pads,
		"expansion_build_rules": _expansion_build_rules,
		"next_expansion_structure_id": _next_expansion_structure_id,
		"build_plots_only": build_plots_only,
		"build_plots": build_plots,
		"has_hero_units": _has_hero_units,
		"unit_ability_rules": _unit_ability_rules,
		"unit_experience_rules": _unit_experience_rules,
		"pending_commands": _pending_commands,
		"creep_lairs_enabled": creep_lairs_enabled,
		"creep_lair_placements": _creep_lair_placements,
		"next_creep_guard_id": _next_creep_guard_id,
		"next_creep_structure_id": _next_creep_structure_id,
	}
	# EMPTY-IS-ABSENT, deliberately: a match with no configured base flags must
	# contribute NOTHING for this subsystem, so the frozen cross-platform state
	# pin keeps proving the addition is inert by default. An unconditional key
	# here would re-mint the pin for every scenario that never touches bases.
	if not unpackable_bases.is_empty():
		state["unpackable_bases"] = unpackable_bases
	if not _summon_aura_source_ids.is_empty():
		state["summon_aura_source_ids"] = _summon_aura_source_ids
	# EMPTY-IS-ABSENT, same contract as above: a match in which nobody casts a
	# reveal/field ping must contribute nothing, so the frozen cross-platform pin
	# stays valid for every scenario that never touches this lane.
	if not _field_pings.is_empty():
		state["field_pings"] = _field_pings
	# Same EMPTY-IS-ABSENT contract for the weather lane: a match in which
	# nobody casts Darkness or Freezing Rain contributes zero bytes.
	if not _weather_effects.is_empty():
		state["weather_effects"] = _weather_effects
	# These selected-pack contracts are absent in legacy/default scenarios. Keep
	# them out of the byte stream unless configured so unrelated state pins remain
	# stable, while selected retail matches still hash and snapshot the contracts.
	if not _banner_respawn_ticks_by_object.is_empty():
		state["banner_respawn_ticks_by_object"] = _banner_respawn_ticks_by_object
	if not _castle_behavior_by_source.is_empty():
		state["castle_behavior_by_source"] = _castle_behavior_by_source
	if not _cah_award_contracts.is_empty():
		state["cah_award_contracts"] = _cah_award_contracts
	if not _cah_award_tallies.is_empty():
		state["cah_award_tallies"] = _cah_award_tallies
	if not cah_award_results.is_empty():
		state["cah_award_results"] = cah_award_results
	# Same discipline for the map named-object namespace: a match that never
	# declares one contributes zero bytes (see the store's block comment).
	if not map_named_object_namespace.is_empty():
		state["map_named_object_namespace"] = map_named_object_namespace
	# Same discipline for the script unit references: a match whose scripts
	# never bind one contributes zero bytes (see the store's block comment).
	if not script_unit_references.is_empty():
		state["script_unit_references"] = script_unit_references
	if not script_entity_references.is_empty():
		state["script_entity_references"] = script_entity_references
	if not create_object_die_pending.is_empty():
		state["create_object_die_pending"] = create_object_die_pending
	if parity != null:
		var parity_state: Dictionary = parity.to_state()
		if not parity_state.is_empty():
			state["parity"] = parity_state
	# And for the script-built OBJECT_TYPE_LIST stores: mutable match state
	# (retail persists them in save games), zero bytes until a script builds
	# one (see the store's block comment).
	if not script_object_type_lists.is_empty():
		state["script_object_type_lists"] = script_object_type_lists
	# Named team membership/flags are outcome-bearing. Empty remains absent so
	# scriptless matches retain their frozen hash.
	var script_team_view := _script_team_state_view()
	if not script_team_view.is_empty():
		state["script_teams"] = script_team_view
	if not building_permissions_by_team.is_empty():
		state["building_permissions_by_team"] = building_permissions_by_team
	if not command_point_overrides_by_team.is_empty():
		state["command_point_overrides_by_team"] = command_point_overrides_by_team
	# And for team behavior state (TEAM_STATE + custom-state token sets):
	# retail save-persists m_state (Team::xfer), the conditions that gate the
	# AI attack loops read it, and a peer adopting a snapshot must answer
	# TEAM_STATE_IS exactly as the peer that wrote the state. Zero bytes until
	# a script writes one (see the store's block comment).
	if not team_behavior_states.is_empty():
		state["team_behavior_states"] = team_behavior_states
	# Sequential-script heads are outcome-bearing AI behavior queues. Empty is
	# absent so scriptless matches keep frozen hash pins.
	if not sequential_script_queues.is_empty():
		state["sequential_script_queues"] = sequential_script_queues
	if not production_controls_by_team.is_empty():
		state["production_controls_by_team"] = production_controls_by_team
	if not tech_buildability.is_empty():
		state["tech_buildability"] = tech_buildability
	if not script_team_references.is_empty():
		state["script_team_references"] = script_team_references
	if not player_progression.is_empty():
		state["player_progression"] = player_progression
	if not player_economy_extras.is_empty():
		state["player_economy_extras"] = player_economy_extras
	if not player_diplomacy_overrides.is_empty():
		state["player_diplomacy_overrides"] = player_diplomacy_overrides
	if not script_event_counts.is_empty():
		state["script_event_counts"] = script_event_counts
	if not containment.is_empty():
		state["containment"] = containment
	if not entity_container.is_empty():
		state["entity_container"] = entity_container
	if not script_areas.is_empty():
		state["script_areas"] = script_areas
	if not script_waypoints.is_empty():
		state["script_waypoints"] = script_waypoints
	if not script_waypoint_paths.is_empty():
		state["script_waypoint_paths"] = script_waypoint_paths
	if not team_created_edge.is_empty():
		state["team_created_edge"] = team_created_edge
	if not match_script_flags.is_empty():
		state["match_script_flags"] = match_script_flags
	if not attack_priority_names.is_empty():
		state["attack_priority_names"] = attack_priority_names
	if not script_surface_bag.is_empty():
		state["script_surface_bag"] = script_surface_bag
	# Historical objective state is match state and hash-visible. Empty is
	# absent so scenarios that never field a hero retain their frozen pins.
	if not _hero_peak_ranks_by_team.is_empty():
		state["hero_peak_ranks_by_team"] = _hero_peak_ranks_by_team
	# And for the logic random stream: the six generator words are the entire
	# stream state (retail's theGameLogicSeed rides save games and its CRC is
	# sync-checked - GetGameLogicRandomSeedCRC). Zero bytes until the first
	# draw, so a scriptless match leaves the frozen pin untouched; an
	# adopting peer receives the words and continues the identical sequence.
	if not _logic_random_state.is_empty():
		state["logic_random_state"] = _logic_random_state
	# And for the script interpreter's own memory: hashed and serialized
	# through its canonical view (zero counters/false flags pruned, every
	# level sorted), so an untouched match contributes zero bytes and state
	# returned to pristine values returns to the pristine hash exactly.
	var script_env_view := _script_env_state_view()
	if not script_env_view.is_empty():
		state["script_env_state"] = script_env_view
	return state


func _restore_authoritative_state(state: Dictionary) -> void:
	tick_index = int(state["tick_index"])
	winner = int(state["winner"])
	ai_enabled = bool(state["ai_enabled"])
	entities = state["entities"]
	structures = state["structures"]
	# The structure table was just replaced wholesale; the id-keyed footprint
	# memo describes the old one.
	_structure_footprint_radius_cache.clear()
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
	ring_mechanic_enabled = bool(_rules.get("allow_ring_heroes", false))
	_configure_ring_mechanic_contract()
	_unit_production_rules = state["unit_production_rules"]
	_completed_hero_identities = state["completed_hero_identities"]
	_production_unit_order = state["production_unit_order"]
	_ai_production_plan = state["ai_production_plan"]
	_unit_damage_types = state["unit_damage_types"]
	_unit_damage_components = state["unit_damage_components"]
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
	_unit_banner_carriers = state.get("unit_banner_carriers", {})
	_banner_respawn_ticks_by_object = state.get("banner_respawn_ticks_by_object", {})
	_castle_behavior_by_source = state.get("castle_behavior_by_source", {})
	_cah_award_contracts = state.get("cah_award_contracts", {})
	_cah_award_tallies = state.get("cah_award_tallies", {})
	cah_award_results = state.get("cah_award_results", {})
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
	_spellbook_command_points_upgrade = (
		(
			(_spellbook_document.get("registration", {}) as Dictionary).get(
				"spellBook", {}
			) as Dictionary
		).get("commandPointsUpgrade", {}) as Dictionary
	).duplicate(true)
	_spellbook_powers = state["spellbook_powers"]
	_spellbook_order = state["spellbook_order"]
	_spellbook_sciences = state["spellbook_sciences"]
	_spellbook_intrinsic = state["spellbook_intrinsic"]
	_science_to_power = state["science_to_power"]
	_team_sciences = state["team_sciences"]
	_power_cooldown_until = state["power_cooldown_until"]
	_staged_purchases = state["staged_purchases"]
	_team_spellbooks = state.get("team_spellbooks", {})
	clock_paused = bool(state["clock_paused"])
	_pending_power_effects = state["pending_power_effects"]
	_active_groves = state["active_groves"]
	_summon_despawn_ticks = state["summon_despawn_ticks"]
	_summon_aura_source_ids = state.get("summon_aura_source_ids", {})
	var adopted_pings: Array[Dictionary] = []
	for ping_value in Array(state.get("field_pings", [])):
		if typeof(ping_value) == TYPE_DICTIONARY:
			adopted_pings.append(ping_value as Dictionary)
	_field_pings = adopted_pings
	var adopted_weather: Array[Dictionary] = []
	for weather_value in Array(state.get("weather_effects", [])):
		if typeof(weather_value) == TYPE_DICTIONARY:
			adopted_weather.append(weather_value as Dictionary)
	_weather_effects = adopted_weather
	expansion_pads = state["expansion_pads"]
	_expansion_build_rules = state["expansion_build_rules"]
	_next_expansion_structure_id = int(state["next_expansion_structure_id"])
	build_plots_only = bool(state.get("build_plots_only", false))
	build_plots = state.get("build_plots", {})
	_has_hero_units = bool(state["has_hero_units"])
	_unit_ability_rules = state["unit_ability_rules"]
	_unit_experience_rules = state["unit_experience_rules"]
	_pending_commands = state["pending_commands"]
	# Absent when empty by construction (empty-is-absent hash discipline).
	unpackable_bases = state.get("unpackable_bases", {})
	map_named_object_namespace = state.get("map_named_object_namespace", {})
	script_unit_references = state.get("script_unit_references", {})
	script_entity_references = state.get("script_entity_references", {})
	create_object_die_pending = state.get("create_object_die_pending", [])
	_ensure_parity()
	if state.has("parity"):
		parity.from_state(state.get("parity", {}) as Dictionary)
	else:
		parity.clear()
	script_object_type_lists = state.get("script_object_type_lists", {})
	var restored_script_teams: Dictionary = state.get("script_teams", {})
	for script_team_name in script_teams.keys():
		var configured := script_teams[script_team_name] as Dictionary
		script_teams[script_team_name] = _script_team_definition(configured)
	for script_team_name in restored_script_teams.keys():
		script_teams[script_team_name] = (restored_script_teams[script_team_name] as Dictionary).duplicate(true)
	building_permissions_by_team = state.get("building_permissions_by_team", {})
	command_point_overrides_by_team = state.get("command_point_overrides_by_team", {})
	team_behavior_states = state.get("team_behavior_states", {})
	sequential_script_queues = state.get("sequential_script_queues", {})
	production_controls_by_team = state.get("production_controls_by_team", {})
	tech_buildability = state.get("tech_buildability", {})
	script_team_references = state.get("script_team_references", {})
	player_progression = state.get("player_progression", {})
	player_economy_extras = state.get("player_economy_extras", {})
	player_diplomacy_overrides = state.get("player_diplomacy_overrides", {})
	script_event_counts = state.get("script_event_counts", {})
	containment = state.get("containment", {})
	entity_container = state.get("entity_container", {})
	script_areas = state.get("script_areas", {})
	script_waypoints = state.get("script_waypoints", {})
	script_waypoint_paths = state.get("script_waypoint_paths", {})
	team_created_edge = state.get("team_created_edge", {})
	match_script_flags = state.get("match_script_flags", {})
	attack_priority_names = state.get("attack_priority_names", {})
	script_surface_bag = state.get("script_surface_bag", {})
	_hero_peak_ranks_by_team = state.get("hero_peak_ranks_by_team", {})
	# Absent when the minter never drew (empty-is-absent): the adopter then
	# also derives the words from the shared rules seed on ITS first draw.
	_logic_random_state = state.get("logic_random_state", [])
	# IN PLACE, never rebind: attached SageScriptEnvs share this dictionary by
	# reference (see the script_env_state block comment), so a rebind here
	# would silently detach every live script environment from the boundary.
	script_env_state.clear()
	script_env_state.merge(state.get("script_env_state", {}))
	creep_lairs_enabled = bool(state.get("creep_lairs_enabled", false))
	_creep_lair_placements = state.get("creep_lair_placements", [])
	_next_creep_guard_id = int(state.get("next_creep_guard_id", 70001))
	_next_creep_structure_id = int(state.get("next_creep_structure_id", 60001))
	# Reconstruct the derived team registry + per-team manifest aliases from the
	# restored authoritative dicts. Roster order matches setup()'s ascending
	# seeding; the manifest tables realias the restored global tables. Neither is
	# part of the snapshot, so this does not affect the restored hash.
	_reseed_roster_from_state()
	_seed_team_manifest_tables()
	# Re-apply the legacy forge fallback into any cross-faction team's derived
	# (unhashed) contract table. Same-faction teams alias the restored global,
	# which already carries the provisionals, so this is a no-op there and never
	# mutates hashed state.
	_register_forge_upgrade_contracts()
	# Both clocks (sim tick and every attached env's interpreter tick) were
	# just set from one snapshot: rebase the recorded executor offsets from
	# those values - the same numbers every peer restoring this snapshot
	# holds, so the derived offset stays peer-identical.
	_rebase_script_executor_offsets()


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

# --- script surface residual bag (honest stores; not full retail subsystems) ---

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


func script_spawn_entity(unit_type: String, team: int, at: Vector2) -> Dictionary:
	if winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if not _script_owner_exists(team):
		return {"ok": false, "reason": "team %d unavailable" % team}
	var eid := spawn_script_object(unit_type, team, at)
	if eid > 0:
		return {"ok": true, "entity_id": eid, "reason": ""}
	## Fallback for script-surface probes: allocate a minimal living entity row
	## when the content pack has no full unit rule for the type. Marks the row
	## as script-spawned so later systems can distinguish it from authored units.
	if unit_type == "":
		return {"ok": false, "reason": "empty unit type"}
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




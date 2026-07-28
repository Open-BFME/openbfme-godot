extends RefCounted

## War of the Ring AUTHORITATIVE STRATEGIC STATE: who owns what, where the
## armies are, and whose turn it is.
##
## The discipline here is deliberately the same as
## `res://src/retail_slice/retail_slice_sim.gd`, because the two will eventually
## have to agree across a network:
##
## * `state_hash()` canonicalises the authoritative dictionary (dictionaries
##   become sorted key/value rows) and SHA-256s it. Two peers that applied the
##   same commands in the same order MUST produce the same string.
## * `snapshot()` / `restore()` round-trip that same dictionary through
##   `var_to_bytes` - no side channel, no view state, no cached derivation.
## * Iteration is over sorted keys, never over `Dictionary.keys()` insertion
##   order. Region ids are strings, so every sweep sorts them first.
## * There is NO RNG here at all. Combat resolution, if it is ever added, must
##   take its randomness from a caller-supplied seeded stream, not from this
##   file, or the hash stops being reproducible.
## * There are NO floats. Command points, turn indices and army sizes are ints;
##   authored decimals arrived from the importer pre-scaled as `*Milli` ints.
##
## What is intentionally NOT here: any tactical battle, any mission scripting,
## any presentation. A strategic->tactical transition is expressed as a request
## dictionary by `wotr_handoff.gd`; resolving it is somebody else's job.

const WorldScript = preload("res://src/wotr/wotr_world.gd")

const SCHEMA := "openbfme.wotr-strategic-state"
const SCHEMA_VERSION := 1

const NEUTRAL := -1

## Who drives a seat. This is AUTHORITATIVE STRATEGIC STATE, not a session
## setting, and that placement is the whole point: it is the value the tactical
## roster's `is_ai` is derived from, so it has to be identical on every peer and
## inside the strategic hash. The retail menu path already works this way -
## `_menu_sim_team_roster()` reads a shared lobby `controller` field rather than
## a per-machine "which seat am I" number - and the normalisation rule below is
## deliberately the same one: `human` means human, anything else means AI.
const CONTROLLER_HUMAN := "human"
const CONTROLLER_AI := "ai"

## The schema of the battle commitment `wotr_battle.gd` mints and `begin_battle`
## admits. It lives HERE rather than there because `begin_battle` has to validate
## what it is about to put inside the hash, and this file cannot preload the
## bridge (the bridge preloads this file). `wotr_battle.gd` aliases these
## constants rather than restating them, so there is one definition.
const BATTLE_COMMITMENT_SCHEMA := "openbfme.wotr-battle-commitment"
## VERSION 2 adds `battlefield_map`: the PACK MAP the tactical match is actually
## fought on, as opposed to `map_name`, which is the region's authored retail
## `MAP WOR <region>` name. They are different things and today they are never
## the same thing, because no `MAP WOR *` map is cooked in any content pack.
##
## It is IN THE COMMITMENT for exactly the reason `attacker_faction` is (see
## `commitment_matches_brief()` in `wotr_battle.gd`): the battlefield decides
## what the battle is, so a value chosen outside the hash and handed to the sim
## as an extra argument is 867447e's desync with a different name on it. The
## resolution table is an argument to `configure()`; its RESULT is recorded here,
## so two peers whose strategic hashes agree fought on the same ground.
## VERSION 3 adds the four fields auto-resolve needs, and adds them HERE rather
## than passing them alongside for the reason version 2 added `battlefield_map`:
## every value that decides what a battle IS has to be inside the record the
## strategic hash covers, or it is 867447e's desync with a different name on it.
##
##   `battle_type`           THE RESOLVED type of THIS battle: `auto_resolve` or
##                           `rts`, never the campaign rule. Retail's campaign
##                           rule "Auto Resolve and RTS" means both are OFFERED,
##                           so the campaign rule alone does not say what this
##                           battle is - only the resolved value does, and only
##                           the resolved value can be checked by a peer.
##   `battle_type_priority`  the campaign rule that resolved it, recorded so the
##                           resolution can be re-derived and disagreed with.
##   `attacker_handicap`     retail's `GUIDisplayedLevel`, which scales that
##   `defender_handicap`     side's auto-resolve weapon and armour multipliers.
##
## The handicaps matter to the hash twice over: they change the arithmetic AND
## they are inside the digest the dice are seeded from, so a peer with a
## different handicap does not merely compute different damage - it rolls
## different dice, which is exactly the divergence a hash comparison should
## catch rather than paper over.
const BATTLE_COMMITMENT_SCHEMA_VERSION := 3

## The three battle types retail's RULES tab offers, as stable ids. Retail's own
## string keys are `VALUE:AutoResolveAndRTS`, `VALUE:AutoResolve` and
## `VALUE:RTS`; these are the ids the strategic layer stores, because a string
## table key is presentation and this is state.
const BATTLE_TYPE_AUTO_RESOLVE_AND_RTS := "auto_resolve_and_rts"
const BATTLE_TYPE_AUTO_RESOLVE := "auto_resolve"
const BATTLE_TYPE_RTS := "rts"
const BATTLE_TYPES := [
	BATTLE_TYPE_AUTO_RESOLVE_AND_RTS, BATTLE_TYPE_AUTO_RESOLVE, BATTLE_TYPE_RTS,
]
## What a single battle can actually BE. "Auto Resolve and RTS" is a campaign
## rule offering both; it is not a way to fight one battle, and a commitment
## carrying it would describe a battle nothing could resolve.
const BATTLE_RESOLVED_TYPES := [BATTLE_TYPE_AUTO_RESOLVE, BATTLE_TYPE_RTS]
## Retail's tooltip: the priority decides "whether a battle will be decided
## through real-time or auto-resolve if players choose differently". Two
## outcomes, so two values.
const BATTLE_PRIORITIES := [BATTLE_TYPE_AUTO_RESOLVE, BATTLE_TYPE_RTS]

## Retail's own handicap rungs, `GUIDisplayedLevel` 0..100 in fives, transcribed
## from `livingworldautoresolvehandicaps.ini`. A commitment carrying a level
## retail never authored is refused rather than resolved to the nearest rung:
## the ladder is retail's and a value off it is not retail's.
const HANDICAP_STEP := 5
const HANDICAP_MAX := 100

## Every field a commitment may carry, and the type it must carry. Exhaustive in
## BOTH directions: a missing field is refused and so is an extra one. A field
## outside this table would ride into `authoritative_state()` uninspected, which
## is precisely the class of defect the hash exists to catch.
const BATTLE_COMMITMENT_FIELDS := {
	"attacker": TYPE_INT,
	"attacker_faction": TYPE_STRING,
	"attacker_handicap": TYPE_INT,
	"attacker_is_ai": TYPE_BOOL,
	"attacker_team": TYPE_INT,
	"battle_type": TYPE_STRING,
	"battle_type_priority": TYPE_STRING,
	"battlefield_map": TYPE_STRING,
	"brief_digest": TYPE_STRING,
	"committed_armies": TYPE_PACKED_INT32_ARRAY,
	"defender": TYPE_INT,
	"defender_faction": TYPE_STRING,
	"defender_handicap": TYPE_INT,
	"defender_is_ai": TYPE_BOOL,
	"defender_team": TYPE_INT,
	"defending_armies": TYPE_PACKED_INT32_ARRAY,
	"map_name": TYPE_STRING,
	"region": TYPE_STRING,
	"schema": TYPE_STRING,
	"schema_version": TYPE_INT,
	"staging_region": TYPE_STRING,
	"turn": TYPE_INT,
}

## Army kinds. A hero army carries a named hero and is capped by hero CP; a
## garrison army is ordinary troops capped by world CP.
const ARMY_HERO := "hero"
const ARMY_GARRISON := "garrison"

const MAX_ARMIES := 4096

var world: WorldScript = null

## Player seats, index-addressed. Each row: {index, template, faction, team,
## controller, handicap, world_cp, hero_cp, defeated}.
##
## `handicap` is retail's `GUIDisplayedLevel` for that seat. It is AUTHORITATIVE
## STRATEGIC STATE for the same reason `controller` is: it scales auto-resolve
## combat, so it decides outcomes, so it has to be identical on every peer and
## inside the hash. The setup screen used to draw the column locked saying
## "nothing carries it"; this field is what now carries it.
var players: Array[Dictionary] = []

## How battles are decided this campaign. AUTHORITATIVE: it selects between two
## different resolution paths, so two peers disagreeing about it would fight two
## different games. Set once by `setup()` from the setup screen's RULES tab and
## never changed mid-campaign.
var battle_type := BATTLE_TYPE_AUTO_RESOLVE_AND_RTS
var battle_type_priority := BATTLE_TYPE_AUTO_RESOLVE

## Roster name -> the auto-resolve units an army of that roster fields, each
## already carrying retail's `HitpointsAtLevel` for its body. Supplied by the
## session from the binding bundle BEFORE any army is placed.
##
## It is NOT hashed and it is NOT restored: it is a pure function of two
## converted bundles, identical on every peer that loaded the same content, and
## putting a 100-entry lookup table inside every snapshot would hash the content
## pack rather than the campaign. What IS hashed is the units it produced, which
## live on the army records themselves.
var roster_units: Dictionary = {}
## Turn order as player indices. Never shuffled here; the caller supplies it.
var turn_order: PackedInt32Array = PackedInt32Array()
## Completed turns since setup. `active_player()` derives from it.
var turn_index := 0

## region id -> player index, or NEUTRAL.
var region_owner: Dictionary = {}
## army id (int) -> army record.
var armies: Dictionary = {}

## The battle currently in flight, or `{}` when none is.
##
## This IS authoritative state, and it is here rather than on the bridge for the
## reason 87cf636 established the hard way: a battle in flight is a strategic
## transaction that is half-applied. A peer that adopted a snapshot taken during
## a battle without this record would not know a battle was happening, would
## have nothing to apply the result to, and would silently diverge the moment the
## result arrived. It is hashed EMPTY-IS-ABSENT (a state with no battle in
## flight contributes zero bytes, so the between-battles hash is exactly what it
## was before this field existed), and `setup()` clears it.
var pending_battle: Dictionary = {}

var _next_army_id := 1

## Non-authoritative: cleared by `restore()` and excluded from the hash, exactly
## like the retail slice's `events`.
var events: Array[Dictionary] = []
var last_command_result: Variant = null


## Bind a world and seat the players. Returns false (and changes nothing that
## matters) when the setup is not fully understood.
##
## `rules` carries the campaign-wide choices the setup screen's RULES tab makes,
## today `battle_type` and `battle_type_priority`. It DEFAULTS rather than being
## required so every existing caller keeps working, and the defaults are
## retail's own screen defaults ("Auto Resolve and RTS", priority "Auto
## Resolve") rather than this project's preference.
func setup(source_world: WorldScript, seats: Array, rules: Dictionary = {}) -> bool:
	if source_world == null or source_world.region_ids.is_empty():
		return false
	if seats.is_empty() or seats.size() > WorldScript.MAX_PLAYERS:
		return false
	world = source_world
	players = []
	region_owner = {}
	armies = {}
	pending_battle = {}
	events = []
	turn_index = 0
	_next_army_id = 1
	battle_type = normalized_battle_type(rules.get("battle_type", BATTLE_TYPE_AUTO_RESOLVE_AND_RTS))
	battle_type_priority = normalized_battle_priority(
		rules.get("battle_type_priority", BATTLE_TYPE_AUTO_RESOLVE))
	var order: Array[int] = []
	for index in range(seats.size()):
		var seat := seats[index] as Dictionary
		var template_name := String(seat.get("template", ""))
		var template: Dictionary = world.player_templates.get(template_name, {})
		players.append({
			"index": index,
			"template": template_name,
			"faction": String(template.get("faction", seat.get("faction", ""))),
			"team": int(seat.get("team", index + 1)),
			"controller": normalized_controller(seat.get("controller", CONTROLLER_AI)),
			"handicap": normalized_handicap(seat.get("handicap", 0)),
			"world_cp": int(template.get("starting_world_cp", 0)),
			"hero_cp": int(template.get("starting_hero_cp", 0)),
			"defeated": false,
		})
		order.append(index)
	turn_order = PackedInt32Array(order)
	for region_id in world.region_ids:
		region_owner[region_id] = NEUTRAL
	return true


## Apply a scenario's `OwnershipSet` rows to seats in order: set index 0 goes to
## player 0, and so on. Extra sets beyond the seated players are ignored (retail
## authors six sets and lets fewer players sit down).
func apply_ownership_sets(scenario_name: String) -> bool:
	if world == null:
		return false
	var scenario := world.scenario(scenario_name)
	if scenario.is_empty():
		return false
	var sets: Array = scenario.get("ownership_sets", []) as Array
	for index in range(players.size()):
		if index >= sets.size():
			break
		var ownership := sets[index] as Dictionary
		for region_id in ownership.get("regions", PackedStringArray()) as PackedStringArray:
			if not world.has_region(region_id):
				return false
			region_owner[region_id] = index
		for spawn_row in ownership.get("spawn_armies", []) as Array:
			var spawn := spawn_row as Dictionary
			var region_id := String(spawn.get("region", ""))
			if not world.has_region(region_id):
				return false
			for army_name in spawn.get("armies", PackedStringArray()) as PackedStringArray:
				if place_army(index, region_id, army_name) < 0:
					return false
	return true


func active_player() -> int:
	if turn_order.is_empty():
		return NEUTRAL
	return turn_order[turn_index % turn_order.size()]


## Completed rounds (one round = one turn for every seat).
func round_index() -> int:
	if turn_order.is_empty():
		return 0
	return turn_index / turn_order.size()


## Advance to the next seat, skipping seats already defeated. Deterministic and
## total: with every seat defeated it still advances exactly one turn, so a
## caller polling for a winner can never spin.
func advance_turn() -> int:
	if turn_order.is_empty():
		return NEUTRAL
	for _step in range(turn_order.size()):
		turn_index += 1
		var candidate := active_player()
		if not bool((players[candidate] as Dictionary).get("defeated", false)):
			return candidate
	turn_index += 1
	return active_player()


## Regions owned by `player`, in sorted order.
func regions_owned_by(player: int) -> PackedStringArray:
	var owned := PackedStringArray()
	for region_id in world.region_ids:
		if int(region_owner.get(region_id, NEUTRAL)) == player:
			owned.append(region_id)
	return owned


func owner_of(region_id: String) -> int:
	return int(region_owner.get(region_id, NEUTRAL))


## Transfer a region. Fails closed on an unknown region or an unseated player;
## a no-op transfer (already the owner) reports false so a caller can never
## mistake it for a capture.
func transfer_region(region_id: String, player: int) -> bool:
	if world == null or not world.has_region(region_id):
		return _reject("unknown region %s" % region_id)
	if player != NEUTRAL and (player < 0 or player >= players.size()):
		return _reject("unknown player %d" % player)
	var previous := owner_of(region_id)
	if previous == player:
		return _reject("region %s already owned by %d" % [region_id, player])
	region_owner[region_id] = player
	events.append({
		"kind": "region_transferred",
		"region": region_id,
		"from": previous,
		"to": player,
		"turn": turn_index,
	})
	last_command_result = true
	return true


## Place an army from the world's `LivingWorldPlayerArmy` roster. Returns the
## new army id, or -1 when the placement is not fully understood.
func place_army(player: int, region_id: String, army_name: String) -> int:
	if world == null or not world.has_region(region_id):
		_reject("unknown region %s" % region_id)
		return -1
	if player < 0 or player >= players.size():
		_reject("unknown player %d" % player)
		return -1
	if armies.size() >= MAX_ARMIES:
		_reject("army count exceeds limit")
		return -1
	var spawn: Dictionary = world.army_spawns.get(army_name, {})
	var roster_name := String(spawn.get("player_army", army_name))
	var roster: Dictionary = world.player_armies.get(roster_name, {})
	var kind := ARMY_HERO if bool(spawn.get("is_hero", false)) else ARMY_GARRISON
	var id := _next_army_id
	_next_army_id += 1
	# THE UNITS ARE AUTHORITATIVE STATE from here on. Each one carries its
	# retail auto-resolve type, body, armour, weapon, combat chain, level and
	# CURRENT HITPOINTS, and all of it enters `authoritative_state()`. That is
	# the largest change this lane makes to the hash surface: an army's strength
	# stops being a command-point integer and becomes a continuous quantity that
	# a battle can reduce without destroying the army.
	#
	# With no `roster_units` table bound - no bindings bundle, or a caller that
	# never set one - the list is EMPTY, and auto-resolve then refuses that army
	# by name rather than inventing a force for it.
	var units: Array[Dictionary] = []
	for row in (roster_units.get(roster_name, []) as Array):
		var unit: Dictionary = (row as Dictionary).duplicate(true)
		unit["army_id"] = id
		units.append(unit)
	armies[id] = {
		"id": id,
		"owner": player,
		"region": region_id,
		"kind": kind,
		"spawn_name": army_name,
		"roster": roster_name,
		"hero_template": String(spawn.get("hero_template_name", "")),
		"command_points": _roster_command_points(roster),
		"units": units,
	}
	events.append({"kind": "army_placed", "army": id, "region": region_id, "turn": turn_index})
	last_command_result = true
	return id


## Army ids in a region, sorted ascending so callers iterate deterministically.
func armies_in_region(region_id: String) -> PackedInt32Array:
	var found: Array[int] = []
	for key in armies.keys():
		if String((armies[key] as Dictionary).get("region", "")) == region_id:
			found.append(int(key))
	found.sort()
	return PackedInt32Array(found)


## Total command points a player has standing in a region.
func command_points_in_region(region_id: String, player: int) -> int:
	var total := 0
	for id in armies_in_region(region_id):
		var army := armies[id] as Dictionary
		if int(army.get("owner", NEUTRAL)) == player:
			total += int(army.get("command_points", 0))
	return total


## Move one army along a single graph edge. Movement is deliberately strict:
## adjacency only, own army only, and the destination must not exceed the
## region's command-point cap for that player.
func move_army(army_id: int, to_region: String) -> bool:
	if not armies.has(army_id):
		return _reject("unknown army %d" % army_id)
	var army := armies[army_id] as Dictionary
	var from_region := String(army.get("region", ""))
	if from_region == to_region:
		return _reject("army %d is already in %s" % [army_id, to_region])
	if not world.are_adjacent(from_region, to_region):
		return _reject("%s is not adjacent to %s" % [to_region, from_region])
	var owner := int(army.get("owner", NEUTRAL))
	var arriving := int(army.get("command_points", 0))
	if command_points_in_region(to_region, owner) + arriving > world.region_cp_limit(to_region):
		return _reject("army %d exceeds the command-point cap of %s" % [army_id, to_region])
	army["region"] = to_region
	events.append({
		"kind": "army_moved",
		"army": army_id,
		"from": from_region,
		"to": to_region,
		"turn": turn_index,
	})
	last_command_result = true
	return true


## True when `player` may attack `region_id` this turn: they must not own it and
## must have an army standing in an adjacent region they do own.
func can_attack(player: int, region_id: String) -> bool:
	if world == null or not world.has_region(region_id):
		return false
	if owner_of(region_id) == player:
		return false
	for neighbour in world.neighbours(region_id):
		if owner_of(neighbour) != player:
			continue
		if not armies_in_region(neighbour).is_empty():
			return true
	return false


## Remove an army from the world entirely - the losing side of a battle. Fails
## closed on an unknown id so a double-remove can never look like a success.
func remove_army(army_id: int) -> bool:
	if not armies.has(army_id):
		return _reject("unknown army %d" % army_id)
	var region_id := String((armies[army_id] as Dictionary).get("region", ""))
	armies.erase(army_id)
	events.append({
		"kind": "army_removed",
		"army": army_id,
		"region": region_id,
		"turn": turn_index,
	})
	last_command_result = true
	return true


## Open the battle transaction. `commitment` is the record `wotr_battle.gd`
## derives from a handoff brief; this file does not build it and does not
## interpret it beyond the two invariants it must enforce itself.
##
## Strictly one battle at a time. A second `begin_battle()` over a live one is
## refused rather than overwritten: overwriting would silently strand the first
## battle's committed armies, which is a lost-update with no symptom until the
## roster is counted much later.
##
## THE COMMITMENT IS VALIDATED, not merely stored. Everything admitted here
## enters `authoritative_state()` and therefore the strategic hash, and it is the
## record `wotr_battle.gd` will later read to decide which regions change hands
## and which armies die. "The bridge only ever hands us a well-formed one" is
## caller discipline, and caller discipline is not a guarantee: an ad-hoc
## dictionary that merely names a real region used to be accepted, hash and all,
## and would then have resolved a battle with no attacker, no sides and no digest
## to check the tactical configuration against. `BATTLE_COMMITMENT_FIELDS` is
## checked exhaustively in both directions and the invariants this file owns -
## seated, distinct sides; distinct teams; a non-empty committed force; a
## well-formed digest - are checked here too.
func begin_battle(commitment: Dictionary) -> bool:
	if commitment.is_empty():
		return _reject("battle commitment is empty")
	if not pending_battle.is_empty():
		return _reject("a battle is already in flight in %s" % String(pending_battle.get("region", "")))
	var schema_reason := _battle_commitment_refusal(commitment)
	if schema_reason != "":
		return _reject(schema_reason)
	var region_id := String(commitment.get("region", ""))
	if world == null or not world.has_region(region_id):
		return _reject("unknown region %s" % region_id)
	pending_battle = commitment.duplicate(true)
	events.append({"kind": "battle_begun", "region": region_id, "turn": turn_index})
	last_command_result = true
	return true


## Close the battle transaction without applying a result. Used when a battle is
## abandoned; the applied-result path lives in `wotr_battle.gd` because it needs
## the tactical winner, which this file deliberately knows nothing about.
func clear_battle() -> bool:
	if pending_battle.is_empty():
		return _reject("no battle is in flight")
	var region_id := String(pending_battle.get("region", ""))
	pending_battle = {}
	events.append({"kind": "battle_cleared", "region": region_id, "turn": turn_index})
	last_command_result = true
	return true


## Write a battle's ATTRITION back to an army: the units that survived, with the
## hitpoints they survived on. This is the outcome path auto-resolve needs and
## `apply_outcome(winner_team)` cannot provide, because auto-resolve does not
## return a boolean - it returns who is left and how hurt they are.
##
## An army whose survivor list is EMPTY is REMOVED, not left standing at zero
## strength: an army with no units is not an army, and leaving one on the board
## would let a wiped force keep holding a region.
##
## Fails closed on an unknown army, and on a survivor list carrying a unit whose
## `army_id` is somebody else's - a mis-keyed writeback would silently move
## units between armies, which is a lost-update with no symptom until a much
## later battle counts the wrong roster.
func apply_attrition(army_id: int, survivors: Array) -> bool:
	if not armies.has(army_id):
		return _reject("unknown army %d" % army_id)
	var kept: Array[Dictionary] = []
	for row in survivors:
		var unit: Dictionary = row
		if int(unit.get("army_id", army_id)) != army_id:
			return _reject("attrition for army %d carries a unit belonging to army %d" % [
				army_id, int(unit.get("army_id", -1))])
		if int(unit.get("hitpoints_milli", 0)) < 0:
			return _reject("attrition for army %d carries negative hitpoints" % army_id)
		kept.append(unit.duplicate(true))
	if kept.is_empty():
		return remove_army(army_id)
	var army := armies[army_id] as Dictionary
	army["units"] = kept
	events.append({
		"kind": "army_attrition",
		"army": army_id,
		"survivors": kept.size(),
		"turn": turn_index,
	})
	last_command_result = true
	return true


## Mark a seat defeated. Idempotent-safe: reports false when already defeated.
func set_defeated(player: int, defeated: bool = true) -> bool:
	if player < 0 or player >= players.size():
		return _reject("unknown player %d" % player)
	var seat := players[player] as Dictionary
	if bool(seat.get("defeated", false)) == defeated:
		return _reject("player %d defeat flag unchanged" % player)
	seat["defeated"] = defeated
	last_command_result = true
	return true


# --- determinism surface -----------------------------------------------------

func state_hash() -> String:
	var context := HashingContext.new()
	context.start(HashingContext.HASH_SHA256)
	context.update(var_to_bytes(canonicalize(authoritative_state())))
	return context.finish().hex_encode()


func snapshot() -> PackedByteArray:
	return var_to_bytes(authoritative_state())


## ALL-OR-NOTHING. Every field is decoded and type-checked into a local BEFORE
## anything on `self` is written, and the writes then happen with no remaining
## way to fail.
##
## The half-applied shape this replaces was not theoretical: a snapshot whose
## `pending_battle` carried a non-Dictionary raised `Invalid cast` at the last
## assignment, AFTER `turn_index`, `players`, `region_owner` and `armies` had
## already been overwritten. `restore()` returned false, so a caller doing the
## right thing - checking the return and refusing to proceed - was nonetheless
## left holding a chimera: the donor's map and armies under the adopter's own
## stale battle, still in flight, still inside the hash. A restore that reports
## failure must leave the adopter exactly as it found it, or the report is worse
## than useless.
func restore(bytes: PackedByteArray) -> bool:
	if bytes.is_empty():
		return false
	var decoded: Variant = bytes_to_var(bytes)
	if typeof(decoded) != TYPE_DICTIONARY:
		return false
	var state := decoded as Dictionary
	for required in [
		["schema", TYPE_STRING], ["schema_version", TYPE_INT],
		["turn_index", TYPE_INT], ["turn_order", TYPE_PACKED_INT32_ARRAY],
		["players", TYPE_ARRAY], ["region_owner", TYPE_DICTIONARY],
		["armies", TYPE_DICTIONARY], ["next_army_id", TYPE_INT],
	]:
		var key := String((required as Array)[0])
		if not state.has(key):
			return false
		if typeof(state[key]) != int((required as Array)[1]):
			return false
	if String(state["schema"]) != SCHEMA:
		return false
	if int(state["schema_version"]) != SCHEMA_VERSION:
		return false

	var staged_players: Array[Dictionary] = []
	for row in state["players"] as Array:
		if typeof(row) != TYPE_DICTIONARY:
			return false
		staged_players.append((row as Dictionary).duplicate(true))
	var source_armies := state["armies"] as Dictionary
	var staged_armies: Dictionary = {}
	for key in source_armies.keys():
		if typeof(key) != TYPE_INT and typeof(key) != TYPE_FLOAT:
			return false
		if typeof(source_armies[key]) != TYPE_DICTIONARY:
			return false
		staged_armies[int(key)] = (source_armies[key] as Dictionary).duplicate(true)
	# The battle in flight rides the snapshot. It is NOT in the required list
	# above, because it is hashed empty-is-absent: a snapshot minted between
	# battles legitimately carries no such key, and demanding one would refuse
	# every ordinary strategic save. Absent therefore restores to `{}` - which is
	# the same thing the absence meant to the hash. PRESENT-BUT-NOT-A-DICTIONARY,
	# however, is a malformed snapshot and refuses the whole restore.
	var staged_battle: Dictionary = {}
	if state.has("pending_battle"):
		if typeof(state["pending_battle"]) != TYPE_DICTIONARY:
			return false
		staged_battle = (state["pending_battle"] as Dictionary).duplicate(true)

	# The battle rules ride the snapshot. They are NOT in the required list: a
	# snapshot minted before version 3 legitimately carries neither, and refusing
	# it would refuse every saved campaign. Absent restores to retail's own screen
	# defaults, which is what such a campaign was played under.
	var staged_type := normalized_battle_type(state.get("battle_type", BATTLE_TYPE_AUTO_RESOLVE_AND_RTS))
	var staged_priority := normalized_battle_priority(
		state.get("battle_type_priority", BATTLE_TYPE_AUTO_RESOLVE))

	# COMMIT. Nothing below can fail.
	battle_type = staged_type
	battle_type_priority = staged_priority
	turn_index = int(state["turn_index"])
	turn_order = PackedInt32Array(state["turn_order"])
	players = staged_players
	region_owner = (state["region_owner"] as Dictionary).duplicate(true)
	armies = staged_armies
	_next_army_id = int(state["next_army_id"])
	pending_battle = staged_battle
	# Derived and view-facing state is rebuilt, never restored, so a snapshot
	# can never smuggle in an event log that the hash does not cover.
	events = []
	last_command_result = null
	return true


## The full authoritative dictionary. Everything the hash covers lives here and
## nothing else does.
func authoritative_state() -> Dictionary:
	var owners: Dictionary = {}
	for region_id in _sorted_strings(region_owner.keys()):
		owners[region_id] = int(region_owner[region_id])
	var army_rows: Dictionary = {}
	var ids: Array[int] = []
	for key in armies.keys():
		ids.append(int(key))
	ids.sort()
	for id in ids:
		army_rows[id] = armies[id]
	var state := {
		"schema": SCHEMA,
		"schema_version": SCHEMA_VERSION,
		"world_campaign": world.campaign_name if world != null else "",
		"turn_index": turn_index,
		"turn_order": turn_order,
		"players": players,
		"region_owner": owners,
		"armies": army_rows,
		"next_army_id": _next_army_id,
		# THE CAMPAIGN'S OWN BATTLE RULES. Hashed, because they select which
		# resolution path runs, and two peers running different paths are not
		# playing the same campaign.
		"battle_type": battle_type,
		"battle_type_priority": battle_type_priority,
	}
	# EMPTY-IS-ABSENT, the same discipline the retail slice applies to
	# `script_env_state`: with no battle in flight the key contributes zero bytes,
	# so a strategic state that has never fought - and one that has fought and
	# resolved - hash identically to the pre-battle layer. No read can tell an
	# absent record from an empty one, so the two must not hash differently.
	if not pending_battle.is_empty():
		state["pending_battle"] = pending_battle
	return state


# --- internals ---------------------------------------------------------------

## The seat-controller normalisation, shared so the strategic layer and anything
## deriving a tactical roster from it can never disagree about what a value
## means. Deliberately the same rule `_menu_sim_team_roster()` applies to the
## lobby's `controller` field: trimmed, lowercased, `human` or else AI.
static func normalized_controller(value: Variant) -> String:
	return CONTROLLER_HUMAN if String(value).strip_edges().to_lower() == CONTROLLER_HUMAN else CONTROLLER_AI


## A seat's handicap, clamped onto retail's own ladder. Retail authors every
## multiple of five from 0 to 100 and states no interpolation, so a value off
## the ladder is snapped DOWN to the rung below and never averaged between two -
## an interpolated rung would be a multiplier retail never wrote.
static func normalized_handicap(value: Variant) -> int:
	var level := int(value)
	if level <= 0:
		return 0
	if level >= HANDICAP_MAX:
		return HANDICAP_MAX
	return (level / HANDICAP_STEP) * HANDICAP_STEP


static func is_authored_handicap(level: int) -> bool:
	return level >= 0 and level <= HANDICAP_MAX and level % HANDICAP_STEP == 0


static func normalized_battle_type(value: Variant) -> String:
	var text := String(value).strip_edges().to_lower()
	return text if BATTLE_TYPES.has(text) else BATTLE_TYPE_AUTO_RESOLVE_AND_RTS


static func normalized_battle_priority(value: Variant) -> String:
	var text := String(value).strip_edges().to_lower()
	return text if BATTLE_PRIORITIES.has(text) else BATTLE_TYPE_AUTO_RESOLVE


## RESOLVE ONE BATTLE'S TYPE from the campaign rule, the priority, and what the
## attacker asked for. Returns `auto_resolve` or `rts` and never anything else.
##
## Retail's three campaign rules mean three different things and this is a
## transcription of them, not an interpretation:
##
##   "RTS"                 every battle is fought. The request is ignored.
##   "Auto Resolve"        every battle is auto-resolved. Likewise.
##   "Auto Resolve and RTS"  BOTH ARE OFFERED, so the player chooses per battle -
##                         which is the only reading under which the row means
##                         anything different from the other two.
##
## When both are offered and the attacker asks for neither in particular, the
## `battle_type_priority` row decides, which is exactly what retail's own
## tooltip says it is for: it settles "whether a battle will be decided through
## real-time or auto-resolve if players choose differently".
##
## THE DEFENDER IS NOT ASKED, and that is stated rather than hidden: no seat
## carries a per-battle preference today, so there is nobody to disagree with
## the attacker. `battle_type_priority` is recorded in the commitment now, and
## validated now, so the day a defender can express a preference the commitment
## does not need a fourth version.
static func resolve_battle_type(
	campaign_type: String, priority: String, requested: String
) -> String:
	if campaign_type == BATTLE_TYPE_AUTO_RESOLVE:
		return BATTLE_TYPE_AUTO_RESOLVE
	if campaign_type == BATTLE_TYPE_RTS:
		return BATTLE_TYPE_RTS
	if BATTLE_RESOLVED_TYPES.has(requested):
		return requested
	return priority if BATTLE_RESOLVED_TYPES.has(priority) else BATTLE_TYPE_RTS


## Why `commitment` may not be admitted, or "" when it may. Sorted iteration:
## the refusal a caller sees must not depend on dictionary insertion order.
func _battle_commitment_refusal(commitment: Dictionary) -> String:
	if String(commitment.get("schema", "")) != BATTLE_COMMITMENT_SCHEMA:
		return "battle commitment schema is not %s" % BATTLE_COMMITMENT_SCHEMA
	if int(commitment.get("schema_version", -1)) != BATTLE_COMMITMENT_SCHEMA_VERSION:
		return "unsupported battle commitment schema_version %d" % int(commitment.get("schema_version", -1))
	var expected := _sorted_strings(BATTLE_COMMITMENT_FIELDS.keys())
	for field in expected:
		if not commitment.has(field):
			return "battle commitment is missing field '%s'" % field
		if typeof(commitment[field]) != int(BATTLE_COMMITMENT_FIELDS[field]):
			return "battle commitment field '%s' has the wrong type" % field
	for field in _sorted_strings(commitment.keys()):
		if not BATTLE_COMMITMENT_FIELDS.has(field):
			return "battle commitment carries unknown field '%s'" % field

	var attacker := int(commitment["attacker"])
	var defender := int(commitment["defender"])
	if attacker < 0 or attacker >= players.size():
		return "battle commitment names an unseated attacker %d" % attacker
	if defender < 0 or defender >= players.size():
		return "battle commitment names an unseated defender %d" % defender
	if attacker == defender:
		return "battle commitment seats player %d on both sides" % attacker
	if int(commitment["attacker_team"]) == int(commitment["defender_team"]):
		return "battle commitment puts both sides on tactical team %d" % int(commitment["attacker_team"])
	if (commitment["committed_armies"] as PackedInt32Array).is_empty():
		return "battle commitment commits no attacking armies"
	# A battle with no battlefield cannot be fought, and a commitment that
	# carried an empty one would admit exactly the hole the field exists to
	# close: the ground would then have to come from somewhere outside the hash.
	if String(commitment["battlefield_map"]).strip_edges().is_empty():
		return "battle commitment names no battlefield map"
	var digest := String(commitment["brief_digest"])
	if digest.length() != 64 or not digest.is_valid_hex_number():
		return "battle commitment carries no well-formed brief digest"

	# THE VERSION 3 FIELDS. Checked here rather than trusted, for the same reason
	# every other field is: they decide the outcome, and they SEED THE DICE. A
	# handicap off retail's ladder would mean a multiplier retail never wrote; a
	# battle type this build does not implement would mean a battle nothing can
	# resolve, admitted into the hash and then stuck.
	if not BATTLE_RESOLVED_TYPES.has(String(commitment["battle_type"])):
		return ("battle commitment names battle_type '%s'; a single battle is either %s "
			+ "or %s, and '%s' is a campaign rule offering both") % [
			String(commitment["battle_type"]), BATTLE_TYPE_AUTO_RESOLVE, BATTLE_TYPE_RTS,
			BATTLE_TYPE_AUTO_RESOLVE_AND_RTS]
	if not BATTLE_PRIORITIES.has(String(commitment["battle_type_priority"])):
		return "battle commitment names battle_type_priority '%s', which is not one of %s" % [
			String(commitment["battle_type_priority"]), str(BATTLE_PRIORITIES)]
	for role in ["attacker", "defender"]:
		var level := int(commitment["%s_handicap" % role])
		if not is_authored_handicap(level):
			return ("battle commitment carries %s_handicap %d, which is not a rung retail "
				+ "authored (0 to %d in steps of %d)") % [role, level, HANDICAP_MAX, HANDICAP_STEP]
	return ""


func _reject(reason: String) -> bool:
	last_command_result = false
	events.append({"kind": "rejected", "reason": reason, "turn": turn_index})
	return false


func _roster_command_points(roster: Dictionary) -> int:
	var total := 0
	for entry in roster.get("entries", []) as Array:
		total += int((entry as Dictionary).get("quantity", 0))
	return total


func _sorted_strings(values: Array) -> PackedStringArray:
	var names: Array[String] = []
	for value in values:
		names.append(String(value))
	names.sort()
	return PackedStringArray(names)


## Same canonical form the retail slice uses: dictionaries collapse to sorted
## key/value rows so insertion order can never leak into the hash.
##
## STATIC and shared on purpose. `wotr_battle.gd` digests the battle brief with
## this exact function rather than a second copy: a canonicalizer that exists
## twice is a canonicalizer that can drift, and the whole point of the digest is
## that two peers derive the same bytes from the same input.
static func canonicalize(value: Variant) -> Variant:
	if typeof(value) == TYPE_DICTIONARY:
		var source := value as Dictionary
		var keys := source.keys()
		keys.sort_custom(_canonical_key_less)
		var rows: Array = []
		for key in keys:
			rows.append([canonicalize(key), canonicalize(source[key])])
		return rows
	if typeof(value) == TYPE_ARRAY:
		var rows: Array = []
		for item in value as Array:
			rows.append(canonicalize(item))
		return rows
	return value


static func _canonical_key_less(a: Variant, b: Variant) -> bool:
	return "%02d:%s" % [typeof(a), var_to_str(a)] < "%02d:%s" % [typeof(b), var_to_str(b)]


## SHA-256 over the canonical form. `state_hash()` is exactly this over the
## authoritative dictionary; the battle bridge is exactly this over a brief.
static func canonical_digest(value: Variant) -> String:
	var context := HashingContext.new()
	context.start(HashingContext.HASH_SHA256)
	context.update(var_to_bytes(canonicalize(value)))
	return context.finish().hex_encode()

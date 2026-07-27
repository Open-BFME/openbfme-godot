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
const BATTLE_COMMITMENT_SCHEMA_VERSION := 2

## Every field a commitment may carry, and the type it must carry. Exhaustive in
## BOTH directions: a missing field is refused and so is an extra one. A field
## outside this table would ride into `authoritative_state()` uninspected, which
## is precisely the class of defect the hash exists to catch.
const BATTLE_COMMITMENT_FIELDS := {
	"attacker": TYPE_INT,
	"attacker_faction": TYPE_STRING,
	"attacker_is_ai": TYPE_BOOL,
	"attacker_team": TYPE_INT,
	"battlefield_map": TYPE_STRING,
	"brief_digest": TYPE_STRING,
	"committed_armies": TYPE_PACKED_INT32_ARRAY,
	"defender": TYPE_INT,
	"defender_faction": TYPE_STRING,
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
## controller, world_cp, hero_cp, defeated}.
var players: Array[Dictionary] = []
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
func setup(source_world: WorldScript, seats: Array) -> bool:
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
	armies[id] = {
		"id": id,
		"owner": player,
		"region": region_id,
		"kind": kind,
		"spawn_name": army_name,
		"roster": roster_name,
		"hero_template": String(spawn.get("hero_template_name", "")),
		"command_points": _roster_command_points(roster),
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

	# COMMIT. Nothing below can fail.
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

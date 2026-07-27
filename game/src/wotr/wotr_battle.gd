extends RefCounted

## The strategic->tactical BRIDGE: it turns a handoff brief into tactical match
## configuration, and a tactical winner back into strategic state.
##
## `wotr_handoff.gd` builds the brief and stops there. This file is the other
## half of that contract - the half its header said did not exist yet. It does
## two things and refuses to do a third:
##
##   configure()      brief  -> {team_roster, gameplay_rules, commitment}
##   apply_outcome()  winner -> strategic state changed, transaction closed
##
## WHAT IT DOES NOT DO, deliberately: it does not build map geometry. The
## tactical sim's `map_configuration` wants validated spawn positions, three ford
## gates and a live route provider (see `_apply_map_configuration`); the brief
## carries a map NAME. Those are not the same kind of thing and no honest
## function turns one into the other. The caller supplies map geometry the same
## way the vertical slice already does; this file supplies only what the
## strategic layer actually authorises.
##
## NO DEPENDENCY ON THE TACTICAL SIM. This file preloads nothing from
## `res://src/retail_slice/`, so the strategic layer stays loadable and testable
## with no tactical simulation in existence - the property `wotr_world.gd`'s
## header asks for. The two team constants below mirror the sim's `PLAYER_TEAM`
## and `ENEMY_TEAM`; a test asserts that correspondence rather than a preload,
## so the coupling is checked without being structural.
##
## IT NEVER GUESSES. Every value it cannot derive is a named refusal, not a
## plausible default. That rule is not stylistic: c930b68 is a faction gate that
## evaluated, never fired, and logged nothing, because a lookup answered a
## plausible-but-wrong token instead of refusing.

const StateScript = preload("res://src/wotr/wotr_state.gd")
const HandoffScript = preload("res://src/wotr/wotr_handoff.gd")

## Aliased, not restated. `wotr_state.gd` owns the commitment schema because
## `begin_battle()` has to validate what it admits into the hash and cannot
## preload this file (this file preloads it).
const SCHEMA := StateScript.BATTLE_COMMITMENT_SCHEMA
const SCHEMA_VERSION := StateScript.BATTLE_COMMITMENT_SCHEMA_VERSION

## Tactical team assignment. FIXED, not derived from the strategic player
## indices, because the tactical sim's victory rule and its observer framing both
## privilege team 0: `_resolve_victory()` breaks ties toward the lowest surviving
## team id, and the EVA/music frame is written from `PLAYER_TEAM`'s point of
## view. Deriving the mapping from strategic seat numbers would make those two
## behaviours depend on which seat happened to attack.
const ATTACKER_TEAM := 0
const DEFENDER_TEAM := 1

## `winner` while a tactical match is still undecided.
const UNDECIDED := -1


## Translate a brief into tactical match configuration.
##
## `faction_bindings` maps a LIVING-WORLD faction name (the verbatim `Faction =`
## value on a `LivingWorldPlayerTemplate`, e.g. `FactionMen`) to a PACK faction
## id (e.g. `men`). It is a required argument with no default table on purpose:
## the two vocabularies are genuinely different, nothing in the repo maps between
## them today, and inventing the mapping here - by stripping a `Faction` prefix
## and lowercasing, say - would be a guess wearing the costume of a lookup.
##
## `map_bindings` maps a REGION MAP NAME (the verbatim `MapName =` value on a
## `LivingWorldRegion`, e.g. `MAP WOR Cair Andros`) to a PACK MAP id (e.g.
## `bfme2.map.dagorlad`). It is required and has no default for the same reason
## `faction_bindings` does, and it exists at all because of a measured gap: NO
## `MAP WOR *` map is cooked in any content pack, so the map the region names
## and the map the tactical simulation can actually boot are never the same map
## today. A binding is therefore a STAND-IN, and the caller owes the player a
## visible statement of which one it chose - it must never look like the region's
## own battlefield.
##
## THERE IS NO `human_player` ARGUMENT, and its removal is the point. See the
## note on `commitment_matches_brief()`: every value that reaches hashed tactical
## state must be reachable from the commitment, and a per-session seat number is
## not. `is_ai` now comes from the seat's `controller`, which is authoritative
## strategic state carried through the brief.
##
## Returns `{ok, refusals, team_roster, gameplay_rules, commitment}`. On refusal
## `ok` is false, `refusals` names every reason, and the other three are empty -
## a caller can never be handed a half-configured match.
static func configure(
	brief: Dictionary,
	faction_bindings: Dictionary,
	map_bindings: Dictionary
) -> Dictionary:
	var refusals := PackedStringArray()
	if brief.is_empty():
		return _refused(refusals, "brief is empty")
	if String(brief.get("schema", "")) != HandoffScript.SCHEMA:
		return _refused(refusals, "brief schema is not %s" % HandoffScript.SCHEMA)
	if int(brief.get("schema_version", -1)) != HandoffScript.SCHEMA_VERSION:
		return _refused(refusals, "unsupported brief schema_version %d" % int(brief.get("schema_version", -1)))

	var attacker := brief.get("attacker", {}) as Dictionary
	var defender := brief.get("defender", {}) as Dictionary
	var attacker_player := int(attacker.get("player", StateScript.NEUTRAL))
	var defender_player := int(defender.get("player", StateScript.NEUTRAL))

	# An unowned region has no defending side. Retail auto-resolves those on the
	# strategic map rather than fighting them, so refusing here is a NAMED
	# BOUNDARY, not a hole: a tactical match needs two sides and this brief
	# describes one.
	if defender_player == StateScript.NEUTRAL:
		refusals.append("region %s is unowned; a neutral region has no tactical defending side" % String((brief.get("region", {}) as Dictionary).get("id", "")))
	if attacker_player == StateScript.NEUTRAL:
		refusals.append("brief carries no attacking player")

	var attacker_faction := _bind_faction(attacker, faction_bindings, "attacker", refusals)
	var defender_faction := _bind_faction(defender, faction_bindings, "defender", refusals)

	# An attack with nothing committed is not a battle. The strategic layer can
	# produce one (the staging region emptied between brief and commit), and a
	# tactical match seeded with an empty roster would resolve instantly for the
	# defender - a real result derived from a configuration error.
	var committed := _army_ids(attacker)
	if committed.is_empty():
		refusals.append("attacker commits no armies")

	var region_row := brief.get("region", {}) as Dictionary
	var battlefield := _bind_battlefield(region_row, map_bindings, refusals)

	if not refusals.is_empty():
		return {
			"ok": false,
			"refusals": refusals,
			"team_roster": [],
			"gameplay_rules": {},
			"commitment": {},
		}

	# A PARTIAL rules overlay, carrying only what the strategic layer actually
	# authorises. Unit rules, the faction manifest and the retail side table come
	# from the content pack, not from a campaign map; merging them in here would
	# put this file in the business of describing units, which it cannot do.
	var gameplay_rules: Dictionary = {}
	var settings := brief.get("settings", {}) as Dictionary
	var starting_cash := int(settings.get("starting_cash", -1))
	# -1 is the world reader's UNAUTHORED sentinel, not a value. Passing it
	# through would clamp to 0 in `_apply_gameplay_rules` and hand both sides a
	# empty purse that no one authored. Absent stays absent, and the sim's own
	# default stands.
	if starting_cash >= 0:
		gameplay_rules["starting_resources"] = starting_cash

	var region := brief.get("region", {}) as Dictionary
	var commitment: Dictionary = {
		"schema": SCHEMA,
		"schema_version": SCHEMA_VERSION,
		"region": String(region.get("id", "")),
		"map_name": String(region.get("map_name", "")),
		"turn": int(brief.get("turn_index", 0)),
		"attacker": attacker_player,
		"defender": defender_player,
		"attacker_team": ATTACKER_TEAM,
		"defender_team": DEFENDER_TEAM,
		# THE RESOLVED tactical seating, not the inputs it was resolved from. The
		# bound PACK faction ids rather than the binding table, so a swapped
		# binding mints a different commitment; the derived `is_ai` rather than a
		# seat number, so it is a value both peers already agree on. `team_roster`
		# below is a pure projection of these four fields - see
		# `team_roster_for()`.
		"attacker_faction": attacker_faction,
		"defender_faction": defender_faction,
		# THE GROUND. The bound PACK map rather than the binding table, for the
		# same reason the bound factions are here rather than their table: a
		# different table mints a different commitment, and the strategic hashes
		# part company before a match is ever built.
		"battlefield_map": battlefield,
		"attacker_is_ai": _is_ai(attacker),
		"defender_is_ai": _is_ai(defender),
		"staging_region": String(attacker.get("staging_region", "")),
		"committed_armies": committed,
		"defending_armies": _army_ids(defender),
		# THE CHAIN LINK between the two hashes. See the note on
		# `commitment_matches_brief()` below.
		"brief_digest": StateScript.canonical_digest(brief),
	}

	return {
		"ok": true,
		"refusals": PackedStringArray(),
		"team_roster": team_roster_for(commitment),
		"gameplay_rules": gameplay_rules,
		"commitment": commitment,
	}


## The tactical team roster a commitment authorises - a PURE PROJECTION of the
## commitment and nothing else, which is what makes the chain structural rather
## than argued. Every field the sim reads out of a roster descriptor is a field
## some peer can read back out of the hashed strategic record; a value that is
## not in the commitment cannot reach the roster, because there is nowhere else
## for this function to get one.
##
## Order is fixed (attacker first) so the sim's descriptor insertion order - and
## therefore its own team ordering - is reproducible.
static func team_roster_for(commitment: Dictionary) -> Array:
	if commitment.is_empty():
		return []
	return [
		{
			"team": int(commitment.get("attacker_team", ATTACKER_TEAM)),
			"faction": String(commitment.get("attacker_faction", "")),
			"is_ai": bool(commitment.get("attacker_is_ai", true)),
		},
		{
			"team": int(commitment.get("defender_team", DEFENDER_TEAM)),
			"faction": String(commitment.get("defender_faction", "")),
			"is_ai": bool(commitment.get("defender_is_ai", true)),
		},
	]


## ONE HASH OR TWO? Two, chained - and this function is the chain.
##
## Two, because the layers have different lifetimes and different clocks. A
## strategic state outlives every battle fought inside it; a tactical sim exists
## for the length of one. A single conflated hash would churn thirty times a
## second during a battle, and two peers sitting BETWEEN battles could not
## compare strategic state at all without also carrying a dead tactical sim to
## hash alongside it.
##
## But two hashes that can disagree about the same match is exactly the desync
## e56a0d4 demonstrated - peers agreeing on a hash and diverging anyway. The
## resolution is not to merge them; it is to make one DERIVE the other's inputs.
## The strategic hash covers `pending_battle`, which carries `brief_digest`: a
## digest of the complete brief, taken with the same canonicaliser
## `state_hash()` uses. The brief is a pure function of strategic state (its own
## header says DERIVED, not authored), and the brief is the entire input to
## `configure()`. So:
##
##   strategic hashes agree  =>  brief digests agree
##                           =>  briefs agree
##                           =>  team roster and rules overlay agree
##                           =>  the tactical sims were configured identically
##
## and from there the tactical hash is load-bearing on its own. Divergence
## cannot hide in the gap between the layers, because there is no gap: agreement
## upstream is checked, by a value inside the upstream hash, to imply identical
## downstream configuration. Either hash catches its own half.
##
## THAT ARGUMENT WAS FALSE AS ORIGINALLY WRITTEN, and this note records why, so
## the property is not re-lost. "The brief is the entire input to configure()"
## was not true: `configure()` took three arguments, and only the brief was
## digested. Both of the other two reached HASHED tactical state.
##
##   `human_player` decided each side's `is_ai`, which seeds `_team_ai_state` in
##   the tactical sim. Two peers with identical strategic states, identical
##   briefs, identical commitments and EQUAL STRATEGIC HASHES produced sim hashes
##   25cf66b6... and be06f4b1... - one passing seat 0, the other seat 1, exactly
##   as the argument's own documentation described it being used. That is
##   e56a0d4's desync reproduced through the mechanism built to prevent it.
##
##   `faction_bindings` decided which pack faction each side fought as. Swapping
##   the table minted a BYTE-IDENTICAL commitment while opposite factions reached
##   the sim.
##
## The fix is not to digest the extra arguments. Digesting `human_player` would
## be actively wrong: it is per-peer by construction (in a two-human War of the
## Ring, seat 0's human is on one machine and seat 1's on the other), so putting
## it in the commitment - which lives inside the strategic hash - would guarantee
## that two correctly-synchronised peers NEVER agree. That trades a silent desync
## for a permanent false alarm.
##
## Instead: `configure()` takes a brief plus RESOLUTION TABLES ONLY, and none of
## them can carry an unhashed value into the sim.
##
##   * `is_ai` is derived from the seat's `controller`, which is authoritative
##     strategic state, hashed, and carried through the brief. This is what the
##     multiplayer path already does - `_menu_sim_team_roster()` reads a shared
##     lobby `controller` field, never a per-machine seat number.
##   * `faction_bindings` stays an argument because it is a resolution table, not
##     state; but its RESULT - the two bound pack faction ids - is recorded in
##     the commitment, so a divergent table mints a divergent commitment and the
##     strategic hashes part company before a match is ever built.
##   * `map_bindings` (schema version 2) follows the same rule for the same
##     reason: a table in, the bound `battlefield_map` recorded. It was added
##     because the ground a battle is fought on is as load-bearing as the
##     factions fighting on it, and a UI that picked one and passed it to the sim
##     alongside the roster would be the third instance of this defect.
##
## And the roster handed to the sim is `team_roster_for(commitment)`, a pure
## projection. The property is now mechanical: there is no code path by which a
## value reaches the tactical roster without first being in the record the
## strategic hash covers.
##
## Returns true when `commitment` was minted from `brief`.
static func commitment_matches_brief(commitment: Dictionary, brief: Dictionary) -> bool:
	if commitment.is_empty() or brief.is_empty():
		return false
	return String(commitment.get("brief_digest", "")) == StateScript.canonical_digest(brief)


## Which strategic seat a tactical team belongs to, or NEUTRAL when the team is
## not one of this battle's two.
static func player_for_team(commitment: Dictionary, team: int) -> int:
	if commitment.is_empty():
		return StateScript.NEUTRAL
	if team == int(commitment.get("attacker_team", ATTACKER_TEAM)):
		return int(commitment.get("attacker", StateScript.NEUTRAL))
	if team == int(commitment.get("defender_team", DEFENDER_TEAM)):
		return int(commitment.get("defender", StateScript.NEUTRAL))
	return StateScript.NEUTRAL


## Apply a decided tactical match back to strategic state, and close the
## transaction. `winner_team` is the tactical sim's `winner`.
##
## Attacker won: the defending garrison is destroyed, the region changes hands,
## and the committed armies advance into it. Defender won: the committed armies
## are destroyed and the region does not move. Either way the pending battle is
## cleared, so the same result can never be applied twice.
##
## THE LOSING SIDE DIES IN BOTH BRANCHES, and the asymmetry this replaces was a
## bug rather than a named gap. `defending_armies` was written by `configure()`
## and read by nothing: on an attacker victory the region changed hands and the
## defeated garrison simply stayed where it was - inside a region its owner no
## longer held, still answering to the defender, free to march away on the next
## command. `battle_outcome_report` is listed unsupported, but that gap is about
## SURVIVOR ROSTERS - which of the winner's units lived, and at what strength.
## Total loss of the defeated side is not a survivor roster; it is the same
## fidelity the defender-win branch already applied to the attacker, and applying
## it in only one direction was not a simplification, it was two different
## games depending on who won.
##
## Defenders are destroyed BEFORE attackers advance, so the region is emptied and
## then occupied rather than briefly holding both sides.
##
## Returns `{ok, refusals, winner_player, region, captured, armies_advanced,
## armies_lost}`. `armies_lost` carries the defeated side in either branch.
##
## A PARTIAL application is REPORTED, not rolled back and not hidden. If the
## region changes hands but one committed army cannot advance - the destination's
## command-point cap is a real strategic rule, and a large enough attacking force
## legitimately trips it - then `captured` is true, that army is absent from
## `armies_advanced`, and `refusals` names it while `ok` is false. Rolling the
## capture back would discard a battle that was genuinely won; reporting `ok`
## anyway would hide an army that is now standing somewhere nobody chose.
## Reporting exactly what moved is the only one of the three a caller can act on.
static func apply_outcome(state: StateScript, winner_team: int) -> Dictionary:
	var refusals := PackedStringArray()
	if state == null:
		return _outcome_refused(refusals, "state is null")
	var commitment := state.pending_battle
	if commitment.is_empty():
		return _outcome_refused(refusals, "no battle is in flight")

	# An UNDECIDED match must never be applied. `winner` is -1 until
	# `_resolve_victory()` fires, and a caller that reads it one tick early would
	# otherwise hand us a -1 that matches neither team - which, without this
	# guard, would silently take the "defender won" branch and destroy the
	# attacker's army for nothing.
	if winner_team == UNDECIDED:
		return _outcome_refused(refusals, "tactical match is undecided (winner is -1)")

	var winner_player := player_for_team(commitment, winner_team)
	if winner_player == StateScript.NEUTRAL:
		return _outcome_refused(
			refusals,
			"tactical team %d is not a side of this battle" % winner_team
		)

	var region_id := String(commitment.get("region", ""))
	var attacker := int(commitment.get("attacker", StateScript.NEUTRAL))
	var committed: PackedInt32Array = commitment.get("committed_armies", PackedInt32Array())
	var captured := false
	var advanced: Array[int] = []
	var lost: Array[int] = []

	if winner_player == attacker:
		# Sorted ascending (the commitment stores them sorted), so the removal
		# order is reproducible. An id already gone is REPORTED, not shrugged off:
		# it means something removed a defender between the commitment and the
		# result, which is a strategic-state divergence worth seeing.
		var defending: PackedInt32Array = commitment.get("defending_armies", PackedInt32Array())
		for army_id in defending:
			if state.remove_army(int(army_id)):
				lost.append(int(army_id))
			else:
				refusals.append("defending army %d could not be removed" % int(army_id))
		captured = state.transfer_region(region_id, attacker)
		if not captured:
			refusals.append("region %s did not change hands" % region_id)
		# Sorted ascending already (the commitment stores them sorted), so the
		# advance order - and therefore which army trips the command-point cap
		# when the force is too large - is reproducible.
		for army_id in committed:
			if state.move_army(int(army_id), region_id):
				advanced.append(int(army_id))
			else:
				refusals.append("army %d could not advance into %s" % [int(army_id), region_id])
	else:
		for army_id in committed:
			if state.remove_army(int(army_id)):
				lost.append(int(army_id))
			else:
				refusals.append("army %d could not be removed" % int(army_id))

	state.clear_battle()
	return {
		"ok": refusals.is_empty(),
		"refusals": refusals,
		"winner_player": winner_player,
		"region": region_id,
		"captured": captured,
		"armies_advanced": PackedInt32Array(advanced),
		"armies_lost": PackedInt32Array(lost),
	}


# --- internals ---------------------------------------------------------------

static func _bind_faction(
	side: Dictionary,
	faction_bindings: Dictionary,
	role: String,
	refusals: PackedStringArray
) -> String:
	var strategic := String(side.get("faction", ""))
	if strategic.is_empty():
		refusals.append("%s carries no faction" % role)
		return ""
	if not faction_bindings.has(strategic):
		# Named, exactly like `team_retail_side()` refuses. The faction is in the
		# message because "a faction is unbound" sends nobody anywhere.
		refusals.append("%s faction '%s' has no pack faction binding" % [role, strategic])
		return ""
	var pack_faction := String(faction_bindings[strategic])
	if pack_faction.is_empty():
		refusals.append("%s faction '%s' binds to an empty pack faction" % [role, strategic])
		return ""
	return pack_faction


## The pack map a region's battle is fought on, or "" with a NAMED refusal.
## Never guesses a battlefield: an unbound region map refuses rather than
## quietly picking whichever map happens to be first, because that choice
## decides the battle and would then be a value the hash never saw.
static func _bind_battlefield(
	region: Dictionary,
	map_bindings: Dictionary,
	refusals: PackedStringArray
) -> String:
	var region_map := String(region.get("map_name", ""))
	if region_map.is_empty():
		refusals.append("region %s carries no map name" % String(region.get("id", "")))
		return ""
	if not map_bindings.has(region_map):
		refusals.append("region map '%s' has no pack map binding" % region_map)
		return ""
	var pack_map := String(map_bindings[region_map])
	if pack_map.is_empty():
		refusals.append("region map '%s' binds to an empty pack map" % region_map)
		return ""
	return pack_map


## Whether a side's seat is machine-driven, from the seat's own `controller`.
## Normalised through `wotr_state.gd` rather than compared here, so the strategic
## layer and the tactical roster can never disagree about what a value means.
static func _is_ai(side: Dictionary) -> bool:
	return StateScript.normalized_controller(
		side.get("controller", StateScript.CONTROLLER_AI)
	) != StateScript.CONTROLLER_HUMAN


static func _army_ids(side: Dictionary) -> PackedInt32Array:
	var ids: Array[int] = []
	for row in side.get("armies", []) as Array:
		ids.append(int((row as Dictionary).get("army_id", 0)))
	ids.sort()
	return PackedInt32Array(ids)


static func _refused(refusals: PackedStringArray, reason: String) -> Dictionary:
	refusals.append(reason)
	return {
		"ok": false,
		"refusals": refusals,
		"team_roster": [],
		"gameplay_rules": {},
		"commitment": {},
	}


static func _outcome_refused(refusals: PackedStringArray, reason: String) -> Dictionary:
	refusals.append(reason)
	return {
		"ok": false,
		"refusals": refusals,
		"winner_player": StateScript.NEUTRAL,
		"region": "",
		"captured": false,
		"armies_advanced": PackedInt32Array(),
		"armies_lost": PackedInt32Array(),
	}

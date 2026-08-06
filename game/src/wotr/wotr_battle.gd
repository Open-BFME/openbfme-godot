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
const ReinforcementsScript = preload("res://src/wotr/wotr_battle_reinforcements.gd")

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
## `requested_battle_type` is the attacker's per-battle pick when the campaign
## rule offers both ("Auto Resolve and RTS"). It is an ARGUMENT and not state,
## for the same reason the two binding tables are - and, like them, its RESULT
## and not itself is recorded, in the commitment's `battle_type`. A peer that
## requested differently mints a different commitment and the strategic hashes
## part company before a battle is fought, which is the property that makes an
## argument safe here. Ignored entirely when the campaign rule fixes the type.
static func configure(
	brief: Dictionary,
	faction_bindings: Dictionary,
	map_bindings: Dictionary,
	requested_battle_type: String = ""
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
			"reinforcement_feed": {},
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
	# THE PREBUILT FORTRESS, carried because the strategic layer authorises it:
	# `has_fort` is derived by the handoff from the region's own `CreateAutoFort`
	# (or a built fort), and the WITH-FORT purse above already answers to it.
	# The tactical sim does not raise a standing fortress from this flag yet -
	# that half of `prebuilt_fortress` stays a named gap in `wotr_handoff.gd` -
	# but the overlay now CARRIES the authorisation, so the sim's half is a
	# consumer to write rather than a contract still to design.
	if bool(region.get("has_fort", false)):
		gameplay_rules["prebuilt_fortress"] = true
	# THE REGION'S OWN BONUSES, verbatim from the brief (which derived them from
	# the region's authored `bonuses` block). No semantics are invented here: the
	# strategic layer authorises the numbers, the overlay carries them, and how a
	# tactical rule consumes an `attack` or `defense` bonus is the remaining
	# tactical half of the `region_bonus_modifiers` gap. Absent stays absent.
	var region_bonuses := region.get("bonuses", {}) as Dictionary
	if not region_bonuses.is_empty():
		gameplay_rules["region_bonuses"] = region_bonuses.duplicate(true)
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
		# THE VERSION 3 FIELDS. Same rule as `battlefield_map` above: the values
		# themselves, not the settings they came from. Both handicaps decide
		# auto-resolve damage AND seed the dice, and the battle type decides
		# which resolution path runs at all - so all four have to be inside the
		# record the strategic hash covers or they are unhashed inputs to a
		# result, which is the defect this file has now been fixed against three
		# times.
		"battle_type": StateScript.resolve_battle_type(
			StateScript.normalized_battle_type(brief.get("battle_type", "")),
			StateScript.normalized_battle_priority(brief.get("battle_type_priority", "")),
			String(requested_battle_type).strip_edges().to_lower()),
		"battle_type_priority": StateScript.normalized_battle_priority(
			brief.get("battle_type_priority", "")),
		"attacker_handicap": StateScript.normalized_handicap(attacker.get("handicap", 0)),
		"defender_handicap": StateScript.normalized_handicap(defender.get("handicap", 0)),
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
		# THE STAGED FEED, derived from the same brief the commitment digests, so
		# it inherits the chain-of-custody argument team_roster does: agreeing
		# strategic hashes imply agreeing feeds. It carries its OWN ok/refusals
		# rather than failing the whole configuration, because an auto-resolved
		# battle needs no feed at all and a document that never authored
		# SecondsPerReinforcement must not make auto-resolve campaigns unplayable.
		# A launcher starting a TACTICAL match must check `reinforcement_feed.ok`
		# and surface its refusals; spawning the whole roster at match start
		# instead is the exact behaviour the `reinforcement_schedule` gap names.
		"reinforcement_feed": ReinforcementsScript.schedule_for(brief),
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
static func _proven_hero_leaders(state: StateScript, ids: PackedInt32Array) -> Dictionary:
	var proven: Dictionary = {}
	for army_id in ids:
		if not state.armies.has(int(army_id)):
			continue
		var army := state.armies[int(army_id)] as Dictionary
		if String(army.get("kind", "")) != StateScript.ARMY_HERO:
			continue
		var template := String(army.get("hero_template", ""))
		var matches: Array = []
		for unit in army.get("units", []) as Array:
			if String((unit as Dictionary).get("template", "")) == template:
				matches.append(unit)
		if not template.is_empty() and matches.size() == 1:
			proven[int(army_id)] = (matches[0] as Dictionary).duplicate(true)
	return proven


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
	var defeated_ids: PackedInt32Array = (
		commitment.get("defending_armies", PackedInt32Array())
		if winner_player == attacker else committed
	)
	var retreat_heroes: Dictionary = _proven_hero_leaders(state, defeated_ids)
	var rollback_snapshot := state.snapshot()
	var rollback_events := state.events.duplicate(true)
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
			if retreat_heroes.has(int(army_id)):
				if not state.queue_hero_retreat(int(army_id), retreat_heroes[int(army_id)]):
					refusals.append("defending hero army %d could not enter retreat" % int(army_id))
			else:
				var unproven_hero := (state.armies.has(int(army_id))
					and String((state.armies[int(army_id)] as Dictionary).get("kind", "")) == StateScript.ARMY_HERO)
				if state.remove_army(int(army_id)):
					lost.append(int(army_id))
					if unproven_hero:
						state.events.append({"kind": "defeated_hero_leader_unprovable_destroyed",
							"army": int(army_id), "turn": state.turn_index})
				else:
					refusals.append("defending army %d could not be removed" % int(army_id))
		captured = state.transfer_region(region_id, attacker)
		if not captured:
			refusals.append("region %s did not change hands" % region_id)
		# Sorted ascending already (the commitment stores them sorted), so the
		# advance order - and therefore which army trips the command-point cap
		# when the force is too large - is reproducible.
		for army_id in committed:
			if state._move_army_phase_internal(int(army_id), region_id):
				advanced.append(int(army_id))
			else:
				refusals.append("army %d could not advance into %s" % [int(army_id), region_id])
	else:
		for army_id in committed:
			if retreat_heroes.has(int(army_id)):
				if not state.apply_attrition(int(army_id), [retreat_heroes[int(army_id)]]):
					refusals.append("hero army %d could not be stripped to its leader" % int(army_id))
				else:
					state.events.append({"kind": "retreat_completed_in_place",
						"army": int(army_id), "turn": state.turn_index})
			else:
				var unproven_hero := (state.armies.has(int(army_id))
					and String((state.armies[int(army_id)] as Dictionary).get("kind", "")) == StateScript.ARMY_HERO)
				if state.remove_army(int(army_id)):
					lost.append(int(army_id))
					if unproven_hero:
						state.events.append({"kind": "defeated_hero_leader_unprovable_destroyed",
							"army": int(army_id), "turn": state.turn_index})
				else:
					refusals.append("army %d could not be removed" % int(army_id))

	if not refusals.is_empty():
		if state.restore(rollback_snapshot):
			state.events = rollback_events
		else:
			refusals.append("battle outcome rollback restore failed")
		return _outcome_refused(refusals, "battle outcome rolled back after a strategic refusal")
	state.clear_battle()
	state.end_phase()
	return {
		"ok": refusals.is_empty(),
		"refusals": refusals,
		"winner_player": winner_player,
		"region": region_id,
		"captured": captured,
		"armies_advanced": PackedInt32Array(advanced),
		"armies_lost": PackedInt32Array(lost),
	}


## Is THIS battle auto-resolved? A pure read of the commitment - no rule is
## re-applied here, because the rule was applied once at `configure()` and its
## answer was recorded. Two peers reading the same commitment cannot disagree.
static func is_auto_resolved(commitment: Dictionary) -> bool:
	if commitment.is_empty():
		return false
	return String(commitment.get("battle_type", "")) == StateScript.BATTLE_TYPE_AUTO_RESOLVE


## THE SIDES an auto-resolved battle is fought with, read out of the strategic
## state the commitment names. Returns
## `{ok, refusals, attacker, defender}` where each side is the shape
## `wotr_autoresolve_battle.resolve()` wants.
##
## THE UNITS COME FROM THE ARMY RECORDS, not from the roster the army was raised
## from, so an army that has already fought arrives at its second battle as
## damaged as it left the first. An army with NO units is refused BY NAME rather
## than fielded empty - an empty army would lose instantly and the loss would
## look like a battle result rather than a missing binding.
static func auto_resolve_sides(state: StateScript) -> Dictionary:
	var refusals := PackedStringArray()
	if state == null:
		return {"ok": false, "refusals": PackedStringArray(["state is null"]),
			"attacker": {}, "defender": {}}
	var commitment := state.pending_battle
	if commitment.is_empty():
		return {"ok": false, "refusals": PackedStringArray(["no battle is in flight"]),
			"attacker": {}, "defender": {}}
	var sides: Dictionary = {}
	for role in ["attacker", "defender"]:
		var seat := int(commitment.get(role, StateScript.NEUTRAL))
		var ids: PackedInt32Array = commitment.get(
			"committed_armies" if role == "attacker" else "defending_armies", PackedInt32Array())
		var armies: Array = []
		for army_id in ids:
			if not state.armies.has(int(army_id)):
				refusals.append("%s army %d named by the commitment is gone" % [role, int(army_id)])
				continue
			var army := state.armies[int(army_id)] as Dictionary
			var units: Array = army.get("units", [])
			if units.is_empty():
				refusals.append(
					("%s army %d fields no auto-resolve units, so it cannot be resolved; "
					+ "its roster '%s' has no binding in the auto-resolve binding bundle") % [
						role, int(army_id), String(army.get("roster", ""))])
				continue
			armies.append(units)
		# AN UNDEFENDED REGION IS NOT A REFUSAL. The strategic layer legitimately
		# lets a player attack a region its owner has left empty, and retail's
		# own end condition already covers it - "the battle is over when all
		# units with CanBeAttacked = Yes are gone" is true of that side before
		# round one, so the resolver declares the other side the winner with no
		# rounds fought and no attrition. Inventing a refusal here would make a
		# legal move impossible; inventing a battle would fabricate casualties.
		#
		# What IS refused is a side that HAS armies none of which can field a
		# unit, because that is a missing binding wearing a walkover's costume.
		if armies.is_empty() and not ids.is_empty():
			refusals.append("the %s has armies but none of them can field a unit" % role)
		sides[role] = {
			"armies": armies,
			"handicap": int(commitment.get("%s_handicap" % role, 0)),
			# The living-world player template name is what retail's resource and
			# science bonus tables are keyed by. Both tables ship with every
			# non-1.0 tier commented out in RotWK, so these look up to 1.0 today -
			# which is retail's data saying nothing, not this project skipping it.
			"side": String((state.players[seat] as Dictionary).get("template", ""))
				if seat >= 0 and seat < state.players.size() else "",
			"resource": 0.0,
			"sciencePoints": 0.0,
		}
	if not refusals.is_empty():
		return {"ok": false, "refusals": refusals, "attacker": {}, "defender": {}}
	return {"ok": true, "refusals": refusals,
		"attacker": sides["attacker"], "defender": sides["defender"]}


## Apply an AUTO-RESOLVED result and close the transaction. This is the outcome
## path `apply_outcome(winner_team)` cannot be, because auto-resolve does not
## return a boolean - it returns who is left and how hurt they are.
##
## THREE BRANCHES, and the third is the one a tactical battle never has:
##
##   attacker won   every side's attrition is written back, the defeated
##                  garrison is removed, the region changes hands, and the
##                  surviving committed armies advance into it.
##   defender won   attrition is written back and the region does not move.
##   UNDECIDED      the battle reached this project's round bound. NO winner is
##                  invented and the region does not move - but the attrition IS
##                  applied, because both armies really did spend four hundred
##                  rounds hitting each other. Retail states no round cap and so
##                  states nothing about this case; leaving both armies at full
##                  strength would be inventing a result just as much as naming
##                  a winner would.
##
## Returns `{ok, refusals, winner_player, region, captured, armies_advanced,
## armies_lost, armies_reduced, undecided}`.
static func apply_auto_resolve_outcome(state: StateScript, outcome: Dictionary) -> Dictionary:
	var refusals := PackedStringArray()
	if state == null:
		return _auto_refused(refusals, "state is null")
	var commitment := state.pending_battle
	if commitment.is_empty():
		return _auto_refused(refusals, "no battle is in flight")
	if not bool(outcome.get("ok", false)):
		return _auto_refused(refusals, "the auto-resolve produced no result: %s" % ", ".join(
			Array(outcome.get("refusals", PackedStringArray()))))

	var winner := String(outcome.get("winner", ""))
	return _settle_survivor_report(state, commitment, winner, _survivors_by_army(outcome), refusals)


## Apply a FOUGHT (tactical) battle's result and close the transaction, with
## the SAME contract auto-resolve settles under: not a boolean winner but the
## surviving roster, so the strategic layer writes armies back into the region
## through the one door it already has - `apply_attrition()`.
##
## This is the bridge half of the `tactical_battle_outcome_report` gap named in
## `wotr_handoff.gd`: a fought battle used to come home as `apply_outcome(
## winner_team)`, which destroys the whole losing side and returns the whole
## winning side untouched - two fidelities depending on which resolution path
## ran. This function takes `winner_team` (the tactical sim's own `winner`) AND
## `survivors`: the flat list of strategic unit rows still standing, in the
## shape auto-resolve's `_summarise()` emits and `survivors_from_feed()` in
## `wotr_battle_reinforcements.gd` reconstructs from a feed - each row carrying
## the `army_id` it entered the battle under and its remaining
## `hitpoints_milli`. An army every unit of which is absent from `survivors` is
## destroyed; anything else is reduced, exactly as an auto-resolved army is.
##
## VALIDATED BEFORE A SINGLE WRITE. A survivor naming an army outside this
## battle would smuggle units into the strategic hash under an id nobody
## committed, so the whole report refuses by name and the state is untouched -
## a caller retries with a corrected report or applies nothing.
static func apply_fought_outcome(
	state: StateScript, winner_team: int, survivors: Array
) -> Dictionary:
	var refusals := PackedStringArray()
	if state == null:
		return _auto_refused(refusals, "state is null")
	var commitment := state.pending_battle
	if commitment.is_empty():
		return _auto_refused(refusals, "no battle is in flight")
	# The same one-tick-early guard `apply_outcome` carries, for the same
	# reason: -1 matches neither team and must never look like a defender win.
	if winner_team == UNDECIDED:
		return _auto_refused(refusals, "tactical match is undecided (winner is -1)")
	var winner_player := player_for_team(commitment, winner_team)
	if winner_player == StateScript.NEUTRAL:
		return _auto_refused(
			refusals, "tactical team %d is not a side of this battle" % winner_team)

	var allowed: Dictionary = {}
	for army_id in commitment.get("committed_armies", PackedInt32Array()) as PackedInt32Array:
		allowed[int(army_id)] = true
	for army_id in commitment.get("defending_armies", PackedInt32Array()) as PackedInt32Array:
		allowed[int(army_id)] = true
	var grouped: Dictionary = {}
	for row in survivors:
		if not (row is Dictionary):
			return _auto_refused(refusals, "a survivor row is not a unit record")
		var unit := row as Dictionary
		var army_id := int(unit.get("army_id", 0))
		if not allowed.has(army_id):
			return _auto_refused(refusals,
				"survivor '%s' names army %d, which is not a side of this battle" % [
					String(unit.get("template", "<unnamed>")), army_id])
		if not grouped.has(army_id):
			grouped[army_id] = []
		(grouped[army_id] as Array).append(unit)

	var winner := "attacker" \
		if winner_player == int(commitment.get("attacker", StateScript.NEUTRAL)) else "defender"
	return _settle_survivor_report(state, commitment, winner, grouped, refusals)


## THE ONE SETTLEMENT, shared by the auto-resolved and the fought path so the
## strategic consequences of a battle cannot depend on which resolver decided
## it. `winner` is "attacker", "defender", or "" for auto-resolve's undecided
## round-bound case (a fought battle never passes ""; its undecided guard
## refuses upstream). `survivors` is army id -> surviving unit rows.
static func _settle_survivor_report(
	state: StateScript,
	commitment: Dictionary,
	winner: String,
	survivors: Dictionary,
	refusals: PackedStringArray
) -> Dictionary:
	var undecided := winner.is_empty()
	var attacker := int(commitment.get("attacker", StateScript.NEUTRAL))
	var defender := int(commitment.get("defender", StateScript.NEUTRAL))
	var region_id := String(commitment.get("region", ""))
	var committed: PackedInt32Array = commitment.get("committed_armies", PackedInt32Array())
	var defending: PackedInt32Array = commitment.get("defending_armies", PackedInt32Array())

	# A bounded auto-resolve is not a battle decision. Keep the commitment and
	# phase open, and apply no attrition, so the caller may retry or abandon.
	if undecided:
		return _auto_refused(refusals, "battle remains undecided")

	var losing: PackedInt32Array = defending if winner == "attacker" else committed
	var retreat_heroes: Dictionary = {}
	# Validate every defeated hero before the first write. Retail preserves the
	# leader only; if its exact template cannot be proven, refusing is safer than
	# manufacturing a unit (LW_InstructionText11/12).
	for army_id in losing:
		if not state.armies.has(int(army_id)):
			continue
		var army := state.armies[int(army_id)] as Dictionary
		if String(army.get("kind", "")) != StateScript.ARMY_HERO:
			continue
		var exact: Array = []
		var hero_template := String(army.get("hero_template", ""))
		for unit in survivors.get(int(army_id), []) as Array:
			if String((unit as Dictionary).get("template", "")) == hero_template:
				exact.append(unit)
		if not hero_template.is_empty() and exact.size() == 1:
			retreat_heroes[int(army_id)] = (exact[0] as Dictionary).duplicate(true)

	var rollback_snapshot := state.snapshot()
	var rollback_events := state.events.duplicate(true)
	var lost: Array[int] = []
	var reduced: Array[int] = []
	# BOTH SIDES FIRST, in ascending army id, so the order the strategic layer
	# is written in is reproducible and does not depend on which side won.
	var every: Array[int] = []
	for army_id in committed:
		every.append(int(army_id))
	for army_id in defending:
		every.append(int(army_id))
	every.sort()
	for army_id in every:
		if retreat_heroes.has(army_id):
			var hero_ok := (
				state.queue_hero_retreat(army_id, retreat_heroes[army_id])
				if defending.has(army_id)
				else state.apply_attrition(army_id, [retreat_heroes[army_id]])
			)
			if hero_ok:
				reduced.append(army_id)
				if not defending.has(army_id):
					state.events.append({"kind": "retreat_completed_in_place",
						"army": army_id, "turn": state.turn_index})
			else:
				refusals.append("hero army %d could not preserve its leader" % army_id)
			continue
		var unproven_hero := (losing.has(army_id) and state.armies.has(army_id)
			and String((state.armies[army_id] as Dictionary).get("kind", "")) == StateScript.ARMY_HERO)
		var kept: Array = [] if losing.has(army_id) else survivors.get(army_id, [])
		if not state.apply_attrition(army_id, kept):
			refusals.append("attrition for army %d could not be written" % army_id)
			continue
		if kept.is_empty():
			lost.append(army_id)
			if unproven_hero:
				state.events.append({"kind": "defeated_hero_leader_unprovable_destroyed",
					"army": army_id, "turn": state.turn_index})
		else:
			reduced.append(army_id)

	var captured := false
	var advanced: Array[int] = []
	if not undecided and winner == "attacker":
		captured = state.transfer_region(region_id, attacker)
		if not captured:
			refusals.append("region %s did not change hands" % region_id)
		for army_id in committed:
			if not state.armies.has(int(army_id)):
				continue
			if state._move_army_phase_internal(int(army_id), region_id):
				advanced.append(int(army_id))
			else:
				refusals.append("army %d could not advance into %s" % [int(army_id), region_id])

	if not refusals.is_empty():
		if state.restore(rollback_snapshot):
			state.events = rollback_events
		else:
			refusals.append("battle outcome rollback restore failed")
		return _auto_refused(refusals, "battle outcome rolled back after a strategic refusal")
	state.clear_battle()
	state.end_phase()
	lost.sort()
	reduced.sort()
	advanced.sort()
	return {
		"ok": refusals.is_empty(),
		"refusals": refusals,
		"winner_player": StateScript.NEUTRAL if undecided else (
			attacker if winner == "attacker" else defender),
		"undecided": undecided,
		"region": region_id,
		"captured": captured,
		"armies_advanced": PackedInt32Array(advanced),
		"armies_lost": PackedInt32Array(lost),
		"armies_reduced": PackedInt32Array(reduced),
	}


static func _survivors_by_army(outcome: Dictionary) -> Dictionary:
	var grouped: Dictionary = {}
	for role in ["attacker", "defender"]:
		var side: Dictionary = outcome.get(role, {})
		for row in side.get("survivors", []) as Array:
			var unit: Dictionary = row
			var army_id := int(unit.get("army_id", 0))
			if not grouped.has(army_id):
				grouped[army_id] = []
			(grouped[army_id] as Array).append(unit)
	return grouped


static func _auto_refused(refusals: PackedStringArray, reason: String) -> Dictionary:
	refusals.append(reason)
	return {
		"ok": false,
		"refusals": refusals,
		"winner_player": StateScript.NEUTRAL,
		"undecided": true,
		"region": "",
		"captured": false,
		"armies_advanced": PackedInt32Array(),
		"armies_lost": PackedInt32Array(),
		"armies_reduced": PackedInt32Array(),
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
		"reinforcement_feed": {},
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

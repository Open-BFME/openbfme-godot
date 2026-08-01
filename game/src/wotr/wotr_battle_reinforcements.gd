extends RefCounted

## THE STAGED FEED: retail's `SecondsPerReinforcement`, made real for the
## tactical half of a War of the Ring battle.
##
## Retail does not pour a WOTR army onto the battlefield at match start. It
## feeds the army in ONE UNIT AT A TIME, `SecondsPerReinforcement` apart - the
## living-world document authors that value per campaign (300 seconds in every
## RotWK campaign's `rtsSettings`; the BFME2-era documents author 900). The
## tactical slice today spawns whatever roster it is given immediately, which
## is why `reinforcement_schedule` sits in
## `wotr_handoff.UNSUPPORTED_BY_TACTICAL_SIM`. This file is the bridge half of
## closing that gap: it turns a handoff brief into the exact, deterministic
## feed order retail's cadence describes, so a tactical launcher has a schedule
## to obey instead of a roster to dump.
##
## WHAT IS RETAIL'S HERE AND WHAT IS NOT:
##
##   retail's   the cadence itself (`SecondsPerReinforcement`, read from the
##              brief and never from a constant in this file - the brief got it
##              from the document's own `rtsSettings`), and the rule that the
##              feed starts MANNED: retail's own auto-resolve reinforcement
##              schedule pins army 1 to round 0 with the comment "VERY
##              IMPORTANT: do not change this, or battles will end
##              immediately", and the same reasoning holds for a tactical
##              battle - a battlefield empty until the first interval elapsed
##              would decide itself.
##
##   this       the ORDER units are fed in. Retail states the cadence and not
##   project's  the queue, so the queue here is the only one that needs no
##              tie-break heuristic: armies ascending by id, then each army's
##              units in the order the strategic record holds them (which is
##              the roster order they were raised in, minus casualties). Both
##              sides are staged symmetrically, the way retail's auto-resolve
##              schedule ships identical Attacker and Defender tables.
##
## EVERYTHING IS DERIVED FROM THE BRIEF. The brief is digested into the
## commitment and the commitment is inside the strategic hash, so two peers
## whose strategic hashes agree cannot build different feeds - the same
## chain-of-custody argument `wotr_battle.gd::commitment_matches_brief()`
## makes for the team roster.
##
## IT NEVER GUESSES. A document that did not author `SecondsPerReinforcement`
## (the world reader's -1 sentinel) refuses BY NAME rather than inventing a
## cadence, and an army that fields no unit records refuses BY NAME rather
## than being quietly skipped - the same rule `auto_resolve_sides()` applies,
## because an army with no units here means the binding bundle never produced
## any, and feeding nothing would make a missing binding look like an empty
## army.

const HandoffScript = preload("res://src/wotr/wotr_handoff.gd")
const Rules = preload("res://src/wotr/wotr_autoresolve_rules.gd")

## The strategic layer's fixed-point scale, aliased from the auto-resolve rules
## table so the two lanes cannot quietly diverge about what a thousandth is.
const MILLI := Rules.MILLI


## Build the staged feed for both sides of `brief`. Returns
## `{ok, refusals, seconds_per_reinforcement, attacker, defender}` where each
## side is an Array of entries, one per unit, in feed order:
##
##   {feed_index, army_id, template, arrival_seconds, unit}
##
## `arrival_seconds` is `feed_index * seconds_per_reinforcement`, so the first
## unit stands at second 0 and the battlefield is never empty (see the header).
## `unit` is the COMPLETE strategic unit record, duplicated - level, body,
## armour, weapon, current hitpoints and whatever else the strategic layer
## carries on it - so anything another lane adds to a unit (a carried hero
## level, a revive timer) rides the feed without this file naming it.
##
## On refusal `ok` is false, `refusals` names every reason, and both sides are
## empty - a launcher can never be handed half a schedule.
static func schedule_for(brief: Dictionary) -> Dictionary:
	var refusals := PackedStringArray()
	if brief.is_empty():
		return _refused(refusals, "brief is empty")
	if String(brief.get("schema", "")) != HandoffScript.SCHEMA:
		return _refused(refusals, "brief schema is not %s" % HandoffScript.SCHEMA)
	if int(brief.get("schema_version", -1)) != HandoffScript.SCHEMA_VERSION:
		return _refused(refusals, "unsupported brief schema_version %d" % int(
			brief.get("schema_version", -1)))

	var settings := brief.get("settings", {}) as Dictionary
	var seconds := int(settings.get("seconds_per_reinforcement", -1))
	# -1 is the world reader's UNAUTHORED sentinel. A cadence nobody authored
	# must refuse, not default: retail's RotWK documents author 300 and the
	# BFME2 ones 900, and picking either here would be a guessed constant
	# deciding when half an army reaches a battle.
	if seconds < 0:
		refusals.append(
			"the living-world document did not author SecondsPerReinforcement, "
			+ "so no staged feed can be built without inventing retail's cadence")

	var sides: Dictionary = {}
	for role in ["attacker", "defender"]:
		var side := brief.get(role, {}) as Dictionary
		var entries: Array[Dictionary] = []
		var rows: Array = side.get("armies", [])
		# Ascending army id, so the feed order is reproducible and matches the
		# order every other part of the bridge iterates armies in.
		var sorted_rows := rows.duplicate()
		sorted_rows.sort_custom(func(a, b):
			return int((a as Dictionary).get("army_id", 0)) < int((b as Dictionary).get("army_id", 0)))
		for row in sorted_rows:
			var army := row as Dictionary
			var units: Array = army.get("units", [])
			if units.is_empty():
				# The same refusal `auto_resolve_sides()` makes, for the same
				# reason: an army with no unit records is a missing binding
				# wearing an empty army's costume, and feeding around it would
				# hide that from the player.
				refusals.append(
					("%s army %d fields no unit records, so it cannot be staged into "
					+ "a tactical battle; its roster has no binding in the "
					+ "auto-resolve binding bundle") % [role, int(army.get("army_id", 0))])
				continue
			for unit_row in units:
				var unit := (unit_row as Dictionary).duplicate(true)
				entries.append({
					"feed_index": entries.size(),
					"army_id": int(army.get("army_id", 0)),
					"template": String(unit.get("template", "")),
					"arrival_seconds": entries.size() * maxi(seconds, 0),
					"unit": unit,
				})
		sides[role] = entries

	if not refusals.is_empty():
		return _refused_joined(refusals)
	return {
		"ok": true,
		"refusals": PackedStringArray(),
		"seconds_per_reinforcement": seconds,
		"attacker": sides["attacker"],
		"defender": sides["defender"],
	}


## The entries of one side that are DUE at `elapsed_seconds`, given that
## `fed_count` of them have already been fed. Pure and cursor-free: the caller
## owns the count, so two callers polling at different rates cannot disagree
## about which unit comes next.
static func due(entries: Array, elapsed_seconds: int, fed_count: int) -> Array:
	var out: Array = []
	for index in range(maxi(fed_count, 0), entries.size()):
		var entry := entries[index] as Dictionary
		if int(entry.get("arrival_seconds", 0)) > elapsed_seconds:
			break
		out.append(entry)
	return out


## Turn a fought battle's survivor report back into the STRATEGIC unit rows
## `wotr_state.apply_attrition()` accepts - the same contract auto-resolve's
## `_summarise()` fulfils, so the strategic layer cannot tell the two battle
## kinds apart when it writes the result.
##
## `outcomes` maps a surviving unit's `feed_index` to its remaining strength as
## a FRACTION of full, in thousandths (0..1000). A feed index absent from
## `outcomes` is dead. The fraction is the tactical layer's own
## hitpoints-over-max, which makes this a pure unit conversion - no combat
## semantics are invented here, and the arithmetic is integer end to end for
## the reason `wotr_state.gd` states: the strategic hash carries no floats.
static func survivors_from_feed(entries: Array, outcomes: Dictionary) -> Array:
	var survivors: Array = []
	for entry_row in entries:
		var entry := entry_row as Dictionary
		var index := int(entry.get("feed_index", -1))
		if not outcomes.has(index):
			continue
		var fraction := clampi(int(outcomes[index]), 0, MILLI)
		var unit := (entry.get("unit", {}) as Dictionary).duplicate(true)
		var maximum := int(unit.get("max_hitpoints_milli", 0))
		unit["hitpoints_milli"] = maximum * fraction / MILLI
		survivors.append(unit)
	return survivors


static func _refused(refusals: PackedStringArray, reason: String) -> Dictionary:
	refusals.append(reason)
	return _refused_joined(refusals)


static func _refused_joined(refusals: PackedStringArray) -> Dictionary:
	return {
		"ok": false,
		"refusals": refusals,
		"seconds_per_reinforcement": -1,
		"attacker": [],
		"defender": [],
	}

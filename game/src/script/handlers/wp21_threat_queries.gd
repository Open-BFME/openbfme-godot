extends RefCounted

## WP21 THREAT QUERIES - the radius-scoped threat CONDITION pair the retail AI
## polls: UNIT_THREAT_LEVEL (17 call sites) and TEAM_THREAT_LEVEL (8 call
## sites), across 3 retail AI libraries. 25 of the 47 formerly blocked AI call
## sites.
##
## PROVENANCE AND THE OWNER RULING
## ===============================
## Both members were gap-registered by wp09_ai_core.gd as `blocked-subsystem`
## while the world exposed only the subject's OWN threat (units.threat /
## teams.threat, no radius). The world has since grown the exact surface the
## signature asks for - teams.threat_within_radius(team, radius) and
## units.threat_within_radius(object, radius) - implemented by the retail
## slice adapter (retail_slice_script_world.gd:2968 and :6083) as the hostile
## combat-weight sum within the radius of the subject.
##
## A codex full-parity attempt to bind these was REJECTED pending
## retail-sourced threat oracles; that reject was RESCINDED by owner ruling
## 2026-08-08: the slice combat-weight formula is accepted as authoritative
## for script conditions for the playtest era. SEMANTIC DIVERGENCE NOTE: the
## slice threat value is a slice formula, NOT a reverse-sourced SAGE quantity
## - absolute magnitudes may differ from retail; the comparison MECHANICS
## (operator table, positional args, refusal honesty) are what this package
## and its runner pin. A retail-oracle follow-up is filed with the owner.
##
## THE CONDITION FORM ONLY. Both names also appear as ACTIONS in the sourced
## BFME2ROTWK table, but all 25 retail AI call sites are CONDITION call sites
## with condition-shaped arguments (see wp09_ai_core.gd's provenance note).
## This package registers conditions only and never claims the action form.
##
## Table rows served, verbatim from script_condition_table.gd (name | params |
## category | game | confidence):
##
##   "TEAM_THREAT_LEVEL|TEAM,COMPARISON,REAL,REAL|-|CNC3KW|S"   (row 268)
##   "UNIT_THREAT_LEVEL|UNIT,COMPARISON,REAL,REAL|-|CNC3KW|S"   (row 299)
##
## Reference semantics: "<UNIT> has <COMPARISON> threat level <REAL> within
## radius <REAL>". Retail AI shapes: AI_GATE | 0 | 10 | 350 and
## <This Team> | 3 | 1 | 300.
##
## THE ARGUMENT TRAP
## =================
## Every read below is BY INDEX, with the signature written directly above it.
## Both signatures put TWO REAL slots side by side - the threat VALUE (arg 2)
## and the RADIUS (arg 3) - which no type-search could tell apart: swapped,
## "threat > 10 within 350" becomes "threat > 350 within 10", answered
## confidently on every AI poll. The runner keys its fixtures on the exact
## radius with value != radius, so a swapped read misses the fixture loudly.
##
## CONDITIONS MAY NOT GUESS, AND MAY NOT WRITE
## ===========================================
## Both gate polled AI scripts, evaluated an unpredictable number of times per
## match. They write ctx.result ONLY - never the env, never the world. A read
## the world cannot answer must never come back as a plain false: "there is no
## threat within the radius" and "the world cannot measure threat" are
## different answers, and only the first may steer the AI on the record's own
## authority. Every condition checks SageWorldQuery.ok first and returns
## WORLD_REFUSED otherwise, which the dispatcher turns into a false gate value
## *and* a structured gap.

const PACKAGE := "WP21-threat-queries"

const Dispatch := preload("res://src/script/script_dispatch.gd")
const ParamTypes := preload("res://src/script/script_param_types.gd")


static func register(reg: SageScriptHandlerRegistry.Registrar) -> void:
	# wp09_ai_core.gd no longer blocks these names; first-registration-wins
	# means a leftover blocked declaration there would shadow this package with
	# a loud duplicate-registration error - the intended signal, not a merge
	# accident.
	reg.condition("TEAM_THREAT_LEVEL", _team_threat_level)
	reg.condition("UNIT_THREAT_LEVEL", _unit_threat_level)


# --- Shared tails -----------------------------------------------------------


static func _operator_out_of_table(ctx: Dictionary, name: String, comparison: int) -> bool:
	## The comparison operator is consumed by THIS file (the world methods
	## carry no comparison parameter), and ParamTypes.compare answers `false`
	## for any operator it does not know - which would turn "this handler does
	## not understand the operator" into a confident "no threat" at an AI
	## decision gate. An out-of-table operator is therefore BAD_ARGUMENTS,
	## refused loudly and never guessed (same verdict as WP20's proximity
	## comparisons and WP17's PLAYER_HAS_OBJECT_COMPARISON).
	if comparison >= ParamTypes.COMPARE_LESS and comparison <= ParamTypes.COMPARE_NOT_EQUAL:
		return false
	ctx["detail"] = (
		"%s was authored with comparison operator %d, outside the sourced "
		+ "COMPARISON table (0..5); compare would silently answer false for "
		+ "it, so it is refused instead"
	) % [name, comparison]
	return true


# --- Conditions ---------------------------------------------------------------
#
# Each may answer true, may answer false, or may refuse - and the third is not
# the second.


static func _team_threat_level(ctx: Dictionary) -> int:
	# TEAM_THREAT_LEVEL(TEAM, COMPARISON, REAL, REAL)
	#   table row 268: "TEAM_THREAT_LEVEL|TEAM,COMPARISON,REAL,REAL|-|CNC3KW|S"
	#   arg 0: TEAM       - the subject team, text payload
	#   arg 1: COMPARISON - the operator, integer payload
	#   arg 2: REAL       - the threat VALUE compared against, real payload
	#   arg 3: REAL       - the RADIUS around the team, real payload
	#
	# The radius is FORWARDED (args 2 and 3 are the adjacent-REAL trap; see the
	# class comment) and the comparison is evaluated HERE against the threat
	# the world reports - teams.threat_within_radius carries no comparison
	# parameter. Threat is the slice combat-weight sum within the radius of
	# the team centroid (owner ruling 2026-08-08; see the class comment for
	# the divergence note).
	var args: SageScriptArgs = ctx["args"]
	var comparison := args.integer(1)
	if _operator_out_of_table(ctx, "TEAM_THREAT_LEVEL", comparison):
		return Dispatch.Status.BAD_ARGUMENTS
	var query := (ctx["world"] as SageScriptWorld).teams().threat_within_radius(
		args.text(0), args.real(3)
	)
	if not query.ok:
		ctx["detail"] = query.detail
		return Dispatch.Status.WORLD_REFUSED
	ctx["result"] = ParamTypes.compare(query.as_float(), comparison, args.real(2))
	return Dispatch.Status.OK


static func _unit_threat_level(ctx: Dictionary) -> int:
	# UNIT_THREAT_LEVEL(UNIT, COMPARISON, REAL, REAL)
	#   table row 299: "UNIT_THREAT_LEVEL|UNIT,COMPARISON,REAL,REAL|-|CNC3KW|S"
	#   arg 0: UNIT       - the subject object, text payload
	#   arg 1: COMPARISON - the operator, integer payload
	#   arg 2: REAL       - the threat VALUE compared against, real payload
	#   arg 3: REAL       - the RADIUS around the unit, real payload
	#
	# Identical mechanics to the team form, against the Units facet: the slice
	# combat-weight sum within the radius of the unit's position
	# (retail_slice_script_world.gd:6083).
	var args: SageScriptArgs = ctx["args"]
	var comparison := args.integer(1)
	if _operator_out_of_table(ctx, "UNIT_THREAT_LEVEL", comparison):
		return Dispatch.Status.BAD_ARGUMENTS
	var query := (ctx["world"] as SageScriptWorld).units().threat_within_radius(
		args.text(0), args.real(3)
	)
	if not query.ok:
		ctx["detail"] = query.detail
		return Dispatch.Status.WORLD_REFUSED
	ctx["result"] = ParamTypes.compare(query.as_float(), comparison, args.real(2))
	return Dispatch.Status.OK

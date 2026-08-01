extends RefCounted

## WP13-special-powers-combat - the AI-CRITICAL SUBSET.
##
## SCOPE, AND WHY THIS FILE IS NOT THE WHOLE PACKAGE
## =================================================
## WP13 is the special-powers-and-combat package. This file implements the
## EIGHT members that the RETAIL AI SCRIPT LIBRARIES actually call, and nothing
## else. The remaining members (the special-power family, the damage/kill
## family, the rest of the ~20-action command-button family) are a later
## agent's work and will land in a SEPARATE file, because two files may not
## claim one PACKAGE constant (see handlers/_registry.gd). This file's PACKAGE
## is therefore suffixed - "WP13-ai-powers-combat" - and the unsuffixed catalog
## name, "WP13-special-powers-combat", is the one the completing package should
## take, together with the file name wp13_special_powers_combat.gd.
##
## The eight were not chosen by taste. They are the exact WP13 membership of
## game/data/retail_ai_call_census.json - the committed census of the 14 AI_*
## WorldBuilder libraries referenced across the 301 decoded retail maps - which
## is the authority for every count below. Both retail trees carry IDENTICAL
## counts for all eight (checked member-by-member against the census; that is
## NOT a corpus-wide fact - 12 census members differ between BFME2 and RotWK,
## none of them in this file - so the counts below need no tree qualifier).
## In total: 48 call sites, 0.84% of bfme2-retail's 5,728 and 0.77% of
## rotwk-retail's 6,213. `libraryCount` is quoted per member because it
## distinguishes engine-wide vocabulary from one library's idiom:
## NAMED_NOT_DESTROYED is spread across 4 libraries (it is the "is this hero /
## objective object still standing" gate everywhere), while TEAM_DESTROYED's 5
## call sites all live in one.
##
## Counts decide the order things appear in this file, highest first, so the
## most-exercised code is the code a reader meets first:
##
##     18  TEAM_USE_COMMANDBUTTON_ABILITY              action     served (2 libraries)
##     17  NAMED_NOT_DESTROYED                         condition  served (4 libraries)
##      5  TEAM_DESTROYED                              condition  served (1 library)
##      4  EVAL_TEAM_HEALTH                            condition  served (1 library)
##      1  UNIT_HEALTH                                 condition  served (1 library)
##      1  TEAM_IS_ATTACKED_AND_CANNOT_RETALIATE_ALL   condition  served (1 library)
##      1  PLAYER_DESTROYED_N_BUILDINGS_PLAYER         condition  served (1 library)
##      1  NAMED_USE_COMMANDBUTTON_ABILITY             action     served (1 library)
##
##
## EVERY MEMBER IS SERVED - AND WHY THAT IS UNUSUAL
## ================================================
## Unlike the WP11/WP15/WP17 AI subsets, this file GAP-REGISTERS NOTHING: every
## authored argument of all eight signatures has a slot on the existing world
## surface. The two command-button spellings fold into
## orders.use_command_button (scope + target_self() express both), and every
## condition's outcome-bearing arguments either name the world method's own
## parameters or are consumed HERE (the COMPARISON/INT pairs). That is not
## luck; the WP13 world surface was designed against exactly this family. The
## flip side: none of the world methods this file calls is simulation-backed
## yet (game/data/script_world_surface.json), so at runtime every one of these
## handlers currently ends in an honest WORLD_REFUSED gap naming the facet
## method. That is the intended half-bridge: the argument wiring - where this
## port can silently corrupt behaviour - is done and pinned; the world comes
## later. One member is closer than the rest:
##
##   * combat.team_health_percent (EVAL_TEAM_HEALTH) is classified
##     ADAPTER-ONLY: the sim already exposes living_ids() and per-entity
##     member_health arrays publicly, and the health-WRITE bypass reasoning
##     that blocks damage/kill does not apply to a read. Verified against
##     retail_slice_sim.gd (living_ids at ~line 3003, member_health rows) and
##     retail_slice_script_world.gd, which already aggregates over living_ids
##     for other reads. Nothing new in the sim is needed - only adapter code.
##
##
## A WORLD-SURFACE DEFECT THIS FILE STEERS AROUND, NOT INTO
## ========================================================
## orders.use_command_button_partial declares `_count: int` where the sourced
## signature TEAM_PARTIAL_USE_COMMANDBUTTON(REAL, TEAM,
## COMMANDBUTTON_ABILITY_TEAM) carries a REAL FRACTION of the team. That
## defect is recorded for the coordinated signature-correction packet
## (script_world_surface.json, "facet-signature-packet"); pouring a percentage
## into a count slot would order a 0.5-team ability onto 0 members. NO MEMBER
## OF THIS FILE touches that method - the census's two command-button members
## are the whole-roster spellings, served by orders.use_command_button - and
## the completing package must refuse TEAM_PARTIAL_USE_COMMANDBUTTON until the
## packet lands, citing the same defect.
##
##
## THE ARGUMENT TRAP
## =================
## sage_scb.py stores integer AND real AND text for EVERY non-position
## argument. There is no complete type-code table to fall back on, so reading
## "the int one" or "the string one" silently succeeds on the wrong slot.
## Every read below is BY INDEX with the signature written directly above it.
## The traps specific to this file:
##
##     EVAL_TEAM_HEALTH(TEAM, COMPARISON, INT)   and
##     UNIT_HEALTH(UNIT, COMPARISON, INT)
##         the WP17 adjacent-integer trap again: argument 1 is the comparison
##         OPERATOR and argument 2 the PERCENT THRESHOLD it compares against,
##         both carried in the `integer` payload field
##         (SageScriptParamTypes.PAYLOAD_FIELD_FOR_PARAM routes COMPARISON and
##         INT to "integer"), so only position separates them. Read backwards,
##         "team health > 60%" becomes an out-of-table operator refusal at
##         best and a different question at worst.
##
##     PLAYER_DESTROYED_N_BUILDINGS_PLAYER(PLAYER, INT, PLAYER)
##         two PLAYER-typed arguments with the count between them. Argument 0
##         is the ATTACKER (the player doing the destroying) and argument 2
##         the VICTIM (the player whose buildings died). Both are text-carried
##         and no type-search could tell them apart; swapping them INVERTS the
##         condition ("has the AI hurt the human" becomes "has the human hurt
##         the AI"), which is worse than refusing - it is a confidently wrong
##         answer at a taunt/response gate.
##
## Of this file's parameter types only COMPARISON and INT are integer-valued;
## TEAM, UNIT, PLAYER, COMMANDBUTTON_ABILITY_TEAM and COMMANDBUTTON_ABILITY_UNIT
## have no PAYLOAD_FIELD_FOR_PARAM row and are carried as text, exactly as the
## decoded records confirm.
##
##
## CONDITIONS MAY NOT GUESS
## ========================
## Six of the eight members are conditions, and every one gates AI response
## logic. A read the world cannot answer must never come back as a plain
## `false` - and for this file's biggest condition the stakes are inverted
## relative to most packages: NAMED_NOT_DESTROYED is a NEGATED read. "That
## object was destroyed" and "I have no idea what happened to that object" are
## different answers, and negating the second would turn ignorance into a
## confident "still standing" - the single worst outcome available here, since
## it is the hero-alive gate in four libraries. Every condition therefore
## checks `SageWorldQuery.ok` FIRST and returns WORLD_REFUSED otherwise, which
## the dispatcher turns into CONDITION_FALLBACK (false) *and* a structured gap
## - negation is applied only to an ANSWERED read, never to a refusal.
##
##
## TWO INTERPRETIVE DECISIONS, STATED RATHER THAN BURIED
## =====================================================
## The source reference carries signatures but not semantics prose for these
## two, so the following readings are DECISIONS, documented here and pinned by
## the runner so a future correction is a visible edit, not a drift:
##
##   1. PLAYER_DESTROYED_N_BUILDINGS_PLAYER reads as a THRESHOLD: "attacker
##      has destroyed AT LEAST <INT> buildings of victim". The underlying
##      count is monotone (buildings destroyed can only grow), so an
##      exact-equality reading would flicker true for one window and then be
##      false forever once building N+1 dies - unusable as the trigger gate
##      its one retail call site authors it as. >= is the only reading under
##      which the condition works as a gate at all.
##
##   2. TEAM_IS_ATTACKED_AND_CANNOT_RETALIATE_ALL on an EMPTY team reads
##      FALSE, not vacuously true. "Is attacked" is an event predicate: a team
##      with no living members is not being attacked by anyone, and a vacuous
##      true would fire the AI's cornered-team response for every dead team on
##      every evaluation pass. The ALL itself is exact: every living member
##      (count == unit_count), not merely some (that is the _ANY sibling, not
##      on the AI list, left to the completing package).

const PACKAGE := "WP13-ai-powers-combat"

const Dispatch := preload("res://src/script/script_dispatch.gd")
const ParamTypes := preload("res://src/script/script_param_types.gd")


static func register(reg: SageScriptHandlerRegistry.Registrar) -> void:
	# --- All served, in AI call-site order (census counts, both trees) ----
	reg.action("TEAM_USE_COMMANDBUTTON_ABILITY", _team_use_command_button_ability)  # 18
	reg.condition("NAMED_NOT_DESTROYED", _condition_named_not_destroyed)  # 17
	reg.condition("TEAM_DESTROYED", _condition_team_destroyed)           #  5
	reg.condition("EVAL_TEAM_HEALTH", _condition_eval_team_health)       #  4
	reg.condition("UNIT_HEALTH", _condition_unit_health)                 #  1
	reg.condition(
		"TEAM_IS_ATTACKED_AND_CANNOT_RETALIATE_ALL", _condition_attacked_cannot_retaliate_all
	)                                                                    #  1
	reg.condition(
		"PLAYER_DESTROYED_N_BUILDINGS_PLAYER", _condition_destroyed_n_buildings
	)                                                                    #  1
	reg.action("NAMED_USE_COMMANDBUTTON_ABILITY", _named_use_command_button_ability)  # 1


# --- Shared tails ---------------------------------------------------------


static func _served(ctx: Dictionary, method: String, accepted: bool) -> int:
	## Every command in this file ends here. A facet that returns false has
	## already told the world (via Facet._refuse_command) which method refused;
	## this turns that into the status the dispatcher records as a gap, naming
	## the same method so the gap points at the missing WORLD surface rather
	## than at the action.
	if not accepted:
		ctx["detail"] = "world does not implement %s" % method
		return Dispatch.Status.WORLD_REFUSED
	return Dispatch.Status.OK


static func _unanswered(ctx: Dictionary, query: SageWorldQuery) -> int:
	## Every condition in this file takes this path when the world could not
	## answer. It must NEVER be replaced by "return false" - and in this file
	## it must ESPECIALLY never be replaced by "result = not false": see the
	## class comment on NAMED_NOT_DESTROYED.
	ctx["detail"] = query.detail
	return Dispatch.Status.WORLD_REFUSED


static func _refuse_unknown_comparison(ctx: Dictionary, name: String, comparison: int) -> int:
	## Shared refusal for the two health conditions. ParamTypes.compare answers
	## `false` for any operator it does not know, which would turn "this
	## handler failed to understand the operator" into a confident "no" at an
	## AI health gate. Same policy as WP17's PLAYER_HAS_OBJECT_COMPARISON, for
	## the same reason: the operator is consumed by THIS file, so an
	## out-of-table value is BAD_ARGUMENTS - refused loudly, never guessed.
	ctx["detail"] = (
		"%s was authored with comparison operator %d, outside the sourced "
		+ "COMPARISON table (0..5); compare would silently answer false for "
		+ "it, so it is refused instead"
	) % [name, comparison]
	return Dispatch.Status.BAD_ARGUMENTS


# --- Conditions -----------------------------------------------------------
#
# Read the class comment before touching any of these. Every one of them may
# answer true, may answer false, or may refuse - and the third is not the
# second.


static func _condition_named_not_destroyed(ctx: Dictionary) -> int:
	# NAMED_NOT_DESTROYED(UNIT) - "<UNIT> has not been destroyed."
	#
	# THE NEGATION SITS AFTER THE OK-CHECK, AND MUST STAY THERE. The world
	# method is the destruction read (units.was_destroyed serves both
	# NAMED_DESTROYED and this negated spelling - the completing package
	# registers the positive one); this handler answers NOT-destroyed, so it
	# negates the ANSWER. Negating a refusal would turn "the world does not
	# track destruction" into "still standing", releasing whatever the gate
	# guards - and this is the most widely spread member of the file (17 call
	# sites across 4 libraries, the hero/objective liveness gate). The refusal
	# path above returns WORLD_REFUSED, which the dispatcher reads as FALSE at
	# the call site: the conservative "treat it as gone" answer, with the gap
	# on the record.
	#
	# The name passes through verbatim, per the world's argument conventions.
	var args: SageScriptArgs = ctx["args"]
	var query := (ctx["world"] as SageScriptWorld).units().was_destroyed(args.text(0))
	if not query.ok:
		return _unanswered(ctx, query)
	ctx["result"] = not query.as_bool()
	return Dispatch.Status.OK


static func _condition_team_destroyed(ctx: Dictionary) -> int:
	# TEAM_DESTROYED(TEAM) - "<TEAM> has been destroyed."
	#
	# The straight (un-negated) read of the team-scope destruction record. The
	# world surface documents teams.was_destroyed as destruction EDGE state
	# (script_world_surface.json: "team destruction edge records"); whether the
	# concrete world answers it as an edge or a level is that world's contract,
	# and this handler passes the answer through without re-deriving it from
	# unit_count == 0 - a team that was never instantiated and a team that was
	# wiped out both have zero members, and only the world can tell them apart.
	var args: SageScriptArgs = ctx["args"]
	var query := (ctx["world"] as SageScriptWorld).teams().was_destroyed(args.text(0))
	if not query.ok:
		return _unanswered(ctx, query)
	ctx["result"] = query.as_bool()
	return Dispatch.Status.OK


static func _condition_eval_team_health(ctx: Dictionary) -> int:
	# EVAL_TEAM_HEALTH(TEAM, COMPARISON, INT)
	#   "<TEAM>'s aggregate health <COMPARISON> <INT> percent"
	#
	# THE ADJACENT-INTEGER TRAP (see the class comment): argument 1 is the
	# comparison OPERATOR and argument 2 is the percent THRESHOLD, both carried
	# in the `integer` payload field, so only position separates them. The
	# operator is validated BEFORE the world is asked - a record authored with
	# an out-of-table operator is BAD_ARGUMENTS regardless of whether the world
	# could have answered, and the world is never bothered for a question this
	# handler could not finish evaluating.
	#
	# THE COMPARISON IS FLOAT, NOT INT. combat.team_health_percent documents
	# "Float 0..100" - an aggregate over member health is fractional - while
	# the authored threshold is an INT. The float compare table
	# (ParamTypes.compare) is used with the threshold widened, because
	# truncating 62.5 to 62 first would flip < and > exactly at the authored
	# boundary, the point where the retail gate is doing its job.
	#
	# This is the file's ADAPTER-ONLY member: the world method needs no new
	# simulation state, only adapter code (see the class comment). The handler
	# is finished either way - when the adapter lands, this starts answering
	# without an edit here.
	var args: SageScriptArgs = ctx["args"]
	var comparison := args.integer(1)
	if comparison < ParamTypes.COMPARE_LESS or comparison > ParamTypes.COMPARE_NOT_EQUAL:
		return _refuse_unknown_comparison(ctx, "EVAL_TEAM_HEALTH", comparison)
	var query := (ctx["world"] as SageScriptWorld).combat().team_health_percent(args.text(0))
	if not query.ok:
		return _unanswered(ctx, query)
	ctx["result"] = ParamTypes.compare(query.as_float(), comparison, float(args.integer(2)))
	return Dispatch.Status.OK


static func _condition_unit_health(ctx: Dictionary) -> int:
	# UNIT_HEALTH(UNIT, COMPARISON, INT)
	#   "<UNIT>'s health <COMPARISON> <INT> percent"
	#
	# The single-object sibling of EVAL_TEAM_HEALTH: same adjacent-integer
	# trap, same operator-before-world validation, same float compare against
	# the widened INT threshold (units.health_percent documents "Float 0..100
	# to match the script parameter" - a multi-member battalion's aggregate is
	# fractional even when each member's health is integral). Only the facet
	# differs: units, keyed by object name.
	var args: SageScriptArgs = ctx["args"]
	var comparison := args.integer(1)
	if comparison < ParamTypes.COMPARE_LESS or comparison > ParamTypes.COMPARE_NOT_EQUAL:
		return _refuse_unknown_comparison(ctx, "UNIT_HEALTH", comparison)
	var query := (ctx["world"] as SageScriptWorld).units().health_percent(args.text(0))
	if not query.ok:
		return _unanswered(ctx, query)
	ctx["result"] = ParamTypes.compare(query.as_float(), comparison, float(args.integer(2)))
	return Dispatch.Status.OK


static func _condition_attacked_cannot_retaliate_all(ctx: Dictionary) -> int:
	# TEAM_IS_ATTACKED_AND_CANNOT_RETALIATE_ALL(TEAM)
	#   "every member of <TEAM> is attacked and cannot retaliate"
	#
	# TWO READS, BOTH MUST ANSWER. The world exposes the numerator
	# (teams.attacked_and_cannot_retaliate_count - how many members are in
	# that state) and the denominator (teams.unit_count) as separate queries;
	# ALL is count == unit_count, evaluated HERE. If EITHER read refuses the
	# whole condition refuses - answering "all" from a numerator without a
	# denominator would be a guess wearing arithmetic.
	#
	# THE EMPTY TEAM READS FALSE, NOT VACUOUSLY TRUE - decision 2 in the class
	# comment: a team with no living members is not being attacked, and a
	# vacuous true would fire the cornered-team response for every dead team
	# on every evaluation pass. The equality is EXACT (==, not >=): the
	# numerator counts a subset of the roster, so a numerator EXCEEDING the
	# denominator means the world double-counted, and reading that as "all"
	# would launder the miscount into an answer.
	var args: SageScriptArgs = ctx["args"]
	var teams := (ctx["world"] as SageScriptWorld).teams()
	var affected := teams.attacked_and_cannot_retaliate_count(args.text(0))
	if not affected.ok:
		return _unanswered(ctx, affected)
	var roster := teams.unit_count(args.text(0))
	if not roster.ok:
		return _unanswered(ctx, roster)
	ctx["result"] = roster.as_int() > 0 and affected.as_int() == roster.as_int()
	return Dispatch.Status.OK


static func _condition_destroyed_n_buildings(ctx: Dictionary) -> int:
	# PLAYER_DESTROYED_N_BUILDINGS_PLAYER(PLAYER, INT, PLAYER)
	#   "<PLAYER> has destroyed at least <INT> buildings of <PLAYER>"
	#
	# THE TWO-PLAYER TRAP (see the class comment): argument 0 is the ATTACKER,
	# argument 2 the VICTIM, with the count THRESHOLD between them in the
	# `integer` payload field. Both players are text-carried, so nothing but
	# position separates them, and a swap INVERTS the condition - a wrong
	# answer, not an error. The runner's stub teaches the fixture in one
	# direction only, so a swapped read shows up as a refusal there instead of
	# a plausible number.
	#
	# THE THRESHOLD READING IS >= - decision 1 in the class comment: the
	# underlying ledger count is monotone, so only an at-least reading works
	# as the trigger gate the retail call site authors. The comparison is
	# consumed HERE; combat.buildings_destroyed_by carries no threshold
	# parameter, it answers the raw per-victim count.
	var args: SageScriptArgs = ctx["args"]
	var query := (ctx["world"] as SageScriptWorld).combat().buildings_destroyed_by(
		args.text(0), args.text(2)
	)
	if not query.ok:
		return _unanswered(ctx, query)
	ctx["result"] = query.as_int() >= args.integer(1)
	return Dispatch.Status.OK


# --- Actions --------------------------------------------------------------


static func _team_use_command_button_ability(ctx: Dictionary) -> int:
	# TEAM_USE_COMMANDBUTTON_ABILITY(TEAM, COMMANDBUTTON_ABILITY_TEAM)
	#   "<TEAM> uses ability <COMMANDBUTTON_ABILITY_TEAM>."
	#
	# SCOPE + TARGET PATTERN. The world implements the entire ~20-member
	# *_USE_COMMANDBUTTON_* family once as orders.use_command_button(scope,
	# name, command_button, target); this file owns only the two bare ABILITY
	# spellings the AI census names. This is the TEAM spelling, so Scope.TEAM;
	# and the bare spelling names NO target - the ability is self-directed or
	# untargeted - so the target is target_self(), the tagged form the world
	# method documents for exactly these variants. The _AT_WAYPOINT /
	# _ON_NAMED / _ON_NEAREST_* siblings pass real targets to the same method
	# and belong to the completing package.
	#
	# The button name is passed through verbatim: retail authors command-button
	# tokens (the heaviest traffic is the attack-execution library cycling
	# battalion abilities); nothing here resolves or validates them, per the
	# world's argument conventions. And note what this file does NOT touch:
	# the PARTIAL sibling and its defective count-for-fraction world slot -
	# see the class comment.
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx, "orders.use_command_button",
		(ctx["world"] as SageScriptWorld).orders().use_command_button(
			SageScriptWorld.Scope.TEAM,
			args.text(0),
			args.text(1),
			SageScriptWorld.target_self()
		)
	)


static func _named_use_command_button_ability(ctx: Dictionary) -> int:
	# NAMED_USE_COMMANDBUTTON_ABILITY(UNIT, COMMANDBUTTON_ABILITY_UNIT)
	#   "<UNIT> uses ability <COMMANDBUTTON_ABILITY_UNIT>."
	#
	# The single-object spelling of the action above: Scope.UNIT, same folded
	# world method, same self-target for the bare ABILITY form. One retail AI
	# call site, in one library (census). The scope is the ONLY difference,
	# and it is load-bearing: a team name and an object name live in different
	# world namespaces, and the scope tells the world which one to resolve.
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx, "orders.use_command_button",
		(ctx["world"] as SageScriptWorld).orders().use_command_button(
			SageScriptWorld.Scope.UNIT,
			args.text(0),
			args.text(1),
			SageScriptWorld.target_self()
		)
	)

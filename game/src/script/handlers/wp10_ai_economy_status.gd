extends RefCounted

## WP10-ai-economy-objectstatus - the AI-CRITICAL SUBSET of
## WP10-economy-objectstatus-objectlists.
##
## SCOPE, AND WHY THIS FILE IS NOT THE WHOLE PACKAGE
## =================================================
## WP10 covers economy, object status and object lists. This file implements
## the TWO members that the retail AI script libraries actually call, and
## nothing else. The remaining members (FREEZE_TIME, OBJECTLIST_ADDOBJECTTYPE,
## the supply-source family, ...) are a later agent's work and will land in a
## SEPARATE file, because two files may not claim one PACKAGE constant (see
## handlers/_registry.gd). The catalog's full-package name is
## "WP10-economy-objectstatus-objectlists"; this subset therefore claims the
## PACKAGE constant "WP10-ai-economy-objectstatus" and the file name
## wp10_ai_economy_status.gd, leaving BOTH the catalog package name and a
## sensible completing file name (wp10_economy_objectstatus_objectlists.gd)
## free for the completing package.
##
## The two were not chosen by taste. They are the AI-called WP10 membership in
## game/data/retail_ai_call_census.json - the committed census of the 14 AI_*
## WorldBuilder libraries referenced across the 301 decoded retail maps - which
## is the authority for the counts below. Both retail trees carry IDENTICAL
## counts for both members (checked against the census), so no tree qualifier
## is needed:
##
##     18  TEAM_CHANGE_OBJECT_STATUS  action     served  (2 libraries)
##     17  PLAYER_HAS_CREDITS         condition  served  (2 libraries)
##
## Both members are implemented; this package registers no gaps. That is not
## optimism: the world surface carries every load-bearing argument of both
## members (units.set_object_status takes the status name AND the set/clear
## flag; economy.money is simulation-backed). units.set_object_status is
## simulation-backed for TEAM/PLAYER/UNIT living-entity scopes (exact authored
## OBJECT_STATUS names stored on entity rows). Remaining entity-status-flags
## members (emoticon, model-condition, stealth presentation, etc.) still refuse.
##
##
## THE ARGUMENT TRAP
## =================
## sage_scb.py stores integer AND real AND text for EVERY non-position
## argument, so a type-search silently succeeds on the wrong slot. Every read
## below is BY INDEX with the signature written directly above it. This file
## carries the sharpest instance of the trap in the whole AI vocabulary:
##
##     PLAYER_HAS_CREDITS(INT, COMPARISON, PLAYER)
##                        ^^^^^^^^^^^^^^^
##
## THE AUTHORED INT IS THE LEFT OPERAND. The engine's own WorldBuilder template
## for this condition reads
##
##     "<INT> is <COMPARISON> the number of credits possessed by <PLAYER>"
##
## so "the AI can afford an 800 spend" is authored as 800 <= credits, with the
## authored value on the LEFT and the live credit count on the RIGHT. That is
## the OPPOSITE operand order to WP17's PLAYER_HAS_OBJECT_COMPARISON ("<PLAYER>
## has <COMPARISON> <INT> objects", live count on the left), and the signature
## broadcasts it: this condition puts the INT before the COMPARISON, where
## PLAYER_HAS_OBJECT_COMPARISON puts the COMPARISON before the INT. A handler
## that "normalised" the two to one direction would turn every affordability
## gate into its inverse - the AI would spend exactly when it cannot afford to
## and freeze when it can, 17 times per tree, with no error anywhere. The
## runner pins the direction with unequal values under an asymmetric operator,
## so the reversed read gives a DIFFERENT answer, not an accidentally equal
## one.
##
## The same two arguments are also the adjacent-integer trap seen in WP17: the
## INT (index 0) and the COMPARISON (index 1) both live in the `integer`
## payload field, so only position separates them. Here the swap is
## self-announcing IF the operator is validated - a retail credit threshold
## (500, 800, ...) read as an operator is far outside the 0..5 COMPARISON
## table - which is one more reason the operator check below exists.
##
## TEAM_CHANGE_OBJECT_STATUS(TEAM, OBJECT_STATUS, BOOLEAN) carries its own
## load-bearing tail: the BOOLEAN is the POLARITY - whether the status is being
## set or cleared ("For all units in team <TEAM> set object status
## <OBJECT_STATUS> to <BOOLEAN>", per the same engine template). Dropping or
## hardcoding it would do the opposite of what the map authored on every
## clear-site. It is read from the `integer` field per
## SageScriptParamTypes.PAYLOAD_FIELD_FOR_PARAM ("BOOLEAN" -> integer); the
## OBJECT_STATUS name is text and passes through verbatim - the reference's
## OBJECT_STATUS bit vocabulary belongs to the object model, not to this
## handler, so nothing here validates or resolves it.
##
##
## CONDITIONS MAY NOT GUESS
## ========================
## PLAYER_HAS_CREDITS gates AI spending, and a read the world cannot answer
## must never come back as a plain `false`. "500 is not <= this player's
## credits" stalls one purchase; "the world does not model money" must keep the
## gate shut WITH a structured gap on the record. The handler checks
## SageWorldQuery.ok first and returns WORLD_REFUSED otherwise, which the
## dispatcher turns into CONDITION_FALLBACK (false) plus the gap.

const PACKAGE := "WP10-ai-economy-objectstatus"

const Dispatch := preload("res://src/script/script_dispatch.gd")
const ParamTypes := preload("res://src/script/script_param_types.gd")


static func register(reg: SageScriptHandlerRegistry.Registrar) -> void:
	# In AI call-site order (census counts, identical in both trees).
	reg.action("TEAM_CHANGE_OBJECT_STATUS", _change_object_status)  # 18
	reg.condition("PLAYER_HAS_CREDITS", _condition_has_credits)     # 17


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
	## The condition in this file takes this path when the world could not
	## answer. It must NEVER be replaced by "return false": see the class
	## comment.
	ctx["detail"] = query.detail
	return Dispatch.Status.WORLD_REFUSED


# --- Actions --------------------------------------------------------------


static func _change_object_status(ctx: Dictionary) -> int:
	# TEAM_CHANGE_OBJECT_STATUS(TEAM, OBJECT_STATUS, BOOLEAN)
	#   "For all units in team <TEAM> set object status <OBJECT_STATUS> to
	#    <BOOLEAN>."
	#
	# THE POLARITY IS LOAD-BEARING. The BOOLEAN says whether the status is
	# being SET or CLEARED, and retail authors both directions (a status
	# granted for a phase and revoked after it). Serving the set-half only, or
	# hardcoding either value, would invert the authored state on every
	# opposite-polarity call site - not an error, not a refusal, the exact
	# opposite effect. It is read from the `integer` payload field (BOOLEAN ->
	# integer per PAYLOAD_FIELD_FOR_PARAM; the enum is {"false": 0, "true": 1})
	# and any non-zero integer is true, matching SageScriptArgs.boolean.
	#
	# SCOPE PATTERN. The world folds this action with WP10's
	# UNIT_CHANGE_OBJECT_STATUS sibling into one method behind a Scope; this is
	# the TEAM spelling, so Scope.TEAM. The UNIT spelling is NOT an AI-called
	# member and belongs to the completing package.
	#
	# The OBJECT_STATUS name is text and passes through verbatim - the world
	# owns the status-bit vocabulary, this handler only relays the name the map
	# authored.
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx, "units.set_object_status",
		(ctx["world"] as SageScriptWorld).units().set_object_status(
			SageScriptWorld.Scope.TEAM, args.text(0), args.text(1), args.boolean(2)
		)
	)


# --- Conditions -----------------------------------------------------------


static func _condition_has_credits(ctx: Dictionary) -> int:
	# PLAYER_HAS_CREDITS(INT, COMPARISON, PLAYER)
	#   "<INT> is <COMPARISON> the number of credits possessed by <PLAYER>"
	#
	# THE AUTHORED INT IS THE LEFT OPERAND - see the class comment. The engine
	# template puts the authored value first and the live credit count last, so
	# the comparison evaluated here is
	#
	#     compare_int(authored_int, operator, credits)
	#
	# and NOT the other way around. This is the opposite operand order to
	# WP17's PLAYER_HAS_OBJECT_COMPARISON; the two conditions' signatures
	# differ in exactly the way that predicts it (INT-then-COMPARISON here,
	# COMPARISON-then-INT there). Reversing it turns "the AI can afford this"
	# into "the AI cannot afford this" at every affordability gate.
	#
	# THE OPERATOR IS VALIDATED, for the same reason as WP17: the comparison is
	# consumed by THIS file, and ParamTypes.compare_int answers `false` for any
	# operator it does not know, which would turn an operator this handler
	# failed to understand into a confident "no" at an AI spending gate. An
	# unknown operator is therefore BAD_ARGUMENTS - refused loudly, never
	# guessed. (This check is also what makes the adjacent-integer swap
	# self-announcing: a credit threshold read as an operator lands far outside
	# the 0..5 table.)
	#
	# The money read goes through the economy facet - one of the 36
	# simulation-backed world methods in the measured surface map - and is a
	# pure READ: this condition is polled by the AI economy loop and must not
	# mutate anything.
	var args: SageScriptArgs = ctx["args"]
	var comparison := args.integer(1)
	if comparison < ParamTypes.COMPARE_LESS or comparison > ParamTypes.COMPARE_NOT_EQUAL:
		ctx["detail"] = (
			"PLAYER_HAS_CREDITS was authored with comparison operator %d, "
			+ "outside the sourced COMPARISON table (0..5); compare_int would "
			+ "silently answer false for it, so it is refused instead"
		) % comparison
		return Dispatch.Status.BAD_ARGUMENTS
	var query := (ctx["world"] as SageScriptWorld).economy().money(args.text(2))
	if not query.ok:
		return _unanswered(ctx, query)
	ctx["result"] = ParamTypes.compare_int(args.integer(0), comparison, query.as_int())
	return Dispatch.Status.OK

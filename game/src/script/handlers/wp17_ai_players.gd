extends RefCounted

## WP17-players - the AI-CRITICAL SUBSET.
##
## SCOPE, AND WHY THIS FILE IS NOT THE WHOLE PACKAGE
## =================================================
## WP17 has 53 members. This file implements the 7 that the RETAIL AI SCRIPT
## LIBRARIES actually call, and nothing else. The remaining 46 are a later
## agent's work and will land in a SEPARATE file, because two files may not
## claim one PACKAGE constant (see handlers/_registry.gd). This file's PACKAGE
## is therefore suffixed - "WP17-ai-players" - and the unsuffixed name,
## "WP17-players", is the one the completing package should take. That later
## file must also skip the 7 member names below: the registry keeps the FIRST
## claimant of every name and reports the second loudly, which is the intended
## signal, not a merge accident.
##
## The 7 were not chosen by taste. They are the exact set the decoder found in
## the 14 referenced AI_* libraries of both retail trees, and the census is
## versioned data in this repo: game/data/retail_ai_call_census.json. Both
## trees agree MEMBER-for-member on this file's subset, but NOT count-for-count
## across the whole census: SKIRMISH_PLAYER_FACTION is 18 call sites in
## bfme2-retail and 19 in rotwk-retail (RotWK's extra factions add call
## traffic); this file's other six members happen to carry identical counts in
## both trees. The counts below are therefore stated per tree where they
## differ. In total: 178 call sites in bfme2-retail (3.11% of that tree's 5728)
## and 179 in rotwk-retail (2.88% of 6213).
##
## Counts decide the order things appear in this file, highest first, so the
## most-exercised code is the code a reader meets first:
##
##     100     PLAYER_HAS_OBJECT_COMPARISON     condition   served
##      33     CAN_BUILD_AT_BASE                condition   GAP-REGISTERED
##      18/19  SKIRMISH_PLAYER_FACTION          condition   served (bfme2/rotwk)
##      17     CAN_BUILD_OBJECTTYPE_AT_BASE     condition   GAP-REGISTERED
##       8     START_POSITION_IS                condition   served
##       1     PLAYER_ENABLE_BASE_CONSTRUCTION  action      served
##       1     PLAYER_SELL_EVERYTHING           action      served
##
##
## WHAT IS SERVED AND WHAT IS NOT
## ==============================
## 5 members are implemented. 2 are GAP-REGISTERED, both for the same reason:
## the condition carries a LOAD-BEARING ARGUMENT - the BASE the question is
## anchored to - that the SageScriptWorld facet surface has no parameter for.
## That is recorded as a finding against the world surface, not papered over
## here - see GAP_CAN_BUILD_AT_BASE below, which states the exact missing
## signature.
##
## The rule applied throughout: an argument that changes the OUTCOME may never
## be dropped. A BFME2 skirmish AI holds several bases at once (the main
## fortress plus outposts and camps), and "can I still build at THIS base" is a
## different question per base - one is full while another has plots free.
## Answering the base-blind form the world offers would collapse every base
## onto one unspecified answer, 50 times per AI tree. A refusal is recoverable;
## a silently wrong answer is not.
##
##
## THE ARGUMENT TRAP
## =================
## sage_scb.py stores integer AND real AND text for EVERY non-position
## argument. There is no complete type-code table to fall back on, so reading
## "the int one" or "the string one" silently succeeds on the wrong slot. Every
## read below is BY INDEX with the signature written directly above it. The
## signature that matters most in this file puts two INTEGER-VALUED arguments
## side by side, where no type-search could ever tell them apart:
##
##     PLAYER_HAS_OBJECT_COMPARISON(PLAYER, COMPARISON, INT, OBJECT_TYPE)
##                                          ^^^^^^^^^^^^^^^
##
## Argument 1 is the comparison OPERATOR and argument 2 is the COUNT it
## compares against. Both are carried in the `integer` payload field
## (SageScriptParamTypes.PAYLOAD_FIELD_FOR_PARAM routes COMPARISON and INT to
## "integer"). Read backwards, "player has >= 2 fortresses" becomes "player has
## == 4 fortresses": not an error, not a refusal, just a different question
## answered confidently 100 times per AI game. This is the single most
## dangerous swap in the package and the runner pins it with values chosen so
## the swapped read gives a DIFFERENT answer, not an accidentally equal one.
##
##
## CONDITIONS MAY NOT GUESS
## ========================
## All three served conditions gate AI build and strategy decisions, and a read
## the world cannot answer must never come back as a plain `false`. "This
## player is not Isengard" and "I have no idea who this player is" are
## different answers, and only the first may steer a faction-specific build
## order. Every condition checks `SageWorldQuery.ok` first and returns
## WORLD_REFUSED otherwise, which the dispatcher turns into CONDITION_FALLBACK
## (false) *and* a structured gap - the gate stays shut and the reason is on
## the record.

const PACKAGE := "WP17-ai-players"

const Dispatch := preload("res://src/script/script_dispatch.gd")
const ParamTypes := preload("res://src/script/script_param_types.gd")


# ==========================================================================
# GAP-REGISTERED MEMBERS
# ==========================================================================
#
# Two members whose world surface cannot carry an argument that changes the
# outcome. They are declared through `reg.blocked_conditions()` rather than
# given a body that returns OK, for two reasons: a body would count as
# coverage, and a shared refusal cannot be mistaken for an implementation
# while skimming.
#
# The string below names the EXACT facet signature that would unblock both.
# Neither needs a new simulation subsystem - the Players facet exists. They
# need one more parameter, which is a coordinated edit to script_world.gd and
# therefore not this agent's to make.

## CAN_BUILD_AT_BASE(PLAYER, UNIT) - 33 AI call sites across 5 libraries.
## CAN_BUILD_OBJECTTYPE_AT_BASE(PLAYER, UNIT, OBJECT_TYPE) - 17, in 1 library.
const GAP_CAN_BUILD_AT_BASE := (
	"base-anchored buildability query (both conditions carry a UNIT naming the "
	+ "BASE the question is anchored to - the sourced signatures are "
	+ "CAN_BUILD_AT_BASE(PLAYER, UNIT) and CAN_BUILD_OBJECTTYPE_AT_BASE(PLAYER, "
	+ "UNIT, OBJECT_TYPE), where the UNIT is a named base object distinct from "
	+ "the OBJECT_TYPE being built. The world offers only "
	+ "players.can_build_at_base(player, object_type), with no slot for the "
	+ "base: its own docstring reads the pair as differing only in the type, "
	+ "but the signatures say otherwise and the signatures win. A skirmish AI "
	+ "holds several bases at once and the answer differs per base - one is "
	+ "full while another has plots free - so serving the base-blind form would "
	+ "collapse every base onto one unspecified answer at 50 retail call sites "
	+ "per tree. NEEDED: players.can_build_at_base(player: String, base: "
	+ "String, object_type: String) -> SageWorldQuery, empty object_type being "
	+ "the 'anything at all' variant that CAN_BUILD_AT_BASE asks for)"
)

const GAP_CAN_BUILD_CONDITIONS := ["CAN_BUILD_AT_BASE", "CAN_BUILD_OBJECTTYPE_AT_BASE"]


static func register(reg: SageScriptHandlerRegistry.Registrar) -> void:
	# --- Served, in AI call-site order (bfme2-retail counts) --------------
	reg.condition("PLAYER_HAS_OBJECT_COMPARISON", _condition_has_object_comparison)  # 100
	reg.condition("SKIRMISH_PLAYER_FACTION", _condition_player_faction)  # 18 (19 rotwk)
	reg.condition("START_POSITION_IS", _condition_start_position_is)     #  8
	reg.action("PLAYER_ENABLE_BASE_CONSTRUCTION", _enable_base_construction)  # 1
	reg.action("PLAYER_SELL_EVERYTHING", _sell_everything)               #  1

	# --- Gap-registered: the world surface cannot carry the base anchor ---
	reg.blocked_conditions(GAP_CAN_BUILD_CONDITIONS, GAP_CAN_BUILD_AT_BASE)  # 33 + 17


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
	## answer. It must NEVER be replaced by "return false": see the class
	## comment.
	ctx["detail"] = query.detail
	return Dispatch.Status.WORLD_REFUSED


# --- Conditions -----------------------------------------------------------
#
# Read the class comment before touching any of these. Every one of them may
# answer true, may answer false, or may refuse - and the third is not the
# second.


static func _condition_has_object_comparison(ctx: Dictionary) -> int:
	# PLAYER_HAS_OBJECT_COMPARISON(PLAYER, COMPARISON, INT, OBJECT_TYPE)
	#   "<PLAYER> has <COMPARISON> <INT> objects of type <OBJECT_TYPE>"
	#
	# THE ADJACENT-INTEGER TRAP. Argument 1 is the comparison operator and
	# argument 2 is the count it compares against; both live in the `integer`
	# payload field, so only position separates them - see the class comment.
	# The comparison is evaluated HERE, in the handler, against the count the
	# world reports: the world method carries no comparison parameter, and per
	# the world's argument conventions enums never reach it as strings either.
	#
	# THE OPERATOR IS VALIDATED, UNLIKE WP15'S AI_MOOD - AND THE DIFFERENCE IS
	# WHERE THE VALUE IS CONSUMED. An out-of-table AI_MOOD is passed through raw
	# because the WORLD interprets it and retail content authors values outside
	# the documented range. The comparison operator is consumed by THIS file:
	# ParamTypes.compare_int answers `false` for any operator it does not know,
	# which would turn an operator this handler failed to understand into a
	# confident "no" at the most-called WP17 gate. An unknown operator is
	# therefore BAD_ARGUMENTS - refused loudly, never guessed.
	#
	# INCLUDE_DEAD IS FALSE. The world folds this read with WP01's ownership
	# counters behind an `include_dead` flag because WP01 has an explicit
	# ..._INCLUDE_DEAD sibling; "has" in this condition's own description is
	# present-tense possession, so the unsuffixed live-only reading is the
	# authored one.
	#
	# The OBJECT_TYPE string is passed through verbatim. Retail AI authors
	# object-type LIST names here (the world method's parameter is documented
	# as an OBJECT_TYPE_LIST name); nothing in this handler resolves or
	# validates the list, per the world's argument conventions.
	var args: SageScriptArgs = ctx["args"]
	var comparison := args.integer(1)
	if comparison < ParamTypes.COMPARE_LESS or comparison > ParamTypes.COMPARE_NOT_EQUAL:
		ctx["detail"] = (
			"PLAYER_HAS_OBJECT_COMPARISON was authored with comparison operator "
			+ "%d, outside the sourced COMPARISON table (0..5); compare_int would "
			+ "silently answer false for it, so it is refused instead"
		) % comparison
		return Dispatch.Status.BAD_ARGUMENTS
	var query := (ctx["world"] as SageScriptWorld).players().object_count_of_types(
		args.text(0), args.text(3), false
	)
	if not query.ok:
		return _unanswered(ctx, query)
	ctx["result"] = ParamTypes.compare_int(query.as_int(), comparison, args.integer(2))
	return Dispatch.Status.OK


static func _condition_player_faction(ctx: Dictionary) -> int:
	# SKIRMISH_PLAYER_FACTION(PLAYER, FACTION)
	#
	# Exact string comparison, with no case folding and no trimming. The
	# world's argument convention is that names arrive exactly as the decoded
	# script carries them; FACTION has no ENUMS row and no observed wire code,
	# so it is carried as TEXT, and a handler that lower-cased here would make
	# a mod's two distinct faction tokens the same faction in this engine and
	# different ones in the retail engine.
	#
	# This is the one member of this file whose call-site count differs between
	# the retail trees (18 bfme2 / 19 rotwk) - RotWK's added factions author an
	# extra faction gate. Same member, same signature, one more call site.
	var args: SageScriptArgs = ctx["args"]
	var query := (ctx["world"] as SageScriptWorld).players().faction(args.text(0))
	if not query.ok:
		return _unanswered(ctx, query)
	ctx["result"] = query.as_string() == args.text(1)
	return Dispatch.Status.OK


static func _condition_start_position_is(ctx: Dictionary) -> int:
	# START_POSITION_IS(PLAYER, INT)
	#
	# The INT is a start-spot index and is read from the `integer` payload
	# field, per PAYLOAD_FIELD_FOR_PARAM. It is compared EXACTLY as authored:
	# no 0-based/1-based translation happens here, because the world's
	# players.start_position contract is "Int start-spot index" in the same
	# numbering the scripts use - if a concrete world numbers its spots
	# differently, the translation belongs in that world's adapter, once, not
	# in a handler where it would also re-map every mod's authored indices.
	var args: SageScriptArgs = ctx["args"]
	var query := (ctx["world"] as SageScriptWorld).players().start_position(args.text(0))
	if not query.ok:
		return _unanswered(ctx, query)
	ctx["result"] = query.as_int() == args.integer(1)
	return Dispatch.Status.OK


# --- Actions --------------------------------------------------------------


static func _enable_base_construction(ctx: Dictionary) -> int:
	# PLAYER_ENABLE_BASE_CONSTRUCTION(PLAYER)
	#
	# FOLDED FLAG. The world collapses the ENABLE/DISABLE pair into one method
	# behind a boolean. This is the ENABLE spelling, so the flag is TRUE;
	# PLAYER_DISABLE_BASE_CONSTRUCTION is a WP17 member but not an AI-used one,
	# so the completing package registers it with FALSE - not this file.
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx, "players.set_base_construction_enabled",
		(ctx["world"] as SageScriptWorld).players().set_base_construction_enabled(
			args.text(0), true
		)
	)


static func _sell_everything(ctx: Dictionary) -> int:
	# PLAYER_SELL_EVERYTHING(PLAYER) - "<PLAYER> sells everything they own."
	#
	# One retail AI call site, in one library (census). The world owns what
	# "everything" means; the handler only names the player.
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx, "players.sell_everything",
		(ctx["world"] as SageScriptWorld).players().sell_everything(args.text(0))
	)

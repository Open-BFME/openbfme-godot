extends RefCounted

## WP11-basebuilding-ai - the AI-CRITICAL SUBSET of WP11-ai-basebuilding.
##
## SCOPE, AND WHY THIS FILE IS NOT THE WHOLE PACKAGE
## =================================================
## WP11 has 72 members. This file accounts for the EIGHT that the retail AI
## script libraries actually call, and nothing else. The remaining members are
## a later agent's work and will land in a SEPARATE file, because two files may
## not claim one PACKAGE constant (see handlers/_registry.gd). The catalog's
## full-package name is "WP11-ai-basebuilding"; this subset therefore claims
## the PACKAGE constant "WP11-basebuilding-ai" and the file name
## wp11_ai_basebuilding.gd, leaving BOTH the catalog package name and the file
## name wp11_basebuilding.gd free for the completing package.
##
## The eight were not chosen by taste. They are the AI-called WP11 membership
## in game/data/retail_ai_call_census.json - the committed census of the 14
## AI_* WorldBuilder libraries referenced across the 301 decoded retail maps -
## which is the authority for every count below. The two retail trees carry
## IDENTICAL counts for all eight of these members (checked against the
## census; that is NOT a corpus-wide fact - 12 census members differ between
## BFME2 and RotWK, none of them in this file - so the counts below need no
## tree qualifier). `libraryCount` from the same census is quoted per member:
## it distinguishes engine-wide vocabulary from one library's idiom, and the
## headline is that NAMED_BASE_UNPACKABLE_FOR_PLAYER's 64 call sites - the
## largest single WP11 bucket - all live in ONE library, the economy-execution
## loop that polls every base flag on the map for expansion opportunities.
##
## THE NINTH MEMBER THIS FILE DOES NOT TOUCH. The census lists one more
## AI-called WP11 action outside this subset's brief: BUILD_BASE_BUILDING
## (2 call sites, 2 libraries). It is served by the tail package
## (wp11_ai_basebuilding_tail.gd), which owns that name.
##
##
## WHAT IS SERVED AND WHAT IS NOT
## ==============================
## 7 members are implemented (1 condition, 6 actions). 1 is GAP-REGISTERED
## for the same class of reason as WP15's gaps: the action carries
## LOAD-BEARING ARGUMENTS that the SageScriptWorld facet surface has no
## parameter for. That is recorded as a finding against the world surface,
## not papered over here - see GAP_BUILD_PER_MARKER below, which states the
## exact missing signature.
##
## The rule applied throughout: an argument that changes the OUTCOME may never
## be dropped. The unpack pair was gap-registered on exactly that rule while
## ai.base_unpack(name, free) had no slot for the authored result reference;
## the world surface has since grown the corrected signature -
## ai.base_unpack(object_name, free, result_reference) - and the pair is now
## served THROUGH it, reference and all (see the RETAIL SHAPES section for
## why the reference is load-bearing). A refusal is recoverable; a reference
## that was silently never bound is not.
##
##
## RETAIL SHAPES (the evidence for the gap classification)
## =======================================================
## The shipped AI libraries were decoded with the importer's own sage_scb
## tooling to establish what the arguments actually carry. The base-building
## family forms one chain, and the chain is why the result reference is
## load-bearing:
##
##   * ai_economy_execution polls NAMED_BASE_UNPACKABLE_FOR_PLAYER over the
##     map's base flags (BASE_FLAG_1..N, "<This Player>") - all 64 call sites
##     of the condition, one library;
##   * the paid NAMED_BASE_UNPACK calls in the same library each bind their
##     expansion to a distinct reference (AI_EXPANSION_1..N);
##   * the free NAMED_BASE_UNPACK_FREE calls in ai_initialize all bind
##     AI_BASE - the AI's own starting base;
##   * ai_opposition then reads AI_BASE as the spawn anchor for all three
##     CREATE_REINFORCEMENT_TEAM_AT_UNIT_POSITION call sites.
##
## So the reference bound by the unpack IS the unit named by the reinforcement
## action this file serves. Dropping it would make the served action look up a
## name that was never bound.
##
## The same decode fixed the trailing-argument reading for the whole family:
## BUILD_BASE_BUILDING and BUILD_BASE_BUILDING_PER_TACTICAL_MARKER both end in
## (..., UNIT, UNIT_REF) authored as (AI_CURRENT_CONSTRUCTION_SITE,
## AI_BARRACKS_1 / AI_FARM_LAST_BUILT / ...): a construction-site object, then
## a reference the NEW building is bound to. The UNIT_REF at the tail of this
## family is a DESTINATION, never an input.
##
##
## THE ARGUMENT TRAP
## =================
## sage_scb.py stores integer AND real AND text for EVERY non-position
## argument, so a type-search silently succeeds on the wrong slot. Every read
## below is BY INDEX with the signature written directly above it. The traps
## specific to this file:
##
##     TEAM_GUARD_TEAM(TEAM, TEAM)    actor first, guarded team second - two
##                                    arguments of the SAME type, unrecoverable
##                                    by any non-positional method
##     NAMED_BASE_UNPACK(UNIT, UNIT_REF)   the subject is FIRST and the
##                                    destination reference LAST - both
##                                    text-carried, so a swapped read would
##                                    unpack the reference name and bind the
##                                    base flag (the tail package meets the
##                                    same shape on BUILD_BASE_BUILDING)
##
## Of this file's parameter types only INT and NEAR_OR_FAR are integer-valued
## per SageScriptParamTypes.PAYLOAD_FIELD_FOR_PARAM; TEAM, UNIT, UNIT_REF,
## PLAYER and OBJECT_TYPE are carried as text.
##
## DURATIONS. TEAM_GUARD_FOR_SECONDS and TEAM_IDLE_FOR_SECONDS declare their
## seconds as INT (not SECONDS/REAL - the retail records carry 5, 10, 15, 30
## in the integer field). The world speaks interpreter TICKS only, so both
## handlers convert through SageScriptEnv.ticks_from_seconds, the same
## rounding the seconds-counter actions use. orders.guard's duration has a
## sentinel - "<= 0 means indefinitely" - which makes an authored duration of
## 0 seconds INEXPRESSIBLE: converting it honestly yields the sentinel and a
## forever-guard, the opposite of a no-op. See _guard_for_seconds.
##
##
## CONDITIONS MAY NOT GUESS
## ========================
## NAMED_BASE_UNPACKABLE_FOR_PLAYER is this package's highest-traffic member
## and it is a CONDITION, polled every evaluation pass of the economy loop. It
## must be perfectly side-effect free, and a read the world cannot answer must
## never come back as a confident "no": "this base flag cannot be unpacked"
## releases the AI to spend elsewhere, while "the world does not model base
## unpacking" must keep the gate shut WITH a structured gap on the record. The
## handler checks SageWorldQuery.ok first and returns WORLD_REFUSED otherwise,
## which the dispatcher turns into CONDITION_FALLBACK (false) plus the gap.

const PACKAGE := "WP11-basebuilding-ai"

const Dispatch := preload("res://src/script/script_dispatch.gd")


# ==========================================================================
# GAP-REGISTERED MEMBERS
# ==========================================================================
#
# One member whose world surface cannot carry arguments that change the
# outcome. It is declared through `reg.blocked_actions()` rather than given
# a body that returns OK, for the same two reasons as WP15's gaps: a body
# would count as coverage, and a shared refusal cannot be mistaken for an
# implementation while skimming.
#
# The string below names the EXACT facet signature that would unblock it.
# (The unpack pair sat here under the same rule until the world grew
# ai.base_unpack's result_reference parameter; it is served below now.)

## BUILD_BASE_BUILDING_PER_TACTICAL_MARKER
## (OBJECT_TYPE, NEAR_OR_FAR, OBJECT_TYPE, UNIT, UNIT_REF) - 8 AI call sites,
## 1 library.
const GAP_BUILD_PER_MARKER := (
	"tactical-marker base building (the action reads: build a <OBJECT_TYPE> "
	+ "on the <NEAR_OR_FAR> side of the marker of type <OBJECT_TYPE>, at "
	+ "construction site <UNIT>, and reference the new building as "
	+ "<UNIT_REF>. The world's corrected ai.build_base_building"
	+ "(building_type, base, result_reference) now carries the site and the "
	+ "reference - which is why the tail package serves plain "
	+ "BUILD_BASE_BUILDING - but this spelling ALSO authors a near/far side "
	+ "bit (retail authors BOTH values - near=0 and far=1 - to place farms "
	+ "away from the fight and barracks toward it) and a marker object type, "
	+ "and no facet parameter exists for either. Dropping the side bit alone "
	+ "would flip authored placement, and the sim models no tactical markers "
	+ "to resolve the pair against. NEEDED: "
	+ "ai.build_base_building_per_tactical_marker(building_type: String, "
	+ "near_or_far: int, marker_type: String, construction_site: String, "
	+ "result_reference: String) -> bool, with near_or_far the raw ParamTypes "
	+ "NEAR_OR_FAR int, plus tactical-marker state in the simulation)"
)

## Codex review: tactical-marker path falls through to ordinary base build and
## invents placement. Stay blocked until marker geometry is modeled.
const GAP_BUILD_PER_MARKER_ACTIONS := ["BUILD_BASE_BUILDING_PER_TACTICAL_MARKER"]


static func register(reg: SageScriptHandlerRegistry.Registrar) -> void:
	# --- Served, in AI call-site order (census counts) --------------------
	reg.condition("NAMED_BASE_UNPACKABLE_FOR_PLAYER", _condition_base_unpackable)  # 64
	reg.action("NAMED_BASE_UNPACK", _base_unpack)                       # 32
	reg.action("NAMED_BASE_UNPACK_FREE", _base_unpack_free)             #  8
	reg.action("TEAM_GUARD_TEAM", _guard_team)                          #  5
	reg.action("TEAM_GUARD_FOR_SECONDS", _guard_for_seconds)            #  4
	reg.action("TEAM_IDLE_FOR_SECONDS", _idle_for_seconds)              #  4
	reg.action(
		"CREATE_REINFORCEMENT_TEAM_AT_UNIT_POSITION", _create_reinforcement_team
	)                                                                   #  3

	reg.blocked_actions(GAP_BUILD_PER_MARKER_ACTIONS, GAP_BUILD_PER_MARKER)  # 8


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


# BUILD_BASE_BUILDING_PER_TACTICAL_MARKER is blocked above; no inventing handler.


# --- Condition ------------------------------------------------------------


static func _condition_base_unpackable(ctx: Dictionary) -> int:
	# NAMED_BASE_UNPACKABLE_FOR_PLAYER(UNIT, PLAYER)
	#
	# The base flag comes FIRST and the player SECOND. Both are text-carried,
	# so nothing but position separates them; the retail shape is
	# (BASE_FLAG_N, "<This Player>") and a swapped read would ask the world
	# whether a player name is an unpackable base - which a fixture-driven
	# world answers with a refusal, but a permissive one might answer with a
	# plausible false. Names pass through exactly as decoded, per the world's
	# argument conventions ("<This Player>" is a literal token the world
	# resolves, not something this handler expands).
	#
	# This is the single most-polled member of the package (64 call sites in
	# one library's economy loop), which is exactly why it must stay
	# side-effect free: it is re-evaluated an unpredictable number of times
	# per expansion decision, and any mutation here would be a determinism
	# bug in lockstep.
	var args: SageScriptArgs = ctx["args"]
	var query := (ctx["world"] as SageScriptWorld).ai().base_unpackable(
		args.text(0), args.text(1)
	)
	if not query.ok:
		return _unanswered(ctx, query)
	ctx["result"] = query.as_bool()
	return Dispatch.Status.OK


# --- Base unpacking --------------------------------------------------------


static func _base_unpack_shared(ctx: Dictionary, free: bool) -> int:
	# NAMED_BASE_UNPACK(UNIT, UNIT_REF) / NAMED_BASE_UNPACK_FREE(UNIT, UNIT_REF)
	#   "The base at <UNIT> unpacks ... and is referenced as <UNIT_REF>."
	#
	# The SUBJECT (base flag) is FIRST and the DESTINATION reference LAST -
	# both text-carried, so nothing but position separates them, and a swapped
	# read would try to unpack the reference name and bind the base flag. The
	# retail shapes are (BASE_FLAG_N, AI_EXPANSION_1..N) for the paid spelling
	# in ai_economy_execution and (BASE_FLAG_N, AI_BASE) for the free spelling
	# in ai_initialize; ai_opposition then reads AI_BASE as the spawn anchor
	# for CREATE_REINFORCEMENT_TEAM_AT_UNIT_POSITION, which is why the
	# reference argument is load-bearing and may never be dropped (class
	# comment). Neither spelling carries a player: the world acts as the
	# script-executing player it was configured with, exactly as the retail
	# engine runs each AI library in its owner's context.
	#
	# THE FREE/PAID FOLD. One world method serves both spellings behind the
	# `free` flag, because the ONLY sourced difference is whether the unpack
	# charges: folding them keeps a single unpack path to get right, and the
	# flag comes from the OPCODE, never from the arguments, so a map cannot
	# author a paid unpack into a free one.
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx, "ai.base_unpack",
		(ctx["world"] as SageScriptWorld).ai().base_unpack(
			args.text(0), free, args.text(1)
		)
	)


static func _base_unpack(ctx: Dictionary) -> int:
	return _base_unpack_shared(ctx, false)


static func _base_unpack_free(ctx: Dictionary) -> int:
	return _base_unpack_shared(ctx, true)


# --- Guard / idle ----------------------------------------------------------


static func _guard_team(ctx: Dictionary) -> int:
	# TEAM_GUARD_TEAM(TEAM, TEAM) - "<TEAM> begins guarding <TEAM>".
	#
	# TWO TEAMS, AND THE ORDER DECIDES WHO PROTECTS WHOM. Argument 0 is the
	# ACTOR (the team that takes the guard order); argument 1 is the team it
	# guards. Both are TEAM-typed strings, so no type information could
	# recover the order if it were read any way other than positionally.
	# Retail authors both directions - an artillery-defense team guarding
	# "<This Team>", and "<This Team>" guarding a weakpoint team - so a swap
	# here would not even fail loudly; it would quietly invert who screens
	# whom.
	#
	# SCOPE PATTERN. The world implements the whole *_GUARD* family once as
	# orders.guard(scope, name, target, duration_ticks); this package owns the
	# TEAM-guards-TEAM spelling and passes Scope.TEAM with a target_team()
	# dictionary. Duration 0 is the documented "<= 0 means indefinitely"
	# sentinel, which is exactly what an untimed guard order means.
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx, "orders.guard",
		(ctx["world"] as SageScriptWorld).orders().guard(
			SageScriptWorld.Scope.TEAM,
			args.text(0),
			SageScriptWorld.target_team(args.text(1)),
			0
		)
	)


static func _guard_for_seconds(ctx: Dictionary) -> int:
	# TEAM_GUARD_FOR_SECONDS(TEAM, INT) - "<TEAM> guards for <INT> seconds."
	#
	# The team guards its CURRENT position - the action names no other target,
	# so the target is target_self(), the tagged form of "the acting object
	# itself". The seconds are an INT (retail authors 10 and 15) and the world
	# takes TICKS, converted with the same rounding as the seconds-counter
	# actions so a guard-for-15 and a counter-set-to-15 agree exactly.
	#
	# A DURATION MEETING A SENTINEL. orders.guard documents duration_ticks
	# "<= 0 means indefinitely". An authored duration of 0 seconds therefore
	# CANNOT be expressed: converting it yields the sentinel and a forever-
	# guard where the map asked for a no-op - an inversion, not an
	# approximation - so 0 refuses and names the surface that would fix it. A
	# negative duration has no defined meaning in the reference at all and is
	# BAD_ARGUMENTS. Both retail call sites author positive values, so neither
	# refusal costs retail coverage; they exist because a mod may author 0.
	var args: SageScriptArgs = ctx["args"]
	var seconds := args.integer(1)
	if seconds < 0:
		ctx["detail"] = (
			"TEAM_GUARD_FOR_SECONDS was authored with a negative duration "
			+ "(%d seconds); the reference gives no meaning to a negative one"
		) % seconds
		return Dispatch.Status.BAD_ARGUMENTS
	if seconds == 0:
		ctx["detail"] = (
			"orders.guard treats duration_ticks <= 0 as 'guard indefinitely', "
			+ "so an authored duration of 0 seconds cannot be expressed - "
			+ "serving it would turn a no-op into a forever-guard; NEEDED: an "
			+ "explicit indefinite flag instead of the <= 0 sentinel"
		)
		return Dispatch.Status.WORLD_REFUSED
	var env: SageScriptEnv = ctx["env"]
	return _served(
		ctx, "orders.guard",
		(ctx["world"] as SageScriptWorld).orders().guard(
			SageScriptWorld.Scope.TEAM,
			args.text(0),
			SageScriptWorld.target_self(),
			env.ticks_from_seconds(float(seconds))
		)
	)


static func _idle_for_seconds(ctx: Dictionary) -> int:
	# TEAM_IDLE_FOR_SECONDS(TEAM, INT) - "<TEAM> idles for <INT> seconds."
	#
	# Same INT-seconds-to-ticks conversion as the guard above, different
	# sentinel situation: orders.idle_for_ticks documents NO indefinite
	# sentinel, so 0 ticks is a plain zero-length idle and an authored 0 is
	# exactly expressible - it is served, not refused. Only a negative
	# duration, which the reference gives no meaning, is rejected. Retail
	# authors 5 and 30, in two libraries (the opposition pacing loop and the
	# economy sequencer).
	var args: SageScriptArgs = ctx["args"]
	var seconds := args.integer(1)
	if seconds < 0:
		ctx["detail"] = (
			"TEAM_IDLE_FOR_SECONDS was authored with a negative duration "
			+ "(%d seconds); the reference gives no meaning to a negative one"
		) % seconds
		return Dispatch.Status.BAD_ARGUMENTS
	var env: SageScriptEnv = ctx["env"]
	return _served(
		ctx, "orders.idle_for_ticks",
		(ctx["world"] as SageScriptWorld).orders().idle_for_ticks(
			SageScriptWorld.Scope.TEAM,
			args.text(0),
			env.ticks_from_seconds(float(seconds))
		)
	)


# --- Reinforcements --------------------------------------------------------


static func _create_reinforcement_team(ctx: Dictionary) -> int:
	# CREATE_REINFORCEMENT_TEAM_AT_UNIT_POSITION(TEAM, UNIT)
	#   "Create a reinforcement team of <TEAM> at <UNIT>'s position."
	#
	# The team TEMPLATE comes first, the anchor OBJECT second; the target is
	# target_object() because the action names the unit and the world resolves
	# its position (the AT_WAYPOINT sibling, not on the AI list, would pass
	# target_waypoint() to the same method). The retail anchor is AI_BASE -
	# the very reference NAMED_BASE_UNPACK_FREE binds (served above, since
	# the world grew the reference parameter), read here where a plain UNIT
	# is declared: the shared object/unit-reference namespace is what makes
	# that chain resolve. The world method itself still refuses (no
	# reinforcement-team model in the sim), which is an honest gap, not a
	# defect in this handler.
	#
	# THE EMPTY PLAYER IS NOT A DROPPED ARGUMENT. The world method is
	# ai.create_reinforcement_team(player, team, target), but NEITHER
	# vocabulary spelling of this action carries a PLAYER parameter (the
	# WAYPOINT sibling is (TEAM, WAYPOINT)); a SAGE team template already
	# belongs to a player. The handler passes "" - "derive ownership from the
	# team template" - because inventing a player here would ADD an argument
	# the map never authored. Reported as an open question against the facet:
	# its player parameter has no possible source in either action.
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx, "ai.create_reinforcement_team",
		(ctx["world"] as SageScriptWorld).ai().create_reinforcement_team(
			"",
			args.text(0),
			SageScriptWorld.target_object(args.text(1))
		)
	)

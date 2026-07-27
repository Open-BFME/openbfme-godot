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
## (2 call sites, 2 libraries). It is left to the completing package, with a
## warning it should read first: its signature (OBJECT_TYPE, UNIT, UNIT_REF)
## carries the same construction-site and result-reference arguments that
## gap-register BUILD_BASE_BUILDING_PER_TACTICAL_MARKER below, so it shares
## that member's world-surface gap and cannot be served today either.
##
##
## WHAT IS SERVED AND WHAT IS NOT
## ==============================
## 5 members are implemented (1 condition, 4 actions). 3 are GAP-REGISTERED,
## all for the same class of reason as WP15's gaps: the action carries a
## LOAD-BEARING ARGUMENT that the SageScriptWorld facet surface has no
## parameter for. Those are recorded as findings against the world surface,
## not papered over here - see GAP_* below, which states the exact missing
## signature for each.
##
## The rule applied throughout: an argument that changes the OUTCOME may never
## be dropped. Serving NAMED_BASE_UNPACK by calling ai.base_unpack(name, free)
## without the authored result reference would not be an approximation: every
## retail call site binds one, and a later script then reads it (see the
## RETAIL SHAPES section). A refusal is recoverable; a reference that was
## silently never bound is not.
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
##                                    destination reference LAST (gap-registered
##                                    here, but the completing agent will meet
##                                    the same shape on BUILD_BASE_BUILDING)
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
# Three members whose world surface cannot carry an argument that changes the
# outcome. They are declared through `reg.blocked_actions()` rather than given
# a body that returns OK, for the same two reasons as WP15's gaps: a body
# would count as coverage, and a shared refusal cannot be mistaken for an
# implementation while skimming.
#
# Each string below names the EXACT facet signature that would unblock it.
# None of these needs a new simulation subsystem - the Ai facet exists. They
# need parameters the facet does not have, which is a coordinated edit to
# script_world.gd and therefore not this agent's to make.

## NAMED_BASE_UNPACK(UNIT, UNIT_REF) - 32 AI call sites, 1 library.
## NAMED_BASE_UNPACK_FREE(UNIT, UNIT_REF) - 8 AI call sites, 1 library.
const GAP_BASE_UNPACK := (
	"base unpack with a result reference (both actions' second argument is a "
	+ "UNIT_REF DESTINATION - the base at <UNIT> unpacks and the resulting "
	+ "base object is referenced as <UNIT_REF> - and every retail AI call "
	+ "site binds one: the 32 paid unpacks in ai_economy_execution bind "
	+ "AI_EXPANSION_1..N and the 8 free unpacks in ai_initialize all bind "
	+ "AI_BASE, which ai_opposition then reads as the spawn anchor for "
	+ "CREATE_REINFORCEMENT_TEAM_AT_UNIT_POSITION. The world offers only "
	+ "ai.base_unpack(object_name, free): the free/paid fold is expressible, "
	+ "the reference binding is not, so serving these would leave every "
	+ "downstream reader looking up a name that was never bound. NEEDED: "
	+ "ai.base_unpack(object_name: String, free: bool, result_reference: "
	+ "String) -> bool. A call authoring an EMPTY reference would be exactly "
	+ "expressible today, but no retail AI call site does, so the pair is "
	+ "declared blocked rather than shipped as an 'implementation' that "
	+ "refuses 100% of retail traffic)"
)

## BUILD_BASE_BUILDING_PER_TACTICAL_MARKER
## (OBJECT_TYPE, NEAR_OR_FAR, OBJECT_TYPE, UNIT, UNIT_REF) - 8 AI call sites,
## 1 library.
const GAP_BUILD_PER_MARKER := (
	"tactical-marker base building (the action reads: build a <OBJECT_TYPE> "
	+ "on the <NEAR_OR_FAR> side of the marker of type <OBJECT_TYPE>, at "
	+ "construction site <UNIT>, and reference the new building as "
	+ "<UNIT_REF>. The world's ai.build_base_building(player, building_type, "
	+ "slot, marker) matches only the building type: it demands a player the "
	+ "action does not carry, and has no parameter for the near/far side "
	+ "(retail authors BOTH values - near=0 and far=1 - to place farms away "
	+ "from the fight and barracks toward it), the marker object type, the "
	+ "construction-site object, or the result reference that the predictive- "
	+ "building scripts read back. Dropping the side bit alone would flip "
	+ "authored placement; dropping the reference orphans the readers, as "
	+ "with the unpack pair. NEEDED: "
	+ "ai.build_base_building_per_tactical_marker(building_type: String, "
	+ "near_or_far: int, marker_type: String, construction_site: String, "
	+ "result_reference: String) -> bool, with near_or_far the raw ParamTypes "
	+ "NEAR_OR_FAR int. The completing package's BUILD_BASE_BUILDING"
	+ "(OBJECT_TYPE, UNIT, UNIT_REF) shares the site+reference half of this "
	+ "gap)"
)

const GAP_BASE_UNPACK_ACTIONS := ["NAMED_BASE_UNPACK", "NAMED_BASE_UNPACK_FREE"]
const GAP_BUILD_PER_MARKER_ACTIONS := ["BUILD_BASE_BUILDING_PER_TACTICAL_MARKER"]


static func register(reg: SageScriptHandlerRegistry.Registrar) -> void:
	# --- Served, in AI call-site order (census counts) --------------------
	reg.condition("NAMED_BASE_UNPACKABLE_FOR_PLAYER", _condition_base_unpackable)  # 64
	reg.action("TEAM_GUARD_TEAM", _guard_team)                          #  5
	reg.action("TEAM_GUARD_FOR_SECONDS", _guard_for_seconds)            #  4
	reg.action("TEAM_IDLE_FOR_SECONDS", _idle_for_seconds)              #  4
	reg.action(
		"CREATE_REINFORCEMENT_TEAM_AT_UNIT_POSITION", _create_reinforcement_team
	)                                                                   #  3

	# --- Gap-registered: the world surface cannot carry the argument ------
	reg.blocked_actions(GAP_BASE_UNPACK_ACTIONS, GAP_BASE_UNPACK)       # 32 + 8
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
	# the very reference the gap-registered NAMED_BASE_UNPACK_FREE was
	# supposed to bind, which is documented in GAP_BASE_UNPACK above: until
	# the world grows the reference parameter, a full retail run will refuse
	# the unpack and this action will then ask about an unbound name, which
	# the world must answer with its own refusal. That is the correct chain of
	# two honest gaps, not a defect in this handler.
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

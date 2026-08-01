extends RefCounted

## WP15-teams - the AI-CRITICAL SUBSET.
##
## SCOPE, AND WHY THIS FILE IS NOT THE WHOLE PACKAGE
## =================================================
## WP15 has 81 members. This file implements the 24 that the RETAIL AI SCRIPT
## LIBRARIES actually call, and nothing else. The remaining 57 are a later
## agent's work and will land in a SEPARATE file, because two files may not
## claim one PACKAGE constant (see handlers/_registry.gd). That is why this
## module is named wp15_ai_teams rather than wp15_teams: the name that is still
## free is the one the completing package should take.
##
## The 24 were not chosen by taste. They are the exact set the decoder found in
## the 14 referenced AI_* libraries of both retail trees (BFME2 and RotWK agree
## member-for-member and count-for-count): 244 call sites, 4.26% of all retail-AI
## script traffic, the second-largest bucket after the WP09 science pair.
##
## Counts below are AI call sites in one tree, highest first. They decide the
## order things appear in this file, so the most-exercised code is the code a
## reader meets first.
##
##
## WHAT IS SERVED AND WHAT IS NOT
## ==============================
## 20 members are implemented. 4 are GAP-REGISTERED.
## same class of reason: the action carries a LOAD-BEARING ARGUMENT that the
## SageScriptWorld facet surface has no parameter for. Those are recorded as
## findings against the world surface, not papered over here - see
## GAP_* below, which states the exact missing signature for each.
## TEAM_SET_CUSTOM_STATE and TEAM_STAND_GROUND were the fifth and sixth: their
## facet-signature packets landed the boolean flags the original gap
## registrations demanded
## (teams.set_custom_state(team, state, enabled)), so the member is now
## SERVED - see _set_custom_state.
##
## The rule applied throughout: an argument that changes the OUTCOME may never
## be dropped. TEAM_STAND_GROUND now passes its FALSE through to the world's
## status setter, which clears HoldGround to Battle rather than silently
## inverting the authored action.
##
##
## THE ARGUMENT TRAP
## =================
## sage_scb.py stores integer AND real AND text for EVERY non-position argument.
## There is no type-code table to fall back on, so reading "the string one" or
## "the int one" silently succeeds on the wrong slot. Every read below is BY
## INDEX with the signature written directly above it. Two signatures in this
## file put the destination FIRST and would be read backwards by any other
## method:
##
##     SET_TEAM_REFERENCE(TEAM_REF, TEAM)              destination first
##     SET_COUNTER_TO_TEAM_THREAT(COUNTER, TEAM, REAL) destination first
##
## and one puts two TEAMs side by side, where no type-search can tell them
## apart at all:
##
##     TEAM_MERGE_INTO_TEAM(TEAM, TEAM)                source, then destination
##
## Which decoded FIELD each parameter is read from comes from
## SageScriptParamTypes.PAYLOAD_FIELD_FOR_PARAM, which commit 45b0b43 corrected
## for ten enums. Of this file's parameter types only AI_MOOD, BOOLEAN and INT
## are integer-valued; TEAM_STATE is NOT an enum in that table and is carried as
## TEXT, which the retail payloads confirm (AI_HUNT, AI_ASSAULTING, ...).
##
##
## CONDITIONS MAY NOT GUESS
## ========================
## All 5 conditions here read team state, and a team-state read that the world
## cannot answer must never come back as `false`. "This team is not retreating"
## and "I have no idea what this team is doing" are different answers, and only
## the first one may release an AI attack wave. Every condition checks
## `SageWorldQuery.ok` first and returns WORLD_REFUSED otherwise, which the
## dispatcher turns into CONDITION_FALLBACK (false) *and* a structured gap - the
## gate stays shut and the reason is on the record.
##
## TEAM_STATE_IS_NOT is deliberately NOT written as `not TEAM_STATE_IS`. Negating
## an unanswered read would turn a refusal into a confident `true`, which is the
## single worst outcome available in this file: it is the condition that gates
## "has this team stopped retreating" in ai_attack_execution.

const PACKAGE := "WP15-teams"

const Dispatch := preload("res://src/script/script_dispatch.gd")


# ==========================================================================
# GAP-REGISTERED MEMBERS
# ==========================================================================
#
# Three members whose world surface cannot carry an argument that changes the
# outcome. They are declared through `reg.blocked_actions()` rather than given a
# body that returns OK, for two reasons: a body would count as coverage, and a
# shared refusal cannot be mistaken for an implementation while skimming.
#
# Each string below names the EXACT facet signature that would unblock it. None
# of these needs a new simulation subsystem - the Teams and Orders facets both
# exist. They need one more parameter each, which is a coordinated edit to
# script_world.gd and therefore not this agent's to make.
#
# TEAM_SET_CUSTOM_STATE and TEAM_STAND_GROUND used to be the fifth and sixth
# entries here, each missing its BOOLEAN. Their demanded signatures have since
# landed, so both members are served above.

## SET_COUNTER_TO_TEAM_THREAT(COUNTER, TEAM, REAL) - 13 AI call sites.
const GAP_TEAM_THREAT := (
	"team threat within a radius (the action reads 'Set <COUNTER> to <TEAM> "
	+ "radius of <REAL>' and the retail AI authors three different radii - 400, "
	+ "450 and 750 - to compare front, back and hard-difficulty threat. The "
	+ "world offers only teams.threat(team), with no radius, so every call site "
	+ "would collapse onto one unspecified number and the three gates would "
	+ "stop distinguishing. NEEDED: teams.threat(team: String, radius: float) "
	+ "-> SageWorldQuery)"
)

## TEAM_RECRUIT_UNITS(TEAM, INT, OBJECT_TYPE_LIST) - 7 AI call sites.
## TEAM_RECRUIT_UNITS_FROM_TEAM(TEAM, INT, OBJECT_TYPE_LIST, TEAM) - 3.
const GAP_RECRUIT_UNITS := (
	"typed unit recruitment (both actions name an OBJECT_TYPE_LIST selecting "
	+ "WHICH units to recruit - retail authors Factory_Buildings, "
	+ "Economy_Buildings, Camp_Keeps, IsengardFighterHorde - and the world's "
	+ "teams.recruit(team, radius, from_team) has no parameter for it, so the "
	+ "type filter would be dropped and the team would recruit the wrong units. "
	+ "The INT was a second, independent problem, and it is now RULED: the "
	+ "community reference's description for these two entries is garbled by "
	+ "placeholder substitution ('will recruit <TEAM> units of type <INT> from "
	+ "nearby recruitable allied teams.<OBJECT_TYPE_LIST>'), but the BFME "
	+ "binary's own action templates are not - action 397 reads '<TEAM> will "
	+ "recruit <INT> units of type <OBJECT_TYPE_LIST> from nearby recruitable "
	+ "allied teams' and action 398 '... from <TEAM>'. So the INT is a unit "
	+ "COUNT, and this pair carries no radius at all - the radius belongs to "
	+ "the sibling RECRUIT_TEAM(TEAM, REAL), which spells it in feet. "
	+ "teams.recruit's (team, radius, from_team) shape therefore fits the "
	+ "RECRUIT_TEAM pair and NOT these two. NEEDED: a separate signature, "
	+ "teams.recruit_units(team, count: int, object_type_list: String, "
	+ "from_team: String))"
)

## SET_REF_TO_NEREST_TEAM_OF_TYPE_OWNED_BY_PLAYER [sic] - 1 AI call site.
const GAP_REF_TO_NEAREST := (
	"nearest-object-to-a-team reference. The exact four payload positions and "
	+ "the corrected five-parameter world signature are known: "
	+ "(OBJECT_TYPE_LIST, PLAYER, TEAM anchor, UNIT_REF destination). What is "
	+ "not yet sourced is the load-bearing BFME2 team-position rule. The "
	+ "available Generals GPL evidence uses both Team::getEstimateTeamPosition "
	+ "(first member) and explicit all-object centroids in related paths, and "
	+ "no BFME2 implementation/runtime oracle proves which this opcode uses. "
	+ "Choosing either can bind a different AI_GATE. NEEDED: BFME2 binary "
	+ "decode or controlled retail observation of the anchor-position rule)"
)

## Formerly gap-registered; now served via recruit_units / threat_within_radius /
## set_reference_to_nearest. Empty arrays preserve runner census shape.
const GAP_TEAM_THREAT_ACTIONS: Array = []
const GAP_RECRUIT_ACTIONS: Array = []
const GAP_REF_TO_NEAREST_ACTIONS: Array = []


static func register(reg: SageScriptHandlerRegistry.Registrar) -> void:
	# --- Served, in AI call-site order ------------------------------------
	reg.action("TEAM_SET_CUSTOM_STATE", _set_custom_state)              # 40
	reg.action("TEAM_AVAILABLE_FOR_RECRUITMENT", _set_available_for_recruitment)
	reg.action("TEAM_TRANSFER_TO_PLAYER", _transfer_to_player)          # 32
	reg.action("TEAM_SET_STATE", _set_state)                            # 32
	reg.action("TEAM_EXECUTE_SEQUENTIAL_SCRIPT", _sequential_script)    # 24
	reg.action("TEAM_MERGE_INTO_TEAM", _merge_into_team)                # 15
	reg.action("TEAM_SET_ATTITUDE", _set_attitude)                      # 14
	reg.action("TEAM_STOP", _stop)                                      # 11
	reg.action("TEAM_HUNT", _hunt)                                      #  6
	reg.action("TEAM_STAND_GROUND", _stand_ground)                       #  2
	reg.action("TEAM_EXECUTE_SEQUENTIAL_SCRIPT_LOOPING", _sequential_script_looping)  # 5
	reg.action("TEAM_STOP_SEQUENTIAL_SCRIPT", _stop_sequential_script)  #  3
	reg.action("SET_TEAM_REFERENCE", _set_team_reference)               #  2
	reg.action("UNIT_SET_TEAM", _unit_set_team)                         #  1
	reg.action("TEAM_EXIT_ALL_BUILDINGS", _exit_all_buildings)          #  1
	reg.action("TEAM_DELETE", _delete)                                  #  1

	reg.condition("TEAM_HAS_CUSTOM_STATE", _condition_has_custom_state) # 16
	reg.condition("TEAM_STATE_IS_NOT", _condition_state_is_not)         #  9
	reg.condition("TEAM_HAS_UNITS", _condition_has_units)               #  4
	reg.condition("TEAM_STATE_IS", _condition_state_is)                 #  1
	reg.condition("TEAM_CREATED", _condition_created)                   #  1

	# Formerly gap-registered; world now carries radius/count/ref signatures.
	reg.action("SET_COUNTER_TO_TEAM_THREAT", _set_counter_to_team_threat)  # 13
	reg.action("TEAM_RECRUIT_UNITS", _recruit_units)                    #  7
	reg.action("TEAM_RECRUIT_UNITS_FROM_TEAM", _recruit_units_from_team)  # 3
	reg.action("SET_REF_TO_NEREST_TEAM_OF_TYPE_OWNED_BY_PLAYER", _set_ref_to_nearest)  # 1


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


# --- Team control ---------------------------------------------------------


static func _transfer_to_player(ctx: Dictionary) -> int:
	# TEAM_TRANSFER_TO_PLAYER(TEAM, PLAYER)
	#
	# 32 AI call sites (16 ai_initialize + 16 ai_mp_inherit_management), all of the shape
	# ("PlyrCivilian/Player_N_Inherit", "<This Player>"): this is how a
	# skirmish player takes control of its authored civilian inheritance team.
	# Retail Fords maps place tactical markers in those teams; the action changes
	# Team::setControllingPlayer rather than invoking player-wide asset transfer.
	# The team name is a literal containing a slash; canonical resolution belongs
	# to the world, not this argument-preserving handler.
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx, "teams.transfer_to_player",
		(ctx["world"] as SageScriptWorld).teams().transfer_to_player(args.text(0), args.text(1))
	)


static func _set_state(ctx: Dictionary) -> int:
	# TEAM_SET_STATE(TEAM, TEAM_STATE)
	#
	# TEAM_STATE is a TEXT token (AI_HUNT, AI_ATTACKING, AI_RETREATING, ...). It
	# is deliberately NOT in SageScriptParamTypes.ENUMS: the reference gives no
	# value table for it, the tokens are content-defined, and inventing an
	# integer mapping here would break any mod that names a state of its own.
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx, "teams.set_state",
		(ctx["world"] as SageScriptWorld).teams().set_state(args.text(0), args.text(1))
	)


static func _set_custom_state(ctx: Dictionary) -> int:
	# TEAM_SET_CUSTOM_STATE(TEAM, TEAM_STATE, BOOLEAN)
	#   "<TEAM> set custom state <TEAM_STATE> to <BOOLEAN>"
	#
	# 40 AI call sites - the single most-called WP15 member, formerly
	# gap-registered here because the facet had no slot for the BOOLEAN. The
	# corrected signature carries it, so the member is served.
	#
	# THE BOOLEAN IS LOAD-BEARING AND LIVES IN THE INTEGER FIELD. The decoded
	# corpus authors both polarities on the SAME tokens (AI_ADVANCING enabled
	# at 34 sites and disabled at 34 others; corpus-wide 202 enables to 183
	# disables), each carried as argumentType 8 with integer 0 or 1 and empty
	# text/real. Reading the wrong field, or dropping the flag, would not
	# approximate the action - it would INVERT the 183 clear sites, which is
	# the exact outcome the original gap registration existed to prevent.
	# The TEAM_STATE token itself is TEXT, like TEAM_SET_STATE's.
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx, "teams.set_custom_state",
		(ctx["world"] as SageScriptWorld).teams().set_custom_state(
			args.text(0), args.text(1), args.boolean(2)
		)
	)


static func _set_available_for_recruitment(ctx: Dictionary) -> int:
	# TEAM_AVAILABLE_FOR_RECRUITMENT(TEAM, BOOLEAN), action 94.
	# The boolean is an explicit tri-state override at the simulation boundary:
	# false cannot be folded into "never set", because retail then falls back
	# to the default-team / prototype recruitability setting.
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx,
		"teams.set_available_for_recruitment",
		(ctx["world"] as SageScriptWorld).teams().set_available_for_recruitment(
			args.text(0), args.boolean(1)
		),
	)


static func _merge_into_team(ctx: Dictionary) -> int:
	# TEAM_MERGE_INTO_TEAM(TEAM, TEAM) - "<TEAM> merges onto <TEAM>".
	#
	# TWO TEAMS, AND THE ORDER DECIDES WHO SURVIVES. Argument 0 is the team that
	# is absorbed; argument 1 is the destination it joins. Both are TEAM-typed
	# strings, so there is no type information that could recover the order if it
	# were read any way other than positionally - swapping them would silently
	# dissolve the wrong team.
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx, "teams.merge_into",
		(ctx["world"] as SageScriptWorld).teams().merge_into(args.text(0), args.text(1))
	)


static func _set_attitude(ctx: Dictionary) -> int:
	# TEAM_SET_ATTITUDE(TEAM, AI_MOOD)
	#
	# AI_MOOD is integer-valued (PAYLOAD_FIELD_FOR_PARAM), so it is read from the
	# `integer` field, not from `text`. Before 45b0b43 the enum table was missing
	# rows of exactly this kind and such a read returned "".
	#
	# THE VALUE IS PASSED THROUGH RAW AND UNVALIDATED, ON PURPOSE. The reference
	# documents AI_MOOD as Peaceful=0 .. Agressive=5, but the retail AI libraries
	# author -3 at real call sites alongside 0 and 2. Whatever -3 means to the
	# engine, it is what the shipped AI says, and this is a new engine
	# implementing authored content: clamping it into 0..5, or rejecting it as
	# BAD_ARGUMENTS, would change the behaviour of retail content to match a
	# table the content itself contradicts. The world receives the authored
	# integer and decides. Reported as an open question about the AI_MOOD range.
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx, "teams.set_attitude",
		(ctx["world"] as SageScriptWorld).teams().set_attitude(args.text(0), args.integer(1))
	)


static func _stop(ctx: Dictionary) -> int:
	# TEAM_STOP(TEAM)
	#
	# The world folds TEAM_STOP and TEAM_STOP_AND_DISBAND into one method with a
	# flag. This is the plain stop, so the team survives: disband is FALSE.
	# TEAM_STOP_AND_DISBAND is a WP15 member but not an AI-used one, so the
	# completing package registers it - not this file.
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx, "teams.stop",
		(ctx["world"] as SageScriptWorld).teams().stop(args.text(0), false)
	)


static func _hunt(ctx: Dictionary) -> int:
	# TEAM_HUNT(TEAM)
	#
	# SCOPE PATTERN. SAGE spells hunting three times - TEAM_HUNT, NAMED_HUNT,
	# PLAYER_HUNT - and the world implements it once as
	# orders.hunt(scope, name, command_button). This package owns only the TEAM
	# spelling; WP16 and WP17 pass their own Scope to the same method.
	#
	# The empty command button is not a placeholder: it is the documented way to
	# say "hunt with the default weapon", as opposed to WP13's
	# TEAM_HUNT_WITH_COMMAND_BUTTON which names one.
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx, "orders.hunt",
		(ctx["world"] as SageScriptWorld).orders().hunt(
			SageScriptWorld.Scope.TEAM, args.text(0), ""
		)
	)

static func _stand_ground(ctx: Dictionary) -> int:
	# TEAM_STAND_GROUND(TEAM, BOOLEAN). Both retail-AI sites author FALSE,
	# but both polarities are preserved: this is a status setter, not an
	# unconditional order.
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx, "orders.stand_ground",
		(ctx["world"] as SageScriptWorld).orders().stand_ground(
			SageScriptWorld.Scope.TEAM, args.text(0), args.boolean(1)
		)
	)


static func _delete(ctx: Dictionary) -> int:
	# TEAM_DELETE(TEAM) - "<TEAM> is removed from the world."
	#
	# Paired in the world with TEAM_DELETE_LIVING behind a `living_only` flag.
	# This is the unqualified delete, so living_only is FALSE: the whole team
	# goes, not just its living members.
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx, "teams.delete",
		(ctx["world"] as SageScriptWorld).teams().delete(args.text(0), false)
	)


static func _exit_all_buildings(ctx: Dictionary) -> int:
	# TEAM_EXIT_ALL_BUILDINGS(TEAM)
	#
	# teams.exit_all(team, buildings_only) also serves TEAM_EXIT_ALL. This is the
	# BUILDINGS variant, so the flag is TRUE - the team leaves buildings but not
	# other containers such as transports.
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx, "teams.exit_all",
		(ctx["world"] as SageScriptWorld).teams().exit_all(args.text(0), true)
	)


# --- References -----------------------------------------------------------


static func _set_team_reference(ctx: Dictionary) -> int:
	# SET_TEAM_REFERENCE(TEAM_REF, TEAM) - "Set <TEAM_REF> to reference team <TEAM>"
	#
	# DESTINATION FIRST. Argument 0 is the reference being bound
	# (AI_TEAM_PATROL_MOVE, AI_MAIN_BASE_ALERT_TEAM at the retail call sites) and
	# argument 1 is the team it points at. Both are strings and the natural
	# reading order of the English description is the reverse of the argument
	# order, which is exactly how a hand-written handler gets this backwards.
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx, "teams.set_reference",
		(ctx["world"] as SageScriptWorld).teams().set_reference(args.text(0), args.text(1))
	)


static func _unit_set_team(ctx: Dictionary) -> int:
	# UNIT_SET_TEAM(UNIT, TEAM) - "Unit named <UNIT> will join team named <TEAM>."
	#
	# Catalogued under WP15 because it changes team membership, but it acts on a
	# single named object, so it goes to the UNITS facet rather than the Teams
	# one. Registered here because the AI calls it and the catalog assigns it
	# here; the facet choice follows the action, not the package.
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx, "units.set_team",
		(ctx["world"] as SageScriptWorld).units().set_team(args.text(0), args.text(1))
	)


# --- Sequential scripts ---------------------------------------------------


static func _sequential_script(ctx: Dictionary) -> int:
	# TEAM_EXECUTE_SEQUENTIAL_SCRIPT(TEAM, SCRIPT)
	#
	# The non-looping form: the team runs the script once through. 24 AI call
	# sites, which is how ai_attack_execution drives every behaviour script
	# ("be_Attack Center", "be_AIAttack - Retreat if can't path to attacker").
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx, "teams.execute_sequential_script",
		(ctx["world"] as SageScriptWorld).teams().execute_sequential_script(
			args.text(0), args.text(1), false
		)
	)


static func _sequential_script_looping(ctx: Dictionary) -> int:
	# TEAM_EXECUTE_SEQUENTIAL_SCRIPT_LOOPING(TEAM, SCRIPT, INT)
	#   "<TEAM> executes <SCRIPT> sequentially, <INT> times. (0=forever)"
	#
	# A REPEAT COUNT MEETING A BOOLEAN. The world models looping as
	# execute_sequential_script(team, script, looping: bool). Two of the three
	# cases are not approximations but exact identities, and are served:
	#
	#     count 0  ->  looping = true   ("0=forever" is the reference's own words)
	#     count 1  ->  looping = false  (run through once - indistinguishable
	#                                    from the non-looping action above)
	#
	# A count of 2 or more is a FINITE repeat, which a boolean cannot express at
	# all. Serving it as `true` would loop forever where the map asked for three
	# passes, and as `false` would drop the repeats; both change the outcome, so
	# it refuses and names the surface that would fix it. All 5 retail AI call
	# sites pass 0, so this refusal costs no retail coverage today - it is here
	# because a mod may legitimately author 3.
	var args: SageScriptArgs = ctx["args"]
	var count := args.integer(2)
	if count < 0:
		ctx["detail"] = (
			"TEAM_EXECUTE_SEQUENTIAL_SCRIPT_LOOPING was authored with a negative "
			+ "repeat count (%d); the reference defines 0 as forever and positive "
			+ "values as a repeat count, and gives no meaning to a negative one"
		) % count
		return Dispatch.Status.BAD_ARGUMENTS
	if count > 1:
		ctx["detail"] = (
			"teams.execute_sequential_script takes a boolean `looping`, which "
			+ "cannot express the finite repeat count of %d that this call "
			+ "authored; NEEDED: a repeat-count parameter "
			+ "(0 = forever) instead of the boolean"
		) % count
		return Dispatch.Status.WORLD_REFUSED
	return _served(
		ctx, "teams.execute_sequential_script",
		(ctx["world"] as SageScriptWorld).teams().execute_sequential_script(
			args.text(0), args.text(1), count == 0
		)
	)


static func _stop_sequential_script(ctx: Dictionary) -> int:
	# TEAM_STOP_SEQUENTIAL_SCRIPT(TEAM)
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx, "teams.stop_sequential_script",
		(ctx["world"] as SageScriptWorld).teams().stop_sequential_script(args.text(0))
	)


# --- Conditions -----------------------------------------------------------
#
# Read the class comment before touching any of these. Every one of them may
# answer true, may answer false, or may refuse - and the third is not the second.


static func _condition_state_is(ctx: Dictionary) -> int:
	# TEAM_STATE_IS(TEAM, TEAM_STATE)
	#
	# Exact string comparison, with no case folding and no trimming. The world's
	# argument convention is that names arrive exactly as the decoded script
	# carries them; a handler that lower-cased here would make a modder's
	# "AI_Retreating" and "ai_retreating" the same state in this engine and
	# different ones in the retail engine.
	var args: SageScriptArgs = ctx["args"]
	var query := (ctx["world"] as SageScriptWorld).teams().state(args.text(0))
	if not query.ok:
		return _unanswered(ctx, query)
	ctx["result"] = query.as_string() == args.text(1)
	return Dispatch.Status.OK


static func _condition_state_is_not(ctx: Dictionary) -> int:
	# TEAM_STATE_IS_NOT(TEAM, TEAM_STATE)
	#
	# NOT `not _condition_state_is(...)`. The two differ only when the world
	# cannot answer, and that is the case that matters: composing the negation
	# would turn "the world does not model team state" into a confident "this
	# team is not retreating" and release an attack wave on it. The refusal path
	# is therefore duplicated rather than shared, and the duplication is the
	# point.
	var args: SageScriptArgs = ctx["args"]
	var query := (ctx["world"] as SageScriptWorld).teams().state(args.text(0))
	if not query.ok:
		return _unanswered(ctx, query)
	ctx["result"] = query.as_string() != args.text(1)
	return Dispatch.Status.OK


static func _condition_has_custom_state(ctx: Dictionary) -> int:
	# TEAM_HAS_CUSTOM_STATE(TEAM, TEAM_STATE)
	#
	# The READ half of the custom-state pair (the write is _set_custom_state
	# above, served since the facet gained its enable flag).
	#
	# TWO SHAPES ARE ACCEPTED, AND ANYTHING ELSE REFUSES. The world declares
	# teams.custom_state(team) -> SageWorldQuery without fixing the value type,
	# and the two readings are both defensible:
	#
	#   * an ARRAY of the tokens currently set - which is what the writer's
	#     BOOLEAN argument implies, since retail toggles AI_ADVANCING on and off
	#     independently of AI_ASSAULTING;
	#   * a single STRING, if a team holds one custom state at a time.
	#
	# Committing to one and silently mis-reading the other would produce a false
	# `false`: a membership test against a String yields [] and answers "no" for
	# a state that IS set. So both are handled explicitly, and a value that is
	# neither refuses rather than defaulting. The ambiguity has since been
	# RULED on the write's evidence (the simulation-backed world answers the
	# ARRAY of enabled tokens - retail toggles tokens independently, so a team
	# holds a set); the String branch stays because stub worlds legitimately
	# answer the single-value shape and every branch here is either correct or
	# a refusal.
	var args: SageScriptArgs = ctx["args"]
	var query := (ctx["world"] as SageScriptWorld).teams().custom_state(args.text(0))
	if not query.ok:
		return _unanswered(ctx, query)
	var wanted := args.text(1)
	if query.value is Array:
		ctx["result"] = (query.value as Array).has(wanted)
		return Dispatch.Status.OK
	if query.value is String or query.value is StringName:
		ctx["result"] = String(query.value) == wanted
		return Dispatch.Status.OK
	ctx["detail"] = (
		"teams.custom_state answered with %s, which is neither the array of "
		+ "active state tokens nor a single state name; TEAM_HAS_CUSTOM_STATE "
		+ "cannot be decided from it and will not guess"
	) % type_string(typeof(query.value))
	return Dispatch.Status.WORLD_REFUSED


static func _condition_has_units(ctx: Dictionary) -> int:
	# TEAM_HAS_UNITS(TEAM) - "<TEAM> has one or more units."
	#
	# Strictly greater than zero. A refused count is NOT zero: the whole reason
	# unit_count returns a SageWorldQuery is that "this team is empty" and "there
	# is no such team / the world models no teams" are different facts, and only
	# the first one may report an AI wave as spent.
	var args: SageScriptArgs = ctx["args"]
	var query := (ctx["world"] as SageScriptWorld).teams().unit_count(args.text(0))
	if not query.ok:
		return _unanswered(ctx, query)
	ctx["result"] = query.as_int() > 0
	return Dispatch.Status.OK


static func _condition_created(ctx: Dictionary) -> int:
	# TEAM_CREATED(TEAM) - "<TEAM> has been created."
	#
	# An EDGE, per the world's own note on teams.was_created: true on the tick
	# the team appeared, not "a team by this name exists" (that is teams.exists,
	# which this must not be wired to - a level-triggered read here would fire
	# the AI's one-shot initialisation on every evaluation).
	var args: SageScriptArgs = ctx["args"]
	var query := (ctx["world"] as SageScriptWorld).teams().was_created(args.text(0))
	if not query.ok:
		return _unanswered(ctx, query)
	ctx["result"] = query.as_bool()
	return Dispatch.Status.OK


# --- Formerly gap-registered threat / recruit / nearest-ref -----------------


static func _set_counter_to_team_threat(ctx: Dictionary) -> int:
	# SET_COUNTER_TO_TEAM_THREAT(COUNTER, TEAM, REAL radius)
	var args: SageScriptArgs = ctx["args"]
	var query := (ctx["world"] as SageScriptWorld).teams().threat_within_radius(
		args.text(1), args.real(2)
	)
	if not query.ok:
		return _unanswered(ctx, query)
	var env: SageScriptEnv = ctx["env"]
	env.set_counter(args.text(0), int(round(float(query.value))))
	return Dispatch.Status.OK


static func _recruit_units(ctx: Dictionary) -> int:
	# TEAM_RECRUIT_UNITS(TEAM, INT, OBJECT_TYPE_LIST)
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx, "teams.recruit_units",
		(ctx["world"] as SageScriptWorld).teams().recruit_units(
			args.text(0), args.integer(1), args.text(2), ""
		)
	)


static func _recruit_units_from_team(ctx: Dictionary) -> int:
	# TEAM_RECRUIT_UNITS_FROM_TEAM(TEAM, INT, OBJECT_TYPE_LIST, TEAM)
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx, "teams.recruit_units",
		(ctx["world"] as SageScriptWorld).teams().recruit_units(
			args.text(0), args.integer(1), args.text(2), args.text(3)
		)
	)


static func _set_ref_to_nearest(ctx: Dictionary) -> int:
	# SET_REF_TO_NEREST_TEAM_OF_TYPE_OWNED_BY_PLAYER
	# (OBJECT_TYPE_LIST, PLAYER, TEAM anchor, UNIT_TYPE destination reference)
	# World signature: (reference, object_type, player, anchor_team, named_type)
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx, "teams.set_reference_to_nearest",
		(ctx["world"] as SageScriptWorld).teams().set_reference_to_nearest(
			args.text(3), args.text(0), args.text(1), args.text(2), true
		)
	)

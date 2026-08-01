extends RefCounted

## WP16-units - the AI-CRITICAL SUBSET.
##
## SCOPE, AND WHY THIS FILE IS NOT THE WHOLE PACKAGE
## =================================================
## WP16 is the largest single-object package in the catalog. This file
## implements the EIGHT members that the RETAIL AI SCRIPT LIBRARIES actually
## call, and nothing else. The remaining members are a later agent's work and
## will land in a SEPARATE file, because two files may not claim one PACKAGE
## constant (see handlers/_registry.gd). This file's PACKAGE is therefore
## suffixed - "WP16-ai-units" - and the unsuffixed catalog name, "WP16-units",
## is the one the completing package should take, along with the file name
## wp16_units.gd. That later file must also skip the 8 member names below: the
## registry keeps the FIRST claimant of every name and reports the second
## loudly, which is the intended signal, not a merge accident.
##
## The 8 were not chosen by taste. They are the exact WP16 membership of
## game/data/retail_ai_call_census.json - the committed census of the 14 AI_*
## WorldBuilder libraries referenced across the 301 decoded retail maps - and
## the two retail trees carry IDENTICAL counts for all eight (checked against
## the census; that is NOT a corpus-wide fact - 12 census members differ
## between BFME2 and RotWK, none of them in this file). In total: 80 call
## sites per tree, 1.40% of bfme2-retail's 5728 and 1.29% of rotwk-retail's
## 6213. `libraryCount` from the same census is quoted per member below.
##
## Counts decide the order things appear in this file, highest first, so the
## most-exercised code is the code a reader meets first:
##
##     32  NAMED_OWNED_BY_PLAYER            condition   served
##     24  SET_UNIT_REFERENCE               action      served
##     16  SET_UNIT_REFERENCE_TO_REFERENCE  action      served
##      4  CREATE_OBJECT                    action      GAP-REGISTERED
##      1  GATE_OPEN                        action      served
##      1  GATE_CLOSE                       action      served
##      1  TYPE_SIGHTED                     condition   GAP-REGISTERED
##      1  PLAYER_ENABLE_UNIT_CONSTRUCTION  action      served
##
##
## WHAT IS SERVED AND WHAT IS NOT
## ==============================
## 6 members are implemented (1 condition, 5 actions). 2 are GAP-REGISTERED,
## both for the same class of reason as WP11's and WP15's gaps: the member
## carries a LOAD-BEARING ARGUMENT that the SageScriptWorld facet surface has
## no parameter for. Those are recorded as findings against the world surface,
## not papered over here - see GAP_* below, which states the exact missing
## signature for each.
##
## The rule applied throughout: an argument that changes the OUTCOME may never
## be dropped. CREATE_OBJECT names the TEAM the spawn joins, and every
## team-scoped verb thereafter finds or misses the object by that membership;
## TYPE_SIGHTED names WHOSE EYES answer the question. A refusal is
## recoverable; a spawn on the wrong roster or a sighting through the wrong
## unit's vision is not.
##
## Note that the world's OWN docstrings route both gapped members to existing
## facet methods (units.create_object, units.type_was_sighted), and
## game/data/script_world_surface.json carries neither with a `signatureGap`
## flag. The sourced signatures say otherwise, and - exactly as WP17 found on
## players.can_build_at_base - the signatures win. Both surface-file rows are
## therefore a finding of this package: the routes exist but cannot carry the
## authored arguments.
##
##
## WHERE THE UNIT-REFERENCE STORE LIVES, AND WHY IT IS THE WORLD
## =============================================================
## SET_UNIT_REFERENCE and SET_UNIT_REFERENCE_TO_REFERENCE bind names in the
## unit-reference store. Three owners were considered before this file routed
## them to the WORLD's units.set_reference:
##
##   * NOT SageScriptEnv. Env owns interpreter state (counters, flags,
##     timers) and has no reference registry - checked, not assumed. A
##     reference is not interpreter state: it RESOLVES to a simulation entity,
##     and env deliberately knows nothing about entities.
##   * NOT SageScriptExecutor. The executor owns script bodies and subroutine
##     seams; it has no name registry either.
##   * The WORLD, by design. units.set_reference(reference, object_name)
##     exists, its docstring names BOTH actions, and the object-name-registry
##     subsystem it waits on is specified as "a lockstep script-name ->
##     entity-id binding ... plus a unit-reference store"
##     (script_world_surface.json) - one registry, one namespace. The retail
##     AI depends on that unification: WP11's census work showed
##     NAMED_BASE_UNPACK_FREE binding AI_BASE as a UNIT_REF and ai_opposition
##     then reading AI_BASE where a plain UNIT is declared, so references and
##     object names must resolve through the SAME lookup or half the AI's
##     chains dangle.
##
## ONE CONTRACT THE FOLD IMPOSES, stated here because the facet docstring does
## not: the world must resolve set_reference's SECOND argument AT CALL TIME.
## SET_UNIT_REFERENCE_TO_REFERENCE copies the CURRENT binding of the source
## reference; a world that stored the source STRING would alias the
## destination to the source's future values instead. For plain object names
## the two readings agree (names are stable); for references they do not.
## Reported as an open question against the facet contract.
##
##
## THE ARGUMENT TRAP
## =================
## sage_scb.py stores integer AND real AND text for EVERY non-position
## argument, so a type-search silently succeeds on the wrong slot. Every read
## below is BY INDEX with the signature written directly above it. The traps
## specific to this file:
##
##     SET_UNIT_REFERENCE(UNIT_REF, UNIT)   destination FIRST, subject second -
##                                     the English description ("set <ref> to
##                                     reference <unit>") reads in argument
##                                     order here, but its WP15 twin taught
##                                     that hand-written handlers get this
##                                     family backwards, and at 24 call sites
##                                     a swap would bind object names AS
##                                     references
##     SET_UNIT_REFERENCE_TO_REFERENCE(UNIT_REF, UNIT_REF)   two arguments of
##                                     the SAME type, unrecoverable by any
##                                     non-positional method - swapped, every
##                                     copy runs backwards and the destination
##                                     overwrites the source
##     NAMED_OWNED_BY_PLAYER(UNIT, PLAYER)   both text-carried; a swapped read
##                                     asks who owns a player name, which a
##                                     fixture world refuses but a permissive
##                                     one might answer with a plausible false
##
## Of this file's parameter types only PLAYER has an observed wire code (11);
## UNIT, UNIT_REF, OBJECT_TYPE and TEAM are unobserved and carried as text,
## and COORD3D/ANGLE appear only in the gap-registered CREATE_OBJECT.
##
##
## CONDITIONS MAY NOT GUESS
## ========================
## NAMED_OWNED_BY_PLAYER is this package's highest-traffic member and it is a
## CONDITION (32 call sites, 3 libraries - the widest library spread in this
## file). It gates on OWNERSHIP, which in a skirmish changes hands: the
## classic shape is a base flag or gate house tested against a player to
## decide whether it is still unclaimed. "That object is not owned by this
## player" and "I have no idea who owns that object" are different answers,
## and only the first may release the AI's claim logic. The handler checks
## SageWorldQuery.ok first and returns WORLD_REFUSED otherwise, which the
## dispatcher turns into CONDITION_FALLBACK (false) *and* a structured gap.
##
## THE COMPARISON IS CONSUMED BY THE WORLD. Both text arguments pass unchanged
## to units.is_owned_by, so player tokens such as "<This Player>" resolve in
## the executing world's binding context. Keeping the comparison world-side is
## what distinguishes a real negative answer from an incomplete-namespace
## refusal without teaching this handler match-specific player identity.

const PACKAGE := "WP16-ai-units"

const Dispatch := preload("res://src/script/script_dispatch.gd")


# ==========================================================================
# GAP-REGISTERED MEMBERS
# ==========================================================================
#
# Two members whose world surface cannot carry an argument that changes the
# outcome. They are declared through `reg.blocked_actions()` /
# `reg.blocked_conditions()` rather than given a body that returns OK, for
# the same two reasons as the earlier packages' gaps: a body would count as
# coverage, and a shared refusal cannot be mistaken for an implementation
# while skimming.
#
# Each string below names the EXACT facet signature that would unblock it.
# Neither needs a facet that does not exist - the Units facet is there. They
# need parameters the facet does not have, which is a coordinated edit to
# script_world.gd and therefore not this agent's to make.

## CREATE_OBJECT(OBJECT_TYPE, TEAM, COORD3D, ANGLE) - 4 AI call sites, 2
## libraries.
const GAP_CREATE_OBJECT := (
	"team-owned spawn with facing (the sourced signature is "
	+ "CREATE_OBJECT(OBJECT_TYPE, TEAM, COORD3D, ANGLE): spawn an "
	+ "<OBJECT_TYPE> on team <TEAM> at <COORD3D> facing <ANGLE>. The world's "
	+ "units.create_object(object_type, player, position, angle) demands a "
	+ "PLAYER the action does not carry and has no slot for the TEAM it does. "
	+ "A SAGE team belongs to a player, so ownership is derivable - but the "
	+ "spawn also JOINS that team's roster, and every team-scoped verb "
	+ "(TEAM_SET_STATE, TEAM_HUNT, the guard family) thereafter finds or "
	+ "misses it by that membership, so collapsing the team to its owning "
	+ "player would strand every scripted spawn outside every team script. "
	+ "units.create_on_team_at(object_type, team, target, object_name) "
	+ "carries the team and can express the position as target_position(), "
	+ "but has no slot for the ANGLE, and dropping the authored facing would "
	+ "spawn objects facing a direction the map never chose. NEEDED: "
	+ "units.create_object(object_type: String, team: String, position: "
	+ "Vector3, angle: float) -> SageWorldQuery, keeping the returns-the-new-"
	+ "name contract. The completing package will meet the IDENTICAL team-vs-"
	+ "player mismatch on units.spawn_at, whose action "
	+ "UNIT_SPAWN_NAMED_LOCATION_ORIENTATION(UNIT, OBJECT_TYPE, TEAM, "
	+ "COORD3D, ANGLE) also authors a TEAM the facet spells player)"
)

## TYPE_SIGHTED(UNIT, OBJECT_TYPE, PLAYER) - 1 AI call site, 1 library.
const GAP_TYPE_SIGHTED := (
	"per-unit sighting (the sourced signature is TYPE_SIGHTED(UNIT, "
	+ "OBJECT_TYPE, PLAYER): <UNIT> has sighted a(n) <OBJECT_TYPE> belonging "
	+ "to <PLAYER> - three load-bearing arguments. The world's "
	+ "units.type_was_sighted(player, object_type) has two slots, so "
	+ "whichever way they are read, either the OBSERVER unit or the OWNER "
	+ "player is dropped. The observer decides whose eyes answer the "
	+ "question - 'my scout saw a fortress' and 'anything anywhere saw a "
	+ "fortress' release different gates - and the owner decides whose "
	+ "fortress counts. NEEDED: units.type_was_sighted(observer: String, "
	+ "object_type: String, owner: String) -> SageWorldQuery)"
)

## Formerly gap-registered; now served via create_object_on_team / type_sighted_by.
const GAP_CREATE_OBJECT_ACTIONS: Array = []
const GAP_TYPE_SIGHTED_CONDITIONS: Array = []


static func register(reg: SageScriptHandlerRegistry.Registrar) -> void:
	# --- Served, in AI call-site order (census counts, equal in both trees) -
	reg.condition("NAMED_OWNED_BY_PLAYER", _condition_owned_by_player)  # 32
	reg.action("SET_UNIT_REFERENCE", _set_unit_reference)               # 24
	reg.action("SET_UNIT_REFERENCE_TO_REFERENCE", _set_unit_reference_to_reference)  # 16
	reg.action("GATE_OPEN", _gate_open)                                 #  1
	reg.action("GATE_CLOSE", _gate_close)                               #  1
	reg.action("PLAYER_ENABLE_UNIT_CONSTRUCTION", _enable_unit_construction)  # 1

	reg.action("CREATE_OBJECT", _create_object)                         #  4
	reg.condition("TYPE_SIGHTED", _type_sighted)                        #  1


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


static func _create_object(ctx: Dictionary) -> int:
	# CREATE_OBJECT(OBJECT_TYPE, TEAM, COORD3D, ANGLE)
	var args: SageScriptArgs = ctx["args"]
	var pos := Vector3(
		args.real(2) if args.size() > 2 else 0.0,
		0.0,
		0.0
	)
	# COORD3D may be packed as text "x,y,z" or three reals depending on decode.
	var coord_text := args.text(2)
	if coord_text.contains(","):
		var parts := coord_text.split(",")
		if parts.size() >= 2:
			pos = Vector3(float(parts[0]), float(parts[1]) if parts.size() > 2 else 0.0, float(parts[parts.size() - 1]))
	var angle := args.real(3)
	var query := (ctx["world"] as SageScriptWorld).units().create_object_on_team(
		args.text(0), args.text(1), pos, angle
	)
	if not query.ok:
		return _unanswered(ctx, query)
	ctx["result"] = query.value
	return Dispatch.Status.OK


static func _type_sighted(ctx: Dictionary) -> int:
	# TYPE_SIGHTED(UNIT observer, OBJECT_TYPE, PLAYER owner)
	var args: SageScriptArgs = ctx["args"]
	var query := (ctx["world"] as SageScriptWorld).units().type_sighted_by(
		args.text(0), args.text(1), args.text(2)
	)
	if not query.ok:
		return _unanswered(ctx, query)
	ctx["result"] = query.as_bool()
	return Dispatch.Status.OK


# --- Condition ------------------------------------------------------------


static func _condition_owned_by_player(ctx: Dictionary) -> int:
	# NAMED_OWNED_BY_PLAYER(UNIT, PLAYER)
	#   "<UNIT> is owned by <PLAYER>"
	#
	# The object comes FIRST and the player SECOND. Both are text-carried, so
	# nothing but position separates them; a swapped read would ask the world
	# who owns a player name - which a fixture-driven world answers with a
	# refusal, but a permissive one might answer with a plausible false.
	#
	# Both values pass to the world unchanged. Player placeholders are binding
	# context, not literal player names; resolving them in the world is what
	# makes all retail "<This Player>" sites answerable without teaching the
	# handler match-specific identity.
	var args: SageScriptArgs = ctx["args"]
	var query := (ctx["world"] as SageScriptWorld).units().is_owned_by(
		args.text(0), args.text(1)
	)
	if not query.ok:
		return _unanswered(ctx, query)
	ctx["result"] = query.as_bool()
	return Dispatch.Status.OK


# --- The reference store --------------------------------------------------


static func _set_unit_reference(ctx: Dictionary) -> int:
	# SET_UNIT_REFERENCE(UNIT_REF, UNIT)
	#   "Set <UNIT_REF> to reference the unit <UNIT>"
	#
	# DESTINATION FIRST. Argument 0 is the reference being bound and argument
	# 1 is the object it points at, the exact shape of WP15's
	# SET_TEAM_REFERENCE. 24 call sites across 2 libraries make this the
	# heaviest action in the package; a swapped read would not fail - it would
	# bind every object name AS a reference and point it at a reference name,
	# and every downstream reader would then resolve the wrong side.
	#
	# The store this binds into is the WORLD's, not env's - see the class
	# comment for why that was checked rather than assumed.
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx, "units.set_reference",
		(ctx["world"] as SageScriptWorld).units().set_reference(args.text(0), args.text(1))
	)


static func _set_unit_reference_to_reference(ctx: Dictionary) -> int:
	# SET_UNIT_REFERENCE_TO_REFERENCE(UNIT_REF, UNIT_REF)
	#   "Set <UNIT_REF> to the unit referenced by <UNIT_REF>"
	#
	# TWO REFERENCES, AND THE ORDER DECIDES WHICH ONE IS OVERWRITTEN.
	# Argument 0 is the DESTINATION and argument 1 the SOURCE - the same
	# destination-first shape as the plain form above, and with both
	# arguments the same type nothing but position could ever tell them
	# apart. Swapped, every copy runs backwards: the source is overwritten
	# with the destination's stale binding, at all 16 call sites of the one
	# library that authors this.
	#
	# ONE WORLD METHOD SERVES BOTH SPELLINGS, and that is the facet's design,
	# not this file's shortcut: units.set_reference's docstring names both
	# actions, and the object-name registry it waits on holds object names
	# and references in ONE namespace (class comment). The world resolves
	# argument 1 through that registry; here it happens to be a reference.
	# The one obligation the fold leaves the world - resolve the source AT
	# CALL TIME, not by storing the string - is documented in the class
	# comment and reported as an open question against the facet contract.
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx, "units.set_reference",
		(ctx["world"] as SageScriptWorld).units().set_reference(args.text(0), args.text(1))
	)


# --- Gates ----------------------------------------------------------------


static func _gate_open(ctx: Dictionary) -> int:
	# GATE_OPEN(UNIT) - "Open gate <UNIT>."
	#
	# FOLDED FLAGS. The world collapses GATE_OPEN / GATE_CLOSE / GATE_READY
	# into units.set_gate_state(object_name, open, ready). This is the OPEN
	# spelling: open=TRUE. The `ready` flag exists for the GATE_READY
	# spelling, which is a WP16 member but not an AI-used one, so the
	# completing package registers it - not this file; both spellings here
	# pass ready=FALSE, the plain open/close order the map authored.
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx, "units.set_gate_state",
		(ctx["world"] as SageScriptWorld).units().set_gate_state(args.text(0), true, false)
	)


static func _gate_close(ctx: Dictionary) -> int:
	# GATE_CLOSE(UNIT) - "Close gate <UNIT>."
	#
	# The CLOSE spelling of the fold above: open=FALSE, ready=FALSE. Getting
	# the flag backwards would swing the AI's gate the wrong way while still
	# returning OK, which is why the runner pins each spelling's exact flag
	# values separately.
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx, "units.set_gate_state",
		(ctx["world"] as SageScriptWorld).units().set_gate_state(args.text(0), false, false)
	)


# --- Production -----------------------------------------------------------


static func _enable_unit_construction(ctx: Dictionary) -> int:
	# PLAYER_ENABLE_UNIT_CONSTRUCTION(PLAYER)
	#   "Allow <PLAYER> to build units."
	#
	# FOLDED FLAG, PLUS A WORLD PARAMETER NO SPELLING CAN FILL. The world
	# collapses the ENABLE/DISABLE pair into
	# players.set_unit_construction_enabled(player, object_type, enabled).
	# This is the ENABLE spelling, so the flag is TRUE;
	# PLAYER_DISABLE_UNIT_CONSTRUCTION is a WP16 member but not an AI-used
	# one, so the completing package registers it with FALSE - not this file.
	#
	# THE EMPTY OBJECT_TYPE IS NOT A DROPPED ARGUMENT. Neither the ENABLE nor
	# the DISABLE spelling carries an OBJECT_TYPE (both sourced signatures
	# are a bare PLAYER), so the facet's middle parameter has no possible
	# source in either action. The handler passes "" - which this surface
	# already defines as the every-type form on its own Players facet
	# (building_count, can_build_at_base) - because inventing a type here
	# would ADD an argument the map never authored. Reported as an open
	# question against the facet: its object_type parameter has no source in
	# either spelling, the same shape as WP11's finding on
	# ai.create_reinforcement_team's player.
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx, "players.set_unit_construction_enabled",
		(ctx["world"] as SageScriptWorld).players().set_unit_construction_enabled(
			args.text(0), "", true
		)
	)

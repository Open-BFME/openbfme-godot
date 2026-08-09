extends RefCounted

## WP09 AI CORE - the seven experience/upgrade/spellbook members the retail AI
## script libraries actually call.
##
## THIS IS A DELIBERATE SLICE OF WP09, NOT THE PACKAGE
## ===================================================
## WP09-experience-upgrades-spellbook has 38 members. A decode of the 14
## referenced retail AI_* script libraries (1021 scripts, 5728 action+condition
## call sites) shows the AI uses exactly SEVEN of them - and those seven are
## 1004 call sites, 17.5% of everything the retail AI does. One pair dominates:
##
##     PLAYER_PURCHASE_SCIENCE      461 call sites   8.05% of all AI calls
##     PLAYER_CAN_PURCHASE_SCIENCE  461 call sites   8.05% of all AI calls
##
## Both come from a single library, ai_spell_execution, and they are always
## paired: the condition guards the action. Together they ARE the spellbook AI.
## Nothing else in the vocabulary comes close, so they are implemented first and
## are the members this file is most careful about.
##
## The remaining 31 WP09 members (experience grants, level setting, the rest of
## the upgrade family, PALANTIR_EVENT, ...) are used by MAP scripts, not by the
## AI, and are owned by a later agent shipping a SEPARATE file. That is why this
## file's PACKAGE name is suffixed: two files may not claim one package name, and
## the registry keeps the FIRST claimant of every member name. A later WP09 file
## must therefore skip the seven names listed in AI_USED_MEMBERS below - it will
## get a loud duplicate-registration error otherwise, which is the intended
## signal, not a merge accident.
##
##
## WHAT IS SERVED AND WHAT IS NOT
## ==============================
## FOUR are served (the science pair, the AI upgrade order, and the veteran
## object predicate). All four map
## one-to-one onto a Progression facet method that already exists with the exact
## parameters the signature carries.
##
## TWO - the threat condition pair - are served by a LATER package,
## wp21_threat_queries.gd, since the owner ruling of 2026-08-08 (see
## SERVED_BY_WP21_CONDITIONS below). This file must NOT register or block them:
## the registry keeps the FIRST claimant of every member name, so a leftover
## declaration here would shadow wp21's handlers.
##
## ONE is gap-registered, because the world surface designed up front cannot
## express its signature: the world method exists but is UNDER-PARAMETERISED -
## it drops an argument that decides the outcome. Calling it anyway would
## produce a handler that returns OK while answering a different question,
## which is the one failure mode this subsystem is built to prevent. It is
## registered as blocked (never counted as coverage, gap reason
## `blocked-subsystem`, full identity in the gap log) rather than left
## unregistered, so a map that uses it says WHY it could not be served instead
## of producing an `unimplemented` gap indistinguishable from "nobody got to it".
## The missing surface is named exactly in the subsystem string below, and is
## reported to the world's owner - this file does not add world methods.
##
##
## THE ARGUMENT TRAP
## =================
## sage_scb.py stores integer AND real AND text for every non-position argument,
## with no type-code table for most parameter types. Searching the argument list
## for "the science-looking one" therefore finds a plausible value in the wrong
## slot instead of failing. Every read below is by INDEX with the signature
## written directly above it, and `_arity_ok()` refuses a record whose argument
## count disagrees with the declared signature before any of it is read.
##
## The two science signatures are (PLAYER, SCIENCE) and AI_PLAYER_BUILD_UPGRADE
## is (PLAYER, UPGRADE): both slots are strings in both, so a type-search would
## be a coin flip 922 times per AI game.

const PACKAGE := "WP09-experience-upgrades-spellbook-ai-core"

const Dispatch := preload("res://src/script/script_dispatch.gd")
const ParamTypes := preload("res://src/script/script_param_types.gd")

## The exact WP09 members the retail AI libraries call, with their call-site
## counts from the decode. Present so the runner can assert that this file
## serves or gap-registers every one of them and nothing else - a census that
## disagrees with the evidence should fail a test, not be discovered later.
const AI_USED_MEMBERS := [
	"AI_PLAYER_BUILD_UPGRADE",        # action,     23 sites, ai_upgrade_execution
	"PLAYER_CAN_PURCHASE_SCIENCE",    # condition, 461 sites, ai_spell_execution
	"PLAYER_HAS_OBJECT_OF_VETERANCY", # condition,  21 sites, ai_desires + upgrades
	"PLAYER_PURCHASE_SCIENCE",        # action,    461 sites, ai_spell_execution
	"TEAM_THREAT_LEVEL",              # condition,   8 sites, 3 libraries
	"UNIT_THREAT_LEVEL",              # condition,  17 sites, 3 libraries
	"UPGRADE_NEAREST_WALL",           # action,     13 sites, ai_upgrade_execution
]

## Served members, by kind.
const SERVED_ACTIONS := [
	"AI_PLAYER_BUILD_UPGRADE",
	"PLAYER_PURCHASE_SCIENCE",
]
const SERVED_CONDITIONS := [
	"PLAYER_CAN_PURCHASE_SCIENCE",
	"PLAYER_HAS_OBJECT_OF_VETERANCY",
]


# --- Gap-registered members ------------------------------------------------
#
# THE THREAT PAIR IS A CONDITION PAIR, NOT AN ACTION PAIR.
#
# UNIT_THREAT_LEVEL and TEAM_THREAT_LEVEL appear in BOTH sourced tables: as
# actions (games BFME2ROTWK) and as conditions (games CNC3KW). The catalog's
# WP09 membership lists them unsuffixed, i.e. as actions, and script_world.gd
# accordingly grew `teams.set_threat_level(team, level)` - a COMMAND - citing
# "WP09 TEAM_THREAT_LEVEL".
#
# The evidence says otherwise, and the evidence wins:
#   * The reference's own semantics for both is a question, not an order:
#     "<UNIT> has <COMPARISON> threat level <REAL> within radius <REAL>", with a
#     WorldBuilder menu path of "Unit_/ Unit has (comparison) threat level
#     within radius."
#   * All 25 retail AI call sites are CONDITION call sites, with arguments
#     shaped exactly like that signature (a comparison enum, a threat, a radius).
#
# So they are conditions only. `teams.set_threat_level` is
# reported to the world owner as mis-attributed; nothing in this file calls it,
# because setting a team's threat level is not what this member does.

## Owner ruling 2026-08-08: threat pair unblocked on the slice combat-weight
## formula (authoritative for scripts; not a reverse-sourced SAGE formula —
## retail-oracle follow-up filed). Served by wp21_threat_queries.gd.
## Wall-upgrade names remain blocked pending sim surface.
##
## These two names are censused here (they ARE AI-used WP09 members) but are
## REGISTERED by wp21_threat_queries.gd - this file must not claim them, or
## first-registration-wins would shadow wp21's handlers.
const SERVED_BY_WP21_CONDITIONS := [
	"TEAM_THREAT_LEVEL",
	"UNIT_THREAT_LEVEL",
]
const BLOCKED_ACTIONS := [
	"UPGRADE_NEAREST_WALL",
]

## UPGRADE_NEAREST_WALL(UNIT, UPGRADE, OBJECT_TYPE, OBJECT_TYPE, UNIT_REF)
## "For Base <UNIT> give upgrade <UPGRADE> to object type <OBJECT_TYPE> nearest
## marker <OBJECT_TYPE> and reference as <UNIT_REF>". Retail AI example:
## AI_BASE | Upgrade_TrebuchetTurret | GondorCastleUpgrade | CastleFront |
## AI_TREBUCHET.
const WALL_UPGRADE_SUBSYSTEM := (
	"targeted wall upgrade with reference binding (the world exposes "
	+ "progression.upgrade_nearest_wall(player, upgrade, origin) - three "
	+ "arguments for a five-argument action, and its first argument is a "
	+ "PLAYER where the script passes a base object. It cannot express which "
	+ "object type is being upgraded, nor which marker the search is nearest "
	+ "to, nor the UNIT_REF the upgraded object must be bound to. That binding "
	+ "is not cosmetic: ai_upgrade_execution issues later orders to the "
	+ "reference it names here, so an upgrade performed without it leaves those "
	+ "orders pointing at nothing. The needed surface is a command taking "
	+ "(base, upgrade, object_type, marker_type, reference))"
)


static func register(reg: SageScriptHandlerRegistry.Registrar) -> void:
	# The pair that is 16.1% of everything the retail AI does.
	reg.action("PLAYER_PURCHASE_SCIENCE", _player_purchase_science)
	reg.condition("PLAYER_CAN_PURCHASE_SCIENCE", _player_can_purchase_science)
	reg.condition("PLAYER_HAS_OBJECT_OF_VETERANCY", _player_has_object_of_veterancy)

	reg.action("AI_PLAYER_BUILD_UPGRADE", _ai_player_build_upgrade)

	# TEAM_THREAT_LEVEL / UNIT_THREAT_LEVEL: deliberately absent - registered
	# by wp21_threat_queries.gd (see SERVED_BY_WP21_CONDITIONS).
	reg.blocked_actions(BLOCKED_ACTIONS, WALL_UPGRADE_SUBSYSTEM)


# --- Argument reading ------------------------------------------------------


static func _arity_ok(ctx: Dictionary, expected: int) -> bool:
	## Defence in depth. SageScriptDispatch validates arity against the declared
	## signature before a handler runs, EXCEPT for entries the source reference
	## itself marks uncertain, which are arity-exempt (script_args.gd
	## `validate()`). None of this file's three signatures is uncertain today, so
	## this never fires - but "never fires today" is not a reason to read
	## args.text(1) out of a one-argument record if that ever changes.
	var args: SageScriptArgs = ctx["args"]
	if args.size() == expected:
		return true
	ctx["detail"] = (
		"expected %d argument(s) for %s, decoded record carries %d; refusing to "
		+ "read a slot the record does not have"
	) % [expected, String(ctx["name"]), args.size()]
	return false


# --- The science pair ------------------------------------------------------
#
# PURCHASE IS NOT GRANT. This is the decision that matters most in this file.
#
# WP09 carries three ways to give a player a science and they are NOT
# interchangeable:
#
#     PLAYER_PURCHASE_SCIENCE       spends the player's science purchase points
#                                   and requires the prerequisites
#     PLAYER_GRANT_SCIENCE          gives it outright, free, no prerequisites
#     PLAYER_SCIENCE_AVAILABILITY   changes whether it may be bought at all
#
# Only the first is AI-used, 461 times. Serving it with `grant_science` because
# grant is the simpler call would hand the AI its entire spellbook for free on
# turn one while every test still passed. The handler below calls
# `purchase_science` and nothing else; the runner asserts that grant_science is
# never reached.
#
# "ATTEMPTS TO PURCHASE" IS THE AUTHORED SEMANTIC, AND IT IS AN ATTEMPT.
#
# The reference spells the action "<PLAYER> attempts to purchase Science
# <SCIENCE>", filed under "Player attempts to purchase a Science". A player who
# cannot afford it does not buy it, and that is a normal outcome of a successful
# script step, not an error - retail does not complain and the script carries on.
#
# So this handler does NOT pre-check with can_purchase_science and skip. Two
# reasons: the attempt owns its own eligibility check (duplicating it here would
# let the two disagree), and a condition evaluated from inside an action is a
# side effect in the wrong place. The AI already guards every one of its 461
# call sites with the condition; a map author is free not to, and retail's
# behaviour when they do not is "the attempt happens and buys nothing".
#
# WHAT THE WORLD'S `false` MEANS. Per the facet contract in script_world.gd, a
# command returning false means "not implemented / refused" - it does NOT mean
# "the purchase was declined". A world that models science purchases returns
# true once it has CARRIED OUT the attempt, whether or not points changed hands,
# and false only if it cannot model science purchases at all. Under that
# contract false is a world gap and is reported as one.


static func _player_purchase_science(ctx: Dictionary) -> int:
	# PLAYER_PURCHASE_SCIENCE(PLAYER, SCIENCE)
	# Retail AI shape: <This Player> | SCIENCE_Heal
	if not _arity_ok(ctx, 2):
		return Dispatch.Status.BAD_ARGUMENTS
	var args: SageScriptArgs = ctx["args"]
	var world: SageScriptWorld = ctx["world"]
	var player := args.text(0)
	var science := args.text(1)
	if not world.progression().purchase_science(player, science):
		ctx["detail"] = (
			"world does not implement progression.purchase_science; player '%s' "
			+ "did not attempt to purchase science '%s'"
		) % [player, science]
		return Dispatch.Status.WORLD_REFUSED
	return Dispatch.Status.OK


static func _player_can_purchase_science(ctx: Dictionary) -> int:
	# PLAYER_CAN_PURCHASE_SCIENCE(PLAYER, SCIENCE)
	# Retail AI shape: <This Player> | SCIENCE_Heal
	#
	# "Player can purchase a particular Science (has all prereqs & points)" - one
	# question, answered by the world, never reconstructed here from points and a
	# prerequisite tree this handler cannot see.
	#
	# THE MOST-CALLED CONDITION IN THE AI VOCABULARY, so the ok-check below is the
	# single most load-bearing line in this file. A refused query must NOT be read
	# as `false`: "the world cannot tell you" and "the player cannot afford it"
	# are different answers, and only the second one may quietly stop the AI from
	# casting. The refusal path returns WORLD_REFUSED, which the dispatcher
	# records as a gap with the science name attached; the condition's VALUE then
	# falls back to false because an unevaluable gate must never fire, but it does
	# so loudly and traceably rather than silently.
	#
	# Side-effect free, as every condition must be: no env write, no world
	# mutation, nothing cached between calls.
	if not _arity_ok(ctx, 2):
		return Dispatch.Status.BAD_ARGUMENTS
	var args: SageScriptArgs = ctx["args"]
	var world: SageScriptWorld = ctx["world"]
	var query := world.progression().can_purchase_science(args.text(0), args.text(1))
	if not query.ok:
		ctx["detail"] = "%s (player '%s', science '%s')" % [
			query.detail, args.text(0), args.text(1)
		]
		return Dispatch.Status.WORLD_REFUSED
	ctx["result"] = query.as_bool()
	return Dispatch.Status.OK


static func _player_has_object_of_veterancy(ctx: Dictionary) -> int:
	# PLAYER_HAS_OBJECT_OF_VETERANCY(PLAYER, OBJECT_TYPE, COMPARISON, INT)
	# The predicate is existential: one living object of the resolved type (or
	# declared type-list) whose current rank satisfies the comparison is enough.
	# All 42 retail AI sites use <This Player>, >=, and rank 2 or 3, but the
	# handler serves the complete sourced comparison enum rather than baking in
	# those observed values.
	if not _arity_ok(ctx, 4):
		return Dispatch.Status.BAD_ARGUMENTS
	var args: SageScriptArgs = ctx["args"]
	var comparison := args.integer(2)
	if comparison < ParamTypes.COMPARE_LESS or comparison > ParamTypes.COMPARE_NOT_EQUAL:
		ctx["detail"] = (
			"PLAYER_HAS_OBJECT_OF_VETERANCY was authored with comparison "
			+ "operator %d, outside the sourced COMPARISON table (0..5)"
		) % comparison
		return Dispatch.Status.BAD_ARGUMENTS
	var query := (ctx["world"] as SageScriptWorld).progression().has_object_of_veterancy(
		args.text(0), args.text(1), comparison, args.integer(3)
	)
	if not query.ok:
		ctx["detail"] = query.detail
		return Dispatch.Status.WORLD_REFUSED
	ctx["result"] = query.as_bool()
	return Dispatch.Status.OK


# --- The AI upgrade order --------------------------------------------------


static func _ai_player_build_upgrade(ctx: Dictionary) -> int:
	# AI_PLAYER_BUILD_UPGRADE(PLAYER, UPGRADE)
	# Retail AI shape: <This Player> | Upgrade_TechnologyMordorFireArrows
	#
	# "Have AI <PLAYER> build this upgrade". BUILD, not receive: the AI is told to
	# go and produce it, paying for it and waiting out its build time at whatever
	# structure produces it. `progression.build_upgrade` is documented "queue it,
	# do not grant it" for exactly this reason, and `progression.give_upgrade` -
	# which hands the upgrade over complete and free - is the wrong call even
	# though it would make the same 23 AI call sites appear to work. Substituting
	# it would make every AI faction upgrade instant and free.
	if not _arity_ok(ctx, 2):
		return Dispatch.Status.BAD_ARGUMENTS
	var args: SageScriptArgs = ctx["args"]
	var world: SageScriptWorld = ctx["world"]
	var player := args.text(0)
	var upgrade := args.text(1)
	if not world.progression().build_upgrade(player, upgrade):
		ctx["detail"] = (
			"world does not implement progression.build_upgrade; player '%s' was "
			+ "not ordered to build upgrade '%s'"
		) % [player, upgrade]
		return Dispatch.Status.WORLD_REFUSED
	return Dispatch.Status.OK


static func _upgrade_nearest_wall(ctx: Dictionary) -> int:
	if not _arity_ok(ctx, 5):
		return Dispatch.Status.BAD_ARGUMENTS
	var args: SageScriptArgs = ctx["args"]
	var world: SageScriptWorld = ctx["world"]
	if not world.progression().upgrade_nearest_wall_bound(
		args.text(0), args.text(1), args.text(2), args.text(3), args.text(4)
	):
		ctx["detail"] = "world does not implement progression.upgrade_nearest_wall_bound"
		return Dispatch.Status.WORLD_REFUSED
	return Dispatch.Status.OK

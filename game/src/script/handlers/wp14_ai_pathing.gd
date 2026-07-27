extends RefCounted

## WP14-ai-pathing - the AI-CRITICAL SUBSET of WP14-pathing-movement.
##
## SCOPE, AND WHY THIS FILE IS NOT THE WHOLE PACKAGE
## =================================================
## WP14 covers movement and pathing (the MOVE_*/FOLLOW_WAYPOINTS/CAN_PATH_TO
## families). This file implements the TWO members that the retail AI script
## libraries actually call, and nothing else. The remaining members are a later
## agent's work and will land in a SEPARATE file, because two files may not
## claim one PACKAGE constant (see handlers/_registry.gd). The catalog's
## full-package name is "WP14-pathing-movement"; this subset therefore claims
## the PACKAGE constant "WP14-ai-pathing" and the file name wp14_ai_pathing.gd,
## leaving BOTH the catalog package name and a sensible completing file name
## (wp14_pathing_movement.gd) free for the completing package.
##
## The two were not chosen by taste. They are the AI-called WP14 membership in
## game/data/retail_ai_call_census.json - the committed census of the 14 AI_*
## WorldBuilder libraries referenced across the 301 decoded retail maps - which
## is the authority for the counts below. Both retail trees carry IDENTICAL
## counts for both members (checked against the census), so no tree qualifier
## is needed:
##
##     21  TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE_OWNED_BY_PLAYER  action  served
##         (2 libraries)
##     10  TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE                  action  served
##         (1 library)
##
##
## SERVED, NOT GAP-REGISTERED - AND WHY THAT IS THE RIGHT CALL
## ===========================================================
## The measured surface map (game/data/script_world_surface.json) routes both
## members to orders.move_to and notes: "the move itself is backed; the
## NEAREST_TYPE target cannot resolve without type identity". The
## `object-type-identity` subsystem is the 2nd-biggest AI blocker at 155 call
## sites, and NO signatureGap is recorded for either member - which is the
## distinction that decides the treatment. The world surface can carry every
## load-bearing argument these actions author: SageScriptWorld.target_nearest_
## type(object_type, owner) exists precisely for the ..._NEAREST_* family, and
## orders.move_to's own docstring names both of these actions. What is missing
## is the ADAPTER's ability to resolve that target, and that refusal is the
## world's to make at runtime - it arrives as a structured WORLD_REFUSED gap
## naming orders.move_to. A blocked-registration here would misfile an adapter
## backlog item as a signature gap AND would go stale silently the day
## object-type-identity lands: these handlers start working that day with no
## edit, where a blocked pair would keep refusing traffic the world could by
## then serve.
##
##
## THE ARGUMENT TRAP
## =================
## sage_scb.py stores integer AND real AND text for EVERY non-position
## argument, so a type-search silently succeeds on the wrong slot. Every read
## below is BY INDEX with the signature written directly above it. Both
## signatures in this file are ALL-TEXT:
##
##     TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE(TEAM, OBJECT_TYPE_LIST)
##     TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE_OWNED_BY_PLAYER(TEAM,
##         OBJECT_TYPE_LIST, PLAYER)
##
## - three name arguments with not one discriminating payload field between
## them, unrecoverable by any non-positional method. A team-for-list swap
## would ask the world to move a team named after an object list toward the
## nearest object of a type named after a team; a fixture-driven world refuses
## that, but a permissive one might resolve "nothing nearby" and quietly stand
## the team down. The engine's own templates fix the order: "Team <TEAM> will
## move to the nearest object of type <OBJECT_TYPE_LIST> [that is owned by the
## player <PLAYER>]."
##
## The OBJECT_TYPE_LIST name passes through verbatim - retail authors list
## names here and the world's argument conventions put list resolution on the
## world, not the handler.
##
##
## THE EMPTY-OWNER SENTINEL
## ========================
## target_nearest_type documents `owner` empty as "any owner" - that IS the
## OF_TYPE spelling. The OWNED_BY_PLAYER spelling therefore meets a sentinel
## collision if a map authors an EMPTY player name: passing it through would
## silently rewrite the owned variant into the any-owner variant - a different
## action answered confidently, the same inversion class as WP11's 0-second
## guard. An authored empty player refuses with the surface fix named instead.
## No retail AI call site authors one (all 21 sites name a concrete player
## token), so the refusal costs no retail coverage; it exists because a mod
## may author the degenerate form.

const PACKAGE := "WP14-ai-pathing"

const Dispatch := preload("res://src/script/script_dispatch.gd")


static func register(reg: SageScriptHandlerRegistry.Registrar) -> void:
	# In AI call-site order (census counts, identical in both trees).
	reg.action(
		"TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE_OWNED_BY_PLAYER",
		_move_to_nearest_of_type_owned_by_player
	)  # 21
	reg.action("TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE", _move_to_nearest_of_type)  # 10


# --- Shared tail ----------------------------------------------------------


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


# --- Actions --------------------------------------------------------------


static func _move_to_nearest_of_type(ctx: Dictionary) -> int:
	# TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE(TEAM, OBJECT_TYPE_LIST)
	#   "Team <TEAM> will move to the nearest object of type <OBJECT_TYPE_LIST>."
	#
	# The team comes FIRST and the type list SECOND - both text, so nothing but
	# position separates them (see the class comment). The target is
	# target_nearest_type with an EMPTY owner, which is that helper's
	# documented "any owner" form and exactly what this unowned spelling asks:
	# the retail single-library user is the economy loop steering harvest
	# teams at the nearest resource object of a type, whoever owns it.
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx, "orders.move_to",
		(ctx["world"] as SageScriptWorld).orders().move_to(
			SageScriptWorld.Scope.TEAM,
			args.text(0),
			SageScriptWorld.target_nearest_type(args.text(1), "")
		)
	)


static func _move_to_nearest_of_type_owned_by_player(ctx: Dictionary) -> int:
	# TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE_OWNED_BY_PLAYER(TEAM,
	#     OBJECT_TYPE_LIST, PLAYER)
	#   "Team <TEAM> will move to the nearest object of type <OBJECT_TYPE_LIST>
	#    that is owned by the player <PLAYER>."
	#
	# Three text arguments, positional only. The OWNER is load-bearing: it is
	# the entire difference between this member and its unowned sibling, and
	# target_nearest_type reserves the empty owner string as the "any owner"
	# sentinel - so an authored empty player CANNOT be expressed and refuses
	# rather than silently becoming the other action (see the class comment).
	# A non-empty owner passes through verbatim, "<This Player>"-style tokens
	# included: the world resolves those, not this handler.
	var args: SageScriptArgs = ctx["args"]
	var owner := args.text(2)
	if owner == "":
		ctx["detail"] = (
			"target_nearest_type treats an empty owner as 'any owner', so an "
			+ "authored empty PLAYER on TEAM_MOVE_TO_NEAREST_OBJECT_OF_TYPE_"
			+ "OWNED_BY_PLAYER cannot be expressed - serving it would silently "
			+ "turn the owned spelling into the any-owner spelling; NEEDED: an "
			+ "explicit any-owner tag on the nearest-type target instead of the "
			+ "empty-string sentinel"
		)
		return Dispatch.Status.WORLD_REFUSED
	return _served(
		ctx, "orders.move_to",
		(ctx["world"] as SageScriptWorld).orders().move_to(
			SageScriptWorld.Scope.TEAM,
			args.text(0),
			SageScriptWorld.target_nearest_type(args.text(1), owner)
		)
	)

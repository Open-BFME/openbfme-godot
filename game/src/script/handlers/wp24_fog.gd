extends RefCounted

## WP24-fog - the four MAP_REVEAL/SHROUD fog actions the retail-slice fog
## surface can now serve honestly, unblocked out of WP02-blocked-fog-of-war
## (packet P6, 2026-08-08).
##
## WP02 declared all 13 fog names blocked because no simulation modelled fog
## of war at all. That is no longer true: the retail slice now carries a fog
## grid (retail_slice_parity.gd fog_reveal/fog_shroud/fog_undo_permanent/
## fog_border_shroud) and SliceFog implements SageScriptWorld.Fog against it
## (retail_slice_script_world.gd). wp02's own header names the sanctioned
## unblock path - build the subsystem, implement the facet, then convert the
## declarations into handlers - and this file is that conversion for exactly
## the names the surface supports. The other nine stay declared in wp02 with
## the surfaces they are missing; see its header for the grouping.
##
## Table rows served, verbatim from script_action_table.gd (name | params |
## category | game | confidence):
##
##   "ENABLE_BORDER_SHROUD|-|Map|SHARED|S"                       (row 212)
##   "DISABLE_BORDER_SHROUD|-|Map|SHARED|S"                      (row 185)
##   "MAP_REVEAL_AT_WAYPOINT|WAYPOINT,REAL,PLAYER|Map|SHARED|S"  (row 300)
##   "MAP_SHROUD_AT_WAYPOINT|WAYPOINT,REAL,PLAYER|Map|SHARED|S"  (row 305)
##
## World surface used:
##   areas().waypoint_position(waypoint)        - Vector3, "the primitive
##                                                under every _AT_WAYPOINT
##                                                action" (script_world.gd)
##   fog().set_border_shroud(enabled)
##   fog().reveal(player, target, permanent)
##   fog().shroud(player, target)
##
##
## THE POSITION-ONLY TARGET RULE
## =============================
## SliceFog's target reader (_fog_target_center_radius) understands POSITION
## targets - and ONLY those. A raw target_waypoint()/target_area() dictionary
## would not error: it would silently fall back to center ZERO / radius 100
## and confidently reveal around the map origin. So the waypoint is resolved
## WORLD-SIDE first, through areas.waypoint_position (the waypoint table
## lives in the world; an unknown waypoint is the world's refusal to report,
## never a position for this handler to invent), and fog receives a POSITION
## target carrying the resolved point plus the authored radius - the `radius`
## key is part of the fog target schema, read by the same target reader.
##
##
## THE ARGUMENT TRAP
## =================
## MAP_REVEAL_AT_WAYPOINT / MAP_SHROUD_AT_WAYPOINT author (WAYPOINT, REAL,
## PLAYER): the waypoint and the player are BOTH text-carried names with no
## observed wire code, so a type-search cannot tell them apart and only
## position separates them. A swapped read would resolve the player name as a
## waypoint - which would not fail loudly in a map whose player names shadow
## waypoint names. Every read below is BY INDEX with the signature written
## directly above it, and the runner pins the exact positional order against
## logged world calls with mutually distinguishable fixture names.
##
## MAP_REVEAL_AT_WAYPOINT is the NON-permanent variant: permanent=false is
## hard-coded, never derived. The _PERMANENTLY_ variants author a REVEAL_NAME
## the surface has no registry for and stay blocked in wp02.

const PACKAGE := "WP24-fog"

const Dispatch := preload("res://src/script/script_dispatch.gd")


static func register(reg: SageScriptHandlerRegistry.Registrar) -> void:
	reg.action("ENABLE_BORDER_SHROUD", _enable_border_shroud)
	reg.action("DISABLE_BORDER_SHROUD", _disable_border_shroud)
	reg.action("MAP_REVEAL_AT_WAYPOINT", _map_reveal_at_waypoint)
	reg.action("MAP_SHROUD_AT_WAYPOINT", _map_shroud_at_waypoint)


# --- Shared tails ------------------------------------------------------------


static func _served(ctx: Dictionary, method: String, accepted: bool) -> int:
	## Every fog command ends here. A facet that returns false has already told
	## the world (via Facet._refuse_command) which method refused; this turns
	## that into the status the dispatcher records as a gap, naming the same
	## method so the gap points at the missing WORLD surface rather than at the
	## action. Never OK after failing to do the thing.
	if not accepted:
		ctx["detail"] = "world does not implement %s" % method
		return Dispatch.Status.WORLD_REFUSED
	return Dispatch.Status.OK


static func _waypoint_fog_call(ctx: Dictionary, reveal: bool) -> int:
	# MAP_REVEAL_AT_WAYPOINT / MAP_SHROUD_AT_WAYPOINT (WAYPOINT, REAL, PLAYER)
	#   arg 0: WAYPOINT - the waypoint name, text payload
	#   arg 1: REAL     - the radius, real payload
	#   arg 2: PLAYER   - whose vision changes, text payload
	#
	# The waypoint resolves WORLD-SIDE; an unresolvable waypoint means NOTHING
	# is revealed or shrouded and must surface as WORLD_REFUSED with the
	# world's own detail - a reveal "somewhere" is not the reveal the map
	# authored. The fog facet then receives a POSITION target (the only kind
	# the slice fog target reader understands - see the class comment) with
	# the authored radius attached under the target schema's `radius` key.
	var args: SageScriptArgs = ctx["args"]
	var world := ctx["world"] as SageScriptWorld
	var resolved := world.areas().waypoint_position(args.text(0))   # WAYPOINT
	if not resolved.ok:
		ctx["detail"] = resolved.detail
		return Dispatch.Status.WORLD_REFUSED
	var target := SageScriptWorld.target_position(resolved.as_vector3())
	target["radius"] = args.real(1)                                 # REAL
	if reveal:
		# permanent=false: this is the non-permanent variant (class comment).
		return _served(
			ctx, "fog.reveal", world.fog().reveal(args.text(2), target, false)
		)
	return _served(ctx, "fog.shroud", world.fog().shroud(args.text(2), target))


# --- Border shroud -----------------------------------------------------------


static func _enable_border_shroud(ctx: Dictionary) -> int:
	# ENABLE_BORDER_SHROUD() - zero arguments; the polarity is the action name.
	return _served(
		ctx,
		"fog.set_border_shroud",
		(ctx["world"] as SageScriptWorld).fog().set_border_shroud(true)
	)


static func _disable_border_shroud(ctx: Dictionary) -> int:
	# DISABLE_BORDER_SHROUD() - zero arguments; the polarity is the action name.
	return _served(
		ctx,
		"fog.set_border_shroud",
		(ctx["world"] as SageScriptWorld).fog().set_border_shroud(false)
	)


# --- Waypoint reveal / shroud ------------------------------------------------


static func _map_reveal_at_waypoint(ctx: Dictionary) -> int:
	return _waypoint_fog_call(ctx, true)


static func _map_shroud_at_waypoint(ctx: Dictionary) -> int:
	return _waypoint_fog_call(ctx, false)

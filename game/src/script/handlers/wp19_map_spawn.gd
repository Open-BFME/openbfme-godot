extends RefCounted

## WP19 map-spawn - CREATE_NAMED_ON_TEAM_AT_WAYPOINT, the highest-traffic
## unregistered map-script action: the retail skirmish creature/Gollum spawn
## scripts author a NAMED spawn on a TEAM at a map WAYPOINT, and without this
## handler every one of those scripts dies as an `unimplemented` gap.
##
##
## THE ARGUMENT TRAP
## =================
## CREATE_NAMED_ON_TEAM_AT_WAYPOINT(UNIT, OBJECT_TYPE, TEAM, WAYPOINT) - all
## FOUR arguments are text-carried names with no observed wire code, so a
## type-search cannot tell any of them apart and only POSITION separates them
## (see handlers/_registry.gd). Every read below is BY INDEX with the
## signature written directly above it. A swapped read would not fail - it
## would, say, spawn an object TYPE named after the script name onto a "team"
## that is actually a waypoint, and every downstream NAMED_/TEAM_ verb would
## then miss it - which is why the runner pins the exact positional order
## against a logged world call with four mutually distinguishable names.
##
## The world method is units.create_on_team_at(object_type, team, target,
## object_name) - note the ORDER DIFFERS FROM THE ACTION'S: the action leads
## with the name (UNIT first), the facet trails with it (object_name last).
## Its docstring names this exact action, and the waypoint travels as a
## target_waypoint() dictionary so waypoint-name resolution stays WORLD-SIDE,
## where the map's waypoint table lives. A missing waypoint is therefore the
## world's refusal to report, not a position for this handler to invent.
##
##
## THE ORIENTATION VARIANT IS GAP-REGISTERED, NOT SERVED
## =====================================================
## CREATE_NAMED_ON_TEAM_AT_WAYPOINT_WITH_ORIENTATION(UNIT, OBJECT_TYPE, TEAM,
## WAYPOINT, REAL) is in the sourced table (a CNC3KW_RA3U member; no BFME2
## retail map authors it) and adds a facing ANGLE that changes the outcome.
## The facet surface cannot carry it: create_on_team_at has no angle slot,
## create_object_on_team(object_type, team, position, angle) has the angle
## but no OBJECT NAME slot (dropping the name strands every downstream
## NAMED_ verb), and spawn_at carries name+angle but spells the owner PLAYER
## rather than TEAM - the identical team-vs-player mismatch WP16 reported on
## CREATE_OBJECT. The rule applied throughout this repo: an argument that
## changes the outcome may never be dropped. So the variant is declared
## through reg.blocked_actions() - a structured BLOCKED gap naming the
## missing surface, never a body that returns OK after discarding the facing.

const PACKAGE := "WP19-map-spawn"

const Dispatch := preload("res://src/script/script_dispatch.gd")

## The exact facet signature that would unblock the orientation variant.
const GAP_WITH_ORIENTATION := (
	"named team spawn with facing (the sourced signature is "
	+ "CREATE_NAMED_ON_TEAM_AT_WAYPOINT_WITH_ORIENTATION(UNIT, OBJECT_TYPE, "
	+ "TEAM, WAYPOINT, REAL): spawn <OBJECT_TYPE> named <UNIT> on team <TEAM> "
	+ "at <WAYPOINT> facing <REAL> degrees. units.create_on_team_at(object_"
	+ "type, team, target, object_name) carries the name, team and waypoint "
	+ "but has no slot for the authored facing angle, and dropping it would "
	+ "spawn objects facing a direction the map never chose. NEEDED: an angle "
	+ "parameter on units.create_on_team_at, or a team-spelled units.spawn_at)"
)


static func register(reg: SageScriptHandlerRegistry.Registrar) -> void:
	reg.action("CREATE_NAMED_ON_TEAM_AT_WAYPOINT", _create_named_on_team_at_waypoint)
	reg.blocked_actions(
		["CREATE_NAMED_ON_TEAM_AT_WAYPOINT_WITH_ORIENTATION"], GAP_WITH_ORIENTATION
	)


static func _create_named_on_team_at_waypoint(ctx: Dictionary) -> int:
	# CREATE_NAMED_ON_TEAM_AT_WAYPOINT(UNIT, OBJECT_TYPE, TEAM, WAYPOINT)
	#   "Spawn <OBJECT_TYPE> named <UNIT> on <TEAM> at <WAYPOINT>."
	#
	# THE NAME COMES FIRST in the action and LAST in the facet call - see the
	# class comment. The waypoint travels as a target dictionary so the world
	# resolves it against the map's waypoint table; a refusal (unknown team,
	# missing waypoint, spawn failure) means NOTHING was spawned and must
	# surface as WORLD_REFUSED with the world's own detail, never as OK.
	var args: SageScriptArgs = ctx["args"]
	var query := (ctx["world"] as SageScriptWorld).units().create_on_team_at(
		args.text(1),                                    # OBJECT_TYPE
		args.text(2),                                    # TEAM
		SageScriptWorld.target_waypoint(args.text(3)),   # WAYPOINT
		args.text(0)                                     # UNIT (the new name)
	)
	if not query.ok:
		ctx["detail"] = query.detail
		return Dispatch.Status.WORLD_REFUSED
	return Dispatch.Status.OK

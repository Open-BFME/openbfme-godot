extends RefCounted

## WP02-blocked-fog-of-war - the shroud/reveal family, BLOCKED.
##
## Neither simulation in this repository models fog of war at all. There is no
## shroud to lift and no vision state to make permanent, so these 13 actions
## cannot be implemented - not "have not been", cannot be.
##
## They are declared here anyway, and that declaration is the entire point of
## the file:
##
##   * A mission that shrouds the map now fails at the exact action name with a
##     `blocked-subsystem` gap saying which subsystem is missing. Without this
##     it would produce an `unimplemented` gap indistinguishable from ordinary
##     backlog - or, before the dispatcher existed, nothing at all, and the
##     mission would simply run with full vision and look fine.
##   * The coverage denominator counts them honestly: known, named, and waiting
##     on a subsystem decision rather than on somebody's time.
##
## There are no handler functions in this file. One shared refusal handler in
## handlers/_registry.gd serves every blocked name, precisely so that nothing
## here can be mistaken for an implementation. The world surface these will
## eventually need already exists as SageScriptWorld.Fog, refusing.
##
## UNBLOCKING: build the fog-of-war subsystem first, implement
## SageScriptWorld.Fog against it, then convert these declarations into
## handlers. That is a content/parity decision for the owner, not a scripting
## one.

const PACKAGE := "WP02-blocked-fog-of-war"

const SUBSYSTEM := "fog of war (shroud, vision state, permanent reveal)"

const BLOCKED_ACTIONS := [
	"DISABLE_BORDER_SHROUD",
	"ENABLE_BORDER_SHROUD",
	"MAP_REVEAL_ALL",
	"MAP_REVEAL_ALL_PERM",
	"MAP_REVEAL_ALL_UNDO_PERM",
	"MAP_REVEAL_AT_WAYPOINT",
	"MAP_REVEAL_IN_TRIGGER",
	"MAP_REVEAL_PERMANENTLY_AT_WAYPOINT",
	"MAP_REVEAL_PERMANENTLY_IN_TRIGGER",
	"MAP_SHROUD_ALL",
	"MAP_SHROUD_AT_WAYPOINT",
	"MAP_UNDO_REVEAL_PERMANENTLY_AT_WAYPOINT",
	"MAP_UNDO_REVEAL_PERMANENTLY_IN_TRIGGER",
]


static func register(reg: SageScriptHandlerRegistry.Registrar) -> void:
	reg.blocked_actions(BLOCKED_ACTIONS, SUBSYSTEM)

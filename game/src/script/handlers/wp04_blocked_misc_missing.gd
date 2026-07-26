extends RefCounted

## WP04-blocked-misc-missing - allegiance/capture, teleport and weapon-mode
## toggle, BLOCKED.
##
## Three unrelated small subsystems, none of which exists: units cannot change
## allegiance by capture, there is no teleport path that bypasses movement, and
## no weapon-mode toggle state to query. Grouped only because each is too small
## to be its own package.
##
## See wp02_blocked_fog_of_war.gd for why blocked packages are declared. No
## handler functions here; the shared refusal handler serves them.

const PACKAGE := "WP04-blocked-misc-missing"

const SUBSYSTEM := "unit capture/allegiance, teleport and weapon-mode toggle"

const BLOCKED_ACTIONS := [
	"PLAYER_CREATE_TEAM_FROM_CAPTURED_UNITS",
	"TEAM_CAPTURE_NEAREST_UNOWNED_FACTION_UNIT",
	"TEAM_TELEPORT_TO_WAYPOINT",
	"UNIT_TELEPORT_TO_WAYPOINT",
]

const BLOCKED_CONDITIONS := [
	"SKIRMISH_PLAYER_HAS_COMPARISON_CAPTURED_UNITS",
	"UNIT_HAS_TOGGLED_WEAPON",
]


static func register(reg: SageScriptHandlerRegistry.Registrar) -> void:
	reg.blocked_actions(BLOCKED_ACTIONS, SUBSYSTEM)
	reg.blocked_conditions(BLOCKED_CONDITIONS, SUBSYSTEM)

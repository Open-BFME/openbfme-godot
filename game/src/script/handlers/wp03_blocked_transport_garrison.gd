extends RefCounted

## WP03-blocked-transport-garrison - garrison, transport and passengers,
## BLOCKED.
##
## The simulation has no passenger-transport or garrison model: nothing can be
## inside anything else, so "load the transports" and "how many passengers does
## this unit have" have no answer to give. See wp02_blocked_fog_of_war.gd for
## why a blocked package is declared rather than skipped; the reasoning is
## identical.
##
## No handler functions here on purpose - one shared refusal handler serves
## every blocked name. The eventual world surface already exists as
## SageScriptWorld.Transport, refusing.

const PACKAGE := "WP03-blocked-transport-garrison"

const SUBSYSTEM := "passenger transport and building garrison (containment)"

const BLOCKED_ACTIONS := [
	"NAMED_GARRISON_NEAREST_BUILDING",
	"NAMED_GARRISON_SPECIFIC_BUILDING",
	"NAMED_GARRISON_SPECIFIC_BUILDING_INSTANTLY",
	"NAMED_USE_COMMANDBUTTON_ON_NEAREST_GARRISONED_BUILDING",
	"PLAYER_GARRISON_ALL_BUILDINGS",
	"TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_GARRISONED_BUILDING",
	"TEAM_GARRISON_NEAREST_BUILDING",
	"TEAM_GARRISON_SPECIFIC_BUILDING",
	"TEAM_GARRISON_SPECIFIC_BUILDING_INSTANTLY",
	"TEAM_GARRISON_TEAM_INSTANTLY",
	"TEAM_LOAD_TRANSPORTS",
]

const BLOCKED_CONDITIONS := [
	"SKIRMISH_PLAYER_HAS_COMPARISON_GARRISONED",
	"UNIT_HAS_PASSENGER",
]


static func register(reg: SageScriptHandlerRegistry.Registrar) -> void:
	reg.blocked_actions(BLOCKED_ACTIONS, SUBSYSTEM)
	reg.blocked_conditions(BLOCKED_CONDITIONS, SUBSYSTEM)

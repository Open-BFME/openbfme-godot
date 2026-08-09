extends RefCounted

## WP02-blocked-fog-of-war - the shroud/reveal names the fog surface cannot
## carry yet, BLOCKED.
##
## HISTORY. This file originally declared all 13 fog actions blocked because
## neither simulation modelled fog of war at all. Its own unblock path -
## build the subsystem, implement SageScriptWorld.Fog against it, then
## convert the declarations into handlers - has since been walked (packet P6,
## 2026-08-08): the retail slice carries a fog grid (retail_slice_parity.gd
## fog_*) and SliceFog implements the facet, so the four names that surface
## supports moved to wp24_fog.gd as real handlers:
##
##   ENABLE_BORDER_SHROUD, DISABLE_BORDER_SHROUD,
##   MAP_REVEAL_AT_WAYPOINT, MAP_SHROUD_AT_WAYPOINT
##
## The NINE names below stay blocked, in three missing-surface groups:
##
##   * WHOLE-MAP EXTENT (MAP_REVEAL_ALL, MAP_REVEAL_ALL_PERM,
##     MAP_REVEAL_ALL_UNDO_PERM, MAP_SHROUD_ALL): the fog surface carries
##     only a center+radius target; there is no whole-map target form and no
##     map-extents query, so a handler would have to invent the map's extent.
##   * TRIGGER-AREA GEOMETRY (MAP_REVEAL_IN_TRIGGER): no areas-facet query
##     exposes an area's center/radius, and the slice fog target reader
##     ignores AREA targets - a raw one would silently reveal around the
##     ORIGIN, which is worse than refusing.
##   * NAMED PERMANENT-REVEAL REGISTRY (MAP_REVEAL_PERMANENTLY_AT_WAYPOINT,
##     MAP_REVEAL_PERMANENTLY_IN_TRIGGER, and the undo pair): the undo
##     actions author ONLY a REVEAL_NAME, the facet's undo_permanent_reveal
##     spells (player, target) with no name slot, and no name->region
##     registry exists. Serving the reveal pair while dropping REVEAL_NAME
##     would strand every later undo - an argument that changes the outcome
##     may never be dropped (the same verdict as WP19's orientation variant).
##
## Why declare them at all: a mission that uses one fails at the exact action
## name with a `blocked-subsystem` gap naming what is missing, instead of an
## `unimplemented` gap indistinguishable from ordinary backlog - and the
## coverage denominator counts them honestly. There are no handler functions
## in this file; one shared refusal handler in handlers/_registry.gd serves
## every blocked name, precisely so that nothing here can be mistaken for an
## implementation.
##
## UNBLOCKING the rest: give the fog surface the missing form (a whole-map
## target, an area-geometry read, a named permanent-reveal registry), then
## convert the group into handlers in wp24_fog.gd. That is a content/parity
## decision for the owner, not a scripting one.

const PACKAGE := "WP02-blocked-fog-of-war"

const SUBSYSTEM := (
	"fog of war script surface "
	+ "(whole-map extent, trigger-area geometry, named permanent-reveal registry)"
)

const BLOCKED_ACTIONS := [
	"MAP_REVEAL_ALL",
	"MAP_REVEAL_ALL_PERM",
	"MAP_REVEAL_ALL_UNDO_PERM",
	"MAP_REVEAL_IN_TRIGGER",
	"MAP_REVEAL_PERMANENTLY_AT_WAYPOINT",
	"MAP_REVEAL_PERMANENTLY_IN_TRIGGER",
	"MAP_SHROUD_ALL",
	"MAP_UNDO_REVEAL_PERMANENTLY_AT_WAYPOINT",
	"MAP_UNDO_REVEAL_PERMANENTLY_IN_TRIGGER",
]


static func register(reg: SageScriptHandlerRegistry.Registrar) -> void:
	reg.blocked_actions(BLOCKED_ACTIONS, SUBSYSTEM)

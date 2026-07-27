extends RefCounted

## WP11-basebuilding-ai-tail - the LAST AI-called WP11 member, gap-registered.
##
## SCOPE, AND WHY THIS FILE EXISTS AT ALL
## ======================================
## wp11_ai_basebuilding.gd ("WP11-basebuilding-ai") implements eight of the
## nine AI-called WP11 members and explicitly left the ninth to a later agent:
## BUILD_BASE_BUILDING, 2 call sites in 2 libraries per tree (identical counts
## in both trees, per game/data/retail_ai_call_census.json). This file is that
## ninth member and NOTHING else - one action, gap-registered - because two
## files may not claim one PACKAGE constant and the first subset's file may not
## be edited while ~17 packages land concurrently.
##
## Names: this file claims the PACKAGE constant "WP11-basebuilding-ai-tail" and
## the file name wp11_ai_basebuilding_tail.gd. The catalog's full-package name,
## "WP11-ai-basebuilding", and the completing file name wp11_basebuilding.gd
## both REMAIN FREE for whoever implements the ~63 non-AI-called WP11 members.
## That completing package must skip the nine names registered by this file and
## its sibling: the registry keeps the FIRST claimant and reports the second
## loudly, which is the intended signal.
##
##
## WHY THIS MEMBER IS GAP-REGISTERED, NOT SERVED
## =============================================
## The previous WP11 agent predicted it ("its signature (OBJECT_TYPE, UNIT,
## UNIT_REF) shares the construction-site and result-reference arguments that
## gap-register BUILD_BASE_BUILDING_PER_TACTICAL_MARKER, so it ... cannot be
## served today either") and the prediction CONFIRMS. The engine's own
## WorldBuilder template reads:
##
##     "Build building <OBJECT_TYPE> in base <UNIT> and reference it as
##      <UNIT_REF>."
##     (uiName: "AI build base building in first available slot in a
##      referenced base.")
##
## So the UNIT is the BASE OBJECT the building goes into - retail authors
## AI_CURRENT_CONSTRUCTION_SITE there, the sibling file's decode of the family
## shape - and the UNIT_REF is a DESTINATION the new building is bound to
## (AI_BARRACKS_1 / AI_FARM_LAST_BUILT / ...), which the predictive-building
## scripts later read back. The world offers only
##
##     ai.build_base_building(player, building_type, slot, marker)
##
## which matches the building type alone. It demands a PLAYER the action does
## not carry, and has no parameter for the base object or for the result
## reference. Both missing arguments change the outcome: the base decides
## WHERE the building lands (a skirmish AI holds several bases at once), and a
## reference that is silently never bound orphans every later reader - the
## same two failure classes the sibling file proved on the unpack pair and the
## tactical-marker build. The measured surface map agrees: its routing for
## this member records `signatureGap: true` against ai.build_base_building.
##
## It is declared through `reg.blocked_actions()` rather than given a body that
## returns OK, for the same two reasons as every gap in the sibling file: a
## body would count as coverage, and a shared refusal cannot be mistaken for an
## implementation while skimming. The string below names the EXACT facet
## signature that would unblock it. It needs no new simulation subsystem beyond
## what the sibling's gaps already need - the Ai facet exists; it needs
## parameters the facet does not have, which is a coordinated edit to
## script_world.gd and therefore not this agent's to make.

const PACKAGE := "WP11-basebuilding-ai-tail"

const Dispatch := preload("res://src/script/script_dispatch.gd")


## BUILD_BASE_BUILDING(OBJECT_TYPE, UNIT, UNIT_REF) - 2 AI call sites,
## 2 libraries.
const GAP_BUILD_BASE_BUILDING := (
	"base-anchored build with a result reference (the action reads: build a "
	+ "<OBJECT_TYPE> in the first available slot of the base object <UNIT> and "
	+ "reference the new building as <UNIT_REF> - retail authors "
	+ "(AI_CURRENT_CONSTRUCTION_SITE, AI_BARRACKS_1 / ...), a base anchor and "
	+ "then a DESTINATION reference the predictive-building scripts read back. "
	+ "The world offers only ai.build_base_building(player, building_type, "
	+ "slot, marker): it demands a player the action does not carry, and has "
	+ "no parameter for the base object or the result reference. Dropping the "
	+ "base collapses every base of a multi-base skirmish AI onto one "
	+ "unspecified build site; dropping the reference orphans every later "
	+ "reader, as with the sibling file's unpack pair. NEEDED: "
	+ "ai.build_base_building(building_type: String, base: String, "
	+ "result_reference: String) -> bool - the same site+reference half "
	+ "already recorded against BUILD_BASE_BUILDING_PER_TACTICAL_MARKER in "
	+ "wp11_ai_basebuilding.gd, minus that member's near/far side bit and "
	+ "marker type)"
)

const GAP_BUILD_ACTIONS := ["BUILD_BASE_BUILDING"]


static func register(reg: SageScriptHandlerRegistry.Registrar) -> void:
	# --- Gap-registered: the world surface cannot carry the base anchor or
	# --- the result reference ---------------------------------------------
	reg.blocked_actions(GAP_BUILD_ACTIONS, GAP_BUILD_BASE_BUILDING)  # 2

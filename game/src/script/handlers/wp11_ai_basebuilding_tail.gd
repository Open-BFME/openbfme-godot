extends RefCounted

## WP11-basebuilding-ai-tail - the LAST AI-called WP11 member, SERVED.
##
## SCOPE, AND WHY THIS FILE EXISTS AT ALL
## ======================================
## wp11_ai_basebuilding.gd ("WP11-basebuilding-ai") accounts for eight of the
## nine AI-called WP11 members and explicitly left the ninth to a later agent:
## BUILD_BASE_BUILDING, 2 call sites in 2 libraries per tree (identical counts
## in both trees, per game/data/retail_ai_call_census.json). This file is that
## ninth member and NOTHING else - one action - because two files may not
## claim one PACKAGE constant and the sibling file may not grow the name.
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
## FROM GAP TO SERVICE
## ===================
## This member landed gap-registered, because the world's
## ai.build_base_building(player, building_type, slot, marker) could not carry
## the authored arguments. The engine's own WorldBuilder template reads:
##
##     "Build building <OBJECT_TYPE> in base <UNIT> and reference it as
##      <UNIT_REF>."
##     (uiName: "AI build base building in first available slot in a
##      referenced base.")
##
## So the UNIT is the BASE OBJECT the building goes into - retail authors
## AI_CURRENT_CONSTRUCTION_SITE there - and the UNIT_REF is a DESTINATION the
## new building is bound to (AI_BARRACKS_1 / AI_FARM_LAST_BUILT / ...), which
## the predictive-building scripts later read back. Both arguments change the
## outcome: the base decides WHERE the building lands (a skirmish AI holds
## several bases at once), and a reference that is silently never bound
## orphans every later reader. The facet-signature correction this gap
## demanded has since been made - the world now offers exactly the signature
## the gap named:
##
##     ai.build_base_building(building_type: String, base: String,
##                            result_reference: String) -> bool
##
## and this handler serves the action through it, positionally, dropping
## nothing. The old signature's `player` is gone because the sourced action
## carries none (the world acts as its configured script player); its `slot`
## and `marker` belonged to the _IN_SLOT and _PER_TACTICAL_MARKER spellings,
## which carry EXTRA load-bearing arguments and stay with their own gaps
## (see the sibling's GAP_BUILD_PER_MARKER).
##
##
## THE ARGUMENT TRAP
## =================
## sage_scb.py stores integer AND real AND text for EVERY non-position
## argument, so a type-search silently succeeds on the wrong slot. All three
## of this action's arguments are text-carried (OBJECT_TYPE, UNIT and
## UNIT_REF have no observed wire code), so nothing but position separates
## the building type from the base from the reference: a rotated read would
## try to build a base named after a building type and bind the base's name
## as the reference. The read below is BY INDEX with the signature written
## directly above it.

const PACKAGE := "WP11-basebuilding-ai-tail"

const Dispatch := preload("res://src/script/script_dispatch.gd")


static func register(reg: SageScriptHandlerRegistry.Registrar) -> void:
	reg.action("BUILD_BASE_BUILDING", _build_base_building)  # 2


static func _served(ctx: Dictionary, method: String, accepted: bool) -> int:
	## A facet that returns false has already told the world (via
	## Facet._refuse_command) which method refused; this turns that into the
	## status the dispatcher records as a gap, naming the same method so the
	## gap points at the missing WORLD surface rather than at the action.
	if not accepted:
		ctx["detail"] = "world does not implement %s" % method
		return Dispatch.Status.WORLD_REFUSED
	return Dispatch.Status.OK


static func _build_base_building(ctx: Dictionary) -> int:
	# BUILD_BASE_BUILDING(OBJECT_TYPE, UNIT, UNIT_REF)
	#   "Build building <OBJECT_TYPE> in base <UNIT> and reference it as
	#    <UNIT_REF>."
	#
	# Argument 0 is the building TYPE, argument 1 the BASE object it goes
	# into, argument 2 the DESTINATION reference the new building is bound to
	# - see the class comment for why all three are load-bearing and why no
	# player is passed (the sourced action carries none; the world acts as
	# its configured script player, and inventing one here would ADD an
	# argument the map never authored).
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx, "ai.build_base_building",
		(ctx["world"] as SageScriptWorld).ai().build_base_building(
			args.text(0), args.text(1), args.text(2)
		)
	)

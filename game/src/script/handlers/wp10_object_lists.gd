extends RefCounted

## WP10-object-lists - the OBJECT_TYPE_LIST maintenance pair of
## WP10-economy-objectstatus-objectlists.
##
## SCOPE, AND WHY THIS FILE IS NOT THE WHOLE PACKAGE
## =================================================
## WP10 covers economy, object status and object lists. The AI-critical
## subset (wp10_ai_economy_status.gd) took the two members the retail AI
## libraries call; THIS file takes exactly the object-list maintenance pair,
## because the object-type-identity subsystem those lists feed now exists in
## the simulation (script_object_type_lists on RetailSliceSim) and a world
## method that serves list edits with no dispatch route would leave retail's
## list-building subroutines (lib_object_lists: 732 OBJECTLIST_ADDOBJECTTYPE
## call sites building 52 named lists) unable to reach it. The remaining WP10
## members (FREEZE_TIME, the supply-source family, ...) stay a later agent's
## work; the catalog package name "WP10-economy-objectstatus-objectlists" and
## the completing file name remain free.
##
## Neither member is in the retail AI call census (the 14 AI_* libraries
## author neither) - the traffic lives in the lib_* subroutine libraries the
## AI libraries CALL_SUBROUTINE into, which the census deliberately does not
## count. So this package moves no census coverage number; it closes the lane
## that lets OBJECT_TYPE_LIST-typed conditions resolve list names at all.
##
## THE ARGUMENT TRAP
## =================
## Both signatures are two text arguments with nothing but position between
## them:
##
##     OBJECTLIST_ADDOBJECTTYPE(OBJECT_TYPE_LIST, OBJECT_TYPE)
##     OBJECTLIST_REMOVEOBJECTTYPE(OBJECT_TYPE_LIST, OBJECT_TYPE)
##
## The LIST comes first and the TYPE second ("<OBJECT_TYPE> is added to
## <OBJECT_TYPE_LIST>", but the wire order is list-first). A swapped read
## would build a list NAMED after every object type with the intended list
## name as its single member - 732 confidently wrong entries with no error
## anywhere. Every read below is BY INDEX with the signature written above
## it, and the runner's mutation checks pin the order.
##
## ADD-vs-REMOVE IS FOLDED FROM THE OPCODE, never from an argument, so the
## two spellings cannot be confused into each other (the same discipline as
## WP11's free-vs-paid unpack fold).

const PACKAGE := "WP10-object-lists"

const Dispatch := preload("res://src/script/script_dispatch.gd")


static func register(reg: SageScriptHandlerRegistry.Registrar) -> void:
	reg.action("OBJECTLIST_ADDOBJECTTYPE", _add_object_type)
	reg.action("OBJECTLIST_REMOVEOBJECTTYPE", _remove_object_type)


static func _change(ctx: Dictionary, add: bool) -> int:
	# OBJECTLIST_ADDOBJECTTYPE / OBJECTLIST_REMOVEOBJECTTYPE
	#     (OBJECT_TYPE_LIST, OBJECT_TYPE) - list FIRST, type second.
	var args: SageScriptArgs = ctx["args"]
	if (ctx["world"] as SageScriptWorld).meta().object_list_change(
		args.text(0), args.text(1), add
	):
		return Dispatch.Status.OK
	ctx["detail"] = "world does not implement meta.object_list_change"
	return Dispatch.Status.WORLD_REFUSED


static func _add_object_type(ctx: Dictionary) -> int:
	return _change(ctx, true)


static func _remove_object_type(ctx: Dictionary) -> int:
	return _change(ctx, false)

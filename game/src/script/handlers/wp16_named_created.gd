extends RefCounted

## WP16 retail map-script name-table presence. This is separate from the
## AI-library subset because the retail AI library census does not call it.

const PACKAGE := "WP16-named-created"
const Dispatch := preload("res://src/script/script_dispatch.gd")


static func register(reg: SageScriptHandlerRegistry.Registrar) -> void:
	reg.condition("NAMED_CREATED", _named_created)


static func _named_created(ctx: Dictionary) -> int:
	# NAMED_CREATED(UNIT). Retail evaluates getUnitNamed(name) != null: current
	# name-table presence, irrespective of the object's alive/dead flag.
	var args: SageScriptArgs = ctx["args"]
	var query := (ctx["world"] as SageScriptWorld).units().was_created(args.text(0))
	if not query.ok:
		ctx["detail"] = query.detail
		return Dispatch.Status.WORLD_REFUSED
	ctx["result"] = query.as_bool()
	return Dispatch.Status.OK

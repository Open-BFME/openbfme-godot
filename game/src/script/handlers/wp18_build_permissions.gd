extends RefCounted

## Map-script construction permission control. This packet serves only the
## exact one-building spelling; ALLOW_DISALLOW_ALL_BUILDINGS remains separate.

const PACKAGE := "WP18-build-permissions"
const Dispatch := preload("res://src/script/script_dispatch.gd")


static func register(reg: SageScriptHandlerRegistry.Registrar) -> void:
	reg.action("ALLOW_DISALLOW_ONE_BUILDING", _allow_disallow_one_building)


static func _allow_disallow_one_building(ctx: Dictionary) -> int:
	# ALLOW_DISALLOW_ONE_BUILDING(PLAYER, OBJECT_TYPE, BOOLEAN)
	var args: SageScriptArgs = ctx["args"]
	var player := args.text(0)
	var object_type := args.text(1)
	var allowed := args.boolean(2)
	if object_type == "":
		ctx["detail"] = (
			"ALLOW_DISALLOW_ONE_BUILDING carries an empty OBJECT_TYPE; refusing "
			+ "to reinterpret it as the separate all-buildings opcode"
		)
		return Dispatch.Status.BAD_ARGUMENTS
	if not (ctx["world"] as SageScriptWorld).ai().set_buildings_allowed(
		player, object_type, allowed
	):
		ctx["detail"] = (
			"world does not implement ai.set_buildings_allowed for player '%s', "
			+ "object type '%s', allowed=%s"
		) % [player, object_type, str(allowed)]
		return Dispatch.Status.WORLD_REFUSED
	return Dispatch.Status.OK

extends RefCounted

## WP17's retail map-script command-point override. This is separate from the
## AI-library subset because the retail AI census does not call this action.

const PACKAGE := "WP17-player-command-points"
const Dispatch := preload("res://src/script/script_dispatch.gd")


static func register(reg: SageScriptHandlerRegistry.Registrar) -> void:
	reg.action("OVERRIDE_PLAYER_COMMAND_POINTS", _override_command_points)


static func _override_command_points(ctx: Dictionary) -> int:
	# OVERRIDE_PLAYER_COMMAND_POINTS(PLAYER, INT total, INT maximum).
	# Both adjacent integer slots are load-bearing and remain independent.
	var args: SageScriptArgs = ctx["args"]
	var total := args.integer(1)
	var maximum := args.integer(2)
	if total < 0 or maximum < 0 or total > maximum:
		ctx["detail"] = (
			"OVERRIDE_PLAYER_COMMAND_POINTS requires 0 <= total <= maximum; "
			+ "received total=%d maximum=%d"
		) % [total, maximum]
		return Dispatch.Status.BAD_ARGUMENTS
	if not (ctx["world"] as SageScriptWorld).players().override_command_points(
		args.text(0), total, maximum
	):
		ctx["detail"] = "world does not implement players.override_command_points"
		return Dispatch.Status.WORLD_REFUSED
	return Dispatch.Status.OK

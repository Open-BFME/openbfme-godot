extends RefCounted

## WP03 transport/garrison. L4 made containment executable, so all eleven
## actions and both count conditions are served here.

const PACKAGE := "WP03-blocked-transport-garrison"

const Dispatch := preload("res://src/script/script_dispatch.gd")

const BLOCKED_ACTIONS := []
const BLOCKED_CONDITIONS := []


static func register(reg: SageScriptHandlerRegistry.Registrar) -> void:
	reg.action("NAMED_GARRISON_NEAREST_BUILDING", _named_nearest)
	reg.action("NAMED_GARRISON_SPECIFIC_BUILDING", _named_specific)
	reg.action("NAMED_GARRISON_SPECIFIC_BUILDING_INSTANTLY", _named_specific_instant)
	reg.action("NAMED_USE_COMMANDBUTTON_ON_NEAREST_GARRISONED_BUILDING", _named_use_button)
	reg.action("PLAYER_GARRISON_ALL_BUILDINGS", _player_all)
	reg.action("TEAM_GARRISON_NEAREST_BUILDING", _team_nearest)
	reg.action("TEAM_GARRISON_SPECIFIC_BUILDING", _team_specific)
	reg.action("TEAM_GARRISON_SPECIFIC_BUILDING_INSTANTLY", _team_specific_instant)
	reg.action("TEAM_GARRISON_TEAM_INSTANTLY", _team_team_instant)
	reg.action("TEAM_LOAD_TRANSPORTS", _team_load)
	reg.action("TEAM_ALL_USE_COMMANDBUTTON_ON_NEAREST_GARRISONED_BUILDING", _team_use_button)
	reg.condition("SKIRMISH_PLAYER_HAS_COMPARISON_GARRISONED", _player_garrisoned)
	reg.condition("UNIT_HAS_PASSENGER", _unit_has_passenger)


static func _served(ctx: Dictionary, method: String, accepted: bool) -> int:
	if not accepted:
		ctx["detail"] = "world does not implement %s" % method
		return Dispatch.Status.WORLD_REFUSED
	return Dispatch.Status.OK


static func _garrison(ctx: Dictionary, scope: int, name: String, target: Dictionary, instantly: bool) -> int:
	return _served(ctx, "transport.garrison", (ctx["world"] as SageScriptWorld).transport().garrison(scope, name, target, instantly))


static func _named_nearest(ctx: Dictionary) -> int:
	var args: SageScriptArgs = ctx["args"]
	return _garrison(ctx, SageScriptWorld.Scope.UNIT, args.text(0), SageScriptWorld.target_nearest_garrisoned(), false)


static func _named_specific(ctx: Dictionary) -> int:
	var args: SageScriptArgs = ctx["args"]
	return _garrison(ctx, SageScriptWorld.Scope.UNIT, args.text(0), SageScriptWorld.target_object(args.text(1)), false)


static func _named_specific_instant(ctx: Dictionary) -> int:
	var args: SageScriptArgs = ctx["args"]
	return _garrison(ctx, SageScriptWorld.Scope.UNIT, args.text(0), SageScriptWorld.target_object(args.text(1)), true)


static func _named_use_button(ctx: Dictionary) -> int:
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx,
		"orders.use_command_button",
		(ctx["world"] as SageScriptWorld).orders().use_command_button(
			SageScriptWorld.Scope.UNIT,
			args.text(0),
			args.text(1),
			SageScriptWorld.target_nearest_garrisoned()
		)
	)


static func _player_all(ctx: Dictionary) -> int:
	var args: SageScriptArgs = ctx["args"]
	return _garrison(ctx, SageScriptWorld.Scope.PLAYER, args.text(0), SageScriptWorld.target_nearest_garrisoned(), false)


static func _team_nearest(ctx: Dictionary) -> int:
	var args: SageScriptArgs = ctx["args"]
	return _garrison(ctx, SageScriptWorld.Scope.TEAM, args.text(0), SageScriptWorld.target_nearest_garrisoned(), false)


static func _team_specific(ctx: Dictionary) -> int:
	var args: SageScriptArgs = ctx["args"]
	return _garrison(ctx, SageScriptWorld.Scope.TEAM, args.text(0), SageScriptWorld.target_object(args.text(1)), false)


static func _team_specific_instant(ctx: Dictionary) -> int:
	var args: SageScriptArgs = ctx["args"]
	return _garrison(ctx, SageScriptWorld.Scope.TEAM, args.text(0), SageScriptWorld.target_object(args.text(1)), true)


static func _team_team_instant(ctx: Dictionary) -> int:
	var args: SageScriptArgs = ctx["args"]
	return _garrison(ctx, SageScriptWorld.Scope.TEAM, args.text(0), SageScriptWorld.target_team(args.text(1)), true)


static func _team_load(ctx: Dictionary) -> int:
	var args: SageScriptArgs = ctx["args"]
	return _served(ctx, "transport.load_transports", (ctx["world"] as SageScriptWorld).transport().load_transports(args.text(0)))


static func _team_use_button(ctx: Dictionary) -> int:
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx,
		"orders.use_command_button",
		(ctx["world"] as SageScriptWorld).orders().use_command_button(
			SageScriptWorld.Scope.TEAM,
			args.text(0),
			args.text(1),
			SageScriptWorld.target_nearest_garrisoned()
		)
	)


static func _player_garrisoned(ctx: Dictionary) -> int:
	var args: SageScriptArgs = ctx["args"]
	var query := (ctx["world"] as SageScriptWorld).transport().garrisoned_count(args.text(0))
	if not query.ok:
		ctx["detail"] = query.detail
		return Dispatch.Status.WORLD_REFUSED
	ctx["result"] = SageScriptParamTypes.compare_int(query.as_int(), args.integer(1), args.integer(2))
	return Dispatch.Status.OK


static func _unit_has_passenger(ctx: Dictionary) -> int:
	var args: SageScriptArgs = ctx["args"]
	var query := (ctx["world"] as SageScriptWorld).transport().passenger_count(args.text(0))
	if not query.ok:
		ctx["detail"] = query.detail
		return Dispatch.Status.WORLD_REFUSED
	ctx["result"] = query.as_int() > 0
	return Dispatch.Status.OK

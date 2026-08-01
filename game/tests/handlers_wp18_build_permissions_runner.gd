extends SceneTree

const Registry := preload("res://src/script/handlers/_registry.gd")
const Dispatch := preload("res://src/script/script_dispatch.gd")
const CoreHandlers := preload("res://src/script/script_handlers_core.gd")
const Env := preload("res://src/script/script_env.gd")
const Vocabulary := preload("res://src/script/script_vocabulary.gd")
const ParamTypes := preload("res://src/script/script_param_types.gd")

const EXPECTED_CHECKS := 8

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var dispatch := Dispatch.new()
	CoreHandlers.register_all(dispatch)
	var registration := Registry.register_all(dispatch)
	var world := PermissionWorld.new()
	var env := Env.new()
	_check("registry is clean", (registration["errors"] as Array).is_empty())
	_check(
		"package is loaded",
		(registration["packages"] as Array).has("WP18-build-permissions")
	)
	_check(
		"action is coverage counted",
		dispatch.implemented_actions().has("ALLOW_DISALLOW_ONE_BUILDING")
	)
	var denied := dispatch.call_by_name(
		Vocabulary.Kind.ACTION,
		"ALLOW_DISALLOW_ONE_BUILDING",
		[_player("<This Player>"), _text("MenFortress"), _boolean(false)],
		env,
		world,
		"fixture"
	)
	_check("disallow dispatches", int(denied["status"]) == Dispatch.Status.OK)
	_check(
		"arguments are forwarded positionally",
		world.ai_facet.calls == [["<This Player>", "MenFortress", false]]
	)
	var allowed := dispatch.call_by_name(
		Vocabulary.Kind.ACTION,
		"ALLOW_DISALLOW_ONE_BUILDING",
		[_player("PlayerHuman"), _text("MenFortress"), _boolean(true)],
		env,
		world,
		"fixture"
	)
	_check("allow dispatches", int(allowed["status"]) == Dispatch.Status.OK)
	_check(
		"last write is forwarded",
		not world.ai_facet.calls.is_empty()
			and world.ai_facet.calls.back() == ["PlayerHuman", "MenFortress", true]
	)
	world.ai_facet.serve = false
	var refused := dispatch.call_by_name(
		Vocabulary.Kind.ACTION,
		"ALLOW_DISALLOW_ONE_BUILDING",
		[_player("PlayerHuman"), _text("MenFortress"), _boolean(false)],
		env,
		world,
		"fixture"
	)
	_check("world refusal stays a gap", int(refused["status"]) == Dispatch.Status.WORLD_REFUSED)
	world.ai_facet.world = null
	world._facets.clear()
	print("HANDLERS_WP18_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 and passed == EXPECTED_CHECKS else 1)


func _check(name: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		printerr("FAIL %s" % name)


func _text(value: String) -> Dictionary:
	return {"argumentType": 15, "text": value, "integer": 0, "real": 0.0}


func _player(value: String) -> Dictionary:
	return {
		"argumentType": ParamTypes.ARGUMENT_PLAYER,
		"text": value,
		"integer": 0,
		"real": 0.0,
	}


func _boolean(value: bool) -> Dictionary:
	return {
		"argumentType": ParamTypes.ARGUMENT_BOOLEAN,
		"text": "",
		"integer": 1 if value else 0,
		"real": 0.0,
	}


class PermissionAi:
	extends SageScriptWorld.Ai

	var calls: Array = []
	var serve := true

	func set_buildings_allowed(player: String, building_type: String, allowed: bool) -> bool:
		calls.append([player, building_type, allowed])
		return serve


class PermissionWorld:
	extends SageScriptWorld

	var ai_facet := PermissionAi.new()

	func _init() -> void:
		ai_facet.world = self

	func _make_ai() -> Ai:
		return ai_facet

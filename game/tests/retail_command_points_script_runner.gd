extends SceneTree

const SimScript := preload("res://src/retail_slice/retail_slice_sim.gd")
const WorldScript := preload("res://src/retail_slice/retail_slice_script_world.gd")
const Registry := preload("res://src/script/handlers/_registry.gd")
const CoreHandlers := preload("res://src/script/script_handlers_core.gd")
const DispatchScript := preload("res://src/script/script_dispatch.gd")
const EnvScript := preload("res://src/script/script_env.gd")
const ParamTypes := preload("res://src/script/script_param_types.gd")

const PLAYER := "PlayerHuman"
const EXPECTED_CHECKS := 19

var passed := 0
var failed := 0
var worlds: Array = []


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var sim := _make_sim()
	var world := _make_world(sim)
	var pristine_hash := sim.state_hash()
	var status := _execute(world, PLAYER, 800, 1000)
	_check("retail action dispatches", status == DispatchScript.Status.OK)
	_check("total is independently retained", sim.command_point_total_for_team(0) == 800)
	_check("maximum is independently retained", sim.command_point_maximum_for_team(0) == 1000)
	_check("enemy keeps the global total", sim.command_point_total_for_team(1) == 200)
	_check("override changes the hash", sim.state_hash() != pristine_hash)
	_check(
		"canonical snapshot records team total and maximum",
		sim.state_snapshot().get("command_point_overrides", []) == [[0, 800, 1000]]
	)
	var adopted := _make_sim()
	_check("snapshot restores", adopted.restore(sim.snapshot()))
	_check("restored hash matches", adopted.state_hash() == sim.state_hash())
	_check("restored total matches", adopted.command_point_total_for_team(0) == 800)
	_check("restored maximum matches", adopted.command_point_maximum_for_team(0) == 1000)
	_check("last write wins", _execute(world, PLAYER, 40, 70) == SageScriptDispatch.Status.OK)
	_check(
		"last write preserves both new values",
		sim.command_point_total_for_team(0) == 40
			and sim.command_point_maximum_for_team(0) == 70
	)
	var producer := sim.producer_id(0, "barracks")
	sim.team_command_points[0] = 31
	var resources_before := sim.resources_for_team(0)
	var queue_before := sim.production_queue_state(producer)
	var rejected: Dictionary = sim.queue_unit(0, producer, SimScript.SOLDIER_HORDE_ID)
	_check("boundary plus one rejects", rejected.get("reason", "") == "command-point-cap")
	_check("rejection preserves resources", sim.resources_for_team(0) == resources_before)
	_check("rejection preserves queue", sim.production_queue_state(producer) == queue_before)
	sim.team_command_points[0] = 30
	_check(
		"exact boundary admits",
		bool(sim.queue_unit(0, producer, SimScript.SOLDIER_HORDE_ID).get("ok", false))
	)
	var hash_before_invalid := sim.state_hash()
	_check(
		"unknown player refuses",
		_execute(world, "Nobody", 10, 20) == DispatchScript.Status.WORLD_REFUSED
	)
	_check("unknown player cannot mutate", sim.state_hash() == hash_before_invalid)
	adopted.setup({}, _rules())
	_check(
		"setup clears overrides",
		adopted.command_point_overrides_by_team.is_empty()
			and adopted.command_point_total_for_team(0) == 200
			and adopted.command_point_maximum_for_team(0) == 200
	)
	_release_worlds()
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("FAIL liveness ran=%d expected=%d" % [ran, EXPECTED_CHECKS])
	print("RETAIL_COMMAND_POINTS_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _execute(world: RetailSliceScriptWorld, player: String, total: int, maximum: int) -> int:
	var dispatch := DispatchScript.new()
	CoreHandlers.register_all(dispatch)
	Registry.register_all(dispatch)
	return dispatch.execute_action({
		"contentType": 0,
		"internalName": {"name": "OVERRIDE_PLAYER_COMMAND_POINTS", "wireTypeCode": 3},
		"arguments": [
			{"argumentType": ParamTypes.ARGUMENT_PLAYER, "integer": 0, "real": 0.0, "text": player},
			{"argumentType": ParamTypes.ARGUMENT_INTEGER, "integer": total, "real": 0.0, "text": ""},
			{"argumentType": ParamTypes.ARGUMENT_INTEGER, "integer": maximum, "real": 0.0, "text": ""},
		],
		"enabled": true,
		"inverted": false,
	}, EnvScript.new(), world, "fixture")


func _rules() -> Dictionary:
	return {
		"enable_base_loop": true,
		"spawn_initial_battalions": false,
		"starting_resources": 1000,
		"command_point_cap": 200,
		# Keep this focused runner independent of the selected private pack's
		# incomplete armor documents; combat is outside this proof.
		"faction_manifest": {
			"structure_armor": {
				"fortress": {},
				"farm": {},
				"barracks": {},
				"archery_range": {},
				"stable": {},
			},
		},
		"soldier_cost": 100,
		"soldier_command_points": 10,
		"soldier_build_ticks": 10,
	}


func _make_sim() -> RetailSliceSim:
	var sim: RetailSliceSim = SimScript.new()
	sim.setup({}, _rules())
	sim.ai_enabled = false
	return sim


func _make_world(sim: RetailSliceSim) -> RetailSliceScriptWorld:
	var world: RetailSliceScriptWorld = WorldScript.new(sim)
	world.bind_player(PLAYER, SimScript.PLAYER_TEAM)
	world.bind_script_player(PLAYER)
	worlds.append(world)
	return world


func _check(name: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		printerr("FAIL %s" % name)


func _release_worlds() -> void:
	for world in worlds:
		for facet in world._facets.values():
			facet.world = null
		world._facets.clear()
	worlds.clear()

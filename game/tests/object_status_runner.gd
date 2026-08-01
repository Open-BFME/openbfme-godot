extends SceneTree

## Focused proof for object-status set/read on TEAM and PLAYER scopes.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const WorldScript = preload("res://src/retail_slice/retail_slice_script_world.gd")

const PLAYER := "Player_1"
const TEAM := "AttackCenter"

var passed := 0
var failed := 0
var worlds: Array = []


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_team_set_and_read()
	_test_require_all_vs_some()
	_test_empty_status_refuses()
	_release()
	print("OBJECT_STATUS_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _test_team_set_and_read() -> void:
	var fx := _fixture()
	var sim: RetailSliceSim = fx["sim"]
	var world: RetailSliceScriptWorld = fx["world"]
	_check(
		"set_status_on_team",
		world.units().set_object_status(
			SageScriptWorld.Scope.TEAM, TEAM, "UNSELECTABLE", true
		)
	)
	_check(
		"entity_stores_status",
		sim.entity_has_object_status(9001, "UNSELECTABLE")
	)
	var all_q := world.units().has_object_status(
		SageScriptWorld.Scope.TEAM, TEAM, "UNSELECTABLE", true
	)
	_check("team_all_has_status", all_q.ok and bool(all_q.value))
	_check(
		"clear_status",
		world.units().set_object_status(
			SageScriptWorld.Scope.TEAM, TEAM, "UNSELECTABLE", false
		)
	)
	_check(
		"cleared_absent",
		not sim.entity_has_object_status(9001, "UNSELECTABLE")
	)
	var after := world.units().has_object_status(
		SageScriptWorld.Scope.TEAM, TEAM, "UNSELECTABLE", false
	)
	_check("some_has_false_after_clear", after.ok and not bool(after.value))


func _test_require_all_vs_some() -> void:
	var sim: RetailSliceSim = SimScript.new()
	sim.setup({}, {"spawn_initial_battalions": false, "starting_resources": 0})
	sim.ai_enabled = false
	sim.entities[9001] = {
		"id": 9001,
		"team": SimScript.PLAYER_TEAM,
		"health": 100,
		"position": Vector2.ZERO,
		"state": "idle",
	}
	sim.entities[9003] = {
		"id": 9003,
		"team": SimScript.PLAYER_TEAM,
		"health": 100,
		"position": Vector2(10, 0),
		"state": "idle",
	}
	var world: RetailSliceScriptWorld = WorldScript.new(sim)
	worlds.append(world)
	world.bind_player(PLAYER, SimScript.PLAYER_TEAM)
	world.bind_script_team(
		TEAM,
		PLAYER,
		[
			{"kind": "entity", "id": 9001},
			{"kind": "entity", "id": 9003},
		],
		true,
		[],
		0,
		false
	)
	world.bind_script_player(PLAYER)
	sim.set_entity_object_status(9001, "INVISIBLE", true)
	var some := world.units().has_object_status(
		SageScriptWorld.Scope.TEAM, TEAM, "INVISIBLE", false
	)
	var all_of := world.units().has_object_status(
		SageScriptWorld.Scope.TEAM, TEAM, "INVISIBLE", true
	)
	_check("some_true_when_one_has", some.ok and bool(some.value))
	_check("all_false_when_one_missing", all_of.ok and not bool(all_of.value))
	sim.set_entity_object_status(9003, "INVISIBLE", true)
	all_of = world.units().has_object_status(
		SageScriptWorld.Scope.TEAM, TEAM, "INVISIBLE", true
	)
	_check("all_true_when_both_have", all_of.ok and bool(all_of.value))


func _test_empty_status_refuses() -> void:
	var fx := _fixture()
	var world: RetailSliceScriptWorld = fx["world"]
	_check(
		"empty_status_write_refuses",
		not world.units().set_object_status(
			SageScriptWorld.Scope.TEAM, TEAM, "", true
		)
	)
	var q := world.units().has_object_status(
		SageScriptWorld.Scope.TEAM, TEAM, "", true
	)
	_check("empty_status_read_refuses", not q.ok)


func _fixture() -> Dictionary:
	var sim: RetailSliceSim = SimScript.new()
	sim.setup({}, {"spawn_initial_battalions": false, "starting_resources": 0})
	sim.ai_enabled = false
	sim.entities[9001] = {
		"id": 9001,
		"team": SimScript.PLAYER_TEAM,
		"health": 100,
		"position": Vector2.ZERO,
		"state": "idle",
	}
	var world: RetailSliceScriptWorld = WorldScript.new(sim)
	worlds.append(world)
	world.bind_player(PLAYER, SimScript.PLAYER_TEAM)
	world.bind_script_team(
		TEAM,
		PLAYER,
		[{"kind": "entity", "id": 9001}],
		true,
		[],
		0,
		false
	)
	world.bind_script_player(PLAYER)
	return {"sim": sim, "world": world}


func _check(label: String, cond: bool) -> void:
	if cond:
		passed += 1
	else:
		failed += 1
		push_error("FAIL %s" % label)


func _release() -> void:
	for world in worlds:
		for facet in world._facets.values():
			facet.world = null
		world._facets.clear()
	worlds.clear()

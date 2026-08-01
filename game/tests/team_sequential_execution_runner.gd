extends SceneTree

## Focused proof for team sequential queues + <This Team> calling context.
##
## Invocation:
##   Godot_v4.7-stable_win64_console.exe --headless --path game \
##     --script res://tests/team_sequential_execution_runner.gd

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const WorldScript = preload("res://src/retail_slice/retail_slice_script_world.gd")
const ExecutorScript = preload("res://src/script/script_executor.gd")

const PLAYER := "Player_1"
const ATTACK_TEAM := "AttackCenter"

var passed := 0
var failed := 0
var worlds: Array = []


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_this_team_refuses_without_context()
	_test_queue_stop_and_this_team_progress()
	_test_looping_requeues()
	_test_snapshot_round_trip()
	_test_unknown_script_refuses()
	_release_worlds()
	print("TEAM_SEQUENTIAL_EXECUTION_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _test_this_team_refuses_without_context() -> void:
	var fx := _fixture([])
	var world: RetailSliceScriptWorld = fx["world"]
	_check(
		"this_team_refuses_without_latch",
		not world.teams().set_state(WorldScript.THIS_TEAM_TOKEN, "AI_ATTACKING")
	)
	_check(
		"named_team_set_state_without_context_still_works",
		world.teams().set_state(ATTACK_TEAM, "AI_HOLD")
	)
	_check_query_string(
		"named_team_state_readable",
		world.teams().state(ATTACK_TEAM),
		"AI_HOLD"
	)


func _test_queue_stop_and_this_team_progress() -> void:
	var behavior := _behavior_script(
		"be_Attack Center",
		[
			_set_state_action(WorldScript.THIS_TEAM_TOKEN, "AI_ATTACKING"),
			_set_state_action(WorldScript.THIS_TEAM_TOKEN, "AI_DONE"),
		]
	)
	var fx := _fixture([behavior])
	var sim: RetailSliceSim = fx["sim"]
	var world: RetailSliceScriptWorld = fx["world"]
	_check(
		"queue_non_looping_sequential_script",
		world.teams().execute_sequential_script(ATTACK_TEAM, "be_Attack Center", false)
	)
	_check(
		"queue_is_hash_visible",
		sim.sequential_script_queues.has(ATTACK_TEAM)
	)
	# Idle instructions multi-step in one frame (retail isIdle spin). Both
	# TEAM_SET_STATE actions use <This Team>, so AI_DONE proves the latch.
	_step_scripts(sim)
	_check_query_string(
		"idle_instructions_multi_step_with_this_team",
		world.teams().state(ATTACK_TEAM),
		"AI_DONE"
	)
	_check(
		"non_looping_queue_clears_after_completion",
		not sim.sequential_script_queues.has(ATTACK_TEAM)
	)
	_check(
		"stop_on_empty_queue_is_success",
		world.teams().stop_sequential_script(ATTACK_TEAM)
	)
	_check(
		"queue_again_then_stop_clears",
		world.teams().execute_sequential_script(ATTACK_TEAM, "be_Attack Center", false)
			and world.teams().stop_sequential_script(ATTACK_TEAM)
			and not sim.sequential_script_queues.has(ATTACK_TEAM)
	)
	# Busy gate: one instruction per resume when not idle.
	_check(
		"queue_for_busy_gate",
		world.teams().execute_sequential_script(ATTACK_TEAM, "be_Attack Center", false)
	)
	sim.mark_team_sequential_busy(ATTACK_TEAM)
	_step_scripts(sim)
	_check(
		"busy_head_does_not_progress",
		sim.sequential_script_queues.has(ATTACK_TEAM)
			and String(
				(sim.team_behavior_states.get(ATTACK_TEAM, {}) as Dictionary).get(
					"state", ""
				)
			)
			== "AI_DONE"
	)
	# Force a known baseline then idle-progress the still-queued head.
	world.teams().set_state(ATTACK_TEAM, "AI_HOLD")
	sim.mark_team_sequential_idle(ATTACK_TEAM)
	_step_scripts(sim)
	_check_query_string(
		"idle_resume_completes_queued_script",
		world.teams().state(ATTACK_TEAM),
		"AI_DONE"
	)


func _test_looping_requeues() -> void:
	var behavior := _behavior_script(
		"be_Circuit",
		[_set_state_action(WorldScript.THIS_TEAM_TOKEN, "AI_LOOP")]
	)
	var fx := _fixture([behavior])
	var sim: RetailSliceSim = fx["sim"]
	var world: RetailSliceScriptWorld = fx["world"]
	_check(
		"queue_looping_sequential_script",
		world.teams().execute_sequential_script(ATTACK_TEAM, "be_Circuit", true)
	)
	_step_scripts(sim)
	_check_query_string(
		"loop_first_pass_writes_state",
		world.teams().state(ATTACK_TEAM),
		"AI_LOOP"
	)
	_check(
		"loop_requeues_after_completion",
		sim.sequential_script_queues.has(ATTACK_TEAM)
	)
	# Clear state and prove a later step re-executes.
	_check(
		"clear_state_for_second_pass",
		world.teams().set_state(ATTACK_TEAM, "")
	)
	_step_scripts(sim)
	_check_query_string(
		"loop_second_pass_rewrites_state",
		world.teams().state(ATTACK_TEAM),
		"AI_LOOP"
	)
	_check(
		"stop_ends_loop",
		world.teams().stop_sequential_script(ATTACK_TEAM)
			and not sim.sequential_script_queues.has(ATTACK_TEAM)
	)


func _test_snapshot_round_trip() -> void:
	var behavior := _behavior_script(
		"be_Snap",
		[
			_set_state_action(WorldScript.THIS_TEAM_TOKEN, "AI_ONE"),
			_set_state_action(WorldScript.THIS_TEAM_TOKEN, "AI_TWO"),
		]
	)
	var fx := _fixture([behavior])
	var sim: RetailSliceSim = fx["sim"]
	var world: RetailSliceScriptWorld = fx["world"]
	_check(
		"snapshot_queue_starts",
		world.teams().execute_sequential_script(ATTACK_TEAM, "be_Snap", false)
	)
	# Snapshot the queued-but-not-yet-progressed head (instruction -1).
	var saved := sim.snapshot()
	var saved_hash := sim.state_hash()
	var twin: RetailSliceSim = SimScript.new()
	twin.setup({}, {"spawn_initial_battalions": false, "starting_resources": 0})
	twin.ai_enabled = false
	var twin_world: RetailSliceScriptWorld = WorldScript.new(twin)
	worlds.append(twin_world)
	twin_world.bind_player(PLAYER, SimScript.PLAYER_TEAM)
	twin_world.bind_script_team(ATTACK_TEAM, PLAYER, [], true, [], 0, false)
	twin_world.bind_script_player(PLAYER)
	var twin_executor: SageScriptExecutor = ExecutorScript.new(twin_world)
	twin_executor.load_script_payload(behavior)
	_check(
		"twin_env_attaches",
		twin.attach_script_env(twin_executor.env, SimScript.PLAYER_TEAM)
	)
	_check(
		"twin_executor_registers",
		twin.register_script_executor(twin_executor, SimScript.PLAYER_TEAM)
	)
	_check("restore_accepts_sequential_state", twin.restore(saved))
	_check("hash_matches_after_restore", twin.state_hash() == saved_hash)
	_step_scripts(sim)
	_step_scripts(twin)
	_check(
		"restored_progress_matches_live",
		String(
			(sim.team_behavior_states.get(ATTACK_TEAM, {}) as Dictionary).get("state", "")
		) == "AI_TWO"
			and String(
				(twin.team_behavior_states.get(ATTACK_TEAM, {}) as Dictionary).get("state", "")
			) == "AI_TWO"
			and twin.state_hash() == sim.state_hash()
	)


func _test_unknown_script_refuses() -> void:
	var fx := _fixture([])
	var world: RetailSliceScriptWorld = fx["world"]
	var sim: RetailSliceSim = fx["sim"]
	var before := sim.state_hash()
	_check(
		"unknown_script_refuses",
		not world.teams().execute_sequential_script(ATTACK_TEAM, "be_Missing", false)
	)
	_check(
		"unknown_script_does_not_mutate_hash",
		sim.state_hash() == before and not sim.sequential_script_queues.has(ATTACK_TEAM)
	)


func _fixture(scripts: Array) -> Dictionary:
	var sim: RetailSliceSim = SimScript.new()
	sim.setup({}, {"spawn_initial_battalions": false, "starting_resources": 0})
	sim.ai_enabled = false
	var world: RetailSliceScriptWorld = WorldScript.new(sim)
	worlds.append(world)
	world.bind_player(PLAYER, SimScript.PLAYER_TEAM)
	world.bind_script_team(ATTACK_TEAM, PLAYER, [], true, [], 0, false)
	world.bind_script_player(PLAYER)
	var executor: SageScriptExecutor = ExecutorScript.new(world)
	if not scripts.is_empty():
		executor.load_script_payloads(scripts)
	else:
		# Empty executor still registers so cadence plumbing is live.
		executor.load_script_payload(_behavior_script("_empty_fixture", []))
	_check(
		"fixture_env_attaches",
		sim.attach_script_env(executor.env, SimScript.PLAYER_TEAM)
	)
	_check(
		"fixture_executor_registers",
		sim.register_script_executor(executor, SimScript.PLAYER_TEAM)
	)
	return {"sim": sim, "world": world, "executor": executor}


func _step_scripts(sim: RetailSliceSim) -> void:
	## Advance only the script seam (ordinary executors + sequential heads).
	## Full sim.tick() would also run victory and, with no living combatants,
	## set winner and refuse later team commands.
	sim._step_script_executors()


func _behavior_script(name: String, actions: Array) -> Dictionary:
	var records: Array = []
	for action in actions:
		records.append(action)
	return {
		"name": name,
		"comment": "",
		"conditionsComment": "",
		"actionsComment": "",
		"isActive": true,
		"deactivateUponSuccess": false,
		"activeInEasy": true,
		"activeInMedium": true,
		"activeInHard": true,
		"isSubroutine": false,
		"evaluationInterval": 0,
		"actionsFireSequentially": false,
		"loopActions": false,
		"loopCount": 0,
		"sequentialTargetType": 1,
		"sequentialTargetName": "",
		"scope": "ALL",
		"records": records,
	}


func _set_state_action(team: String, state_token: String) -> Dictionary:
	return {
		"name": "ScriptAction",
		"value": {
			"contentType": 0,
			"enabled": true,
			"internalName": {"name": "TEAM_SET_STATE"},
			"arguments": [
				{"typeName": "TEAM", "text": team},
				{"typeName": "TEAM_STATE", "text": state_token},
			],
		},
	}


func _check(label: String, cond: bool, detail: String = "") -> void:
	if cond:
		passed += 1
	else:
		failed += 1
		push_error("FAIL %s %s" % [label, detail])


func _check_query_string(label: String, query: SageWorldQuery, expected: String) -> void:
	if query == null or not query.ok:
		_check(
			label,
			false,
			"query refused: %s" % (query.detail if query != null else "null")
		)
		return
	_check(label, String(query.value) == expected, "got=%s" % str(query.value))


func _release_worlds() -> void:
	for world in worlds:
		for facet in world._facets.values():
			facet.world = null
		world._facets.clear()
	worlds.clear()

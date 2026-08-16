extends SceneTree

## A retail lair's authored respawn cadence, at retail's own magnitudes.
##
## WargLair (data/ini/object/neutral/warglair.ini:171) authors SpawnBehavior with
## SpawnNumber 2, InitialBurst 2, SpawnReplaceDelay 45000 ms, CanReclaimOrphans
## Yes and SpawnTemplateName NeutralWarg. The existing spawn/reclaim runner
## proves the binary-matched reclaim rules with a 100 ms fixture delay; this one
## proves the number the player actually waits: 45 seconds is 450 ticks, the
## garrison never exceeds two, and a dead warg is not replaced one tick early.
##
## SpawnBehavior is already registered executable evidence; this runner is the
## authored-magnitude half of that claim for the lair/creep family.
const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")

const LAIR_ID := 4200
const AUTHORED_SPAWN_NUMBER := 2
const AUTHORED_INITIAL_BURST := 2
const AUTHORED_REPLACE_MS := 45000
const AUTHORED_TEMPLATE := "NeutralWarg"
## Every check a healthy run makes. A GDScript runtime error aborts one section
## and lets the rest report green with a quietly smaller total.
const EXPECTED_CHECKS := 11

var passed := 0
var failed := 0
var _finished := false
var _frames := 0


func _initialize() -> void:
	call_deferred("_run")


func _process(_delta: float) -> bool:
	_frames += 1
	if _finished:
		return true
	if _frames > 600:
		push_error("LAIR_RESPAWN_CADENCE_FAIL runner_aborted_before_reporting")
		print("LAIR_RESPAWN_CADENCE_RESULT passed=%d failed=%d" % [passed, failed + 1])
		quit(1)
		return true
	return false


func _run() -> void:
	_authored_delay_becomes_ticks()
	_initial_burst_fills_the_garrison_at_once()
	_a_dead_warg_returns_only_after_the_authored_delay()
	_finished = true
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		push_error(
			"LAIR_RESPAWN_CADENCE_FAIL check_count_drifted expected=%d observed=%d"
			% [EXPECTED_CHECKS, passed + failed - 1]
		)
	print("LAIR_RESPAWN_CADENCE_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _authored_delay_becomes_ticks() -> void:
	var sim: RetailSliceSim = _sim()
	var lair := _lair()
	sim.structures[LAIR_ID] = lair
	sim._attach_spawn_behavior_contract(lair, _warg_lair_contract())
	var policy := lair["spawn_behavior"] as Dictionary
	_check("authored_ms_become_ticks", int(policy.get("replace_ticks", -1)) == 450)
	_check("authored_ms_are_not_read_as_ticks", int(policy.get("replace_ticks", -1)) != AUTHORED_REPLACE_MS)
	_check("authored_spawn_number_is_carried", int(policy.get("spawn_number", -1)) == AUTHORED_SPAWN_NUMBER)
	_check("authored_burst_is_carried", int(policy.get("initial_remaining", -1)) == AUTHORED_INITIAL_BURST)
	_check("authored_template_is_carried", policy.get("templates", []) == [AUTHORED_TEMPLATE])


func _initial_burst_fills_the_garrison_at_once() -> void:
	var sim: RetailSliceSim = _sim()
	var lair := _lair()
	sim.structures[LAIR_ID] = lair
	sim._attach_spawn_behavior_contract(lair, _warg_lair_contract())
	sim._step_spawn_behaviors()
	var living := Array((lair["spawn_behavior"] as Dictionary).get("spawned_ids", []))
	_check("burst_puts_out_the_authored_count", living.size() == AUTHORED_INITIAL_BURST)
	_check(
		"burst_spawns_the_authored_template",
		living.all(func(id: Variant) -> bool:
			return String((sim.entities[int(id)] as Dictionary).get("object_id", "")) == AUTHORED_TEMPLATE)
	)
	# A full garrison must not keep spawning while the delay is unspent.
	for _tick in range(0, 600):
		sim.tick_index += 1
		sim._step_spawn_behaviors()
	_check(
		"full_garrison_never_exceeds_the_authored_number",
		Array((lair["spawn_behavior"] as Dictionary).get("spawned_ids", [])).size() == AUTHORED_SPAWN_NUMBER
	)


func _a_dead_warg_returns_only_after_the_authored_delay() -> void:
	var sim: RetailSliceSim = _sim()
	var lair := _lair()
	sim.structures[LAIR_ID] = lair
	sim._attach_spawn_behavior_contract(lair, _warg_lair_contract())
	sim._step_spawn_behaviors()
	var living := Array((lair["spawn_behavior"] as Dictionary).get("spawned_ids", []))
	if living.size() != AUTHORED_INITIAL_BURST:
		_check("garrison_ready_before_the_kill", false)
		_check("replacement_is_not_early", false)
		_check("replacement_arrives_on_the_authored_tick", false)
		return
	_check("garrison_ready_before_the_kill", true)
	# Kill one warg; the loss is noticed on the next step, which is the tick the
	# authored delay starts counting from.
	(sim.entities[int(living[0])] as Dictionary)["health"] = 0
	sim.tick_index += 1
	sim._step_spawn_behaviors()
	var due_tick := int((lair["spawn_behavior"] as Dictionary).get("next_spawn_tick", -1))
	while sim.tick_index <= due_tick:
		sim.tick_index += 1
		sim._step_spawn_behaviors()
		if sim.tick_index == due_tick:
			_check(
				"replacement_is_not_early",
				Array((lair["spawn_behavior"] as Dictionary).get("spawned_ids", [])).size() == 1
			)
	_check(
		"replacement_arrives_on_the_authored_tick",
		Array((lair["spawn_behavior"] as Dictionary).get("spawned_ids", [])).size() == AUTHORED_SPAWN_NUMBER
			and due_tick - int(sim.tick_index) == -1
	)


func _sim() -> RetailSliceSim:
	var sim: RetailSliceSim = Sim.new()
	sim.setup({}, {
		"game": "rotwk",
		"spawn_initial_battalions": false,
		"source_map_transform_scale": 1.0,
		"source_unit_scale": 1.0,
		"scenario_unit_runtimes": {AUTHORED_TEMPLATE: _unit_document(AUTHORED_TEMPLATE)},
	})
	sim.ai_enabled = false
	sim.base_loop_enabled = false
	sim.entities.clear()
	sim.structures.clear()
	return sim


func _lair() -> Dictionary:
	return {
		"id": LAIR_ID, "team": 0, "kind": "structure", "structure_kind": "lair",
		"source_object_id": "WargLair", "health": 1000, "maximum_health": 1000,
		"position": Vector2.ZERO, "construction_progress": 1.0,
		"completed_upgrades": [], "upgrade_queue": [], "queue": [],
	}


func _warg_lair_contract() -> Dictionary:
	return {"module": "SpawnBehavior", "extraction": "typed", "fields": {
		"SpawnNumber": {"value": AUTHORED_SPAWN_NUMBER},
		"InitialBurst": {"value": AUTHORED_INITIAL_BURST},
		"SpawnReplaceDelay": {"milliseconds": AUTHORED_REPLACE_MS},
		"SpawnTemplateName": {"value": [AUTHORED_TEMPLATE]},
		"CanReclaimOrphans": {"value": true},
	}}


func _unit_document(object_id: String) -> Dictionary:
	return {"objectId": object_id, "category": "monster", "registration": {
		"production": [],
		"composition": {"containerObjectId": object_id, "primaryMemberObjectId": object_id},
		"scenarioAdmission": {
			"kind": "authored-non-buildable", "role": "creature",
			"surfaces": ["lair-spawn"], "buildCommandExposed": false,
		},
		"presentation": {},
		"simulation": {
			"displayName": object_id, "buildCost": 0, "buildTimeSeconds": 1.0,
			"commandPoints": 1, "memberCount": 1, "memberHealth": 100,
			"speed": 90.0, "visionRange": 200.0,
			"movement": {"acceleration": 90.0, "braking": 90.0, "turnRateDegreesPerSecond": 360.0},
			"formation": {"positions": [{"x": 0.0, "y": 0.0}]},
			"combat": {
				"attackRange": 20.0, "delayBetweenShotsMs": 1500.0,
				"preAttackDelayMs": 500.0, "firingDurationMs": 500.0, "damage": 45,
			},
		},
	}}


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("LAIR_RESPAWN_CADENCE_FAIL %s" % label)

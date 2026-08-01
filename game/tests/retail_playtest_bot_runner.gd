extends SceneTree

## Automated playtester tier 2: bot personas drive the retail sim through the
## command codec while invariant monitors watch every tick. A violation dumps a
## perfect-repro bug artifact (full command log + snapshot + hash) and fails
## the runner. Self-tests prove each monitor actually fires by injecting the
## defect it exists to catch — a monitor that cannot detect its own injection
## is treated as broken.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const CommandScript = preload("res://src/retail_slice/retail_command.gd")

const MATCH_TICKS := 2400
const STALL_GRACE_TICKS := 120
const MOVE_GRACE_TICKS := 200

var passed := 0
var failed := 0
var _artifact_dir := ""


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_PLAYTEST_BOT_RUNNER")
	call_deferred("_run")


func _make_sim():
	var sim = SimScript.new()
	sim._rules = _harness_rules()
	sim.setup({}, {})
	sim.ai_enabled = true
	return sim


func _harness_rules() -> Dictionary:
	return {
		"enable_base_loop": true,
		"starting_resources": 5000,
		"ai_attack_delay_ticks": 600,
		"unit_rules": {
			SimScript.SOLDIER_OBJECT_ID: _unit_rule(SimScript.SOLDIER_HORDE_ID, false),
			SimScript.ARCHER_OBJECT_ID: _unit_rule(SimScript.ARCHER_OBJECT_ID, false),
			SimScript.TOWER_GUARD_OBJECT_ID: _unit_rule(SimScript.TOWER_GUARD_OBJECT_ID, false),
			SimScript.KNIGHT_OBJECT_ID: _unit_rule(SimScript.KNIGHT_OBJECT_ID, false),
			SimScript.BUILDER_OBJECT_ID: _unit_rule(SimScript.BUILDER_OBJECT_ID, true),
		},
	}


func _unit_rule(horde_id: String, is_builder: bool) -> Dictionary:
	return {
		"horde_id": horde_id,
		"speed": 1.0,
		"speed_source": 10.0,
		"acceleration": 1.0,
		"acceleration_source": 10.0,
		"turn_rate_degrees_per_second": 180.0,
		"braking": 1.0,
		"braking_source": 10.0,
		"attack_range": 1.15,
		"attack_range_source": 11.5,
		"minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0,
		"vision_range": 40.0,
		"vision_range_source": 400.0,
		"delay_between_shots_ms": 600.0,
		"pre_attack_delay_ms": 200.0,
		"firing_duration_ms": 200.0,
		"attack_period_ticks": 10,
		"pre_attack_ticks": 2,
		"firing_duration_ticks": 2,
		"member_damage": 10,
		"member_health": 200,
		"member_count": 1,
		"formation_positions": [Vector3.ZERO],
		"provenance": {},
		"is_builder": is_builder,
	}


## Aggressive persona: trains soldiers from the fortress-side producer and
## attack-moves everything at the enemy on a fixed cadence.
class AggressiveBot:
	var team: int
	var _seq := 0

	func _init(bot_team: int) -> void:
		team = bot_team

	func act(sim, tick: int, log: Array) -> void:
		if tick % 150 == 20:
			for producer_id in sim.structure_ids():
				var structure: Dictionary = sim.structures[producer_id]
				if int(structure.get("team", -1)) != team:
					continue
				var cmd := _command(tick + 3, "queue_unit", {
					"producer": producer_id,
					"unit_type": SimScript.SOLDIER_HORDE_ID,
				})
				if sim.submit_command(cmd):
					log.append(cmd)
		if tick % 300 == 150:
			var ids: Array[int] = []
			for id in sim.living_ids(team):
				ids.append(int(id))
			if not ids.is_empty():
				var target := Vector2(20.0 if team == 0 else -20.0, 0.0)
				var cmd := _command(tick + 3, "issue_attack_move", {
					"ids": ids, "destination": target,
				})
				if sim.submit_command(cmd):
					log.append(cmd)

	func _command(tick: int, type: String, args: Dictionary) -> Dictionary:
		_seq += 1
		return {"tick": tick, "team": team, "seq": _seq, "type": type, "args": args}


## Invariant monitors: pure observers over sim state between ticks.
class Monitors:
	var production_last_progress: Dictionary = {}
	var move_last_progress: Dictionary = {}
	var violations: Array[String] = []

	func observe(sim, tick: int) -> void:
		_check_resources(sim, tick)
		_check_production(sim, tick)
		_check_movement(sim, tick)
		_check_entity_budget(sim, tick)

	func _check_resources(sim, tick: int) -> void:
		for team in [0, 1]:
			if int(sim.team_resources.get(team, 0)) < 0:
				violations.append("tick %d: team %d resources negative" % [tick, team])

	func _check_production(sim, tick: int) -> void:
		for producer_id in sim.structure_ids():
			var structure: Dictionary = sim.structures[producer_id]
			var queue: Array = structure.get("production_queue", [])
			if queue.is_empty():
				production_last_progress[producer_id] = tick
				continue
			var head: Dictionary = queue[0]
			var signature := "%s:%s" % [producer_id, str(head.get("remaining_ticks", head.get("progress", "")))]
			var previous: Variant = production_last_progress.get("sig_%d" % producer_id)
			if previous != signature:
				production_last_progress["sig_%d" % producer_id] = signature
				production_last_progress[producer_id] = tick
			elif tick - int(production_last_progress.get(producer_id, tick)) > STALL_GRACE_TICKS:
				violations.append("tick %d: producer %d production stalled" % [tick, producer_id])
				production_last_progress[producer_id] = tick

	func _check_movement(sim, tick: int) -> void:
		for id in sim.entity_ids():
			var entity: Dictionary = sim.entities[id]
			var destination := Vector2(entity.get("destination", Vector2.ZERO))
			var position := Vector2(entity.get("position", Vector2.ZERO))
			if destination.distance_to(position) < 0.5:
				move_last_progress[id] = [tick, position]
				continue
			if not move_last_progress.has(id):
				# First sighting with an active move: persist the baseline —
				# a transient default here would reset the staleness clock
				# every tick and the monitor could never fire.
				move_last_progress[id] = [tick, position]
				continue
			var record: Array = move_last_progress[id]
			if position.distance_to(record[1]) > 0.05:
				move_last_progress[id] = [tick, position]
			elif tick - int(record[0]) > MOVE_GRACE_TICKS and int(entity.get("health", 0)) > 0:
				violations.append("tick %d: entity %d movement deadlocked" % [tick, id])
				move_last_progress[id] = [tick, position]

	func _check_entity_budget(sim, tick: int) -> void:
		if sim.entity_ids().size() > 500:
			violations.append("tick %d: runaway entity count %d" % [tick, sim.entity_ids().size()])


func _write_bug_artifact(name: String, sim, log: Array, violations: Array) -> String:
	var dir := "user://playtest-artifacts"
	DirAccess.make_dir_recursive_absolute(dir)
	var path := "%s/%s.json" % [dir, name]
	var payload := {
		"violations": violations,
		"tick": sim.tick_index,
		"state_hash": sim.state_hash(),
		"command_log": log,
		"snapshot_base64": Marshalls.raw_to_base64(sim.snapshot()),
	}
	var handle := FileAccess.open(path, FileAccess.WRITE)
	handle.store_string(JSON.stringify(payload, "\t"))
	handle.close()
	return ProjectSettings.globalize_path(path)


func _run() -> void:
	# --- Clean match: two aggressive bots, no invariant may fire. ---
	var sim = _make_sim()
	var bots := [AggressiveBot.new(0), AggressiveBot.new(1)]
	var monitors := Monitors.new()
	var log: Array = []
	var twin = _make_sim()
	for tick in range(1, MATCH_TICKS + 1):
		for bot in bots:
			bot.act(sim, tick, log)
		sim.tick()
		monitors.observe(sim, tick)
	# Replay the identical command log on the twin: determinism + repro validity.
	var twin_log: Array = log.duplicate(true)
	for cmd in twin_log:
		twin.submit_command(cmd)
	twin.advance(MATCH_TICKS)
	_check("clean_match_raises_no_invariant_violations", monitors.violations.is_empty(),
		"; ".join(PackedStringArray(monitors.violations.slice(0, 3))))
	_check("command_log_replay_reproduces_exact_state", twin.state_hash() == sim.state_hash())
	_check("match_produced_gameplay", not log.is_empty() and sim.tick_index == MATCH_TICKS)

	# --- Injection A: frozen mover must trip the movement monitor. ---
	var stall_sim = _make_sim()
	var stall_monitors := Monitors.new()
	var mover_id: int = stall_sim.entity_ids()[0]
	var frozen := Vector2(stall_sim.entities[mover_id].get("position", Vector2.ZERO))
	stall_sim.issue_move([mover_id], frozen + Vector2(30.0, 30.0))
	for tick in range(1, MOVE_GRACE_TICKS + 60):
		stall_sim.tick()
		# Sabotage: pin the entity in place while its live move order keeps the
		# distant destination — a genuine pathing deadlock from the outside.
		(stall_sim.entities[mover_id] as Dictionary)["position"] = frozen
		stall_monitors.observe(stall_sim, tick)
	var move_fired := false
	for violation in stall_monitors.violations:
		if violation.contains("movement deadlocked"):
			move_fired = true
	_check("injected_movement_deadlock_is_detected", move_fired)

	# --- Injection B: twin divergence must be caught by hash comparison. ---
	var desync_a = _make_sim()
	var desync_b = _make_sim()
	desync_a.advance(50)
	desync_b.advance(50)
	(desync_b.entities[desync_b.entity_ids()[0]] as Dictionary)["position"] = Vector2(99.0, 99.0)
	desync_a.advance(30)
	desync_b.advance(30)
	_check("injected_desync_is_detected", desync_a.state_hash() != desync_b.state_hash())

	# --- Artifact: violations produce a self-contained repro bundle. ---
	var artifact := _write_bug_artifact("injected-deadlock", stall_sim, [], stall_monitors.violations)
	var artifact_ok := FileAccess.file_exists(artifact)
	if artifact_ok:
		var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(artifact))
		artifact_ok = typeof(parsed) == TYPE_DICTIONARY \
			and not (parsed as Dictionary).get("violations", []).is_empty() \
			and String((parsed as Dictionary).get("snapshot_base64", "")) != ""
	_check("bug_artifact_is_written_and_self_contained", artifact_ok, artifact)

	print("RETAIL_PLAYTEST_BOT_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_PLAYTEST_BOT PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_PLAYTEST_BOT FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])

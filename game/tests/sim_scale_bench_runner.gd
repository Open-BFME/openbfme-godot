extends SceneTree

## Simulation scaling benchmark - how does tick cost grow with unit count?
##
## Why this exists: the product target is large battles (order of 10,000 units on
## large maps). The simulation is deterministic lockstep, which means EVERY client
## simulates EVERY unit and the slowest machine sets the pace for the whole match.
## Network bandwidth is independent of unit count under lockstep; CPU is not. So
## the unit-count ceiling is a simulation-performance question, and nobody has
## measured it.
##
## Budget: TICK_SECONDS is 0.1, so a tick must complete in 100 ms. Anything above
## that on one machine means the simulation cannot keep real time, and in lockstep
## that stalls every player.
##
## This is a MEASUREMENT, not a gate. It prints numbers and always exits 0 unless
## the harness itself breaks. Turning a threshold into a gate is a separate
## decision, and should be made against real numbers rather than a guess.
##
## Run:
##   godot --headless --path game -s res://tests/sim_scale_bench_runner.gd

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")

## Battalion counts to sample. Each battalion carries MEMBERS_PER_BATTALION
## members, so unit count is the product - see the printed summary.
const BATTALION_COUNTS := [50, 100, 200, 400, 800]
const MEMBERS_PER_BATTALION := 5
const WARMUP_TICKS := 5
const MEASURED_TICKS := 40
const TICK_BUDGET_MS := 100.0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	print("SIM_SCALE_BENCH begin members_per_battalion=%d budget_ms=%.1f" % [
		MEMBERS_PER_BATTALION, TICK_BUDGET_MS,
	])
	var rows: Array[Dictionary] = []
	for count in BATTALION_COUNTS:
		var row := _measure(int(count))
		if row.is_empty():
			printerr("SIM_SCALE_BENCH FAIL could not build a sim at battalions=%d" % count)
			quit(1)
			return
		rows.append(row)
		print(
			"SIM_SCALE_BENCH_SAMPLE battalions=%d units=%d mean_ms=%.2f p95_ms=%.2f max_ms=%.2f budget_pct=%.1f"
			% [
				row["battalions"], row["units"], row["mean_ms"], row["p95_ms"],
				row["max_ms"], 100.0 * row["mean_ms"] / TICK_BUDGET_MS,
			]
		)

	# Growth ratio between the two largest samples. Linear scaling roughly doubles
	# cost when unit count doubles; quadratic roughly quadruples it. That distinction
	# decides whether spatial partitioning is optional or mandatory.
	var last: Dictionary = rows[rows.size() - 1]
	var prev: Dictionary = rows[rows.size() - 2]
	var unit_ratio: float = float(last["units"]) / maxf(1.0, float(prev["units"]))
	var cost_ratio: float = float(last["mean_ms"]) / maxf(0.001, float(prev["mean_ms"]))
	var exponent: float = log(cost_ratio) / maxf(0.0001, log(unit_ratio))
	print(
		"SIM_SCALE_BENCH_GROWTH unit_ratio=%.2f cost_ratio=%.2f exponent=%.2f (1.0=linear, 2.0=quadratic)"
		% [unit_ratio, cost_ratio, exponent]
	)

	var headroom: int = 0
	for row in rows:
		if float(row["mean_ms"]) <= TICK_BUDGET_MS:
			headroom = int(row["units"])
	print("SIM_SCALE_BENCH_RESULT largest_within_budget_units=%d samples=%d" % [
		headroom, rows.size(),
	])
	quit(0)


func _measure(battalions: int) -> Dictionary:
	var sim = _make_sim(battalions)
	if sim == null:
		return {}

	for _warm in range(WARMUP_TICKS):
		sim.tick()

	var samples: Array[float] = []
	for _measured in range(MEASURED_TICKS):
		var started := Time.get_ticks_usec()
		sim.tick()
		samples.append(float(Time.get_ticks_usec() - started) / 1000.0)

	samples.sort()
	var total := 0.0
	for value in samples:
		total += value
	var p95_index := clampi(int(floor(float(samples.size()) * 0.95)), 0, samples.size() - 1)
	return {
		"battalions": battalions,
		"units": battalions * MEMBERS_PER_BATTALION,
		"mean_ms": total / float(samples.size()),
		"p95_ms": samples[p95_index],
		"max_ms": samples[samples.size() - 1],
	}


func _make_sim(battalions: int):
	var sim = SimScript.new()
	sim._rules = _harness_rules()
	sim.setup({}, {})
	sim.ai_enabled = false  # isolate per-unit cost from AI planning cost
	for structure_id in sim.structure_ids():
		if structure_id != 1003:
			sim.structures.erase(structure_id)
	for entity_id in sim.entity_ids():
		sim.entities.erase(entity_id)
	sim.expansion_pads.clear()
	sim._ai_production_plan.clear()

	# Two opposing blocks placed so they are in motion and acquiring targets -
	# the interesting cost is target acquisition and movement, not idle units.
	var per_side := maxi(1, battalions / 2)
	var next_id := 5000
	for side in range(2):
		var team: int = SimScript.PLAYER_TEAM if side == 0 else SimScript.ENEMY_TEAM
		for index in range(per_side):
			var column := index % 40
			var row := index / 40
			var x := -60.0 + float(column) * 2.0 if side == 0 else 20.0 + float(column) * 2.0
			var y := -30.0 + float(row) * 2.0
			sim._add_battalion(
				next_id, team, Vector2(x, y), "bench-%d" % next_id,
				SimScript.SOLDIER_OBJECT_ID, SimScript.SOLDIER_HORDE_ID, 0
			)
			next_id += 1

	if sim.entities.is_empty():
		return null

	# Send each side at the other so movement, routing and acquisition all run.
	var ids_a: Array = []
	var ids_b: Array = []
	for entity_id in sim.entity_ids():
		var entity: Dictionary = sim.entities[entity_id]
		if int(entity.get("team", 0)) == SimScript.PLAYER_TEAM:
			ids_a.append(entity_id)
		else:
			ids_b.append(entity_id)
	sim.submit_command({
		"tick": 1, "team": 0, "seq": 1, "type": "issue_attack_move",
		"args": {"ids": ids_a, "destination": Vector2(30.0, 0.0)},
	})
	sim.submit_command({
		"tick": 1, "team": 1, "seq": 2, "type": "issue_attack_move",
		"args": {"ids": ids_b, "destination": Vector2(-50.0, 0.0)},
	})
	return sim


func _harness_rules() -> Dictionary:
	return {"faction_manifest": _q80_harness_manifest(), 
		"enable_base_loop": true,
		"starting_resources": 1000000,
		"ai_attack_delay_ticks": 999999,
		"unit_rules": {
			SimScript.SOLDIER_OBJECT_ID: _unit_rule(SimScript.SOLDIER_HORDE_ID, false),
			SimScript.ARCHER_OBJECT_ID: _unit_rule(SimScript.ARCHER_OBJECT_ID, false),
			SimScript.TOWER_GUARD_OBJECT_ID: _unit_rule(SimScript.TOWER_GUARD_OBJECT_ID, false),
			SimScript.KNIGHT_OBJECT_ID: _unit_rule(SimScript.KNIGHT_OBJECT_ID, false),
			SimScript.BUILDER_OBJECT_ID: _unit_rule(SimScript.BUILDER_OBJECT_ID, true),
		},
	}


func _unit_rule(horde_id: String, is_builder: bool) -> Dictionary:
	var formation: Array = []
	for _member in range(MEMBERS_PER_BATTALION):
		formation.append(Vector3.ZERO)
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
		"member_count": MEMBERS_PER_BATTALION,
		"formation_positions": formation,
		"provenance": {},
		"is_builder": is_builder,
	}


static func _q80_harness_manifest() -> Dictionary:
	# Q80: labeled SYNTHETIC default_manifest() for this harness (its unit
	# rules cover the default roster).
	return preload("res://src/retail_slice/retail_faction_manifest.gd").default_manifest()

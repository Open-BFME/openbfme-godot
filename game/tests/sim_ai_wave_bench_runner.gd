extends SceneTree

## Scaling benchmark for the AI wave-target selection path.
##
## sim_scale_bench_runner.gd sets ai_enabled = false, deliberately, to isolate
## per-unit cost from AI planning cost. That means it never reaches
## _update_ai_controllers' wave-target loop - which was the last remaining
## all-pairs scan in the tick after the spatial index landed.
##
## This variant enables the AI and drops the attack delay to zero so waves form
## immediately, so the loop is on the measured path. Everything else mirrors the
## main bench: same fixture, same two opposing blocks, same tick budget.
##
## Run:
##   godot --headless --path game -s res://tests/sim_ai_wave_bench_runner.gd

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")

const BATTALION_COUNTS := [50, 100, 200, 400, 800]
const MEMBERS_PER_BATTALION := 5
const WARMUP_TICKS := 30
const MEASURED_TICKS := 60
const TICK_BUDGET_MS := 100.0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	print("SIM_AI_WAVE_BENCH begin members_per_battalion=%d budget_ms=%.1f" % [
		MEMBERS_PER_BATTALION, TICK_BUDGET_MS,
	])
	var rows: Array[Dictionary] = []
	for count in BATTALION_COUNTS:
		var row := _measure(int(count))
		if row.is_empty():
			printerr("SIM_AI_WAVE_BENCH FAIL battalions=%d" % count)
			quit(1)
			return
		rows.append(row)
		print(
			"SIM_AI_WAVE_BENCH_SAMPLE battalions=%d units=%d mean_ms=%.2f p95_ms=%.2f budget_pct=%.1f"
			% [
				row["battalions"], row["units"], row["mean_ms"], row["p95_ms"],
				100.0 * row["mean_ms"] / TICK_BUDGET_MS,
			]
		)

	var last: Dictionary = rows[rows.size() - 1]
	var prev: Dictionary = rows[rows.size() - 2]
	var unit_ratio: float = float(last["units"]) / maxf(1.0, float(prev["units"]))
	var cost_ratio: float = float(last["mean_ms"]) / maxf(0.001, float(prev["mean_ms"]))
	print(
		"SIM_AI_WAVE_BENCH_GROWTH unit_ratio=%.2f cost_ratio=%.2f exponent=%.2f (1.0=linear, 2.0=quadratic)"
		% [unit_ratio, cost_ratio, log(cost_ratio) / maxf(0.0001, log(unit_ratio))]
	)
	var headroom: int = 0
	for row in rows:
		if float(row["mean_ms"]) <= TICK_BUDGET_MS:
			headroom = int(row["units"])
	print("SIM_AI_WAVE_BENCH_RESULT largest_within_budget_units=%d" % headroom)
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
	var p95 := clampi(int(floor(float(samples.size()) * 0.95)), 0, samples.size() - 1)
	return {
		"battalions": battalions,
		"units": battalions * MEMBERS_PER_BATTALION,
		"mean_ms": total / float(samples.size()),
		"p95_ms": samples[p95],
	}


func _make_sim(battalions: int):
	var sim = SimScript.new()
	sim._rules = _harness_rules()
	sim.setup({}, {})
	# The whole point of this variant: the AI must run AND its attack delay must
	# be zero, so waves form and the target-selection loop is on the hot path.
	sim.ai_enabled = true
	for structure_id in sim.structure_ids():
		if structure_id != 1003:
			sim.structures.erase(structure_id)
	for entity_id in sim.entity_ids():
		sim.entities.erase(entity_id)
	sim.expansion_pads.clear()
	sim._ai_production_plan.clear()
	sim.force_ai_construction_complete()

	var per_side := maxi(1, battalions / 2)
	var next_id := 5000
	for side in range(2):
		var team: int = SimScript.PLAYER_TEAM if side == 0 else SimScript.ENEMY_TEAM
		for index in range(per_side):
			var column := index % 40
			var row := index / 40
			var x := -60.0 + float(column) * 2.0 if side == 0 else 20.0 + float(column) * 2.0
			sim._add_battalion(
				next_id, team, Vector2(x, -30.0 + float(row) * 2.0), "bench-%d" % next_id,
				SimScript.SOLDIER_OBJECT_ID, SimScript.SOLDIER_HORDE_ID, 0
			)
			next_id += 1
	return null if sim.entities.is_empty() else sim


func _harness_rules() -> Dictionary:
	return {"faction_manifest": _q80_harness_manifest(), 
		"enable_base_loop": true,
		"starting_resources": 1000000,
		"ai_attack_delay_ticks": 0,
		"ai_wave_size": 2,
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
		"speed": 1.0, "speed_source": 10.0,
		"acceleration": 1.0, "acceleration_source": 10.0,
		"turn_rate_degrees_per_second": 180.0,
		"braking": 1.0, "braking_source": 10.0,
		"attack_range": 1.15, "attack_range_source": 11.5,
		"minimum_attack_range": 0.0, "minimum_attack_range_source": 0.0,
		"vision_range": 40.0, "vision_range_source": 400.0,
		"delay_between_shots_ms": 600.0,
		"pre_attack_delay_ms": 200.0,
		"firing_duration_ms": 200.0,
		"attack_period_ticks": 10, "pre_attack_ticks": 2, "firing_duration_ticks": 2,
		"member_damage": 10, "member_health": 200,
		"member_count": MEMBERS_PER_BATTALION,
		"formation_positions": formation,
		"provenance": {},
		"is_builder": is_builder,
	}


static func _q80_harness_manifest() -> Dictionary:
	# Q80: labeled SYNTHETIC default_manifest() for this harness (its unit
	# rules cover the default roster).
	return preload("res://src/retail_slice/retail_faction_manifest.gd").default_manifest()

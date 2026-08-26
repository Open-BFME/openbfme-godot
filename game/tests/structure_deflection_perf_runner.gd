extends SceneTree

## Lane L2b item 6: measured cost of structure deflection on Carn Dum,
## before and after the spatial-hash broad-phase.
##
## Scenario: the locally cooked Carn Dum (tools/cook_castle_fixture_test_pack.py,
## same pack the spawn runner uses) with enable_castle_fixtures ON — 260 real
## seeded structures at their authored positions — plus 40 battalions walking
## across the castle grounds so _step_route deflects every mover every tick.
##
## Reports two numbers, both wall-clock (Time.get_ticks_usec), N ticks each:
##   TICK  — full sim tick cost with the movers live (the delta between the
##           pre/post broad-phase builds isolates the deflection share);
##   CALL  — a focused microbenchmark calling _deflect_around_structures
##           directly M times from inside the wall ring.
## Prints one line per measurement plus a RESULT line; exit 0 always (this is
## a measurement, not a gate).

const Watchdog := preload("res://tests/runner_watchdog.gd")

const MOVERS := 40
const TICKS := 300
const MICRO_CALLS := 2000

var _watchdog := Watchdog.new()


func _initialize() -> void:
	_watchdog.start(self, "STRUCTURE_DEFLECT_PERF", 0, 0, true)
	call_deferred("_run")


func _rules() -> Dictionary:
	return {"faction_manifest": preload("res://src/retail_slice/retail_faction_manifest.gd").default_manifest(), 
		"enable_castle_fixtures": true,
		"spawn_initial_battalions": false,
		"unit_rules": {
			"bfme2.object.gondor-soldier": {
				"horde_id": "bfme2.object.gondor-fighter-horde",
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
				"is_builder": false,
			},
		},
	}


func _run() -> void:
	var MapDataScript: GDScript = load("res://src/retail_slice/retail_map_data.gd")
	var SimScript: GDScript = load("res://src/retail_slice/retail_slice_sim.gd")
	var pack_root := OS.get_environment("OPENBFME_CASTLE_FIXTURE_PACK").strip_edges()
	if pack_root == "":
		pack_root = OS.get_temp_dir() + "/kimi-L2b-castle-pack"
	var map_path := pack_root + "/wor-ang-carn-dum/map.json"
	if not FileAccess.file_exists(map_path):
		printerr("STRUCTURE_DEFLECT_PERF missing cooked test pack at %s — run tools/cook_castle_fixture_test_pack.py first" % pack_root)
		quit(1)
		return
	var definition: Variant = JSON.parse_string(FileAccess.get_file_as_string(map_path))
	definition["_source"] = map_path
	var data: Object = MapDataScript.new()
	if not data.load_from_pack(pack_root, definition):
		printerr("STRUCTURE_DEFLECT_PERF Carn Dum load refused: %s" % String(data.error))
		quit(1)
		return
	var config: Dictionary = data.simulation_configuration()
	var structure_total: int = (config.get("castle_fixture_placements", []) as Array).size()

	var sim = SimScript.new()
	sim.setup(config, _rules())
	var castle_rows: Array[Dictionary] = []
	for structure_id in sim.structure_ids():
		castle_rows.append(sim.structures[structure_id])
	# Anchor the walk on the castle fixture centroid so movers cross the walls.
	var centroid := Vector2.ZERO
	for row in castle_rows:
		centroid += Vector2(row.get("position", Vector2.ZERO))
	centroid /= float(maxi(1, castle_rows.size()))

	# Deterministic mover ring around the centroid, all ordered across it.
	var mover_ids: Array[int] = []
	for index in range(MOVERS):
		var angle := TAU * float(index) / float(MOVERS)
		var start := centroid + Vector2.RIGHT.rotated(angle) * 30.0
		var battalion_id := 1000 + index
		sim._add_battalion(battalion_id, 0, start, "mover", "bfme2.object.gondor-soldier", "bfme2.object.gondor-fighter-horde", 0)
		if sim.entities.has(battalion_id):
			mover_ids.append(battalion_id)
	for battalion_id in mover_ids:
		var row: Dictionary = sim.entities[battalion_id]
		var opposite := centroid - (Vector2(row.get("position", centroid)) - centroid)
		var move_ids: Array[int] = [battalion_id]
		sim.issue_move(move_ids, opposite, "order.move", 0)

	# Warmup so lazy tables exist before the measured window.
	for _tick in range(20):
		sim.tick()
	# Keep the movers walking for the whole measured window.
	for battalion_id in mover_ids:
		var row: Dictionary = sim.entities.get(battalion_id, {})
		if not row.is_empty():
			var opposite := centroid - (Vector2(row.get("position", centroid)) - centroid)
			var move_ids: Array[int] = [battalion_id]
			sim.issue_move(move_ids, opposite, "order.move", 0)

	var tick_start := Time.get_ticks_usec()
	for _tick in range(TICKS):
		sim.tick()
	var tick_total := Time.get_ticks_usec() - tick_start

	# Focused microbenchmark: deflect a standing mover from inside the wall
	# ring, M times, nothing else running.
	var probe_row: Dictionary = sim.entities.get(mover_ids[0], {})
	var micro_start := Time.get_ticks_usec()
	var probe_position := centroid + Vector2(0.5, 0.0)
	for _call in range(MICRO_CALLS):
		sim._deflect_around_structures(probe_position, probe_row, Vector2(0.05, 0.0))
	var micro_total := Time.get_ticks_usec() - micro_start

	print("STRUCTURE_DEFLECT_PERF map=ang-carn-dum structures=%d movers=%d ticks=%d tick_total_us=%d per_tick_us=%.1f" % [
		structure_total, mover_ids.size(), TICKS, tick_total, float(tick_total) / float(TICKS),
	])
	print("STRUCTURE_DEFLECT_PERF microbench calls=%d total_us=%d per_call_us=%.2f" % [
		MICRO_CALLS, micro_total, float(micro_total) / float(MICRO_CALLS),
	])
	print("STRUCTURE_DEFLECT_PERF_RESULT ok")
	_watchdog.stop()
	quit(0)

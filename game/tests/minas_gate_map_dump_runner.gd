extends SceneTree
## DIAGNOSTIC CAMERA for Q64b: prints the ground component map around a castle
## map's gate. Asserts nothing.
const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
var _watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_watchdog.start(self, "MINAS_GATE_MAP_DUMP")
	call_deferred("_run")


func _run() -> void:
	var scene: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	var slice = scene.instantiate()
	root.add_child(slice)
	var frames := 0
	while not bool(slice.ready_ok) and frames < 3000 and String(slice.failure_reason) == "":
		frames += 1
		await process_frame
	var map_data = slice.source_map_data
	var sim = slice.simulation
	for sid in sim.structure_ids():
		var row: Dictionary = sim.structure(sid)
		if String(row.get("castle_fixture_role", "")) != "gate":
			continue
		var centre: Vector2i = map_data.local_to_grid_cell(Vector2(row.get("position", Vector2.ZERO)))
		print("GATE %d centre=%s facing=%.3f geometries=%s" % [sid, str(centre), float(row.get("facing_radians", 0.0)), JSON.stringify(row.get("gate_geometries", {}))])
		var starts: Dictionary = map_data.local_player_starts
		for name in starts.keys():
			var p: Vector3 = starts[name]
			var c: Vector2i = map_data.local_to_grid_cell(Vector2(p.x, p.z))
			print("START %s cell=%s comp=%d" % [name, str(c), map_data.navigation_component_id(c)])
		var legend: Dictionary = {}
		var letters := "FCrabdeghijklmnopqstuvwxyz"
		for y in range(centre.y - 45, centre.y + 46):
			var line := ""
			for x in range(centre.x - 60, centre.x + 61):
				var cell := Vector2i(x, y)
				if not map_data.is_grid_inside_navigation(cell):
					line += " "
				elif not map_data.is_navigation_walkable(cell):
					line += "#"
				else:
					var comp: int = map_data.navigation_component_id(cell)
					if not legend.has(comp):
						legend[comp] = letters[legend.size() % letters.length()]
					line += String(legend[comp])
			if cell_marker(y, centre):
				line = line.substr(0, 60) + "@" + line.substr(61)
			print("ROW %4d %s" % [y, line])
		for comp in legend.keys():
			print("LEGEND %s = component %d size=%d" % [String(legend[comp]), int(comp), map_data.navigation_component_size(int(comp))])
	for name in ["Player_1_Start", "Player_2_Start"]:
		var pp: Vector3 = map_data.local_player_starts[name]
		var cc: Vector2i = map_data.local_to_grid_cell(Vector2(pp.x, pp.z))
		print("HEIGHT %s cell=%s raw=%d" % [name, str(cc), map_data.height_raw_at(cc.x, cc.y)])
	for sid2 in sim.structure_ids():
		var grow: Dictionary = sim.structure(sid2)
		if String(grow.get("castle_fixture_role", "")) == "gate":
			var gc: Vector2i = map_data.local_to_grid_cell(Vector2(grow.get("position", Vector2.ZERO)))
			print("HEIGHT gate cell=%s raw=%d" % [str(gc), map_data.height_raw_at(gc.x, gc.y)])
	_thin_walls(map_data, [[0, 3], [0, 7], [3, 7]])
	quit(0)


func _thin_walls(map_data, pairs: Array) -> void:
	## For each component pair, the thinnest solid separation (cardinal steps
	## through solid cells, max 12) and where it is.
	for pair_value in pairs:
		var a := int(pair_value[0])
		var b := int(pair_value[1])
		var best := 999
		var best_at := Vector2i(-1, -1)
		var count_thin := 0
		for y in range(map_data.navigation_grid_min.y, map_data.navigation_grid_max.y + 1):
			for x in range(map_data.navigation_grid_min.x, map_data.navigation_grid_max.x + 1):
				var cell := Vector2i(x, y)
				if map_data.navigation_component_id(cell) != a:
					continue
				for dir_value in [Vector2i(1, 0), Vector2i(-1, 0), Vector2i(0, 1), Vector2i(0, -1)]:
					var dir: Vector2i = dir_value
					var steps := 0
					var probe: Vector2i = cell + dir
					while steps < 12 and map_data.is_grid_inside_navigation(probe) and not map_data.is_navigation_walkable(probe):
						steps += 1
						probe += dir
					if steps > 0 and steps < 12 and map_data.is_grid_inside_navigation(probe) and map_data.navigation_component_id(probe) == b:
						if steps < best:
							best = steps
							best_at = cell
						if steps <= 6:
							count_thin += 1
							if a == 0 and b == 3 and count_thin <= 40:
								print("THINCELL 0<->3 at=%s steps=%d elev=%.1f" % [str(cell), steps, float(map_data.height_raw_at(cell.x, cell.y))])
		print("THIN %d<->%d thinnest=%d at=%s cells_with_wall<=6=%d" % [a, b, best, str(best_at), count_thin])


func cell_marker(y: int, centre: Vector2i) -> bool:
	return y == centre.y

extends SceneTree
## Cross-platform pin for the pathing gap explicitly excluded by
## retail_state_pin_runner.gd. This frozen scenario uses RetailMapData's real
## AStarGrid2D ground grid plus Q51's authored ramp/deck layer, submits a ground
## -> deck order, and hashes the authoritative sim while the unit is in transit.
##
## The fixture is deliberately duplicated instead of shared with
## castle_wall_walk_runner.gd: changing a shared helper must never silently
## redefine the pin scenario.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const WIDTH := 24
const HEIGHT := 16
const PIN_TICKS := 5

## New Q51 pin measured twice from this exact frozen file on 2026-08-20 after
## the authoritative per-waypoint surface elevation was included:
##   workspace/logs/q51-pathing-pin-remeasure-1.txt
##   workspace/logs/q51-pathing-pin-remeasure-2.txt
## Both independently produced the digest below at tick 5. Unlike the main
## state pin, this is a NEW coverage contract, not a re-mint of prior behavior.
## ---------------------------------------------------------------------------
## RE-MINT 2026-08-25 - Q80: MANIFEST REQUIRED + CARRIED IN THE HASHED RULES.
## ORCHESTRATOR-DIRECTED (owner delegated Q80 takeover); CONSCIOUS MINT.
## Superseded value: 2e5ad58054d28dc93f37ef4728549bb538f6d4a1c22be922ec19b59fb2d1b12d
## New value:        a43f07e45ed20257a0d89913b1763e4cd7f0b49a0e88a4405da014c22f9cb39d
## WHY: the fixture previously passed NO faction manifest; under Q80's
## required-fields rule its sim misconfigured silently, so a config guard was
## added and the labeled synthetic default_manifest() is now supplied. The
## full rules dictionary (manifest included) is hashed by design (exclusion
## reverted for lockstep safety). Pathing semantics themselves are covered
## unchanged by wall_walk/gate/castle-boot runners (all green this session).
## Measured twice; both runs a43f07e4....
## ---------------------------------------------------------------------------
const EXPECTED_HASH := "a43f07e45ed20257a0d89913b1763e4cd7f0b49a0e88a4405da014c22f9cb39d"

var _map_data_script: GDScript


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_map_data_script = load("res://src/retail_slice/retail_map_data.gd") as GDScript
	if _map_data_script == null or not _map_data_script.can_instantiate():
		printerr("RETAIL_PATHING_PIN FAIL retail_map_data.gd did not load")
		quit(1)
		return
	var map = _make_map()
	if not map.install_walk_surface_cells_for_test(_surface_projection()):
		printerr("RETAIL_PATHING_PIN FAIL wall layer did not install")
		quit(1)
		return
	var start := _local(map, Vector2i(2, 7))
	var sim = _make_sim(map, start)
	var selected: Array[int] = [1]
	var destination := _local(map, Vector2i(12, 7))
	if sim.issue_move(selected, destination) != 1:
		printerr("RETAIL_PATHING_PIN FAIL deck order rejected: %s" % sim.last_route_rejection)
		quit(1)
		return
	for _tick in range(PIN_TICKS):
		sim.tick()
	var row: Dictionary = sim.entity(1)
	if not (row.get("route_surface_roles", []) as Array).has("ramp") or (row.get("route", []) as Array).is_empty():
		printerr("RETAIL_PATHING_PIN FAIL frozen scenario did not remain on its ramp-bearing route: position=%s route=%s roles=%s layer=%s state=%s" % [row.get("position"), row.get("route"), row.get("route_surface_roles"), row.get("pathing_layer", "ground"), row.get("state")])
		quit(1)
		return
	var hash := String(sim.state_hash())
	print("RETAIL_PATHING_PIN ticks=%d hash=%s" % [PIN_TICKS, hash])
	if EXPECTED_HASH == "":
		print("RETAIL_PATHING_PIN MEASURE unpinned hash")
		quit(0)
		return
	if hash != EXPECTED_HASH:
		printerr("RETAIL_PATHING_PIN FAIL behaviour moved: got %s, pinned %s" % [hash, EXPECTED_HASH])
		quit(1)
		return
	print("RETAIL_PATHING_PIN OK hash matches the pinned value")
	quit(0)


func _make_map():
	var map = _map_data_script.new()
	map.width = WIDTH
	map.height = HEIGHT
	map.horizontal_scale = 1.0
	map.border_width = 0
	map.reference_elevation = 0.0
	map.local_transform_scale = 1.0
	map.local_transform_origin = Vector2.ZERO
	map.local_axis_x = Vector2.RIGHT
	map.local_axis_z = Vector2.DOWN
	map.playable_grid_min = Vector2i.ZERO
	map.playable_grid_max = Vector2i(WIDTH - 1, HEIGHT - 1)
	map.navigation_grid_min = Vector2i.ZERO
	map.navigation_grid_max = Vector2i(WIDTH - 1, HEIGHT - 1)
	map._water_cells.resize(WIDTH * HEIGHT)
	map._water_cells.fill(0)
	map._ford_corridor_cells.clear()
	for ford_name in ["ford1", "ford2", "ford3"]:
		var mask := PackedByteArray()
		mask.resize(WIDTH * HEIGHT)
		mask.fill(0)
		map._ford_corridor_cells[ford_name] = mask
	var grid := AStarGrid2D.new()
	grid.region = Rect2i(Vector2i.ZERO, Vector2i(WIDTH, HEIGHT))
	grid.cell_size = Vector2.ONE
	grid.diagonal_mode = AStarGrid2D.DIAGONAL_MODE_NEVER
	grid.default_compute_heuristic = AStarGrid2D.HEURISTIC_MANHATTAN
	grid.default_estimate_heuristic = AStarGrid2D.HEURISTIC_MANHATTAN
	grid.update()
	map._navigation_grid = grid
	map.navigation_ready = true
	map.navigation_walkable_count = WIDTH * HEIGHT
	for surface in _surface_projection():
		for cell_value in surface.get("cells", []) as Array:
			var cell := Vector2i(cell_value)
			if String(surface.get("role", "")) != "ramp" or cell != Vector2i(5, 7):
				grid.set_point_solid(cell, true)
	return map


func _surface_projection() -> Array[Dictionary]:
	return [
		{"id": "main-ramp", "role": "ramp", "cells": _line_cells(5, 8, 7), "portal_cells": [Vector2i(5, 7)]},
		{"id": "main-deck", "role": "deck", "cells": _rect_cells(Vector2i(8, 6), Vector2i(14, 8))},
		{"id": "orphan-deck", "role": "deck", "cells": _rect_cells(Vector2i(18, 6), Vector2i(21, 8))},
	]


func _line_cells(from_x: int, to_x: int, y: int) -> Array[Vector2i]:
	var cells: Array[Vector2i] = []
	for x in range(from_x, to_x + 1):
		cells.append(Vector2i(x, y))
	return cells


func _rect_cells(minimum: Vector2i, maximum: Vector2i) -> Array[Vector2i]:
	var cells: Array[Vector2i] = []
	for y in range(minimum.y, maximum.y + 1):
		for x in range(minimum.x, maximum.x + 1):
			cells.append(Vector2i(x, y))
	return cells


func _make_sim(map, start: Vector2):
	var sim = SimScript.new()
	var configuration := {
		"source_map_configured": true,
		"source_map_axis_x": Vector2.RIGHT,
		"source_map_axis_z": Vector2.DOWN,
		"route_provider": map,
		"playable_outline": PackedVector2Array([
			_local(map, Vector2i.ZERO),
			_local(map, Vector2i(WIDTH - 1, 0)),
			_local(map, Vector2i(WIDTH - 1, HEIGHT - 1)),
			_local(map, Vector2i(0, HEIGHT - 1)),
		]),
		"spawn_positions": {1: start, 2: start, 101: start, 102: start},
		"player_starts": {"Player_1_Start": start, "Player_2_Start": start},
		"ford_gates": [
			{"name": "ford1", "edge_a": start, "edge_b": start, "center": start},
			{"name": "ford2", "edge_a": start, "edge_b": start, "center": start},
			{"name": "ford3", "edge_a": start, "edge_b": start, "center": start},
		],
	}
	var unit_rules := {
		SimScript.SOLDIER_OBJECT_ID: _unit_rule(SimScript.SOLDIER_HORDE_ID),
		SimScript.ARCHER_OBJECT_ID: _unit_rule(SimScript.ARCHER_OBJECT_ID),
		SimScript.TOWER_GUARD_OBJECT_ID: _unit_rule(SimScript.TOWER_GUARD_OBJECT_ID),
		SimScript.KNIGHT_OBJECT_ID: _unit_rule(SimScript.KNIGHT_OBJECT_ID),
	}
	# Q80: the 8 core manifest tables are required; this fixture supplies the
	# labeled SYNTHETIC default_manifest() explicitly and refuses loudly on
	# any configuration error instead of hashing a half-configured sim.
	sim.setup(configuration, {
		"spawn_initial_battalions": true,
		"unit_rules": unit_rules,
		"faction_manifest": preload("res://src/retail_slice/retail_faction_manifest.gd").default_manifest(),
	})
	if String(sim.configuration_error) != "":
		printerr("RETAIL_PATHING_PIN FAIL configuration error: %s" % sim.configuration_error)
		quit(1)
		return sim
	sim.ai_enabled = false
	sim.structures.clear()
	sim.expansion_pads.clear()
	for entity_id in sim.entity_ids():
		if entity_id not in [1, 101]:
			sim.entities.erase(entity_id)
	var row: Dictionary = sim.entities[1]
	row["position"] = start
	row["facing"] = Vector2.RIGHT
	row["speed"] = 2.0
	row["current_speed"] = 0.0
	row["state"] = "idle"
	row["target_id"] = 0
	row["attack_move"] = false
	sim._spatial_sync(row)
	return sim


func _unit_rule(horde_id: String) -> Dictionary:
	return {
		"horde_id": horde_id,
		"speed": 2.0,
		"speed_source": 2.0,
		"acceleration": 20.0,
		"acceleration_source": 20.0,
		"braking": 20.0,
		"braking_source": 20.0,
		"turn_rate_degrees_per_second": 720.0,
		"attack_range": 1.0,
		"attack_range_source": 1.0,
		"minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0,
		"vision_range": 40.0,
		"vision_range_source": 40.0,
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
	}


func _local(map, cell: Vector2i) -> Vector2:
	return map.grid_to_local_horizontal(cell)

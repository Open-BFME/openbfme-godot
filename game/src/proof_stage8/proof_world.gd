class_name Stage8ProofWorld
extends RefCounted
## Deterministic maps, groups, formations, and complete save-state authority.

const FormationScript = preload("res://src/proof_stage8/formation_system.gd")
const SaveCodecScript = preload("res://src/proof_stage8/save_codec.gd")
const TEAM_BLUE: int = 0
const TEAM_RED: int = 1
const MAP_KEYS: Array[String] = ["blockerCells", "displayName", "heightCells", "id", "lighting", "resourceCells", "startCells", "widthCells"]

var tick_index: int = 0
var paused: bool = false
var game_speed: int = 1
var map_id: String = ""
var map_definition: Dictionary = {}
var units: Dictionary = {}
var control_groups: Dictionary = {}
var stats: Dictionary = {}
var winner: int = -1

var _maps: Dictionary = {}
var _next_unit_id: int = 1
var _formation: RefCounted = FormationScript.new()


func setup(map_document: Dictionary, selected_map_id: String) -> String:
	var error: String = _configure_maps(map_document)
	if error != "":
		return error
	if not _maps.has(selected_map_id):
		return "unknown_map"
	map_id = selected_map_id
	map_definition = Dictionary(_maps[map_id]).duplicate(true)
	_reset_state()
	return ""


func map_catalog() -> Array[Dictionary]:
	var ids: Array[String] = []
	for key: Variant in _maps.keys():
		ids.append(String(key))
	ids.sort()
	var result: Array[Dictionary] = []
	for id: String in ids:
		result.append(Dictionary(_maps[id]).duplicate(true))
	return result


func switch_map(selected_map_id: String) -> String:
	if not _maps.has(selected_map_id):
		return "unknown_map"
	map_id = selected_map_id
	map_definition = Dictionary(_maps[map_id]).duplicate(true)
	_reset_state()
	return ""


func add_unit(team: int, cell: Vector2i, maximum_health: int = 100, forced_id: int = 0) -> int:
	if team < TEAM_BLUE or team > TEAM_RED or maximum_health <= 0 or not contains_cell(cell) or is_blocked(cell):
		return 0
	var id: int = forced_id if forced_id > 0 else _next_unit_id
	if units.has(id):
		return 0
	_next_unit_id = maxi(_next_unit_id, id + 1)
	units[id] = {
		"id": id,
		"team": team,
		"cell": cell,
		"health": maximum_health,
		"maximum_health": maximum_health,
		"order": "hold",
		"destination": cell,
		"formation": "line",
		"formation_slot": 0,
		"alive": true,
	}
	return id


func entity(entity_id: int) -> Dictionary:
	return units.get(entity_id, {})


func entity_ids() -> Array[int]:
	var ids: Array[int] = []
	for key: Variant in units.keys():
		ids.append(int(key))
	ids.sort()
	return ids


func assign_control_group(group: int, entity_ids: Array[int]) -> Dictionary:
	if group < 1 or group > 9:
		return {"ok": false, "reason": "invalid_group"}
	var assigned: Array[int] = []
	var sorted_ids: Array[int] = entity_ids.duplicate()
	sorted_ids.sort()
	for id: int in sorted_ids:
		var unit: Dictionary = entity(id)
		if unit.is_empty() or not bool(unit.get("alive", false)) or int(unit.get("team", -1)) != TEAM_BLUE or assigned.has(id):
			continue
		assigned.append(id)
	control_groups[group] = assigned
	return {"ok": true, "reason": "", "group": group, "entity_ids": assigned.duplicate()}


func recall_control_group(group: int) -> Array[int]:
	if group < 1 or group > 9:
		return []
	var result: Array[int] = []
	for value: Variant in Array(control_groups.get(group, [])):
		var id: int = int(value)
		var unit: Dictionary = entity(id)
		if not unit.is_empty() and bool(unit.get("alive", false)) and int(unit.get("team", -1)) == TEAM_BLUE:
			result.append(id)
	result.sort()
	control_groups[group] = result.duplicate()
	return result


func order_formation(entity_ids: Array[int], kind: String, anchor: Vector2i) -> Dictionary:
	var eligible: Array[int] = []
	for id: int in entity_ids:
		var unit: Dictionary = entity(id)
		if not unit.is_empty() and bool(unit.get("alive", false)) and int(unit.get("team", -1)) == TEAM_BLUE and not eligible.has(id):
			eligible.append(id)
	eligible.sort()
	if eligible.is_empty():
		return {"ok": false, "reason": "no_units", "assignments": []}
	var assignments: Array[Dictionary] = _formation.generate(eligible, kind, anchor)
	if assignments.is_empty():
		return {"ok": false, "reason": "invalid_formation", "assignments": []}
	for assignment: Dictionary in assignments:
		var cell: Vector2i = assignment["cell"]
		if not contains_cell(cell) or is_blocked(cell):
			return {"ok": false, "reason": "formation_blocked", "assignments": []}
	for assignment: Dictionary in assignments:
		var unit: Dictionary = entity(int(assignment["entity_id"]))
		unit["destination"] = assignment["cell"]
		unit["order"] = "move"
		unit["formation"] = kind
		unit["formation_slot"] = int(assignment["slot_index"])
	stats["orders_issued"] = int(stats["orders_issued"]) + 1
	return {"ok": true, "reason": "", "assignments": assignments}


func set_paused(value: bool) -> void:
	paused = value


func set_game_speed(value: int) -> bool:
	if not [1, 2, 4].has(value):
		return false
	game_speed = value
	return true


func advance_frame() -> void:
	for _step: int in range(game_speed):
		tick()


func tick() -> void:
	if paused:
		return
	for id: int in entity_ids():
		var unit: Dictionary = units[id]
		if not bool(unit["alive"]) or String(unit["order"]) != "move":
			continue
		var current: Vector2i = unit["cell"]
		var destination: Vector2i = unit["destination"]
		if current == destination:
			unit["order"] = "hold"
			continue
		var next: Vector2i = current
		if current.x != destination.x:
			next.x += signi(destination.x - current.x)
		if is_blocked(next) and current.y != destination.y:
			next = current + Vector2i(0, signi(destination.y - current.y))
		elif next == current and current.y != destination.y:
			next.y += signi(destination.y - current.y)
		if is_blocked(next):
			unit["order"] = "blocked"
			continue
		unit["cell"] = next
		stats["distance_cells"] = int(stats["distance_cells"]) + 1
		if next == destination:
			unit["order"] = "hold"
	tick_index += 1


func advance(ticks: int) -> void:
	for _index: int in range(maxi(0, ticks)):
		tick()


func damage_unit(entity_id: int, damage: int) -> Dictionary:
	var unit: Dictionary = entity(entity_id)
	if unit.is_empty() or not bool(unit.get("alive", false)) or damage <= 0:
		return {"ok": false, "reason": "invalid_target", "damage": 0}
	var old: int = int(unit["health"])
	unit["health"] = maxi(0, old - damage)
	var applied: int = old - int(unit["health"])
	stats["damage_applied"] = int(stats["damage_applied"]) + applied
	if int(unit["health"]) == 0:
		unit["alive"] = false
		unit["order"] = "defeated"
		var casualty_key: String = "blue_losses" if int(unit["team"]) == TEAM_BLUE else "red_losses"
		stats[casualty_key] = int(stats[casualty_key]) + 1
	return {"ok": true, "reason": "", "damage": applied, "defeated": not bool(unit["alive"])}


func declare_victory(team: int) -> bool:
	if winner != -1 or team < TEAM_BLUE or team > TEAM_RED:
		return false
	winner = team
	stats["victory_tick"] = tick_index
	return true


func contains_cell(cell: Vector2i) -> bool:
	return cell.x >= 0 and cell.x < int(map_definition.get("widthCells", 0)) and cell.y >= 0 and cell.y < int(map_definition.get("heightCells", 0))


func is_blocked(cell: Vector2i) -> bool:
	if not contains_cell(cell):
		return true
	for value: Variant in Array(map_definition.get("blockerCells", [])):
		var pair: Array = value
		if pair.size() == 2 and Vector2i(int(pair[0]), int(pair[1])) == cell:
			return true
	return false


func snapshot() -> Dictionary:
	var unit_rows: Array[Dictionary] = []
	for id: int in entity_ids():
		var unit: Dictionary = units[id]
		var cell: Vector2i = unit["cell"]
		var destination: Vector2i = unit["destination"]
		unit_rows.append({
			"id": id,
			"team": int(unit["team"]),
			"cell": [cell.x, cell.y],
			"health": int(unit["health"]),
			"maximum_health": int(unit["maximum_health"]),
			"order": String(unit["order"]),
			"destination": [destination.x, destination.y],
			"formation": String(unit["formation"]),
			"formation_slot": int(unit["formation_slot"]),
			"alive": bool(unit["alive"]),
		})
	var group_rows: Array[Dictionary] = []
	for group: int in range(1, 10):
		var ids: Array[int] = []
		for value: Variant in Array(control_groups.get(group, [])):
			ids.append(int(value))
		ids.sort()
		group_rows.append({"group": group, "entity_ids": ids})
	return {
		"schema": "openbfme.stage8-save",
		"schema_version": 1,
		"authority": "gdscript-proof",
		"map_id": map_id,
		"tick": tick_index,
		"paused": paused,
		"game_speed": game_speed,
		"next_unit_id": _next_unit_id,
		"winner": winner,
		"stats": _stats_snapshot(),
		"control_groups": group_rows,
		"units": unit_rows,
	}


func serialize_state() -> String:
	return SaveCodecScript.encode(snapshot())


func restore_state(snapshot_value: Dictionary) -> String:
	if String(snapshot_value.get("schema", "")) != "openbfme.stage8-save" or int(snapshot_value.get("schema_version", 0)) != 1:
		return "invalid_save_schema"
	var saved_map: String = String(snapshot_value.get("map_id", ""))
	if not _maps.has(saved_map):
		return "save_map_missing"
	var raw_units: Variant = snapshot_value.get("units", null)
	var raw_groups: Variant = snapshot_value.get("control_groups", null)
	if typeof(raw_units) != TYPE_ARRAY or typeof(raw_groups) != TYPE_ARRAY:
		return "invalid_save_arrays"
	map_id = saved_map
	map_definition = Dictionary(_maps[map_id]).duplicate(true)
	units.clear()
	control_groups.clear()
	for value: Variant in Array(raw_units):
		if typeof(value) != TYPE_DICTIONARY:
			return "invalid_save_unit"
		var row: Dictionary = value
		var cell_value: Array = row.get("cell", [])
		var destination_value: Array = row.get("destination", [])
		if cell_value.size() != 2 or destination_value.size() != 2:
			return "invalid_save_position"
		var id: int = int(row.get("id", 0))
		units[id] = {
			"id": id,
			"team": int(row.get("team", -1)),
			"cell": Vector2i(int(cell_value[0]), int(cell_value[1])),
			"health": int(row.get("health", 0)),
			"maximum_health": int(row.get("maximum_health", 0)),
			"order": String(row.get("order", "hold")),
			"destination": Vector2i(int(destination_value[0]), int(destination_value[1])),
			"formation": String(row.get("formation", "line")),
			"formation_slot": int(row.get("formation_slot", 0)),
			"alive": bool(row.get("alive", false)),
		}
	for value: Variant in Array(raw_groups):
		var row: Dictionary = value
		var ids: Array[int] = []
		for id_value: Variant in Array(row.get("entity_ids", [])):
			ids.append(int(id_value))
		control_groups[int(row.get("group", 0))] = ids
	tick_index = int(snapshot_value.get("tick", 0))
	paused = bool(snapshot_value.get("paused", false))
	game_speed = int(snapshot_value.get("game_speed", 1))
	_next_unit_id = int(snapshot_value.get("next_unit_id", 1))
	winner = int(snapshot_value.get("winner", -1))
	var saved_stats: Dictionary = snapshot_value.get("stats", {})
	stats = {
		"orders_issued": int(saved_stats.get("orders_issued", 0)),
		"distance_cells": int(saved_stats.get("distance_cells", 0)),
		"damage_applied": int(saved_stats.get("damage_applied", 0)),
		"blue_losses": int(saved_stats.get("blue_losses", 0)),
		"red_losses": int(saved_stats.get("red_losses", 0)),
		"victory_tick": int(saved_stats.get("victory_tick", -1)),
	}
	return validate_state()


func deserialize_state(text: String) -> String:
	var decoded: Dictionary = SaveCodecScript.decode(text)
	return "invalid_save_json" if decoded.is_empty() else restore_state(decoded)


func save_slot(slot: int) -> Dictionary:
	return SaveCodecScript.write_slot(slot, snapshot())


func load_slot(slot: int) -> Dictionary:
	var read: Dictionary = SaveCodecScript.read_slot(slot)
	if not bool(read.get("ok", false)):
		return read
	var error: String = restore_state(read["snapshot"])
	return {"ok": error == "", "reason": error, "bytes": int(read.get("bytes", 0))}


func state_hash() -> int:
	var value: int = 0x811C9DC5
	for byte: int in serialize_state().to_utf8_buffer():
		value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
	return value


func state_hash_text() -> String:
	return "%08X" % state_hash()


func validate_state() -> String:
	if not _maps.has(map_id):
		return "invalid_map"
	if not [1, 2, 4].has(game_speed) or tick_index < 0 or _next_unit_id < 1:
		return "invalid_clock"
	var seen: Dictionary = {}
	for id: int in entity_ids():
		var unit: Dictionary = units[id]
		if id <= 0 or seen.has(id):
			return "invalid_unit_id"
		seen[id] = true
		if not contains_cell(Vector2i(unit["cell"])) or int(unit["maximum_health"]) <= 0 or int(unit["health"]) < 0 or int(unit["health"]) > int(unit["maximum_health"]):
			return "invalid_unit_state"
		if bool(unit["alive"]) != (int(unit["health"]) > 0):
			return "unit_life_mismatch"
	for group: int in range(1, 10):
		for value: Variant in Array(control_groups.get(group, [])):
			if not units.has(int(value)):
				return "group_missing_unit"
	return ""


func _configure_maps(document: Dictionary) -> String:
	var top_keys: Array[String] = ["maps", "rulesVersion", "schema", "schemaVersion"]
	if not _exact_keys(document, top_keys) or String(document.get("schema", "")) != "openbfme.skirmish-maps" or int(document.get("schemaVersion", -1)) != 0 or int(document.get("rulesVersion", -1)) != 1:
		return "invalid_map_document"
	if typeof(document.get("maps", null)) != TYPE_ARRAY or Array(document["maps"]).size() != 4:
		return "four_maps_required"
	var parsed: Dictionary = {}
	for value: Variant in Array(document["maps"]):
		if typeof(value) != TYPE_DICTIONARY:
			return "invalid_map_entry"
		var map: Dictionary = value
		if not _exact_keys(map, MAP_KEYS):
			return "invalid_map_fields"
		var id: String = String(map.get("id", ""))
		if id.is_empty() or parsed.has(id) or int(map.get("widthCells", 0)) < 12 or int(map.get("heightCells", 0)) < 10:
			return "invalid_map_identity"
		for field: String in ["startCells", "resourceCells", "blockerCells"]:
			if typeof(map[field]) != TYPE_ARRAY:
				return "invalid_map_cells"
		parsed[id] = map.duplicate(true)
	_maps = parsed
	return ""


func _reset_state() -> void:
	tick_index = 0
	paused = false
	game_speed = 1
	units.clear()
	control_groups.clear()
	for group: int in range(1, 10):
		control_groups[group] = []
	stats = {"orders_issued": 0, "distance_cells": 0, "damage_applied": 0, "blue_losses": 0, "red_losses": 0, "victory_tick": -1}
	winner = -1
	_next_unit_id = 1


func _stats_snapshot() -> Dictionary:
	return {
		"orders_issued": int(stats.get("orders_issued", 0)),
		"distance_cells": int(stats.get("distance_cells", 0)),
		"damage_applied": int(stats.get("damage_applied", 0)),
		"blue_losses": int(stats.get("blue_losses", 0)),
		"red_losses": int(stats.get("red_losses", 0)),
		"victory_tick": int(stats.get("victory_tick", -1)),
	}


static func _exact_keys(value: Dictionary, expected: Array[String]) -> bool:
	if value.size() != expected.size():
		return false
	for key: String in expected:
		if not value.has(key):
			return false
	return true

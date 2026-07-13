class_name Stage6ProofWorld
extends RefCounted
## Deterministic multi-faction research, art-resolution, and combat proof world.

const CatalogScript = preload("res://src/proof_stage6/faction_catalog.gd")
const MatrixScript = preload("res://src/proof_stage6/combat_matrix.gd")

var width: int = 14
var height: int = 9
var tick_index: int = 0
var catalog: RefCounted
var matrix: RefCounted
var faction_states: Dictionary = {}
var entities: Dictionary = {}
var combat_events: Array[Dictionary] = []
var _next_entity_id: int = 1
var _configured: bool = false


func configure(document: Dictionary) -> String:
	var next_catalog := CatalogScript.new()
	var error: String = next_catalog.configure(document)
	if error != "":
		return error
	catalog = next_catalog
	matrix = MatrixScript.new(catalog)
	_configured = true
	reset()
	return ""


func reset() -> void:
	assert(_configured)
	tick_index = 0
	entities.clear()
	combat_events.clear()
	_next_entity_id = 1
	faction_states.clear()
	for faction_id: String in catalog.faction_ids():
		var definition: Dictionary = catalog.faction(faction_id)
		faction_states[faction_id] = {
			"resources": int(definition["startingResources"]),
			"completed_upgrades": [],
			"research": {},
		}


func setup_showcase() -> void:
	assert(_configured)
	reset()
	var faction_ids: Array[String] = catalog.faction_ids()
	var columns: Dictionary = {"aurora_compact": 2, "ember_union": 5, "verdant_league": 8, "dusk_accord": 11}
	for faction_id: String in faction_ids:
		var roster: Array[Dictionary] = catalog.roster_for(faction_id)
		for unit_index: int in range(roster.size()):
			add_unit(faction_id, String(roster[unit_index]["unitId"]), Vector2i(int(columns[faction_id]), 3 + unit_index * 3))


func setup_battalion_probe(count: int = 80) -> int:
	assert(_configured)
	reset()
	var target_count: int = mini(maxi(0, count), width * height)
	var faction_ids: Array[String] = catalog.faction_ids()
	for index: int in range(target_count):
		var faction_id: String = faction_ids[index % faction_ids.size()]
		var roster: Array[Dictionary] = catalog.roster_for(faction_id)
		var roster_index: int = (index / faction_ids.size()) % roster.size()
		var unit: Dictionary = roster[roster_index]
		var cell := Vector2i(index % width, index / width)
		add_unit(faction_id, String(unit["unitId"]), cell)
	return entities.size()


func add_unit(faction_id: String, unit_id: String, position: Vector2i) -> int:
	if not faction_states.has(faction_id) or catalog.faction_for_unit(unit_id) != faction_id or not contains_cell(position) or _live_entity_at(position) != 0:
		return 0
	var definition: Dictionary = catalog.unit(unit_id)
	var entity_id: int = _next_entity_id
	_next_entity_id += 1
	entities[entity_id] = {
		"id": entity_id,
		"faction_id": faction_id,
		"unit_id": unit_id,
		"position": position,
		"health": int(definition["maximumHealth"]),
		"maximum_health": int(definition["maximumHealth"]),
		"alive": true,
		"ready_tick": 0,
	}
	return entity_id


func entity(entity_id: int) -> Dictionary:
	return entities.get(entity_id, {})


func entity_ids() -> Array[int]:
	var ids: Array[int] = []
	for key: Variant in entities.keys():
		ids.append(int(key))
	ids.sort()
	return ids


func contains_cell(cell: Vector2i) -> bool:
	return cell.x >= 0 and cell.x < width and cell.y >= 0 and cell.y < height


func completed_upgrades(faction_id: String) -> Array:
	var state: Dictionary = faction_states.get(faction_id, {})
	return Array(state.get("completed_upgrades", [])).duplicate()


func begin_research(faction_id: String, upgrade_id: String) -> Dictionary:
	var state: Dictionary = faction_states.get(faction_id, {})
	var faction: Dictionary = catalog.faction(faction_id) if catalog != null else {}
	var definition: Dictionary = catalog.upgrade(upgrade_id) if catalog != null else {}
	if state.is_empty() or faction.is_empty() or definition.is_empty() or not Array(faction.get("upgradeIds", [])).has(upgrade_id):
		return {"ok": false, "reason": "upgrade_unavailable"}
	if Array(state["completed_upgrades"]).has(upgrade_id):
		return {"ok": false, "reason": "already_researched"}
	if not Dictionary(state["research"]).is_empty():
		return {"ok": false, "reason": "research_busy"}
	var cost: int = int(definition["cost"])
	if int(state["resources"]) < cost:
		return {"ok": false, "reason": "insufficient_resources", "cost": cost}
	state["resources"] = int(state["resources"]) - cost
	var complete_tick: int = tick_index + int(definition["researchTicks"])
	state["research"] = {"upgrade_id": upgrade_id, "started_tick": tick_index, "complete_tick": complete_tick}
	return {"ok": true, "reason": "", "upgrade_id": upgrade_id, "complete_tick": complete_tick, "cost": cost}


func attack(attacker_id: int, target_id: int) -> Dictionary:
	var attacker: Dictionary = entity(attacker_id)
	var target: Dictionary = entity(target_id)
	if attacker.is_empty() or target.is_empty() or not bool(attacker.get("alive", false)) or not bool(target.get("alive", false)):
		return {"ok": false, "reason": "invalid_entity", "damage": 0}
	if String(attacker["faction_id"]) == String(target["faction_id"]):
		return {"ok": false, "reason": "friendly_target", "damage": 0}
	if tick_index < int(attacker["ready_tick"]):
		return {"ok": false, "reason": "cooldown", "damage": 0, "ready_tick": int(attacker["ready_tick"])}
	var attacker_definition: Dictionary = catalog.unit(String(attacker["unit_id"]))
	if _distance(Vector2i(attacker["position"]), Vector2i(target["position"])) > int(attacker_definition["rangeCells"]):
		return {"ok": false, "reason": "target_out_of_range", "damage": 0}
	var resolution: Dictionary = matrix.resolve(
		String(attacker["unit_id"]),
		String(target["unit_id"]),
		completed_upgrades(String(attacker["faction_id"])),
		completed_upgrades(String(target["faction_id"]))
	)
	if not bool(resolution.get("ok", false)):
		return resolution
	var old_health: int = int(target["health"])
	target["health"] = maxi(0, old_health - int(resolution["damage"]))
	target["alive"] = int(target["health"]) > 0
	attacker["ready_tick"] = tick_index + int(attacker_definition["cooldownTicks"])
	var event := {
		"tick": tick_index,
		"attacker_id": attacker_id,
		"target_id": target_id,
		"damage": old_health - int(target["health"]),
		"target_health": int(target["health"]),
		"target_defeated": not bool(target["alive"]),
	}
	combat_events.append(event)
	var result := resolution.duplicate(true)
	result["damage"] = int(event["damage"])
	result["ready_tick"] = int(attacker["ready_tick"])
	result["target_defeated"] = bool(event["target_defeated"])
	return result


func damage_preview(attacker_unit_id: String, defender_unit_id: String, attacker_faction_id: String, defender_faction_id: String) -> Dictionary:
	return matrix.resolve(attacker_unit_id, defender_unit_id, completed_upgrades(attacker_faction_id), completed_upgrades(defender_faction_id))


func set_entity_position(entity_id: int, cell: Vector2i) -> bool:
	var row: Dictionary = entity(entity_id)
	if row.is_empty() or not contains_cell(cell) or (_live_entity_at(cell) != 0 and _live_entity_at(cell) != entity_id):
		return false
	row["position"] = cell
	return true


func tick() -> void:
	tick_index += 1
	for faction_id: String in catalog.faction_ids():
		var state: Dictionary = faction_states[faction_id]
		var research: Dictionary = state["research"]
		if research.is_empty() or tick_index < int(research["complete_tick"]):
			continue
		var completed: Array = state["completed_upgrades"]
		completed.append(String(research["upgrade_id"]))
		completed.sort()
		state["completed_upgrades"] = completed
		state["research"] = {}


func advance(ticks: int) -> void:
	for _index: int in range(maxi(0, ticks)):
		tick()


func snapshot() -> Dictionary:
	var faction_rows: Array[Dictionary] = []
	for faction_id: String in catalog.faction_ids():
		var state: Dictionary = faction_states[faction_id]
		faction_rows.append({
			"faction_id": faction_id,
			"resources": int(state["resources"]),
			"completed_upgrades": Array(state["completed_upgrades"]).duplicate(),
			"research": Dictionary(state["research"]).duplicate(true),
		})
	var entity_rows: Array[Dictionary] = []
	for entity_id: int in entity_ids():
		var row: Dictionary = entities[entity_id]
		entity_rows.append({
			"id": entity_id,
			"faction_id": String(row["faction_id"]),
			"unit_id": String(row["unit_id"]),
			"position": [int(Vector2i(row["position"]).x), int(Vector2i(row["position"]).y)],
			"health": int(row["health"]),
			"maximum_health": int(row["maximum_health"]),
			"alive": bool(row["alive"]),
			"ready_tick": int(row["ready_tick"]),
		})
	return {
		"authority": "gdscript-proof",
		"rules_version": 1,
		"tick": tick_index,
		"width": width,
		"height": height,
		"next_entity_id": _next_entity_id,
		"definitions": catalog.definitions_snapshot(),
		"art_coverage": catalog.art_coverage(),
		"faction_states": faction_rows,
		"entities": entity_rows,
		"combat_events": combat_events.duplicate(true),
	}


func state_hash() -> int:
	var value: int = 0x811C9DC5
	for byte: int in JSON.stringify(snapshot()).to_utf8_buffer():
		value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
	return value


func state_hash_text() -> String:
	return "%08X" % state_hash()


func validate_state() -> String:
	if not _configured or catalog == null or matrix == null:
		return "world_not_configured"
	var coverage: Dictionary = catalog.art_coverage()
	if not bool(coverage.get("legal_safe", false)) or int(coverage.get("resolved", 0)) != int(coverage.get("total", -1)):
		return "art_coverage_incomplete"
	var seen_positions: Dictionary = {}
	for faction_id: String in catalog.faction_ids():
		var state: Dictionary = faction_states.get(faction_id, {})
		if state.is_empty() or int(state.get("resources", -1)) < 0:
			return "invalid_faction_state"
		for raw_upgrade_id: Variant in Array(state.get("completed_upgrades", [])):
			if not catalog.upgrades.has(String(raw_upgrade_id)):
				return "unknown_completed_upgrade"
	for entity_id: int in entity_ids():
		var row: Dictionary = entities[entity_id]
		if int(row.get("id", 0)) != entity_id or catalog.faction_for_unit(String(row.get("unit_id", ""))) != String(row.get("faction_id", "")):
			return "invalid_entity_identity"
		var cell: Vector2i = row.get("position", Vector2i(-1, -1))
		if not contains_cell(cell) or (bool(row.get("alive", false)) and seen_positions.has(cell)):
			return "invalid_entity_position"
		if bool(row.get("alive", false)):
			seen_positions[cell] = true
		if int(row.get("health", -1)) < 0 or int(row.get("health", 0)) > int(row.get("maximum_health", 0)) or bool(row.get("alive", false)) != (int(row.get("health", 0)) > 0):
			return "invalid_entity_health"
	return ""


func _live_entity_at(cell: Vector2i) -> int:
	for entity_id: int in entity_ids():
		var row: Dictionary = entities[entity_id]
		if bool(row.get("alive", false)) and Vector2i(row.get("position", Vector2i(-1, -1))) == cell:
			return entity_id
	return 0


static func _distance(left: Vector2i, right: Vector2i) -> int:
	return absi(left.x - right.x) + absi(left.y - right.y)

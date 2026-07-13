class_name Stage9ProofWorld
extends RefCounted
## Deterministic legal-safe relic-ring objective and match outcome proof.

const AudioRouterScript = preload("res://src/proof_stage9/audio_event_router.gd")
const TEAM_BLUE: int = 0
const TEAM_RED: int = 1
const TEAM_COUNT: int = 2
const WIDTH: int = 20
const HEIGHT: int = 13
const OBJECTIVE_KEYS: Array[String] = ["claimRadiusCells", "displayName", "dropOnHolderDefeat", "enabledByDefault", "holderDamagePermille", "holderSpeedPermille", "reclaimDelayTicks", "revealHolder", "spawnCell", "victoryHoldTicks"]

var tick_index: int = 0
var units: Dictionary = {}
var ring: Dictionary = {}
var stronghold_health: Array[int] = [1000, 1000]
var winner: int = -1
var loser: int = -1
var victory_reason: String = ""
var objective_enabled: bool = true
var music_state: String = "explore"
var audio: RefCounted = AudioRouterScript.new()
var rules: Dictionary = {}
var _next_unit_id: int = 1


func setup(document: Dictionary) -> String:
	var error: String = _configure(document)
	if error != "":
		return error
	reset()
	return ""


func reset() -> void:
	tick_index = 0
	units.clear()
	stronghold_health = [1000, 1000]
	winner = -1
	loser = -1
	victory_reason = ""
	objective_enabled = bool(rules["enabledByDefault"])
	music_state = "explore"
	_next_unit_id = 1
	var spawn: Array = rules["spawnCell"]
	ring = {
		"state": "dormant",
		"position": Vector2i(int(spawn[0]), int(spawn[1])),
		"holder_id": 0,
		"last_holder_team": -1,
		"spawned_tick": -1,
		"claimed_tick": -1,
		"reclaim_available_tick": 0,
		"ticks_held": 0,
		"claim_count": 0,
	}
	audio.reset()
	audio.route("music_explore", tick_index)


func set_objective_enabled(enabled: bool) -> Dictionary:
	if winner != -1:
		return {"ok": false, "reason": "match_resolved"}
	if objective_enabled == enabled:
		return {"ok": true, "reason": "", "enabled": enabled}
	if not enabled and String(ring.get("state", "")) == "held":
		_drop_ring(int(ring["holder_id"]))
	objective_enabled = enabled
	if not enabled:
		var spawn: Array = rules["spawnCell"]
		ring["state"] = "dormant"
		ring["position"] = Vector2i(int(spawn[0]), int(spawn[1]))
		ring["holder_id"] = 0
		ring["ticks_held"] = 0
		ring["claim_count"] = 0
		ring["spawned_tick"] = -1
		ring["claimed_tick"] = -1
		ring["reclaim_available_tick"] = 0
		music_state = "explore"
		audio.route("music_explore", tick_index)
	return {"ok": true, "reason": "", "enabled": enabled}


func add_unit(team: int, cell: Vector2i, maximum_health: int = 500, base_damage: int = 100) -> int:
	if team < 0 or team >= TEAM_COUNT or not contains_cell(cell) or maximum_health <= 0 or base_damage <= 0:
		return 0
	var id: int = _next_unit_id
	_next_unit_id += 1
	units[id] = {
		"id": id,
		"team": team,
		"cell": cell,
		"health": maximum_health,
		"maximum_health": maximum_health,
		"base_damage": base_damage,
		"alive": true,
		"order": "hold",
		"destination": cell,
		"movement_credit": 0,
	}
	return id


func entity(entity_id: int) -> Dictionary:
	return units.get(entity_id, {})


func entity_ids() -> Array[int]:
	var result: Array[int] = []
	for key: Variant in units.keys():
		result.append(int(key))
	result.sort()
	return result


func spawn_ring() -> Dictionary:
	if not objective_enabled:
		return {"ok": false, "reason": "objective_disabled"}
	if String(ring["state"]) != "dormant" or winner != -1:
		return {"ok": false, "reason": "ring_already_spawned"}
	ring["state"] = "spawned"
	ring["spawned_tick"] = tick_index
	audio.route("spawn", tick_index)
	music_state = "contest"
	audio.route("music_contest", tick_index)
	return {"ok": true, "reason": "", "position": ring["position"]}


func claim_ring(entity_id: int) -> Dictionary:
	var unit: Dictionary = entity(entity_id)
	if unit.is_empty() or not bool(unit.get("alive", false)):
		return {"ok": false, "reason": "invalid_holder"}
	var state: String = String(ring["state"])
	if not ["spawned", "dropped"].has(state):
		return {"ok": false, "reason": "ring_not_claimable"}
	if state == "dropped" and tick_index < int(ring["reclaim_available_tick"]):
		return {"ok": false, "reason": "reclaim_delay"}
	if distance_cells(Vector2i(unit["cell"]), Vector2i(ring["position"])) > int(rules["claimRadiusCells"]):
		return {"ok": false, "reason": "holder_out_of_range"}
	var kind: String = "claim" if int(ring["claim_count"]) == 0 else "reclaim"
	ring["state"] = "held"
	ring["holder_id"] = entity_id
	ring["last_holder_team"] = int(unit["team"])
	ring["claimed_tick"] = tick_index
	ring["ticks_held"] = 0
	ring["claim_count"] = int(ring["claim_count"]) + 1
	audio.route(kind, tick_index, int(unit["team"]), entity_id)
	return {"ok": true, "reason": "", "kind": kind, "holder_id": entity_id}


func order_move(entity_id: int, destination: Vector2i) -> bool:
	var unit: Dictionary = entity(entity_id)
	if unit.is_empty() or not bool(unit.get("alive", false)) or not contains_cell(destination):
		return false
	unit["destination"] = destination
	unit["order"] = "move"
	return true


func effective_speed_permille(entity_id: int) -> int:
	return int(rules["holderSpeedPermille"]) if int(ring.get("holder_id", 0)) == entity_id and String(ring.get("state", "")) == "held" else 1000


func effective_damage(entity_id: int) -> int:
	var unit: Dictionary = entity(entity_id)
	if unit.is_empty():
		return 0
	var multiplier: int = int(rules["holderDamagePermille"]) if int(ring.get("holder_id", 0)) == entity_id and String(ring.get("state", "")) == "held" else 1000
	return floori(float(int(unit["base_damage"]) * multiplier) / 1000.0)


func damage_unit(target_id: int, source_id: int) -> Dictionary:
	var target: Dictionary = entity(target_id)
	var source: Dictionary = entity(source_id)
	if target.is_empty() or source.is_empty() or not bool(target.get("alive", false)) or not bool(source.get("alive", false)) or int(target["team"]) == int(source["team"]):
		return {"ok": false, "reason": "invalid_combat", "damage": 0}
	var damage: int = effective_damage(source_id)
	var old: int = int(target["health"])
	target["health"] = maxi(0, old - damage)
	audio.route("hit", tick_index, int(target["team"]), target_id)
	if int(target["health"]) == 0:
		target["alive"] = false
		target["order"] = "defeated"
		if int(ring.get("holder_id", 0)) == target_id and bool(rules["dropOnHolderDefeat"]):
			_drop_ring(target_id)
	return {"ok": true, "reason": "", "damage": old - int(target["health"]), "defeated": not bool(target["alive"])}


func destroy_stronghold(team: int) -> Dictionary:
	if team < 0 or team >= TEAM_COUNT or winner != -1:
		return {"ok": false, "reason": "invalid_stronghold"}
	stronghold_health[team] = 0
	var holder: Dictionary = entity(int(ring.get("holder_id", 0)))
	if not holder.is_empty() and int(holder.get("team", -1)) == team:
		_drop_ring(int(holder["id"]))
	_set_victory(1 - team, team, "stronghold")
	return {"ok": true, "reason": "", "winner": winner, "loser": loser}


func tick() -> void:
	if winner != -1:
		return
	for id: int in entity_ids():
		var unit: Dictionary = units[id]
		if not bool(unit["alive"]) or String(unit["order"]) != "move":
			continue
		unit["movement_credit"] = int(unit["movement_credit"]) + effective_speed_permille(id)
		if int(unit["movement_credit"]) < 1000:
			continue
		unit["movement_credit"] = int(unit["movement_credit"]) - 1000
		var current: Vector2i = unit["cell"]
		var destination: Vector2i = unit["destination"]
		if current.x != destination.x:
			current.x += signi(destination.x - current.x)
		elif current.y != destination.y:
			current.y += signi(destination.y - current.y)
		unit["cell"] = current
		if current == destination:
			unit["order"] = "hold"
	if String(ring["state"]) == "held":
		var holder: Dictionary = entity(int(ring["holder_id"]))
		if holder.is_empty() or not bool(holder.get("alive", false)):
			_drop_ring(int(ring["holder_id"]))
		else:
			ring["position"] = holder["cell"]
			ring["ticks_held"] = int(ring["ticks_held"]) + 1
			if int(ring["ticks_held"]) >= int(rules["victoryHoldTicks"]):
				_set_victory(int(holder["team"]), 1 - int(holder["team"]), "ring_hold")
	tick_index += 1


func advance(ticks: int) -> void:
	for _index: int in range(maxi(0, ticks)):
		tick()


func contains_cell(cell: Vector2i) -> bool:
	return cell.x >= 0 and cell.x < WIDTH and cell.y >= 0 and cell.y < HEIGHT


func distance_cells(a: Vector2i, b: Vector2i) -> int:
	return absi(a.x - b.x) + absi(a.y - b.y)


func snapshot() -> Dictionary:
	var unit_rows: Array[Dictionary] = []
	for id: int in entity_ids():
		var unit: Dictionary = units[id]
		var cell: Vector2i = unit["cell"]
		var destination: Vector2i = unit["destination"]
		unit_rows.append({"id": id, "team": int(unit["team"]), "cell": [cell.x, cell.y], "health": int(unit["health"]), "maximum_health": int(unit["maximum_health"]), "base_damage": int(unit["base_damage"]), "alive": bool(unit["alive"]), "order": String(unit["order"]), "destination": [destination.x, destination.y], "movement_credit": int(unit["movement_credit"]), "effective_speed_permille": effective_speed_permille(id), "effective_damage": effective_damage(id)})
	var ring_position: Vector2i = ring["position"]
	return {
		"rules_version": 1,
		"authority": "gdscript-proof",
		"tick": tick_index,
		"next_unit_id": _next_unit_id,
		"winner": winner,
		"loser": loser,
		"victory_reason": victory_reason,
		"objective_enabled": objective_enabled,
		"music_state": music_state,
		"stronghold_health": stronghold_health.duplicate(),
		"rules": rules.duplicate(true),
		"ring": {"state": String(ring["state"]), "position": [ring_position.x, ring_position.y], "holder_id": int(ring["holder_id"]), "last_holder_team": int(ring["last_holder_team"]), "spawned_tick": int(ring["spawned_tick"]), "claimed_tick": int(ring["claimed_tick"]), "reclaim_available_tick": int(ring["reclaim_available_tick"]), "ticks_held": int(ring["ticks_held"]), "claim_count": int(ring["claim_count"])},
		"units": unit_rows,
		"audio": audio.snapshot(),
	}


func state_hash() -> int:
	var value: int = 0x811C9DC5
	for byte: int in JSON.stringify(snapshot()).to_utf8_buffer():
		value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
	return value


func state_hash_text() -> String:
	return "%08X" % state_hash()


func validate_state() -> String:
	if not ["dormant", "spawned", "held", "dropped"].has(String(ring.get("state", ""))):
		return "invalid_ring_state"
	if String(ring["state"]) == "held":
		var holder: Dictionary = entity(int(ring["holder_id"]))
		if holder.is_empty() or not bool(holder.get("alive", false)) or Vector2i(holder["cell"]) != Vector2i(ring["position"]):
			return "invalid_ring_holder"
	for id: int in entity_ids():
		var unit: Dictionary = units[id]
		if not contains_cell(Vector2i(unit["cell"])) or bool(unit["alive"]) != (int(unit["health"]) > 0):
			return "invalid_unit"
	if (winner == -1) != (loser == -1):
		return "invalid_outcome"
	return ""


func _drop_ring(holder_id: int) -> void:
	var holder: Dictionary = entity(holder_id)
	if not holder.is_empty():
		ring["position"] = holder["cell"]
	ring["state"] = "dropped"
	ring["holder_id"] = 0
	ring["ticks_held"] = 0
	ring["reclaim_available_tick"] = tick_index + int(rules["reclaimDelayTicks"])
	audio.route("drop", tick_index, int(holder.get("team", -1)), holder_id)


func _set_victory(winning_team: int, losing_team: int, reason: String) -> void:
	if winner != -1:
		return
	winner = winning_team
	loser = losing_team
	victory_reason = reason
	audio.route("victory", tick_index, winning_team)
	audio.route("loss", tick_index, losing_team)
	music_state = "victory"
	audio.route("music_victory", tick_index, winning_team)
	audio.route("music_defeat", tick_index, losing_team)


func _configure(document: Dictionary) -> String:
	var top: Array[String] = ["audioEvents", "objective", "rulesVersion", "schema", "schemaVersion"]
	if not _exact_keys(document, top) or String(document.get("schema", "")) != "openbfme.relic-ring-objective" or int(document.get("schemaVersion", -1)) != 0 or int(document.get("rulesVersion", -1)) != 1:
		return "invalid_ring_document"
	var objective: Dictionary = document.get("objective", {})
	if not _exact_keys(objective, OBJECTIVE_KEYS):
		return "invalid_objective_fields"
	var spawn: Array = objective.get("spawnCell", [])
	if spawn.size() != 2 or typeof(objective.get("enabledByDefault", null)) != TYPE_BOOL or int(objective.get("victoryHoldTicks", 0)) < 1 or int(objective.get("reclaimDelayTicks", -1)) < 0 or int(objective.get("holderSpeedPermille", 0)) < 1 or int(objective.get("holderDamagePermille", 0)) < 1:
		return "invalid_objective_values"
	var error: String = audio.configure(document.get("audioEvents", {}))
	if error != "":
		return error
	rules = objective.duplicate(true)
	return ""


static func _exact_keys(value: Dictionary, expected: Array[String]) -> bool:
	if value.size() != expected.size(): return false
	for key: String in expected:
		if not value.has(key): return false
	return true

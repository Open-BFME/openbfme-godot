class_name Stage5ProofWorld
extends RefCounted
## Self-contained deterministic Stage 5 spellbook and power-effect proof world.

const SpellbookSystemScript = preload("res://src/proof_stage5/spellbook_system.gd")
const EffectSystemScript = preload("res://src/proof_stage5/effect_system.gd")

const TEAM_BLUE: int = 0
const TEAM_RED: int = 1
const FNV_OFFSET: int = 2166136261
const FNV_PRIME: int = 16777619
const COMMAND_KINDS: Array[String] = ["experience", "grant_points", "unlock", "cast"]

var width: int
var height: int
var tick_index: int = 0
var entities: Dictionary = {}
var weather: Dictionary = {}
var team_states: Array[Dictionary] = []
var definition_document: Dictionary = {}
var spellbook: RefCounted
var effects: RefCounted

var _commands: Array[Dictionary] = []
var _next_command_index: int = 0
var _next_command_sequence: int = 0
var _next_unit_id: int = 100
var _next_building_id: int = 1000
var _configured: bool = false


func _init(p_width: int = 16, p_height: int = 10) -> void:
	assert(p_width >= 8 and p_height >= 6)
	width = p_width
	height = p_height
	spellbook = SpellbookSystemScript.new()
	effects = EffectSystemScript.new()


func setup(document: Dictionary) -> String:
	var error: String = configure(document)
	if error != "":
		return error
	reset()
	return ""


func configure(document: Dictionary) -> String:
	var error: String = spellbook.configure(document)
	if error != "":
		return error
	definition_document = document.duplicate(true)
	_configured = true
	return ""


func reset() -> void:
	assert(_configured)
	tick_index = 0
	entities.clear()
	weather.clear()
	team_states = [spellbook.new_team_state(), spellbook.new_team_state()]
	_commands.clear()
	_next_command_index = 0
	_next_command_sequence = 0
	_next_unit_id = 100
	_next_building_id = 1000


func add_unit(team: int, position: Vector2i, maximum_health: int = 1000, health: int = -1, forced_id: int = 0) -> int:
	if not _valid_team(team) or not contains_cell(position) or maximum_health <= 0:
		return 0
	var entity_id: int = forced_id if forced_id > 0 else _next_unit_id
	if entity_id <= 0 or entities.has(entity_id):
		return 0
	_next_unit_id = maxi(_next_unit_id, entity_id + 1)
	entities[entity_id] = {
		"id": entity_id,
		"kind": "unit",
		"team": team,
		"position": position,
		"health": clampi(maximum_health if health < 0 else health, 0, maximum_health),
		"max_health": maximum_health,
	}
	return entity_id


func add_building(team: int, position: Vector2i, maximum_health: int = 2000, health: int = -1, forced_id: int = 0) -> int:
	if not _valid_team(team) or not contains_cell(position) or maximum_health <= 0:
		return 0
	var entity_id: int = forced_id if forced_id > 0 else _next_building_id
	if entity_id <= 0 or entities.has(entity_id):
		return 0
	_next_building_id = maxi(_next_building_id, entity_id + 1)
	entities[entity_id] = {
		"id": entity_id,
		"kind": "building",
		"team": team,
		"position": position,
		"health": clampi(maximum_health if health < 0 else health, 0, maximum_health),
		"max_health": maximum_health,
	}
	return entity_id


func entity(entity_id: int) -> Dictionary:
	return entities.get(entity_id, {})


func entity_ids() -> Array[int]:
	var ids: Array[int] = []
	for raw_id: Variant in entities.keys():
		ids.append(int(raw_id))
	ids.sort()
	return ids


func entity_at(position: Vector2i, preferred_kind: String = "", preferred_team: int = -1) -> int:
	for entity_id: int in entity_ids():
		var row: Dictionary = entity(entity_id)
		if Vector2i(row.get("position", Vector2i(-1, -1))) != position or int(row.get("health", 0)) <= 0:
			continue
		if preferred_kind != "" and String(row.get("kind", "")) != preferred_kind:
			continue
		if preferred_team >= 0 and int(row.get("team", -1)) != preferred_team:
			continue
		return entity_id
	return 0


func team_state(team: int) -> Dictionary:
	return team_states[team] if _valid_team(team) else {}


func power_definitions() -> Array[Dictionary]:
	return spellbook.definitions.duplicate(true)


func power_definition(power_code: int) -> Dictionary:
	return spellbook.definition(power_code)


func award_spellbook_experience(team: int, amount: int) -> Dictionary:
	if not _valid_team(team):
		return {"ok": false, "reason": "invalid_team"}
	return spellbook.award_experience(team_states[team], amount)


func grant_power_points(team: int, amount: int) -> Dictionary:
	if not _valid_team(team):
		return {"ok": false, "reason": "invalid_team"}
	return spellbook.grant_points(team_states[team], amount)


func unlock_power(team: int, power_code: int) -> Dictionary:
	if not _valid_team(team):
		return {"ok": false, "reason": "invalid_team"}
	return spellbook.unlock(team_states[team], power_code)


func is_power_unlocked(team: int, power_code: int) -> bool:
	return _valid_team(team) and spellbook.is_unlocked(team_states[team], power_code)


func cooldown_remaining(team: int, power_code: int) -> int:
	return spellbook.cooldown_remaining(team_states[team], power_code, tick_index) if _valid_team(team) else 0


func cast_power(team: int, power_code: int, target_spec: Dictionary = {}) -> Dictionary:
	if not _valid_team(team):
		return {"ok": false, "reason": "invalid_team"}
	var power: Dictionary = power_definition(power_code)
	if power.is_empty():
		return {"ok": false, "reason": "unknown_power"}
	if not is_power_unlocked(team, power_code):
		return {"ok": false, "reason": "power_locked"}
	var remaining: int = cooldown_remaining(team, power_code)
	if remaining > 0:
		return {"ok": false, "reason": "cooldown", "remaining_ticks": remaining}
	var target: Dictionary = effects.resolve_target(self, team, power, target_spec)
	if not bool(target.get("ok", false)):
		return target
	var result: Dictionary = effects.apply(self, team, power, target)
	var ready_tick: int = spellbook.start_cooldown(team_states[team], power_code, tick_index)
	result["ok"] = true
	result["reason"] = ""
	result["power_code"] = power_code
	result["ready_tick"] = ready_tick
	return result


func apply_heal(entity_id: int, amount: int) -> int:
	var target: Dictionary = entity(entity_id)
	if target.is_empty() or amount <= 0 or int(target.get("health", 0)) <= 0:
		return 0
	var prior: int = int(target["health"])
	target["health"] = mini(int(target["max_health"]), prior + amount)
	return int(target["health"]) - prior


func apply_damage(entity_id: int, amount: int, source_team: int = -1) -> int:
	var target: Dictionary = entity(entity_id)
	if target.is_empty() or amount <= 0 or int(target.get("health", 0)) <= 0:
		return 0
	var prior: int = int(target["health"])
	target["health"] = maxi(0, prior - amount)
	if int(target["health"]) == 0 and _valid_team(source_team) and source_team != int(target["team"]):
		var reward: int = spellbook.building_defeat_power_points if String(target["kind"]) == "building" else spellbook.unit_defeat_power_points
		grant_power_points(source_team, reward)
	return prior - int(target["health"])


func activate_weather(team: int, effect: Dictionary) -> void:
	weather = {
		"code": String(effect["weatherCode"]),
		"owner": team,
		"remaining_ticks": int(effect["durationTicks"]),
		"unit_damage_per_tick": int(effect["unitDamagePerTick"]),
		"building_damage_per_tick": int(effect["buildingDamagePerTick"]),
	}


func schedule_command(kind: String, team: int, execute_tick: int, sequence: int = -1, power_code: int = 0, target_spec: Dictionary = {}, amount: int = 0) -> bool:
	_compact_consumed_commands()
	if not COMMAND_KINDS.has(kind) or not _valid_team(team) or execute_tick < tick_index or _commands.size() >= spellbook.maximum_queued_commands:
		return false
	var actual_sequence: int = _next_command_sequence if sequence < 0 else sequence
	if actual_sequence < 0:
		return false
	_next_command_sequence = maxi(_next_command_sequence, actual_sequence + 1)
	_commands.append({
		"execute_tick": execute_tick,
		"sequence": actual_sequence,
		"kind": kind,
		"team": team,
		"power_code": power_code,
		"target_spec": target_spec.duplicate(true),
		"amount": amount,
	})
	_commands.sort_custom(_command_before)
	return true


func tick() -> void:
	_apply_commands()
	_advance_weather()
	tick_index += 1


func advance(ticks: int) -> void:
	for _index: int in range(maxi(0, ticks)):
		tick()


func contains_cell(cell: Vector2i) -> bool:
	return cell.x >= 0 and cell.x < width and cell.y >= 0 and cell.y < height


func snapshot() -> Dictionary:
	var rows: Array[Dictionary] = []
	for entity_id: int in entity_ids():
		rows.append(entity(entity_id).duplicate(true))
	return {
		"tick": tick_index,
		"hash": state_hash_text(),
		"entities": rows,
		"teams": team_states.duplicate(true),
		"weather": weather.duplicate(true),
	}


func validate_state() -> String:
	if not _configured or team_states.size() != spellbook.team_count:
		return "world_configuration"
	for team: int in range(team_states.size()):
		var state_error: String = spellbook.validate_team_state(team_states[team])
		if state_error != "":
			return "%s_team_%d" % [state_error, team]
	var maximum_unit_id: int = 0
	var maximum_building_id: int = 0
	for entity_id: int in entity_ids():
		var row: Dictionary = entity(entity_id)
		if int(row.get("id", 0)) != entity_id or not _valid_team(int(row.get("team", -1))) or not contains_cell(Vector2i(row.get("position", Vector2i(-1, -1)))):
			return "entity_identity_%d" % entity_id
		var maximum_health: int = int(row.get("max_health", 0))
		var health: int = int(row.get("health", -1))
		if maximum_health <= 0 or health < 0 or health > maximum_health:
			return "entity_health_%d" % entity_id
		match String(row.get("kind", "")):
			"unit": maximum_unit_id = maxi(maximum_unit_id, entity_id)
			"building": maximum_building_id = maxi(maximum_building_id, entity_id)
			_: return "entity_kind_%d" % entity_id
	if _next_unit_id <= maximum_unit_id or _next_building_id <= maximum_building_id:
		return "entity_next_id"
	if not weather.is_empty():
		if not _valid_team(int(weather.get("owner", -1))) or String(weather.get("code", "")) == "" or int(weather.get("remaining_ticks", 0)) <= 0 or int(weather.get("unit_damage_per_tick", -1)) < 0 or int(weather.get("building_damage_per_tick", -1)) < 0:
			return "weather_state"
	if _next_command_index < 0 or _next_command_index > _commands.size() or _next_command_sequence < 0:
		return "command_cursor"
	for index: int in range(1, _commands.size()):
		if _command_before(_commands[index], _commands[index - 1]):
			return "command_order"
	return ""


func state_hash() -> int:
	var hash: int = FNV_OFFSET
	for value: int in [tick_index, width, height, _next_unit_id, _next_building_id, _next_command_sequence]:
		hash = _hash_int(hash, value)
	hash = _hash_variant(hash, definition_document)
	hash = _hash_variant(hash, spellbook.definitions)
	hash = _hash_variant(hash, team_states)
	hash = _hash_int(hash, _commands.size() - _next_command_index)
	for index: int in range(_next_command_index, _commands.size()):
		hash = _hash_variant(hash, _commands[index])
	var ids: Array[int] = entity_ids()
	hash = _hash_int(hash, ids.size())
	for entity_id: int in ids:
		hash = _hash_variant(hash, entity(entity_id))
	hash = _hash_variant(hash, weather)
	return hash & 0xffffffff


func state_hash_text() -> String:
	return "%08X" % state_hash()


func _apply_commands() -> void:
	while _next_command_index < _commands.size() and int(_commands[_next_command_index]["execute_tick"]) <= tick_index:
		var command: Dictionary = _commands[_next_command_index]
		_next_command_index += 1
		match String(command["kind"]):
			"experience": award_spellbook_experience(int(command["team"]), int(command["amount"]))
			"grant_points": grant_power_points(int(command["team"]), int(command["amount"]))
			"unlock": unlock_power(int(command["team"]), int(command["power_code"]))
			"cast": cast_power(int(command["team"]), int(command["power_code"]), command["target_spec"])


func _compact_consumed_commands() -> void:
	if _next_command_index <= 0:
		return
	var pending: Array[Dictionary] = []
	for index: int in range(_next_command_index, _commands.size()):
		pending.append(_commands[index])
	_commands = pending
	_next_command_index = 0


func _advance_weather() -> void:
	if weather.is_empty():
		return
	var owner: int = int(weather["owner"])
	for entity_id: int in entity_ids():
		var row: Dictionary = entity(entity_id)
		if int(row.get("team", -1)) == owner or int(row.get("health", 0)) <= 0:
			continue
		var damage: int = int(weather["building_damage_per_tick"]) if String(row.get("kind", "")) == "building" else int(weather["unit_damage_per_tick"])
		apply_damage(entity_id, damage, owner)
	weather["remaining_ticks"] = int(weather["remaining_ticks"]) - 1
	if int(weather["remaining_ticks"]) <= 0:
		weather.clear()


func _command_before(left: Dictionary, right: Dictionary) -> bool:
	for key: String in ["execute_tick", "sequence"]:
		var left_value: int = int(left[key])
		var right_value: int = int(right[key])
		if left_value != right_value:
			return left_value < right_value
	var left_kind: String = String(left["kind"])
	var right_kind: String = String(right["kind"])
	if left_kind != right_kind:
		return left_kind < right_kind
	if int(left["team"]) != int(right["team"]):
		return int(left["team"]) < int(right["team"])
	return int(left["power_code"]) < int(right["power_code"])


func _valid_team(team: int) -> bool:
	return team >= 0 and team < 2


func _hash_int(hash: int, value: int) -> int:
	var result: int = hash
	for shift: int in [0, 8, 16, 24]:
		result = _hash_byte(result, value >> shift)
	return result


func _hash_string(hash: int, value: String) -> int:
	var result: int = _hash_int(hash, value.length())
	for byte: int in value.to_utf8_buffer():
		result = _hash_byte(result, byte)
	return result


func _hash_vector(hash: int, value: Vector2i) -> int:
	return _hash_int(_hash_int(hash, value.x), value.y)


func _hash_variant(hash: int, value: Variant) -> int:
	var result: int = _hash_int(hash, typeof(value))
	match typeof(value):
		TYPE_NIL:
			return result
		TYPE_BOOL:
			return _hash_int(result, 1 if bool(value) else 0)
		TYPE_INT:
			return _hash_int(result, int(value))
		TYPE_FLOAT:
			return _hash_string(result, str(value))
		TYPE_STRING, TYPE_STRING_NAME:
			return _hash_string(result, str(value))
		TYPE_VECTOR2I:
			var vector: Vector2i = value
			return _hash_vector(result, vector)
		TYPE_ARRAY:
			var values: Array = value
			result = _hash_int(result, values.size())
			for item: Variant in values:
				result = _hash_variant(result, item)
			return result
		TYPE_DICTIONARY:
			var dictionary: Dictionary = value
			var keys: Array = dictionary.keys()
			keys.sort_custom(func(left: Variant, right: Variant) -> bool: return str(left) < str(right))
			result = _hash_int(result, keys.size())
			for key: Variant in keys:
				result = _hash_string(result, str(key))
				result = _hash_variant(result, dictionary[key])
			return result
		_:
			return _hash_string(result, var_to_str(value))


func _hash_byte(hash: int, value: int) -> int:
	return int(((hash ^ (value & 0xff)) * FNV_PRIME) & 0xffffffff)

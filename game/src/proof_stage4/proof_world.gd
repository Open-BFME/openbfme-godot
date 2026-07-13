class_name Stage4ProofWorld
extends RefCounted
## Isolated deterministic Stage 4 proof world with neutral placeholder entities.

const AbilitySystemScript = preload("res://src/proof_stage4/ability_system.gd")
const ProgressionSystemScript = preload("res://src/proof_stage4/progression_system.gd")
const StatusSystemScript = preload("res://src/proof_stage4/status_system.gd")

const TEAM_BLUE: int = 0
const TEAM_RED: int = 1
const TEAM_COUNT: int = 2
const REVIVAL_KEYS: Array[String] = [
	"base_cost",
	"death_cost",
	"rank_cost",
	"revive_health_permille",
]

var width: int
var height: int
var tick_index: int = 0
var entities: Dictionary = {}
var resources: Array[int] = [2000, 2000]
var death_records: Array[Dictionary] = [{}, {}]
var death_counts: Array[int] = [0, 0]

var ability_system: RefCounted
var progression_system: RefCounted
var status_system: RefCounted

var _blocked: Dictionary = {}
var _next_entity_id: int = 1
var _next_member_id: int = 1000
var _configured: bool = false
var _revival_definition: Dictionary = {}
var _spawn_cells: Array[Vector2i]


func _init(p_width: int = 16, p_height: int = 12) -> void:
	assert(p_width >= 8 and p_height >= 6)
	width = p_width
	height = p_height
	_spawn_cells = [Vector2i(2, height / 2), Vector2i(width - 3, height / 2)]
	ability_system = AbilitySystemScript.new()
	progression_system = ProgressionSystemScript.new()
	status_system = StatusSystemScript.new()


func setup_default(ability_definitions: Array[Dictionary], progression_definition: Dictionary, status_definition: Dictionary, revival_definition: Dictionary) -> String:
	var error: String = configure(ability_definitions, progression_definition, status_definition, revival_definition)
	if error != "":
		return error
	reset_default()
	return ""


func configure(ability_definitions: Array[Dictionary], progression_definition: Dictionary, status_definition: Dictionary, revival_definition: Dictionary) -> String:
	var error: String = ability_system.configure(ability_definitions)
	if error != "":
		return error
	error = progression_system.configure(progression_definition)
	if error != "":
		return error
	error = status_system.configure(status_definition)
	if error != "":
		return error
	error = _validate_revival_definition(revival_definition)
	if error != "":
		return error
	_revival_definition = revival_definition.duplicate(true)
	_configured = true
	return ""


func reset_default() -> void:
	assert(_configured)
	tick_index = 0
	entities.clear()
	_blocked.clear()
	resources = [2000, 2000]
	death_records = [{}, {}]
	death_counts = [0, 0]
	_next_entity_id = 1
	_next_member_id = 1000
	_spawn_champion(TEAM_BLUE)
	_spawn_champion(TEAM_RED)


func entity(entity_id: int) -> Dictionary:
	if not entities.has(entity_id):
		return {}
	return entities[entity_id]


func champion_for(team: int, include_dead: bool = false) -> Dictionary:
	if not _valid_team(team):
		return {}
	for entity_id: int in entity_ids():
		var candidate: Dictionary = entities[entity_id]
		if String(candidate.get("kind", "")) == "champion" and int(candidate.get("team", -1)) == team:
			if include_dead or bool(candidate.get("alive", false)):
				return candidate
	return {}


func add_squad(team: int, position: Vector2i, member_count: int, member_max_health: int = 100) -> int:
	if not _valid_team(team) or not contains_cell(position) or is_blocked(position) or member_count <= 0 or member_max_health <= 0:
		return 0
	var squad_id: int = _allocate_entity_id()
	var members: Array[Dictionary] = []
	for _index: int in range(member_count):
		var member_id: int = _next_member_id
		_next_member_id += 1
		members.append({
			"id": member_id,
			"health": member_max_health,
			"max_health": member_max_health,
			"alive": true,
		})
	entities[squad_id] = {
		"id": squad_id,
		"kind": "squad",
		"display_name": "Neutral Cohort",
		"formation_size": member_count,
		"team": team,
		"position": position,
		"spawn_position": position,
		"alive": true,
		"health": 0,
		"max_health": 0,
		"xp": 0,
		"rank": 1,
		"stance": "balanced",
		"cooldowns": {},
		"active_toggles": {},
		"toggle_armor_bonus_permille": 0,
		"toggle_speed_penalty_permille": 0,
		"fear_immune": false,
		"fear_resistance": 20,
		"status": "normal",
		"flee_source_id": 0,
		"flee_direction": Vector2i.ZERO,
		"flee_until_tick": 0,
		"last_damage_tick": -1,
		"next_replenish_tick": 0,
		"members": members,
	}
	return squad_id


func entity_ids() -> Array[int]:
	var ids: Array[int] = []
	for key: Variant in entities.keys():
		ids.append(int(key))
	ids.sort()
	return ids


func cast_ability(caster_id: int, ability_code: int, target_spec: Dictionary = {}) -> Dictionary:
	return ability_system.cast(self, caster_id, ability_code, target_spec)


func award_xp(entity_id: int, amount: int) -> Dictionary:
	var target: Dictionary = entity(entity_id)
	if target.is_empty():
		return {"ok": false, "reason": "invalid_entity"}
	return progression_system.award_xp(target, amount)


func set_stance(entity_id: int, stance_code: String) -> Dictionary:
	var target: Dictionary = entity(entity_id)
	if target.is_empty():
		return {"ok": false, "reason": "invalid_entity"}
	return progression_system.set_stance(target, stance_code)


func configure_fear_profile(entity_id: int, resistance: int, immune: bool) -> Dictionary:
	var target: Dictionary = entity(entity_id)
	if target.is_empty() or resistance < 0:
		return {"ok": false, "reason": "invalid_fear_profile"}
	target["fear_resistance"] = resistance
	target["fear_immune"] = immune
	return {"ok": true, "reason": ""}


func apply_fear(source_id: int, target_id: int, power: int, duration_ticks: int, terror: bool = false) -> Dictionary:
	return status_system.apply_fear(self, source_id, target_id, power, duration_ticks, terror)


func fear_immunity_reason(entity_id: int) -> String:
	var target: Dictionary = entity(entity_id)
	return "" if target.is_empty() else String(status_system.immunity_reason(target))


func is_fear_immune(entity_id: int) -> bool:
	return fear_immunity_reason(entity_id) != ""


func apply_knockback(target_id: int, source_position: Vector2i, distance_cells: int) -> Dictionary:
	return status_system.apply_knockback(self, target_id, source_position, distance_cells)


func damage_entity(target_id: int, base_damage: int, source_id: int = 0) -> Dictionary:
	var target: Dictionary = entity(target_id)
	if target.is_empty() or not bool(target.get("alive", false)) or base_damage <= 0:
		return {"ok": false, "reason": "invalid_damage_target", "damage": 0}
	var source: Dictionary = entity(source_id)
	if source.is_empty():
		source = {"stance": "balanced"}
	var damage: int = progression_system.damage_after_stances(base_damage, source, target)
	if String(target.get("kind", "")) == "champion":
		var old_health: int = int(target["health"])
		target["health"] = maxi(0, old_health - damage)
		var applied: int = old_health - int(target["health"])
		_award_damage_xp(source_id, applied, int(target["team"]))
		if int(target["health"]) == 0:
			kill_champion(target_id, source_id)
		return {"ok": true, "reason": "", "damage": applied, "member_id": 0}
	if String(target.get("kind", "")) == "squad":
		var member: Dictionary = _lowest_living_member(target)
		if member.is_empty():
			return {"ok": false, "reason": "squad_defeated", "damage": 0}
		var old_member_health: int = int(member["health"])
		member["health"] = maxi(0, old_member_health - damage)
		var applied_member_damage: int = old_member_health - int(member["health"])
		_award_damage_xp(source_id, applied_member_damage, int(target["team"]))
		_mark_casualty(target)
		if int(member["health"]) == 0:
			member["alive"] = false
			_award_defeat_xp(source_id, progression_system.xp_per_member_defeat, int(target["team"]))
		target["alive"] = progression_system.living_member_count(target) > 0
		return {"ok": true, "reason": "", "damage": applied_member_damage, "member_id": int(member["id"])}
	return {"ok": false, "reason": "unsupported_target", "damage": 0}


func heal_entity(target_id: int, amount: int) -> int:
	var target: Dictionary = entity(target_id)
	if target.is_empty() or not bool(target.get("alive", false)) or String(target.get("kind", "")) != "champion" or amount <= 0:
		return 0
	var old_health: int = int(target["health"])
	target["health"] = mini(int(target["max_health"]), old_health + amount)
	return int(target["health"]) - old_health


func defeat_member(squad_id: int, member_id: int, source_id: int = 0) -> Dictionary:
	var squad: Dictionary = entity(squad_id)
	if squad.is_empty() or String(squad.get("kind", "")) != "squad":
		return {"ok": false, "reason": "invalid_squad"}
	for member_value: Variant in Array(squad.get("members", [])):
		var member: Dictionary = member_value
		if int(member.get("id", 0)) != member_id:
			continue
		if not bool(member.get("alive", false)):
			return {"ok": false, "reason": "member_already_defeated"}
		member["health"] = 0
		member["alive"] = false
		_mark_casualty(squad)
		_award_defeat_xp(source_id, progression_system.xp_per_member_defeat, int(squad["team"]))
		squad["alive"] = progression_system.living_member_count(squad) > 0
		return {"ok": true, "reason": ""}
	return {"ok": false, "reason": "member_missing"}


func replenish_squad(squad_id: int, member_count: int) -> Dictionary:
	var squad: Dictionary = entity(squad_id)
	if squad.is_empty():
		return {"ok": false, "reason": "invalid_squad", "restored_ids": []}
	var result: Dictionary = progression_system.replenish(squad, member_count)
	if bool(result.get("ok", false)):
		squad["next_replenish_tick"] = 0 if _dead_member_count(squad) == 0 else tick_index + progression_system.replenish_interval_ticks
	return result


func toggle_guard(entity_id: int, ability_code: int, armor_bonus_permille: int, speed_penalty_permille: int) -> Dictionary:
	var target: Dictionary = entity(entity_id)
	if target.is_empty() or String(target.get("kind", "")) != "champion" or not bool(target.get("alive", false)):
		return {"ok": false, "reason": "invalid_caster"}
	var active_toggles: Dictionary = target.get("active_toggles", {})
	var next_active: bool = not bool(active_toggles.get(ability_code, false))
	if next_active:
		active_toggles[ability_code] = true
		target["toggle_armor_bonus_permille"] = armor_bonus_permille
		target["toggle_speed_penalty_permille"] = speed_penalty_permille
	else:
		active_toggles.erase(ability_code)
		target["toggle_armor_bonus_permille"] = 0
		target["toggle_speed_penalty_permille"] = 0
	target["active_toggles"] = active_toggles
	return {"ok": true, "reason": "", "active": next_active, "armor_bonus_permille": int(target["toggle_armor_bonus_permille"]), "speed_penalty_permille": int(target["toggle_speed_penalty_permille"])}


func is_toggle_active(entity_id: int, ability_code: int) -> bool:
	var target: Dictionary = entity(entity_id)
	return not target.is_empty() and bool(Dictionary(target.get("active_toggles", {})).get(ability_code, false))


func living_member_ids(squad_id: int) -> Array[int]:
	var result: Array[int] = []
	var squad: Dictionary = entity(squad_id)
	if squad.is_empty() or String(squad.get("kind", "")) != "squad":
		return result
	for member_value: Variant in Array(squad.get("members", [])):
		var member: Dictionary = member_value
		if bool(member.get("alive", false)):
			result.append(int(member["id"]))
	result.sort()
	return result


func train_champion(team: int) -> Dictionary:
	if not _valid_team(team):
		return {"ok": false, "reason": "invalid_team"}
	var existing: Dictionary = champion_for(team, true)
	if not existing.is_empty():
		return {"ok": false, "reason": "champion_exists" if bool(existing.get("alive", false)) else "must_revive"}
	if not death_records[team].is_empty():
		return {"ok": false, "reason": "must_revive"}
	var champion: Dictionary = _spawn_champion(team)
	return {"ok": true, "reason": "", "champion_id": int(champion["id"])}


func kill_champion(champion_id: int, source_id: int = 0) -> Dictionary:
	var champion: Dictionary = entity(champion_id)
	if champion.is_empty() or String(champion.get("kind", "")) != "champion":
		return {"ok": false, "reason": "invalid_champion"}
	if not bool(champion.get("alive", false)):
		return {"ok": false, "reason": "already_dead"}
	var team: int = int(champion["team"])
	death_counts[team] += 1
	champion["alive"] = false
	champion["health"] = 0
	champion["status"] = "dead"
	champion["flee_source_id"] = 0
	champion["flee_direction"] = Vector2i.ZERO
	champion["flee_until_tick"] = 0
	champion["active_toggles"] = {}
	champion["toggle_armor_bonus_permille"] = 0
	champion["toggle_speed_penalty_permille"] = 0
	death_records[team] = {
		"team": team,
		"hero_id": champion_id,
		"rank": int(champion["rank"]),
		"xp": int(champion["xp"]),
		"death_count": death_counts[team],
		"last_death_tick": tick_index,
	}
	_award_defeat_xp(source_id, progression_system.xp_per_champion_defeat, team)
	return {"ok": true, "reason": "", "team": team, "revival_cost": revival_cost(team)}


func death_record(team: int) -> Dictionary:
	if not _valid_team(team):
		return {}
	return death_records[team].duplicate(true)


func revival_cost(team: int) -> int:
	if not _valid_team(team) or death_records[team].is_empty():
		return 0
	var record: Dictionary = death_records[team]
	return (
		int(_revival_definition["base_cost"])
		+ maxi(0, int(record["rank"]) - 1) * int(_revival_definition["rank_cost"])
		+ int(record["death_count"]) * int(_revival_definition["death_cost"])
	)


func revive_champion(team: int) -> Dictionary:
	if not _valid_team(team):
		return {"ok": false, "reason": "invalid_team"}
	var record: Dictionary = death_records[team]
	if record.is_empty():
		return {"ok": false, "reason": "no_death_record"}
	var champion: Dictionary = entity(int(record["hero_id"]))
	if champion.is_empty() or bool(champion.get("alive", false)) or int(champion.get("team", -1)) != team:
		return {"ok": false, "reason": "invalid_death_record"}
	var cost: int = revival_cost(team)
	if resources[team] < cost:
		return {"ok": false, "reason": "insufficient_resources", "cost": cost}
	resources[team] -= cost
	champion["alive"] = true
	champion["health"] = maxi(1, floori(float(int(champion["max_health"]) * int(_revival_definition["revive_health_permille"])) / 1000.0))
	champion["position"] = _spawn_cells[team]
	champion["status"] = "normal"
	champion["cooldowns"] = {}
	champion["active_toggles"] = {}
	champion["toggle_armor_bonus_permille"] = 0
	champion["toggle_speed_penalty_permille"] = 0
	champion["flee_source_id"] = 0
	champion["flee_direction"] = Vector2i.ZERO
	champion["flee_until_tick"] = 0
	death_records[team] = {}
	return {"ok": true, "reason": "", "champion_id": int(champion["id"]), "cost": cost, "health": int(champion["health"])}


func set_team_resources(team: int, amount: int) -> bool:
	if not _valid_team(team) or amount < 0:
		return false
	resources[team] = amount
	return true


func set_blocked(cell: Vector2i, blocked: bool = true) -> bool:
	if not contains_cell(cell):
		return false
	var key: String = _cell_key(cell)
	if blocked:
		_blocked[key] = true
	else:
		_blocked.erase(key)
	return true


func is_blocked(cell: Vector2i) -> bool:
	return not contains_cell(cell) or _blocked.has(_cell_key(cell))


func contains_cell(cell: Vector2i) -> bool:
	return cell.x >= 0 and cell.x < width and cell.y >= 0 and cell.y < height


func distance_cells(left: Vector2i, right: Vector2i) -> int:
	return absi(left.x - right.x) + absi(left.y - right.y)


func try_step_entity(entity_id: int, direction: Vector2i) -> bool:
	var target: Dictionary = entity(entity_id)
	if target.is_empty() or not bool(target.get("alive", false)):
		return false
	if absi(direction.x) + absi(direction.y) != 1:
		return false
	var destination: Vector2i = Vector2i(target["position"]) + direction
	if is_blocked(destination):
		return false
	target["position"] = destination
	return true


func move_entity_safely(entity_id: int, destination: Vector2i, maximum_steps: int) -> Dictionary:
	var target: Dictionary = entity(entity_id)
	if target.is_empty() or not bool(target.get("alive", false)):
		return {"ok": false, "reason": "invalid_entity", "moved_cells": 0}
	if not contains_cell(destination):
		return {"ok": false, "reason": "position_out_of_bounds", "moved_cells": 0}
	var origin: Vector2i = target["position"]
	var distance: int = distance_cells(origin, destination)
	if distance == 0:
		return {"ok": false, "reason": "already_at_position", "moved_cells": 0}
	var effective_maximum: int = floori(float(maximum_steps * progression_system.speed_permille(target)) / 1000.0)
	if effective_maximum < distance:
		return {"ok": false, "reason": "movement_limit", "moved_cells": 0}
	var path: Array[Vector2i] = _axis_path(origin, destination)
	for cell: Vector2i in path:
		if is_blocked(cell):
			return {"ok": false, "reason": "path_blocked", "moved_cells": 0}
	target["position"] = destination
	return {"ok": true, "reason": "", "moved_cells": distance, "destination": destination}


func tick() -> void:
	for entity_id: int in entity_ids():
		status_system.tick_entity(self, entities[entity_id])
	tick_index += 1
	_tick_replenishment()


func advance(ticks: int) -> void:
	for _index: int in range(maxi(0, ticks)):
		tick()


func snapshot() -> Dictionary:
	var entity_rows: Array[Dictionary] = []
	for entity_id: int in entity_ids():
		entity_rows.append(_entity_snapshot(entities[entity_id]))
	var blocked_cells: Array[Vector2i] = []
	for key: Variant in _blocked.keys():
		var pieces: PackedStringArray = String(key).split(",")
		blocked_cells.append(Vector2i(int(pieces[0]), int(pieces[1])))
	blocked_cells.sort_custom(func(left: Vector2i, right: Vector2i) -> bool: return left.y < right.y or (left.y == right.y and left.x < right.x))
	var blocked_rows: Array[Array] = []
	for cell: Vector2i in blocked_cells:
		blocked_rows.append([cell.x, cell.y])
	var record_rows: Array[Dictionary] = []
	for team: int in range(TEAM_COUNT):
		var record: Dictionary = death_records[team]
		if record.is_empty():
			record_rows.append({"team": team, "pending": false})
		else:
			record_rows.append({
				"team": team,
				"pending": true,
				"hero_id": int(record["hero_id"]),
				"rank": int(record["rank"]),
				"xp": int(record["xp"]),
				"death_count": int(record["death_count"]),
				"last_death_tick": int(record["last_death_tick"]),
				"revival_cost": revival_cost(team),
			})
	return {
		"rules_version": 4,
		"authority": "gdscript-proof",
		"width": width,
		"height": height,
		"tick": tick_index,
		"next_entity_id": _next_entity_id,
		"next_member_id": _next_member_id,
		"resources": resources.duplicate(),
		"death_counts": death_counts.duplicate(),
		"death_records": record_rows,
		"blocked_cells": blocked_rows,
		"abilities": ability_system.definitions_snapshot(),
		"progression": progression_system.definitions_snapshot(),
		"status_definition": status_system.definition_snapshot(),
		"revival_definition": _revival_definition.duplicate(true),
		"entities": entity_rows,
	}


func state_hash() -> int:
	var bytes: PackedByteArray = JSON.stringify(snapshot()).to_utf8_buffer()
	var value: int = 0x811C9DC5
	for byte: int in bytes:
		value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
	return value


func state_hash_text() -> String:
	return "%08X" % state_hash()


func validate_state() -> String:
	if not _configured:
		return "world not configured"
	var seen_entity_ids: Dictionary = {}
	var seen_member_ids: Dictionary = {}
	var champion_counts: Array[int] = [0, 0]
	for entity_id: int in entity_ids():
		if entity_id <= 0 or seen_entity_ids.has(entity_id):
			return "duplicate or invalid entity id"
		seen_entity_ids[entity_id] = true
		var row: Dictionary = entities[entity_id]
		var team: int = int(row.get("team", -1))
		if not _valid_team(team):
			return "entity has invalid team"
		if not contains_cell(Vector2i(row.get("position", Vector2i(-1, -1)))):
			return "entity outside map"
		if not progression_system.stance_definition(String(row.get("stance", ""))):
			return "entity has invalid stance"
		if int(row.get("rank", 0)) != progression_system.rank_for_xp(int(row.get("xp", -1))):
			return "entity rank does not match xp"
		if String(row.get("kind", "")) == "champion":
			champion_counts[team] += 1
			if bool(row.get("alive", false)) != (int(row.get("health", 0)) > 0):
				return "champion health/alive mismatch"
		elif String(row.get("kind", "")) == "squad":
			for member_value: Variant in Array(row.get("members", [])):
				var member: Dictionary = member_value
				var member_id: int = int(member.get("id", 0))
				if member_id <= 0 or seen_member_ids.has(member_id):
					return "duplicate or invalid member id"
				seen_member_ids[member_id] = true
				if bool(member.get("alive", false)) != (int(member.get("health", 0)) > 0):
					return "member health/alive mismatch"
			if bool(row.get("alive", false)) != (progression_system.living_member_count(row) > 0):
				return "squad alive/count mismatch"
		else:
			return "unknown entity kind"
	for team: int in range(TEAM_COUNT):
		if champion_counts[team] != 1:
			return "team %d must own exactly one champion record" % team
		if not death_records[team].is_empty() and int(death_records[team].get("team", -1)) != team:
			return "death record belongs to wrong team"
	return ""


func _mark_casualty(squad: Dictionary) -> void:
	squad["last_damage_tick"] = tick_index
	squad["next_replenish_tick"] = tick_index + progression_system.replenish_delay_ticks


func _dead_member_count(squad: Dictionary) -> int:
	var count: int = 0
	for member_value: Variant in Array(squad.get("members", [])):
		if not bool(Dictionary(member_value).get("alive", false)):
			count += 1
	return count


func _tick_replenishment() -> void:
	for entity_id: int in entity_ids():
		var squad: Dictionary = entities[entity_id]
		if String(squad.get("kind", "")) != "squad" or not bool(squad.get("alive", false)):
			continue
		var dead_count: int = _dead_member_count(squad)
		if dead_count == 0:
			squad["next_replenish_tick"] = 0
			continue
		var next_tick: int = int(squad.get("next_replenish_tick", 0))
		if next_tick <= 0:
			next_tick = tick_index + progression_system.replenish_delay_ticks
			squad["next_replenish_tick"] = next_tick
		if tick_index < next_tick:
			continue
		var result: Dictionary = progression_system.replenish(squad, progression_system.replenish_members_per_interval)
		if bool(result.get("ok", false)):
			squad["next_replenish_tick"] = 0 if _dead_member_count(squad) == 0 else tick_index + progression_system.replenish_interval_ticks


func _award_defeat_xp(source_id: int, amount: int, defeated_team: int) -> void:
	if source_id == 0 or amount <= 0:
		return
	var source: Dictionary = entity(source_id)
	if source.is_empty() or not bool(source.get("alive", false)) or int(source.get("team", -1)) == defeated_team:
		return
	progression_system.award_xp(source, amount)


func _award_damage_xp(source_id: int, applied_damage: int, target_team: int) -> void:
	var amount: int = floori(float(applied_damage * progression_system.xp_damage_permille) / 1000.0)
	_award_defeat_xp(source_id, amount, target_team)


func _spawn_champion(team: int) -> Dictionary:
	var champion_id: int = _allocate_entity_id()
	var champion: Dictionary = {
		"id": champion_id,
		"kind": "champion",
		"display_name": "Neutral Champion",
		"formation_size": 1,
		"team": team,
		"position": _spawn_cells[team],
		"spawn_position": _spawn_cells[team],
		"alive": true,
		"health": 1000,
		"max_health": 1000,
		"xp": 0,
		"rank": 1,
		"stance": "balanced",
		"cooldowns": {},
		"active_toggles": {},
		"toggle_armor_bonus_permille": 0,
		"toggle_speed_penalty_permille": 0,
		"fear_immune": false,
		"fear_resistance": 40,
		"status": "normal",
		"flee_source_id": 0,
		"flee_direction": Vector2i.ZERO,
		"flee_until_tick": 0,
		"last_damage_tick": -1,
		"next_replenish_tick": 0,
		"members": [],
	}
	entities[champion_id] = champion
	return champion


func _allocate_entity_id() -> int:
	var result: int = _next_entity_id
	_next_entity_id += 1
	return result


func _lowest_living_member(squad: Dictionary) -> Dictionary:
	var candidates: Array[Dictionary] = []
	for member_value: Variant in Array(squad.get("members", [])):
		var member: Dictionary = member_value
		if bool(member.get("alive", false)):
			candidates.append(member)
	candidates.sort_custom(func(left: Dictionary, right: Dictionary) -> bool: return int(left["id"]) < int(right["id"]))
	return {} if candidates.is_empty() else candidates[0]


func _axis_path(origin: Vector2i, destination: Vector2i) -> Array[Vector2i]:
	var result: Array[Vector2i] = []
	var cursor: Vector2i = origin
	while cursor.x != destination.x:
		cursor.x += signi(destination.x - cursor.x)
		result.append(cursor)
	while cursor.y != destination.y:
		cursor.y += signi(destination.y - cursor.y)
		result.append(cursor)
	return result


func _entity_snapshot(row: Dictionary) -> Dictionary:
	var cooldown_codes: Array[int] = []
	for key: Variant in Dictionary(row.get("cooldowns", {})).keys():
		cooldown_codes.append(int(key))
	cooldown_codes.sort()
	var cooldown_rows: Array[Array] = []
	for code: int in cooldown_codes:
		cooldown_rows.append([code, int(Dictionary(row["cooldowns"])[code])])
	var toggle_codes: Array[int] = []
	for key: Variant in Dictionary(row.get("active_toggles", {})).keys():
		if bool(Dictionary(row["active_toggles"])[key]):
			toggle_codes.append(int(key))
	toggle_codes.sort()
	var member_rows: Array[Dictionary] = []
	for member_value: Variant in Array(row.get("members", [])):
		var member: Dictionary = member_value
		member_rows.append({
			"id": int(member["id"]),
			"health": int(member["health"]),
			"max_health": int(member["max_health"]),
			"alive": bool(member["alive"]),
		})
	member_rows.sort_custom(func(left: Dictionary, right: Dictionary) -> bool: return int(left["id"]) < int(right["id"]))
	var position: Vector2i = row["position"]
	var spawn_position: Vector2i = row["spawn_position"]
	var flee_direction: Vector2i = row.get("flee_direction", Vector2i.ZERO)
	return {
		"id": int(row["id"]),
		"kind": String(row["kind"]),
		"display_name": String(row["display_name"]),
		"formation_size": int(row["formation_size"]),
		"team": int(row["team"]),
		"position": [position.x, position.y],
		"spawn_position": [spawn_position.x, spawn_position.y],
		"alive": bool(row["alive"]),
		"health": int(row["health"]),
		"max_health": int(row["max_health"]),
		"xp": int(row["xp"]),
		"rank": int(row["rank"]),
		"stance": String(row["stance"]),
		"cooldowns": cooldown_rows,
		"active_toggles": toggle_codes,
		"toggle_armor_bonus_permille": int(row.get("toggle_armor_bonus_permille", 0)),
		"toggle_speed_penalty_permille": int(row.get("toggle_speed_penalty_permille", 0)),
		"fear_immune": bool(row["fear_immune"]),
		"fear_resistance": int(row["fear_resistance"]),
		"status": String(row["status"]),
		"flee_source_id": int(row["flee_source_id"]),
		"flee_direction": [flee_direction.x, flee_direction.y],
		"flee_until_tick": int(row["flee_until_tick"]),
		"last_damage_tick": int(row.get("last_damage_tick", -1)),
		"next_replenish_tick": int(row.get("next_replenish_tick", 0)),
		"members": member_rows,
	}


func _validate_revival_definition(definition: Dictionary) -> String:
	var key_error: String = _require_exact_keys(definition, REVIVAL_KEYS, "revival")
	if key_error != "":
		return key_error
	for field: String in REVIVAL_KEYS:
		if not _is_integer_value(definition[field]):
			return "revival.%s must be an integer" % field
	if int(definition["base_cost"]) < 0 or int(definition["rank_cost"]) < 0 or int(definition["death_cost"]) < 0:
		return "revival costs must not be negative"
	if int(definition["revive_health_permille"]) < 1 or int(definition["revive_health_permille"]) > 1000:
		return "revival.revive_health_permille must be in 1..1000"
	return ""


func _valid_team(team: int) -> bool:
	return team >= 0 and team < TEAM_COUNT


func _cell_key(cell: Vector2i) -> String:
	return "%d,%d" % [cell.x, cell.y]


static func _require_exact_keys(value: Dictionary, expected: Array[String], path: String) -> String:
	if value.size() != expected.size():
		return "%s must contain exactly: %s" % [path, ",".join(expected)]
	for key: String in expected:
		if not value.has(key):
			return "%s is missing %s" % [path, key]
	return ""


static func _is_integer_value(value: Variant) -> bool:
	return typeof(value) == TYPE_INT or (typeof(value) == TYPE_FLOAT and float(value) == floorf(float(value)))

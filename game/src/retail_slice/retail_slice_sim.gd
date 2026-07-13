class_name RetailSliceSim
extends RefCounted
## Deterministic battalion-level gameplay used by the private retail slice.
## Positions are X/Z world coordinates stored as Vector2 values.

const TICK_SECONDS := 0.1
const PLAYER_TEAM := 0
const ENEMY_TEAM := 1
const STRUCTURE_KINDS: Array[String] = ["fortress", "farm", "barracks", "archery_range", "stable"]
const PLAYER_STRUCTURE_BASE := 1000
const ENEMY_STRUCTURE_BASE := 2000
const SOLDIER_HORDE_ID := "bfme2.object.gondor-fighter-horde"

var tick_index := 0
var winner := -1
var ai_enabled := true
var selected_ids: Array[int] = []
var control_groups: Dictionary = {}
var events: Array[Dictionary] = []
var entities: Dictionary = {}
var structures: Dictionary = {}
var team_resources: Dictionary = {PLAYER_TEAM: 0, ENEMY_TEAM: 0}
var team_command_points: Dictionary = {PLAYER_TEAM: 0, ENEMY_TEAM: 0}
var command_point_cap := 200
var base_loop_enabled := false
var source_map_configured := false
var ford_gates: Array[Dictionary] = []
var source_player_starts: Dictionary = {}
var route_provider: RefCounted
var playable_outline := PackedVector2Array()
var last_route_rejection := ""
var _spawn_positions: Dictionary = {}
var _home_layout: Dictionary = {}
var _rules: Dictionary = {}
var _next_dynamic_id: Dictionary = {PLAYER_TEAM: 10, ENEMY_TEAM: 110}
var _next_event_sequence := 1
var _next_order_sequence := 1
var _music_state := ""


func setup(map_configuration: Dictionary = {}, gameplay_rules: Dictionary = {}) -> void:
	if not map_configuration.is_empty():
		_apply_map_configuration(map_configuration)
	elif _spawn_positions.is_empty():
		_apply_fallback_configuration()
	tick_index = 0
	winner = -1
	ai_enabled = true
	selected_ids.clear()
	reset_control_groups()
	events.clear()
	entities.clear()
	structures.clear()
	_next_event_sequence = 1
	_next_order_sequence = 1
	_music_state = ""
	last_route_rejection = ""
	_apply_gameplay_rules(gameplay_rules if not gameplay_rules.is_empty() else _rules)
	_next_dynamic_id = {PLAYER_TEAM: 10, ENEMY_TEAM: 110}
	_add_battalion(1, PLAYER_TEAM, Vector2(_spawn_positions[1]), "Gondor Soldiers I")
	_add_battalion(2, PLAYER_TEAM, Vector2(_spawn_positions[2]), "Gondor Soldiers II")
	_add_battalion(101, ENEMY_TEAM, Vector2(_spawn_positions[101]), "Enemy Soldiers I")
	_add_battalion(102, ENEMY_TEAM, Vector2(_spawn_positions[102]), "Enemy Soldiers II")
	if base_loop_enabled:
		_initialize_base_loop()
	_emit_music("explore")


func _apply_map_configuration(configuration: Dictionary) -> void:
	var configured_spawns: Variant = configuration.get("spawn_positions", {})
	var configured_gates: Variant = configuration.get("ford_gates", [])
	var configured_starts: Variant = configuration.get("player_starts", {})
	var configured_provider: Variant = configuration.get("route_provider")
	var configured_outline: Variant = configuration.get("playable_outline", PackedVector2Array())
	if typeof(configured_spawns) != TYPE_DICTIONARY or typeof(configured_gates) != TYPE_ARRAY or typeof(configured_starts) != TYPE_DICTIONARY or typeof(configured_provider) != TYPE_OBJECT or typeof(configured_outline) != TYPE_PACKED_VECTOR2_ARRAY:
		_apply_fallback_configuration()
		return
	if configured_provider == null or not configured_provider.has_method("query_route"):
		_apply_fallback_configuration()
		return
	var candidate_spawns: Dictionary = configured_spawns
	for id in [1, 2, 101, 102]:
		if not candidate_spawns.has(id) or typeof(candidate_spawns[id]) != TYPE_VECTOR2:
			_apply_fallback_configuration()
			return
	var candidate_gates: Array = configured_gates
	if candidate_gates.size() != 3:
		_apply_fallback_configuration()
		return
	var validated_gates: Array[Dictionary] = []
	for value in candidate_gates:
		if typeof(value) != TYPE_DICTIONARY:
			_apply_fallback_configuration()
			return
		var gate: Dictionary = value
		if typeof(gate.get("edge_a")) != TYPE_VECTOR2 or typeof(gate.get("edge_b")) != TYPE_VECTOR2 or typeof(gate.get("center")) != TYPE_VECTOR2:
			_apply_fallback_configuration()
			return
		validated_gates.append(gate.duplicate(true))
	_spawn_positions = candidate_spawns.duplicate(true)
	ford_gates = validated_gates
	source_player_starts = (configured_starts as Dictionary).duplicate(true)
	route_provider = configured_provider as RefCounted
	playable_outline = (configured_outline as PackedVector2Array).duplicate()
	var configured_home_layout: Variant = configuration.get("home_layout", {})
	_home_layout = (configured_home_layout as Dictionary).duplicate(true) if typeof(configured_home_layout) == TYPE_DICTIONARY else {}
	source_map_configured = bool(configuration.get("source_map_configured", false))


func _apply_fallback_configuration() -> void:
	_spawn_positions = {
		1: Vector2(-38.0, -14.0),
		2: Vector2(-38.0, 14.0),
		101: Vector2(38.0, -14.0),
		102: Vector2(38.0, 14.0),
	}
	ford_gates = [
		{"name": "fallback-south", "edge_a": Vector2(-11.0, -23.0), "edge_b": Vector2(11.0, -23.0), "center": Vector2(0.0, -23.0)},
		{"name": "fallback-center", "edge_a": Vector2(-11.0, 0.0), "edge_b": Vector2(11.0, 0.0), "center": Vector2.ZERO},
		{"name": "fallback-north", "edge_a": Vector2(-11.0, 23.0), "edge_b": Vector2(11.0, 23.0), "center": Vector2(0.0, 23.0)},
	]
	source_player_starts.clear()
	route_provider = null
	playable_outline = PackedVector2Array()
	_home_layout.clear()
	source_map_configured = false


func _apply_gameplay_rules(gameplay_rules: Dictionary) -> void:
	_rules = gameplay_rules.duplicate(true)
	base_loop_enabled = bool(_rules.get("enable_base_loop", false))
	command_point_cap = maxi(60, int(_rules.get("command_point_cap", 200)))
	team_resources = {
		PLAYER_TEAM: maxi(0, int(_rules.get("starting_resources", 1200 if base_loop_enabled else 0))),
		ENEMY_TEAM: maxi(0, int(_rules.get("starting_resources", 1200 if base_loop_enabled else 0))),
	}
	team_command_points = {PLAYER_TEAM: 120, ENEMY_TEAM: 120}


func _initialize_base_loop() -> void:
	structures.clear()
	var layout := _home_layout if not _home_layout.is_empty() else _derive_home_layout()
	for team in [PLAYER_TEAM, ENEMY_TEAM]:
		var team_layout: Dictionary = layout.get(team, layout.get(str(team), {}))
		var base_id := PLAYER_STRUCTURE_BASE if team == PLAYER_TEAM else ENEMY_STRUCTURE_BASE
		for index in range(STRUCTURE_KINDS.size()):
			var kind := STRUCTURE_KINDS[index]
			var position := Vector2(team_layout.get(kind, _fallback_structure_position(team, index)))
			var maximum_health := 9000 if kind == "fortress" else (3600 if kind == "farm" else 5000)
			var production: Array[String] = []
			if kind == "barracks":
				production.append(SOLDIER_HORDE_ID)
			structures[base_id + index + 1] = {
				"id": base_id + index + 1,
				"team": team,
				"kind": "structure",
				"structure_kind": kind,
				"name": kind.replace("_", " ").capitalize(),
				"position": position,
				"rally": Vector2(team_layout.get("rally", _fallback_rally_position(team))),
				"health": maximum_health,
				"maximum_health": maximum_health,
				"construction_progress": 1.0,
				"production": production,
				"queue": [],
				"income_per_payout": int(_rules.get("farm_income", 25)) if kind == "farm" else 0,
			}


func _derive_home_layout() -> Dictionary:
	var player_center := (Vector2(_spawn_positions[1]) + Vector2(_spawn_positions[2])) * 0.5
	var enemy_center := (Vector2(_spawn_positions[101]) + Vector2(_spawn_positions[102])) * 0.5
	var map_center := (player_center + enemy_center) * 0.5
	var result: Dictionary = {}
	for team in [PLAYER_TEAM, ENEMY_TEAM]:
		var anchor := player_center if team == PLAYER_TEAM else enemy_center
		var outward := anchor.direction_to(map_center) * -1.0
		if outward.length_squared() < 0.01:
			outward = Vector2.LEFT if team == PLAYER_TEAM else Vector2.RIGHT
		var side := Vector2(-outward.y, outward.x)
		result[team] = {
			"fortress": anchor + outward * 10.0,
			"farm": anchor + side * 11.0 + outward * 2.0,
			"barracks": anchor - side * 11.0 + outward * 1.0,
			"archery_range": anchor + side * 18.0 - outward * 3.0,
			"stable": anchor - side * 18.0 - outward * 3.0,
			"rally": anchor - outward * 8.0,
		}
	return result


func _fallback_structure_position(team: int, index: int) -> Vector2:
	var anchor := (Vector2(_spawn_positions[1]) + Vector2(_spawn_positions[2])) * 0.5 if team == PLAYER_TEAM else (Vector2(_spawn_positions[101]) + Vector2(_spawn_positions[102])) * 0.5
	var sign_value := -1.0 if team == PLAYER_TEAM else 1.0
	return anchor + Vector2(sign_value * (8.0 + float(index) * 2.5), (float(index) - 2.0) * 7.0)


func _fallback_rally_position(team: int) -> Vector2:
	return (Vector2(_spawn_positions[1]) + Vector2(_spawn_positions[2])) * 0.5 if team == PLAYER_TEAM else (Vector2(_spawn_positions[101]) + Vector2(_spawn_positions[102])) * 0.5


func _add_battalion(id: int, team: int, at: Vector2, display_name: String) -> void:
	var member_health := maxi(1, int(_rules.get("member_health", 200)))
	var member_count := maxi(1, int(_rules.get("member_count", 15)))
	var maximum_health := member_health * member_count
	entities[id] = {
		"id": id,
		"team": team,
		"name": display_name,
		"position": at,
		"destination": at,
		"route": [],
		"route_cells": [],
		"route_ford": "",
		"order_sequence": 0,
		"state": "idle",
		"target_id": 0,
		"target_kind": "battalion",
		"health": maximum_health,
		"maximum_health": maximum_health,
		"damage": maxi(1, int(_rules.get("battalion_damage", 600))),
		"speed": maxf(0.1, float(_rules.get("speed_world_per_second", 5.5))),
		"attack_range": 5.2,
		"attack_period_ticks": maxi(1, int(_rules.get("attack_period_ticks", 10))),
		"pre_attack_ticks": maxi(0, int(_rules.get("pre_attack_ticks", 5))),
		"attack_cooldown": 0,
		"attack_windup": 0,
		"attack_sequence": 0,
		"member_count": member_count,
		"unit_type": SOLDIER_HORDE_ID,
	}


func entity(id: int) -> Dictionary:
	return entities.get(id, {})


func entity_ids() -> Array[int]:
	var result: Array[int] = []
	for value in entities.keys():
		result.append(int(value))
	result.sort()
	return result


func living_ids(team: int) -> Array[int]:
	var result: Array[int] = []
	for id in entity_ids():
		var row: Dictionary = entities[id]
		if int(row["team"]) == team and int(row["health"]) > 0:
			result.append(id)
	return result


func structure(id: int) -> Dictionary:
	return structures.get(id, {})


func structure_ids(team: int = -1) -> Array[int]:
	var result: Array[int] = []
	for value in structures.keys():
		var id := int(value)
		if team < 0 or int((structures[id] as Dictionary).get("team", -1)) == team:
			result.append(id)
	result.sort()
	return result


func living_structure_ids(team: int) -> Array[int]:
	var result: Array[int] = []
	for id in structure_ids(team):
		if int((structures[id] as Dictionary).get("health", 0)) > 0:
			result.append(id)
	return result


func fortress_id(team: int) -> int:
	for id in structure_ids(team):
		if String((structures[id] as Dictionary).get("structure_kind", "")) == "fortress":
			return id
	return 0


func producer_id(team: int, kind: String = "barracks") -> int:
	for id in structure_ids(team):
		var row: Dictionary = structures[id]
		if String(row.get("structure_kind", "")) == kind and int(row.get("health", 0)) > 0:
			return id
	return 0


func resources_for_team(team: int) -> int:
	return int(team_resources.get(team, 0))


func command_points_for_team(team: int) -> int:
	return int(team_command_points.get(team, 0))


func queue_unit(team: int, producer: int, unit_type: String = SOLDIER_HORDE_ID) -> Dictionary:
	if not base_loop_enabled or winner != -1:
		return {"ok": false, "reason": "match-unavailable"}
	if not structures.has(producer):
		return {"ok": false, "reason": "unknown-producer"}
	var building: Dictionary = structures[producer]
	if int(building.get("team", -1)) != team:
		return {"ok": false, "reason": "wrong-owner"}
	if int(building.get("health", 0)) <= 0 or float(building.get("construction_progress", 0.0)) < 1.0:
		return {"ok": false, "reason": "producer-unavailable"}
	if not Array(building.get("production", [])).has(unit_type):
		return {"ok": false, "reason": "unsupported-unit"}
	var queue: Array = building.get("queue", [])
	if queue.size() >= maxi(1, int(_rules.get("maximum_queue", 5))):
		return {"ok": false, "reason": "queue-full"}
	var cost := maxi(0, int(_rules.get("soldier_cost", 200)))
	var command_cost := maxi(1, int(_rules.get("soldier_command_points", 60)))
	var queued_command_points := 0
	for item_value in queue:
		if typeof(item_value) == TYPE_DICTIONARY:
			queued_command_points += int((item_value as Dictionary).get("command_points", 0))
	if resources_for_team(team) < cost:
		return {"ok": false, "reason": "insufficient-resources"}
	if command_points_for_team(team) + queued_command_points + command_cost > command_point_cap:
		return {"ok": false, "reason": "command-point-cap"}
	var build_ticks := maxi(1, int(_rules.get("soldier_build_ticks", 200)))
	var starts_at := tick_index if queue.is_empty() else int((queue.back() as Dictionary).get("complete_tick", tick_index))
	var item := {
		"unit_type": unit_type,
		"cost": cost,
		"command_points": command_cost,
		"queued_tick": tick_index,
		"complete_tick": starts_at + build_ticks,
	}
	queue.append(item)
	building["queue"] = queue
	team_resources[team] = resources_for_team(team) - cost
	_emit_event("production.queued", producer, 0, {"team": team, "unit_type": unit_type, "complete_tick": int(item["complete_tick"])})
	return {"ok": true, "reason": "", "producer_id": producer, "item": item.duplicate(true)}


func select_only(id: int) -> bool:
	if not _is_commandable(id):
		return false
	selected_ids.assign([id])
	_emit_event("voice.select", id, 0)
	return true


func toggle_selection(id: int) -> bool:
	if not _is_commandable(id):
		return false
	if selected_ids.has(id):
		selected_ids.erase(id)
	else:
		selected_ids.append(id)
		selected_ids.sort()
		_emit_event("voice.select", id, 0)
	return true


func clear_selection() -> void:
	selected_ids.clear()


func assign_control_group(group: int, ids: Array[int]) -> Dictionary:
	if group < 1 or group > 9:
		return {"ok": false, "reason": "invalid-group", "group": group, "entity_ids": []}
	var assigned: Array[int] = []
	var candidates: Array[int] = ids.duplicate()
	candidates.sort()
	for id in candidates:
		if assigned.has(id) or not _is_living_player(id):
			continue
		assigned.append(id)
	control_groups[group] = assigned.duplicate()
	return {"ok": true, "reason": "", "group": group, "entity_ids": assigned}


func recall_control_group(group: int) -> Array[int]:
	if group < 1 or group > 9:
		return []
	_prune_control_group(group)
	var recalled: Array[int] = []
	for value: Variant in Array(control_groups.get(group, [])):
		recalled.append(int(value))
	return recalled


func prune_control_groups() -> void:
	for group in range(1, 10):
		_prune_control_group(group)


func reset_control_groups() -> void:
	control_groups.clear()
	for group in range(1, 10):
		control_groups[group] = []


func control_groups_snapshot() -> Array[Dictionary]:
	var rows: Array[Dictionary] = []
	for group in range(1, 10):
		var ids: Array[int] = []
		for value: Variant in Array(control_groups.get(group, [])):
			ids.append(int(value))
		ids.sort()
		rows.append({"group": group, "entity_ids": ids})
	return rows


# Keep a singular spelling available to presentation callers without making
# the serialized contract depend on a Dictionary's key ordering.
func control_group_snapshot() -> Array[Dictionary]:
	return control_groups_snapshot()


func issue_move(ids: Array[int], destination: Vector2) -> int:
	var accepted_ids: Array[int] = []
	last_route_rejection = ""
	for id in ids:
		if accepted_ids.has(id) or not _is_commandable(id):
			continue
		var row: Dictionary = entities[id]
		if not _assign_route(row, destination):
			continue
		row["target_id"] = 0
		row["target_kind"] = "battalion"
		row["attack_windup"] = 0
		row["state"] = "run"
		accepted_ids.append(id)
	if not accepted_ids.is_empty():
		_stamp_order_sequence(accepted_ids)
		last_route_rejection = ""
		_emit_event("order.move", accepted_ids[0], 0)
	return accepted_ids.size()


func issue_attack(ids: Array[int], target_id: int) -> int:
	var target_kind := "battalion" if entities.has(target_id) else ("structure" if structures.has(target_id) else "")
	if target_kind == "":
		return 0
	var target: Dictionary = entities[target_id] if target_kind == "battalion" else structures[target_id]
	if int(target["health"]) <= 0:
		return 0
	var accepted_ids: Array[int] = []
	last_route_rejection = ""
	for id in ids:
		if accepted_ids.has(id) or not _is_commandable(id):
			continue
		var row: Dictionary = entities[id]
		if int(row["team"]) == int(target["team"]):
			continue
		if not _assign_route(row, Vector2(target["position"])):
			continue
		row["target_id"] = target_id
		row["target_kind"] = target_kind
		row["attack_windup"] = 0
		row["state"] = "run"
		accepted_ids.append(id)
	if not accepted_ids.is_empty():
		_stamp_order_sequence(accepted_ids)
		last_route_rejection = ""
		_emit_event("voice.attack", accepted_ids[0], target_id)
		_emit_music("battle")
	return accepted_ids.size()


func advance(ticks: int) -> void:
	for _index in range(maxi(0, ticks)):
		tick()


func tick() -> void:
	if winner != -1:
		return
	tick_index += 1
	if base_loop_enabled:
		_step_economy()
		_step_production()
	if ai_enabled and tick_index % 15 == 0:
		_update_enemy_ai()
	for id in entity_ids():
		_step_entity(id)
	_resolve_victory()


func _step_economy() -> void:
	var interval := maxi(1, int(_rules.get("farm_payout_ticks", 50)))
	if tick_index % interval != 0:
		return
	for id in structure_ids():
		var row: Dictionary = structures[id]
		var income := int(row.get("income_per_payout", 0))
		if income <= 0 or int(row.get("health", 0)) <= 0 or float(row.get("construction_progress", 0.0)) < 1.0:
			continue
		var team := int(row.get("team", -1))
		team_resources[team] = resources_for_team(team) + income
		_emit_event("economy.payout", id, 0, {"team": team, "amount": income})


func _step_production() -> void:
	for id in structure_ids():
		var building: Dictionary = structures[id]
		if int(building.get("health", 0)) <= 0:
			continue
		var queue: Array = building.get("queue", [])
		if queue.is_empty():
			continue
		var item: Dictionary = queue[0]
		if tick_index < int(item.get("complete_tick", tick_index + 1)):
			continue
		queue.pop_front()
		building["queue"] = queue
		var team := int(building.get("team", -1))
		var new_id := int(_next_dynamic_id.get(team, 10 if team == PLAYER_TEAM else 110))
		_next_dynamic_id[team] = new_id + 1
		var rally := Vector2(building.get("rally", building.get("position", Vector2.ZERO)))
		_add_battalion(new_id, team, rally, "Gondor Soldiers %d" % new_id)
		team_command_points[team] = command_points_for_team(team) + int(item.get("command_points", 60))
		_emit_event("production.complete", id, new_id, {"team": team, "unit_type": String(item.get("unit_type", SOLDIER_HORDE_ID))})


func _step_entity(id: int) -> void:
	var row: Dictionary = entities[id]
	if int(row["health"]) <= 0:
		row["state"] = "death"
		_clear_pending_route(row, true)
		return
	row["attack_cooldown"] = maxi(0, int(row["attack_cooldown"]) - 1)
	var target_id := int(row["target_id"])
	if target_id != 0:
		var target_kind := String(row.get("target_kind", "battalion"))
		if not _target_alive(target_id, target_kind):
			row["target_id"] = 0
			row["target_kind"] = "battalion"
			row["attack_windup"] = 0
			_clear_pending_route(row, true)
			row["state"] = "idle"
			return
		var target_position := _target_position(target_id, target_kind)
		var distance := Vector2(row["position"]).distance_to(target_position)
		var target_radius := 4.5 if target_kind == "structure" else 0.0
		if distance <= float(row["attack_range"]) + target_radius:
			row["state"] = "attack"
			_clear_pending_route(row, true)
			if int(row["attack_cooldown"]) == 0:
				if int(row.get("attack_windup", 0)) <= 0:
					row["attack_windup"] = int(row.get("pre_attack_ticks", 0))
					row["attack_sequence"] = int(row.get("attack_sequence", 0)) + 1
					_emit_event("combat.swing", id, target_id, {"attack_sequence": int(row["attack_sequence"])})
				if int(row["attack_windup"]) > 0:
					row["attack_windup"] = int(row["attack_windup"]) - 1
				if int(row["attack_windup"]) == 0:
					row["attack_cooldown"] = int(row["attack_period_ticks"])
					_apply_damage(id, target_id, int(row["damage"]), target_kind)
			return
		row["attack_windup"] = 0
		# Targets can move. Refresh the route after reaching a stale endpoint.
		if (row["route"] as Array).is_empty():
			if not _assign_route(row, target_position):
				row["target_id"] = 0
				_clear_pending_route(row, true)
				row["state"] = "idle"
				return
		row["state"] = "run"
		_step_route(row)
		return
	if not (row["route"] as Array).is_empty():
		row["state"] = "run"
		_step_route(row)
	else:
		row["state"] = "idle"


func _step_route(row: Dictionary) -> void:
	var route: Array = row["route"]
	if route.is_empty():
		return
	var position := Vector2(row["position"])
	var waypoint := Vector2(route[0])
	var step_distance := float(row["speed"]) * TICK_SECONDS
	if position.distance_to(waypoint) <= step_distance:
		position = waypoint
		route.pop_front()
	else:
		position += position.direction_to(waypoint) * step_distance
	row["position"] = position
	row["route"] = route
	if route.is_empty():
		_clear_pending_route(row, int(row["target_id"]) == 0)
		if int(row["target_id"]) == 0:
			row["state"] = "idle"


func _apply_damage(attacker_id: int, target_id: int, amount: int, target_kind: String = "battalion") -> void:
	if target_kind == "structure":
		_apply_structure_damage(attacker_id, target_id, amount)
		return
	if not entities.has(target_id):
		return
	var target: Dictionary = entities[target_id]
	if int(target["health"]) <= 0:
		return
	target["health"] = maxi(0, int(target["health"]) - amount)
	_emit_event("combat.hit", attacker_id, target_id)
	if int(target["target_id"]) == 0 and int(target["health"]) > 0:
		var attacker_position := Vector2((entities[attacker_id] as Dictionary)["position"])
		if _assign_route(target, attacker_position):
			target["target_id"] = attacker_id
			_stamp_order_sequence([target_id])
			_emit_music("battle")
	if int(target["health"]) == 0:
		target["state"] = "death"
		target["target_id"] = 0
		_clear_pending_route(target, true)
		selected_ids.erase(target_id)
		if base_loop_enabled:
			var target_team := int(target.get("team", -1))
			team_command_points[target_team] = maxi(0, command_points_for_team(target_team) - int(_rules.get("soldier_command_points", 60)))
		prune_control_groups()
		_emit_event("battalion.defeated", attacker_id, target_id)


func _apply_structure_damage(attacker_id: int, target_id: int, amount: int) -> void:
	if not structures.has(target_id):
		return
	var target: Dictionary = structures[target_id]
	if int(target.get("health", 0)) <= 0:
		return
	target["health"] = maxi(0, int(target["health"]) - amount)
	_emit_event("combat.hit_structure", attacker_id, target_id)
	if int(target["health"]) == 0:
		var queue: Array = target.get("queue", [])
		# Queued costs stay spent, matching the deterministic no-refund contract.
		queue.clear()
		target["queue"] = queue
		_emit_event("structure.destroyed", attacker_id, target_id)


func _target_alive(target_id: int, target_kind: String) -> bool:
	if target_kind == "structure":
		return structures.has(target_id) and int((structures[target_id] as Dictionary).get("health", 0)) > 0
	return entities.has(target_id) and int((entities[target_id] as Dictionary).get("health", 0)) > 0


func _target_position(target_id: int, target_kind: String) -> Vector2:
	var row: Dictionary = structures.get(target_id, {}) if target_kind == "structure" else entities.get(target_id, {})
	return Vector2(row.get("position", Vector2.ZERO))


func _update_enemy_ai() -> void:
	if base_loop_enabled and tick_index % maxi(15, int(_rules.get("ai_queue_interval_ticks", 60))) == 0:
		var barracks := producer_id(ENEMY_TEAM, "barracks")
		if barracks != 0:
			queue_unit(ENEMY_TEAM, barracks, SOLDIER_HORDE_ID)
	# Give the player one full Soldier production window before the first wave.
	# Economy/production still advance symmetrically during the preparation time.
	if base_loop_enabled and tick_index < maxi(0, int(_rules.get("ai_attack_delay_ticks", 0))):
		return
	var players := living_ids(PLAYER_TEAM)
	var player_fortress := fortress_id(PLAYER_TEAM) if base_loop_enabled else 0
	if players.is_empty() and player_fortress == 0:
		return
	for id in living_ids(ENEMY_TEAM):
		var row: Dictionary = entities[id]
		if int(row["target_id"]) != 0:
			continue
		var target_id := 0
		var target_kind := "battalion"
		if players.is_empty() and player_fortress != 0:
			target_id = player_fortress
			target_kind = "structure"
		else:
			target_id = players[0]
			var closest_distance := Vector2(row["position"]).distance_to(Vector2((entities[target_id] as Dictionary)["position"]))
			for candidate in players:
				var distance := Vector2(row["position"]).distance_to(Vector2((entities[candidate] as Dictionary)["position"]))
				if distance < closest_distance or (is_equal_approx(distance, closest_distance) and candidate < target_id):
					target_id = candidate
					closest_distance = distance
		var target_position := _target_position(target_id, target_kind)
		if _assign_route(row, target_position):
			row["target_id"] = target_id
			row["target_kind"] = target_kind
			row["attack_windup"] = 0
			row["state"] = "run"
			_stamp_order_sequence([id])
			_emit_music("battle")


func _build_route(from: Vector2, to: Vector2) -> Array[Vector2]:
	var result := _query_route(from, to)
	if not bool(result.get("valid", false)):
		return []
	var points: Array[Vector2] = []
	points.assign(result.get("points", []))
	return points


func _assign_route(row: Dictionary, destination: Vector2) -> bool:
	var result := _query_route(Vector2(row["position"]), destination)
	if not bool(result.get("valid", false)):
		last_route_rejection = String(result.get("reason", "route-rejected"))
		return false
	var points: Array[Vector2] = []
	points.assign(result.get("points", []))
	if points.is_empty():
		last_route_rejection = String(result.get("reason", ""))
		if last_route_rejection.is_empty():
			last_route_rejection = "empty-route"
		return false
	var cells: Array[Vector2i] = []
	cells.assign(result.get("cells", []))
	row["destination"] = destination
	row["route"] = points
	row["route_cells"] = cells
	row["route_ford"] = String(result.get("ford_name", ""))
	return not points.is_empty()


func _query_route(from: Vector2, to: Vector2) -> Dictionary:
	if route_provider != null and route_provider.has_method("query_route"):
		var value: Variant = route_provider.call("query_route", from, to)
		if typeof(value) == TYPE_DICTIONARY:
			return value as Dictionary
	# The non-retail fallback remains bounded and direct. The selected retail
	# slice cannot reach this branch because configuration requires a provider.
	return {"valid": true, "reason": "", "points": [to], "cells": [], "ford_name": ""}


func _resolve_victory() -> void:
	if base_loop_enabled:
		var player_fortress := fortress_id(PLAYER_TEAM)
		var enemy_fortress := fortress_id(ENEMY_TEAM)
		if player_fortress != 0 and int((structures[player_fortress] as Dictionary).get("health", 0)) <= 0:
			winner = ENEMY_TEAM
		elif enemy_fortress != 0 and int((structures[enemy_fortress] as Dictionary).get("health", 0)) <= 0:
			winner = PLAYER_TEAM
		else:
			return
	else:
		var players_alive := living_ids(PLAYER_TEAM)
		var enemies_alive := living_ids(ENEMY_TEAM)
		if players_alive.is_empty():
			winner = ENEMY_TEAM
		elif enemies_alive.is_empty():
			winner = PLAYER_TEAM
		else:
			return
	for id in living_ids(winner):
		var row: Dictionary = entities[id]
		row["target_id"] = 0
		_clear_pending_route(row, true)
		row["state"] = "victory"
	if winner == PLAYER_TEAM:
		_emit_event("match.victory", 0, 0)
		_emit_music("victory")
	else:
		_emit_event("match.defeat", 0, 0)
		_emit_music("defeat")


func _is_commandable(id: int) -> bool:
	return entities.has(id) and int((entities[id] as Dictionary)["team"]) == PLAYER_TEAM and int((entities[id] as Dictionary)["health"]) > 0 and winner == -1


func _is_living_player(id: int) -> bool:
	return entities.has(id) and int((entities[id] as Dictionary)["team"]) == PLAYER_TEAM and int((entities[id] as Dictionary)["health"]) > 0


func _prune_control_group(group: int) -> void:
	var retained: Array[int] = []
	for value: Variant in Array(control_groups.get(group, [])):
		var id := int(value)
		if _is_living_player(id) and not retained.has(id):
			retained.append(id)
	retained.sort()
	control_groups[group] = retained


func _stamp_order_sequence(ids: Array[int]) -> int:
	var sequence := _next_order_sequence
	_next_order_sequence += 1
	for id in ids:
		if entities.has(id):
			(entities[id] as Dictionary)["order_sequence"] = sequence
	return sequence


func _clear_pending_route(row: Dictionary, settle_destination: bool) -> void:
	row["route"] = []
	row["route_cells"] = []
	row["route_ford"] = ""
	if settle_destination:
		row["destination"] = Vector2(row["position"])


func _emit_music(state: String) -> void:
	if state == _music_state:
		return
	_music_state = state
	_emit_event("music.%s" % state, 0, 0)


func _emit_event(kind: String, entity_id: int, target_id: int, data: Dictionary = {}) -> void:
	var event := {
		"sequence": _next_event_sequence,
		"tick": tick_index,
		"kind": kind,
		"entity_id": entity_id,
		"target_id": target_id,
	}
	for key in data.keys():
		if not event.has(key):
			event[key] = data[key]
	events.append(event)
	_next_event_sequence += 1


func state_snapshot() -> Dictionary:
	var rows: Array[Dictionary] = []
	for id in entity_ids():
		var entity_row: Dictionary = entities[id]
		var position := Vector2(entity_row["position"])
		var destination := Vector2(entity_row["destination"])
		var route_rows: Array[Array] = []
		for value: Variant in Array(entity_row.get("route", [])):
			var point := Vector2(value)
			route_rows.append([snappedf(point.x, 0.001), snappedf(point.y, 0.001)])
		var route_cell_rows: Array[Array] = []
		for value: Variant in Array(entity_row.get("route_cells", [])):
			var cell := Vector2i(value)
			route_cell_rows.append([cell.x, cell.y])
		rows.append({
			"id": id,
			"team": int(entity_row["team"]),
			"position": [snappedf(position.x, 0.001), snappedf(position.y, 0.001)],
			"destination": [snappedf(destination.x, 0.001), snappedf(destination.y, 0.001)],
			"state": String(entity_row["state"]),
			"target": int(entity_row["target_id"]),
			"target_kind": String(entity_row.get("target_kind", "battalion")),
			"health": int(entity_row["health"]),
			"attack_windup": int(entity_row.get("attack_windup", 0)),
			"attack_sequence": int(entity_row.get("attack_sequence", 0)),
			"route": route_rows,
			"route_cells": route_cell_rows,
			"route_ford": String(entity_row.get("route_ford", "")),
			"order_sequence": int(entity_row.get("order_sequence", 0)),
		})
	var structure_rows: Array[Dictionary] = []
	for id in structure_ids():
		var structure_row: Dictionary = structures[id]
		var position := Vector2(structure_row.get("position", Vector2.ZERO))
		var rally := Vector2(structure_row.get("rally", Vector2.ZERO))
		var queue_rows: Array[Dictionary] = []
		for item_value in Array(structure_row.get("queue", [])):
			if typeof(item_value) != TYPE_DICTIONARY:
				continue
			var item: Dictionary = item_value
			queue_rows.append({
				"unit_type": String(item.get("unit_type", "")),
				"cost": int(item.get("cost", 0)),
				"command_points": int(item.get("command_points", 0)),
				"queued_tick": int(item.get("queued_tick", 0)),
				"complete_tick": int(item.get("complete_tick", 0)),
			})
		structure_rows.append({
			"id": id,
			"team": int(structure_row.get("team", -1)),
			"kind": String(structure_row.get("structure_kind", "")),
			"position": [snappedf(position.x, 0.001), snappedf(position.y, 0.001)],
			"rally": [snappedf(rally.x, 0.001), snappedf(rally.y, 0.001)],
			"health": int(structure_row.get("health", 0)),
			"maximum_health": int(structure_row.get("maximum_health", 0)),
			"construction_progress": snappedf(float(structure_row.get("construction_progress", 0.0)), 0.001),
			"queue": queue_rows,
		})
	var gate_rows: Array[Dictionary] = []
	for gate in ford_gates:
		var edge_a := Vector2(gate.get("edge_a", Vector2.ZERO))
		var edge_b := Vector2(gate.get("edge_b", Vector2.ZERO))
		gate_rows.append({
			"name": String(gate.get("name", "")),
			"edge_a": [snappedf(edge_a.x, 0.001), snappedf(edge_a.y, 0.001)],
			"edge_b": [snappedf(edge_b.x, 0.001), snappedf(edge_b.y, 0.001)],
		})
	return {
		"tick": tick_index,
		"winner": winner,
		"base_loop_enabled": base_loop_enabled,
		"resources": [resources_for_team(PLAYER_TEAM), resources_for_team(ENEMY_TEAM)],
		"command_points": [command_points_for_team(PLAYER_TEAM), command_points_for_team(ENEMY_TEAM)],
		"command_point_cap": command_point_cap,
		"next_dynamic_ids": [int(_next_dynamic_id.get(PLAYER_TEAM, 10)), int(_next_dynamic_id.get(ENEMY_TEAM, 110))],
		"selected": selected_ids.duplicate(),
		"control_groups": control_groups_snapshot(),
		"next_order_sequence": _next_order_sequence,
		"music": _music_state,
		"source_map_configured": source_map_configured,
		"ford_gates": gate_rows,
		"entities": rows,
		"structures": structure_rows,
		"events": events.duplicate(true),
	}


func state_signature() -> String:
	var value: int = 0x811C9DC5
	for byte in JSON.stringify(state_snapshot()).to_utf8_buffer():
		value = ((value ^ int(byte)) * 16777619) & 0xFFFFFFFF
	return "%08X" % value

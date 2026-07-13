class_name Stage3StructureSystem
extends RefCounted
## Legal-safe fortifications, gates, tower attachments, and tower projectiles.

const TEAM_BLUE: int = 0
const TEAM_RED: int = 1
const TopologyScript = preload("res://src/proof_stage3/topology_grid.gd")


class StructureRecord extends RefCounted:
	var id: int
	var definition_id: String
	var kind: String
	var owner: int
	var cell: Vector2i
	var position: Vector2i
	var rotation_quarters: int
	var health: int
	var max_health: int
	var gate_open: bool = false
	var attach_to_id: int = 0
	var cooldown_remaining: int = 0
	var last_fired_target_id: int = 0
	var definition: Dictionary

	func _init(p_id: int, request: Dictionary) -> void:
		id = p_id
		definition_id = String(request["definition_id"])
		definition = (request["definition"] as Dictionary).duplicate(true)
		kind = String(definition["kind"])
		owner = int(request["owner"])
		cell = request["cell"]
		position = request["position"]
		rotation_quarters = int(request["rotation_quarters"])
		max_health = maxi(1, int(definition.get("maximumHealth", 100)))
		health = max_health
		attach_to_id = int(request["attach_to_id"])

	func is_alive() -> bool:
		return health > 0

	func is_attachment() -> bool:
		return bool(definition.get("attachment", false))


class ProjectileRecord extends RefCounted:
	var id: int
	var owner: int
	var source_id: int
	var target_id: int
	var cell: Vector2i
	var last_target_cell: Vector2i
	var damage: int
	var speed_cells: int

	func _init(p_id: int, p_owner: int, p_source_id: int, p_target_id: int, p_cell: Vector2i, p_target_cell: Vector2i, p_damage: int, p_speed_cells: int) -> void:
		id = p_id
		owner = p_owner
		source_id = p_source_id
		target_id = p_target_id
		cell = p_cell
		last_target_cell = p_target_cell
		damage = p_damage
		speed_cells = maxi(1, p_speed_cells)


var topology: TopologyScript
var definitions: Dictionary
var structures: Dictionary = {}
var projectiles: Dictionary = {}
var _base_by_cell: Dictionary = {}
var _attachment_by_cell: Dictionary = {}
var _team_resources := PackedInt64Array([0, 0])
var _next_structure_id: int = 1
var _next_projectile_id: int = 100000


func _init(p_topology: TopologyScript, p_definitions: Dictionary) -> void:
	topology = p_topology
	definitions = _index_definitions(p_definitions)


func set_team_resources(team: int, amount: int) -> void:
	assert(team == TEAM_BLUE or team == TEAM_RED)
	_team_resources[team] = maxi(0, amount)


func resources_for(team: int) -> int:
	return int(_team_resources[team]) if team == TEAM_BLUE or team == TEAM_RED else 0


func next_structure_id() -> int:
	return _next_structure_id


func next_projectile_id() -> int:
	return _next_projectile_id


func get_structure(structure_id: int) -> StructureRecord:
	return structures.get(structure_id) as StructureRecord


func structure_at(cell: Vector2i) -> StructureRecord:
	return get_structure(int(_base_by_cell.get(cell, 0)))


func attachment_at(cell: Vector2i) -> StructureRecord:
	return get_structure(int(_attachment_by_cell.get(cell, 0)))


func place_structure(definition_id: String, owner: int, world_position: Vector2i, rotation_quarters: int = 0, attach_to_id: int = 0) -> Dictionary:
	var raw_request: Dictionary = {
		"definition": definition_id,
		"owner": owner,
		"position": world_position,
		"rotation_quarters": rotation_quarters,
		"attach_to_id": attach_to_id,
	}
	var validation: Dictionary = _normalize_request(raw_request, {})
	if not bool(validation["ok"]):
		return validation
	var requests: Array[Dictionary] = [validation["request"]]
	return _commit(requests)


func place_chain(raw_requests: Array[Dictionary]) -> Dictionary:
	if raw_requests.is_empty():
		return _failure("chain_empty")
	var occupied_in_request: Dictionary = {}
	var normalized: Array[Dictionary] = []
	for raw_request: Dictionary in raw_requests:
		var validation: Dictionary = _normalize_request(raw_request, occupied_in_request)
		if not bool(validation["ok"]):
			return validation
		var request: Dictionary = validation["request"]
		if bool((request["definition"] as Dictionary).get("attachment", false)):
			return _failure("chain_cannot_contain_attachment")
		var kind: String = String((request["definition"] as Dictionary)["kind"])
		if kind != "wall" and kind != "gate":
			return _failure("chain_requires_wall_or_gate")
		occupied_in_request[request["cell"]] = true
		normalized.append(request)
	for i: int in range(1, normalized.size()):
		var previous: Vector2i = normalized[i - 1]["cell"]
		var current: Vector2i = normalized[i]["cell"]
		if absi(previous.x - current.x) + absi(previous.y - current.y) != 1:
			return _failure("chain_not_contiguous")
	return _commit(normalized)


func set_gate_open(structure_id: int, is_open: bool) -> bool:
	var structure: StructureRecord = get_structure(structure_id)
	if structure == null or structure.kind != "gate" or not structure.is_alive():
		return false
	if structure.gate_open == is_open:
		return true
	var changes: Array[Dictionary] = [{"cell": structure.cell, "mask": _gate_mask(structure.owner, is_open)}]
	if not topology.apply_masks(changes):
		return false
	structure.gate_open = is_open
	return true


func damage_structure(structure_id: int, damage: int) -> bool:
	var structure: StructureRecord = get_structure(structure_id)
	if structure == null or not structure.is_alive() or damage <= 0:
		return false
	structure.health = maxi(0, structure.health - damage)
	if structure.health > 0:
		return true
	if structure.is_attachment():
		_attachment_by_cell.erase(structure.cell)
		return true
	var changes: Array[Dictionary] = [{"cell": structure.cell, "mask": 0}]
	topology.apply_masks(changes)
	_base_by_cell.erase(structure.cell)
	var attachment: StructureRecord = attachment_at(structure.cell)
	if attachment != null and attachment.is_alive():
		attachment.health = 0
		_attachment_by_cell.erase(structure.cell)
	return true


func tick(targets: Dictionary) -> void:
	_tick_projectiles(targets)
	var ids: Array = structures.keys()
	ids.sort()
	for raw_id: Variant in ids:
		var structure: StructureRecord = get_structure(int(raw_id))
		if structure == null or not structure.is_alive() or not bool(structure.definition.get("canFire", false)):
			continue
		if structure.cooldown_remaining > 0:
			structure.cooldown_remaining -= 1
			continue
		var target: Variant = _select_target(structure, targets)
		if target == null:
			continue
		var projectile_id: int = _next_projectile_id
		_next_projectile_id += 1
		var projectile := ProjectileRecord.new(
			projectile_id,
			structure.owner,
			structure.id,
			int(target.id),
			structure.cell,
			target.cell,
			maxi(1, int(structure.definition.get("damage", 20))),
			maxi(1, int(structure.definition.get("projectileSpeedCellsPerTick", 1)))
		)
		projectiles[projectile_id] = projectile
		structure.last_fired_target_id = int(target.id)
		structure.cooldown_remaining = maxi(1, int(structure.definition.get("cooldownTicks", 6)))


func active_projectile_count() -> int:
	return projectiles.size()


func _normalize_request(raw_request: Dictionary, occupied_in_request: Dictionary) -> Dictionary:
	var definition_id: String = String(raw_request.get("definition", ""))
	var definition: Dictionary = definitions.get(definition_id, {})
	if definition.is_empty() or not definition.has("kind"):
		return _failure("unknown_definition")
	var owner: int = int(raw_request.get("owner", -1))
	if owner != TEAM_BLUE and owner != TEAM_RED:
		return _failure("invalid_owner")
	var world_position: Vector2i = raw_request.get("position", Vector2i(-1, -1))
	if world_position.x < 0 or world_position.y < 0 or world_position.x >= topology.width * TopologyScript.CELL_SIZE or world_position.y >= topology.height * TopologyScript.CELL_SIZE:
		return _failure("out_of_bounds")
	var cell: Vector2i = topology.to_cell(world_position)
	var is_attachment: bool = bool(definition.get("attachment", false))
	var attach_to_id: int = int(raw_request.get("attach_to_id", 0))
	if is_attachment:
		var base: StructureRecord = get_structure(attach_to_id)
		if base == null or not base.is_alive():
			return _failure("attachment_requires_live_base")
		if base.owner != owner:
			return _failure("attachment_owner_mismatch")
		if base.cell != cell:
			return _failure("attachment_position_mismatch")
		if _attachment_by_cell.has(cell):
			return _failure("attachment_slot_occupied")
		var compatible: Array = definition.get("compatibleBaseKinds", [])
		if not compatible.has(base.kind):
			return _failure("incompatible_attachment_base")
	else:
		if attach_to_id != 0:
			return _failure("base_cannot_attach")
		if _base_by_cell.has(cell) or occupied_in_request.has(cell) or topology.mask_at(cell) != 0:
			return _failure("cell_occupied")
	var cost: int = maxi(0, int(definition.get("cost", 0)))
	if resources_for(owner) < cost:
		return _failure("insufficient_resources")
	return {
		"ok": true,
		"request": {
			"definition_id": definition_id,
			"definition": definition,
			"owner": owner,
			"cell": cell,
			"position": topology.snap_position(world_position),
			"rotation_quarters": TopologyScript.normalize_quarter_rotation(int(raw_request.get("rotation_quarters", 0))),
			"attach_to_id": attach_to_id,
			"cost": cost,
		},
	}


func _commit(requests: Array[Dictionary]) -> Dictionary:
	var total_costs := PackedInt64Array([0, 0])
	var changes: Array[Dictionary] = []
	for request: Dictionary in requests:
		var owner: int = int(request["owner"])
		total_costs[owner] += int(request["cost"])
		var definition: Dictionary = request["definition"]
		if not bool(definition.get("attachment", false)):
			changes.append({"cell": request["cell"], "mask": _initial_mask(definition, owner)})
	for team: int in range(2):
		if total_costs[team] > _team_resources[team]:
			return _failure("insufficient_resources")
	if not topology.apply_masks(changes):
		return _failure("topology_commit_failed")
	var ids: Array[int] = []
	for request: Dictionary in requests:
		var id: int = _next_structure_id
		_next_structure_id += 1
		var structure := StructureRecord.new(id, request)
		structures[id] = structure
		if structure.is_attachment():
			_attachment_by_cell[structure.cell] = id
		else:
			_base_by_cell[structure.cell] = id
		_team_resources[structure.owner] -= int(request["cost"])
		ids.append(id)
	return {"ok": true, "ids": ids, "id": ids[0]}


func _initial_mask(definition: Dictionary, owner: int) -> int:
	var kind: String = String(definition["kind"])
	if kind == "gate":
		return _gate_mask(owner, false)
	return TopologyScript.BLOCK_BOTH if bool(definition.get("blocksMovement", true)) else 0


func _gate_mask(owner: int, is_open: bool) -> int:
	if not is_open:
		return TopologyScript.BLOCK_BOTH
	return TopologyScript.BLOCK_RED if owner == TEAM_BLUE else TopologyScript.BLOCK_BLUE


func _select_target(structure: StructureRecord, targets: Dictionary) -> Variant:
	var best: Variant = null
	var best_distance_squared: int = 0x7fffffff
	var target_ids: Array = targets.keys()
	target_ids.sort()
	var range_cells: int = maxi(0, int(structure.definition.get("rangeCells", 0)))
	var range_squared: int = range_cells * range_cells
	for raw_id: Variant in target_ids:
		var target: Variant = targets[raw_id]
		if int(target.health) <= 0 or int(target.team) == structure.owner:
			continue
		var delta: Vector2i = target.cell - structure.cell
		var distance_squared: int = delta.x * delta.x + delta.y * delta.y
		if distance_squared > range_squared:
			continue
		if best == null or distance_squared < best_distance_squared or (distance_squared == best_distance_squared and int(target.id) < int(best.id)):
			best = target
			best_distance_squared = distance_squared
	return best


func _tick_projectiles(targets: Dictionary) -> void:
	var projectile_ids: Array = projectiles.keys()
	projectile_ids.sort()
	var finished: Array[int] = []
	for raw_id: Variant in projectile_ids:
		var projectile: ProjectileRecord = projectiles[raw_id] as ProjectileRecord
		var target: Variant = targets.get(projectile.target_id)
		if target != null and int(target.health) > 0:
			projectile.last_target_cell = target.cell
		var remaining: int = projectile.speed_cells
		var delta: Vector2i = projectile.last_target_cell - projectile.cell
		var x_step: int = mini(absi(delta.x), remaining)
		if x_step > 0:
			projectile.cell.x += x_step if delta.x > 0 else -x_step
			remaining -= x_step
		delta = projectile.last_target_cell - projectile.cell
		var y_step: int = mini(absi(delta.y), remaining)
		if y_step > 0:
			projectile.cell.y += y_step if delta.y > 0 else -y_step
		if projectile.cell != projectile.last_target_cell:
			continue
		if target != null and int(target.health) > 0 and target.cell == projectile.cell:
			target.health = maxi(0, int(target.health) - projectile.damage)
		finished.append(projectile.id)
	for projectile_id: int in finished:
		projectiles.erase(projectile_id)


func _failure(error: String) -> Dictionary:
	return {"ok": false, "error": error}


func _index_definitions(source: Dictionary) -> Dictionary:
	## Accepts the parsed defenses JSON root, while retaining keyed dictionaries for tests/tools.
	if source.get("structures") is Array:
		var indexed: Dictionary = {}
		var entries: Array = source.get("structures", [])
		for raw_entry: Variant in entries:
			if not raw_entry is Dictionary:
				continue
			var entry: Dictionary = raw_entry
			var id: String = String(entry.get("id", ""))
			if id != "" and not indexed.has(id):
				indexed[id] = entry.duplicate(true)
		return indexed
	return source.duplicate(true)

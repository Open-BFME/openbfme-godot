class_name Stage3ProofWorld
extends RefCounted
## Narrow integration shell: topology, fortifications, movement, combat, and fog.

const TEAM_BLUE: int = 0
const TEAM_RED: int = 1
const FNV_OFFSET: int = 2166136261
const FNV_PRIME: int = 16777619
const TopologyScript = preload("res://src/proof_stage3/topology_grid.gd")
const StructureScript = preload("res://src/proof_stage3/structure_system.gd")
const VisionScript = preload("res://src/proof_stage3/vision_system.gd")


class UnitRecord extends RefCounted:
	var id: int
	var team: int
	var cell: Vector2i
	var health: int
	var max_health: int
	var vision_radius: int
	var destination: Vector2i
	var path: Array[Vector2i] = []
	var path_index: int = 0
	var path_revision: int = 0
	var replan_count: int = 0

	func _init(p_id: int, p_team: int, p_cell: Vector2i, p_health: int, p_vision_radius: int) -> void:
		id = p_id
		team = p_team
		cell = p_cell
		health = p_health
		max_health = p_health
		vision_radius = p_vision_radius
		destination = p_cell

	func is_alive() -> bool:
		return health > 0


var topology: TopologyScript
var structures: StructureScript
var vision: VisionScript
var units: Dictionary = {}
var tick_index: int = 0
var _next_unit_id: int = 1000


func _init(width: int, height: int, definitions: Dictionary) -> void:
	topology = TopologyScript.new(width, height)
	structures = StructureScript.new(topology, definitions)
	vision = VisionScript.new(width, height)


func add_unit(team: int, world_position: Vector2i, health: int = 100, vision_radius: int = 3, forced_id: int = 0) -> UnitRecord:
	assert(team == TEAM_BLUE or team == TEAM_RED)
	var id: int = forced_id if forced_id > 0 else _next_unit_id
	assert(not units.has(id))
	_next_unit_id = maxi(_next_unit_id, id + 1)
	var unit := UnitRecord.new(id, team, topology.to_cell(world_position), maxi(1, health), maxi(0, vision_radius))
	units[id] = unit
	return unit


func get_unit(unit_id: int) -> UnitRecord:
	return units.get(unit_id) as UnitRecord


func next_unit_id() -> int:
	return _next_unit_id


func place_structure(definition_id: String, owner: int, world_position: Vector2i, rotation_quarters: int = 0, attach_to_id: int = 0) -> Dictionary:
	var request: Dictionary = {
		"definition": definition_id,
		"owner": owner,
		"position": world_position,
		"rotation_quarters": rotation_quarters,
		"attach_to_id": attach_to_id,
	}
	if _blocking_request_hits_live_unit(request):
		return {"ok": false, "error": "live_unit_occupied"}
	return structures.place_structure(definition_id, owner, world_position, rotation_quarters, attach_to_id)


func place_chain(raw_requests: Array[Dictionary]) -> Dictionary:
	for request: Dictionary in raw_requests:
		if _blocking_request_hits_live_unit(request):
			return {"ok": false, "error": "live_unit_occupied"}
	return structures.place_chain(raw_requests)


func set_unit_cell(unit_id: int, cell: Vector2i) -> bool:
	var unit: UnitRecord = get_unit(unit_id)
	if unit == null or not topology.contains(cell):
		return false
	unit.cell = cell
	unit.destination = cell
	unit.path.clear()
	unit.path_index = 0
	unit.path_revision = topology.revision
	return true


func order_move(unit_id: int, destination: Vector2i) -> bool:
	var unit: UnitRecord = get_unit(unit_id)
	if unit == null or not unit.is_alive() or not topology.contains(destination):
		return false
	unit.destination = destination
	unit.path = topology.find_path(unit.cell, destination, unit.team)
	unit.path_index = 1 if unit.path.size() > 1 else unit.path.size()
	unit.path_revision = topology.revision
	return not unit.path.is_empty()


func tick() -> void:
	tick_index += 1
	var unit_ids: Array = units.keys()
	unit_ids.sort()
	for raw_id: Variant in unit_ids:
		var unit: UnitRecord = get_unit(int(raw_id))
		if unit == null or not unit.is_alive() or unit.cell == unit.destination:
			continue
		if unit.path_revision != topology.revision:
			_replan(unit)
		if unit.path_index >= unit.path.size():
			continue
		var next_cell: Vector2i = unit.path[unit.path_index]
		if topology.is_blocked(next_cell, unit.team):
			_replan(unit)
			if unit.path_index >= unit.path.size():
				continue
			next_cell = unit.path[unit.path_index]
		unit.cell = next_cell
		unit.path_index += 1
	structures.tick(units)
	recompute_visibility()


func advance(ticks: int) -> void:
	for _tick: int in range(maxi(0, ticks)):
		tick()


func recompute_visibility() -> void:
	vision.begin_tick()
	var unit_ids: Array = units.keys()
	unit_ids.sort()
	for raw_id: Variant in unit_ids:
		var unit: UnitRecord = get_unit(int(raw_id))
		if unit != null and unit.is_alive():
			vision.reveal(unit.team, unit.cell, unit.vision_radius)
	var structure_ids: Array = structures.structures.keys()
	structure_ids.sort()
	for raw_id: Variant in structure_ids:
		var structure: StructureScript.StructureRecord = structures.get_structure(int(raw_id))
		if structure == null or not structure.is_alive():
			continue
		var radius: int = maxi(0, int(structure.definition.get("visionRadiusCells", 0)))
		if radius > 0:
			vision.reveal(structure.owner, structure.cell, radius)


func team_filtered_snapshot(team: int) -> Dictionary:
	assert(team == TEAM_BLUE or team == TEAM_RED)
	var visible_units: Array[Dictionary] = []
	var unit_ids: Array = units.keys()
	unit_ids.sort()
	for raw_id: Variant in unit_ids:
		var unit: UnitRecord = get_unit(int(raw_id))
		if unit == null or not unit.is_alive():
			continue
		if unit.team != team and not vision.is_visible(team, unit.cell):
			continue
		visible_units.append({"id": unit.id, "team": unit.team, "cell": unit.cell, "health": unit.health})
	var visible_structures: Array[Dictionary] = []
	var structure_ids: Array = structures.structures.keys()
	structure_ids.sort()
	for raw_id: Variant in structure_ids:
		var structure: StructureScript.StructureRecord = structures.get_structure(int(raw_id))
		if structure == null or not structure.is_alive():
			continue
		if structure.owner != team and not vision.is_visible(team, structure.cell):
			continue
		visible_structures.append({
			"id": structure.id,
			"definition": structure.definition_id,
			"team": structure.owner,
			"cell": structure.cell,
			"health": structure.health,
			"gate_open": structure.gate_open,
		})
	var visible_projectiles: Array[Dictionary] = []
	var projectile_ids: Array = structures.projectiles.keys()
	projectile_ids.sort()
	for raw_id: Variant in projectile_ids:
		var projectile: StructureScript.ProjectileRecord = structures.projectiles[raw_id] as StructureScript.ProjectileRecord
		if projectile.owner != team and not vision.is_visible(team, projectile.cell):
			continue
		visible_projectiles.append({"id": projectile.id, "team": projectile.owner, "cell": projectile.cell})
	return {
		"tick": tick_index,
		"topology_revision": topology.revision,
		"visible_count": vision.visible_count(team),
		"explored_count": vision.explored_count(team),
		"units": visible_units,
		"structures": visible_structures,
		"projectiles": visible_projectiles,
	}


func path_contains(unit_id: int, cell: Vector2i) -> bool:
	var unit: UnitRecord = get_unit(unit_id)
	return unit != null and unit.path.has(cell)


func state_hash() -> int:
	var hash: int = FNV_OFFSET
	hash = _hash_int(hash, tick_index)
	hash = _hash_int(hash, topology.width)
	hash = _hash_int(hash, topology.height)
	hash = _hash_int(hash, topology.revision)
	hash = _hash_int(hash, _next_unit_id)
	hash = _hash_int(hash, structures.next_structure_id())
	hash = _hash_int(hash, structures.next_projectile_id())
	for y: int in range(topology.height):
		for x: int in range(topology.width):
			hash = _hash_int(hash, topology.mask_at(Vector2i(x, y)))
	for team: int in range(2):
		hash = _hash_int(hash, structures.resources_for(team))
	hash = _hash_variant(hash, structures.definitions)
	var structure_ids: Array = structures.structures.keys()
	structure_ids.sort()
	for raw_id: Variant in structure_ids:
		var structure: StructureScript.StructureRecord = structures.get_structure(int(raw_id))
		hash = _hash_int(hash, structure.id)
		hash = _hash_string(hash, structure.definition_id)
		hash = _hash_string(hash, structure.kind)
		hash = _hash_int(hash, structure.owner)
		hash = _hash_vector(hash, structure.cell)
		hash = _hash_vector(hash, structure.position)
		hash = _hash_int(hash, structure.rotation_quarters)
		hash = _hash_int(hash, structure.health)
		hash = _hash_int(hash, structure.max_health)
		hash = _hash_int(hash, 1 if structure.gate_open else 0)
		hash = _hash_int(hash, structure.attach_to_id)
		hash = _hash_int(hash, structure.cooldown_remaining)
		hash = _hash_int(hash, structure.last_fired_target_id)
		hash = _hash_variant(hash, structure.definition)
	var unit_ids: Array = units.keys()
	unit_ids.sort()
	for raw_id: Variant in unit_ids:
		var unit: UnitRecord = get_unit(int(raw_id))
		hash = _hash_int(hash, unit.id)
		hash = _hash_int(hash, unit.team)
		hash = _hash_vector(hash, unit.cell)
		hash = _hash_int(hash, unit.health)
		hash = _hash_int(hash, unit.max_health)
		hash = _hash_int(hash, unit.vision_radius)
		hash = _hash_vector(hash, unit.destination)
		hash = _hash_int(hash, unit.path_revision)
		hash = _hash_int(hash, unit.path_index)
		hash = _hash_int(hash, unit.replan_count)
		hash = _hash_int(hash, unit.path.size())
		for cell: Vector2i in unit.path:
			hash = _hash_vector(hash, cell)
	var projectile_ids: Array = structures.projectiles.keys()
	projectile_ids.sort()
	for raw_id: Variant in projectile_ids:
		var projectile: StructureScript.ProjectileRecord = structures.projectiles[raw_id] as StructureScript.ProjectileRecord
		hash = _hash_int(hash, projectile.id)
		hash = _hash_int(hash, projectile.owner)
		hash = _hash_int(hash, projectile.source_id)
		hash = _hash_int(hash, projectile.target_id)
		hash = _hash_vector(hash, projectile.cell)
		hash = _hash_vector(hash, projectile.last_target_cell)
		hash = _hash_int(hash, projectile.damage)
		hash = _hash_int(hash, projectile.speed_cells)
	for team: int in range(2):
		for value: int in vision.visible_bytes(team):
			hash = _hash_byte(hash, value)
		for value: int in vision.explored_bytes(team):
			hash = _hash_byte(hash, value)
	return hash


func state_hash_text() -> String:
	return "%08X" % state_hash()


func validate_state() -> String:
	for raw_id: Variant in units.keys():
		var unit: UnitRecord = get_unit(int(raw_id))
		if unit == null or unit.id != int(raw_id):
			return "unit_id_mismatch"
		if not topology.contains(unit.cell) or not topology.contains(unit.destination):
			return "unit_out_of_bounds"
		for cell: Vector2i in unit.path:
			if not topology.contains(cell):
				return "path_out_of_bounds"
	for raw_id: Variant in structures.structures.keys():
		var structure: StructureScript.StructureRecord = structures.get_structure(int(raw_id))
		if structure == null or structure.id != int(raw_id):
			return "structure_id_mismatch"
		if not topology.contains(structure.cell):
			return "structure_out_of_bounds"
		if structure.is_attachment() and structures.get_structure(structure.attach_to_id) == null:
			return "attachment_base_missing"
	for team: int in range(2):
		if structures.resources_for(team) < 0:
			return "negative_resources"
	return ""


func _replan(unit: UnitRecord) -> void:
	unit.path = topology.find_path(unit.cell, unit.destination, unit.team)
	unit.path_index = 1 if unit.path.size() > 1 else unit.path.size()
	unit.path_revision = topology.revision
	unit.replan_count += 1


func _blocking_request_hits_live_unit(request: Dictionary) -> bool:
	var definition_id: String = String(request.get("definition", ""))
	var definition: Dictionary = structures.definitions.get(definition_id, {})
	if definition.is_empty() or bool(definition.get("attachment", false)):
		return false
	var blocks_movement: bool = String(definition.get("kind", "")) == "gate" or bool(definition.get("blocksMovement", true))
	if not blocks_movement:
		return false
	var raw_position: Variant = request.get("position")
	if not raw_position is Vector2i:
		return false
	var world_position: Vector2i = raw_position
	if world_position.x < 0 or world_position.y < 0 or world_position.x >= topology.width * TopologyScript.CELL_SIZE or world_position.y >= topology.height * TopologyScript.CELL_SIZE:
		return false
	var cell: Vector2i = topology.to_cell(world_position)
	for raw_id: Variant in units.keys():
		var unit: UnitRecord = get_unit(int(raw_id))
		if unit != null and unit.is_alive() and unit.cell == cell:
			return true
	return false


func _hash_int(hash: int, value: int) -> int:
	var result: int = hash
	for shift: int in [0, 8, 16, 24]:
		result = _hash_byte(result, value >> shift)
	return result


func _hash_vector(hash: int, value: Vector2i) -> int:
	return _hash_int(_hash_int(hash, value.x), value.y)


func _hash_string(hash: int, value: String) -> int:
	var result: int = _hash_int(hash, value.length())
	for byte: int in value.to_utf8_buffer():
		result = _hash_byte(result, byte)
	return result


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
			return _hash_string(result, String(value))
		TYPE_STRING, TYPE_STRING_NAME:
			return _hash_string(result, String(value))
		TYPE_VECTOR2I:
			return _hash_vector(result, value as Vector2i)
		TYPE_ARRAY:
			var values: Array = value
			result = _hash_int(result, values.size())
			for item: Variant in values:
				result = _hash_variant(result, item)
			return result
		TYPE_DICTIONARY:
			var dictionary: Dictionary = value
			var keys: Array = dictionary.keys()
			keys.sort_custom(func(left: Variant, right: Variant) -> bool: return String(left) < String(right))
			result = _hash_int(result, keys.size())
			for key: Variant in keys:
				result = _hash_string(result, String(key))
				result = _hash_variant(result, dictionary[key])
			return result
		_:
			return _hash_string(result, var_to_str(value))


func _hash_byte(hash: int, value: int) -> int:
	return int(((hash ^ (value & 0xff)) * FNV_PRIME) & 0xffffffff)

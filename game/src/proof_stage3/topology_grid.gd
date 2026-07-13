class_name Stage3TopologyGrid
extends RefCounted
## Team-aware deterministic topology used by the isolated Stage 3 proof.

const CELL_SIZE: int = 1000
const TEAM_BLUE: int = 0
const TEAM_RED: int = 1
const BLOCK_BLUE: int = 1
const BLOCK_RED: int = 2
const BLOCK_BOTH: int = BLOCK_BLUE | BLOCK_RED
const INF: int = 0x3fffffff
const NEIGHBORS: Array[Vector2i] = [
	Vector2i(0, -1),
	Vector2i(1, 0),
	Vector2i(0, 1),
	Vector2i(-1, 0),
]

var width: int
var height: int
var revision: int = 0
var _block_masks: PackedByteArray


func _init(p_width: int = 32, p_height: int = 24) -> void:
	assert(p_width >= 3 and p_height >= 3)
	width = p_width
	height = p_height
	_block_masks.resize(width * height)
	_block_masks.fill(0)


func contains(cell: Vector2i) -> bool:
	return cell.x >= 0 and cell.x < width and cell.y >= 0 and cell.y < height


func to_cell(position: Vector2i) -> Vector2i:
	return Vector2i(
		clampi(floori(float(position.x) / float(CELL_SIZE)), 0, width - 1),
		clampi(floori(float(position.y) / float(CELL_SIZE)), 0, height - 1)
	)


func snap_position(position: Vector2i) -> Vector2i:
	return cell_center(to_cell(position))


static func cell_center(cell: Vector2i) -> Vector2i:
	return Vector2i(cell.x * CELL_SIZE + CELL_SIZE / 2, cell.y * CELL_SIZE + CELL_SIZE / 2)


static func normalize_quarter_rotation(rotation_quarters: int) -> int:
	return posmod(rotation_quarters, 4)


static func quarter_direction(rotation_quarters: int) -> Vector2i:
	match normalize_quarter_rotation(rotation_quarters):
		0:
			return Vector2i.RIGHT
		1:
			return Vector2i.DOWN
		2:
			return Vector2i.LEFT
		_:
			return Vector2i.UP


func mask_at(cell: Vector2i) -> int:
	if not contains(cell):
		return BLOCK_BOTH
	return int(_block_masks[_index(cell)])


func is_blocked(cell: Vector2i, team: int) -> bool:
	if not contains(cell):
		return true
	var team_bit: int = BLOCK_BLUE if team == TEAM_BLUE else BLOCK_RED
	return (mask_at(cell) & team_bit) != 0


func apply_masks(changes: Array[Dictionary]) -> bool:
	## Applies a set of final cell masks as one topology revision.
	var seen: Dictionary = {}
	var changed: bool = false
	for change: Dictionary in changes:
		var cell: Vector2i = change.get("cell", Vector2i(-1, -1))
		var mask: int = int(change.get("mask", -1))
		if not contains(cell) or mask < 0 or mask > BLOCK_BOTH or seen.has(cell):
			return false
		seen[cell] = true
		changed = changed or mask_at(cell) != mask
	if not changed:
		return true
	for change: Dictionary in changes:
		var cell: Vector2i = change["cell"]
		_block_masks[_index(cell)] = int(change["mask"])
	revision += 1
	return true


func find_path(start: Vector2i, destination: Vector2i, team: int) -> Array[Vector2i]:
	if not contains(start) or not contains(destination):
		return []
	if is_blocked(start, team) or is_blocked(destination, team):
		return []
	var count: int = width * height
	var costs := PackedInt32Array()
	costs.resize(count)
	costs.fill(INF)
	var parents := PackedInt32Array()
	parents.resize(count)
	parents.fill(-1)
	var closed := PackedByteArray()
	closed.resize(count)
	closed.fill(0)
	var start_index: int = _index(start)
	var destination_index: int = _index(destination)
	costs[start_index] = 0
	var sequence: int = 0
	var start_h: int = _manhattan(start, destination) * 10
	var open: Array[Array] = [[start_index, start_h, start_h, start.y, start.x, sequence]]
	sequence += 1
	while not open.is_empty():
		var best: int = 0
		for i: int in range(1, open.size()):
			if _open_less(open[i], open[best]):
				best = i
		var node: Array = open.pop_at(best)
		var current_index: int = int(node[0])
		if closed[current_index] != 0:
			continue
		closed[current_index] = 1
		if current_index == destination_index:
			return _reconstruct(parents, start_index, destination_index)
		var current: Vector2i = _from_index(current_index)
		for offset: Vector2i in NEIGHBORS:
			var neighbor: Vector2i = current + offset
			if is_blocked(neighbor, team):
				continue
			var neighbor_index: int = _index(neighbor)
			if closed[neighbor_index] != 0:
				continue
			var tentative: int = costs[current_index] + 10
			if tentative >= costs[neighbor_index]:
				continue
			costs[neighbor_index] = tentative
			parents[neighbor_index] = current_index
			var heuristic: int = _manhattan(neighbor, destination) * 10
			open.append([neighbor_index, tentative + heuristic, heuristic, neighbor.y, neighbor.x, sequence])
			sequence += 1
	return []


func path_is_walkable(path: Array[Vector2i], from_index: int, team: int) -> bool:
	for i: int in range(maxi(0, from_index), path.size()):
		if is_blocked(path[i], team):
			return false
	return true


func blocked_cells(team: int) -> Array[Vector2i]:
	var result: Array[Vector2i] = []
	for y: int in range(height):
		for x: int in range(width):
			var cell := Vector2i(x, y)
			if is_blocked(cell, team):
				result.append(cell)
	return result


func _open_less(left: Array, right: Array) -> bool:
	# total cost, heuristic, y, x, then insertion sequence.
	for field: int in range(1, 6):
		var a: int = int(left[field])
		var b: int = int(right[field])
		if a != b:
			return a < b
	return false


func _reconstruct(parents: PackedInt32Array, start_index: int, destination_index: int) -> Array[Vector2i]:
	var reverse_path: Array[Vector2i] = []
	var cursor: int = destination_index
	while cursor != -1:
		reverse_path.append(_from_index(cursor))
		if cursor == start_index:
			reverse_path.reverse()
			return reverse_path
		cursor = parents[cursor]
	return []


func _index(cell: Vector2i) -> int:
	return cell.y * width + cell.x


func _from_index(index: int) -> Vector2i:
	return Vector2i(index % width, index / width)


func _manhattan(a: Vector2i, b: Vector2i) -> int:
	return absi(a.x - b.x) + absi(a.y - b.y)

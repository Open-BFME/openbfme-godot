class_name Stage1Grid
extends RefCounted
## Deterministic four-neighbor A* with declared tie-breaking.

const CELL_SIZE := 1000
const INF := 0x3fffffff
const NEIGHBORS: Array[Vector2i] = [
	Vector2i(0, -1),
	Vector2i(1, 0),
	Vector2i(0, 1),
	Vector2i(-1, 0),
]

var width: int
var height: int
var _blocked: PackedByteArray

func _init(p_width: int = 48, p_height: int = 32) -> void:
	assert(p_width >= 3 and p_height >= 3)
	width = p_width
	height = p_height
	_blocked.resize(width * height)
	_blocked.fill(0)

func clear() -> void:
	_blocked.fill(0)

func contains(cell: Vector2i) -> bool:
	return cell.x >= 0 and cell.x < width and cell.y >= 0 and cell.y < height

func set_blocked(cell: Vector2i, value: bool = true) -> void:
	assert(contains(cell))
	_blocked[_index(cell)] = 1 if value else 0

func is_blocked(cell: Vector2i) -> bool:
	return not contains(cell) or _blocked[_index(cell)] != 0

func is_walkable(position: Vector2i) -> bool:
	return not is_blocked(to_cell(position))

func to_cell(position: Vector2i) -> Vector2i:
	return Vector2i(
		clampi(position.x / CELL_SIZE, 0, width - 1),
		clampi(position.y / CELL_SIZE, 0, height - 1)
	)

static func cell_center(cell: Vector2i) -> Vector2i:
	return Vector2i(cell.x * CELL_SIZE + CELL_SIZE / 2, cell.y * CELL_SIZE + CELL_SIZE / 2)

func blocked_cells() -> Array[Vector2i]:
	var result: Array[Vector2i] = []
	for y in height:
		for x in width:
			var cell := Vector2i(x, y)
			if is_blocked(cell):
				result.append(cell)
	return result

func find_path(start_position: Vector2i, destination_position: Vector2i) -> Array[Vector2i]:
	var start := to_cell(start_position)
	var destination := to_cell(destination_position)
	if is_blocked(start) or is_blocked(destination):
		return []
	var count := width * height
	var costs := PackedInt32Array()
	costs.resize(count)
	costs.fill(INF)
	var parents := PackedInt32Array()
	parents.resize(count)
	parents.fill(-1)
	var closed := PackedByteArray()
	closed.resize(count)
	closed.fill(0)
	var start_index := _index(start)
	var destination_index := _index(destination)
	costs[start_index] = 0
	var sequence := 0
	var start_h := _manhattan(start, destination) * 10
	var open: Array[Array] = [[start_index, start_h, start_h, start.y, start.x, sequence]]
	sequence += 1
	while not open.is_empty():
		var best := 0
		for i in range(1, open.size()):
			if _open_less(open[i], open[best]):
				best = i
		var node: Array = open.pop_at(best)
		var current_index := int(node[0])
		if closed[current_index] != 0:
			continue
		closed[current_index] = 1
		if current_index == destination_index:
			return _reconstruct(parents, start_index, destination_index)
		var current := _from_index(current_index)
		for offset in NEIGHBORS:
			var neighbor := current + offset
			if is_blocked(neighbor):
				continue
			var neighbor_index := _index(neighbor)
			if closed[neighbor_index] != 0:
				continue
			var tentative := costs[current_index] + 10
			if tentative >= costs[neighbor_index]:
				continue
			costs[neighbor_index] = tentative
			parents[neighbor_index] = current_index
			var heuristic := _manhattan(neighbor, destination) * 10
			open.append([neighbor_index, tentative + heuristic, heuristic, neighbor.y, neighbor.x, sequence])
			sequence += 1
	return []

func _open_less(left: Array, right: Array) -> bool:
	# total, heuristic, y, x, insertion sequence
	for field in range(1, 6):
		var a := int(left[field])
		var b := int(right[field])
		if a != b:
			return a < b
	return false

func _reconstruct(parents: PackedInt32Array, start_index: int, destination_index: int) -> Array[Vector2i]:
	var reverse_path: Array[Vector2i] = []
	var cursor := destination_index
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

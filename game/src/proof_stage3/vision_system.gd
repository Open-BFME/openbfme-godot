class_name Stage3VisionSystem
extends RefCounted
## Per-team explored and currently-visible bitsets.

const TEAM_COUNT: int = 2

var width: int
var height: int
var _visible: Array[PackedByteArray] = []
var _explored: Array[PackedByteArray] = []


func _init(p_width: int, p_height: int) -> void:
	assert(p_width > 0 and p_height > 0)
	width = p_width
	height = p_height
	var byte_count: int = (width * height + 7) / 8
	for _team: int in range(TEAM_COUNT):
		var visible_bits := PackedByteArray()
		visible_bits.resize(byte_count)
		visible_bits.fill(0)
		_visible.append(visible_bits)
		var explored_bits := PackedByteArray()
		explored_bits.resize(byte_count)
		explored_bits.fill(0)
		_explored.append(explored_bits)


func begin_tick() -> void:
	for team: int in range(TEAM_COUNT):
		var bits: PackedByteArray = _visible[team]
		bits.fill(0)
		_visible[team] = bits


func reveal(team: int, center: Vector2i, radius_cells: int) -> void:
	assert(team >= 0 and team < TEAM_COUNT)
	assert(radius_cells >= 0)
	var visible_bits: PackedByteArray = _visible[team]
	var explored_bits: PackedByteArray = _explored[team]
	var radius_squared: int = radius_cells * radius_cells
	for y: int in range(maxi(0, center.y - radius_cells), mini(height - 1, center.y + radius_cells) + 1):
		for x: int in range(maxi(0, center.x - radius_cells), mini(width - 1, center.x + radius_cells) + 1):
			var dx: int = x - center.x
			var dy: int = y - center.y
			if dx * dx + dy * dy > radius_squared:
				continue
			var index: int = y * width + x
			_set_bit(visible_bits, index)
			_set_bit(explored_bits, index)
	_visible[team] = visible_bits
	_explored[team] = explored_bits


func is_visible(team: int, cell: Vector2i) -> bool:
	return _valid_query(team, cell) and _get_bit(_visible[team], _index(cell))


func is_explored(team: int, cell: Vector2i) -> bool:
	return _valid_query(team, cell) and _get_bit(_explored[team], _index(cell))


func visible_count(team: int) -> int:
	return _count_bits(_visible[team]) if team >= 0 and team < TEAM_COUNT else 0


func explored_count(team: int) -> int:
	return _count_bits(_explored[team]) if team >= 0 and team < TEAM_COUNT else 0


func visible_bytes(team: int) -> PackedByteArray:
	assert(team >= 0 and team < TEAM_COUNT)
	return _visible[team].duplicate()


func explored_bytes(team: int) -> PackedByteArray:
	assert(team >= 0 and team < TEAM_COUNT)
	return _explored[team].duplicate()


func _valid_query(team: int, cell: Vector2i) -> bool:
	return team >= 0 and team < TEAM_COUNT and cell.x >= 0 and cell.x < width and cell.y >= 0 and cell.y < height


func _index(cell: Vector2i) -> int:
	return cell.y * width + cell.x


func _set_bit(bits: PackedByteArray, index: int) -> void:
	var byte_index: int = index >> 3
	bits[byte_index] = int(bits[byte_index]) | (1 << (index & 7))


func _get_bit(bits: PackedByteArray, index: int) -> bool:
	return (int(bits[index >> 3]) & (1 << (index & 7))) != 0


func _count_bits(bits: PackedByteArray) -> int:
	var total: int = 0
	for value: int in bits:
		var cursor: int = value
		while cursor != 0:
			total += cursor & 1
			cursor >>= 1
	return total

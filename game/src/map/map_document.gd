class_name NativeMapDocument
extends RefCounted
## Read-only presentation decoder for contracts/map-v1 documents.

const SCHEMA := "openbfme.map.v1"
const MAX_DOCUMENT_BYTES := 128 * 1024 * 1024
const MAX_GRID_CELLS := 4 * 1024 * 1024
const HEIGHT_SCALE := 5.0 / 128.0

var path := ""
var error := ""
var source: Dictionary = {}
var world: Dictionary = {}
var start_positions: Dictionary = {}
var objects: Array = []
var plots: Array = []
var grid_width := 0
var grid_height := 0
var cell_size := 0
var height_samples := PackedInt32Array()
var passability_bits := PackedByteArray()
var passability_row_stride := 0


func load_path(document_path: String) -> bool:
	_reset()
	path = ProjectSettings.globalize_path(document_path) if document_path.begins_with("res://") else document_path
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return _fail("map-v1 could not be opened: %s" % path)
	var byte_count := file.get_length()
	if byte_count <= 0 or byte_count > MAX_DOCUMENT_BYTES:
		return _fail("map-v1 byte count is invalid or unbounded: %d" % byte_count)
	var parsed: Variant = JSON.parse_string(file.get_as_text())
	file.close()
	if not (parsed is Dictionary):
		return _fail("map-v1 JSON root is not an object")
	var document := parsed as Dictionary
	if String(document.get("schema", "")) != SCHEMA:
		return _fail("map-v1 schema is not %s" % SCHEMA)
	source = _dictionary(document.get("source"))
	world = _dictionary(document.get("world"))
	var heights := _dictionary(document.get("height_grid"))
	var passability := _dictionary(document.get("passability_grid"))
	start_positions = _dictionary(document.get("start_positions")).duplicate(true)
	objects = _array(document.get("objects")).duplicate(true)
	plots = _array(document.get("plots")).duplicate(true)
	grid_width = int(heights.get("width", 0))
	grid_height = int(heights.get("height", 0))
	cell_size = int(world.get("cell_size", 0))
	var cells := grid_width * grid_height
	if grid_width < 2 or grid_height < 2 or cells > MAX_GRID_CELLS or cell_size <= 0:
		return _fail("map-v1 grid dimensions or cell size are invalid")
	if (
		int(world.get("width", 0)) != grid_width * cell_size
		or int(world.get("height", 0)) != grid_height * cell_size
		or String(heights.get("encoding", "")) != "uint16-little-endian-row-major"
	):
		return _fail("map-v1 world extent or height encoding is inconsistent")
	var height_bytes := Marshalls.base64_to_raw(String(heights.get("data_base64", "")))
	if height_bytes.size() != cells * 2:
		return _fail("map-v1 height payload length does not match its grid")
	height_samples.resize(cells)
	for index in cells:
		height_samples[index] = int(height_bytes[index * 2]) | (int(height_bytes[index * 2 + 1]) << 8)
	passability_row_stride = int(passability.get("row_stride_bytes", 0))
	passability_bits = Marshalls.base64_to_raw(String(passability.get("data_base64", "")))
	if (
		int(passability.get("width", 0)) != grid_width
		or int(passability.get("height", 0)) != grid_height
		or String(passability.get("encoding", "")) != "one-is-impassable-lsb-first-row-padded"
		or passability_row_stride != (grid_width + 7) / 8
		or passability_bits.size() != passability_row_stride * grid_height
	):
		return _fail("map-v1 passability payload is inconsistent")
	if start_positions.is_empty():
		return _fail("map-v1 has no start positions")
	return true


func height_raw_at(x: int, y: int) -> int:
	var safe_x := clampi(x, 0, grid_width - 1)
	var safe_y := clampi(y, 0, grid_height - 1)
	return height_samples[safe_y * grid_width + safe_x]


func height_at_grid(x: int, y: int) -> float:
	return float(height_raw_at(x, y)) * HEIGHT_SCALE


func height_at_world(horizontal: Vector2) -> float:
	if grid_width <= 0 or grid_height <= 0:
		return 0.0
	var grid := horizontal / float(cell_size)
	var x0 := clampi(int(floor(grid.x)), 0, grid_width - 1)
	var y0 := clampi(int(floor(grid.y)), 0, grid_height - 1)
	var x1 := mini(x0 + 1, grid_width - 1)
	var y1 := mini(y0 + 1, grid_height - 1)
	var fx := clampf(grid.x - float(x0), 0.0, 1.0)
	var fy := clampf(grid.y - float(y0), 0.0, 1.0)
	var low := lerpf(height_at_grid(x0, y0), height_at_grid(x1, y0), fx)
	var high := lerpf(height_at_grid(x0, y1), height_at_grid(x1, y1), fx)
	return lerpf(low, high, fy)


func is_impassable_at(x: int, y: int) -> bool:
	if x < 0 or y < 0 or x >= grid_width or y >= grid_height:
		return true
	var byte_index := y * passability_row_stride + x / 8
	return (int(passability_bits[byte_index]) & (1 << (x % 8))) != 0


func start_horizontal(index: int) -> Vector2:
	var row := _dictionary(start_positions.get(str(index)))
	return Vector2(float(row.get("x", 0.0)), float(row.get("y", 0.0)))


func start_world(index: int) -> Vector3:
	var horizontal := start_horizontal(index)
	return Vector3(horizontal.x, height_at_world(horizontal), horizontal.y)


func _reset() -> void:
	error = ""
	source.clear()
	world.clear()
	start_positions.clear()
	objects.clear()
	plots.clear()
	grid_width = 0
	grid_height = 0
	cell_size = 0
	height_samples = PackedInt32Array()
	passability_bits = PackedByteArray()
	passability_row_stride = 0


func _fail(message: String) -> bool:
	error = message
	return false


func _dictionary(value: Variant) -> Dictionary:
	return value as Dictionary if value is Dictionary else {}


func _array(value: Variant) -> Array:
	return value as Array if value is Array else []

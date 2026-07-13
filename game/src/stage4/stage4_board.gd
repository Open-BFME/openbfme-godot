class_name Stage4Board
extends Node2D
## Primitive top-down presentation of the deterministic Stage 4 proof world.

const CELL_SIZE: float = 50.0
const BLUE: Color = Color("58b8ff")
const BLUE_DARK: Color = Color("174d73")
const RED: Color = Color("ff7067")
const RED_DARK: Color = Color("762e34")
const GOLD: Color = Color("ffd166")
const TARGET: Color = Color("ed8cff")

var world: RefCounted
var selected_entity_id: int = 0
var target_entity_id: int = 0
var targeting_mode: String = ""
var targeting_range: int = 0
var hover_cell: Vector2i = Vector2i(-1, -1)


func configure(p_world: RefCounted) -> void:
	world = p_world
	queue_redraw()


func set_selection(entity_id: int, inspected_target_id: int = 0) -> void:
	selected_entity_id = entity_id
	target_entity_id = inspected_target_id
	queue_redraw()


func set_targeting(mode: String, range_cells: int) -> void:
	targeting_mode = mode
	targeting_range = range_cells
	queue_redraw()


func clear_targeting() -> void:
	targeting_mode = ""
	targeting_range = 0
	queue_redraw()


func update_hover(screen_position: Vector2) -> void:
	var cell: Vector2i = cell_from_screen(screen_position)
	if cell != hover_cell:
		hover_cell = cell
		queue_redraw()


func cell_from_screen(screen_position: Vector2) -> Vector2i:
	var local: Vector2 = to_local(screen_position)
	var cell := Vector2i(floori(local.x / CELL_SIZE), floori(local.y / CELL_SIZE))
	if world == null or not bool(world.contains_cell(cell)):
		return Vector2i(-1, -1)
	return cell


func pick_entity(screen_position: Vector2, radius: float = 29.0) -> int:
	if world == null:
		return 0
	var local: Vector2 = to_local(screen_position)
	var best_id: int = 0
	var best_distance: float = radius
	for entity_id: int in world.entity_ids():
		var row: Dictionary = world.entity(entity_id)
		var center: Vector2 = cell_center(Vector2i(row.get("position", Vector2i.ZERO)))
		var distance: float = local.distance_to(center)
		if distance < best_distance or (is_equal_approx(distance, best_distance) and (best_id == 0 or entity_id < best_id)):
			best_id = entity_id
			best_distance = distance
	return best_id


func cell_center(cell: Vector2i) -> Vector2:
	return Vector2(float(cell.x) + 0.5, float(cell.y) + 0.5) * CELL_SIZE


func board_size() -> Vector2:
	if world == null:
		return Vector2(16.0, 12.0) * CELL_SIZE
	return Vector2(float(world.width), float(world.height)) * CELL_SIZE


func _draw() -> void:
	if world == null:
		return
	var size: Vector2 = board_size()
	draw_rect(Rect2(Vector2(9.0, 12.0), size), Color(0.0, 0.0, 0.0, 0.36), true)
	draw_rect(Rect2(Vector2.ZERO, size), Color("0c1924"), true)
	for y: int in range(world.height):
		for x: int in range(world.width):
			var cell := Vector2i(x, y)
			var rect := Rect2(Vector2(x, y) * CELL_SIZE, Vector2.ONE * CELL_SIZE)
			var color := Color("132735") if (x + y) % 2 == 0 else Color("10222f")
			draw_rect(rect, color, true)
			if bool(world.is_blocked(cell)):
				draw_rect(rect.grow(-3.0), Color("563c3a"), true)
				draw_line(rect.position + Vector2(8, 8), rect.end - Vector2(8, 8), Color("d68d62"), 3.0)
				draw_line(Vector2(rect.end.x - 8, rect.position.y + 8), Vector2(rect.position.x + 8, rect.end.y - 8), Color("d68d62"), 3.0)
			draw_rect(rect, Color(0.26, 0.42, 0.52, 0.2), false, 1.0)
	_draw_targeting_cells()
	for entity_id: int in world.entity_ids():
		_draw_entity(world.entity(entity_id))
	if hover_cell.x >= 0:
		var hover_rect := Rect2(Vector2(hover_cell) * CELL_SIZE, Vector2.ONE * CELL_SIZE).grow(-2.0)
		draw_rect(hover_rect, GOLD if targeting_mode != "" else Color(0.7, 0.9, 1.0, 0.65), false, 2.0)
	draw_rect(Rect2(Vector2.ZERO, size), Color("477a91"), false, 3.0)


func _draw_targeting_cells() -> void:
	if targeting_mode == "" or selected_entity_id == 0:
		return
	var selected: Dictionary = world.entity(selected_entity_id)
	if selected.is_empty():
		return
	var origin: Vector2i = selected.get("position", Vector2i.ZERO)
	for y: int in range(world.height):
		for x: int in range(world.width):
			var cell := Vector2i(x, y)
			if int(world.distance_cells(origin, cell)) <= targeting_range:
				var tint := Color(0.2, 0.78, 1.0, 0.13) if not bool(world.is_blocked(cell)) else Color(1.0, 0.3, 0.25, 0.14)
				draw_rect(Rect2(Vector2(cell) * CELL_SIZE, Vector2.ONE * CELL_SIZE).grow(-3.0), tint, true)


func _draw_entity(row: Dictionary) -> void:
	var entity_id: int = int(row.get("id", 0))
	var team: int = int(row.get("team", 0))
	var alive: bool = bool(row.get("alive", false))
	var color: Color = BLUE if team == 0 else RED
	var dark: Color = BLUE_DARK if team == 0 else RED_DARK
	if not alive:
		color = Color("5c6670")
		dark = Color("2b3138")
	var center: Vector2 = cell_center(Vector2i(row.get("position", Vector2i.ZERO)))
	if entity_id == selected_entity_id:
		draw_circle(center, 27.0, Color(GOLD, 0.2))
		draw_arc(center, 27.0, 0.0, TAU, 40, GOLD, 3.0)
	if entity_id == target_entity_id:
		draw_arc(center, 31.0, 0.0, TAU, 40, TARGET, 3.0)
	if String(row.get("kind", "")) == "champion":
		var points := PackedVector2Array([
			center + Vector2(0, -20),
			center + Vector2(18, 0),
			center + Vector2(0, 20),
			center + Vector2(-18, 0),
		])
		draw_colored_polygon(points, color)
		draw_polyline(PackedVector2Array([points[0], points[1], points[2], points[3], points[0]]), dark, 3.0)
		if not alive:
			draw_line(center - Vector2(13, 13), center + Vector2(13, 13), RED, 3.0)
			draw_line(center + Vector2(-13, 13), center + Vector2(13, -13), RED, 3.0)
		_draw_health_bar(center + Vector2(-24, 25), int(row.get("health", 0)), int(row.get("max_health", 1)), color)
		_draw_label(center + Vector2(-38, -37), "%s Champion R%d" % ["BLUE" if team == 0 else "RED", int(row.get("rank", 1))], color)
	else:
		_draw_squad(row, center, color, dark)
	var status: String = String(row.get("status", "normal"))
	if status != "normal":
		_draw_label(center + Vector2(-28, 38), status.to_upper(), Color("f2b5ff") if status == "flee" else Color("ff9c9c"))
	if status == "flee":
		var direction: Vector2i = row.get("flee_direction", Vector2i.ZERO)
		var end: Vector2 = center + Vector2(direction) * 24.0
		draw_line(center, end, Color("f2b5ff"), 4.0)
		draw_circle(end, 4.0, Color("f2b5ff"))


func _draw_squad(row: Dictionary, center: Vector2, color: Color, dark: Color) -> void:
	var members: Array = row.get("members", [])
	var columns: int = maxi(1, ceili(sqrt(float(members.size()))))
	var spacing: float = 11.0
	for index: int in range(members.size()):
		var member: Dictionary = members[index]
		var column: int = index % columns
		var line: int = index / columns
		var offset := Vector2((float(column) - float(columns - 1) * 0.5) * spacing, (float(line) - 0.5) * spacing)
		if bool(member.get("alive", false)):
			var member_health: float = float(int(member.get("health", 0))) / float(maxi(1, int(member.get("max_health", 1))))
			draw_circle(center + offset, 5.0, dark)
			draw_circle(center + offset, 3.5, color.lerp(Color.WHITE, 0.25 * member_health))
		else:
			draw_circle(center + offset, 4.5, Color("59616a"), false, 1.5)
			draw_line(center + offset - Vector2(3, 3), center + offset + Vector2(3, 3), Color("8d5660"), 1.0)
	var living: int = int(world.living_member_ids(int(row["id"])).size())
	_draw_label(center + Vector2(-32, -34), "%s Squad %d/%d" % ["BLUE" if int(row["team"]) == 0 else "RED", living, members.size()], color)


func _draw_health_bar(position: Vector2, health: int, maximum: int, color: Color) -> void:
	var size := Vector2(48, 6)
	draw_rect(Rect2(position, size), Color("1a2028"), true)
	var ratio: float = clampf(float(health) / float(maxi(1, maximum)), 0.0, 1.0)
	draw_rect(Rect2(position + Vector2.ONE, Vector2((size.x - 2.0) * ratio, size.y - 2.0)), color, true)


func _draw_label(position: Vector2, text: String, color: Color) -> void:
	draw_string(ThemeDB.fallback_font, position, text, HORIZONTAL_ALIGNMENT_LEFT, -1, 12, Color(0.0, 0.0, 0.0, 0.8), 3)
	draw_string(ThemeDB.fallback_font, position, text, HORIZONTAL_ALIGNMENT_LEFT, -1, 12, color)

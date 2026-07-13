class_name Stage5Board
extends Node2D
## Primitive legal-safe rendering for the deterministic Stage 5 proof world.

const CELL_SIZE: float = 48.0
const BLUE: Color = Color("55b9ff")
const RED: Color = Color("ff7067")
const GOLD: Color = Color("ffd166")
const GREEN: Color = Color("75e6a4")

var world: RefCounted
var targeting_mode: String = ""
var hover_cell: Vector2i = Vector2i(-1, -1)


func configure(p_world: RefCounted) -> void:
	world = p_world
	queue_redraw()


func set_targeting(mode: String) -> void:
	targeting_mode = mode
	queue_redraw()


func clear_targeting() -> void:
	targeting_mode = ""
	queue_redraw()


func update_hover(screen_position: Vector2) -> void:
	var next_hover: Vector2i = cell_from_screen(screen_position)
	if next_hover != hover_cell:
		hover_cell = next_hover
		queue_redraw()


func cell_from_screen(screen_position: Vector2) -> Vector2i:
	var local: Vector2 = to_local(screen_position)
	var cell := Vector2i(floori(local.x / CELL_SIZE), floori(local.y / CELL_SIZE))
	if world == null or not bool(world.contains_cell(cell)):
		return Vector2i(-1, -1)
	return cell


func pick_entity(screen_position: Vector2, preferred_kind: String = "", preferred_team: int = -1) -> int:
	var cell: Vector2i = cell_from_screen(screen_position)
	return 0 if cell.x < 0 or world == null else int(world.entity_at(cell, preferred_kind, preferred_team))


func board_size() -> Vector2:
	if world == null:
		return Vector2(16, 10) * CELL_SIZE
	return Vector2(float(world.width), float(world.height)) * CELL_SIZE


func cell_center(cell: Vector2i) -> Vector2:
	return (Vector2(cell) + Vector2(0.5, 0.5)) * CELL_SIZE


func _draw() -> void:
	if world == null:
		return
	var size: Vector2 = board_size()
	draw_rect(Rect2(Vector2(9, 11), size), Color(0, 0, 0, 0.4), true)
	draw_rect(Rect2(Vector2.ZERO, size), Color("0b1822"), true)
	for y: int in range(world.height):
		for x: int in range(world.width):
			var rect := Rect2(Vector2(x, y) * CELL_SIZE, Vector2.ONE * CELL_SIZE)
			var cell_color := Color("132a37") if (x + y) % 2 == 0 else Color("102330")
			draw_rect(rect, cell_color, true)
			draw_rect(rect, Color(0.3, 0.5, 0.62, 0.18), false, 1.0)
	if not Dictionary(world.weather).is_empty():
		var pulse: float = 0.13 + 0.025 * float(int(world.tick_index) % 2)
		draw_rect(Rect2(Vector2.ZERO, size), Color(0.48, 0.36, 0.84, pulse), true)
		for x: int in range(0, int(size.x), 64):
			draw_line(Vector2(x, 0), Vector2(x + 120, size.y), Color(0.75, 0.72, 1.0, 0.13), 2.0)
	for entity_id: int in world.entity_ids():
		_draw_entity(world.entity(entity_id))
	if hover_cell.x >= 0:
		var hover_rect := Rect2(Vector2(hover_cell) * CELL_SIZE, Vector2.ONE * CELL_SIZE).grow(-2.0)
		draw_rect(hover_rect, GOLD if targeting_mode != "" else Color(0.7, 0.9, 1.0, 0.7), false, 3.0)
	draw_rect(Rect2(Vector2.ZERO, size), Color("4b7f96"), false, 3.0)


func _draw_entity(row: Dictionary) -> void:
	var entity_id: int = int(row.get("id", 0))
	var team: int = int(row.get("team", 0))
	var health: int = int(row.get("health", 0))
	var maximum: int = maxi(1, int(row.get("max_health", 1)))
	var color: Color = BLUE if team == 0 else RED
	if health <= 0:
		color = Color("68737c")
	var center: Vector2 = cell_center(Vector2i(row.get("position", Vector2i.ZERO)))
	if String(row.get("kind", "")) == "building":
		var rect := Rect2(center - Vector2(17, 17), Vector2(34, 34))
		draw_rect(rect, color.darkened(0.48), true)
		draw_rect(rect, color, false, 3.0)
		draw_line(rect.position + Vector2(6, 6), rect.end - Vector2(6, 6), color, 2.0)
		draw_line(Vector2(rect.end.x - 6, rect.position.y + 6), Vector2(rect.position.x + 6, rect.end.y - 6), color, 2.0)
	else:
		draw_circle(center, 15.0, color.darkened(0.5))
		draw_circle(center, 11.0, color)
		draw_circle(center, 4.0, Color.WHITE if health > 0 else Color("39434b"))
	_draw_health_bar(center + Vector2(-20, 21), health, maximum, color)
	var label: String = "%s %s #%d" % ["B" if team == 0 else "R", "BLDG" if String(row.get("kind", "")) == "building" else "UNIT", entity_id]
	_draw_label(center + Vector2(-34, -27), label, color)


func _draw_health_bar(position: Vector2, health: int, maximum: int, color: Color) -> void:
	var size := Vector2(40, 6)
	draw_rect(Rect2(position, size), Color("10171e"), true)
	var ratio: float = clampf(float(health) / float(maximum), 0.0, 1.0)
	draw_rect(Rect2(position + Vector2.ONE, Vector2((size.x - 2.0) * ratio, size.y - 2.0)), color, true)


func _draw_label(position: Vector2, value: String, color: Color) -> void:
	draw_string(ThemeDB.fallback_font, position, value, HORIZONTAL_ALIGNMENT_LEFT, -1, 11, Color(0, 0, 0, 0.85), 3)
	draw_string(ThemeDB.fallback_font, position, value, HORIZONTAL_ALIGNMENT_LEFT, -1, 11, color)

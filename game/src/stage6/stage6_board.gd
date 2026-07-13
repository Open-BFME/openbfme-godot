class_name Stage6Board
extends Node2D
## Legal-safe primitive presentation for the Stage 6 faction matrix.

const CELL_SIZE: float = 56.0
const GOLD: Color = Color("ffd166")
const TARGET: Color = Color("ef86ff")

var world: RefCounted
var attacker_id: int = 0
var target_id: int = 0


func configure(p_world: RefCounted) -> void:
	world = p_world
	queue_redraw()


func set_selection(p_attacker_id: int, p_target_id: int) -> void:
	attacker_id = p_attacker_id
	target_id = p_target_id
	queue_redraw()


func cell_center(cell: Vector2i) -> Vector2:
	return (Vector2(cell) + Vector2(0.5, 0.5)) * CELL_SIZE


func pick_entity(screen_position: Vector2, radius: float = 27.0) -> int:
	if world == null:
		return 0
	var local: Vector2 = to_local(screen_position)
	var best_id: int = 0
	var best_distance: float = radius
	for entity_id: int in world.entity_ids():
		var row: Dictionary = world.entity(entity_id)
		if not bool(row.get("alive", false)):
			continue
		var distance := local.distance_to(cell_center(Vector2i(row["position"])))
		if distance < best_distance or (is_equal_approx(distance, best_distance) and (best_id == 0 or entity_id < best_id)):
			best_id = entity_id
			best_distance = distance
	return best_id


func _draw() -> void:
	if world == null:
		return
	var board_size := Vector2(float(world.width), float(world.height)) * CELL_SIZE
	draw_rect(Rect2(Vector2(8, 10), board_size), Color(0, 0, 0, 0.38), true)
	draw_rect(Rect2(Vector2.ZERO, board_size), Color("0b1820"), true)
	for y: int in range(world.height):
		for x: int in range(world.width):
			var rect := Rect2(Vector2(x, y) * CELL_SIZE, Vector2.ONE * CELL_SIZE)
			draw_rect(rect, Color("132a32") if (x + y) % 2 == 0 else Color("10242c"), true)
			draw_rect(rect, Color(0.35, 0.58, 0.62, 0.18), false, 1.0)
	for entity_id: int in world.entity_ids():
		_draw_entity(world.entity(entity_id))
	draw_rect(Rect2(Vector2.ZERO, board_size), Color("4d8992"), false, 3.0)


func _draw_entity(row: Dictionary) -> void:
	var entity_id: int = int(row["id"])
	var faction: Dictionary = world.catalog.faction(String(row["faction_id"]))
	var unit: Dictionary = world.catalog.unit(String(row["unit_id"]))
	var art: Dictionary = world.catalog.resolve_art(String(row["unit_id"]))
	var color := Color.from_string(String(faction.get("teamColor", "#ffffff")), Color.WHITE)
	if not bool(row.get("alive", false)):
		color = Color("69727a")
	var center := cell_center(Vector2i(row["position"]))
	if entity_id == attacker_id:
		draw_circle(center, 25.0, Color(GOLD, 0.2))
		draw_arc(center, 26.0, 0, TAU, 36, GOLD, 3.0)
	if entity_id == target_id:
		draw_arc(center, 30.0, 0, TAU, 36, TARGET, 3.0)
	_draw_shape(center, String(art.get("shape", "box")), color)
	var ratio: float = float(int(row["health"])) / float(maxi(1, int(row["maximum_health"])))
	draw_rect(Rect2(center + Vector2(-22, 21), Vector2(44, 5)), Color("172027"), true)
	draw_rect(Rect2(center + Vector2(-21, 22), Vector2(42.0 * ratio, 3)), color.lightened(0.2), true)
	_draw_label(center + Vector2(-48, -31), String(unit.get("displayName", row["unit_id"])), color)
	_draw_label(center + Vector2(-42, 39), "%s -> %s" % [String(unit["damageType"]), String(unit["armorClass"])], Color("b8cbd0"))


func _draw_shape(center: Vector2, shape: String, color: Color) -> void:
	var outline := color.darkened(0.45)
	match shape:
		"sphere":
			draw_circle(center, 17.0, outline)
			draw_circle(center, 13.0, color)
		"cylinder":
			draw_rect(Rect2(center - Vector2(13, 15), Vector2(26, 30)), color, true)
			draw_arc(center - Vector2(0, 15), 13.0, 0, PI, 20, outline, 3.0)
			draw_arc(center + Vector2(0, 15), 13.0, PI, TAU, 20, outline, 3.0)
		"capsule":
			draw_rect(Rect2(center - Vector2(11, 13), Vector2(22, 26)), color, true)
			draw_circle(center - Vector2(0, 13), 11.0, color)
			draw_circle(center + Vector2(0, 13), 11.0, color)
			draw_arc(center, 20.0, 0, TAU, 28, outline, 2.0)
		"diamond":
			var points := PackedVector2Array([center + Vector2(0, -20), center + Vector2(18, 0), center + Vector2(0, 20), center + Vector2(-18, 0)])
			draw_colored_polygon(points, color)
			draw_polyline(PackedVector2Array([points[0], points[1], points[2], points[3], points[0]]), outline, 3.0)
		"hexagon":
			var points := PackedVector2Array()
			for index: int in range(6):
				points.append(center + Vector2.from_angle(float(index) * TAU / 6.0) * 18.0)
			draw_colored_polygon(points, color)
			draw_polyline(PackedVector2Array([points[0], points[1], points[2], points[3], points[4], points[5], points[0]]), outline, 3.0)
		_:
			draw_rect(Rect2(center - Vector2(16, 16), Vector2(32, 32)), color, true)
			draw_rect(Rect2(center - Vector2(16, 16), Vector2(32, 32)), outline, false, 3.0)


func _draw_label(position: Vector2, text: String, color: Color) -> void:
	draw_string(ThemeDB.fallback_font, position, text, HORIZONTAL_ALIGNMENT_LEFT, -1, 12, Color(0, 0, 0, 0.85), 3)
	draw_string(ThemeDB.fallback_font, position, text, HORIZONTAL_ALIGNMENT_LEFT, -1, 12, color)

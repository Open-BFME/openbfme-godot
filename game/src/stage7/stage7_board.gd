class_name Stage7Board
extends Node2D
## Primitive strategic overview for the finite-resource AI victory loop.

var world: RefCounted


func configure(p_world: RefCounted) -> void:
	world = p_world
	queue_redraw()


func _draw() -> void:
	if world == null:
		return
	draw_rect(Rect2(Vector2.ZERO, Vector2(780, 560)), Color("0b1822"), true)
	for x: int in range(0, 781, 52):
		draw_line(Vector2(x, 0), Vector2(x, 560), Color(0.32, 0.52, 0.62, 0.12), 1.0)
	for y: int in range(0, 561, 52):
		draw_line(Vector2(0, y), Vector2(780, y), Color(0.32, 0.52, 0.62, 0.12), 1.0)
	_draw_blue_base()
	_draw_resource_site()
	_draw_army()
	_draw_enemy_fortress()
	_draw_plan()
	draw_rect(Rect2(Vector2.ZERO, Vector2(780, 560)), Color("4d7f99"), false, 3.0)


func _draw_blue_base() -> void:
	var center := Vector2(100, 180)
	draw_rect(Rect2(center - Vector2(45, 38), Vector2(90, 76)), Color("245f83"), true)
	draw_rect(Rect2(center - Vector2(45, 38), Vector2(90, 76)), Color("72c9ff"), false, 4.0)
	if world.extractor_built:
		draw_circle(center + Vector2(0, -55), 18, Color("69e7bb"))
		draw_arc(center + Vector2(0, -55), 18, 0, TAU, 28, Color("d2ffef"), 3)
	_draw_label(center + Vector2(-62, 58), "AI BASE%s" % [" + EXTRACTOR" if world.extractor_built else ""], Color("8edbff"), 15)


func _draw_resource_site() -> void:
	var center := Vector2(305, 130)
	var maximum: int = maxi(1, int(world.scenario["finiteResourceAmount"]))
	var remaining: int = int(world.finite_resource_remaining)
	var ratio: float = float(remaining) / float(maximum)
	for index: int in range(6):
		var angle: float = float(index) * TAU / 6.0
		var radius: float = 22.0 + float(index % 2) * 8.0
		draw_circle(center + Vector2.from_angle(angle) * radius, 10.0, Color("e6bd59").darkened(0.55 * (1.0 - ratio)))
	draw_arc(center, 46, 0, TAU * ratio, 40, Color("ffe29a"), 5.0)
	_draw_label(center + Vector2(-68, 70), "FINITE DEPOSIT %d/%d" % [remaining, maximum], Color("ffe29a"), 14)


func _draw_army() -> void:
	var origin := Vector2(305, 300)
	for index: int in range(int(world.army_size)):
		var offset := Vector2(float(index % 6) * 28.0, float(index / 6) * 30.0)
		draw_circle(origin + offset, 10.0, Color("55b8ff"))
		draw_arc(origin + offset, 10.0, 0, TAU, 18, Color("c9efff"), 2.0)
	_draw_label(origin + Vector2(-25, 60), "ARMY %d%s" % [int(world.army_size), "  ATTACKING" if world.attack_started else ""], Color("8edbff"), 16)


func _draw_enemy_fortress() -> void:
	var center := Vector2(650, 220)
	var ratio: float = float(int(world.enemy_fortress_health)) / float(maxi(1, int(world.enemy_fortress_maximum)))
	var color := Color("67d98d") if world.victory else Color("ff7167")
	draw_rect(Rect2(center - Vector2(62, 62), Vector2(124, 124)), color.darkened(0.55), true)
	draw_rect(Rect2(center - Vector2(62, 62), Vector2(124, 124)), color, false, 5.0)
	draw_rect(Rect2(center - Vector2(54, 82), Vector2(108, 10)), Color("171e25"), true)
	draw_rect(Rect2(center - Vector2(52, 80), Vector2(104 * ratio, 6)), color, true)
	_draw_label(center + Vector2(-78, 88), "ENEMY CORE %d/%d" % [int(world.enemy_fortress_health), int(world.enemy_fortress_maximum)], color, 16)
	if world.victory:
		_draw_label(center + Vector2(-46, 5), "VICTORY", Color("b9ffd0"), 20)


func _draw_plan() -> void:
	var steps: Array = world.planner.plan_definition.get("steps", [])
	var origin := Vector2(42, 470)
	for index: int in range(steps.size()):
		var step: Dictionary = steps[index]
		var rect := Rect2(origin + Vector2(index * 180, 0), Vector2(158, 62))
		var color := Color("4c9cc7") if index == int(world.plan_index) and not world.attack_started else Color("4e6774")
		if index < int(world.plan_index):
			color = Color("4b9c6c")
		draw_rect(rect, color.darkened(0.55), true)
		draw_rect(rect, color, false, 2.0)
		_draw_label(rect.position + Vector2(8, 23), "%d. %s" % [index + 1, String(step["action"]).to_upper()], color.lightened(0.3), 14)
		_draw_label(rect.position + Vector2(8, 45), String(step.get("objectId", "army threshold")), Color("b7cbd4"), 12)


func _draw_label(position: Vector2, text: String, color: Color, size: int) -> void:
	draw_string(ThemeDB.fallback_font, position, text, HORIZONTAL_ALIGNMENT_LEFT, -1, size, Color(0, 0, 0, 0.85), 3)
	draw_string(ThemeDB.fallback_font, position, text, HORIZONTAL_ALIGNMENT_LEFT, -1, size, color)

class_name Stage1SelectionBox
extends Control
## Lightweight selection marquee owned entirely by presentation.

var active: bool = false
var start_point: Vector2 = Vector2.ZERO
var end_point: Vector2 = Vector2.ZERO

func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	set_process(false)

func begin(point: Vector2) -> void:
	active = true
	start_point = point
	end_point = point
	queue_redraw()

func update_end(point: Vector2) -> void:
	if not active:
		return
	end_point = point
	queue_redraw()

func finish(point: Vector2) -> Rect2:
	end_point = point
	active = false
	queue_redraw()
	return selection_rect()

func cancel() -> void:
	active = false
	queue_redraw()

func selection_rect() -> Rect2:
	return Rect2(start_point, end_point - start_point).abs()

func _draw() -> void:
	if not active:
		return
	var rect := selection_rect()
	draw_rect(rect, Color(0.18, 0.72, 1.0, 0.16), true)
	draw_rect(rect, Color(0.35, 0.86, 1.0, 0.95), false, 2.0)

class_name OpenBFMENavDiamonds
extends Control
## Draws the small green diamond markers that sit above each shell navigation
## button, echoing the BFME2 main-menu row. Purely decorative: it never
## intercepts input and only renders for buttons that are currently visible.

const MARKER_OFFSET := 14.0
const MARKER_RADIUS := 7.0
const FILL := Color(0.24, 0.52, 0.26, 0.95)
const FILL_INNER := Color(0.45, 0.72, 0.42, 0.95)
const EDGE := Color(0.72, 0.60, 0.32, 0.9)

var watched_buttons: Array[Button] = []


func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	resized.connect(queue_redraw)


func watch(buttons: Array[Button]) -> void:
	watched_buttons = buttons
	queue_redraw()


func _draw() -> void:
	for button in watched_buttons:
		if button == null or not button.visible:
			continue
		var rect := button.get_global_rect()
		var center := Vector2(rect.get_center().x, rect.position.y - MARKER_OFFSET)
		var diamond := PackedVector2Array([
			center + Vector2(0, -MARKER_RADIUS),
			center + Vector2(MARKER_RADIUS, 0),
			center + Vector2(0, MARKER_RADIUS),
			center + Vector2(-MARKER_RADIUS, 0),
		])
		draw_colored_polygon(diamond, FILL)
		draw_polyline(diamond + PackedVector2Array([diamond[0]]), EDGE, 1.4, true)
		var inner := PackedVector2Array([
			center + Vector2(0, -MARKER_RADIUS * 0.45),
			center + Vector2(MARKER_RADIUS * 0.45, 0),
			center + Vector2(0, MARKER_RADIUS * 0.45),
			center + Vector2(-MARKER_RADIUS * 0.45, 0),
		])
		draw_colored_polygon(inner, FILL_INNER)

class_name OpenBFMENavDiamonds
extends Control
## Draws the small upward-pointing triangle indicators that sit above the
## shell navigation buttons which own a flyout (REF-04/07: TUTORIALS, SOLO
## PLAY, MULTIPLAYER and OPTIONS carry one; MY HEROES and QUIT do not).
## Purely decorative: it never intercepts input and only renders for buttons
## that are currently visible.

const MARKER_OFFSET := 10.0
const MARKER_HALF_WIDTH := 11.0
const MARKER_HEIGHT := 12.0
const FILL := Color(0.24, 0.52, 0.26, 0.95)
const FILL_INNER := Color(0.52, 0.78, 0.44, 0.95)
const EDGE := Color(0.72, 0.60, 0.32, 0.9)

var watched_buttons: Array[Button] = []
## The button whose flyout is currently open; its marker renders bright and
## slightly taller so the open flyout reads as anchored to it (REF-02/04).
var active_button: Button = null


func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	resized.connect(queue_redraw)


func watch(buttons: Array[Button]) -> void:
	watched_buttons = buttons
	queue_redraw()


func set_active(button: Button) -> void:
	if active_button == button:
		return
	active_button = button
	queue_redraw()


func _draw() -> void:
	for button in watched_buttons:
		if button == null or not button.visible:
			continue
		var active := button == active_button
		var rect := button.get_global_rect()
		var base_y := rect.position.y - MARKER_OFFSET
		var center_x := rect.get_center().x
		var half := MARKER_HALF_WIDTH * (1.15 if active else 1.0)
		var height := MARKER_HEIGHT * (1.2 if active else 1.0)
		var triangle := PackedVector2Array([
			Vector2(center_x - half, base_y),
			Vector2(center_x + half, base_y),
			Vector2(center_x, base_y - height),
		])
		draw_colored_polygon(triangle, FILL_INNER if active else FILL)
		draw_polyline(triangle + PackedVector2Array([triangle[0]]), EDGE, 1.4, true)
		if not active:
			draw_colored_polygon(PackedVector2Array([
				Vector2(center_x - half * 0.45, base_y - 2.0),
				Vector2(center_x + half * 0.45, base_y - 2.0),
				Vector2(center_x, base_y - height + 3.0),
			]), FILL_INNER)

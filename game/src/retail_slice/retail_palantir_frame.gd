class_name RetailPalantirFrame
extends Control
## Repository-authored ornamental frame inspired by the BFME2 Palantir silhouette.
## It deliberately uses no copied retail textures or UI artwork.


func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	queue_redraw()


func _notification(what: int) -> void:
	if what == NOTIFICATION_RESIZED:
		queue_redraw()


func _draw() -> void:
	var center := Vector2(size.x * 0.5, size.y * 0.46)
	var radius := minf(size.x * 0.47, size.y * 0.48)
	var outer := Color("9f8650")
	var bright := Color("e4ce8b")
	var shadow := Color("1a2028")
	draw_circle(center, radius + 10.0, Color(0.015, 0.025, 0.035, 0.96))
	draw_arc(center, radius + 8.0, 0.0, TAU, 96, shadow, 14.0, true)
	draw_arc(center, radius + 4.0, 0.0, TAU, 96, outer, 7.0, true)
	draw_arc(center, radius, 0.0, TAU, 96, bright, 2.0, true)
	draw_arc(center, radius - 7.0, 0.0, TAU, 96, Color("423c2c"), 4.0, true)
	# Side fins and the lower resource-scroll hooks give the dock the same visual
	# weight as a classic RTS command Palantir without reproducing retail art.
	var left := center + Vector2(-radius - 4.0, radius * 0.35)
	var right := center + Vector2(radius + 4.0, radius * 0.35)
	var left_fin := PackedVector2Array([
		left + Vector2(0, -42), left + Vector2(-34, -22), left + Vector2(-12, 0),
		left + Vector2(-42, 25), left + Vector2(4, 18),
	])
	var right_fin := PackedVector2Array()
	for point in left_fin:
		right_fin.append(Vector2(size.x - point.x, point.y))
	draw_colored_polygon(left_fin, shadow)
	draw_polyline(left_fin, outer, 4.0, true)
	draw_colored_polygon(right_fin, shadow)
	draw_polyline(right_fin, outer, 4.0, true)
	var jewel := center + Vector2(0, -radius - 4.0)
	draw_circle(jewel, 14.0, shadow)
	draw_circle(jewel, 9.0, Color("2f7caa"))
	draw_circle(jewel - Vector2(2, 2), 3.0, Color("bceaff"))

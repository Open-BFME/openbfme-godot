class_name OpenBFMEMenuBackdrop
extends Control
## Deterministic, code-native menu atmosphere. This draws an original distant
## fortress and battlefield silhouette; it contains no imported or retail art.


func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	resized.connect(queue_redraw)
	queue_redraw()


func _draw() -> void:
	if size.x <= 1.0 or size.y <= 1.0:
		return
	_draw_sky()
	_draw_mountains()
	_draw_fortress()
	_draw_battlefield()
	_draw_atmosphere()


func _draw_sky() -> void:
	var top := Color("07131f")
	var horizon := Color("294354")
	var dusk := Color("725b4b")
	var bands := 28
	for band in range(bands):
		var t := float(band) / float(bands - 1)
		var color := top.lerp(horizon, minf(t * 1.35, 1.0))
		if t > 0.62:
			color = color.lerp(dusk, (t - 0.62) * 0.42)
		var y0 := size.y * 0.72 * t
		var y1 := size.y * 0.72 * float(band + 1) / float(bands)
		draw_rect(Rect2(0.0, y0, size.x, y1 - y0 + 1.0), color)
	var moon_center := Vector2(size.x * 0.77, size.y * 0.20)
	draw_circle(moon_center, size.y * 0.068, Color(0.70, 0.82, 0.85, 0.07))
	draw_circle(moon_center, size.y * 0.043, Color(0.82, 0.90, 0.89, 0.11))


func _draw_mountains() -> void:
	draw_colored_polygon(PackedVector2Array([
		_point(0.0, 0.59), _point(0.10, 0.38), _point(0.18, 0.54),
		_point(0.29, 0.31), _point(0.40, 0.55), _point(0.52, 0.36),
		_point(0.64, 0.56), _point(0.76, 0.34), _point(0.88, 0.54),
		_point(0.96, 0.42), _point(1.0, 0.49), _point(1.0, 0.75),
		_point(0.0, 0.75),
	]), Color("172b36"))
	draw_colored_polygon(PackedVector2Array([
		_point(0.0, 0.66), _point(0.14, 0.51), _point(0.24, 0.63),
		_point(0.39, 0.48), _point(0.53, 0.65), _point(0.67, 0.49),
		_point(0.79, 0.62), _point(0.91, 0.50), _point(1.0, 0.61),
		_point(1.0, 0.80), _point(0.0, 0.80),
	]), Color("10212b"))
	for ridge_x in [0.08, 0.33, 0.62, 0.86]:
		draw_line(_point(ridge_x, 0.54), _point(ridge_x + 0.07, 0.48), Color(0.57, 0.69, 0.72, 0.09), 2.0)


func _draw_fortress() -> void:
	var silhouette := Color("09151d")
	var edge := Color(0.34, 0.49, 0.55, 0.24)
	var keep_left := size.x * 0.64
	var keep_right := size.x * 0.82
	var base_y := size.y * 0.72
	var keep_top := size.y * 0.35
	draw_rect(Rect2(keep_left, keep_top, keep_right - keep_left, base_y - keep_top), silhouette)
	_draw_battlements(keep_left, keep_right, keep_top, silhouette)
	_draw_tower(size.x * 0.60, size.x * 0.68, size.y * 0.29, base_y, silhouette)
	_draw_tower(size.x * 0.79, size.x * 0.87, size.y * 0.27, base_y, silhouette)
	draw_colored_polygon(PackedVector2Array([
		Vector2(size.x * 0.70, keep_top), Vector2(size.x * 0.73, size.y * 0.25),
		Vector2(size.x * 0.76, keep_top),
	]), silhouette)
	draw_line(Vector2(keep_left, keep_top), Vector2(keep_left, base_y), edge, 2.0)
	draw_line(Vector2(keep_right, keep_top), Vector2(keep_right, base_y), edge, 2.0)
	for column in range(4):
		var x := lerpf(keep_left + 28.0, keep_right - 28.0, float(column) / 3.0)
		draw_rect(Rect2(x - 4.0, keep_top + size.y * 0.09, 8.0, 25.0), Color(0.83, 0.59, 0.28, 0.20))
	# An original split banner establishes scale without using faction marks.
	var mast_x := size.x * 0.835
	draw_line(Vector2(mast_x, size.y * 0.21), Vector2(mast_x, size.y * 0.43), Color("263b46"), 3.0)
	draw_colored_polygon(PackedVector2Array([
		Vector2(mast_x + 2.0, size.y * 0.225), Vector2(mast_x + size.x * 0.045, size.y * 0.245),
		Vector2(mast_x + 2.0, size.y * 0.285),
	]), Color(0.24, 0.42, 0.48, 0.65))


func _draw_tower(left: float, right: float, top: float, bottom: float, color: Color) -> void:
	draw_rect(Rect2(left, top, right - left, bottom - top), color)
	draw_colored_polygon(PackedVector2Array([
		Vector2(left - 8.0, top), Vector2((left + right) * 0.5, top - size.y * 0.075),
		Vector2(right + 8.0, top),
	]), color)
	_draw_battlements(left - 3.0, right + 3.0, top + 4.0, color)


func _draw_battlements(left: float, right: float, y: float, color: Color) -> void:
	var count := maxi(3, int((right - left) / 28.0))
	var width := (right - left) / float(count)
	for index in range(count):
		draw_rect(Rect2(left + float(index) * width, y - 11.0, width * 0.56, 13.0), color)


func _draw_battlefield() -> void:
	draw_colored_polygon(PackedVector2Array([
		_point(0.0, 0.67), _point(0.17, 0.61), _point(0.36, 0.69),
		_point(0.56, 0.62), _point(0.75, 0.70), _point(1.0, 0.63),
		_point(1.0, 1.0), _point(0.0, 1.0),
	]), Color("08141a"))
	# A winding pale road leads toward the fortress.
	draw_colored_polygon(PackedVector2Array([
		_point(0.37, 1.0), _point(0.47, 1.0), _point(0.58, 0.75),
		_point(0.70, 0.68), _point(0.67, 0.67), _point(0.53, 0.72),
	]), Color(0.33, 0.38, 0.37, 0.18))
	# Foreground spear and standard silhouettes suggest a mustering army.
	for index in range(18):
		var x := size.x * (0.025 + float(index) * 0.027)
		var baseline := size.y * (0.81 + 0.025 * sin(float(index) * 1.7))
		draw_line(Vector2(x, baseline), Vector2(x + 4.0, baseline - 42.0 - float(index % 4) * 7.0), Color("071016"), 3.0)
		draw_circle(Vector2(x, baseline - 8.0), 4.5, Color("071016"))
	for x_fraction in [0.08, 0.24, 0.93]:
		_draw_bare_tree(x_fraction, 0.71)


func _draw_bare_tree(x_fraction: float, ground_fraction: float) -> void:
	var root := _point(x_fraction, ground_fraction)
	var top := root + Vector2(0.0, -size.y * 0.12)
	draw_line(root, top, Color("071116"), 8.0)
	draw_line(top, top + Vector2(-24.0, -34.0), Color("071116"), 5.0)
	draw_line(top, top + Vector2(31.0, -28.0), Color("071116"), 5.0)
	draw_line(top + Vector2(-15.0, -20.0), top + Vector2(-42.0, -35.0), Color("071116"), 3.0)
	draw_line(top + Vector2(18.0, -16.0), top + Vector2(47.0, -25.0), Color("071116"), 3.0)


func _draw_atmosphere() -> void:
	for index in range(5):
		var y := size.y * (0.49 + float(index) * 0.055)
		draw_line(Vector2(0.0, y), Vector2(size.x, y - 20.0), Color(0.62, 0.76, 0.78, 0.035), 28.0)
	# Darken the lower edge so navigation text stays readable.
	for index in range(10):
		var t := float(index) / 9.0
		var y := size.y * (0.78 + t * 0.22)
		draw_rect(Rect2(0.0, y, size.x, size.y * 0.025 + 2.0), Color(0.0, 0.0, 0.0, 0.025 + t * 0.055))


func _point(x_fraction: float, y_fraction: float) -> Vector2:
	return Vector2(size.x * x_fraction, size.y * y_fraction)

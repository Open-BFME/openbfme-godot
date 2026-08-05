extends SceneTree
## Pins retail_minimap.gd's _sanitized_radar_polygon guard: the radar must
## never hand a degenerate or self-intersecting polygon to
## draw_colored_polygon (renderer triangulation fails and spams an error every
## redraw — observed live 2026-08-05 on standing-water polygons and river
## strips). Broken-on-purpose case included: a bowtie quad MUST be rejected,
## so deleting the triangulation check turns this runner red.

const MinimapScript := preload("res://src/retail_slice/retail_minimap.gd")

var passed := 0
var failed := 0


func _check(name: String, ok: bool, detail: String = "") -> void:
	if ok:
		passed += 1
		print("MINIMAP_GEOMETRY PASS %s" % name)
	else:
		failed += 1
		print("MINIMAP_GEOMETRY FAIL %s | %s" % [name, detail])


func _init() -> void:
	var triangle := PackedVector2Array([Vector2(0, 0), Vector2(10, 0), Vector2(5, 8)])
	var result := MinimapScript._sanitized_radar_polygon(triangle)
	_check("valid_triangle_fills", result.size() == 3, "size=%d" % result.size())

	var closed_square := PackedVector2Array([
		Vector2(0, 0), Vector2(10, 0), Vector2(10, 10), Vector2(0, 10), Vector2(0, 0)
	])
	result = MinimapScript._sanitized_radar_polygon(closed_square)
	_check("closing_point_stripped", result.size() == 4, "size=%d" % result.size())

	var collapsed := PackedVector2Array([Vector2(3, 3), Vector2(3, 3), Vector2(3, 3), Vector2(3, 3)])
	result = MinimapScript._sanitized_radar_polygon(collapsed)
	_check("collapsed_points_rejected", result.is_empty(), "size=%d" % result.size())

	var collinear := PackedVector2Array([Vector2(0, 0), Vector2(5, 0), Vector2(10, 0)])
	result = MinimapScript._sanitized_radar_polygon(collinear)
	_check("collinear_run_rejected", result.is_empty(), "size=%d" % result.size())

	# The live failure shape: a river-strip quad whose second section flipped
	# orientation — self-intersecting "bowtie". Renderer triangulation fails on
	# it, so the guard MUST return empty. This is the teeth check: remove the
	# Geometry2D.triangulate_polygon gate and this goes red.
	var bowtie := PackedVector2Array([
		Vector2(0, 0), Vector2(10, 10), Vector2(10, 0), Vector2(0, 10)
	])
	result = MinimapScript._sanitized_radar_polygon(bowtie)
	_check("bowtie_quad_rejected", result.is_empty(), "size=%d" % result.size())

	var two_points := PackedVector2Array([Vector2(0, 0), Vector2(4, 4)])
	result = MinimapScript._sanitized_radar_polygon(two_points)
	_check("under_three_points_rejected", result.is_empty(), "size=%d" % result.size())

	var duplicated_edge := PackedVector2Array([
		Vector2(0, 0), Vector2(0, 0), Vector2(10, 0), Vector2(10, 0), Vector2(5, 8)
	])
	result = MinimapScript._sanitized_radar_polygon(duplicated_edge)
	_check("consecutive_duplicates_collapsed", result.size() == 3, "size=%d" % result.size())

	print("MINIMAP_GEOMETRY_RESULT passed=%d failed=%d" % [passed, failed])
	quit(1 if failed > 0 else 0)

## Lane UI-3: the window's background, drawn: a steel gradient, a faint ring ornament behind the title and a dark edge. No image.
extends Control

const Style := preload("res://scripts/ui/style.gd")


func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	resized.connect(queue_redraw)


func _draw() -> void:
	var s := size
	draw_polygon(PackedVector2Array([Vector2.ZERO, Vector2(s.x, 0), s, Vector2(0, s.y)]),
		PackedColorArray([Style.BG_TOP, Style.BG_TOP.lerp(Style.BG, 0.4), Style.BG, Style.BG]))
	# the ornament: concentric rings and spokes, very faint, centred off to the upper left behind the title
	var c := Vector2(s.x * 0.2, s.y * 0.36)
	var gold := Color(Style.GOLD, 0.05)
	for i in 4:
		draw_arc(c, s.y * (0.22 + i * 0.13), 0.0, TAU, 128, gold, 1.5, true)
	for i in 24:
		var a := TAU * i / 24.0
		var r0 := s.y * 0.22
		var r1 := s.y * (0.61 if i % 2 == 0 else 0.48)
		draw_line(c + Vector2(cos(a), sin(a)) * r0, c + Vector2(cos(a), sin(a)) * r1, gold, 1.0, true)
	# the edge: a dark band at the bottom under the action bar
	draw_rect(Rect2(0, s.y - 112, s.x, 112), Color(0, 0, 0, 0.28))
	draw_line(Vector2(0, s.y - 112), Vector2(s.x, s.y - 112), Color(Style.EDGE, 0.8), 1.0)

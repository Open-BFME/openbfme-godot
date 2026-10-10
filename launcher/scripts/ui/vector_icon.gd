## Lane UI-3: the launcher's icons, drawn as vectors (no image files, nothing from the games). A Texture2D, so a Button shows it as its icon
## and tints it with the theme's icon colours; it draws at any size (DPI scaling stays sharp).
##   VectorIcon.make("gear", 20)
## Kinds: play, gear, folder, copy, pause, resume, stop, download, refresh, check, warning, close, back, link.
extends Texture2D

var kind := ""
var px := 20


static func make(k: String, size_px: int = 20) -> Texture2D:
	var t = load("res://scripts/ui/vector_icon.gd").new()
	t.kind = k
	t.px = size_px
	return t


func _get_width() -> int:
	return px


func _get_height() -> int:
	return px


func _has_alpha() -> bool:
	return true


func _draw(ci: RID, pos: Vector2, modulate: Color, _transpose: bool) -> void:
	_draw_rect(ci, Rect2(pos, Vector2(px, px)), false, modulate, false)


func _draw_rect(ci: RID, rect: Rect2, _tile: bool, modulate: Color, _transpose: bool) -> void:
	var s := minf(rect.size.x, rect.size.y)
	var o := rect.position + (rect.size - Vector2(s, s)) / 2.0
	draw_into(ci, o, s, modulate)


func _draw_rect_region(ci: RID, rect: Rect2, _src: Rect2, modulate: Color, _transpose: bool, _clip_uv: bool) -> void:
	_draw_rect(ci, rect, false, modulate, false)


## the drawing itself, on a 0..1 square scaled to `s` at `o`
func draw_into(ci: RID, o: Vector2, s: float, c: Color) -> void:
	var rs := RenderingServer
	var w := maxf(1.5, s * 0.1)  # stroke width
	var p := func(x: float, y: float) -> Vector2: return o + Vector2(x, y) * s
	var line := func(pts: Array, closed: bool) -> void:
		var arr := PackedVector2Array()
		for q in pts:
			arr.append(p.call(q[0], q[1]))
		if closed:
			arr.append(arr[0])
		rs.canvas_item_add_polyline(ci, arr, PackedColorArray([c]), w, true)
	var fill := func(pts: Array) -> void:
		var arr := PackedVector2Array()
		for q in pts:
			arr.append(p.call(q[0], q[1]))
		rs.canvas_item_add_polygon(ci, arr, PackedColorArray([c]))
	var arc := func(cx: float, cy: float, r: float, a0: float, a1: float) -> void:
		var arr := PackedVector2Array()
		var n := 24
		for i in n + 1:
			var a := lerpf(a0, a1, float(i) / n)
			arr.append(p.call(cx + cos(a) * r, cy + sin(a) * r))
		rs.canvas_item_add_polyline(ci, arr, PackedColorArray([c]), w, true)
	match kind:
		"play":
			fill.call([[0.28, 0.16], [0.84, 0.5], [0.28, 0.84]])
		"resume":
			fill.call([[0.3, 0.18], [0.8, 0.5], [0.3, 0.82]])
		"pause":
			fill.call([[0.26, 0.18], [0.42, 0.18], [0.42, 0.82], [0.26, 0.82]])
			fill.call([[0.58, 0.18], [0.74, 0.18], [0.74, 0.82], [0.58, 0.82]])
		"stop", "close":
			line.call([[0.24, 0.24], [0.76, 0.76]], false)
			line.call([[0.76, 0.24], [0.24, 0.76]], false)
		"gear":
			var teeth := 8
			var pts := []
			for i in teeth * 4:
				var a := TAU * i / (teeth * 4.0)
				var r := 0.44 if (i % 4) in [0, 1] else 0.32
				pts.append([0.5 + cos(a) * r, 0.5 + sin(a) * r])
			line.call(pts, true)
			arc.call(0.5, 0.5, 0.13, 0.0, TAU)
		"folder":
			line.call([[0.1, 0.24], [0.4, 0.24], [0.48, 0.34], [0.9, 0.34], [0.9, 0.8], [0.1, 0.8]], true)
		"copy":
			line.call([[0.34, 0.12], [0.86, 0.12], [0.86, 0.66], [0.34, 0.66]], true)
			line.call([[0.24, 0.34], [0.14, 0.34], [0.14, 0.88], [0.66, 0.88], [0.66, 0.78]], false)
		"download":
			line.call([[0.5, 0.12], [0.5, 0.64]], false)
			line.call([[0.28, 0.44], [0.5, 0.66], [0.72, 0.44]], false)
			line.call([[0.14, 0.7], [0.14, 0.86], [0.86, 0.86], [0.86, 0.7]], false)
		"refresh":
			arc.call(0.5, 0.5, 0.32, -PI * 0.35, PI * 1.35)
			fill.call([[0.62, 0.04], [0.86, 0.22], [0.58, 0.32]])
		"check":
			line.call([[0.18, 0.52], [0.4, 0.74], [0.82, 0.28]], false)
		"warning":
			line.call([[0.5, 0.1], [0.92, 0.86], [0.08, 0.86]], true)
			line.call([[0.5, 0.36], [0.5, 0.6]], false)
			rs.canvas_item_add_circle(ci, p.call(0.5, 0.73), w * 0.6, c)
		"back":
			line.call([[0.62, 0.2], [0.32, 0.5], [0.62, 0.8]], false)
		"link":
			line.call([[0.42, 0.18], [0.18, 0.18], [0.18, 0.82], [0.82, 0.82], [0.82, 0.58]], false)
			line.call([[0.5, 0.5], [0.86, 0.14]], false)
			line.call([[0.6, 0.14], [0.86, 0.14], [0.86, 0.4]], false)

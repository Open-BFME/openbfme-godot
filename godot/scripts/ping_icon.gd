## Ping wheel (NOT RETAIL): the icon of a ping kind (scripts/ping_board.gd KINDS), drawn as plain vector shapes so no picture ships with the game.
## draw_glyph() is shared: the wheel draws it in its sectors, this Control draws it in the small viewport the ground marker's floating icon shows.
extends Control

const GOLD := Color(0.86, 0.72, 0.42)
const DARK := Color(0.06, 0.05, 0.04, 0.82)

var kind := "here"
var color := Color.WHITE
var backdrop := false # a dark disc with a gold rim behind the glyph (the floating icon)


func _draw() -> void:
	var c := size * 0.5
	var r := minf(size.x, size.y) * 0.5
	if backdrop:
		draw_circle(c, r * 0.92, DARK)
		draw_arc(c, r * 0.92, 0.0, TAU, 48, GOLD, maxf(2.0, r * 0.07), true)
	draw_glyph(self, kind, c, r * 0.55, color)


## the glyph of `kind` centred on `c`, about `r` from the centre to its edge
static func draw_glyph(ci: CanvasItem, kind: String, c: Vector2, r: float, color: Color) -> void:
	var w := maxf(2.0, r * 0.16)
	var outline := Color(0, 0, 0, 0.85 * color.a)
	match kind:
		"here":
			# a map pin: a ring on a point
			var head := c + Vector2(0, -r * 0.3)
			var pin := PackedVector2Array([head + Vector2(-r * 0.5, r * 0.25), head + Vector2(r * 0.5, r * 0.25), c + Vector2(0, r)])
			ci.draw_colored_polygon(pin, outline)
			ci.draw_circle(head, r * 0.62, outline)
			ci.draw_colored_polygon(_shrink(pin, c + Vector2(0, r * 0.2), w * 0.6), color)
			ci.draw_circle(head, r * 0.62 - w * 0.6, color)
			ci.draw_circle(head, r * 0.24, outline)
		"attack":
			# two crossed swords
			for s in [-1.0, 1.0]:
				var a := c + Vector2(-r * 0.8 * s, r * 0.8)
				var b := c + Vector2(r * 0.75 * s, -r * 0.75)
				ci.draw_line(a, b, outline, w * 1.9, true)
				ci.draw_line(a, b, color, w, true)
				var g := a.lerp(b, 0.22)
				var n := (b - a).normalized().orthogonal() * r * 0.32
				ci.draw_line(g - n, g + n, outline, w * 1.9, true)
				ci.draw_line(g - n, g + n, color, w, true)
		"reinforce":
			# a banner on a pole (a rally point)
			var top := c + Vector2(-r * 0.55, -r * 0.95)
			var foot := c + Vector2(-r * 0.55, r * 0.95)
			var flag := PackedVector2Array([top, top + Vector2(r * 1.35, r * 0.3), top + Vector2(r * 0.9, r * 0.55), top + Vector2(r * 1.35, r * 0.85),
				top + Vector2(0, r * 1.05)])
			ci.draw_colored_polygon(_shrink(flag, flag[0] + Vector2(r * 0.5, r * 0.5), -w * 0.7), outline)
			ci.draw_colored_polygon(flag, color)
			ci.draw_line(top - Vector2(0, w), foot, outline, w * 1.9, true)
			ci.draw_line(top - Vector2(0, w), foot, color, w, true)
		"danger":
			# a warning triangle with an exclamation mark
			var tri := PackedVector2Array([c + Vector2(0, -r), c + Vector2(r, r * 0.8), c + Vector2(-r, r * 0.8)])
			ci.draw_colored_polygon(_shrink(tri, c + Vector2(0, r * 0.2), -w * 0.8), outline)
			ci.draw_colored_polygon(tri, color)
			ci.draw_line(c + Vector2(0, -r * 0.45), c + Vector2(0, r * 0.25), outline, w * 1.3, true)
			ci.draw_circle(c + Vector2(0, r * 0.52), w * 0.75, outline)
		"assist":
			# a shield with a cross
			var sh := PackedVector2Array([c + Vector2(-r * 0.8, -r * 0.85), c + Vector2(r * 0.8, -r * 0.85), c + Vector2(r * 0.8, -r * 0.1),
				c + Vector2(r * 0.45, r * 0.55), c + Vector2(0, r), c + Vector2(-r * 0.45, r * 0.55), c + Vector2(-r * 0.8, -r * 0.1)])
			ci.draw_colored_polygon(_shrink(sh, c, -w * 0.8), outline)
			ci.draw_colored_polygon(sh, color)
			ci.draw_line(c + Vector2(0, -r * 0.55), c + Vector2(0, r * 0.55), outline, w * 1.2, true)
			ci.draw_line(c + Vector2(-r * 0.45, -r * 0.1), c + Vector2(r * 0.45, -r * 0.1), outline, w * 1.2, true)


## the polygon moved `by` pixels towards `centre` (a negative `by` grows it: the outline drawn under the glyph)
static func _shrink(poly: PackedVector2Array, centre: Vector2, by: float) -> PackedVector2Array:
	var out := PackedVector2Array()
	for p in poly:
		out.append(p + (centre - p).normalized() * by)
	return out

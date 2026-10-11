## Lane UI-5: the gold filigree of "Parchment & Steel", drawn as vectors (no image): "divider" (the scrollwork under the title: a
## bevelled diamond with a fleur-de-lis, and on each side a tapering scroll of curls, acanthus leaf fans and tendrils ending in a trefoil)
## and "corners" (the same scrollwork folded into each corner of a parchment card). Every stroke and leaf is drawn three times, a dark
## edge, the gold body and a light inner highlight, for the embossed look of the prototype's gold. It draws at any size and ignores the
## mouse.
extends Control

const GOLD := Color("e6b84f")
const GOLD_DARK := Color("8f6420")
const GOLD_LIGHT := Color("fbe6a6")

var kind := "divider"
var _k := 1.0          # design units -> pixels
var _o := Vector2.ZERO # the design's origin in pixels
var _sx := 1.0         # mirror (x)
var _sy := 1.0         # mirror (y)


static func make(k: String) -> Control:
	var o: Control = load("res://scripts/ui/ornament.gd").new()
	o.kind = k
	o.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return o


func _ready() -> void:
	resized.connect(queue_redraw)


func _draw() -> void:
	if kind == "divider":
		# laid out on 210 x 34 design units, centred, scaled to fit
		_k = minf(size.x / 210.0, size.y / 34.0)
		_o = size / 2.0
		_sy = 1.0
		for sx in [1.0, -1.0]:
			_sx = sx
			_divider_side()
		_sx = 1.0
		_divider_centre()
	else:
		# laid out on 64 x 64 design units per corner
		_k = clampf(minf(size.x, size.y) / 300.0, 0.55, 0.9)
		for fx in [1.0, -1.0]:
			for fy in [1.0, -1.0]:
				_sx = fx
				_sy = fy
				_o = Vector2(0.0 if fx > 0 else size.x, 0.0 if fy > 0 else size.y)
				_corner()


## a design point (mirrored, scaled, placed) in pixels
func _p(x: float, y: float) -> Vector2:
	return _o + Vector2(x * _sx, y * _sy) * _k


# ---- the pieces ---------------------------------------------------------------------------------------------------------------------------

func _divider_side() -> void:
	# the main scroll: out from the diamond, over a big curl, under a second one, to a tendril
	_stroke(_bez([10, 0], [22, -2], [26, -12], [20, -14]), 2.4, 1.6)
	_curl(Vector2(19, -9), 4.6, -1.0, PI * 1.5, 1.6)
	_stroke(_bez([10, 0], [30, 2], [40, 10], [52, 8]), 2.2, 1.4)
	_stroke(_bez([52, 8], [62, 6], [62, -4], [74, -4]), 1.8, 1.2)
	_curl(Vector2(52, 3.5), 4.2, 1.0, PI * 0.5, 1.4)
	_stroke(_bez([74, -4], [84, -4], [88, 4], [96, 2]), 1.4, 0.9)
	_curl(Vector2(76, -8.5), 3.6, -1.0, PI * 0.5, 1.1)
	_stroke(_bez([84, -1], [88, -9], [94, -10], [97, -7]), 1.0, 0.6)
	# leaf fans
	_fan(Vector2(30, 2), -PI * 0.42, 9.0, 3.4)
	_fan(Vector2(42, 8), PI * 0.55, 8.0, 3.0)
	_fan(Vector2(64, 1), -PI * 0.5, 7.5, 2.8)
	_fan(Vector2(86, 0), PI * 0.4, 6.0, 2.3)
	_leaf(Vector2(14, -1), Vector2(0.6, 1.0).normalized(), 7.0, 2.6)
	# the trefoil at the end and a few dots
	for d in [[101, 2, 1.7], [98.5, -1, 1.2], [98.5, 5, 1.2]]:
		_dot(Vector2(d[0], d[1]), d[2])
	_dot(Vector2(35, -6), 1.1)
	_dot(Vector2(68, 9), 1.0)


func _divider_centre() -> void:
	var d := [Vector2(0, -11), Vector2(8, 0), Vector2(0, 11), Vector2(-8, 0)]
	_poly(d, GOLD_DARK, 1.2)
	_poly(d, GOLD, 0.0)
	_poly([Vector2(0, -6.5), Vector2(4.5, 0), Vector2(0, 6.5), Vector2(-4.5, 0)], GOLD_LIGHT, 0.0)
	_poly([Vector2(0, -6.5), Vector2(4.5, 0), Vector2(0, 0)], Color(GOLD, 0.8), 0.0)
	# the fleur-de-lis above, a drop below
	_leaf(Vector2(0, -11), Vector2(0, -1), 7.5, 2.6)
	_leaf(Vector2(-0.5, -11.5), Vector2(-0.8, -0.6).normalized(), 5.0, 1.8)
	_leaf(Vector2(0.5, -11.5), Vector2(0.8, -0.6).normalized(), 5.0, 1.8)
	_leaf(Vector2(0, 11), Vector2(0, 1), 5.5, 2.2)
	_dot(Vector2(0, 17.5), 1.2)


func _corner() -> void:
	_stroke(_bez([5, 5], [20, 1], [36, 9], [54, 5]), 2.2, 1.2)
	_stroke(_bez([5, 5], [1, 20], [9, 36], [5, 54]), 2.2, 1.2)
	_curl(Vector2(57, 9), 4.0, 1.0, PI, 1.2)
	_curl(Vector2(9, 57), 4.0, -1.0, -PI * 0.5, 1.2)
	_stroke(_bez([5, 5], [16, 14], [22, 22], [20, 28]), 1.6, 1.0)
	_curl(Vector2(24, 25), 4.8, -1.0, PI, 1.3)
	_fan(Vector2(28, 6), PI * 0.35, 8.0, 3.0)
	_fan(Vector2(6, 28), PI * 0.15, 8.0, 3.0)
	_fan(Vector2(15, 14), PI * 0.25, 7.0, 2.6)
	_leaf(Vector2(44, 7), Vector2(0.4, 1.0).normalized(), 6.0, 2.2)
	_leaf(Vector2(7, 44), Vector2(1.0, 0.4).normalized(), 6.0, 2.2)
	_dot(Vector2(5, 5), 3.2)
	_dot(Vector2(62, 4), 1.2)
	_dot(Vector2(4, 62), 1.2)


# ---- drawing helpers (design units) --------------------------------------------------------------------------------------------------------

static func _bez(a: Array, b: Array, c: Array, d: Array, n := 18) -> PackedVector2Array:
	var p0 := Vector2(a[0], a[1])
	var p1 := Vector2(b[0], b[1])
	var p2 := Vector2(c[0], c[1])
	var p3 := Vector2(d[0], d[1])
	var out := PackedVector2Array()
	for i in n + 1:
		var t := float(i) / n
		var u := 1.0 - t
		out.append(p0 * u * u * u + p1 * 3.0 * u * u * t + p2 * 3.0 * u * t * t + p3 * t * t * t)
	return out


## a stroke along design points, tapering from width w0 to w1: dark edge, gold body, light highlight
func _stroke(pts: PackedVector2Array, w0: float, w1: float) -> void:
	for layer in 3:
		var col: Color = [GOLD_DARK, GOLD, GOLD_LIGHT][layer]
		var grow: float = [0.9, 0.0, -0.55][layer]
		var shift := Vector2(-0.25, -0.35) * float(layer == 2)
		for i in pts.size() - 1:
			var t := float(i) / maxf(1.0, pts.size() - 2.0)
			var w := maxf(0.3, lerpf(w0, w1, t) * (1.0 if layer < 2 else 0.35) + grow)
			draw_line(_p(pts[i].x + shift.x, pts[i].y + shift.y), _p(pts[i + 1].x + shift.x, pts[i + 1].y + shift.y), col, w * _k, true)
			if layer < 2:
				draw_circle(_p(pts[i + 1].x, pts[i + 1].y), w * _k * 0.5, col, true, -1.0, true)


## a spiral of 1.25 turns winding in from radius `r` around `c`, starting at angle `a0`, with a bead at its heart
func _curl(c: Vector2, r: float, dir: float, a0: float, w: float) -> void:
	var pts := PackedVector2Array()
	var n := 26
	for i in n + 1:
		var t := float(i) / n
		var a := a0 + dir * t * TAU * 1.25
		pts.append(c + Vector2(cos(a), sin(a)) * r * (1.0 - 0.72 * t))
	_stroke(pts, w, w * 0.55)
	_dot(pts[pts.size() - 1], w * 0.75)


## a pointed leaf from `base` along `dir` (design units), bevelled
func _leaf(base: Vector2, dir: Vector2, length: float, width: float) -> void:
	var side := Vector2(-dir.y, dir.x)
	var pts := []
	var n := 10
	for i in n + 1:
		var t := float(i) / n
		pts.append(base + dir * length * t + side * width * sin(PI * t) * (1.0 - 0.3 * t))
	for i in range(n - 1, 0, -1):
		var t := float(i) / n
		pts.append(base + dir * length * t - side * width * sin(PI * t) * 0.75)
	_poly(pts, GOLD_DARK, 0.9)
	_poly(pts, GOLD, 0.0)
	var hi := []
	for i in n + 1:
		var t := float(i) / n
		hi.append(base + dir * length * t * 0.85 + side * width * sin(PI * t) * 0.45)
	for i in range(n - 1, 0, -1):
		var t := float(i) / n
		hi.append(base + dir * length * t * 0.85 + side * width * sin(PI * t) * 0.1)
	_poly(hi, Color(GOLD_LIGHT, 0.85), 0.0)


## an acanthus fan: three leaves from one point around angle `a`
func _fan(base: Vector2, a: float, length: float, width: float) -> void:
	for da in [-0.5, 0.5, 0.0]:
		var d := Vector2(cos(a + da), sin(a + da))
		_leaf(base, d, length * (1.0 if da == 0.0 else 0.75), width * (1.0 if da == 0.0 else 0.8))


func _dot(c: Vector2, r: float) -> void:
	draw_circle(_p(c.x, c.y), (r + 0.5) * _k, GOLD_DARK, true, -1.0, true)
	draw_circle(_p(c.x, c.y), r * _k, GOLD, true, -1.0, true)
	draw_circle(_p(c.x - r * 0.3, c.y - r * 0.3), r * 0.45 * _k, GOLD_LIGHT, true, -1.0, true)


## a filled polygon of design points; `outline` > 0 draws only its edge that wide (the dark rim under the fill)
func _poly(pts: Array, col: Color, outline: float) -> void:
	var px := PackedVector2Array()
	for q in pts:
		px.append(_p(q.x, q.y))
	if outline > 0.0:
		px.append(px[0])
		draw_polyline(px, col, outline * _k, true)
	else:
		draw_colored_polygon(px, col)

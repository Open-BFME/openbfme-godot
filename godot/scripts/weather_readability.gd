## OpenBFME. GPL-3.0.
## Lane WEATHERFX-1 (the owner 2026-10-29: "weather never hurts readability"): how readable a frame of the playfield is, and whether the Enhanced
## look's weather keeps it as readable as the Original look. Pure functions on Images (scripts/weatherfx1_player.gd measures the live game with
## them; tests/weatherfx_test.gd checks them on synthetic images).
##   mean      the mean luma (Rec. 601 on the stored sRGB values) of the region
##   contrast  the standard deviation of that luma (RMS contrast)
##   detail    the mean absolute luma difference to the right and lower neighbour pixel (edges and texture: paths, outlines, the ground's grain)
## compare(): Enhanced against Original, each ratio within BANDS; `units` are the same measures in small boxes around the units on screen. The fine
## `detail` of the playfield is compared with the Enhanced look WITHOUT weather when that is given (`look_only`): it measures what the weather
## does to the ground's grain (the Enhanced look's own AgX tone mapping keeps ~0.75 of the Original's grain on a bright snowy map with no weather
## at all); `detail_vs_original` is reported beside it.
extends RefCounted

## ratio of Enhanced to Original: [lowest, highest] (a missing highest: no upper bound)
const BANDS := {
	"mean": [0.75, 1.3],
	"contrast": [0.85],
	"detail": [0.85],
	"units_detail": [0.85],
	"units_contrast": [0.85],
}
const STEP := 2 # every second pixel and row (a 1280 x 720 playfield in ~0.1 s)


static func luma(c: Color) -> float:
	return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b


## { mean, contrast, detail, pixels } of `img` inside `rect`
static func stats(img: Image, rect: Rect2i) -> Dictionary:
	var r := rect.intersection(Rect2i(0, 0, img.get_width() - 1, img.get_height() - 1))
	var n := 0
	var sum := 0.0
	var sum2 := 0.0
	var grad := 0.0
	var y := r.position.y
	while y < r.end.y:
		var x := r.position.x
		while x < r.end.x:
			var l := luma(img.get_pixel(x, y))
			sum += l
			sum2 += l * l
			grad += absf(l - luma(img.get_pixel(x + 1, y))) + absf(l - luma(img.get_pixel(x, y + 1)))
			n += 1
			x += STEP
		y += STEP
	if n == 0:
		return {"mean": 0.0, "contrast": 0.0, "detail": 0.0, "pixels": 0}
	var mean := sum / n
	return {"mean": mean, "contrast": sqrt(maxf(sum2 / n - mean * mean, 0.0)), "detail": grad / (2.0 * n), "pixels": n}


## the playfield of a frame: the screen without the Palantir (lower left) and the top edge
static func playfield(size: Vector2i) -> Rect2i:
	return Rect2i(int(size.x * 0.3), int(size.y * 0.04), int(size.x * 0.68), int(size.y * 0.9))


## { playfield: stats, units: { contrast, detail } (the median over the boxes), boxes } of a frame; `units` pixel positions of units on screen
static func measure(img: Image, units: Array) -> Dictionary:
	var pf := playfield(Vector2i(img.get_width(), img.get_height()))
	var c: Array = []
	var d: Array = []
	for p in units:
		var at := Vector2i(p)
		if not pf.has_point(at):
			continue
		var s := stats(img, Rect2i(at - Vector2i(20, 20), Vector2i(40, 40)))
		c.append(s.contrast)
		d.append(s.detail)
	return {"playfield": stats(img, pf), "units": {"contrast": _median(c), "detail": _median(d)}, "boxes": c.size()}


static func _median(a: Array) -> float:
	if a.is_empty():
		return 0.0
	var s := a.duplicate()
	s.sort()
	return s[s.size() / 2] if s.size() % 2 == 1 else (s[s.size() / 2 - 1] + s[s.size() / 2]) * 0.5


## the ratios Enhanced / Original and the failures (empty: as readable). `original` and `enhanced` from measure()
static func compare(original: Dictionary, enhanced: Dictionary, look_only: Dictionary = {}) -> Dictionary:
	var ratios := {}
	var fails: Array = []
	for k in ["mean", "contrast", "detail"]:
		ratios[k] = _ratio(enhanced.playfield[k], original.playfield[k])
	var info := {"detail_vs_original": ratios.detail}
	if not look_only.is_empty():
		ratios.detail = _ratio(enhanced.playfield.detail, look_only.playfield.detail)
	if int(original.get("boxes", 0)) > 0 and int(enhanced.get("boxes", 0)) > 0:
		ratios["units_detail"] = _ratio(enhanced.units.detail, original.units.detail)
		ratios["units_contrast"] = _ratio(enhanced.units.contrast, original.units.contrast)
	else:
		fails.append("no unit on screen to check")
	for k in ratios:
		var band: Array = BANDS[k]
		if ratios[k] < band[0] or (band.size() > 1 and ratios[k] > band[1]):
			var of := "the Enhanced look without weather" if k == "detail" and not look_only.is_empty() else "the Original look"
			fails.append("%s %.2f of %s (band %s)" % [k, ratios[k], of, band])
	return {"ratios": ratios, "fails": fails, "info": info}


static func _ratio(a: float, b: float) -> float:
	return a / b if b > 0.000001 else (1.0 if a <= 0.000001 else 99.0)


## lane WEATHERFX-2 (the owner: "a washout on the menus with the snow"): a screen with no game on it (a menu, the score screen, a loading screen) must
## be drawn the same in every look. `a` and `a2` the screen in the Original look (before and after `b`, the Enhanced one: the menus animate, so a pixel
## of `b` counts as changed only when it differs from both). Fails on a mean change, on bright specks (the weather's flakes over the menu) and on any
## light in the bars that are black in the Original (outside the menu art). { ok, why, stats }
const SCREEN_MEAN_DIFF := 0.004   # the mean luma change over the screen
const SCREEN_SPECKS := 0.0005     # the share of pixels brighter by more than SPECK_LUMA
const SPECK_LUMA := 0.2
static func screens_match(a: Image, b: Image, a2: Image = null) -> Dictionary:
	if a2 == null:
		a2 = a
	var w := mini(a.get_width(), b.get_width())
	var h := mini(a.get_height(), b.get_height())
	var n := 0
	var diff := 0.0
	var specks := 0
	var bar_n := 0
	var bar_lit := 0
	var y := 0
	while y < h:
		var x := 0
		while x < w:
			var la := luma(a.get_pixel(x, y))
			var la2 := luma(a2.get_pixel(x, y))
			var lb := luma(b.get_pixel(x, y))
			var d := minf(absf(lb - la), absf(lb - la2))
			diff += d
			if lb - maxf(la, la2) > SPECK_LUMA:
				specks += 1
			if maxf(la, la2) < 0.004:
				bar_n += 1
				if lb > 0.02:
					bar_lit += 1
			n += 1
			x += 1
		y += STEP
	var mean := diff / maxi(n, 1)
	var speck_share := float(specks) / maxi(n, 1)
	var stats := {"mean_diff": snappedf(mean, 0.0001), "specks": snappedf(speck_share, 0.00001), "black": bar_n, "black_lit": bar_lit, "pixels": n}
	var why: Array = []
	if mean > SCREEN_MEAN_DIFF:
		why.append("the Enhanced look changes the screen (mean luma change %.4f > %.4f)" % [mean, SCREEN_MEAN_DIFF])
	if speck_share > SCREEN_SPECKS:
		why.append("bright specks over the screen in the Enhanced look (%.3f%% of the pixels)" % (speck_share * 100.0))
	if bar_lit > maxi(bar_n / 2000, 4):
		why.append("%d of the %d black pixels are lit in the Enhanced look" % [bar_lit, bar_n])
	return {"ok": why.is_empty(), "why": "; ".join(why), "stats": stats}


## lane INGAMEBG-1 (the owner 2026-10-29: the Pause menu and the Powers screen are washed out): an in-game panel lies over the in-game background
## (Background.apt's ShowInGameBackground: the dark View3D SFE_MenuOverlay, RW 0x6230B6 mode 2), which DIMS the battlefield. `game` is the same
## frame with no panel (the reference), `panel` the frame with it; regions are fractions of the frame (a 16:9 window). Over the battlefield beside
## the panel the mean luma must drop to at most PANEL_DIM_MAX of the reference and the light the panel adds on its own (the intercept of the panel's
## luma against the reference's, a cream veil) stay under PANEL_ADDED_MAX; on the Powers screen the globe's lower middle (between its buttons, the
## View3D SFE_MenuSpells) must be dark, at most PANEL_GLOBE_MAX. Before the port the Pause menu read ~2.0x the reference (+0.7) and the globe 0.72.
## { ok, why, stats }
const PANEL_DIM_MAX := 0.6
const PANEL_ADDED_MAX := 0.25
const PANEL_GLOBE_MAX := 0.35
const PANEL_SIDES := [Rect2(0.72, 0.15, 0.25, 0.45), Rect2(0.03, 0.12, 0.27, 0.48)] # right of the panel; left of it, above the Palantir
const PANEL_GLOBE := Rect2(0.44, 0.62, 0.12, 0.08)


static func panel_backdrop(game: Image, panel: Image, kind: String) -> Dictionary:
	var why: Array = []
	var stats := {}
	var sides: Array = PANEL_SIDES if kind == "pause" else [PANEL_SIDES[0]] # the Powers globe reaches the left region at 16:9
	for i in sides.size():
		var r: Dictionary = _pair_stats(game, panel, sides[i])
		stats["side%d" % i] = r
		if r.panel > r.game * PANEL_DIM_MAX:
			why.append("%s: the battlefield beside the panel is not dimmed (luma %.3f, %.2fx the game's %.3f)" % [kind, r.panel, r.panel / maxf(r.game, 0.0001), r.game])
		if r.added > PANEL_ADDED_MAX:
			why.append("%s: a light veil over the battlefield (+%.3f luma)" % [kind, r.added])
	if kind == "powers":
		var g: Dictionary = _pair_stats(game, panel, PANEL_GLOBE)
		stats["globe"] = g
		if g.panel > PANEL_GLOBE_MAX:
			why.append("powers: the globe is washed out (luma %.3f > %.2f)" % [g.panel, PANEL_GLOBE_MAX])
	return {"ok": why.is_empty(), "why": "; ".join(why), "stats": stats}


## the mean luma of `game` and `panel` over the region `r` (fractions) and the intercept of a least-squares line panel = k * game + added
static func _pair_stats(game: Image, panel: Image, r: Rect2) -> Dictionary:
	var w := mini(game.get_width(), panel.get_width())
	var h := mini(game.get_height(), panel.get_height())
	var x0 := int(r.position.x * w)
	var x1 := int(r.end.x * w)
	var y0 := int(r.position.y * h)
	var y1 := int(r.end.y * h)
	var n := 0
	var sg := 0.0
	var sp := 0.0
	var sgg := 0.0
	var sgp := 0.0
	var y := y0
	while y < y1:
		var x := x0
		while x < x1:
			var g := luma(game.get_pixel(x, y))
			var p := luma(panel.get_pixel(x, y))
			sg += g
			sp += p
			sgg += g * g
			sgp += g * p
			n += 1
			x += STEP
		y += STEP
	n = maxi(n, 1)
	var mg := sg / n
	var mp := sp / n
	var vg := sgg / n - mg * mg
	var k := (sgp / n - mg * mp) / vg if vg > 0.000001 else 0.0
	return {"game": snappedf(mg, 0.001), "panel": snappedf(mp, 0.001), "added": snappedf(mp - k * mg, 0.001)}

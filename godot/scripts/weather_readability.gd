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

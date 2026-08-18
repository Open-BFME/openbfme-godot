class_name RetailHudArcGauge
extends Control
## A curved gauge that hugs a circular portrait, for the hero roster health
## rings and the palantir big-portrait level bar (Q38).
##
## Retail does NOT draw these as rectangles. `InGameHeroSelect.apt` gives each
## hero cell a `HealthBar` clip at cell-local [25.35, 49.85] whose backdrop is
## `InGameHeroSelect_geometry/73.ru` - a 77 x 34 quad - and whose fill is
## `74.ru`, a 57.5 x 19.5 quad, both riding the bottom of the 59-unit portrait
## circle. The CURVE lives in the retail texture, not in the geometry, so there
## is no authored sweep to read; `RetailHudStage.hero_health_arc_half_angle()`
## documents the approximation used here (the portion of the portrait circle
## below the authored quad's top edge, acos(4.85 / 29.5) = 80.54 degrees each
## side of straight down).
##
## The rank/level ring is authored as a real arc: `RankProgress` (character 95,
## cell-local [4.85, 48.40] scale 0.42) draws `90.ru`, a triangle fan whose
## vertices all sit at radius ~25.1. That one is geometry, not texture.

## Gauge centre in this control's local coordinates.
var arc_center := Vector2.ZERO
## Per-axis radius - the stage is exact-fit stretched, so a retail "circle" is
## an ellipse on any non-4:3 screen (see RetailHudStage).
var arc_radius := Vector2(29.5, 29.5)
var arc_thickness := 6.0
## Straight down is +PI/2 in Godot screen space; the sweep is symmetric about it.
var arc_half_angle := deg_to_rad(80.54)
var arc_value := 1.0
var arc_track_color := Color(0.04, 0.05, 0.03, 0.85)
var arc_fill_color := Color("3fae3f")
## Segment count for one full sweep; keeps the polyline smooth without inviting
## a per-frame allocation storm.
const ARC_SEGMENTS := 28


func configure(center: Vector2, radius: Vector2, thickness: float, half_angle: float) -> void:
	arc_center = center
	arc_radius = radius
	arc_thickness = thickness
	arc_half_angle = half_angle
	queue_redraw()


func set_value(value: float) -> void:
	var clamped := clampf(value, 0.0, 1.0)
	if is_equal_approx(clamped, arc_value):
		return
	arc_value = clamped
	queue_redraw()


func _points(from_angle: float, to_angle: float) -> PackedVector2Array:
	var points := PackedVector2Array()
	if is_equal_approx(from_angle, to_angle):
		return points
	for step in ARC_SEGMENTS + 1:
		var angle := lerpf(from_angle, to_angle, float(step) / float(ARC_SEGMENTS))
		points.append(arc_center + Vector2(cos(angle) * arc_radius.x, sin(angle) * arc_radius.y))
	return points


func _draw() -> void:
	var start := PI * 0.5 + arc_half_angle
	var end := PI * 0.5 - arc_half_angle
	var track := _points(start, end)
	if track.size() >= 2:
		draw_polyline(track, arc_track_color, arc_thickness, true)
	if arc_value <= 0.0:
		return
	var fill := _points(start, lerpf(start, end, arc_value))
	if fill.size() >= 2:
		draw_polyline(fill, arc_fill_color, arc_thickness, true)

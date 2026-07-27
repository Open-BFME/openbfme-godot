extends RefCounted

## THE WAR OF THE RING SCREEN'S FRAME, drawn rather than imported.
##
## READ THIS BEFORE ASSUMING ANY OF IT IS RETAIL ART. IT IS NOT.
##
## Retail's strategic shell is warm gold and brass: rounded frames with a bright
## inner rule and a dark outer one, a heavy title plate with a rule under it,
## inset panels a shade darker than their frame, and circular studs at the
## corners. This file draws that language with primitives.
##
## WHY IT IS DRAWN AND NOT CONVERTED. Retail's shell frame art is a nine-slice
## set of `MappedImage` definitions - `FrameT`, `FrameB`, `FrameL`, `FrameR` and
## the four `FrameCorner*` - and every one of them resolves to a texture that IS
## NOT IN THE ARCHIVES: `SCShellUserInterface512_001.tga` and
## `MainMenuRuleruserinterface.tga` are named by the INI and shipped by nothing.
## The same is true of `Ruler` and `MainMenuRuler`, retail's gold title rules.
## So the ids resolve, the pixels do not exist, and this project will not invent
## a picture and call it retail's.
##
## WHAT IS RETAIL'S, AND IS DRAWN ELSEWHERE, IS SAID ON SCREEN:
##   * every portrait and structure icon (141 MappedImage crops, 64 atlases);
##   * the seven faction standards (`Banner_*`, reinforcementbanners_001.dds);
##   * the radial ring (`RadialBorder`, radialborders.dds);
##   * the two elvish rings off `apt_LivingWorldUI_1.tga`, the shell's own sheet.
## Everything in THIS file is hand-built in that style and is reported as such.
##
## Nothing here reads or writes simulation state; it takes a canvas and a rect.

## The palette. Warm brass rather than the shell's cooler green, because the
## strategic screen in retail is the warm one.
const BRASS_DARK := Color("#2a1e10")
const BRASS := Color("#6b5327")
const BRASS_BRIGHT := Color("#c9a463")
const GOLD := Color("#b99a55")
const GOLD_BRIGHT := Color("#e1c77d")
const GOLD_PALE := Color("#f0e0b0")
const INK := Color("#0b0a07")
const PANEL_FILL := Color(0.06, 0.055, 0.045, 0.86)
const PANEL_FILL_DEEP := Color(0.04, 0.038, 0.032, 0.93)


## A framed panel: a dark inset field inside a brass rule, with a bright inner
## hairline and four corner studs. `depth` picks the fill: 0 a normal panel, 1 a
## recessed one for a list or a report.
static func draw_panel(canvas: CanvasItem, rect: Rect2, depth: int = 0) -> void:
	if rect.size.x <= 4.0 or rect.size.y <= 4.0:
		return
	canvas.draw_rect(rect, PANEL_FILL_DEEP if depth > 0 else PANEL_FILL)
	# Outer dark rule, then the brass body, then a bright hairline inside it.
	canvas.draw_rect(rect.grow(1.0), INK, false, 2.0)
	canvas.draw_rect(rect, BRASS, false, 2.0)
	canvas.draw_rect(rect.grow(-3.0), Color(GOLD.r, GOLD.g, GOLD.b, 0.55), false, 1.0)
	_draw_studs(canvas, rect)


## The heavy title plate retail puts a screen's name on: a filled bar, a bright
## rule under it, and a flourish at each end of that rule.
static func draw_title_plate(canvas: CanvasItem, rect: Rect2) -> void:
	if rect.size.x <= 8.0 or rect.size.y <= 4.0:
		return
	canvas.draw_rect(rect, Color(0.09, 0.075, 0.05, 0.92))
	canvas.draw_rect(rect, BRASS, false, 2.0)
	var rule_y := rect.position.y + rect.size.y - 3.0
	draw_rule(canvas, Vector2(rect.position.x + 8.0, rule_y),
		rect.size.x - 16.0)


## A gold rule with a diamond at each end and one in the middle - the shape
## retail puts under a title. Hand-built: retail's own `Ruler` image names a
## texture the archives do not carry.
static func draw_rule(canvas: CanvasItem, at: Vector2, width: float) -> void:
	if width <= 12.0:
		return
	canvas.draw_line(at, at + Vector2(width, 0.0), Color(GOLD.r, GOLD.g, GOLD.b, 0.75), 2.0)
	canvas.draw_line(at + Vector2(0.0, 1.5), at + Vector2(width, 1.5),
		Color(0.0, 0.0, 0.0, 0.5), 1.0)
	for fraction in [0.0, 0.5, 1.0]:
		_draw_diamond(canvas, at + Vector2(width * fraction, 0.0), 4.0, GOLD_BRIGHT)


## A round stud, the fitting retail puts at a frame's corners and beside a value.
static func draw_stud(canvas: CanvasItem, centre: Vector2, radius: float) -> void:
	canvas.draw_circle(centre, radius, BRASS_DARK)
	canvas.draw_arc(centre, radius, 0.0, TAU, 20, GOLD, 1.5)
	canvas.draw_circle(centre - Vector2(radius * 0.3, radius * 0.3), radius * 0.28,
		Color(GOLD_PALE.r, GOLD_PALE.g, GOLD_PALE.b, 0.55))


## A small filled chip with a coloured dot - the shape a legend entry should be
## rather than a word in a row of words.
static func draw_chip(canvas: CanvasItem, rect: Rect2, tint: Color) -> void:
	canvas.draw_rect(rect, Color(0.10, 0.09, 0.07, 0.92))
	canvas.draw_rect(rect, Color(tint.r, tint.g, tint.b, 0.85), false, 1.5)
	var dot := Vector2(rect.position.x + 10.0, rect.position.y + rect.size.y * 0.5)
	canvas.draw_circle(dot, 5.0, tint)
	canvas.draw_arc(dot, 5.0, 0.0, TAU, 16, Color(0.0, 0.0, 0.0, 0.55), 1.0)


## A value plate: the little recessed box a number sits in on retail's header.
static func draw_value_plate(canvas: CanvasItem, rect: Rect2) -> void:
	canvas.draw_rect(rect, Color(0.05, 0.045, 0.035, 0.95))
	canvas.draw_rect(rect, Color(BRASS.r, BRASS.g, BRASS.b, 0.9), false, 1.5)
	canvas.draw_line(
		rect.position + Vector2(2.0, 1.5),
		rect.position + Vector2(rect.size.x - 2.0, 1.5),
		Color(GOLD.r, GOLD.g, GOLD.b, 0.35), 1.0)


static func _draw_studs(canvas: CanvasItem, rect: Rect2) -> void:
	var inset := 7.0
	for corner in [
		rect.position + Vector2(inset, inset),
		rect.position + Vector2(rect.size.x - inset, inset),
		rect.position + Vector2(inset, rect.size.y - inset),
		rect.position + Vector2(rect.size.x - inset, rect.size.y - inset),
	]:
		draw_stud(canvas, corner, 4.0)


static func _draw_diamond(canvas: CanvasItem, centre: Vector2, radius: float, tint: Color) -> void:
	canvas.draw_colored_polygon(PackedVector2Array([
		centre + Vector2(0.0, -radius),
		centre + Vector2(radius, 0.0),
		centre + Vector2(0.0, radius),
		centre + Vector2(-radius, 0.0),
	]), tint)

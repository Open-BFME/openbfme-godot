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


# =============================================================================
# THE SECOND LANGUAGE: BLUE STEEL, for the setup screens.
#
# ALSO HAND-BUILT. The paragraph at the top of this file applies to every
# primitive below it without exception - retail's frame art is a nine-slice of
# `MappedImage` definitions naming `SCShellUserInterface512_001.tga` and
# `MainMenuRuleruserinterface.tga`, and both of those files are shipped by
# nothing in the archives. Nothing here is retail's pixels.
#
# WHY A SECOND PALETTE AT ALL. Retail's shell speaks two visual languages and
# uses them to mean different things. The IN-PLAY strategic screen is warm gold
# and brass (everything above this line). The SETUP screens - lobby, skirmish
# setup, and the War of the Ring GAME SETUP screen this was added for - are COLD
# BLUE STEEL: a dark blue-grey field, engraved rules rather than cast ones, tabs
# that sit above their panel and merge into it when active, list boxes recessed
# behind a faint glass sheen, and dropdowns fitted with a chevron stud on a
# separate plate at the right-hand end. Drawing the setup screen in the in-play
# gold would put the wrong screen's clothes on it.
#
# Nothing below reads or writes simulation state; every function takes a canvas
# and a rect.
# =============================================================================

const STEEL_VOID := Color("#070a0e")
const STEEL_DEEP := Color("#0d1219")
const STEEL_FIELD := Color(0.055, 0.075, 0.10, 0.94)
const STEEL_FIELD_DEEP := Color(0.035, 0.05, 0.072, 0.96)
const STEEL_DARK := Color("#1b2735")
const STEEL := Color("#3c5674")
const STEEL_BRIGHT := Color("#6f90b4")
const STEEL_PALE := Color("#b9cee4")
const STEEL_ICE := Color("#dbe9f7")


## The outer frame of a setup screen: a deep field, a heavy engraved steel rule,
## a bright inner hairline, and a stud at each corner. `depth` 0 is the screen
## frame, 1 a panel inside it, 2 a recessed field.
static func draw_steel_frame(canvas: CanvasItem, rect: Rect2, depth: int = 0) -> void:
	if rect.size.x <= 6.0 or rect.size.y <= 6.0:
		return
	var fill := STEEL_FIELD
	if depth >= 2:
		fill = STEEL_FIELD_DEEP
	elif depth == 1:
		fill = Color(0.048, 0.066, 0.09, 0.95)
	canvas.draw_rect(rect, fill)
	# The engraving: a dark groove outside a lit rule, which is what makes a flat
	# rectangle read as a bevel cut into metal.
	canvas.draw_rect(rect.grow(2.0), STEEL_VOID, false, 3.0)
	canvas.draw_rect(rect, STEEL, false, 2.0)
	canvas.draw_rect(rect.grow(-2.0), Color(STEEL_BRIGHT.r, STEEL_BRIGHT.g, STEEL_BRIGHT.b, 0.42), false, 1.0)
	# The lit top edge and shadowed bottom edge - a single light from above.
	canvas.draw_line(rect.position + Vector2(3.0, 3.0),
		rect.position + Vector2(rect.size.x - 3.0, 3.0),
		Color(STEEL_PALE.r, STEEL_PALE.g, STEEL_PALE.b, 0.28), 1.0)
	canvas.draw_line(rect.position + Vector2(3.0, rect.size.y - 3.0),
		rect.position + Vector2(rect.size.x - 3.0, rect.size.y - 3.0),
		Color(0.0, 0.0, 0.0, 0.45), 1.0)
	if depth == 0:
		_draw_steel_studs(canvas, rect)


## The heavy plate a setup screen's name sits on, with an engraved rule under it
## and a diamond at each end - the cold twin of `draw_title_plate`.
static func draw_steel_title_plate(canvas: CanvasItem, rect: Rect2) -> void:
	if rect.size.x <= 8.0 or rect.size.y <= 6.0:
		return
	canvas.draw_rect(rect, Color(0.06, 0.085, 0.115, 0.95))
	canvas.draw_rect(rect, STEEL, false, 2.0)
	canvas.draw_rect(rect.grow(-3.0), Color(STEEL_BRIGHT.r, STEEL_BRIGHT.g, STEEL_BRIGHT.b, 0.35), false, 1.0)
	draw_steel_rule(canvas,
		Vector2(rect.position.x + 10.0, rect.position.y + rect.size.y - 4.0),
		rect.size.x - 20.0)


## An engraved rule: a dark groove with a lit line above it and a diamond at each
## end and the middle. Retail's `Ruler` image names a texture no archive ships,
## so this is drawn, like everything else in this file.
static func draw_steel_rule(canvas: CanvasItem, at: Vector2, width: float) -> void:
	if width <= 12.0:
		return
	canvas.draw_line(at, at + Vector2(width, 0.0),
		Color(STEEL_BRIGHT.r, STEEL_BRIGHT.g, STEEL_BRIGHT.b, 0.7), 1.5)
	canvas.draw_line(at + Vector2(0.0, 1.5), at + Vector2(width, 1.5),
		Color(0.0, 0.0, 0.0, 0.6), 1.0)
	for fraction in [0.0, 0.5, 1.0]:
		_draw_diamond(canvas, at + Vector2(width * fraction, 0.0), 3.5, STEEL_PALE)


## A TAB. Retail's setup tabs are trapezoids that sit on top of their panel; the
## active one loses its bottom edge so it reads as continuous with the panel
## below, and gains a lit top edge.
static func draw_tab(canvas: CanvasItem, rect: Rect2, active: bool) -> void:
	if rect.size.x <= 8.0 or rect.size.y <= 6.0:
		return
	var slant := minf(10.0, rect.size.x * 0.18)
	var body := PackedVector2Array([
		rect.position + Vector2(slant, 0.0),
		rect.position + Vector2(rect.size.x - slant, 0.0),
		rect.position + Vector2(rect.size.x, rect.size.y),
		rect.position + Vector2(0.0, rect.size.y),
	])
	canvas.draw_colored_polygon(body,
		Color(0.075, 0.105, 0.145, 0.97) if active else Color(0.032, 0.046, 0.064, 0.92))
	# The outline stops short of the bottom on an active tab, which is the whole
	# trick: the tab and its panel become one shape.
	var edge := STEEL_BRIGHT if active else STEEL_DARK
	canvas.draw_polyline(PackedVector2Array([
		body[3], body[0], body[1], body[2],
	]), edge, 2.0)
	if not active:
		canvas.draw_line(body[3], body[2], STEEL_DARK, 2.0)
	else:
		canvas.draw_line(rect.position + Vector2(slant + 2.0, 2.5),
			rect.position + Vector2(rect.size.x - slant - 2.0, 2.5),
			Color(STEEL_ICE.r, STEEL_ICE.g, STEEL_ICE.b, 0.35), 1.0)


## AN INSET GLASS FIELD - retail's list and description boxes. A deep recess with
## a hard dark top-left shadow, a lit bottom-right, and a faint sheen across the
## upper third so it reads as glass over the recess rather than a hole.
static func draw_inset_glass(canvas: CanvasItem, rect: Rect2) -> void:
	if rect.size.x <= 6.0 or rect.size.y <= 6.0:
		return
	canvas.draw_rect(rect, STEEL_FIELD_DEEP)
	canvas.draw_line(rect.position, rect.position + Vector2(rect.size.x, 0.0),
		Color(0.0, 0.0, 0.0, 0.85), 2.0)
	canvas.draw_line(rect.position, rect.position + Vector2(0.0, rect.size.y),
		Color(0.0, 0.0, 0.0, 0.85), 2.0)
	canvas.draw_line(rect.position + Vector2(0.0, rect.size.y),
		rect.position + rect.size,
		Color(STEEL_BRIGHT.r, STEEL_BRIGHT.g, STEEL_BRIGHT.b, 0.5), 1.5)
	canvas.draw_line(rect.position + Vector2(rect.size.x, 0.0),
		rect.position + rect.size,
		Color(STEEL_BRIGHT.r, STEEL_BRIGHT.g, STEEL_BRIGHT.b, 0.5), 1.5)
	# The sheen is a HIGHLIGHT, not a fill: it is capped so a tall list box does
	# not end up two-tone from top to bottom.
	var sheen := Rect2(rect.position + Vector2(2.0, 2.0),
		Vector2(rect.size.x - 4.0, clampf(rect.size.y * 0.28, 3.0, 16.0)))
	canvas.draw_rect(sheen, Color(STEEL_PALE.r, STEEL_PALE.g, STEEL_PALE.b, 0.05))
	canvas.draw_rect(rect, STEEL_DARK, false, 1.0)


## A DROPDOWN. Retail fits a separate plate at the right-hand end carrying a
## chevron stud; this draws the field, that plate and the chevron. `enabled`
## false drains the colour, which is how a locked row is shown as locked rather
## than as an ordinary row nobody happened to click.
static func draw_dropdown(canvas: CanvasItem, rect: Rect2, enabled: bool = true) -> void:
	if rect.size.x <= 20.0 or rect.size.y <= 8.0:
		return
	var dim := 1.0 if enabled else 0.42
	canvas.draw_rect(rect, Color(0.045, 0.062, 0.086, 0.95))
	canvas.draw_rect(rect, Color(STEEL.r, STEEL.g, STEEL.b, dim), false, 1.5)
	canvas.draw_line(rect.position + Vector2(1.5, 1.5),
		rect.position + Vector2(rect.size.x - 1.5, 1.5),
		Color(0.0, 0.0, 0.0, 0.6), 1.0)
	var stud_width := minf(24.0, rect.size.x * 0.22)
	var plate := Rect2(
		rect.position + Vector2(rect.size.x - stud_width, 1.0),
		Vector2(stud_width - 1.0, rect.size.y - 2.0))
	canvas.draw_rect(plate, Color(0.09, 0.125, 0.17, 0.95))
	canvas.draw_rect(plate, Color(STEEL_BRIGHT.r, STEEL_BRIGHT.g, STEEL_BRIGHT.b, 0.45 * dim), false, 1.0)
	var centre := plate.position + plate.size * 0.5
	var arm := minf(5.0, plate.size.x * 0.28)
	canvas.draw_polyline(PackedVector2Array([
		centre + Vector2(-arm, -arm * 0.55),
		centre + Vector2(0.0, arm * 0.6),
		centre + Vector2(arm, -arm * 0.55),
	]), Color(STEEL_ICE.r, STEEL_ICE.g, STEEL_ICE.b, dim), 2.0)


## A CHECKBOX in the same steel. `checked` draws retail's tick; `enabled` false
## drains it the same way a locked dropdown is drained.
static func draw_checkbox(canvas: CanvasItem, rect: Rect2, checked: bool, enabled: bool = true) -> void:
	if rect.size.x <= 6.0 or rect.size.y <= 6.0:
		return
	var dim := 1.0 if enabled else 0.42
	canvas.draw_rect(rect, Color(0.035, 0.05, 0.072, 0.96))
	canvas.draw_line(rect.position, rect.position + Vector2(rect.size.x, 0.0),
		Color(0.0, 0.0, 0.0, 0.85), 2.0)
	canvas.draw_line(rect.position, rect.position + Vector2(0.0, rect.size.y),
		Color(0.0, 0.0, 0.0, 0.85), 2.0)
	canvas.draw_rect(rect, Color(STEEL.r, STEEL.g, STEEL.b, dim), false, 1.5)
	if not checked:
		return
	var inset := rect.size.x * 0.24
	canvas.draw_polyline(PackedVector2Array([
		rect.position + Vector2(inset, rect.size.y * 0.52),
		rect.position + Vector2(rect.size.x * 0.44, rect.size.y - inset),
		rect.position + Vector2(rect.size.x - inset, inset),
	]), Color(STEEL_ICE.r, STEEL_ICE.g, STEEL_ICE.b, dim), 2.5)


## A PUSH BUTTON on the bottom bar. `state` is 0 normal, 1 hovered, 2 the primary
## action (retail lights PLAY brighter than its neighbours), 3 disabled.
static func draw_steel_button(canvas: CanvasItem, rect: Rect2, state: int = 0) -> void:
	if rect.size.x <= 8.0 or rect.size.y <= 8.0:
		return
	var body := Color(0.07, 0.098, 0.135, 0.95)
	var edge := STEEL
	match state:
		1:
			body = Color(0.10, 0.14, 0.19, 0.96)
			edge = STEEL_BRIGHT
		2:
			body = Color(0.09, 0.13, 0.185, 0.97)
			edge = STEEL_PALE
		3:
			body = Color(0.04, 0.052, 0.068, 0.85)
			edge = Color(STEEL_DARK.r, STEEL_DARK.g, STEEL_DARK.b, 0.7)
	canvas.draw_rect(rect, body)
	canvas.draw_rect(rect.grow(1.0), STEEL_VOID, false, 2.0)
	canvas.draw_rect(rect, edge, false, 2.0)
	if state != 3:
		canvas.draw_line(rect.position + Vector2(3.0, 2.5),
			rect.position + Vector2(rect.size.x - 3.0, 2.5),
			Color(STEEL_ICE.r, STEEL_ICE.g, STEEL_ICE.b, 0.22), 1.0)
		draw_stud(canvas, rect.position + Vector2(9.0, rect.size.y * 0.5), 3.0)
		draw_stud(canvas, rect.position + Vector2(rect.size.x - 9.0, rect.size.y * 0.5), 3.0)


## A SEAT COLOUR SWATCH: retail's colour is shown as a filled chip in a steel
## bezel, not as a word. `tint` is the colour block's own `LivingWorldColor`.
static func draw_swatch(canvas: CanvasItem, rect: Rect2, tint: Color) -> void:
	if rect.size.x <= 4.0 or rect.size.y <= 4.0:
		return
	canvas.draw_rect(rect, tint)
	canvas.draw_rect(rect.grow(1.0), STEEL_VOID, false, 2.0)
	canvas.draw_rect(rect, Color(STEEL_PALE.r, STEEL_PALE.g, STEEL_PALE.b, 0.55), false, 1.0)
	canvas.draw_line(rect.position + Vector2(1.5, 1.5),
		rect.position + Vector2(rect.size.x - 1.5, 1.5),
		Color(1.0, 1.0, 1.0, 0.22), 1.0)


## The rule under a table's column headings, and the hairlines between its rows.
static func draw_row_rule(canvas: CanvasItem, at: Vector2, width: float, strong: bool = false) -> void:
	if width <= 4.0:
		return
	canvas.draw_line(at, at + Vector2(width, 0.0),
		Color(STEEL_BRIGHT.r, STEEL_BRIGHT.g, STEEL_BRIGHT.b, 0.55 if strong else 0.16),
		2.0 if strong else 1.0)


## The band a table row sits in. `tone` 0 is an ordinary row, 1 the alternate
## stripe, 2 the row belonging to the seat this machine is playing.
static func draw_row_band(canvas: CanvasItem, rect: Rect2, tone: int) -> void:
	match tone:
		1:
			canvas.draw_rect(rect, Color(1.0, 1.0, 1.0, 0.022))
		2:
			canvas.draw_rect(rect, Color(STEEL_BRIGHT.r, STEEL_BRIGHT.g, STEEL_BRIGHT.b, 0.10))
			canvas.draw_rect(rect, Color(STEEL_PALE.r, STEEL_PALE.g, STEEL_PALE.b, 0.30), false, 1.0)


static func _draw_steel_studs(canvas: CanvasItem, rect: Rect2) -> void:
	var inset := 9.0
	for corner in [
		rect.position + Vector2(inset, inset),
		rect.position + Vector2(rect.size.x - inset, inset),
		rect.position + Vector2(inset, rect.size.y - inset),
		rect.position + Vector2(rect.size.x - inset, rect.size.y - inset),
	]:
		canvas.draw_circle(corner, 5.0, STEEL_DEEP)
		canvas.draw_arc(corner, 5.0, 0.0, TAU, 20, STEEL_BRIGHT, 1.5)
		canvas.draw_circle(corner - Vector2(1.6, 1.6), 1.5,
			Color(STEEL_ICE.r, STEEL_ICE.g, STEEL_ICE.b, 0.6))

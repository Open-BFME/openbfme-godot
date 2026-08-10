class_name OpenBFMETheme
extends RefCounted
## Repository-authored presentation for the legal-safe OpenBFME shell, styled
## toward the BFME2 shell (reference captures REF-01/02/05/06): translucent
## dark-green metallic buttons with pointed end caps, bright green edge,
## pale green-gold text, and a bright lime hover state with outer glow.
## Button art is generated procedurally here (nine-slice textures); nothing is
## copied from retail assets. When the selected content pack ships the
## Albertus MT face it is passed in and used as the shell font, otherwise
## Godot's default face renders the shell.

const INK := Color("#050d08")
const PANEL := Color("#0c1810")
const FOREST := Color("#1d3a24")
const FOREST_BRIGHT := Color("#3f6b46")
const PARCHMENT := Color("#e8e3c8")
const PARCHMENT_DIM := Color("#a9b39a")
const GOLD := Color("#b99a55")
const GOLD_BRIGHT := Color("#e1c77d")
const TEXT_LEAF := Color("#cfe0b0")
const TEXT_LEAF_BRIGHT := Color("#eaf7d0")
const TEXT_ON_LIME := Color("#12300f")
const EDGE_LEAF := Color(0.55, 0.80, 0.45, 0.95)
## Retail shell button language (REF-01..REF-07): pale-gold small-caps labels
## over a dark green-glass body inside a metallic green-gold rim.
const TEXT_PALE_GOLD := Color("#ddd6a8")
const TEXT_PALE_GOLD_BRIGHT := Color("#f4eecb")
const RIM_GOLD_GREEN := Color("#89a55c")
const RIM_GOLD_GREEN_DARK := Color("#3c5a33")
const GLASS_TOP := Color(0.23, 0.38, 0.20, 0.90)
const GLASS_BOTTOM := Color(0.05, 0.13, 0.06, 0.92)
## Flyout item bars (REF-02/04/05): translucent dark bars with a lit top-left
## bevel; the hovered bar turns bright lime like the source button.
const ITEM_BAR := Color(0.045, 0.115, 0.055, 0.62)
const ITEM_BAR_EDGE := Color(0.78, 0.90, 0.66, 0.55)


static func create_theme(retail_font: Font = null) -> Theme:
	var result := Theme.new()
	result.default_font_size = 20
	if retail_font != null:
		result.default_font = retail_font

	var button_states := _button_state_textures()
	var primary_states := _button_state_textures(true)

	result.set_color("font_color", "Label", PARCHMENT)
	result.set_color("font_shadow_color", "Label", Color(0.0, 0.0, 0.0, 0.85))
	result.set_constant("shadow_offset_x", "Label", 2)
	result.set_constant("shadow_offset_y", "Label", 2)

	result.set_color("font_color", "Button", TEXT_LEAF)
	result.set_color("font_hover_color", "Button", TEXT_ON_LIME)
	result.set_color("font_pressed_color", "Button", TEXT_LEAF_BRIGHT)
	result.set_color("font_focus_color", "Button", TEXT_LEAF_BRIGHT)
	result.set_color("font_disabled_color", "Button", Color(0.45, 0.52, 0.40, 0.5))
	result.set_font_size("font_size", "Button", 22)
	result.set_stylebox("normal", "Button", _cap_box(button_states["normal"]))
	result.set_stylebox("hover", "Button", _cap_box(button_states["hover"]))
	result.set_stylebox("pressed", "Button", _cap_box(button_states["pressed"]))
	result.set_stylebox("focus", "Button", _cap_box(button_states["focus"]))
	result.set_stylebox("disabled", "Button", _cap_box(button_states["disabled"]))
	result.set_constant("outline_size", "Button", 1)
	result.set_color("font_outline_color", "Button", Color(0.0, 0.0, 0.0, 0.8))

	result.set_color("font_color", "CheckButton", TEXT_LEAF)
	result.set_color("font_hover_color", "CheckButton", TEXT_LEAF_BRIGHT)
	result.set_color("font_pressed_color", "CheckButton", TEXT_LEAF_BRIGHT)
	result.set_font_size("font_size", "CheckButton", 20)
	# Retail bright-green checked box: Godot's default check icons render near
	# black on our dark buttons, so the toggle graphics are generated here.
	result.set_icon("checked", "CheckButton", _generate_check_icon(true, false))
	result.set_icon("unchecked", "CheckButton", _generate_check_icon(false, false))
	result.set_icon("checked_disabled", "CheckButton", _generate_check_icon(true, true))
	result.set_icon("unchecked_disabled", "CheckButton", _generate_check_icon(false, true))
	result.set_icon("radio_checked", "CheckButton", _generate_check_icon(true, false))
	result.set_icon("radio_unchecked", "CheckButton", _generate_check_icon(false, false))
	result.set_icon("radio_checked_disabled", "CheckButton", _generate_check_icon(true, true))
	result.set_icon("radio_unchecked_disabled", "CheckButton", _generate_check_icon(false, true))

	result.set_stylebox("slider", "HSlider", _slider_track(Color("#0a130c")))
	result.set_stylebox("grabber_area", "HSlider", _slider_track(FOREST_BRIGHT))
	result.set_stylebox("grabber_area_highlight", "HSlider", _slider_track(Color("7fb35a")))

	result.set_stylebox("normal", "OptionButton", _cap_box(button_states["normal"]))
	result.set_stylebox("hover", "OptionButton", _cap_box(button_states["hover"]))
	result.set_stylebox("pressed", "OptionButton", _cap_box(button_states["pressed"]))
	result.set_stylebox("focus", "OptionButton", _cap_box(button_states["focus"]))
	result.set_stylebox("disabled", "OptionButton", _cap_box(button_states["disabled"]))
	result.set_color("font_color", "OptionButton", TEXT_LEAF)
	result.set_color("font_hover_color", "OptionButton", TEXT_ON_LIME)
	result.set_color("font_pressed_color", "OptionButton", TEXT_LEAF_BRIGHT)
	result.set_color("font_disabled_color", "OptionButton", Color(0.45, 0.52, 0.40, 0.5))
	result.set_font_size("font_size", "OptionButton", 18)

	result.set_stylebox("panel", "Panel", _panel_box())
	result.set_stylebox("panel", "PanelContainer", _panel_box())

	result.set_type_variation("NavigationPanel", "Panel")
	result.set_stylebox("panel", "NavigationPanel", _navigation_panel_box())
	result.set_type_variation("OverlayPanel", "Panel")
	result.set_stylebox("panel", "OverlayPanel", _overlay_panel_box())
	result.set_type_variation("FlyoutPanel", "Panel")
	result.set_stylebox("panel", "FlyoutPanel", _flyout_panel_box())
	result.set_type_variation("ColumnHeader", "Panel")
	result.set_stylebox("panel", "ColumnHeader", _column_header_box())

	result.set_type_variation("NavButton", "Button")
	result.set_font_size("font_size", "NavButton", 19)
	result.set_color("font_color", "NavButton", TEXT_PALE_GOLD)
	result.set_color("font_hover_color", "NavButton", TEXT_ON_LIME)
	result.set_color("font_pressed_color", "NavButton", TEXT_LEAF_BRIGHT)
	result.set_color("font_focus_color", "NavButton", TEXT_LEAF_BRIGHT)
	result.set_color("font_disabled_color", "NavButton", Color(0.45, 0.52, 0.40, 0.5))
	result.set_stylebox("normal", "NavButton", _cap_box(button_states["normal"]))
	result.set_stylebox("hover", "NavButton", _cap_box(button_states["hover"]))
	result.set_stylebox("pressed", "NavButton", _cap_box(button_states["pressed"]))
	result.set_stylebox("focus", "NavButton", _cap_box(button_states["focus"]))
	result.set_stylebox("disabled", "NavButton", _cap_box(button_states["disabled"]))

	# Flyout item bars (REF-02/04/05): flat translucent rows inside the flyout
	# frame, a lit top edge and gold left accent, lime hover with dark text.
	result.set_type_variation("FlyoutItem", "Button")
	result.set_font_size("font_size", "FlyoutItem", 17)
	result.set_color("font_color", "FlyoutItem", TEXT_PALE_GOLD)
	result.set_color("font_hover_color", "FlyoutItem", TEXT_ON_LIME)
	result.set_color("font_pressed_color", "FlyoutItem", TEXT_ON_LIME)
	result.set_color("font_focus_color", "FlyoutItem", TEXT_PALE_GOLD_BRIGHT)
	result.set_color("font_disabled_color", "FlyoutItem", Color(0.62, 0.62, 0.50, 0.42))
	result.set_stylebox("normal", "FlyoutItem", _flyout_item_box("normal"))
	result.set_stylebox("hover", "FlyoutItem", _flyout_item_box("hover"))
	result.set_stylebox("pressed", "FlyoutItem", _flyout_item_box("pressed"))
	result.set_stylebox("focus", "FlyoutItem", _flyout_item_box("focus"))
	result.set_stylebox("disabled", "FlyoutItem", _flyout_item_box("disabled"))

	# Hover tooltips (REF-06 "Quit to desktop"): a small dark parchment-text box.
	var tooltip_box := StyleBoxFlat.new()
	tooltip_box.bg_color = Color(0.03, 0.06, 0.04, 0.94)
	tooltip_box.border_color = Color(GOLD, 0.75)
	tooltip_box.set_border_width_all(1)
	tooltip_box.content_margin_left = 10.0
	tooltip_box.content_margin_right = 10.0
	tooltip_box.content_margin_top = 5.0
	tooltip_box.content_margin_bottom = 5.0
	result.set_stylebox("panel", "TooltipPanel", tooltip_box)
	result.set_color("font_color", "TooltipLabel", PARCHMENT)
	result.set_font_size("font_size", "TooltipLabel", 15)

	# ---------------------------------------------------------------- CARD SHELL
	#
	# THE VARIATIONS THE CREATE-A-HERO SCREEN IS COMPOSED OUT OF. That screen is
	# a page of framed cards rather than a wall of controls on black, and it draws
	# rows of 56px retail icons - neither of which the shell's own button language
	# can express: `_cap_box` reserves 26px of content margin on each side for a
	# text label, so a 56px icon button had 4px of room left to draw a 56px icon
	# in and the class row came out as a row of dots.
	result.set_type_variation("CahCard", "PanelContainer")
	result.set_stylebox("panel", "CahCard", _card_box(false))
	result.set_type_variation("CahCardSunken", "PanelContainer")
	result.set_stylebox("panel", "CahCardSunken", _card_box(true))

	var slot_states := _icon_slot_boxes()
	result.set_type_variation("CahIconSlot", "Button")
	result.set_font_size("font_size", "CahIconSlot", 14)
	result.set_color("font_color", "CahIconSlot", TEXT_PALE_GOLD)
	result.set_color("font_hover_color", "CahIconSlot", TEXT_LEAF_BRIGHT)
	result.set_color("font_pressed_color", "CahIconSlot", TEXT_LEAF_BRIGHT)
	result.set_color("font_focus_color", "CahIconSlot", TEXT_LEAF_BRIGHT)
	result.set_color("font_disabled_color", "CahIconSlot", Color(0.50, 0.55, 0.45, 0.55))
	for state in slot_states.keys():
		result.set_stylebox(String(state), "CahIconSlot", slot_states[state])

	# The small square stepper the garment and attribute rows are driven with.
	result.set_type_variation("CahStepper", "Button")
	result.set_font_size("font_size", "CahStepper", 18)
	result.set_color("font_color", "CahStepper", TEXT_PALE_GOLD)
	result.set_color("font_hover_color", "CahStepper", TEXT_ON_LIME)
	result.set_color("font_pressed_color", "CahStepper", TEXT_LEAF_BRIGHT)
	var stepper_states := _stepper_boxes()
	for state in stepper_states.keys():
		result.set_stylebox(String(state), "CahStepper", stepper_states[state])

	# Attribute and stat bars. Godot's default ProgressBar is a grey slab that
	# reads as a loading bar; these are the gold-on-ink pips the shell uses.
	result.set_type_variation("CahBar", "ProgressBar")
	var bar_background := StyleBoxFlat.new()
	bar_background.bg_color = Color(0.02, 0.05, 0.03, 0.92)
	bar_background.border_color = Color(GOLD, 0.45)
	bar_background.set_border_width_all(1)
	bar_background.set_corner_radius_all(2)
	result.set_stylebox("background", "CahBar", bar_background)
	var bar_fill := StyleBoxFlat.new()
	bar_fill.bg_color = Color(0.62, 0.52, 0.24, 0.96)
	bar_fill.border_color = Color(GOLD_BRIGHT, 0.85)
	bar_fill.set_border_width_all(1)
	bar_fill.set_corner_radius_all(2)
	result.set_stylebox("fill", "CahBar", bar_fill)

	result.set_type_variation("PrimaryButton", "Button")
	result.set_font_size("font_size", "PrimaryButton", 21)
	result.set_color("font_color", "PrimaryButton", Color("f0deb0"))
	result.set_color("font_hover_color", "PrimaryButton", TEXT_ON_LIME)
	result.set_color("font_pressed_color", "PrimaryButton", TEXT_LEAF_BRIGHT)
	result.set_color("font_disabled_color", "PrimaryButton", Color(0.45, 0.52, 0.40, 0.5))
	result.set_stylebox("normal", "PrimaryButton", _cap_box(primary_states["normal"]))
	result.set_stylebox("hover", "PrimaryButton", _cap_box(primary_states["hover"]))
	result.set_stylebox("pressed", "PrimaryButton", _cap_box(primary_states["pressed"]))
	result.set_stylebox("focus", "PrimaryButton", _cap_box(primary_states["focus"]))
	result.set_stylebox("disabled", "PrimaryButton", _cap_box(primary_states["disabled"]))
	return result


static func _button_state_textures(brighter: bool = false) -> Dictionary:
	var lift := 0.05 if brighter else 0.0
	return {
		"normal": _generate_cap_texture(
			Color(0.15 + lift, 0.33 + lift, 0.17 + lift, 0.88),
			Color(0.06 + lift, 0.15 + lift, 0.08 + lift, 0.90),
			EDGE_LEAF,
			Color(0.35, 0.75, 0.30, 0.30)
		),
		"hover": _generate_cap_texture(
			Color(0.62, 0.84, 0.42, 0.98),
			Color(0.38, 0.60, 0.27, 0.98),
			Color(0.86, 1.00, 0.66, 1.00),
			Color(0.65, 1.00, 0.45, 0.85)
		),
		"pressed": _generate_cap_texture(
			Color(0.10, 0.24, 0.12, 0.97),
			Color(0.04, 0.11, 0.06, 0.97),
			Color(0.70, 0.92, 0.55, 1.00),
			Color(0.35, 0.75, 0.30, 0.25)
		),
		"focus": _generate_cap_texture(
			Color(0.15 + lift, 0.33 + lift, 0.17 + lift, 0.88),
			Color(0.06 + lift, 0.15 + lift, 0.08 + lift, 0.90),
			Color(0.88, 0.85, 0.50, 0.95),
			Color(0.55, 0.80, 0.40, 0.40)
		),
		"disabled": _generate_cap_texture(
			Color(0.09, 0.12, 0.08, 0.60),
			Color(0.05, 0.07, 0.05, 0.64),
			Color(0.30, 0.38, 0.26, 0.55),
			Color(0.0, 0.0, 0.0, 0.0)
		),
	}


static func _cap_box(texture: Texture2D) -> StyleBoxTexture:
	var box := StyleBoxTexture.new()
	box.texture = texture
	box.texture_margin_left = 20.0
	box.texture_margin_right = 20.0
	box.texture_margin_top = 14.0
	box.texture_margin_bottom = 14.0
	box.content_margin_left = 26.0
	box.content_margin_right = 26.0
	box.content_margin_top = 10.0
	box.content_margin_bottom = 10.0
	return box


static func _generate_cap_texture(top: Color, bottom: Color, edge: Color, glow: Color) -> ImageTexture:
	## 120x56 nine-slice button matching the retail stone-glass treatment
	## (REF-01..REF-07): a chamfered-corner slab with pointed side caps, a
	## metallic green-gold double rim (dark outer line, lit inner line), a
	## vertical glass gradient with a sheen across the upper half, and a soft
	## outer glow baked around the silhouette. Procedural repository-authored
	## art; no retail bytes.
	var width := 120
	var height := 56
	var chamfer := 10.0
	var side_point := 6.0
	var image := Image.create_empty(width, height, false, Image.FORMAT_RGBA8)
	image.fill(Color(0, 0, 0, 0))
	var center := Vector2(width, height) * 0.5
	var polygon := PackedVector2Array([
		Vector2(chamfer + side_point, 2), Vector2(width - chamfer - side_point, 2),
		Vector2(width - side_point - 2, chamfer), Vector2(width - 2, height * 0.5),
		Vector2(width - side_point - 2, height - chamfer),
		Vector2(width - chamfer - side_point, height - 2), Vector2(chamfer + side_point, height - 2),
		Vector2(side_point + 2, height - chamfer), Vector2(2, height * 0.5),
		Vector2(side_point + 2, chamfer),
	])
	for layer in range(3, 0, -1):
		var glow_color := glow
		glow_color.a *= 0.34 / float(layer)
		_fill_polygon(image, _dilate_polygon(polygon, float(layer) * 2.0, center), glow_color)
	for y in range(height):
		var t := float(y) / float(height - 1)
		var row := top.lerp(bottom, t)
		# Glass sheen: the upper half carries a soft highlight that fades out by
		# mid-height, echoing the retail buttons' lit top edge.
		if t < 0.48:
			var sheen := (0.48 - t) / 0.48
			row = row.lightened(sheen * 0.16)
		for x in range(width):
			if Geometry2D.is_point_in_polygon(Vector2(x + 0.5, y + 0.5), polygon):
				image.set_pixel(x, y, row)
	# Double rim: a dark metallic outer line under a lit inner line reads as the
	# retail gold-green frame at shell scale.
	var rim_dark := Color(edge.r * 0.35, edge.g * 0.40, edge.b * 0.30, minf(1.0, edge.a + 0.05))
	for edge_index in range(polygon.size()):
		var a := polygon[edge_index]
		var b := polygon[(edge_index + 1) % polygon.size()]
		_stroke_segment(image, a, b, rim_dark, 3)
	var inner := _dilate_polygon(polygon, -2.0, center)
	for edge_index in range(inner.size()):
		var a := inner[edge_index]
		var b := inner[(edge_index + 1) % inner.size()]
		_stroke_segment(image, a, b, edge, 1)
	return ImageTexture.create_from_image(image)


static func _flyout_item_box(state: String) -> StyleBoxFlat:
	## One flyout row (REF-02/04/05): a translucent dark bar with a lit top
	## edge and a gold left accent; hover/pressed swap to the bright lime bar.
	var box := StyleBoxFlat.new()
	match state:
		"hover":
			box.bg_color = Color(0.55, 0.76, 0.36, 0.95)
			box.border_color = Color(0.88, 1.0, 0.68, 0.95)
		"pressed":
			box.bg_color = Color(0.44, 0.64, 0.29, 0.97)
			box.border_color = Color(0.88, 1.0, 0.68, 0.95)
		"disabled":
			box.bg_color = Color(0.035, 0.075, 0.045, 0.48)
			box.border_color = Color(ITEM_BAR_EDGE, 0.22)
		"focus":
			box.bg_color = ITEM_BAR
			box.border_color = Color(GOLD_BRIGHT, 0.8)
		_:
			box.bg_color = ITEM_BAR
			box.border_color = ITEM_BAR_EDGE
	box.border_width_top = 1
	box.border_width_left = 3
	box.content_margin_left = 14.0
	box.content_margin_right = 14.0
	box.content_margin_top = 7.0
	box.content_margin_bottom = 7.0
	return box


static func _dilate_polygon(polygon: PackedVector2Array, amount: float, center: Vector2) -> PackedVector2Array:
	var result := PackedVector2Array()
	for point in polygon:
		var direction := point - center
		if direction.length() > 0.001:
			result.append(point + direction.normalized() * amount)
		else:
			result.append(point)
	return result


static func _fill_polygon(image: Image, polygon: PackedVector2Array, color: Color) -> void:
	if color.a <= 0.001:
		return
	for y in range(image.get_height()):
		for x in range(image.get_width()):
			if Geometry2D.is_point_in_polygon(Vector2(x + 0.5, y + 0.5), polygon):
				image.set_pixel(x, y, color)


static func _stroke_segment(image: Image, a: Vector2, b: Vector2, color: Color, width: int) -> void:
	var steps := int(a.distance_to(b)) + 1
	for step in range(steps + 1):
		var point := a.lerp(b, float(step) / float(steps))
		for dx in range(-width / 2, width / 2 + 1):
			for dy in range(-width / 2, width / 2 + 1):
				var px := int(point.x) + dx
				var py := int(point.y) + dy
				if px >= 0 and py >= 0 and px < image.get_width() and py < image.get_height():
					image.set_pixel(px, py, color)


static func _generate_check_icon(checked: bool, disabled: bool) -> ImageTexture:
	## 20x20 toggle graphic: dark box with a green border; the checked state is
	## a bright green fill with a light check mark, never the near-black
	## default icon.
	var size := 20
	var image := Image.create_empty(size, size, false, Image.FORMAT_RGBA8)
	var border := Color(0.55, 0.80, 0.45, 0.9 if not disabled else 0.45)
	var fill := Color(0.04, 0.09, 0.05, 0.92) if not checked else Color(0.38, 0.66, 0.30, 0.98)
	if disabled and checked:
		fill = Color(0.20, 0.34, 0.17, 0.6)
	for y in range(size):
		for x in range(size):
			var on_border := x < 2 or y < 2 or x >= size - 2 or y >= size - 2
			image.set_pixel(x, y, border if on_border else fill)
	if checked:
		var mark := Color(0.92, 1.0, 0.82, 1.0 if not disabled else 0.6)
		for step in range(4):
			image.set_pixel(5 + step, 11 + step, mark)
			image.set_pixel(5 + step, 12 + step, mark)
		for step in range(7):
			image.set_pixel(9 + step, 15 - step, mark)
			image.set_pixel(9 + step, 16 - step, mark)
	return ImageTexture.create_from_image(image)


static func _slider_track(fill: Color) -> StyleBoxFlat:
	var box := StyleBoxFlat.new()
	box.bg_color = fill
	box.border_color = Color(GOLD, 0.6)
	box.set_border_width_all(1)
	box.corner_radius_top_left = 3
	box.corner_radius_top_right = 3
	box.corner_radius_bottom_left = 3
	box.corner_radius_bottom_right = 3
	box.content_margin_top = 5.0
	box.content_margin_bottom = 5.0
	return box


static func _panel_box() -> StyleBoxFlat:
	var box := StyleBoxFlat.new()
	box.bg_color = Color(PANEL, 0.96)
	box.border_color = Color(GOLD, 0.8)
	box.set_border_width_all(1)
	box.corner_radius_top_left = 5
	box.corner_radius_top_right = 5
	box.corner_radius_bottom_left = 5
	box.corner_radius_bottom_right = 5
	box.shadow_color = Color(0.0, 0.0, 0.0, 0.72)
	box.shadow_size = 18
	return box


static func _navigation_panel_box() -> StyleBoxFlat:
	var box := StyleBoxFlat.new()
	box.bg_color = Color(0.008, 0.02, 0.012, 0.88)
	box.border_color = Color(GOLD, 0.72)
	box.border_width_top = 2
	box.shadow_color = Color(0.0, 0.0, 0.0, 0.82)
	box.shadow_size = 22
	box.content_margin_top = 10.0
	return box


static func _overlay_panel_box() -> StyleBoxFlat:
	var box := _panel_box()
	box.bg_color = Color(0.02, 0.055, 0.03, 0.965)
	box.border_color = Color(GOLD, 0.85)
	box.set_border_width_all(2)
	box.corner_radius_top_left = 2
	box.corner_radius_top_right = 2
	box.corner_radius_bottom_left = 2
	box.corner_radius_bottom_right = 2
	return box


static func _flyout_panel_box() -> StyleBoxFlat:
	## The shell flyout's thin bright-green neon frame (REF-02/05), dark
	## translucent green fill with a soft glow shadow.
	var box := StyleBoxFlat.new()
	box.bg_color = Color(0.008, 0.035, 0.018, 0.94)
	box.border_color = Color(0.45, 0.90, 0.45, 0.9)
	box.set_border_width_all(2)
	box.corner_radius_top_left = 2
	box.corner_radius_top_right = 2
	box.corner_radius_bottom_left = 2
	box.corner_radius_bottom_right = 2
	box.shadow_color = Color(0.25, 0.65, 0.25, 0.25)
	box.shadow_size = 14
	return box


static func _card_box(sunken: bool) -> StyleBoxFlat:
	## One framed card on a full-bleed screen: dark green glass inside a gold
	## hairline, with a lit top edge. `sunken` is the well a list or a grid sits
	## in - darker and unlit, so a scroll view reads as a hole in the card rather
	## than as a second card floating on it.
	var box := StyleBoxFlat.new()
	box.bg_color = Color(0.020, 0.052, 0.030, 0.90) if not sunken else Color(0.010, 0.026, 0.016, 0.86)
	box.border_color = Color(GOLD, 0.55 if not sunken else 0.34)
	box.set_border_width_all(1)
	if not sunken:
		box.border_width_top = 2
		box.border_color = Color(GOLD, 0.55)
	box.set_corner_radius_all(3)
	box.content_margin_left = 16.0
	box.content_margin_right = 16.0
	box.content_margin_top = 12.0
	box.content_margin_bottom = 12.0
	if not sunken:
		box.shadow_color = Color(0.0, 0.0, 0.0, 0.55)
		box.shadow_size = 10
	return box


static func _icon_slot_boxes() -> Dictionary:
	## The frame around one retail icon. FOUR PIXELS of content margin, because
	## the whole point of the slot is that the art fills it.
	var out := {}
	for state in ["normal", "hover", "pressed", "focus", "disabled"]:
		var box := StyleBoxFlat.new()
		box.set_corner_radius_all(2)
		box.set_border_width_all(1)
		match state:
			"hover":
				box.bg_color = Color(0.18, 0.33, 0.16, 0.95)
				box.border_color = Color(0.86, 1.00, 0.66, 0.95)
				box.set_border_width_all(2)
			"pressed":
				# SELECTED, not merely held: these slots are toggles, so `pressed`
				# is the state a chosen class or an equipped power lives in and it
				# has to be legible across the room.
				box.bg_color = Color(0.30, 0.44, 0.16, 0.98)
				box.border_color = Color(GOLD_BRIGHT, 1.0)
				box.set_border_width_all(3)
				box.shadow_color = Color(0.75, 0.65, 0.28, 0.35)
				box.shadow_size = 8
			"focus":
				box.bg_color = Color(0.06, 0.13, 0.07, 0.92)
				box.border_color = Color(GOLD_BRIGHT, 0.95)
				box.set_border_width_all(2)
			"disabled":
				box.bg_color = Color(0.035, 0.055, 0.040, 0.75)
				box.border_color = Color(0.30, 0.36, 0.28, 0.45)
			_:
				box.bg_color = Color(0.06, 0.13, 0.07, 0.92)
				box.border_color = Color(GOLD, 0.45)
		box.content_margin_left = 4.0
		box.content_margin_right = 4.0
		box.content_margin_top = 4.0
		box.content_margin_bottom = 4.0
		out[state] = box
	return out


static func _stepper_boxes() -> Dictionary:
	var out := {}
	for state in ["normal", "hover", "pressed", "focus", "disabled"]:
		var box := StyleBoxFlat.new()
		box.set_corner_radius_all(2)
		box.set_border_width_all(1)
		match state:
			"hover":
				box.bg_color = Color(0.55, 0.76, 0.36, 0.95)
				box.border_color = Color(0.88, 1.0, 0.68, 0.95)
			"pressed":
				box.bg_color = Color(0.34, 0.52, 0.22, 0.97)
				box.border_color = Color(GOLD_BRIGHT, 0.95)
			"focus":
				box.bg_color = Color(0.07, 0.15, 0.08, 0.92)
				box.border_color = Color(GOLD_BRIGHT, 0.9)
			"disabled":
				box.bg_color = Color(0.04, 0.06, 0.04, 0.6)
				box.border_color = Color(0.28, 0.33, 0.26, 0.4)
			_:
				box.bg_color = Color(0.07, 0.15, 0.08, 0.92)
				box.border_color = Color(GOLD, 0.5)
		box.content_margin_left = 6.0
		box.content_margin_right = 6.0
		box.content_margin_top = 2.0
		box.content_margin_bottom = 2.0
		out[state] = box
	return out


static func _column_header_box() -> StyleBoxFlat:
	## The deep-green banner behind an options column title, echoing the retail
	## settings screen's green header bars.
	var box := StyleBoxFlat.new()
	box.bg_color = Color(0.10, 0.28, 0.12, 0.95)
	box.border_color = Color(GOLD, 0.55)
	box.border_width_bottom = 1
	box.content_margin_left = 8.0
	box.content_margin_right = 8.0
	box.content_margin_top = 6.0
	box.content_margin_bottom = 6.0
	return box

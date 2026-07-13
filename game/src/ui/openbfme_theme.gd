class_name OpenBFMETheme
extends RefCounted
## Repository-authored presentation for the legal-safe OpenBFME shell.
## The palette evokes forged steel, cold blue glass, and aged gold without
## copying retail textures, artwork, frames, or trademarks.

const INK := Color("#071018")
const PANEL := Color("#101d29")
const STEEL := Color("#263c4d")
const STEEL_BRIGHT := Color("#527086")
const ICE := Color("#c9e1ee")
const ICE_DIM := Color("#8eadbd")
const GOLD := Color("#b99a55")
const GOLD_BRIGHT := Color("#e1c77d")


static func create_theme() -> Theme:
	var result := Theme.new()
	result.default_font_size = 20

	result.set_color("font_color", "Label", ICE)
	result.set_color("font_shadow_color", "Label", Color(0.0, 0.0, 0.0, 0.85))
	result.set_constant("shadow_offset_x", "Label", 2)
	result.set_constant("shadow_offset_y", "Label", 2)

	result.set_color("font_color", "Button", ICE)
	result.set_color("font_hover_color", "Button", Color.WHITE)
	result.set_color("font_pressed_color", "Button", GOLD_BRIGHT)
	result.set_color("font_focus_color", "Button", Color.WHITE)
	result.set_color("font_disabled_color", "Button", Color(ICE_DIM, 0.45))
	result.set_font_size("font_size", "Button", 22)
	result.set_stylebox("normal", "Button", _button_box(Color("#13293a"), STEEL))
	result.set_stylebox("hover", "Button", _button_box(Color("#1b3b50"), GOLD))
	result.set_stylebox("pressed", "Button", _button_box(Color("#091923"), GOLD_BRIGHT))
	result.set_stylebox("focus", "Button", _focus_box())
	result.set_stylebox("disabled", "Button", _button_box(Color("#0b141c"), Color("#24323c")))
	result.set_constant("outline_size", "Button", 1)
	result.set_color("font_outline_color", "Button", Color(0.0, 0.0, 0.0, 0.8))

	result.set_color("font_color", "CheckButton", ICE)
	result.set_color("font_hover_color", "CheckButton", Color.WHITE)
	result.set_font_size("font_size", "CheckButton", 20)

	result.set_stylebox("slider", "HSlider", _slider_track(Color("#09141d")))
	result.set_stylebox("grabber_area", "HSlider", _slider_track(STEEL_BRIGHT))
	result.set_stylebox("grabber_area_highlight", "HSlider", _slider_track(GOLD))

	result.set_stylebox("normal", "OptionButton", _button_box(Color("#0d202e"), STEEL))
	result.set_stylebox("hover", "OptionButton", _button_box(Color("#173247"), GOLD))
	result.set_stylebox("pressed", "OptionButton", _button_box(Color("#08151e"), GOLD_BRIGHT))
	result.set_stylebox("focus", "OptionButton", _focus_box())
	result.set_color("font_color", "OptionButton", ICE)
	result.set_color("font_hover_color", "OptionButton", Color.WHITE)
	result.set_font_size("font_size", "OptionButton", 18)

	result.set_stylebox("panel", "Panel", _panel_box())
	result.set_stylebox("panel", "PanelContainer", _panel_box())

	result.set_type_variation("NavigationPanel", "Panel")
	result.set_stylebox("panel", "NavigationPanel", _navigation_panel_box())
	result.set_type_variation("OverlayPanel", "Panel")
	result.set_stylebox("panel", "OverlayPanel", _overlay_panel_box())

	result.set_type_variation("NavButton", "Button")
	result.set_font_size("font_size", "NavButton", 25)
	result.set_color("font_color", "NavButton", ICE_DIM)
	result.set_color("font_hover_color", "NavButton", Color.WHITE)
	result.set_color("font_pressed_color", "NavButton", GOLD_BRIGHT)
	result.set_stylebox("normal", "NavButton", _nav_button_box(Color(0.04, 0.10, 0.14, 0.86), STEEL))
	result.set_stylebox("hover", "NavButton", _nav_button_box(Color(0.08, 0.22, 0.29, 0.96), GOLD))
	result.set_stylebox("pressed", "NavButton", _nav_button_box(Color(0.03, 0.08, 0.11, 1.0), GOLD_BRIGHT))
	result.set_stylebox("focus", "NavButton", _focus_box())

	result.set_type_variation("PrimaryButton", "Button")
	result.set_font_size("font_size", "PrimaryButton", 21)
	result.set_color("font_color", "PrimaryButton", Color("f0deb0"))
	result.set_stylebox("normal", "PrimaryButton", _button_box(Color("173447"), GOLD))
	result.set_stylebox("hover", "PrimaryButton", _button_box(Color("24516a"), GOLD_BRIGHT))
	result.set_stylebox("pressed", "PrimaryButton", _button_box(Color("0d2634"), GOLD_BRIGHT))
	return result


static func _button_box(fill: Color, border: Color) -> StyleBoxFlat:
	var box := StyleBoxFlat.new()
	box.bg_color = fill
	box.border_color = border
	box.set_border_width_all(1)
	box.corner_radius_top_left = 4
	box.corner_radius_top_right = 4
	box.corner_radius_bottom_left = 4
	box.corner_radius_bottom_right = 4
	box.content_margin_left = 24.0
	box.content_margin_right = 24.0
	box.content_margin_top = 12.0
	box.content_margin_bottom = 12.0
	box.shadow_color = Color(0.0, 0.0, 0.0, 0.55)
	box.shadow_size = 5
	return box


static func _focus_box() -> StyleBoxFlat:
	var box := StyleBoxFlat.new()
	box.bg_color = Color.TRANSPARENT
	box.border_color = GOLD_BRIGHT
	box.set_border_width_all(2)
	box.corner_radius_top_left = 5
	box.corner_radius_top_right = 5
	box.corner_radius_bottom_left = 5
	box.corner_radius_bottom_right = 5
	box.expand_margin_left = 2.0
	box.expand_margin_right = 2.0
	box.expand_margin_top = 2.0
	box.expand_margin_bottom = 2.0
	return box


static func _slider_track(fill: Color) -> StyleBoxFlat:
	var box := StyleBoxFlat.new()
	box.bg_color = fill
	box.border_color = STEEL
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
	box.border_color = STEEL_BRIGHT
	box.set_border_width_all(1)
	box.corner_radius_top_left = 7
	box.corner_radius_top_right = 7
	box.corner_radius_bottom_left = 7
	box.corner_radius_bottom_right = 7
	box.shadow_color = Color(0.0, 0.0, 0.0, 0.72)
	box.shadow_size = 18
	return box


static func _navigation_panel_box() -> StyleBoxFlat:
	var box := StyleBoxFlat.new()
	box.bg_color = Color(0.015, 0.035, 0.05, 0.94)
	box.border_color = Color(GOLD, 0.72)
	box.border_width_top = 2
	box.shadow_color = Color(0.0, 0.0, 0.0, 0.82)
	box.shadow_size = 22
	box.content_margin_top = 10.0
	return box


static func _overlay_panel_box() -> StyleBoxFlat:
	var box := _panel_box()
	box.bg_color = Color(0.025, 0.065, 0.09, 0.965)
	box.border_color = Color(GOLD, 0.78)
	box.set_border_width_all(2)
	box.corner_radius_top_left = 2
	box.corner_radius_top_right = 2
	box.corner_radius_bottom_left = 2
	box.corner_radius_bottom_right = 2
	return box


static func _nav_button_box(fill: Color, border: Color) -> StyleBoxFlat:
	var box := StyleBoxFlat.new()
	box.bg_color = fill
	box.border_color = border
	box.border_width_top = 1
	box.border_width_bottom = 1
	box.content_margin_top = 18.0
	box.content_margin_bottom = 18.0
	box.shadow_color = Color(0.0, 0.0, 0.0, 0.55)
	box.shadow_size = 6
	return box

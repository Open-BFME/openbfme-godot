## Lane UI-3: the launcher's look, made here from flat colours (no image, logo or font from the games). Dark steel panels, parchment text,
## a muted gold accent. The title uses a serif the system has (SystemFont: Georgia / Cambria on Windows, DejaVu or Noto Serif on Linux;
## Godot's own font when none is there); the body uses Godot's built-in font.
extends RefCounted

const BG := Color("101216")          # the window behind everything
const BG_TOP := Color("1b1f26")      # the backdrop's gradient, top
const PANEL := Color("181b21")       # cards
const PANEL_HI := Color("21252d")    # inputs, hovered rows
const EDGE := Color("343a44")        # steel borders
const EDGE_HI := Color("566070")
const TEXT := Color("e9e3d5")        # parchment white
const MUTED := Color("9c978c")
const PARCHMENT := Color("d8c7a0")   # titles
const GOLD := Color("c9a24a")        # the accent: Play, links, the progress bar
const GOLD_HI := Color("dfba62")
const GOLD_LO := Color("a3812f")
const INK := Color("17140e")         # text on gold
const GOOD := Color("86b273")
const BAD := Color("d4664c")
const WARN := Color("e0a84a")

const RADIUS := 6


static func title_font(weight: int = 600) -> Font:
	var f := SystemFont.new()
	f.font_names = PackedStringArray(["Georgia", "Cambria", "Constantia", "Noto Serif", "DejaVu Serif", "Liberation Serif", "serif"])
	f.font_weight = weight
	return f


static func box(bg: Color, edge: Color = Color(0, 0, 0, 0), border := 0, radius := RADIUS, pad := Vector2(0, 0)) -> StyleBoxFlat:
	var s := StyleBoxFlat.new()
	s.bg_color = bg
	s.border_color = edge
	s.set_border_width_all(border)
	s.set_corner_radius_all(radius)
	s.content_margin_left = pad.x
	s.content_margin_right = pad.x
	s.content_margin_top = pad.y
	s.content_margin_bottom = pad.y
	s.anti_aliasing = true
	return s


static func card() -> StyleBoxFlat:
	var s := box(PANEL, EDGE, 1, RADIUS + 2, Vector2(22, 18))
	s.shadow_color = Color(0, 0, 0, 0.35)
	s.shadow_size = 10
	s.shadow_offset = Vector2(0, 3)
	return s


## the window's theme: every Control below the launcher's root inherits it
static func make_theme() -> Theme:
	var t := Theme.new()
	t.default_font_size = 16
	t.set_color("font_color", "Label", TEXT)
	for k in ["Button", "OptionButton", "CheckBox", "CheckButton", "MenuButton"]:
		t.set_color("font_color", k, TEXT)
		t.set_color("font_hover_color", k, Color.WHITE)
		t.set_color("font_pressed_color", k, Color.WHITE)
		t.set_color("font_focus_color", k, TEXT)
		t.set_color("font_disabled_color", k, Color(TEXT, 0.35))
		t.set_color("icon_normal_color", k, TEXT)
		t.set_color("icon_hover_color", k, Color.WHITE)
		t.set_color("icon_disabled_color", k, Color(TEXT, 0.35))
		t.set_constant("h_separation", k, 8)
	_button(t, "Button", PANEL_HI, EDGE, Color("2a2f38"), EDGE_HI)
	_button(t, "OptionButton", PANEL_HI, EDGE, Color("2a2f38"), EDGE_HI)
	# the gold variant (Play, Update, Download): Button.theme_type_variation = "GoldButton"
	t.add_type("GoldButton")
	t.set_type_variation("GoldButton", "Button")
	_button(t, "GoldButton", GOLD, GOLD_LO, GOLD_HI, GOLD_HI)
	for c in ["font_color", "font_hover_color", "font_pressed_color", "font_focus_color", "icon_normal_color", "icon_hover_color", "icon_pressed_color", "icon_focus_color"]:
		t.set_color(c, "GoldButton", INK)
	t.set_color("font_disabled_color", "GoldButton", Color(INK, 0.5))
	t.set_color("icon_disabled_color", "GoldButton", Color(INK, 0.5))
	# flat icon buttons (the gear, close)
	t.add_type("FlatButton")
	t.set_type_variation("FlatButton", "Button")
	t.set_stylebox("normal", "FlatButton", box(Color(0, 0, 0, 0), Color(0, 0, 0, 0), 0, RADIUS, Vector2(8, 6)))
	t.set_stylebox("hover", "FlatButton", box(Color(1, 1, 1, 0.06), Color(0, 0, 0, 0), 0, RADIUS, Vector2(8, 6)))
	t.set_stylebox("pressed", "FlatButton", box(Color(1, 1, 1, 0.1), Color(0, 0, 0, 0), 0, RADIUS, Vector2(8, 6)))
	t.set_stylebox("disabled", "FlatButton", box(Color(0, 0, 0, 0), Color(0, 0, 0, 0), 0, RADIUS, Vector2(8, 6)))
	t.set_stylebox("focus", "FlatButton", box(Color(0, 0, 0, 0), GOLD_LO, 1, RADIUS, Vector2(8, 6)))
	# the channel switch: two toggle buttons in a ButtonGroup
	t.add_type("SegButton")
	t.set_type_variation("SegButton", "Button")
	t.set_stylebox("normal", "SegButton", box(Color(0, 0, 0, 0), Color(0, 0, 0, 0), 0, RADIUS - 2, Vector2(14, 5)))
	t.set_stylebox("hover", "SegButton", box(Color(1, 1, 1, 0.05), Color(0, 0, 0, 0), 0, RADIUS - 2, Vector2(14, 5)))
	t.set_stylebox("pressed", "SegButton", box(Color("3a3424"), GOLD_LO, 1, RADIUS - 2, Vector2(14, 5)))
	t.set_stylebox("hover_pressed", "SegButton", box(Color("3a3424"), GOLD, 1, RADIUS - 2, Vector2(14, 5)))
	t.set_stylebox("disabled", "SegButton", box(Color(0, 0, 0, 0), Color(0, 0, 0, 0), 0, RADIUS - 2, Vector2(14, 5)))
	t.set_stylebox("focus", "SegButton", box(Color(0, 0, 0, 0), GOLD_LO, 1, RADIUS - 2, Vector2(14, 5)))
	t.set_color("font_pressed_color", "SegButton", PARCHMENT)
	t.set_color("font_color", "SegButton", MUTED)
	t.set_color("font_hover_pressed_color", "SegButton", PARCHMENT)
	t.set_stylebox("panel", "PanelContainer", card())
	t.add_type("Inset")
	t.set_type_variation("Inset", "PanelContainer")
	t.set_stylebox("panel", "Inset", box(Color("121418"), EDGE, 1, RADIUS, Vector2(14, 12)))
	t.add_type("Pill")
	t.set_type_variation("Pill", "PanelContainer")
	t.set_stylebox("panel", "Pill", box(Color("22281f"), Color("3f5636"), 1, 12, Vector2(12, 3)))
	t.add_type("Banner")
	t.set_type_variation("Banner", "PanelContainer")
	t.set_stylebox("panel", "Banner", box(Color("2c2414"), Color("6b5426"), 1, RADIUS, Vector2(14, 8)))
	# labels
	t.add_type("Muted")
	t.set_type_variation("Muted", "Label")
	t.set_color("font_color", "Muted", MUTED)
	t.add_type("Heading")
	t.set_type_variation("Heading", "Label")
	t.set_color("font_color", "Heading", PARCHMENT)
	t.set_font("font", "Heading", title_font())
	t.set_font_size("font_size", "Heading", 24)
	t.add_type("Section")
	t.set_type_variation("Section", "Label")
	t.set_color("font_color", "Section", GOLD)
	t.set_font_size("font_size", "Section", 13)
	# progress
	t.set_stylebox("background", "ProgressBar", box(Color("0d0f12"), EDGE, 1, 4))
	t.set_stylebox("fill", "ProgressBar", box(GOLD, Color(0, 0, 0, 0), 0, 4))
	t.set_color("font_color", "ProgressBar", TEXT)
	# checkboxes (their own flat boxes: a theme's Button styles would otherwise frame them), lists, text
	for st in ["normal", "hover", "pressed", "disabled", "hover_pressed"]:
		t.set_stylebox(st, "CheckBox", box(Color(1, 1, 1, 0.04) if st.begins_with("hover") else Color(0, 0, 0, 0), Color(0, 0, 0, 0), 0, RADIUS, Vector2(6, 4)))
	t.set_stylebox("focus", "CheckBox", box(Color(0, 0, 0, 0), GOLD_LO, 1, RADIUS, Vector2(6, 4)))
	t.set_color("font_color", "CheckBox", TEXT)
	t.set_color("font_color", "LinkButton", GOLD)
	t.set_color("font_hover_color", "LinkButton", GOLD_HI)
	t.set_color("font_pressed_color", "LinkButton", GOLD_HI)
	t.set_color("font_focus_color", "LinkButton", GOLD)
	t.set_stylebox("panel", "ItemList", box(Color("121418"), EDGE, 1, RADIUS, Vector2(6, 6)))
	t.set_stylebox("selected", "ItemList", box(Color("3a3424"), Color(0, 0, 0, 0), 0, 4))
	t.set_stylebox("selected_focus", "ItemList", box(Color("3a3424"), GOLD_LO, 1, 4))
	t.set_stylebox("hovered", "ItemList", box(Color(1, 1, 1, 0.05), Color(0, 0, 0, 0), 0, 4))
	t.set_color("font_color", "ItemList", TEXT)
	t.set_color("font_selected_color", "ItemList", PARCHMENT)
	t.set_stylebox("normal", "RichTextLabel", box(Color(0, 0, 0, 0)))
	t.set_color("default_color", "RichTextLabel", TEXT)
	t.set_stylebox("panel", "PopupMenu", box(PANEL_HI, EDGE, 1, RADIUS, Vector2(6, 6)))
	t.set_stylebox("hover", "PopupMenu", box(Color("3a3424"), Color(0, 0, 0, 0), 0, 4))
	t.set_color("font_color", "PopupMenu", TEXT)
	t.set_color("font_hover_color", "PopupMenu", PARCHMENT)
	t.set_stylebox("panel", "TooltipPanel", box(PANEL_HI, EDGE, 1, 4, Vector2(8, 5)))
	t.set_color("font_color", "TooltipLabel", TEXT)
	t.set_stylebox("normal", "LineEdit", box(Color("121418"), EDGE, 1, RADIUS, Vector2(10, 6)))
	t.set_stylebox("focus", "LineEdit", box(Color(0, 0, 0, 0), GOLD_LO, 1, RADIUS))
	t.set_stylebox("read_only", "LineEdit", box(Color("121418"), EDGE, 1, RADIUS, Vector2(10, 6)))
	t.set_color("font_color", "LineEdit", TEXT)
	t.set_color("font_uneditable_color", "LineEdit", TEXT)
	t.set_stylebox("h_grabber", "HScrollBar", box(EDGE, Color(0, 0, 0, 0), 0, 3))
	t.set_stylebox("grabber", "VScrollBar", box(EDGE, Color(0, 0, 0, 0), 0, 3))
	t.set_stylebox("grabber_highlight", "VScrollBar", box(EDGE_HI, Color(0, 0, 0, 0), 0, 3))
	t.set_stylebox("grabber_pressed", "VScrollBar", box(EDGE_HI, Color(0, 0, 0, 0), 0, 3))
	t.set_stylebox("scroll", "VScrollBar", box(Color(0, 0, 0, 0.2), Color(0, 0, 0, 0), 0, 3))
	t.set_stylebox("separator", "HSeparator", box(EDGE, Color(0, 0, 0, 0), 0, 0))
	t.set_constant("separation", "HSeparator", 1)
	return t


static func _button(t: Theme, type: String, bg: Color, edge: Color, hover: Color, hover_edge: Color) -> void:
	var pad := Vector2(16, 8)
	t.set_stylebox("normal", type, box(bg, edge, 1, RADIUS, pad))
	t.set_stylebox("hover", type, box(hover, hover_edge, 1, RADIUS, pad))
	t.set_stylebox("pressed", type, box(bg.darkened(0.15), hover_edge, 1, RADIUS, pad))
	t.set_stylebox("disabled", type, box(Color(bg, 0.45), Color(edge, 0.45), 1, RADIUS, pad))
	t.set_stylebox("focus", type, box(Color(0, 0, 0, 0), GOLD_HI if type == "GoldButton" else GOLD_LO, 1, RADIUS, pad))

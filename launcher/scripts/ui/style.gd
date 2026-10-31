## Lane UI-5: the launcher's look, "Parchment & Steel" (the owner's pick of ten launcher concepts, 2026-10-10; the clickable prototype's
## design 1, whose CSS gives these colours, sizes and fonts). Dark steel and mist, parchment cards, a muted gold accent.
## Fonts: Cinzel (titles), Cormorant Garamond (the subtitle, parchment text) and Inter (everything else), free fonts under the SIL Open
## Font License 1.1, bundled in res://fonts with their licences (OFL-*.txt) and loaded from their own bytes (importer "keep": the pack
## carries the exact files tools/release/public_binaries.txt pins). Textures are made in shaders (scripts/ui/textures.gd): no image.
extends RefCounted

# the prototype's CSS variables (design "1 · Parchment & Steel")
const ACCENT := Color("f0d891")      # --ac: titles, icons, links
const ACCENT2 := Color("c9a54e")     # --ac2: section captions, table heads
const LINE := Color("4d3d1f")        # --ln: every border
const LINE2 := Color(1, 1, 1, 0.06)  # --ln2: the rules between rows
const TEXT := Color("ece2c9")        # --tx
const TEXT2 := Color("cdbf9f")       # --tx2
const TEXT3 := Color("9c917a")       # --tx3: muted
const PANEL := Color(12 / 255.0, 12 / 255.0, 14 / 255.0, 0.72)  # --pnl
const SELECTED := Color(201 / 255.0, 165 / 255.0, 78 / 255.0, 0.18)  # --sel
const HOVER := Color(1, 1, 1, 0.05)  # --hov
const BG := Color("0b0b0d")
const INK := Color("2a1b05")         # text on gold (the Play button)
const GOLD_TOP := Color("e9be56")    # the Play button's gradient
const GOLD_BOTTOM := Color("a77a22")
const FILL_LEFT := Color("8a6420")   # the progress bar's gradient
const FILL_RIGHT := Color("f0c45a")
const PARCH_INK := Color("2c2113")   # text on parchment
const PARCH_TITLE := Color("3a2810")
const PARCH_MUTED := Color("5b4422")
const GOOD := Color("8fd08a")
const GOOD_EDGE := Color("3e6b3a")
const WARN := Color("e0a84a")
const BAD := Color("d4664c")

# names kept from UI-3 (message_panel.gd and the icons use them)
const MUTED := TEXT3
const GOLD := ACCENT2
const PARCHMENT := ACCENT
const EDGE := LINE

const RADIUS := 10
const NAV_PAD := 12   # lane LAUNCHERUI-1: a rail entry's side padding: its icon, and the logo and footer above / below it, start here
const NAV_HEIGHT := 36
const NAV_GAP := 2
const SCREEN_GAP := 14  # the gap between a screen's heading, tabs and cards (every screen's root column; the layout test measures it)

static var _files := {}
static var _fonts := {}


## a bundled font file (res://fonts/<name>.ttf), read once
static func _file(name: String) -> FontFile:
	if not _files.has(name):
		var f := FontFile.new()
		var err := f.load_dynamic_font("res://fonts/%s.ttf" % name)
		if err != OK:
			push_error("launcher font %s did not load (%s)" % [name, error_string(err)])
		_files[name] = f
	return _files[name]


## `family` ("Cinzel", "CormorantGaramond", "CormorantGaramond-Italic", "Inter") at a weight (the variable fonts' wght axis), with extra
## space between letters (the CSS letter-spacing in pixels)
static func font(family: String, weight := 400, spacing := 0) -> Font:
	var key := "%s/%d/%d" % [family, weight, spacing]
	if not _fonts.has(key):
		var v := FontVariation.new()
		v.base_font = _file(family)
		v.variation_opentype = {TextServerManager.get_primary_interface().name_to_tag("wght"): weight}
		v.spacing_glyph = spacing
		_fonts[key] = v
	return _fonts[key]


static func title_font(weight := 700, spacing := 0) -> Font:
	return font("Cinzel", weight, spacing)


static func body_font(weight := 500) -> Font:
	return font("Inter", weight)


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


## .panel: the dark glass card with a gold-brown line
static func panel() -> StyleBoxFlat:
	return box(PANEL, LINE, 1, RADIUS, Vector2(20, 17))


## a label's font, size and colour in one call
static func text(l: Control, family: String, weight: int, size: int, color: Color, spacing := 0) -> void:
	l.add_theme_font_override("font", font(family, weight, spacing))
	l.add_theme_font_size_override("font_size", size)
	l.add_theme_color_override("font_color", color)


## the window's theme: every Control below the launcher's root inherits it
static func make_theme() -> Theme:
	var t := Theme.new()
	t.default_font = body_font(500)
	t.default_font_size = 15
	t.set_color("font_color", "Label", TEXT)
	for k in ["Button", "OptionButton", "CheckBox", "CheckButton", "MenuButton"]:
		t.set_font("font", k, body_font(600))
		t.set_font_size("font_size", k, 14)
		t.set_color("font_color", k, TEXT2)
		t.set_color("font_hover_color", k, TEXT)
		t.set_color("font_pressed_color", k, TEXT)
		t.set_color("font_hover_pressed_color", k, TEXT)
		t.set_color("font_focus_color", k, TEXT2)
		t.set_color("font_disabled_color", k, Color(TEXT2, 0.4))
		t.set_color("icon_normal_color", k, ACCENT)
		t.set_color("icon_hover_color", k, ACCENT)
		t.set_color("icon_pressed_color", k, ACCENT)
		t.set_color("icon_focus_color", k, ACCENT)
		t.set_color("icon_disabled_color", k, Color(ACCENT, 0.35))
		t.set_constant("h_separation", k, 10)
	# .btn: a dark button with a gold-brown line that lights up on hover
	_button(t, "Button", Color(0, 0, 0, 0.3), LINE, Color(0, 0, 0, 0.42), ACCENT, 8, Vector2(16, 9))
	_button(t, "OptionButton", Color(0, 0, 0, 0.35), LINE, Color(0, 0, 0, 0.45), ACCENT, 6, Vector2(10, 5))
	t.set_font_size("font_size", "OptionButton", 13)
	t.set_color("font_color", "OptionButton", TEXT)
	# the small gold button (Update to ..., Download): the Play button's gradient is drawn by scripts/ui/gold_button.gd
	t.add_type("GoldButton")
	t.set_type_variation("GoldButton", "Button")
	_button(t, "GoldButton", GOLD_TOP.darkened(0.08), GOLD_BOTTOM, GOLD_TOP, ACCENT, 8, Vector2(16, 9))
	for c in ["font_color", "font_hover_color", "font_pressed_color", "font_focus_color", "font_hover_pressed_color", "icon_normal_color",
			"icon_hover_color", "icon_pressed_color", "icon_focus_color"]:
		t.set_color(c, "GoldButton", INK)
	t.set_color("font_disabled_color", "GoldButton", Color(INK, 0.5))
	t.set_color("icon_disabled_color", "GoldButton", Color(INK, 0.5))
	# the left rail's entries (.nav button): icon in gold, the selected one on a gold tint
	t.add_type("NavButton")
	t.set_type_variation("NavButton", "Button")
	for st in ["normal", "disabled"]:
		t.set_stylebox(st, "NavButton", box(Color(0, 0, 0, 0), Color(0, 0, 0, 0), 0, 8, Vector2(NAV_PAD, 6)))
	t.set_stylebox("hover", "NavButton", box(HOVER, Color(0, 0, 0, 0), 0, 8, Vector2(NAV_PAD, 6)))
	t.set_stylebox("pressed", "NavButton", box(SELECTED, Color(0, 0, 0, 0), 0, 8, Vector2(NAV_PAD, 6)))
	t.set_stylebox("hover_pressed", "NavButton", box(SELECTED, Color(0, 0, 0, 0), 0, 8, Vector2(NAV_PAD, 6)))
	t.set_stylebox("focus", "NavButton", box(Color(0, 0, 0, 0), Color(ACCENT2, 0.6), 1, 8, Vector2(NAV_PAD, 6)))
	t.set_color("font_color", "NavButton", TEXT2)
	t.set_color("font_pressed_color", "NavButton", TEXT)
	t.set_color("font_hover_pressed_color", "NavButton", TEXT)
	t.set_font_size("font_size", "NavButton", 15)
	t.set_constant("h_separation", "NavButton", 12)
	# the Settings tabs (.stabs button) and other toggles in a ButtonGroup
	t.add_type("TabButton")
	t.set_type_variation("TabButton", "Button")
	for st in ["normal", "disabled"]:
		t.set_stylebox(st, "TabButton", box(Color(0, 0, 0, 0), Color(0, 0, 0, 0), 0, 7, Vector2(12, 7)))
	t.set_stylebox("hover", "TabButton", box(HOVER, Color(0, 0, 0, 0), 0, 7, Vector2(12, 7)))
	t.set_stylebox("pressed", "TabButton", box(SELECTED, Color(0, 0, 0, 0), 0, 7, Vector2(12, 7)))
	t.set_stylebox("hover_pressed", "TabButton", box(SELECTED, Color(0, 0, 0, 0), 0, 7, Vector2(12, 7)))
	t.set_stylebox("focus", "TabButton", box(Color(0, 0, 0, 0), Color(ACCENT2, 0.6), 1, 7, Vector2(12, 7)))
	t.set_color("font_pressed_color", "TabButton", TEXT)
	t.set_color("font_hover_pressed_color", "TabButton", TEXT)
	t.set_font_size("font_size", "TabButton", 14)
	# flat icon buttons (close)
	t.add_type("FlatButton")
	t.set_type_variation("FlatButton", "Button")
	t.set_stylebox("normal", "FlatButton", box(Color(0, 0, 0, 0), Color(0, 0, 0, 0), 0, 8, Vector2(8, 6)))
	t.set_stylebox("hover", "FlatButton", box(HOVER, Color(0, 0, 0, 0), 0, 8, Vector2(8, 6)))
	t.set_stylebox("pressed", "FlatButton", box(SELECTED, Color(0, 0, 0, 0), 0, 8, Vector2(8, 6)))
	t.set_stylebox("disabled", "FlatButton", box(Color(0, 0, 0, 0), Color(0, 0, 0, 0), 0, 8, Vector2(8, 6)))
	t.set_stylebox("focus", "FlatButton", box(Color(0, 0, 0, 0), Color(ACCENT2, 0.6), 1, 8, Vector2(8, 6)))
	t.set_stylebox("panel", "PanelContainer", panel())
	# .chip: a small rounded tag (status, channel, version); the "ok" one is green
	t.add_type("Chip")
	t.set_type_variation("Chip", "PanelContainer")
	t.set_stylebox("panel", "Chip", box(Color(0, 0, 0, 0.25), LINE, 1, 99, Vector2(11, 4)))
	t.add_type("Banner")
	t.set_type_variation("Banner", "PanelContainer")
	t.set_stylebox("panel", "Banner", box(Color("2c2414e6"), Color("6b5426"), 1, 8, Vector2(14, 8)))
	# labels: muted text, h1 / h2 (Cinzel in gold), h3 (the small spaced caption)
	t.add_type("Muted")
	t.set_type_variation("Muted", "Label")
	t.set_color("font_color", "Muted", TEXT3)
	t.set_font_size("font_size", "Muted", 13)
	t.set_font("font", "Muted", body_font(400))
	t.add_type("Heading")
	t.set_type_variation("Heading", "Label")
	t.set_color("font_color", "Heading", ACCENT)
	t.set_font("font", "Heading", title_font(700, 1))
	t.set_font_size("font_size", "Heading", 28)
	t.add_type("Section")
	t.set_type_variation("Section", "Label")
	t.set_color("font_color", "Section", ACCENT2)
	t.set_font("font", "Section", font("Inter", 600, 2))
	t.set_font_size("font_size", "Section", 12)
	# progress: the dark track with a gold gradient fill (scripts/ui/widgets.gd gold_fill)
	t.set_stylebox("background", "ProgressBar", box(Color(0, 0, 0, 0.45), LINE, 1, 6))
	t.set_stylebox("fill", "ProgressBar", box(FILL_RIGHT, Color(0, 0, 0, 0), 0, 6))
	t.set_color("font_color", "ProgressBar", TEXT)
	# toggles (.tog) and check boxes
	for k in ["CheckButton", "CheckBox"]:
		for st in ["normal", "hover", "pressed", "disabled", "hover_pressed"]:
			t.set_stylebox(st, k, _toggle_box(HOVER if st.begins_with("hover") else Color(0, 0, 0, 0), 0, Color(0, 0, 0, 0)))
		t.set_stylebox("focus", k, _toggle_box(Color(0, 0, 0, 0), 1, Color(ACCENT2, 0.6)))
		t.set_color("font_color", k, TEXT)
		t.set_font("font", k, body_font(500))
	var VectorIcon = load("res://scripts/ui/vector_icon.gd")
	t.set_icon("checked", "CheckButton", VectorIcon.make("toggle_on", 20))
	t.set_icon("unchecked", "CheckButton", VectorIcon.make("toggle_off", 20))
	t.set_icon("checked_disabled", "CheckButton", VectorIcon.make("toggle_on", 20))
	t.set_icon("unchecked_disabled", "CheckButton", VectorIcon.make("toggle_off", 20))
	t.set_color("font_color", "LinkButton", ACCENT)
	t.set_color("font_hover_color", "LinkButton", Color.WHITE)
	t.set_color("font_pressed_color", "LinkButton", Color.WHITE)
	t.set_color("font_focus_color", "LinkButton", ACCENT)
	t.set_font("font", "LinkButton", body_font(500))
	t.set_font_size("font_size", "LinkButton", 13)
	t.set_stylebox("panel", "ItemList", box(Color(0, 0, 0, 0.3), LINE, 1, 6, Vector2(6, 6)))
	t.set_stylebox("selected", "ItemList", box(SELECTED, Color(0, 0, 0, 0), 0, 4))
	t.set_stylebox("selected_focus", "ItemList", box(SELECTED, Color(ACCENT2, 0.6), 1, 4))
	t.set_stylebox("hovered", "ItemList", box(HOVER, Color(0, 0, 0, 0), 0, 4))
	t.set_stylebox("focus", "ItemList", box(Color(0, 0, 0, 0), Color(0, 0, 0, 0), 0, 4))
	t.set_color("font_color", "ItemList", TEXT)
	t.set_color("font_selected_color", "ItemList", ACCENT)
	t.set_font_size("font_size", "ItemList", 14)
	t.set_stylebox("panel", "PopupMenu", box(Color("16151a"), LINE, 1, 6, Vector2(6, 6)))
	t.set_stylebox("hover", "PopupMenu", box(SELECTED, Color(0, 0, 0, 0), 0, 4))
	t.set_color("font_color", "PopupMenu", TEXT)
	t.set_color("font_hover_color", "PopupMenu", ACCENT)
	t.set_stylebox("panel", "TooltipPanel", box(Color("16151a"), LINE, 1, 4, Vector2(8, 5)))
	t.set_color("font_color", "TooltipLabel", TEXT)
	t.set_stylebox("normal", "LineEdit", box(Color(0, 0, 0, 0.35), LINE, 1, 6, Vector2(10, 6)))
	t.set_stylebox("focus", "LineEdit", box(Color(0, 0, 0, 0), ACCENT2, 1, 6))
	t.set_stylebox("read_only", "LineEdit", box(Color(0, 0, 0, 0.35), LINE, 1, 6, Vector2(10, 6)))
	t.set_color("font_color", "LineEdit", TEXT)
	t.set_color("font_uneditable_color", "LineEdit", TEXT)
	t.set_stylebox("h_grabber", "HScrollBar", box(LINE, Color(0, 0, 0, 0), 0, 3))
	for k in ["grabber", "grabber_highlight", "grabber_pressed"]:
		t.set_stylebox(k, "VScrollBar", box(LINE if k == "grabber" else ACCENT2, Color(0, 0, 0, 0), 0, 3))
	t.set_stylebox("scroll", "VScrollBar", box(Color(0, 0, 0, 0.15), Color(0, 0, 0, 0), 0, 3))
	var rule := StyleBoxLine.new()
	rule.color = LINE2
	rule.thickness = 1
	t.set_stylebox("separator", "HSeparator", rule)
	t.set_constant("separation", "HSeparator", 1)
	return t


## a toggle's box: it hugs its drawing on the right, so the toggle's right edge is the row's (the dropdown above it ends there too)
static func _toggle_box(bg: Color, border: int, edge: Color) -> StyleBoxFlat:
	var b := box(bg, edge, border, 6, Vector2(4, 4))
	b.content_margin_right = 0
	return b


static func _button(t: Theme, type: String, bg: Color, edge: Color, hover: Color, hover_edge: Color, radius: int, pad: Vector2) -> void:
	t.set_stylebox("normal", type, box(bg, edge, 1, radius, pad))
	t.set_stylebox("hover", type, box(hover, hover_edge, 1, radius, pad))
	t.set_stylebox("pressed", type, box(hover, hover_edge, 1, radius, pad))
	t.set_stylebox("disabled", type, box(Color(bg, bg.a * 0.5), Color(edge, 0.45), 1, radius, pad))
	t.set_stylebox("focus", type, box(Color(0, 0, 0, 0), ACCENT if type == "GoldButton" else Color(ACCENT2, 0.7), 1, radius, pad))

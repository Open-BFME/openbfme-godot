## Lane UI-3: small builders the launcher's screens share (labels, icon buttons, the modal card); lane UI-5: the "Parchment & Steel"
## pieces (h2 / h3, panels, rows, chips, parchment cards, the gold progress bar).
extends RefCounted

const Style := preload("res://scripts/ui/style.gd")
const VectorIcon := preload("res://scripts/ui/vector_icon.gd")
const Textures := preload("res://scripts/ui/textures.gd")
const Ornament := preload("res://scripts/ui/ornament.gd")
const L10n := preload("res://scripts/l10n/l10n.gd")


static func label(text: String, variation := "", size := 0) -> Label:
	var l := Label.new()
	l.text = text
	if variation != "":
		l.theme_type_variation = variation
	if size > 0:
		l.add_theme_font_size_override("font_size", size)
	return l


## a one-line label that trims with an ellipsis and shows its whole text as the tooltip (scripts/ui/trim_label.gd)
static func trim_label(text: String, variation := "") -> Label:
	var l: Label = load("res://scripts/ui/trim_label.gd").new()
	l.text = text
	if variation != "":
		l.theme_type_variation = variation
	return l


static func wrap(text: String, variation := "") -> Label:
	var l := label(text, variation)
	l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	l.custom_minimum_size.x = 120
	return l


static func button(text: String, icon := "", variation := "", icon_px := 18) -> Button:
	var b := Button.new()
	b.text = text
	if icon != "":
		b.icon = VectorIcon.make(icon, icon_px)
	if variation != "":
		b.theme_type_variation = variation
	b.focus_mode = Control.FOCUS_ALL
	return b


static func icon_button(icon: String, tip: String, icon_px := 22) -> Button:
	var b := button("", icon, "FlatButton", icon_px)
	b.tooltip_text = tip
	return b


static func hbox(sep := 10) -> HBoxContainer:
	var h := HBoxContainer.new()
	h.add_theme_constant_override("separation", sep)
	return h


static func vbox(sep := 10) -> VBoxContainer:
	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", sep)
	return v


static func spacer() -> Control:
	var c := Control.new()
	c.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	return c


## The modal: a dimmed full-window layer with a centred card. {root, card, body (VBox), head (HBox), close (Button)}. The owner (a
## panel Control holding `root`) shows and hides itself; `close` only signals (Sol r1: it hid `root` for good, so a panel closed with X
## never opened again). `title` "" leaves out the header row.
static func modal(title: String, width: int) -> Dictionary:
	var root := Control.new()
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.mouse_filter = Control.MOUSE_FILTER_STOP
	var dim := ColorRect.new()
	dim.color = Color(0.02, 0.02, 0.03, 0.72)
	dim.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.add_child(dim)
	var center := CenterContainer.new()
	center.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.add_child(center)
	var card := PanelContainer.new()
	card.custom_minimum_size.x = width
	var look := Style.box(Color("141317f5"), Style.LINE, 1, Style.RADIUS, Vector2(24, 20))
	look.shadow_color = Color(0, 0, 0, 0.5)
	look.shadow_size = 18
	card.add_theme_stylebox_override("panel", look)
	center.add_child(card)
	var body := vbox(14)
	card.add_child(body)
	var out := {"root": root, "card": card, "body": body}
	if title != "":
		var head := hbox(8)
		head.add_child(label(title, "Heading"))
		head.get_child(0).size_flags_horizontal = Control.SIZE_EXPAND_FILL
		var close := icon_button("close", L10n.t("common.close"), 18)
		head.add_child(close)
		body.add_child(head)
		out["head"] = head
		out["close"] = close
	return out


## "Section" caption + its rows
static func section(parent: Control, caption: String) -> VBoxContainer:
	var v := vbox(8)
	v.add_child(label(caption.to_upper(), "Section"))
	parent.add_child(v)
	return v


## h2: a screen's title (Cinzel, gold)
static func h2(text: String) -> Label:
	var l := label(text, "Heading")
	return l


## h3: a panel's small spaced caption (Inter 600, upper case, --ac2)
static func h3(text: String) -> Label:
	return label(text.to_upper(), "Section")


## .panel with a VBox inside; {panel, box}
static func panel(sep := 8) -> Dictionary:
	var p := PanelContainer.new()
	var v := vbox(sep)
	p.add_child(v)
	return {"panel": p, "box": v}


## .row: a line of a panel, `left` and (optionally) a right-hand control, with a faint rule below unless `last`
static func row(left: Variant, right: Control = null, last := false) -> VBoxContainer:
	var outer := vbox(0)
	var h := hbox(10)
	h.custom_minimum_size.y = 36
	var l: Control = label(left) if left is String else left
	l.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	l.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	h.add_child(l)
	if right != null:
		right.size_flags_vertical = Control.SIZE_SHRINK_CENTER
		h.add_child(right)
	var pad := MarginContainer.new()
	pad.add_theme_constant_override("margin_top", 5)
	pad.add_theme_constant_override("margin_bottom", 5)
	pad.add_child(h)
	outer.add_child(pad)
	if not last:
		outer.add_child(HSeparator.new())
	return outer


## .chip: a small rounded tag; `color` tints its text and line (the green "ok" chip)
static func chip(text: String, color := Color(0, 0, 0, 0)) -> PanelContainer:
	var c := PanelContainer.new()
	c.theme_type_variation = "Chip"
	var l := label(text)
	Style.text(l, "Inter", 600, 12, Style.TEXT2 if color.a == 0 else color)
	c.add_child(l)
	if color.a > 0:
		c.add_theme_stylebox_override("panel", Style.box(Color(0, 0, 0, 0.25), color.darkened(0.45), 1, 99, Vector2(11, 4)))
	c.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	return c


## sets a chip's text and tint
static func set_chip(c: PanelContainer, text: String, color := Color(0, 0, 0, 0)) -> void:
	var l: Label = c.get_child(0)
	l.text = text
	l.add_theme_color_override("font_color", Style.TEXT2 if color.a == 0 else color)
	c.add_theme_stylebox_override("panel", Style.box(Color(0, 0, 0, 0.25), Style.LINE if color.a == 0 else color.darkened(0.45), 1, 99, Vector2(11, 4)))


## .parch: a parchment card with gold corners and a soft shadow; its content goes into the returned `box`. {card, box}
static func parchment(sep := 6, pad := Vector2(24, 22), corners := true) -> Dictionary:
	var card := PanelContainer.new()
	var shadow := Style.box(Color(0, 0, 0, 0), Color(0, 0, 0, 0), 0, 6)
	shadow.shadow_color = Color(0, 0, 0, 0.5)
	shadow.shadow_size = 14
	shadow.shadow_offset = Vector2(0, 6)
	card.add_theme_stylebox_override("panel", shadow)
	card.add_child(Textures.surface("parchment"))
	if corners:
		card.add_child(Ornament.make("corners"))
	var m := MarginContainer.new()
	for side in ["left", "right"]:
		m.add_theme_constant_override("margin_" + side, int(pad.x))
	for side in ["top", "bottom"]:
		m.add_theme_constant_override("margin_" + side, int(pad.y))
	card.add_child(m)
	var v := vbox(sep)
	m.add_child(v)
	return {"card": card, "box": v}


## text on parchment: "title" (Cinzel), "sub" (Cormorant italic, muted), "body" (Cormorant)
static func parch_label(text: String, kind: String, size := 0) -> Label:
	var l := label(text)
	match kind:
		"title":
			Style.text(l, "Cinzel", 700, size if size > 0 else 22, Style.PARCH_TITLE, 1)
		"sub":
			Style.text(l, "CormorantGaramond-Italic", 500, size if size > 0 else 16, Style.PARCH_MUTED)
		_:
			Style.text(l, "CormorantGaramond", 600, size if size > 0 else 18, Style.PARCH_INK)
			l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
			l.custom_minimum_size.x = 60
	return l


## a bullet item on parchment: "•" and the wrapped text
static func parch_item(text: String, size := 18) -> HBoxContainer:
	var h := hbox(8)
	var dot := parch_label("•", "body", size)
	dot.autowrap_mode = TextServer.AUTOWRAP_OFF
	dot.custom_minimum_size.x = 0
	dot.size_flags_vertical = Control.SIZE_SHRINK_BEGIN
	h.add_child(dot)
	var t := parch_label(text, "body", size)
	t.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	h.add_child(t)
	return h


## the gold progress bar (.track / .fill)
static func progress_bar() -> ProgressBar:
	var b := ProgressBar.new()
	b.show_percentage = false
	b.custom_minimum_size.y = 9
	var fill := StyleBoxTexture.new()
	var g := GradientTexture2D.new()
	g.gradient = Gradient.new()
	g.gradient.set_color(0, Style.FILL_LEFT)
	g.gradient.set_color(1, Style.FILL_RIGHT)
	g.width = 64
	g.height = 4
	fill.texture = g
	b.add_theme_stylebox_override("fill", fill)
	return b


## a vertical scroll area with the content inside; {scroll, box}
static func scroll(sep := 14) -> Dictionary:
	var s := ScrollContainer.new()
	s.follow_focus = true
	s.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	s.size_flags_vertical = Control.SIZE_EXPAND_FILL
	var v := vbox(sep)
	v.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	s.add_child(v)
	return {"scroll": s, "box": v}

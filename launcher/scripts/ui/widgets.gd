## Lane UI-3: small builders the launcher's screens share (labels, icon buttons, the modal card).
extends RefCounted

const Style := preload("res://scripts/ui/style.gd")
const VectorIcon := preload("res://scripts/ui/vector_icon.gd")


static func label(text: String, variation := "", size := 0) -> Label:
	var l := Label.new()
	l.text = text
	if variation != "":
		l.theme_type_variation = variation
	if size > 0:
		l.add_theme_font_size_override("font_size", size)
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
	center.add_child(card)
	var body := vbox(14)
	card.add_child(body)
	var out := {"root": root, "card": card, "body": body}
	if title != "":
		var head := hbox(8)
		head.add_child(label(title, "Heading"))
		head.get_child(0).size_flags_horizontal = Control.SIZE_EXPAND_FILL
		var close := icon_button("close", "Close", 18)
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

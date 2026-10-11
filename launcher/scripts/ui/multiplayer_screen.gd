## Lane UI-5: Multiplayer, the place of the coming server browser: the prototype's table, empty ("Online play is coming"), and a link to
## docs/ONLINE.md. No made-up games: online play is not built yet (docs/ROADMAP.md, "Online multiplayer direction"); LAN games are in
## the game itself.
extends Control

const W := preload("res://scripts/ui/widgets.gd")
const Style := preload("res://scripts/ui/style.gd")

const COLUMNS := ["Game", "Host", "Players", "Ping", "Region"]


func _init() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)


## repo: the GitHub <owner>/<name> whose docs/ONLINE.md the link opens
func build(repo: String) -> void:
	var v := W.vbox(14)
	v.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(v)
	v.add_child(W.h2("Multiplayer"))
	var p := W.panel(0)
	v.add_child(p.panel)
	var top := W.hbox(8)
	var what := W.wrap("Online play is coming: it will go through an OpenBFME relay, so players never see each other's addresses. " +
		"LAN games work today, in the game's Multiplayer menu.", "Muted")
	what.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	top.add_child(what)
	for t in ["Host game", "Join"]:
		var b := W.button(t)
		b.disabled = true
		b.tooltip_text = "Comes with online play"
		b.size_flags_vertical = Control.SIZE_SHRINK_CENTER
		top.add_child(b)
	p.box.add_child(top)
	var gap := Control.new()
	gap.custom_minimum_size.y = 12
	p.box.add_child(gap)
	var head := W.hbox(0)
	for i in COLUMNS.size():
		var l := W.label(String(COLUMNS[i]).to_upper(), "Section")
		l.custom_minimum_size = Vector2(0, 30)
		l.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		l.size_flags_stretch_ratio = 3.0 if i == 0 else 1.0
		head.add_child(l)
	p.box.add_child(head)
	var rule := ColorRect.new()
	rule.color = Style.LINE
	rule.custom_minimum_size.y = 1
	p.box.add_child(rule)
	var empty := W.label("Online play is coming")
	Style.text(empty, "CormorantGaramond-Italic", 500, 22, Style.TEXT2)
	empty.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	empty.custom_minimum_size.y = 110
	empty.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	p.box.add_child(empty)
	var link := LinkButton.new()
	link.text = "How online play will work (docs/ONLINE.md)"
	link.uri = "https://github.com/%s/blob/main/docs/ONLINE.md" % repo
	link.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
	p.box.add_child(link)

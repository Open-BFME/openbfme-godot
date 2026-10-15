## Lane UI-5: Mods. Edain is the first mod target and comes later (docs/ROADMAP.md, M10); the installed content is what the launcher knows
## for real: the two games and whether they were found. No mod is installed yet, and the screen says so.
extends Control

const W := preload("res://scripts/ui/widgets.gd")
const Style := preload("res://scripts/ui/style.gd")

var _rotwk: Label
var _bfme2: Label


func _init() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	var v := W.vbox(Style.SCREEN_GAP)
	v.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(v)
	v.add_child(W.h2("Mods"))
	var grid := W.hbox(16)
	v.add_child(grid)
	var edain := W.panel(6)
	edain.panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	edain.panel.size_flags_vertical = Control.SIZE_SHRINK_BEGIN
	grid.add_child(edain.panel)
	edain.box.add_child(W.h3("Edain"))
	var name_row := W.hbox(10)
	var name := W.label("Edain Mod")
	Style.text(name, "Inter", 600, 18, Style.TEXT)
	name_row.add_child(name)
	name_row.add_child(W.chip("coming later"))
	edain.box.add_child(name_row)
	edain.box.add_child(W.wrap("Mods will run unmodified with their own files, the way the original game loads them. Edain is the " +
		"first mod on the roadmap, after the base game.", "Muted"))
	var inst := W.panel(0)
	inst.panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	inst.panel.size_flags_vertical = Control.SIZE_SHRINK_BEGIN
	grid.add_child(inst.panel)
	inst.box.add_child(W.h3("Installed"))
	_rotwk = W.label("", "Muted")
	_bfme2 = W.label("", "Muted")
	inst.box.add_child(W.row("The Rise of the Witch-king 2.01", _rotwk))
	inst.box.add_child(W.row("Battle for Middle-earth II 1.06", _bfme2))
	inst.box.add_child(W.row("Mods", W.label("none installed", "Muted"), true))


func set_games(found: bool) -> void:
	for l in [_rotwk, _bfme2]:
		l.text = "found" if found else "not found"
		l.add_theme_color_override("font_color", Style.GOOD if found else Style.TEXT3)

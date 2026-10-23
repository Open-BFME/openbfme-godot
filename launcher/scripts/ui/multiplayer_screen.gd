## Multiplayer: the public games on this launcher's lobby. The list is only what the lobby returned (set_games). The screen
## never invents a game. Addresses stay behind the relay. A locked, started or full game is listed and is not joined from here
## (there is no password field: a locked game is joined inside the game). LAN games stay in the game's own Multiplayer menu.
extends Control

const W := preload("res://scripts/ui/widgets.gd")
const Style := preload("res://scripts/ui/style.gd")
const GameList := preload("res://scripts/core/game_list.gd")

const COLUMNS := ["Game", "Host", "Players", "Map"]

signal host_pressed
signal join_pressed(game_id: String)
signal refresh_pressed

var _host: Button
var _join: Button
var _rows: VBoxContainer
var _empty: Label
var _status: Label
var _games: Array = []
var _selected := -1
var _ready := false


func _init() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)


## repo: the GitHub <owner>/<name> whose docs/ONLINE.md the link opens
func build(repo: String) -> void:
	var v := W.vbox(Style.SCREEN_GAP)
	v.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(v)
	v.add_child(W.h2("Multiplayer"))
	var p := W.panel(0)
	p.panel.size_flags_vertical = Control.SIZE_EXPAND_FILL
	v.add_child(p.panel)
	var top := W.hbox(8)
	var what := W.wrap("Open games on this launcher's lobby. Addresses stay hidden behind the relay. A locked game is joined " +
		"from inside the game, and everyone in a match needs the same build. LAN games stay in the game's Multiplayer menu.", "Muted")
	what.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	what.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	top.add_child(what)
	_host = W.button("Host game")
	_join = W.button("Join")
	var refresh := W.button("Refresh")
	for b in [_host, _join, refresh]:
		b.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	_host.pressed.connect(func() -> void: host_pressed.emit())
	_join.pressed.connect(_on_join)
	refresh.pressed.connect(func() -> void: refresh_pressed.emit())
	top.add_child(_host)
	top.add_child(_join)
	top.add_child(refresh)
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
	_empty = W.label("No open games")
	Style.text(_empty, "CormorantGaramond-Italic", 500, 22, Style.TEXT2)
	_empty.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_empty.custom_minimum_size.y = 110
	_empty.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	p.box.add_child(_empty)
	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	p.box.add_child(scroll)
	_rows = W.vbox(0)
	_rows.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(_rows)
	_status = W.label("", "Muted")
	_status.visible = false
	p.box.add_child(_status)
	var link := LinkButton.new()
	link.text = "How online play will work (docs/ONLINE.md)"
	link.uri = "https://github.com/%s/blob/main/docs/ONLINE.md" % repo
	link.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
	p.box.add_child(link)
	set_games([])
	_apply_ready()


## Host and Join follow whether a version can be started. Join also needs a joinable row.
func set_ready(can_play: bool) -> void:
	_ready = can_play
	_apply_ready()


## replace the list. Only rows the lobby returned. Pass [] for none.
func set_games(games: Array) -> void:
	_games = games
	_selected = -1
	for c in _rows.get_children():
		_rows.remove_child(c)
		c.free()
	_empty.visible = games.is_empty()
	_rows.get_parent().visible = not games.is_empty()
	for i in games.size():
		var g: Dictionary = games[i]
		var row := Button.new()
		row.custom_minimum_size.y = 32
		row.theme_type_variation = "FlatButton"
		row.focus_mode = Control.FOCUS_ALL
		row.alignment = HORIZONTAL_ALIGNMENT_LEFT
		row.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		var box := W.hbox(0)
		box.mouse_filter = Control.MOUSE_FILTER_IGNORE
		box.set_anchors_preset(Control.PRESET_FULL_RECT)
		row.add_child(box)
		var cells := [GameList.display_name(g), str(g.host), "%d" % int(g.players), str(g.map)]
		for c in cells.size():
			var l := W.trim_label(cells[c])
			l.tooltip_text = cells[c]
			l.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			l.size_flags_stretch_ratio = 3.0 if c == 0 else 1.0
			l.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
			l.custom_minimum_size = Vector2(0, 32)
			box.add_child(l)
		var index := i
		row.pressed.connect(_select.bind(index))
		row.gui_input.connect(_on_row_input.bind(index))
		_rows.add_child(row)
	_apply_ready()


func set_status(text: String) -> void:
	_status.text = text
	_status.visible = text != ""


func _select(index: int) -> void:
	_selected = index
	for i in _rows.get_child_count():
		var row := _rows.get_child(i) as Button
		if row == null:
			continue
		row.modulate = Color(1, 1, 1, 1) if i != index else Color(1.15, 1.08, 0.85, 1)
	_apply_ready()


func _on_row_input(event: InputEvent, index: int) -> void:
	if event is InputEventMouseButton and event.pressed and event.double_click and event.button_index == MOUSE_BUTTON_LEFT:
		_select(index)
		if _can_join():
			join_pressed.emit(str(_games[index].id))


func _on_join() -> void:
	if _can_join():
		join_pressed.emit(str(_games[_selected].id))


func _can_join() -> bool:
	return _ready and _selected >= 0 and _selected < _games.size() and GameList.joinable(_games[_selected])


func _apply_ready() -> void:
	if _host == null:
		return
	_host.disabled = not _ready
	_join.disabled = not _can_join()
	_host.tooltip_text = "" if _ready else "Install a version before hosting"
	_join.tooltip_text = "" if _can_join() else "Select an open game"

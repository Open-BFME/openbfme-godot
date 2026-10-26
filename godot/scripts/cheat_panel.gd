## OpenBFME. GPL-3.0.
## Lane CHEAT-1: the open Cheat Mode's panel and lines (GameLogic/CheatMode.h; the command API: docs/CHEAT_MODE.md). NOT RETAIL: RotWK compiles its cheats
## out. Everyone in a Cheat Mode game agreed to it in the lobby (the game list and the Rules tab show it); everyone sees every action:
##   * the lines: every command, run or refused, is announced on every peer on the same logic frame (GameWorld.take_cheat_lines) and goes into the
##     game's own message log (InGameHudNode.add_player_message: RotWK's six message lines, docs/PLAYER-MESSAGES.md);
##   * the marker: "CHEAT MODE (OpenBFME): <host> is the host" under the message lines for the whole game, for every player;
##   * the panel: only the host's seat. The in-game menu (QuitMenu.apt, Esc) gets a CHEAT MODE button (AptQuitMenu: the menus' own look) that opens an
##     OpenBFME page over the menu (RotWK's combobox.wnd gadgets, Options.apt's frames and lettering): Resources, Units, Heroes, Map, Game. Each button
##     sends one MSG_OPENBFME_CHEAT through GameWorld.cheat_command; the lockstep command list carries it to every peer.
## --cheat-script=FRAME:STEP;... presses the panel's steps for unattended captures, FRAME counting the game's drawn frames (a paused skirmish's logic
## frame stands still while the menu is open). STEP: menu (Esc: the in-game menu opens / closes), open (the menu's CHEAT MODE button), tab:N,
## value:KEY:INDEX, press:KEY, close, shot:NAME (a screenshot in --screens).
extends Node

const MenuText := preload("res://scripts/menu_text.gd")

const TABS := ["Resources", "Units", "Heroes", "Map", "Game"]
const RESOURCES := [500, 1000, 2000, 5000, 10000, 50000]
const COMMAND_POINTS := [10, 25, 50, 100, 250]
const POWER_POINTS := [1, 5, 10, 25, 50]
const COUNTS := [1, 2, 3, 5, 10]
const LEVELS := [1, 2, 5, 10]

var world: Node
var shell: Object
var hud: Node
var marker: Label
var errors: Array = []
var info := {}
var sent: Array = []      # the commands this client sent: [ { command, ok, error } ] (reports, tests)
var announced: Array = [] # every line announced: [ { frame, text, executed } ]
var shown: Array = []     # the texts handed to the message log
var page_open := false

var _layer: CanvasLayer
var _tab := 0
var _choice := {}         # page key -> index (kept across tabs)
var _players: Array = []
var _units: Array = []
var _heroes: Array = []
var _menu_was_open := false
var _script: Array = []   # [ [frame, step] ]
var toggle_menu: Callable # game.gd: Esc (the in-game menu)
var shot: Callable        # game.gd: a named screenshot


## Returns { ok, errors, enabled }.
func setup(p_world: Node, p_shell: Object, p_hud: Node, script_arg := "") -> Dictionary:
	world = p_world
	shell = p_shell
	hud = p_hud
	name = "CheatMode"
	info = world.cheat_info()
	_layer = CanvasLayer.new()
	_layer.layer = 89 # under the end message (90) and every menu: the shell draws its movies above the game's overlays
	add_child(_layer)
	if info.get("enabled", false):
		marker = Label.new()
		marker.name = "CheatModeMarker"
		marker.set_meta("game_text", true)
		marker.mouse_filter = Control.MOUSE_FILTER_IGNORE
		marker.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
		marker.text = "CHEAT MODE (OpenBFME): %s is the host" % info.get("seat_name", "")
		_layer.add_child(marker)
		_place_marker()
		get_viewport().size_changed.connect(_place_marker)
	for part in script_arg.split(";", false):
		var colon := part.find(":")
		if colon > 0 and part.substr(0, colon).is_valid_int():
			_script.append([int(part.substr(0, colon)), part.substr(colon + 1)])
	return {"ok": errors.is_empty(), "errors": errors, "enabled": info.get("enabled", false)}


func _place_marker() -> void:
	if marker == null:
		return
	var size := get_viewport().get_visible_rect().size
	var h := MenuText.apply(marker, shell, size, 12.0, errors, Color(1.0, 0.85, 0.35, 0.95))
	marker.anchor_left = 0.0
	marker.anchor_right = 1.0
	# under the six message lines (InGameUI's MessagePosition, the y of seven lines down), at the same right edge
	var k := MenuText.scale_of(size)
	var layout: Dictionary = hud.get_player_message_layout() if hud != null and hud.has_method("get_player_message_layout") else {}
	var px: float = float(layout.get("x", -10))
	marker.offset_right = floorf(size.x * px / 1024.0 + 0.5) if px < 0 else -10.0
	marker.offset_top = floorf(size.y * (float(layout.get("y", 10)) + 7.0 * float(layout.get("point_size", 10)) * 1.5) / 768.0 + 0.5)
	marker.offset_bottom = marker.offset_top + h + 4.0 * k


## every frame of the live game (game.gd): the announced lines, the menu's button, the page's events, the scripted steps
func tick(logic_frame: int, quit_open: bool, drawn_frame: int) -> void:
	for l in world.take_cheat_lines():
		announced.append(l)
		var text: String = l.text
		if l.has("template"):
			text = text.replace(l.template, _label(l.get("label", ""), l.template)) # the unit's game text, as the player knows it
		shown.append(text)
		if hud != null and hud.has_method("add_player_message"):
			hud.add_player_message(text)
		print("CHEAT line: frame %d %s" % [int(l.frame), l.text])
	if not info.get("local_is_seat", false):
		return
	if quit_open and not _menu_was_open:
		_menu_was_open = true
		shell.quit_menu_cheat_button("CHEAT MODE")
	if not quit_open:
		page_open = false
	_menu_was_open = quit_open
	if quit_open:
		for e in shell.quit_menu_cheat_events():
			_on_event(e)
	while not _script.is_empty() and drawn_frame >= int(_script[0][0]):
		var step: String = _script.pop_front()[1]
		print("CHEAT script step at drawn frame %d (logic %d): %s" % [drawn_frame, logic_frame, step])
		_run_step(step)


func _run_step(step: String) -> void:
	var p := step.split(":")
	match p[0]:
		"menu":
			if toggle_menu.is_valid():
				toggle_menu.call()
		"shot":
			if shot.is_valid() and p.size() > 1:
				shot.call(p[1])
		"open":
			_open_page()
		"close":
			_close_page()
		"tab":
			_on_event({"kind": "tab", "key": "", "value": int(p[1])})
		"value":
			_on_event({"kind": "value", "key": p[1], "value": int(p[2])})
		"press":
			_on_event({"kind": "press", "key": p[1], "value": 0})


func _on_event(e: Dictionary) -> void:
	match e.kind:
		"entry":
			if e.key == "cheat_button":
				_open_page()
		"tab":
			_tab = clampi(int(e.value), 0, TABS.size() - 1)
			_show()
		"value":
			_choice[e.key] = int(e.value)
			if e.key == "player":
				_show() # another player's units and heroes
		"press":
			_press(e.key)


func _open_page() -> void:
	page_open = true
	_show()


func _close_page() -> void:
	page_open = false
	shell.quit_menu_cheat_page_close()


func _items(values: Array, fmt: String) -> Array:
	return values.map(func(v): return fmt % v)


func _label(label: String, fallback: String) -> String:
	if label.is_empty() or shell == null:
		return fallback
	var t: Dictionary = shell.fetch_text(label)
	return t.text if t.get("found", true) and not String(t.get("text", "")).is_empty() else fallback


func _show() -> void:
	_players = world.cheat_players()
	var player := _player()
	_units = world.cheat_units(player) if player >= 0 else []
	_heroes = world.cheat_heroes(player) if player >= 0 else []
	var player_items: Array = _players.map(func(p): return p.name)
	var rows: Array = []
	var choice := func(key: String, label: String, items: Array, help: String) -> Dictionary:
		var at: int = clampi(_choice.get(key, 0), 0, maxi(0, items.size() - 1))
		_choice[key] = at
		return {"kind": "choice", "key": key, "label": label, "items": items, "value": at, "help": help, "enabled": not items.is_empty()}
	var button := func(key: String, label: String, text: String, help: String, enabled := true) -> Dictionary:
		return {"kind": "button", "key": key, "label": label, "value_text": text, "help": help, "enabled": enabled}
	match _tab:
		0:
			rows.append(choice.call("player", "Player", player_items, "The player who gets it"))
			rows.append(choice.call("resources", "Resources", _items(RESOURCES, "%d"), "How much to give"))
			rows.append(button.call("give_resources", "", "GIVE RESOURCES", "Every player sees: <host> gave N resources to <player>"))
			rows.append(choice.call("command_points", "Command points", _items(COMMAND_POINTS, "+%d"), "Raises the player's command point limit"))
			rows.append(button.call("give_command_points", "", "GIVE COMMAND POINTS", "Raises the player's command point limit"))
			rows.append(choice.call("power_points", "Power points", _items(POWER_POINTS, "+%d"), "Points for the spell book"))
			rows.append(button.call("give_power_points", "", "GIVE POWER POINTS", "Points for the spell book"))
		1:
			rows.append(choice.call("player", "Player", player_items, "The player who gets the units"))
			var unit_items: Array = _units.map(func(u): return ("Hero: " if u.hero else "") + _label(u.label, u.template))
			rows.append(choice.call("unit", "Unit or hero", unit_items, "The units and heroes of the player's faction"))
			rows.append(choice.call("count", "How many", _items(COUNTS, "%d"), "1 to 10 at once"))
			rows.append(button.call("spawn", "", "SPAWN", "At the centre of your view", not unit_items.is_empty()))
		2:
			rows.append(choice.call("player", "Player", player_items, "Whose hero"))
			var hero_items: Array = _heroes.map(func(h): return "%s (level %d)" % [_label(h.label, h.template), int(h.level)])
			rows.append(choice.call("hero", "Hero", hero_items, "The player's living heroes"))
			rows.append(choice.call("levels", "Levels", _items(LEVELS, "+%d"), "How many levels"))
			rows.append(button.call("level_up", "", "LEVEL UP", "The hero gains the levels", not hero_items.is_empty()))
		3:
			rows.append(choice.call("player", "Player", player_items, "Whose map"))
			rows.append(button.call("reveal_map", "", "REVEAL MAP", "The player sees the whole map until it is hidden again"))
			rows.append(button.call("hide_map", "", "HIDE MAP AGAIN", "Undoes the reveal"))
		4:
			rows.append(choice.call("player", "Winner", player_items, "This player's alliance wins"))
			rows.append(button.call("end_game", "", "END GAME", "Every player outside the winner's alliance is defeated"))
			rows.append(button.call("void_game", "", "VOID GAME", "The game ends with no winner"))
	rows.append({"kind": "section", "label": ""})
	rows.append(button.call("back", "", "BACK TO MENU", "Close the Cheat Mode panel"))
	var spec := {
		"tabs_header": "CHEAT MODE",
		"tabs": TABS,
		"tab": _tab,
		"panel_header": TABS[_tab].to_upper(),
		"note": "Cheat Mode (OpenBFME): every action is shown to every player. This game never counts in statistics.",
		"rows": rows,
	}
	var r: Dictionary = shell.quit_menu_cheat_page_show(spec)
	if not r.get("ok", false):
		errors.append("the Cheat Mode page: " + str(r.get("error", "")))


func _player() -> int:
	if _players.is_empty():
		return -1
	return int(_players[clampi(_choice.get("player", 0), 0, _players.size() - 1)].index)


func _send(command: Dictionary) -> void:
	var r: Dictionary = world.cheat_command(command)
	sent.append({"command": command, "ok": r.get("ok", false), "error": r.get("error", "")})
	print("CHEAT command: %s -> %s" % [JSON.stringify(command), JSON.stringify(r)])
	if not r.get("ok", false):
		errors.append("cheat_command: " + str(r.get("error", "")))


func _press(key: String) -> void:
	var target := _player()
	match key:
		"back":
			_close_page()
		"give_resources":
			_send({"command": "add_resources", "target": target, "value": RESOURCES[_choice.get("resources", 0)]})
		"give_command_points":
			_send({"command": "add_command_points", "target": target, "value": COMMAND_POINTS[_choice.get("command_points", 0)]})
		"give_power_points":
			_send({"command": "add_power_points", "target": target, "value": POWER_POINTS[_choice.get("power_points", 0)]})
		"spawn":
			if _units.is_empty():
				return
			var at := Vector2.ZERO
			if hud != null and hud.has_method("get_camera"):
				at = hud.get_camera().get("position", Vector2.ZERO)
			var unit: Dictionary = _units[clampi(_choice.get("unit", 0), 0, _units.size() - 1)]
			_send({"command": "spawn", "target": target, "value": COUNTS[_choice.get("count", 0)], "template": unit.template, "x": at.x, "y": at.y})
		"level_up":
			if _heroes.is_empty():
				return
			var hero: Dictionary = _heroes[clampi(_choice.get("hero", 0), 0, _heroes.size() - 1)]
			_send({"command": "level_up", "target": target, "value": LEVELS[_choice.get("levels", 0)], "object": hero.id})
		"reveal_map":
			_send({"command": "reveal_map", "target": target, "value": 1})
		"hide_map":
			_send({"command": "reveal_map", "target": target, "value": 0})
		"end_game":
			_send({"command": "end_game", "target": target, "value": 0})
		"void_game":
			_send({"command": "void_game", "target": -1, "value": 0})


func report() -> Dictionary:
	return {"info": world.cheat_info(), "lines": shown, "announced": announced.size(), "sent": sent,
		"errors": errors, "page_open": page_open}

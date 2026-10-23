## Headless test of the ping wheel (not retail): scripts/ping_wheel.gd, scripts/ping_board.gd, scripts/ping_icon.gd.
##  * the wheel and the board on their own (no game files): the key opens the wheel at the pointer, the pointer's direction picks the kind, letting go
##    places the ping where the wheel opened, Esc / a right click close it without one, a click's release goes nowhere, the rate limit, the key choices
##    and the options, a mod's CommandMap record keeps the key, the markers go after their time;
##  * a skirmish (scripts/ping_wheel_player.gd, needs the games): the wheel over the real HUD, the ping on the ground and the minimap, Space, the menus, the
##    options; then two runs of the same game, one with pings placed, whose logic frame hashes must match (pings never reach the simulation).
##
##   godot --headless --path godot --script res://tests/pingwheel_test.gd
##
## The unit part runs without the games; the game runs need ROTWK_INSTALL and BFME2_INSTALL (a SKIP line without). Exit codes: 0 pass, 1 fail.
extends SceneTree

const PingWheel := preload("res://scripts/ping_wheel.gd")
const PingBoard := preload("res://scripts/ping_board.gd")
const GameOptions := preload("res://scripts/game_options.gd")

var _failures := 0
var _wheel: CanvasLayer
var _board: Node3D
var _pinged: Array = []


func _initialize() -> void:
	var code: int = await _run()
	print("PINGWHEEL TEST %s" % ["PASS" if code == 0 else "FAIL"])
	quit(code)


func _check(ok: bool, what: String) -> void:
	print("  ", "ok   " if ok else "FAIL ", what)
	if not ok:
		_failures += 1


func _run() -> int:
	await _unit_checks()
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: the game runs (set ROTWK_INSTALL and BFME2_INSTALL)")
		return 0 if _failures == 0 else 1
	if not ClassDB.class_exists("InGameHudNode"):
		_check(false, "the openbfme extension is loaded")
		return 1
	var common := ["--auto", "--faction=FactionMen", "--seed=7"]
	_game_run(common + ["--pingwheel=wheel,tap,kinds,minimap,space,menus,options"], 7, "the ping wheel in a skirmish")
	var quiet := _hash_run(common, "quiet")
	var pinged := _hash_run(common, "pings")
	var same := 0
	var differ := []
	for f in quiet:
		if pinged.has(f):
			if quiet[f] == pinged[f]:
				same += 1
			else:
				differ.append(f)
	_check(same >= 10 and differ.is_empty(), "the logic's frame hashes with pings placed are those without (%d frames compared, %d differ: %s)" % [same, differ.size(), differ.slice(0, 5)])
	return 0 if _failures == 0 else 1


# ---- the wheel on its own ----------------------------------------------------------------------------------------------------------------------------

func _key(code: Key, down: bool, alt := false, echo := false) -> InputEventKey:
	var e := InputEventKey.new()
	e.keycode = code
	e.physical_keycode = code
	e.pressed = down
	e.alt_pressed = alt
	e.echo = echo
	return e


func _push(e: InputEvent) -> bool:
	root.push_input(e)
	return root.is_input_handled()


func _move(p: Vector2) -> void:
	var m := InputEventMouseMotion.new()
	m.position = p
	_push(m)


func _button(index: MouseButton, down: bool, at: Vector2) -> bool:
	var b := InputEventMouseButton.new()
	b.button_index = index
	b.pressed = down
	b.position = at
	return _push(b)


## Alt+G at `at`, the pointer to `to`, G and Alt let go; the pings the wheel emitted meanwhile
func _ping(at: Vector2, to: Vector2) -> Array:
	_pinged.clear()
	_move(at)
	_push(_key(KEY_ALT, true, true))
	_push(_key(KEY_G, true, true))
	_move(to)
	_push(_key(KEY_G, false, true))
	_push(_key(KEY_ALT, false, false))
	return _pinged.duplicate()


func _unit_checks() -> void:
	_board = PingBoard.new()
	root.add_child(_board)
	var r: Dictionary = _board.setup(null, null, null, null, 1)
	_check(r.ok, "the board sets up without a game")
	_wheel = PingWheel.new()
	_wheel.board = _board
	_wheel.pick = func(p: Vector2) -> Vector2: return Vector2(-1e9, -1e9) if p.x < 10 else p * 2.0 # the "ground" is the pixel times two, none at the left edge
	_wheel.pinged.connect(func(kind: int, xy: Vector2, accepted: bool) -> void: _pinged.append([kind, xy, accepted]))
	root.add_child(_wheel)
	await process_frame
	_wheel.configure(true, "altg")
	var R: float = _wheel._radius()
	var c := Vector2(600, 400)

	# G alone is the Hold Ground stance: the wheel lets it through
	_move(c)
	var handled := _push(_key(KEY_G, true))
	_push(_key(KEY_G, false))
	_check(not _wheel.is_open() and not handled, "G without Alt opens nothing and goes on to the game")
	# Alt+G opens the wheel at the pointer and takes the key
	_push(_key(KEY_ALT, true, true))
	handled = _push(_key(KEY_G, true, true))
	_check(_wheel.is_open() and handled and _wheel._centre == c, "Alt+G opens the wheel at the pointer and takes the key")
	_check(_wheel.selection() == 0, "the wheel opens on the middle (Look here)")
	_check(_push(_key(KEY_G, true, true, true)) and _wheel.is_open(), "the key's repeat is the wheel's")
	await process_frame
	await process_frame # it draws (headless: the dummy renderer)
	var dirs := {"attack": Vector2(0, -1), "reinforce": Vector2(1, 0), "danger": Vector2(0, 1), "assist": Vector2(-1, 0)}
	for id in dirs:
		_move(c + dirs[id] * R * 0.7)
		_check(_wheel.selection() == PingBoard.kind_index(id), "the pointer %s selects %s" % [dirs[id], id])
	_move(c + Vector2(R * 0.2, -R * 0.1))
	_check(_wheel.selection() == 0, "inside the middle circle: Look here")
	_move(c + Vector2(R * 0.9, -R * 3))
	_check(_wheel.selection() == PingBoard.kind_index("attack"), "far out (up and a little right) still picks the nearest sector")
	_pinged.clear()
	_push(_key(KEY_G, false, false)) # Alt went up first: G's release still closes the wheel
	_push(_key(KEY_ALT, false))
	_check(not _wheel.is_open() and _pinged.size() == 1 and _pinged[0] == [PingBoard.kind_index("attack"), c * 2.0, true],
		"letting go of G places the selected ping where the wheel opened (%s)" % [_pinged])
	_check(_board.shown_total == 1 and _board.marker_count() == 1 and _board.last.player == 1, "the board shows the local player's ping")

	# the rate limit: three in five seconds
	var p2 := _ping(c, c + Vector2(R * 0.7, 0))
	var p3 := _ping(c, c)
	var p4 := _ping(c, c + Vector2(0, R * 0.7))
	_check(p2.size() == 1 and p2[0][2] and p3.size() == 1 and p3[0][2] and p3[0][0] == 0, "the second and third pings are placed (a tap is Look here)")
	_check(p4.size() == 1 and not p4[0][2] and _board.refused_total == 1 and _board.shown_total == 3, "the fourth ping within five seconds is refused")
	var now := Time.get_ticks_msec()
	_check(_board.allowed(1, now + PingBoard.BURST_WINDOW_MS + 1), "five seconds later pings are allowed again")
	_check(not _board.request_ping(99, Vector2.ZERO), "an unknown kind is refused")
	_board._recent.clear()

	# Esc and a right click close the wheel without a ping; a click's release goes nowhere
	_pinged.clear()
	_move(c)
	_push(_key(KEY_ALT, true, true))
	_push(_key(KEY_G, true, true))
	handled = _push(_key(KEY_ESCAPE, true))
	_check(handled and not _wheel.is_open(), "Esc closes the wheel and goes no further (no quit menu)")
	_check(_push(_key(KEY_G, true, false, true)), "after Esc the key's repeat is still the wheel's")
	_push(_key(KEY_ALT, false))
	_check(_push(_key(KEY_G, false)), "after Esc, G released after Alt is still the wheel's (no Hold Ground hotkey)")
	_check(not _push(_key(KEY_G, false)), "... and only that release")
	_push(_key(KEY_ALT, true, true))
	_push(_key(KEY_G, true, true))
	_move(c + Vector2(0, R * 0.7))
	_check(_button(MOUSE_BUTTON_RIGHT, true, c) and not _wheel.is_open(), "a right click closes the wheel")
	_check(_button(MOUSE_BUTTON_RIGHT, false, c), "... and its release is the wheel's (no order)")
	_check(_wheel._swallow_button == -1, "the wheel takes that one release only")
	_push(_key(KEY_G, false))
	_push(_key(KEY_ALT, false))
	_check(_pinged.is_empty(), "no ping from Esc or the right click")
	_move(c)
	_push(_key(KEY_ALT, true, true))
	_push(_key(KEY_G, true, true))
	_move(c + Vector2(-R * 0.7, 0))
	_check(_button(MOUSE_BUTTON_LEFT, true, c) and _button(MOUSE_BUTTON_LEFT, false, c), "a left click places the ping, its release is the wheel's")
	_push(_key(KEY_G, false))
	_push(_key(KEY_ALT, false))
	_check(_pinged.size() == 1 and _pinged[0][0] == PingBoard.kind_index("assist"), "the left click placed the selected ping (%s)" % [_pinged])
	_board._recent.clear()

	# no ground under the pointer: the key is taken, the wheel stays shut
	_move(Vector2(5, 400))
	_push(_key(KEY_ALT, true, true))
	handled = _push(_key(KEY_G, true, true))
	_check(handled and not _wheel.is_open(), "over a pixel with no ground the wheel stays shut")
	_push(_key(KEY_G, false))
	_push(_key(KEY_ALT, false))
	_board._recent.clear()

	# the default key: G held. A tap is the game's G (handed back when it is let go), a hold opens the wheel
	_wheel.configure(true, "g")
	_check(_wheel.enabled and _wheel.binding == PingWheel.DEFAULT_BINDING, "the default key is the held G")
	_wheel.hold_ms = 60
	_move(c)
	var taps: int = _wheel.taps_passed
	handled = _push(_key(KEY_G, true))
	_check(handled and _wheel.is_pending() and not _wheel.is_open(), "G goes down: the wheel waits (the press is held back)")
	_check(_push(_key(KEY_G, true, false, true)) and _wheel.is_pending(), "... its repeat too")
	handled = _push(_key(KEY_G, false))
	_check(handled and not _wheel.is_pending() and not _wheel.is_open() and _wheel.taps_passed == taps + 1, "a quick tap of G is handed to the game (press and release)")
	var replay := _key(KEY_G, true)
	replay.set_meta(PingWheel.REPLAY_META, true)
	_check(not _push(replay) and not _wheel.is_pending(), "the handed-back G passes the wheel")
	_push(_key(KEY_G, false))
	await process_frame
	_pinged.clear()
	_push(_key(KEY_G, true))
	await create_timer(0.12).timeout
	await process_frame
	_check(_wheel.is_open() and _wheel._centre == c, "G held: the wheel opens where G went down")
	_move(c + Vector2(0, -R * 0.7))
	_check(_push(_key(KEY_G, false)) and not _wheel.is_open() and _pinged.size() == 1 and _pinged[0][0] == PingBoard.kind_index("attack"),
		"letting go of the held G places the ping (%s)" % [_pinged])
	_board._recent.clear()
	_move(c)
	_pinged.clear()
	_push(_key(KEY_G, true))
	_move(c + Vector2(R * 0.7, 0)) # a flick towards "Reinforcements" before the wheel shows
	await create_timer(0.12).timeout
	await process_frame
	_check(_wheel.is_open() and _wheel._centre == c and _wheel.selection() == PingBoard.kind_index("reinforce"), "a flick during the hold time selects that choice")
	_push(_key(KEY_G, false))
	_check(_pinged.size() == 1 and _pinged[0][0] == PingBoard.kind_index("reinforce"), "... and letting go places it (%s)" % [_pinged])
	_move(c)
	taps = _wheel.taps_passed
	_push(_key(KEY_G, true))
	_check(not _push(_key(KEY_S, true)) and not _wheel.is_pending() and _wheel.taps_passed == taps + 1, "another key before the hold time: the G is the game's, and so is that key")
	_push(_key(KEY_S, false))
	_check(not _push(_key(KEY_G, false)), "... and so is G's release")
	_move(Vector2(5, 400))
	_push(_key(KEY_G, true))
	await create_timer(0.12).timeout
	await process_frame
	_check(not _wheel.is_open() and not _wheel.is_pending() and not _push(_key(KEY_G, false)), "G held over no ground (the HUD's buttons): the G is the game's")
	_move(c)
	_check(not _push(_key(KEY_G, true, true)) and not _wheel.is_pending(), "Alt+G with the held G is the game's")
	_push(_key(KEY_G, false, true))
	_wheel.commandmap = [{"name": "STANCE_HOLDGROUND", "key": "KEY_G", "alt": false}]
	_wheel.configure(true, "g")
	_check(_wheel.enabled, "the held G shares G with the CommandMap's own G (a tap is the stance)")
	_wheel.commandmap = []

	# a menu has the input
	var allowed := [false]
	_wheel.can_open = func() -> bool: return allowed[0]
	_check(_ping(c, c).is_empty() and not _wheel.is_open(), "the wheel stays shut while the game says no (a menu is up)")
	_wheel.can_open = Callable()

	# the options: off, the other keys, a mod's CommandMap record
	_wheel.configure(false, "altg")
	_check(_ping(c, c).is_empty() and not _wheel.enabled, "OpenBFMEPingWheel = no: Alt+G opens nothing")
	_wheel.configure(true, "j")
	_move(c)
	_push(_key(KEY_J, true))
	_check(_wheel.is_open(), "OpenBFMEPingKey = j: J opens the wheel")
	_push(_key(KEY_J, false))
	_check(not _wheel.is_open(), "... and its release closes it")
	_board._recent.clear()
	_wheel.configure(true, "mouse4")
	_check(_button(MOUSE_BUTTON_XBUTTON1, true, c) and _wheel.is_open(), "OpenBFMEPingKey = mouse4: the back button opens the wheel")
	_check(_button(MOUSE_BUTTON_XBUTTON1, false, c) and not _wheel.is_open(), "... and its release places the ping")
	_wheel.configure(true, "nonsense")
	_check(_wheel.binding == "mouse4", "an unknown key choice keeps the key in effect")
	_wheel.commandmap = [{"name": "SOME_MOD_COMMAND", "key": "KEY_G", "alt": true}]
	_wheel.configure(true, "altg")
	_check(not _wheel.enabled and _wheel.blocked_by == "SOME_MOD_COMMAND", "a CommandMap record on Alt+G keeps the key (the wheel is off)")
	_wheel.commandmap = [{"name": "STANCE_HOLDGROUND", "key": "KEY_G", "alt": false}]
	_wheel.configure(true, "altg")
	_check(_wheel.enabled, "G alone in the CommandMap (the retail stance) leaves Alt+G to the wheel")
	_wheel.commandmap = [{"name": "SOME_MOD_COMMAND", "key": "KEY_J", "ctrl": true}]
	_wheel.configure(true, "j")
	_check(not _wheel.enabled, "any CommandMap record on J keeps it")
	_wheel.commandmap = []
	_wheel.configure(true, "altg")
	_check(GameOptions.EXTRAS.OpenBFMEPingWheel.default == "yes" and GameOptions.EXTRAS.OpenBFMEPingKey.default == PingWheel.DEFAULT_BINDING,
		"the options' defaults: on, Alt+G")
	_check(Array(GameOptions.EXTRAS.OpenBFMEPingKey.choices) == PingWheel.BINDINGS.keys(), "the key option's choices are the wheel's bindings")

	# the markers go with the radar event (4 s)
	for m in _board._markers:
		m.age = 0.0 # the waits above let them age already
	var before: int = _board.marker_count()
	_board._process(PingBoard.PING_SECONDS - 0.1)
	_check(_board.marker_count() == before and before > 0, "the markers stay for their time (%d)" % before)
	_board._process(0.2)
	_check(_board.marker_count() == 0, "then they go")
	_wheel.queue_free()
	_board.queue_free()
	await process_frame


# ---- the games ---------------------------------------------------------------------------------------------------------------------------------------

func _game_run(args: Array, min_ok: int, what: String) -> String:
	var output: Array = []
	var project := ProjectSettings.globalize_path("res://")
	var t0 := Time.get_ticks_msec()
	var code := OS.execute(OS.get_executable_path(), PackedStringArray(["--headless", "--path", project, "--"] + args), output, true)
	var text: String = "".join(output)
	var result := ""
	var oks := 0
	for line in text.split("\n"):
		if line.begins_with("PINGWHEEL ") or line.begins_with("GAME ping"):
			print("    ", line.substr(0, 300))
			if line.ends_with(": ok"):
				oks += 1
			if line.begins_with("PINGWHEEL RESULT"):
				result = line
		elif line.contains("SCRIPT ERROR") or line.contains("Parse Error"):
			print("    ", line)
	_check(code == 0 and result.ends_with(" 0 failed") and oks >= min_ok,
		"%s (%s): exit %d, %s, %d ok (at least %d), %.0f s" % [what, " ".join(args), code, result if not result.is_empty() else "NO RESULT", oks, min_ok, (Time.get_ticks_msec() - t0) / 1000.0])
	return text


## frame -> hash of a run with --pingwheel=hash
func _hash_run(args: Array, mode: String) -> Dictionary:
	OS.set_environment("PINGWHEEL_HASH", mode)
	var text := _game_run(args + ["--pingwheel=hash"], 1, "the frame hashes, " + mode)
	OS.unset_environment("PINGWHEEL_HASH")
	var out := {}
	for line in text.split("\n"):
		var at := line.find("PINGWHEEL hash %s" % mode)
		if at < 0:
			continue
		var parsed = JSON.parse_string(line.substr(line.find(": ", at) + 2))
		if parsed is Array:
			for pair in parsed:
				out[int(pair[0])] = int(pair[1])
	return out

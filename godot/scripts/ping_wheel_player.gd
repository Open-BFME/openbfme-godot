## The ping wheel (scripts/ping_wheel.gd, not retail) in a running skirmish, driven by input events as a player gives them (Input.parse_input_event: the
## path a real keyboard and mouse take, so the wheel and the HUD see them in their real order). Every scenario prints "PINGWHEEL <name>: ok" or
## "PINGWHEEL <name>: FAIL <why>"; "PINGWHEEL RESULT: <n> ok, <m> failed" ends the run. game.gd runs it with --auto --pingwheel=<a,b,...>:
##   wheel    G held over the map opens the wheel where the pointer is, the pointer moved up selects "Attack here", letting go of G places it: the marker is
##            on the ground (with the faction's beacon model), the minimap has a beacon event there, the HUD never saw the G
##   tap      a quick tap of G opens nothing and reaches the game as its own G: the HUD gets the press and the release, the CommandMap's Hold Ground
##            stance is translated (STANCE_HOLDGROUND)
##   kinds    the other sectors (right, down, left, a tap in the middle): each its own kind; "danger" is the radar's under-attack event; a fourth ping within
##            five seconds of three is refused
##   minimap  G held over the minimap: the ping is at the map point the minimap shows there, not on the terrain behind the HUD
##   space    the camera moved away, Space (the CommandMap's VIEW_LAST_RADAR_EVENT) looks at a ping again
##   menus    with the quit menu open a held G opens nothing; Esc over an open wheel closes it and leaves the quit menu shut
##   options  OpenBFMEPingWheel = no (the Options screen's apply): a held G opens nothing and is the game's; OpenBFMEPingKey = altg / j: those keys open
##            it; back to the held G
##   hash     (with --pingwheel-hash=quiet|pings) the logic's frame hashes after 60 logic frames, with or without pings placed meanwhile: the test compares
##            two runs (pings never reach the simulation)
##   optshot  the OpenBFME options screen's Interface tab with the two ping rows (pingwheel-options.png; for the proof, opened over the quit menu)
##   looks    the wheel with each choice under the pointer, and a placed ping up close (pingwheel-look-<kind>.png, pingwheel-marker.png; a windowed run)
##   video    a scripted demonstration for the proof video: the frames are saved in --screens as pingwheel-video-<n>.jpg (a windowed run)
extends "res://scripts/scenario_player.gd"

var game: Node
var hud: Node
var world: Node
var wheel: CanvasLayer
var board: Node3D
var _mods := {"alt": false}


func _run(p_game: Node, scenarios: PackedStringArray) -> void:
	game = p_game
	hud = game._hud
	world = game._world
	wheel = game._ping_wheel
	board = game._pings
	if wheel == null or board == null:
		print("PINGWHEEL setup: FAIL the game installed no ping wheel")
		return # every requested scenario is then reported as failed (scenario_player.gd)
	print("PINGWHEEL setup: beacon model '%s', errors %s" % [board.beacon_model, board.errors])
	await _run_scenarios(scenarios)


# ---- input -------------------------------------------------------------------------------------------------------------------------------------------

func _key(code: Key, down: bool) -> void:
	var e := InputEventKey.new()
	e.keycode = code
	e.physical_keycode = code
	e.pressed = down
	if code == KEY_ALT:
		_mods.alt = down
	e.alt_pressed = _mods.alt
	Input.parse_input_event(e)
	await game._step(2)


func _move(p: Vector2, steps: int = 1) -> void:
	var from: Vector2 = wheel._pointer
	for i in steps:
		var m := InputEventMouseMotion.new()
		m.position = from.lerp(p, float(i + 1) / steps)
		m.global_position = m.position
		Input.parse_input_event(m)
		await game._step(1)


## G held past the wheel's hold time (it opens), and let go
func hold_down() -> void:
	await _key(KEY_G, true)
	await game._step(int(wheel.hold_ms * 60 / 1000) + 6) # at least that long (game._step waits frames / 60 seconds too)


func hold_up() -> void:
	await _key(KEY_G, false)


## hold G at `at`, move to `to`, let go
func ping(at: Vector2, to: Vector2, steps: int = 3, shot_name := "") -> void:
	await _move(at)
	await hold_down()
	await _move(to, steps)
	if shot_name != "":
		game._save_named(shot_name)
	await hold_up()


func centre() -> Vector2:
	return game.get_viewport().get_visible_rect().size * Vector2(0.5, 0.45)


func key_events() -> int:
	return int(hud.get_state().get("keys", {}).get("key_events", -1))


func radar_events() -> Array:
	return hud.get_radar_state().get("events", [])


func has_event(type: int, at: Vector2) -> bool:
	for e in radar_events():
		if int(e.type) == type and (e.world as Vector2).distance_to(at) < 1.0:
			return true
	return false


## a ping refused by the rate limit leaves no room for the next scenario: wait until the window is empty
func wait_burst() -> void:
	await game._step(int(board.BURST_WINDOW_MS / 1000.0 * 60) + 10)


# ---- scenarios ---------------------------------------------------------------------------------------------------------------------------------------

func _scenario_wheel() -> String:
	var c := centre()
	var ground: Vector2 = hud.pixel_to_world(c)
	if ground.x < -1e8:
		return "the screen's centre shows no ground"
	var keys_before: int = key_events()
	var shown: int = board.shown_total
	await _move(c)
	await hold_down()
	if not wheel.is_open():
		return "holding G did not open the wheel"
	await _move(c + Vector2(0, -wheel._radius() * 0.7), 4)
	if wheel.selection() != board.kind_index("attack"):
		return "the pointer up selects kind %d, not attack" % wheel.selection()
	game._save_named("pingwheel-open")
	await hold_up()
	if wheel.is_open():
		return "letting go of G left the wheel open"
	if board.shown_total != shown + 1 or board.last.kind != board.kind_index("attack"):
		return "no attack ping: %s" % [board.last]
	var at: Vector2 = Vector2(board.last.x, board.last.y)
	if at.distance_to(ground) > 0.5:
		return "the ping is at %s, the wheel opened over %s" % [at, ground]
	if board.marker_count() < 1:
		return "no marker on the ground"
	await game._step(10)
	if not has_event(6, at):
		return "the minimap has no beacon event at the ping: %s" % [radar_events()]
	# the held G was the wheel's: the HUD saw no key
	if key_events() != keys_before:
		return "the HUD saw %d key events (none expected)" % (key_events() - keys_before)
	if board.beacon_model == "":
		return "no beacon model: %s" % [board.errors]
	await game._step(20)
	game._save_named("pingwheel-ground")
	return ""


func _scenario_tap() -> String:
	await _move(centre())
	var keys_before: int = key_events()
	var metas_before: int = int(hud.get_state().get("keys", {}).get("metas", 0))
	var opened: int = wheel.opened_total
	for down in [true, false]: # one after the other, well inside the hold time even on a slow frame
		var e := InputEventKey.new()
		e.keycode = KEY_G
		e.physical_keycode = KEY_G
		e.pressed = down
		Input.parse_input_event(e)
	await game._step(6)
	var keys: Dictionary = hud.get_state().get("keys", {})
	if wheel.opened_total != opened or wheel.is_open():
		return "a tap of G opened the wheel"
	if key_events() - keys_before != 2:
		return "the HUD saw %d key events of the tap (the press and the release expected)" % (key_events() - keys_before)
	if int(keys.get("metas", 0)) <= metas_before or not str(keys.get("last_meta", "")).contains("HOLDGROUND"):
		return "the tap's G made no Hold Ground command (last meta %s)" % keys.get("last_meta", "")
	return ""


func _scenario_kinds() -> String:
	await wait_burst()
	var c := centre()
	var r: float = wheel._radius() * 0.7
	var want := [["reinforce", Vector2(r, 0)], ["danger", Vector2(0, r)], ["assist", Vector2(-r, 0)]]
	var spot := 0
	for w in want:
		spot += 1
		var at := c + Vector2(-260 + 180 * spot, 60)
		await ping(at, at + w[1])
		if board.last.kind != board.kind_index(w[0]):
			return "%s: the ping is kind %d" % [w[0], board.last.kind]
	await game._step(6)
	var danger_at: Vector2 = hud.pixel_to_world(c + Vector2(-260 + 360, 60))
	if not has_event(3, danger_at):
		return "the danger ping is no under-attack event on the minimap: %s" % [radar_events()]
	# the rate limit: a slow machine may take more than five seconds for the three pings above, so the window is filled with this moment's three (the
	# board's own record of the local player's pings) before the fourth
	var times: Array = []
	for i in board.BURST:
		times.append(Time.get_ticks_msec())
	board._recent[board.local_player] = times
	var refused: int = board.refused_total
	var shown: int = board.shown_total
	await ping(c, c) # a tap in the middle: "here", but a fourth ping within 5 s
	if board.refused_total != refused + 1 or board.shown_total != shown:
		return "the fourth ping within five seconds was not refused"
	await wait_burst()
	await ping(c, c)
	if board.last.kind != board.kind_index("here") or board.shown_total != shown + 1:
		return "a tap in the middle is no 'here' ping: %s" % [board.last]
	return ""


func _scenario_minimap() -> String:
	await wait_burst()
	var sq: Rect2 = hud.get_radar_square()
	if sq.size.x < 4: # a headless run's window is 64 x 64: its minimap is a few pixels wide, still enough for the mapping
		return "no minimap: %s" % sq
	var at := sq.position + sq.size * Vector2(0.62, 0.4)
	var shown: int = board.shown_total
	await ping(at, at)
	if board.shown_total != shown + 1:
		return "no ping over the minimap"
	var p := Vector2(board.last.x, board.last.y)
	var back: Vector2 = hud.world_to_radar_pixel(p)
	var cell := sq.size.x / 128.0
	if back.distance_to(at) > 2.5 * cell + 1.5: # ping_point takes the whole pixel
		return "the ping at %s shows at minimap pixel %s, the pointer was at %s" % [p, back, at]
	var behind: Vector2 = hud.pixel_to_world(at)
	if behind.x > -1e8 and behind.distance_to(p) < 1.0:
		return "the ping is the terrain behind the minimap"
	return ""


func _scenario_space() -> String:
	await wait_burst()
	var c := centre()
	await ping(c, c)
	var at: Vector2 = Vector2(board.last.x, board.last.y)
	var cam: Vector2 = hud.get_camera().position
	hud.camera_look_at(cam + Vector2(900, 900))
	await game._step(20)
	var away: Vector2 = hud.get_camera().position
	await _key(KEY_SPACE, true)
	await _key(KEY_SPACE, false)
	await game._step(20)
	var back: Vector2 = hud.get_camera().position
	var best := INF
	for e in radar_events():
		best = minf(best, back.distance_to(e.world))
	if back.distance_to(away) < 100.0 or best > 120.0:
		return "Space did not look at a ping (camera %s -> %s, the pings %s, the last %s)" % [away, back, radar_events(), at]
	return ""


func _scenario_menus() -> String:
	var c := centre()
	game._toggle_quit_menu()
	await game._step(30)
	var opened: int = wheel.opened_total
	await _move(c)
	await hold_down()
	var open_under_menu: bool = wheel.is_open() or wheel.opened_total != opened
	await hold_up()
	game._toggle_quit_menu()
	await game._step(30)
	if open_under_menu:
		return "the wheel opened over the quit menu"
	await hold_down()
	if not wheel.is_open():
		return "the wheel does not open after the quit menu closed"
	var shown: int = board.shown_total
	await _key(KEY_ESCAPE, true)
	await _key(KEY_ESCAPE, false)
	await hold_up()
	await game._step(10)
	if wheel.is_open() or board.shown_total != shown:
		return "Esc did not close the wheel without a ping"
	if game._quit_open:
		game._toggle_quit_menu()
		return "Esc over the wheel opened the quit menu"
	return ""


func _scenario_options() -> String:
	var opts: Node = game._game_options
	var values: Dictionary = opts.current()
	values.OpenBFMEPingWheel = "no"
	opts.apply_global(values)
	var c := centre()
	await _move(c)
	var keys_before: int = key_events()
	await hold_down()
	var opened: bool = wheel.is_open()
	await hold_up()
	var off_to_game: int = key_events() - keys_before
	values.OpenBFMEPingWheel = "yes"
	values.OpenBFMEPingKey = "altg"
	opts.apply_global(values)
	await _key(KEY_ALT, true)
	await _key(KEY_G, true)
	var altg_opens: bool = wheel.is_open()
	await _key(KEY_G, false)
	await _key(KEY_ALT, false)
	values.OpenBFMEPingKey = "j"
	opts.apply_global(values)
	await _key(KEY_J, true)
	var j_opens: bool = wheel.is_open()
	await _key(KEY_J, false)
	values.OpenBFMEPingKey = "g"
	opts.apply_global(values)
	if opened or off_to_game != 2:
		return "OpenBFMEPingWheel = no: the wheel opened (%s), or the G did not reach the game (%d key events)" % [opened, off_to_game]
	if not altg_opens:
		return "OpenBFMEPingKey = altg: Alt+G does not open the wheel"
	if not j_opens:
		return "OpenBFMEPingKey = j: J does not open the wheel"
	return ""


func _scenario_hash() -> String:
	var mode := OS.get_environment("PINGWHEEL_HASH")
	var start: int = world.get_presented_frame()
	var c := centre()
	while world.get_presented_frame() < start + 60:
		if mode == "pings" and board.allowed(board.local_player):
			await ping(c + Vector2(randf_range(-200, 200), randf_range(-100, 100)), c + Vector2.from_angle(randf() * TAU) * 80.0)
		await game._step(5)
	await game._step(5)
	print("PINGWHEEL hash %s pings %d: %s" % [mode, board.shown_total, JSON.stringify(world.get_frame_hashes())])
	if mode == "pings" and board.shown_total < 3:
		return "only %d pings were placed: the comparison would test nothing" % board.shown_total
	if mode != "pings" and board.shown_total != 0:
		return "the quiet run placed %d pings" % board.shown_total
	return ""


func _scenario_looks() -> String:
	await wait_burst()
	var c: Vector2 = game.get_viewport().get_visible_rect().size * Vector2(0.27, 0.56) # open ground beside the start fortress
	var r: float = wheel._radius()
	await _move(c)
	await hold_down()
	if not wheel.is_open():
		return "the wheel did not open"
	for d in [["here", Vector2.ZERO], ["attack", Vector2(0, -r)], ["reinforce", Vector2(r, 0)], ["danger", Vector2(0, r)], ["assist", Vector2(-r, 0)]]:
		await _move(c + d[1], 6)
		await game._step(12)
		game._save_named("pingwheel-look-" + d[0])
	await _move(c + Vector2(r, 0), 4)
	await hold_up()
	await game._step(40)
	game._save_named("pingwheel-marker")
	return ""


func _scenario_optshot() -> String:
	game._toggle_quit_menu()
	await game._step(30)
	var screen: CanvasLayer = game._display_screen
	var stack := func() -> PackedStringArray: return game._shell.shell_stack()
	if not await game._click_quit("Options"):
		game._toggle_quit_menu()
		return "the quit menu has no Options button"
	for i in 40:
		if not stack.call().is_empty() and stack.call()[-1] == "Options.apt" and screen.is_open():
			break
		await game._step(10)
	if not screen.is_open():
		return "the quit menu's Options did not open the settings: %s" % [stack.call()]
	screen._show_tab("Interface")
	await game._step(30)
	game._save_named("pingwheel-options")
	# back to the game as a player goes back: Esc (the settings page, then Options, then the quit menu if it is still up)
	for i in 6:
		var top: PackedStringArray = stack.call()
		if not screen.is_open() and not game._quit_open and (top.is_empty() or top[-1] != "Options.apt"):
			break
		await _key(KEY_ESCAPE, true)
		await _key(KEY_ESCAPE, false)
		await game._step(20)
	if screen.is_open() or game._quit_open:
		return "Esc did not lead back to the game: %s" % [stack.call()]
	return ""


## the demonstration: the camera over the local base, the wheel swept over every sector, five pings placed, the camera turned to show them. The frames
## (a windowed run) go to --screens as pingwheel-video-<n>.jpg with their times in pingwheel-video.txt (ffmpeg's concat list)
var _frames := 0
var _rec_dir := ""
var _rec_last := 0
var _rec_list: FileAccess
func _scenario_video() -> String:
	_rec_dir = game._screens
	if _rec_dir.is_empty():
		return "no --screens folder"
	_rec_list = FileAccess.open(_rec_dir.path_join("pingwheel-video.txt"), FileAccess.WRITE)
	_rec_last = Time.get_ticks_msec()
	game.get_tree().process_frame.connect(_record)
	var c := centre()
	var r: float = wheel._radius() * 0.75
	await game._step(30)
	# open the wheel and sweep it: up, right, down, left, back to the middle; let go on "Attack here"
	await _move(c)
	await hold_down()
	for d in [Vector2(0, -r), Vector2(r, 0), Vector2(0, r), Vector2(-r, 0), Vector2.ZERO, Vector2(0, -r)]:
		await _move(c + d, 14)
		await game._step(20)
	await hold_up()
	await game._step(70)
	for p in [[Vector2(-300, 80), Vector2(r, 0)], [Vector2(320, 40), Vector2(0, r)]]:
		var at: Vector2 = c + p[0]
		await _move(at, 12)
		await hold_down()
		await _move(at + p[1], 12)
		await game._step(18)
		await hold_up()
		await game._step(50)
	await wait_burst()
	var at2 := c + Vector2(-120, 160)
	await _move(at2, 12)
	await hold_down()
	await _move(at2 + Vector2(-r, 0), 12)
	await game._step(18)
	await hold_up()
	await game._step(150)
	game.get_tree().process_frame.disconnect(_record)
	_rec_list.close()
	print("PINGWHEEL video: %d frames in %s" % [_frames, _rec_dir])
	return "" if _frames > 100 else "only %d frames recorded (a headless run?)" % _frames


func _record() -> void:
	var image: Image = game.get_viewport().get_texture().get_image()
	if image == null or image.is_empty():
		return
	var now := Time.get_ticks_msec()
	var file := "pingwheel-video-%05d.jpg" % _frames
	image.save_jpg(_rec_dir.path_join(file), 0.9)
	if _frames > 0:
		_rec_list.store_line("duration %.4f" % ((now - _rec_last) / 1000.0))
	_rec_list.store_line("file '%s'" % file)
	_rec_last = now
	_frames += 1


## the runner's line prefix: PINGWHEEL (pingwheel_test.gd reads these lines), not the file name's PING_WHEEL. Kept below the scenarios: the
## runner's regression test (tools/qa/test_scenario_players.py) takes the first _scenario_ function as the scenario
func _scenario_label() -> String:
	return "PINGWHEEL"

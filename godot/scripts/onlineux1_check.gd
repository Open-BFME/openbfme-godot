## Lane ONLINEUX-1 r2 (Sol 2, Sol 3): game.gd --auto --onlineux1-game, run by tools/net/test_onlineux1_game.py. An online game the online screens started
## that cannot go on (a rejoin that failed) is given up through game.gd's own path: _net_pause_tick fed with net_status's "rejoin" (the status a
## failed rejoin reports: GameWorld::net_status) runs the rejoin watch and _online_game_failed. Here the game is the --auto skirmish marked as one the
## online screens started (the real online START needs a lobby and a relay: tools/net/onlineux1_session.sh walks those by hand):
##   1. Esc's quit menu open and the catch-up line shown when the rejoin fails: both go (before r2 the quit menu's overlay, _quit_open and the
##      "Rejoining" line stayed over the menus);
##   2. a second game whose rejoin went live, then the quit menu's Options open and a rejoin of a new attempt that fails at once (no catch-up seen):
##      given up too (before r2 the watch a live attempt left spent ignored it and the game hung on the map), Options goes with it;
##   3. a third game plays and its Esc opens the quit menu as usual.
## The first failure leaves the world paused and slowed (what a failed Watch does): the next game must run, unpaused, at speed 1.
## Prints "UX1 <name>: ok / FAIL <why>" and "UX1 RESULT: <n> ok, <m> failed".
extends RefCounted

var game: Node
var shell: Node
var ok_count := 0
var fail_count := 0


func check(name: String, cond: bool, detail := "") -> void:
	if cond:
		ok_count += 1
		print("UX1 %s: ok" % name)
	else:
		fail_count += 1
		print("UX1 %s: FAIL %s" % [name, detail])


func _status(state: String, attempt: int, frame: int) -> Dictionary:
	return {"frame": frame, "pause": {"paused": false}, "rejoin": {"state": state, "attempt": attempt, "frame": frame, "live_frame": 0, "crcs_compared": 0,
		"catch_up_ms": 0, "failure": "the relay keeps no log of this game: it cannot be rejoined"}}


func _wait(cond: Callable, seconds: float) -> bool:
	var until := Time.get_ticks_msec() + int(seconds * 1000.0)
	while Time.get_ticks_msec() < until:
		if cond.call():
			return true
		await game.get_tree().process_frame
	return false


func _back_in_menus(tag: String) -> void:
	var back: bool = await _wait(func(): return game._state == game.State.MENU and shell.shell_stack().size() > 0, 20.0)
	check("%s: back in the menus" % tag, back and game._hud == null, "state %d, stack %s" % [game._state, str(shell.shell_stack())])
	check("%s: the quit menu is closed (overlay and flag)" % tag, not bool(shell.quit_menu({"kind": "state"}).get("open", true)) and not game._quit_open,
		"%s, _quit_open %s" % [JSON.stringify(shell.quit_menu({"kind": "state"})), str(game._quit_open)])
	check("%s: no catch-up / pause line over the menus" % tag, game._pause_label == null or game._pause_label.text == "",
		game._pause_label.text if game._pause_label != null else "")
	check("%s: no in-game screen left on the stack" % tag, not shell.shell_stack().has("Options.apt") and not shell.shell_stack().has("QuitMenu.apt"),
		str(shell.shell_stack()))
	check("%s: the OpenBFME options page is closed" % tag, game._display_screen == null or not game._display_screen.is_open())


func _next_game(tag: String) -> bool:
	await game._step(game.LEVEL_MAIN_MENU_FRAMES + 70)
	if not await game._auto_lobby_and_start():
		check("%s: the next game starts" % tag, false, "the lobby path did not start")
		return false
	var live: bool = await _wait(func(): return game._state == game.State.PLAYING and game._hud != null and game._world.get_stats().frame > 5, 60.0)
	check("%s: the next game starts" % tag, live, "state %d" % game._state)
	check("%s: the next game is not paused and runs at speed 1" % tag, not game._world.is_paused() and game._world.get_time_scale() == 1.0,
		"paused %s, speed %s" % [str(game._world.is_paused()), str(game._world.get_time_scale())])
	return live


func run(p_game: Node) -> int:
	game = p_game
	shell = game._shell
	check("a live game", game._state == game.State.PLAYING and game._hud != null, "state %d" % game._state)
	# 1. the quit menu open, the catch-up line up, then the rejoin fails
	game._online_from_screens = true
	await game._press_escape()
	var opened: bool = await _wait(func(): return game._quit_open and bool(shell.quit_menu({"kind": "state"}).get("open", false)), 10.0)
	check("1: Esc opened the quit menu", opened)
	game._net_pause_tick(_status("catching_up", 1001, 3))
	check("1: the catch-up line is shown", game._pause_label != null and game._pause_label.text.begins_with("Rejoining the game"),
		game._pause_label.text if game._pause_label != null else "no label")
	# the state a failed Watch leaves (GameWorld::advance pauses the world of a failed spectator; the caster's bar also sets the speed): the game that
	# is given up must not hand it to the next one. v0.6.11: the next online game stood at frame 1 on that peer, and so on every player's
	# (a network game's quit menu does not pause, so its close does not resume either: here the game is a skirmish, whose menu would)
	game._quit_paused = false
	game._world.set_paused(true)
	game._world.set_time_scale(0.25)
	game._net_pause_tick(_status("failed", 1001, 3))
	await _back_in_menus("1")
	if not await _next_game("2"):
		return _finish()
	# 2. a rejoin that went live; then Options open over the quit menu and a new attempt that fails at once
	game._online_from_screens = true
	game._net_pause_tick(_status("catching_up", 2001, 0))
	game._net_pause_tick(_status("live", 2001, 4))
	check("2: the live rejoin left the game playing", game._state == game.State.PLAYING and game._pause_label.text == "", "state %d" % game._state)
	await game._press_escape()
	await _wait(func(): return game._quit_open, 10.0)
	var options := false
	if await game._click_quit("Options"):
		options = await _wait(func(): return shell.shell_stack().size() > 0 and shell.shell_stack()[-1] == "Options.apt", 10.0)
	check("2: the quit menu's Options is open", options, str(shell.shell_stack()))
	game._net_pause_tick(_status("failed", 2002, 0))
	await _back_in_menus("2")
	if not await _next_game("3"):
		return _finish()
	# 3. the third game's Esc opens the quit menu as usual (nothing stale from the failures)
	await game._press_escape()
	var reopened: bool = await _wait(func(): return game._quit_open and bool(shell.quit_menu({"kind": "state"}).get("open", false)), 10.0)
	check("3: Esc opens the quit menu in the next game", reopened)
	return _finish()


func _finish() -> int:
	print("UX1 RESULT: %d ok, %d failed" % [ok_count, fail_count])
	return fail_count

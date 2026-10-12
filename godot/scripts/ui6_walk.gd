## Lane UI-6: the paths of a Discord tester's menu findings on v0.3.0-preview.6 (GitGut, 2026-10-11), with real input, and their proof pictures:
## the main menu's button texts (1), its SOLO PLAY nav closing when the pointer leaves it, Skirmish's first entry (2: MENU-1's fix), the Game Setup map
## list's rows (3), the Profile nav opened by the pointer and closed when it leaves (4), the lobby's
## map, players and rules kept after a trip to the main menu (5), and the power points over the spell book button in the game (6).
## game.gd runs it with --ui6-walk[=DIR] (DIR: screenshots ui6-<step>.png, windowed runs only); "UI6 <step>: ok" / "UI6 <step>: FAIL <why>" per step,
## "UI6 RESULT: <n> ok, <m> failed" at the end; exit 0 when nothing failed. A build before lane UI-6 has no lobby_state(): its checks are skipped (the
## "before" pictures), the pictures are taken all the same.
extends RefCounted

var game: Node
var shell: Object
var dir := ""
var ok_count := 0
var fail_count := 0
# the map the walk picks (not the lobby's first), as the lobby lists it
const PICKED_MAPS := ["maps/map mp evendim/map mp evendim.map", "maps\\map mp evendim\\map mp evendim.map"]


func run(p_game: Node, p_dir: String) -> void:
	game = p_game
	shell = game._shell
	dir = p_dir
	if not dir.is_empty():
		DirAccess.make_dir_recursive_absolute(dir)
	await game._step(game.LEVEL_MAIN_MENU_FRAMES + 90)
	await game._wait_main_ready()
	await _shot("main")
	# a main menu button under the pointer (its "over" look)
	var level: int = shell.shell_top_level()
	var solo: Dictionary = shell.find_button(level, "SoloPlayNav")
	if solo.get("found", false):
		game._mouse_event(Vector2(solo.x, solo.y), false, false)
		await game._step(30)
		await _shot("main-hover")
		# the pointer off the nav: its alpha 0 surround closes it (MainMenu.apt sprite 25 depth 1, as Skirmish's Profile nav)
		game._mouse_event(Vector2(512, 200), false, false)
		await game._step(90)
		if shell.find_button(level, "SoloPlayNav.Skirmish").get("found", false):
			_fail("main-nav-left", "the SOLO PLAY nav stays open after the pointer left it")
		else:
			_ok("main-nav-left")
	await _texts("main")
	await _skirmish_first_visit()
	await _profile_nav()
	await _change_and_return()
	await _start_game()
	print("UI6 RESULT: %d ok, %d failed" % [ok_count, fail_count])
	game.get_tree().quit(0 if fail_count == 0 else 1)


func _skirmish_first_visit() -> void:
	await game._click_button(shell.shell_top_level(), "SoloPlayNav")
	await game._step(60)
	await game._click_button(shell.shell_top_level(), "SoloPlayNav.Skirmish")
	await game._step(120)
	# item 2 (MENU-1's fix, verified here): the first entry shows the profile box over the greyed game setup, nothing else on top
	await _shot("skirmish-first-entry")
	var r: Dictionary = shell.get_layering_report() if shell.has_method("get_layering_report") else {}
	var problems: Array = r.get("problems", [])
	if problems.is_empty():
		_ok("skirmish-first-entry")
	else:
		_fail("skirmish-first-entry", "; ".join(problems))
	var applied: Dictionary = shell.lobby_apply({"profile": "asdasd"})
	if not applied.get("ok", false):
		print("UI6 profile: ", applied)
	await game._step(150)
	await _shot("lobby")
	await _texts("lobby")


# ---- 4: the Profile nav opens under the pointer and closes when the pointer leaves it (Skirmish.apt: sprite 34's bttn rollOver -> _show, the
# surround button's rollOver at depth 1 -> _close) ---------------------------------------------------------------------------------------------------
func _profile_nav() -> void:
	var bttn := _button_ending("ProfileNav.bttn")
	if bttn.is_empty():
		bttn = _button_ending("ProfileNav")
	if bttn.is_empty():
		_fail("profile-nav-open", "no ProfileNav button")
		return
	game._mouse_event(Vector2(bttn.x, bttn.y), false, false)
	await game._step(90)
	await _shot("profile-nav-open")
	var item := _button_ending("ProfileNav.ChangeProfile")
	if item.is_empty() or not item.hittable:
		_fail("profile-nav-open", "the pointer on PROFILE did not open the nav (no hittable ProfileNav.ChangeProfile)")
		return
	_ok("profile-nav-open")
	# along the nav's items, then off it onto the game setup (the stage's middle)
	game._mouse_event(Vector2(item.x, item.y), false, false)
	await game._step(30)
	var size: Vector2 = shell.get_stage_size()
	game._mouse_event(Vector2(size.x * 0.5, size.y * 0.55), false, false)
	await game._step(120)
	await _shot("profile-nav-left")
	item = _button_ending("ProfileNav.ChangeProfile")
	if not item.is_empty() and item.hittable:
		_fail("profile-nav-left", "the nav stays open after the pointer left it")
	else:
		_ok("profile-nav-left")
	# the layering report: the nav and the game setup do not draw over each other's titles
	var r: Dictionary = shell.get_layering_report() if shell.has_method("get_layering_report") else {}
	var problems: Array = r.get("problems", [])
	if not problems.is_empty():
		_fail("profile-nav-layering", "; ".join(problems))


# ---- 5: the lobby's choices kept after the main menu --------------------------------------------------------------------------------------------
func _change_and_return() -> void:
	var applied: Dictionary = {}
	for key in PICKED_MAPS:
		applied = shell.lobby_apply({"map": key})
		if applied.get("ok", false):
			break
	if not applied.get("ok", false):
		_fail("lobby-changed", str(applied.get("errors", [])))
	applied = shell.lobby_apply({"slots": [{"slot": 0, "faction": "FactionDwarves"}, {"slot": 1, "state": 4}, {"slot": 2, "state": 3}]})
	if not applied.get("ok", false):
		_fail("lobby-changed", str(applied.get("errors", [])))
	await game._step(60)
	await _shot("lobby-changed")
	var before: Dictionary = _lobby_state()
	if not await _click_hittable("lobby.MainMenu"):
		_fail("lobby-again", "no hittable lobby.MainMenu")
		return
	await game._step(150)
	await game._wait_main_ready()
	await game._click_button(shell.shell_top_level(), "SoloPlayNav")
	await game._step(60)
	await game._click_button(shell.shell_top_level(), "SoloPlayNav.Skirmish")
	await game._step(150)
	await _shot("lobby-again")
	var after: Dictionary = _lobby_state()
	if before.is_empty():
		print("UI6 lobby-again: no lobby_state() in this build (the picture only)")
		return
	print("UI6 lobby before %s" % JSON.stringify(before))
	print("UI6 lobby after  %s" % JSON.stringify(after))
	var why := ""
	if str(after.get("restored", "")).is_empty():
		why += "the lobby did not begin from the kept game; "
	for key in ["map", "slots", "rules"]:
		if str(before.get(key)) != str(after.get(key)):
			why += "%s changed: %s -> %s; " % [key, str(before.get(key)), str(after.get(key))]
	if why.is_empty():
		_ok("lobby-again")
	else:
		_fail("lobby-again", why)


# ---- 6: the power points over the spell book button --------------------------------------------------------------------------------------------
func _start_game() -> void:
	if not await _click_hittable("lobby.StartGame"):
		_fail("game", "no hittable lobby.StartGame")
		return
	for i in 240:
		await game._step(30)
		if game._state == game.State.PLAYING:
			break
	if game._state != game.State.PLAYING:
		_fail("game", "the game did not start (state %d)" % game._state)
		return
	await game._step(240)
	await _shot("game")
	_ok("game")


# ---- helpers --------------------------------------------------------------------------------------------------------------------------------------
func _lobby_state() -> Dictionary:
	return shell.lobby_state() if shell.has_method("lobby_state") else {}


func _texts(step: String) -> void:
	# the drawn texts' window rectangles (the placement the pictures show), for the report
	if not shell.has_method("drawn_texts"):
		return
	var texts: Array = shell.drawn_texts(false)
	var n := 0
	for t in texts:
		if n < 60:
			print("UI6 text %s: %s" % [step, JSON.stringify(t)])
		n += 1


func _ok(step: String) -> void:
	ok_count += 1
	print("UI6 %s: ok" % step)


func _fail(step: String, why: String) -> void:
	fail_count += 1
	print("UI6 %s: FAIL %s" % [step, why])


func _shot(step: String) -> void:
	if dir.is_empty() or DisplayServer.get_name() == "headless":
		return
	await RenderingServer.frame_post_draw
	var image: Image = game._capture_image(step)
	if image != null:
		print("UI6 screenshot %s -> %s" % [step, error_string(image.save_png("%s/ui6-%s.png" % [dir, step]))])


func _button_ending(name: String) -> Dictionary:
	for level in range(15, -1, -1):
		for b in shell.list_buttons(level):
			var p := String(b.path)
			if p.ends_with("." + name) or p == name or p.ends_with("." + name + ".bttn"):
				return b
	return {}


func _click_hittable(name: String) -> bool:
	for level in range(15, -1, -1):
		for b in shell.list_buttons(level):
			var p := String(b.path).trim_suffix(".bttn")
			if b.hittable and (p.ends_with("." + name) or p == name):
				game._mouse_event(Vector2(b.x, b.y), false, false)
				await game._step(2)
				game._mouse_event(Vector2(b.x, b.y), true, true)
				await game._step(2)
				game._mouse_event(Vector2(b.x, b.y), false, true)
				await game._step(2)
				print("UI6 clicked ", b.path)
				return true
	return false

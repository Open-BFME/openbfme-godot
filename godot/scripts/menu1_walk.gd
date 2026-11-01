## Lane MENU-1: the front-end paths a player takes, with real input events (mouse clicks pushed into the viewport, keys through Input.parse_input_event),
## checked for the owner's layering bugs of preview.4 (2026-10-10): after every step the shell's layering report (AptMenuPlayer.get_layering_report,
## GameClient/GUI/Shell/ShellLayering.h) must name one screen drawing (the top), at most one of its sub-screens, and no overprinting titles; the modal
## add-profile box must keep the input from the game setup under it.
## Paths: Skirmish's first-visit profile box (typed, accepted), the lobby, its PROFILE page and back, the main menu's Options, LAN, Replays, Load,
## Credits and Create-a-Hero, Skirmish again (no box: the profile is kept), a skirmish with three players started from the lobby (the loading screen),
## and in the game Esc, the quit menu's Options and back.
## game.gd runs it with --menu1-walk[=DIR] (DIR: screenshots walk1-<step>.png, windowed runs only); "MENU1 <step>: ok" / "MENU1 <step>: FAIL <why>"
## per step, "MENU1 RESULT: <n> ok, <m> failed" at the end; exit 0 when nothing failed.
## Lane MODUI-1: a step also fails when the shell reported an error since the last step (a movie or sub-movie that did not load: a mod's screen
## left empty) and the Skirmish lobby must list maps and show its player slots; the movies' calls the shell does not handle are printed at the end
## ("MENU1 calls without function: [...]", "MENU1 unhandled commands: [...]") so tools/mod/test_modui1_shell_screens.py compares a mod with 2.01.
extends RefCounted

var game: Node
var shell: Object
var dir := ""
var ok_count := 0
var fail_count := 0
var loading_checked := false
var errors_seen := 0 # lane MODUI-1: the shell report's errors already reported
const PROFILE_ENTRY_STAGE := Rect2(369, 350, 272, 26) # the MENU-1 build's "profile-entry" rect (stage pixels, Skirmish.apt's layout)

const MENU_PATHS := [
	["options", "OptionsNav", "Settings"],
	["lan", "MultiPlayNav", "LocalNetwork"],
	["online", "MultiPlayNav", "Online"],
	["replays", "MultiPlayNav", "Replay"],
	["load", "SoloPlayNav", "LoadGame"],
	["credits", "OptionsNav", "Credits"],
	["create-a-hero", "", "MyHeroes"],
]


func run(p_game: Node, p_dir: String) -> void:
	game = p_game
	shell = game._shell
	dir = p_dir
	if not dir.is_empty():
		DirAccess.make_dir_recursive_absolute(dir)
	await game._step(game.LEVEL_MAIN_MENU_FRAMES + 90)
	await game._wait_main_ready()
	print("MENU1 style: ", game._menu_style, " presentation: ", shell.get_shell_report().get("presentation_style", "missing"))
	await _check("main")
	await _campaign_entries()
	await _skirmish_first_visit()
	if OS.get_environment("MENU1_SKIP_PROFILE_PAGE").is_empty():
		await _skirmish_profile_page()
	else:
		print("MENU1 the profile page skipped (MENU1_SKIP_PROFILE_PAGE: the build before lane MENU-1 crashes on its way back)")
	# back to the main menu with the lobby's MAIN MENU
	if await _click_hittable("lobby.MainMenu"):
		await game._step(150)
		await game._wait_main_ready()
		await _check("main-after-skirmish")
	else:
		_fail("main-after-skirmish", "no hittable lobby.MainMenu")
	for p in MENU_PATHS:
		await _menu_path(p[0], p[1], p[2])
	await _skirmish_start()
	var report: Dictionary = shell.get_shell_report()
	print("MENU1 calls without function: ", JSON.stringify(report.get("calls_without_function", [])))
	var unhandled := []
	for line in report.get("note_lines", []):
		if String(line).begins_with("command-unhandled: "):
			unhandled.push_back(String(line).trim_prefix("command-unhandled: "))
	print("MENU1 unhandled commands: ", JSON.stringify(unhandled))
	print("MENU1 mod data notes: ", JSON.stringify(report.get("mod_data_notes", [])))
	print("MENU1 RESULT: %d ok, %d failed" % [ok_count, fail_count])
	game.get_tree().quit(0 if fail_count == 0 else 1)


## lane CAMPMENU-1: the campaign entry points are retail's Solo Play buttons: Solo Play, The Rise of the Witch-king campaign (SoloPlayNav.Expansion1Campaign),
## the movie's difficulty choice shown, left with its Cancel (the campaign itself starts in camp2_test / campsave1_test)
func _campaign_entries() -> void:
	if game._menu_style != "rotwk":
		return
	var level: int = shell.shell_top_level()
	await game._click_button(level, "SoloPlayNav")
	await game._step(60)
	var entry: Dictionary = game.campaign_entry("Expansion1Campaign")
	if entry.is_empty():
		_fail("campaign", "SoloPlayNav.Expansion1Campaign cannot be clicked: %s" % [game.solo_play_entries()])
		return
	await _click_at(Vector2(entry.x, entry.y))
	await game._step(60)
	await _check("campaign-difficulty")
	if not await _click_hittable("LevelOfDifficulty.cancelButton"):
		_fail("campaign-difficulty", "no hittable LevelOfDifficulty.cancelButton")
		return
	await game._step(90)
	await game._wait_main_ready()
	await _check("main-after-campaign")


# ---- Skirmish ------------------------------------------------------------------------------------------------------------------------------------

func _skirmish_first_visit() -> void:
	await _press_nav("SoloPlayNav", "SoloPlayNav.Skirmish")
	await game._step(90)
	await _check("skirmish-profile-prompt")
	var r: Dictionary = _report()
	if r.get("enabled_gadgets", 0) > 1:
		_fail("skirmish-profile-prompt", "the game setup's gadgets take input under the box (%d enabled)" % r.get("enabled_gadgets", 0))
	# a click on the covered Army combo of slot 0 must not open it (the box is modal: DisableComponents, RW 0x81456F)
	var army: Dictionary = shell.lobby_gadget_rect("0/PlayerTemplate")
	if army.get("found", false):
		var before: Dictionary = shell.lobby_gadget_rect("0/PlayerTemplate")
		await _click_at(Vector2(army.x + army.w * 0.5, army.y + army.h * 0.5))
		await game._step(10)
		var after: Dictionary = shell.lobby_gadget_rect("0/PlayerTemplate")
		if after.get("h", 0) != before.get("h", 0):
			_fail("skirmish-profile-modal", "the Army combo under the box opened (%d -> %d)" % [before.get("h", 0), after.get("h", 0)])
		else:
			ok_count += 1
			print("MENU1 skirmish-profile-modal: ok")
	var entry: Dictionary = shell.lobby_gadget_rect("profile-entry")
	print("MENU1 profile entry rect ", JSON.stringify(entry))
	if not entry.get("found", false) and not shell.has_method("get_layering_report"):
		# a build before lane MENU-1 has no "profile-entry" rect: the entry's stage rectangle as the MENU-1 build reports it (Skirmish.apt's layout)
		entry = {"found": true, "x": PROFILE_ENTRY_STAGE.position.x, "y": PROFILE_ENTRY_STAGE.position.y, "w": PROFILE_ENTRY_STAGE.size.x, "h": PROFILE_ENTRY_STAGE.size.y}
	if entry.get("found", false):
		await _click_at(Vector2(entry.x + entry.w * 0.5, entry.y + entry.h * 0.5))
		await _type_text("asdasd")
		await game._step(10)
		await _check("skirmish-profile-typed")
		if not await _click_hittable("ProfilePopup.Main.Select"):
			_fail("skirmish-profile-accept", "no hittable ProfilePopup.Main.Select")
	else:
		_fail("skirmish-profile-entry", "no profile name entry")
	await game._step(60)
	await _check("skirmish-lobby")
	_check_lobby_filled("skirmish-lobby-filled")


func _skirmish_profile_page() -> void:
	# PROFILE opens the profile nav, its statistics entry the profile page (Stats.swf in SkirmishStats) in place of the game setup
	if not await _click_hittable("ProfileNav"):
		_fail("skirmish-profile-page", "no hittable ProfileNav")
		return
	await game._step(60)
	await _check("skirmish-profile-nav")
	if not await _click_hittable("ProfileNav.ProfileStats"):
		_fail("skirmish-profile-page", "no hittable ProfileNav.ProfileStats")
		return
	await game._step(150)
	await _check("skirmish-profile-page")
	if await _click_hittable("stats.BackProfile"):
		await game._step(150)
		await _check("skirmish-lobby-again")
	else:
		_fail("skirmish-lobby-again", "no hittable stats.BackProfile")


func _skirmish_start() -> void:
	await game._wait_main_ready()
	await _press_nav("SoloPlayNav", "SoloPlayNav.Skirmish")
	await game._step(120)
	var stack: PackedStringArray = shell.shell_stack()
	if stack.size() == 0 or stack[-1] != "Skirmish.apt":
		_fail("skirmish-second-visit", "Skirmish is not on top: %s" % str(stack))
		return
	if _hittable("ProfilePopup.Main.Select") or _hittable("ProfilePopup.Main.Cancel"):
		_fail("skirmish-second-visit", "the profile box opened again although the profile asdasd exists")
	await _check("skirmish-second-visit")
	# two computer players (medium) besides the player: three cards on the loading screen
	# the player's army fixed (the radial step needs a fortress citadel; the same army before and after)
	var spec := {"slots": [{"slot": 0, "faction": "FactionDwarves"}, {"slot": 1, "state": 3}, {"slot": 2, "state": 3}]}
	# lane MODUI-1: on a map that takes three players: the lobby's own if it does, else the first listed one that does (a mod's first map can be a
	# two-player one, Edain's "aldburg horde": the start check refuses three players there, LAN:TooManyPlayers, as retail)
	var st: Dictionary = shell.lobby_state()
	var keys: Array = st.get("map_keys", [])
	var players: Array = st.get("map_players", [])
	var current: int = keys.find(st.get("map", ""))
	if current >= 0 and current < players.size() and players[current] < 3:
		for i in range(min(keys.size(), players.size())):
			if players[i] >= 3:
				spec["map"] = keys[i]
				print("MENU1 the lobby's map %s takes %d players: %s for three" % [keys[current], players[current], keys[i]])
				break
	var applied: Dictionary = shell.lobby_apply(spec)
	if not applied.get("ok", false):
		_fail("skirmish-three-players", str(applied.get("errors", [])))
	await game._step(30)
	await _check("skirmish-three-players")
	if not OS.get_environment("MENU1_SHELL_ONLY").is_empty():
		# lane MODUI-1: the shell screens only (tools/mod/test_modui1_shell_screens.py): a mod's game itself is the simulation's, not the menus'
		print("MENU1 the game start skipped (MENU1_SHELL_ONLY)")
		return
	if not await _click_hittable("lobby.StartGame"):
		_fail("skirmish-play", "no hittable lobby.StartGame")
		return
	for i in 240:
		await game._step(30)
		if game._state == game.State.PLAYING:
			break
	if game._state != game.State.PLAYING:
		_fail("skirmish-play", "the game did not start (state %d) on %s; the lobby's messages: %s" % [game._state, shell.lobby_state().get("map", "?"),
			str(shell.get_shell_report().get("refusals", []))])
		return
	if not loading_checked:
		_fail("skirmish-loading", "the load reported no progress past 40 %")
	await game._step(120)
	await _check("game")
	# the fortress selected: its radial command bubbles (HUD-6), which the quit menu must cover (the tester's report, 2026-10-11)
	var h6: RefCounted = load("res://scripts/hud6_player.gd").new()
	h6.game = game
	h6.hud = game._hud
	h6.world = game._world
	var radial_result: String = await h6._scenario_radial()
	if radial_result != "":
		_fail("game-radial", radial_result)
	elif String(game._hud.get_hud6_state().get("radial", {}).get("unverified", "")).find("[S-3507]") != 0 and game._hud.get_hud6_state().get("radial", {}).has("count") and _report().has("top"):
		_fail("game-radial", "the radial state does not report S-3507")
	await _check("game-radial")
	# Esc: the quit menu over the game; its Options; back
	await game._press_escape()
	await game._step(60)
	await _check("game-quit-menu")
	if await game._click_quit("Options"):
		await game._step(120)
		var stack2: PackedStringArray = shell.shell_stack()
		if stack2.is_empty() or stack2[-1] != "Options.apt":
			_fail("game-options", "Options.apt is not on top: %s" % str(stack2))
		await _check("game-options")
		if await _click_hittable("Buttons.Cancel") or await _click_hittable_containing("Cancel") or await _click_hittable_containing("Back"):
			await game._step(120)
			await _check("game-options-closed")
		else:
			_fail("game-options-closed", "no Back / Cancel on the in-game Options")
	else:
		_fail("game-options", "no Options button on the quit menu")


## game.gd's load progress hook calls this once the load passed 40 %: the loading screen's report and picture (synchronous, as the load runs)
func loading_check(percent: int) -> void:
	if loading_checked:
		return
	loading_checked = true
	var r: Dictionary = _report()
	print("MENU1 loading (%d %%): stack %s report %s" % [percent, str(shell.shell_stack()), JSON.stringify(r)])
	var problems: Array = r.get("problems", []).duplicate()
	var errors: Array = shell.get_shell_report().get("errors", [])
	for i in range(errors_seen, errors.size()):
		problems.push_back("shell error: %s" % errors[i])
	errors_seen = errors.size()
	if problems.is_empty():
		ok_count += 1
		print("MENU1 loading: ok")
	else:
		_fail("loading", "; ".join(problems))
	if not dir.is_empty() and DisplayServer.get_name() != "headless":
		var image: Image = game._capture_image("loading")
		if image != null:
			print("MENU1 screenshot loading -> %s" % error_string(image.save_png("%s/walk1-skirmish-loading.png" % dir)))


# ---- the main menu's other screens ----------------------------------------------------------------------------------------------------------------

func _menu_path(name: String, nav: String, item: String) -> void:
	await game._wait_main_ready()
	var level: int = shell.shell_top_level()
	if not nav.is_empty():
		await game._click_button(level, nav)
		await game._step(60)
	var target := (nav + "." + item) if not nav.is_empty() else item
	if not shell.find_button(level, target).get("found", false):
		# lane MODUI-1: a mod's own MainMenu.apt can leave an entry out (Ennorath has no Create-a-Hero): reported apart from a button that does not work
		_fail(name, "the menu has no button %s" % target)
		return
	if not await game._click_button(level, target):
		_fail(name, "cannot press %s" % target)
		return
	await game._step(150)
	await _check(name)
	if name == "create-a-hero":
		# past the introduction (the promo page's Continue: CahNewFeatures -> ShowScreen Manager), the hero manager
		var cont := false
		for b in shell.list_buttons(shell.shell_top_level()):
			if b.hittable and String(b.path).containsn("Continue"):
				await _click_at(Vector2(b.x, b.y))
				print("MENU1 clicked ", b.path)
				cont = true
				break
		if not cont and game._menu_style == "rotwk" and not shell.screen_state().get("cah_suppress_promo", false):
			_fail("create-a-hero-manager", "no hittable Continue on the introduction")
		await game._step(150)
		await _check("create-a-hero-manager")
	# a box some screens open first (Create-a-Hero's introduction, an unavailable screen's message) is part of the step: its OK / Continue closes it
	var stack: PackedStringArray = shell.shell_stack()
	if stack.size() > 0 and stack[-1] == "MainMenu.apt":
		# a message box (on a level of its own) or a page of the main menu itself (Credits): left by its Ok / Exit
		var ok: Dictionary = game._walk_find_box_ok()
		if not ok.is_empty():
			await _click_at(Vector2(ok.x, ok.y))
		else:
			for b in shell.list_buttons(shell.shell_top_level()):
				var path := String(b.path)
				if b.hittable and path.containsn("Exit") and not path.containsn("Quit"):
					await _click_at(Vector2(b.x, b.y))
					break
		await game._step(150)
	elif game._display_screen.is_open():
		var buttons: Dictionary = shell.openbfme_page_state().get("buttons", {})
		if buttons.has("cancel"):
			await _click_at((buttons.cancel as Rect2).get_center())
		else:
			_fail(name, "Options has no Cancel button")
	elif not await game._walk_back(name):
		_fail(name, "no usable back button")
		shell.shell_pop()
		await game._step(120)
	await game._step(120)
	await game._wait_main_ready()
	await _check("main-after-" + name)


# ---- checks and input ---------------------------------------------------------------------------------------------------------------------------

# lane MODUI-1: the setup panel came up with its contents (owner 10-30: Edain's lobby showed the frame only): the map list and the player slots
func _check_lobby_filled(step: String) -> void:
	var st: Dictionary = shell.lobby_state()
	var missing := []
	if not st.get("found", false):
		missing.push_back("no lobby")
	if (st.get("map_keys", []) as Array).is_empty():
		missing.push_back("no map listed")
	if not shell.lobby_gadget_rect("0/PlayerTemplate").get("found", false):
		missing.push_back("no Army combo in player slot 0")
	if missing.is_empty():
		ok_count += 1
		print("MENU1 %s: ok (%d maps)" % [step, st.map_keys.size()])
	else:
		_fail(step, ", ".join(missing))


# the layering report; a build before lane MENU-1 has none (the "before" pictures: every check passes, only the pictures count)
func _report() -> Dictionary:
	return shell.get_layering_report() if shell.has_method("get_layering_report") else {}


func _fail(step: String, why: String) -> void:
	fail_count += 1
	print("MENU1 %s: FAIL %s" % [step, why])


func _check(step: String) -> void:
	await _shot(step)
	var r: Dictionary = _report()
	print("MENU1 %s: stack %s report %s" % [step, str(shell.shell_stack()), JSON.stringify(r)])
	var problems: Array = r.get("problems", []).duplicate()
	var errors: Array = shell.get_shell_report().get("errors", [])
	for i in range(errors_seen, errors.size()):
		problems.push_back("shell error: %s" % errors[i])
	errors_seen = errors.size()
	if problems.is_empty():
		ok_count += 1
		print("MENU1 %s: ok" % step)
	else:
		_fail(step, "; ".join(problems))


func _shot(step: String) -> void:
	if dir.is_empty() or DisplayServer.get_name() == "headless":
		return
	await RenderingServer.frame_post_draw
	var image: Image = game._capture_image(step)
	if image != null:
		print("MENU1 screenshot %s -> %s" % [step, error_string(image.save_png("%s/walk1-%s.png" % [dir, step]))])


func _press_nav(nav: String, item: String) -> void:
	var level: int = shell.shell_top_level()
	await game._click_button(level, nav)
	await game._step(60)
	await game._click_button(level, item)


func _hittable(name: String) -> bool:
	for level in range(15, -1, -1):
		for b in shell.list_buttons(level):
			var p := String(b.path).trim_suffix(".bttn")
			if b.hittable and (p.ends_with("." + name) or p == name):
				return true
	return false


# the hittable button of any shell level whose path ends with ".<name>" (or ".<name>.bttn"); clicked with the mouse
func _click_hittable(name: String) -> bool:
	for level in range(15, -1, -1):
		for b in shell.list_buttons(level):
			var p := String(b.path).trim_suffix(".bttn")
			if b.hittable and (p.ends_with("." + name) or p == name):
				await _click_at(Vector2(b.x, b.y))
				print("MENU1 clicked ", b.path)
				return true
	return false


# the first hittable button of the top screen whose path holds `fragment`
func _click_hittable_containing(fragment: String) -> bool:
	for b in shell.list_buttons(shell.shell_top_level()):
		if b.hittable and String(b.path).containsn(fragment):
			await _click_at(Vector2(b.x, b.y))
			print("MENU1 clicked ", b.path)
			return true
	return false


func _click_at(at: Vector2) -> void:
	game._mouse_event(at, false, false)
	await game._step(2)
	game._mouse_event(at, true, true)
	await game._step(2)
	game._mouse_event(at, false, true)
	await game._step(2)


func _type_text(text: String) -> void:
	for ch in text:
		var down := InputEventKey.new()
		down.keycode = OS.find_keycode_from_string(ch.to_upper())
		down.unicode = ch.unicode_at(0)
		down.pressed = true
		Input.parse_input_event(down)
		await game._step(2)
		var up := InputEventKey.new()
		up.keycode = down.keycode
		up.pressed = false
		Input.parse_input_event(up)
		await game._step(2)

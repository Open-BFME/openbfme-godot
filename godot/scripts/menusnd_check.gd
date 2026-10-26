## Lane MENUSND-1: game.gd --menusnd-check. The menus' click and hover sounds in the running game, with real mouse events: the main menu's three
## navigations, the entries of their lists, and the bottom bar of Options (the OpenBFME menu, as the main menu's SETTINGS opens it). RotWK plays
## them from the movies' ActionScript: MenuExport.apt's GenericButton (the bar) calls _root.PlaySound("Gui_ShellMapMouseOver") in its `_over` frame
## (the pointer on it) and _root.PlaySound("Gui_ShellMapSelect") in its `_down` frame (the press); a navigation's OpenButton (GenericButton_Med
## _wArrow) goes to its `_over` (the same hover sound) when the pointer opens the navigation. They reach the game as FSCommand PlaySound
## (RW 0x815772). A navigation's entries (MenuExport.apt's menu1_sub01) ask for no sound in RotWK: their button only shows `_over` and, on the
## release, `_down` and OnButtonClick; the check holds them to that silence. A sound counts when the menu asked for it exactly once
## (game.gd shell_sounds_played) AND the audio manager plays it (its handle is a playing instance within WAIT_PLAYING frames: a request is
## queued first and starts on an audio update).
## Prints "MENUSND <name>: ok" / "MENUSND <name>: FAIL <why>" and "MENUSND RESULT: <n> ok, <m> failed"; run() returns the failures.
extends RefCounted

const OVER := "Gui_ShellMapMouseOver"
const SELECT := "Gui_ShellMapSelect"
const FIRST_HANDLE := 5 # AHSV_FirstHandle: the handles below it are refusals (error, no sound, muted, not for the local player, music stops)
const WAIT_PLAYING := 10 # frames the request may take to start (the audio manager's next updates)
const REST := Vector2(512, 740) # the pointer's resting place: no button of the main menu there
## the navigations and the entries a player can press (BonusCampaign shows its disabled button in RotWK 2.01, not listed)
const ENTRIES := {
	"SoloPlayNav": ["Expansion1Campaign", "WarOfTheRing", "Skirmish", "LoadGame"],
	"MultiPlayNav": ["Online", "LocalNetwork", "Replay"],
	"OptionsNav": ["Settings", "AdvancedSettings", "Credits"],
}

var game: Node
var shell: Node
var ok_count := 0
var fail_count := 0


func run(p_game: Node) -> int:
	game = p_game
	shell = game._shell
	await game._step(10)
	await game._wait_main_ready()
	await game._step(60)
	# MENUSND_CHECK_PART=options: only the Options bar (a short run, e.g. for a recording)
	if OS.get_environment("MENUSND_CHECK_PART") != "options":
		for nav in ENTRIES:
			await opener_check(nav)
			for entry in ENTRIES[nav]:
				await entry_check(nav, entry)
	await options_bar_check()
	check("the check checked something", ok_count > 0, "no check passed") # fails closed on an empty run
	print("MENUSND RESULT: %d ok, %d failed" % [ok_count, fail_count])
	return fail_count


func check(what: String, ok: bool, why: String = "") -> void:
	if ok:
		ok_count += 1
		print("MENUSND %s: ok" % what)
	else:
		fail_count += 1
		print("MENUSND %s: FAIL %s" % [what, why])


func top() -> String:
	var stack: Array = shell.shell_stack()
	return str(stack[-1]) if not stack.is_empty() else ""


## the requests for `name` in `sounds`
func requests(sounds: Array, name: String) -> Array:
	return sounds.filter(func(s): return s.name == name)


## true when, over the whole WAIT_PLAYING window, the menu asked for `name` exactly once since `sounds` was cleared and the audio manager played that
## request (the request is queued, then started by an audio update; a second request later in the window fails it). `sounds` is
## game.shell_sounds_played itself, so every request made while watching counts
func played_once(name: String) -> bool:
	var started := false
	for i in WAIT_PLAYING:
		var asked := requests(game.shell_sounds_played, name)
		if asked.size() == 1 and int(asked[0].handle) >= FIRST_HANDLE and game._audio.is_playing(int(asked[0].handle)):
			started = true
		await game._step(1)
	return started and requests(game.shell_sounds_played, name).size() == 1


func names(sounds: Array) -> String:
	return ", ".join(sounds.map(func(s): return "%s(%d)" % [s.name, int(s.handle)]))


func move(p: Vector2) -> void:
	game._mouse_event(p, false, false)


## a fresh main menu: the shell back to MainMenu.apt, the pointer at rest
func main_menu() -> int:
	for i in 20:
		if top() == "MainMenu.apt":
			break
		shell.shell_pop()
		await game._step(30)
	move(REST)
	await game._step(60)
	return shell.shell_top_level()


## a navigation: the pointer on it opens its list (MainMenu.apt: the navigation's unnamed button, rollOver -> gotoAndPlay("_show")); the `_show`
## frame sends its OpenButton (GenericButton_Med _wArrow) to `_over`, whose action plays Gui_ShellMapMouseOver
func opener_check(nav: String) -> void:
	var level := await main_menu()
	var b: Dictionary = shell.find_button(level, nav)
	if not b.found:
		check("%s" % nav, false, "no button")
		return
	game.shell_sounds_played.clear()
	move(Vector2(b.x, b.y))
	var hovered: bool = await played_once(OVER)
	check("%s: the pointer on it plays %s" % [nav, OVER], hovered, names(game.shell_sounds_played))
	move(REST)
	await game._step(30)


## an entry of an open navigation: the pointer on it and its press ask for no sound (RotWK's menu1_sub01 has none)
func entry_check(nav: String, entry: String) -> void:
	var level := await main_menu()
	check("%s: the main menu is up" % entry, top() == "MainMenu.apt", str(shell.shell_stack()))
	var opener: Dictionary = shell.find_button(level, nav)
	if not opener.found:
		check("%s.%s" % [nav, entry], false, "no button of " + nav)
		return
	move(Vector2(opener.x, opener.y))
	await game._step(90) # the pointer on the navigation opens its list
	var b: Dictionary = shell.find_button(level, nav + "." + entry)
	if not b.found:
		check("%s.%s" % [nav, entry], false, "the entry is not on the open navigation")
		return
	var p := Vector2(b.x, b.y)
	game.shell_sounds_played.clear()
	move(p)
	await game._step(20)
	var hover: Array = game.shell_sounds_played.duplicate()
	check("%s.%s: the pointer on it asks for no sound (as in RotWK)" % [nav, entry], requests(hover, SELECT).is_empty() and requests(hover, OVER).is_empty(),
		names(hover))
	game.shell_sounds_played.clear()
	game._mouse_event(p, true, true)
	await game._step(WAIT_PLAYING)
	var press: Array = game.shell_sounds_played.duplicate()
	check("%s.%s: the press asks for no sound (as in RotWK)" % [nav, entry], requests(press, SELECT).is_empty() and requests(press, OVER).is_empty(), names(press))
	# the release away from the entry: no click (the next entry starts from the main menu again)
	move(REST)
	await game._step(2)
	game._mouse_event(REST, false, true)
	await game._step(30)


func options_bar_check() -> void:
	var level := await main_menu()
	for b in ["OptionsNav", "OptionsNav.Settings"]:
		if not await game._click_button(level, b):
			check("open Options", false, "cannot press " + b)
			return
		await game._step(60)
	for i in 30:
		if top() == "Options.apt" and game._display_screen.is_open():
			break
		await game._step(15)
	check("Options.apt is up on the OpenBFME menu", top() == "Options.apt" and game._display_screen.is_open(), str(shell.shell_stack()))
	await game._step(30)
	var options: int = shell.shell_top_level()
	# RESET TO DEFAULTS keeps the menu up; CANCEL closes it (the page's own events, AptOptionsScreen::mouseFilter)
	for name in ["Default", "Cancel"]:
		var b: Dictionary = shell.find_button(options, "Buttons." + name)
		if not b.found:
			check("Options %s" % name, false, "no button Buttons." + name)
			continue
		var p := Vector2(b.x, b.y)
		game.shell_sounds_played.clear()
		move(p)
		var hovered: bool = await played_once(OVER)
		await game._step(10)
		hovered = hovered and requests(game.shell_sounds_played, OVER).size() == 1 # still one, just before the press
		check("Options %s: the pointer on it plays %s" % [name, OVER], hovered, names(game.shell_sounds_played))
		game.shell_sounds_played.clear()
		game._mouse_event(p, true, true)
		var pressed: bool = await played_once(SELECT)
		await game._step(4)
		pressed = pressed and requests(game.shell_sounds_played, SELECT).size() == 1 # still one, just before the release
		check("Options %s: the press plays %s" % [name, SELECT], pressed, names(game.shell_sounds_played))
		game.shell_sounds_played.clear()
		game._mouse_event(p, false, true)
		await game._step(60)
		var release: Array = game.shell_sounds_played.duplicate()
		check("Options %s: the release asks for no second click" % name, requests(release, SELECT).is_empty(), names(release))
	for i in 20:
		if top() == "MainMenu.apt":
			break
		await game._step(15)
	check("Options CANCEL closed the menu", top() == "MainMenu.apt", str(shell.shell_stack()))

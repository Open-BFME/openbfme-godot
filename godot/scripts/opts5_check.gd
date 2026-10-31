## Lane OPTS-5: game.gd --opts5-check (with --options-file: a file of the check's own), the one Options menu as a player uses it, at the window's size
## (--opts5-size=WxH):
##   * the main menu's SETTINGS and CUSTOM SETTINGS open the OpenBFME menu at once (the detail settings first for CUSTOM SETTINGS); none of the retail
##     page's controls (its three settings frames, the CUSTOM SETTINGS button) and no OPENBFME button can be pressed;
##   * every tab: its rows inside the panel and above the help line, no two gadgets or labels meeting, each label fitting its room (measured with the
##     menus' own font), the open drop-down drawn last; every row has a help line, the original game's rows the string file's text;
##   * the original game's settings through their rows, read back from the engine: the brightness's gamma while the slider moves, the music volume,
##     the detail level and a custom setting (the texture reduction), the scroll speed, Show All Health Bars; ACCEPT writes Options.ini in the original
##     game's keys and closes Options; the menu opens again on the saved values; CANCEL puts the gamma and the volume back and writes nothing; RESET TO
##     DEFAULTS gives the original game's defaults.
## run_game (--opts5-game with --auto): the quit menu's Options opens the menu over the game, the detail rows greyed with the game's own reason.
## Prints "OPTS5 <name>: ok" / "OPTS5 <name>: FAIL <why>" and "OPTS5 RESULT: <n> ok, <m> failed"; tests/opts5_test.gd runs it headless; a windowed run with
## --opts5-shots=DIR writes opts5-<tab>-<w>x<h>.png and the entry points.
extends RefCounted

## Fixed horizontal bounds; each page reports its actual help line, whose rule is four pixels above it.
const PANEL := Rect2(305, 182, 622, 358)
const LABEL_FONT := "Albertus MT"
const LABEL_SIZE := 16
## the original game's rows and the tab each is on (the layout of docs/OPTIONS.md)
const RETAIL_TABS := {
	"Retail.Brightness": "Display", "Retail.Detail": "Graphics", "Retail.AdvancedOption0": "Graphics", "Retail.AdvancedOption1": "Graphics",
	"Retail.AdvancedOption2": "Graphics", "Retail.AdvancedOption3": "Graphics", "Retail.AdvancedOption4": "Graphics", "Retail.AdvancedOption5": "Graphics",
	"Retail.AdvancedOption6": "Graphics", "Retail.AdvancedOption7": "Graphics", "Retail.AdvancedOption8": "Graphics", "Retail.MusicVolume": "Audio",
	"Retail.SoundFxVolume": "Audio", "Retail.VoiceVolume": "Audio", "Retail.AmbientVolume": "Audio", "Retail.MovieVolume": "Audio", "Retail.EAX3": "Audio",
	"Retail.HighAudioQuality": "Audio", "Retail.ScrollSpeed": "Camera", "Retail.HealthBars": "Interface", "Retail.OnlineIp": "Online",
	"Retail.OnlinePortNum": "Online", "Retail.SendDelay": "Online", "Retail.RefreshNat": "Online",
}

var game: Node
var shell: Node
var screen: CanvasLayer
var options: Node
var shots := ""
var ok_count := 0
var fail_count := 0
var size_tag := ""


func run(p_game: Node, p_shots: String) -> int:
	game = p_game
	shell = game._shell
	screen = game._display_screen
	options = game._game_options
	shots = p_shots
	check("initial main-menu movie is still pending", not shell.has_level(shell.shell_top_level()), str(shell.shell_stack()))
	game._frames = game.LEVEL_MAIN_MENU_FRAMES # first-run setup and intro movies can consume the host's initial frames
	game._process(0.0)
	check("a pending main-menu movie is not marked revealed", not game._menu_revealed, str(game._menu_revealed))
	var root: Window = game.get_tree().root
	if game._opts5_size != Vector2i.ZERO:
		root.mode = Window.MODE_WINDOWED
		root.size = game._opts5_size
	elif DisplayServer.get_name() == "headless" and root.size.x < 640:
		root.size = Vector2i(1280, 800)
	await game._step(10)
	size_tag = "%dx%d" % [root.size.x, root.size.y]
	await game._wait_main_ready()
	await game._step(60)
	var main_menu_shown: Dictionary = shell.get_member(shell.shell_top_level(), "", "MainMenuShown")
	check("loaded main menu is revealed", game._menu_revealed and main_menu_shown.get("found", false) and main_menu_shown.get("value", "") == "true",
		str(main_menu_shown))
	var file: String = game._options_file
	check("the check has an Options.ini of its own", not file.is_empty() and not file.ends_with("/Options.ini"), file)

	# 0. a real ACCEPT with no edit leaves the file's bytes alone (the file holds every key RotWK's Save writes, in its own format): no default
	# OpenBFME key and no AlternateMouseSetup is added (Sol review r1)
	var untouched := FileAccess.get_file_as_string(file)
	if not await open_options("OptionsNav.Settings"):
		return finish()
	await click_bar("accept")
	await game._step(90)
	var after_noop := FileAccess.get_file_as_string(file)
	check("ACCEPT with no edit leaves Options.ini byte for byte as it was", after_noop == untouched, "was:\n%s\nnow:\n%s" % [untouched, after_noop])
	await game._wait_main_ready()

	# 1. the main menu's SETTINGS: the menu at once, nothing of the retail page
	if not await open_options("OptionsNav.Settings"):
		return finish()
	check("SETTINGS opens the OpenBFME menu on its Display tab", screen.current_tab() == "Display" and shell.options_page() == 3, "tab %s page %d" % [screen.current_tab(), shell.options_page()])
	retail_unreachable("SETTINGS")
	await shot("opts5-entry-main-menu")

	# 2. every tab: the layout rules and the help lines
	for tab in screen.TABS:
		await click_tab(tab)
		await game._step(6)
		await layout_check(tab)
		await shot("opts5-%s" % tab.to_lower())

	# 3. the original game's settings through their rows, read back from the engine
	await effects_check(file)

	# 4. the main menu's CUSTOM SETTINGS: the menu on its detail settings
	if await open_options("OptionsNav.AdvancedSettings"):
		check("CUSTOM SETTINGS opens the menu on its Graphics tab (the detail settings)", screen.current_tab() == "Graphics", screen.current_tab())
		retail_unreachable("CUSTOM SETTINGS")
		await shot("opts5-entry-custom-settings")
		await click_bar("cancel")
		await game._step(90)
	return finish()


## --opts5-game (with --auto): the menu over a live game
func run_game(p_game: Node, p_shots: String) -> int:
	game = p_game
	shell = game._shell
	screen = game._display_screen
	options = game._game_options
	shots = p_shots
	var root: Window = game.get_tree().root
	if game._opts5_size != Vector2i.ZERO:
		root.mode = Window.MODE_WINDOWED
		root.size = game._opts5_size
		await game._step(30)
	elif DisplayServer.get_name() == "headless" and root.size.x < 640:
		root.size = Vector2i(1280, 800)
		await game._step(10)
	size_tag = "%dx%d" % [root.size.x, root.size.y]
	check("a live game with its HUD", game._hud != null and game._state == game.State.PLAYING, "state %d" % game._state)
	await game._press_escape()
	await game._wait_seconds(3.0)
	if not await game._click_quit("Options"):
		check("the quit menu's Options", false, "no Options button")
		return finish()
	for i in 40:
		if top() == "Options.apt" and screen.is_open():
			break
		await game._step(10)
	check("the quit menu's Options opens the OpenBFME menu over the game", top() == "Options.apt" and screen.is_open() and shell.options_page() == 3, str(shell.shell_stack()))
	if not screen.is_open():
		return finish()
	retail_unreachable("the quit menu's Options")
	await game._step(30)
	await shot("opts5-entry-in-game")
	await click_tab("Graphics")
	var rows: Dictionary = page().get("rows", {})
	var cannot: String = str(shell.fetch_text("APT:CannotChangeGraphics").text)
	for key in ["Retail.Detail", "Retail.AdvancedOption2", "Retail.AdvancedOption6"]:
		var r = rows.get(key)
		check("in a game %s is greyed with the game's reason ('%s')" % [key, cannot], r != null and not r.enabled and str(r.help).ends_with(cannot), str(r))
	var choice: Dictionary = shell.openbfme_page_choose("Retail.Detail", 3)
	check("in a game the detail level is refused", not choice.get("ok", true), str(choice))
	await shot("opts5-in-game-graphics")
	await click_tab("Audio")
	rows = page().get("rows", {})
	check("in a game the volumes may change", rows.has("Retail.MusicVolume") and rows["Retail.MusicVolume"].enabled, str(rows.get("Retail.MusicVolume")))
	# a click on the Music track over the paused game: the menu's (the HUD hands it over), the slider moves and the music volume follows
	if rows.has("Retail.MusicVolume") and rows["Retail.MusicVolume"].has("rect"):
		var before := int(rows["Retail.MusicVolume"].value)
		var r: Rect2 = rows["Retail.MusicVolume"].rect
		await click_stage(Vector2(r.position.x + r.size.x * 0.05, r.get_center().y))
		var now := int(page().rows["Retail.MusicVolume"].value)
		check("in a game a click on the Music track moves it (%d -> %d) and sets the music volume" % [before, now], now != before and game._audio != null
			and is_equal_approx(game._audio.get_volume("music"), now / 100.0), "music %s" % str(game._audio.get_volume("music") if game._audio != null else "no audio"))
	await game._step(10)
	await key_escape()
	await game._wait_seconds(2.0)
	check("Esc closes Options (the quit menu stays)", not screen.is_open() and top() != "Options.apt" and top() != "", str(shell.shell_stack()))
	return finish()


# ---------------------------------------------------------------------------------------------------------------------------------
# the parts
# ---------------------------------------------------------------------------------------------------------------------------------
## no control of the retail page can be pressed: the hittable buttons of Options.apt are its bottom bar's only (CANCEL, RESET TO DEFAULTS, ACCEPT)
func retail_unreachable(what: String) -> void:
	var level: int = shell.shell_top_level()
	var others := PackedStringArray()
	for b in shell.list_buttons(level):
		var path := String(b.path)
		if b.hittable and not path.contains("Buttons."):
			others.append(path)
	check("%s: only the bottom bar of Options.apt can be pressed (no retail page, no CUSTOM SETTINGS, no OPENBFME button)" % what, others.is_empty()
		and not screen.entry_visible() and shell.options_page() == 3, str(others))


func layout_check(tab: String) -> void:
	var st := page()
	check("%s: the page reports its footer position" % tab, st.has("help_top"), str(st))
	if not st.has("help_top"):
		return
	var help_top := float(st.help_top)
	check("%s: the two-line footer fits above the bottom bar" % tab, help_top >= 540 and help_top + 2 + 2 * 14 <= 602, str(help_top))
	var panel := Rect2(PANEL.position, Vector2(PANEL.size.x, help_top - 4 - PANEL.position.y))
	var rows: Dictionary = st.get("rows", {})
	var keys: Array = rows.keys()
	if tab == "Controls":
		check("Controls: the hot key list fits the panel", st.has("list") and panel.encloses(st.list), str(st.get("list")))
		return
	check("%s: the tab has rows" % tab, not keys.is_empty(), "")
	var bad := PackedStringArray()
	var boxes := []
	for key in keys:
		var r: Dictionary = rows[key]
		if r.has("rect"):
			if not panel.encloses(r.rect):
				bad.append("%s's gadget %s is outside the panel %s" % [key, r.rect, panel])
			boxes.append([key, r.rect])
		if r.has("label_rect"):
			var lr: Rect2 = r.label_rect
			if not panel.encloses(lr):
				bad.append("%s's label room %s is outside the panel" % [key, lr])
			boxes.append([key + " (label)", lr])
			var w: float = float(shell.font_metrics(LABEL_FONT, LABEL_SIZE, false, str(r.label)).width)
			if w > lr.size.x:
				bad.append("%s's label '%s' is %.1f wide, its room %.1f" % [key, r.label, w, lr.size.x])
	for i in boxes.size():
		for j in range(i + 1, boxes.size()):
			if (boxes[i][1] as Rect2).intersects(boxes[j][1]):
				bad.append("%s meets %s" % [boxes[i][0], boxes[j][0]])
	check("%s at %s: every row inside the panel above the help line, no two gadgets or labels meeting, every label fitting its room" % [tab, size_tag], bad.is_empty(), "; ".join(bad))
	# the help line of each row under the pointer: never empty; the original game's rows start with the string file's text
	var missing := PackedStringArray()
	for key in keys:
		var r: Dictionary = rows[key]
		if not r.has("label_rect"):
			continue
		game._mouse_event((r.label_rect as Rect2).get_center(), false, false)
		await game._step(3)
		var help := str(page().get("help", ""))
		if help.is_empty() or help != str(r.help):
			missing.append("%s: '%s'" % [key, help])
		elif String(key).begins_with("Retail."):
			var label := str(shell.options_retail_rows()[key].help_label)
			var parts := PackedStringArray()
			for part in str(shell.fetch_text(label).text).replace("\\n", "\n").split("\n", false):
				if not part.strip_edges().is_empty():
					parts.append(part.strip_edges())
			var text := " ".join(parts)
			if not help.begins_with(text):
				missing.append("%s: '%s' is not %s's '%s'" % [key, help, label, text])
	check("%s: every row's help line shows under the pointer (the original game's from its string file)" % tab, missing.is_empty(), "; ".join(missing))
	for key in RETAIL_TABS:
		if RETAIL_TABS[key] == tab:
			check("%s is on the %s tab" % [key, tab], rows.has(key), str(keys))


func effects_check(file: String) -> void:
	var audio: Node = game._audio
	var saved_text := FileAccess.get_file_as_string(file)
	# Brightness: the gamma at once while it moves (RW 0x91F90D), nothing written
	await click_tab("Display")
	await select("Retail.Brightness", 80)
	check("Brightness 80 sets the display's gamma at once (1.6)", is_equal_approx(options.gamma_in_effect, 1.6) and options._gamma_layer.visible, str(options.gamma_in_effect))
	check("nothing is written before ACCEPT", FileAccess.get_file_as_string(file) == saved_text, "")
	await click_tab("Audio")
	await select("OpenBFMEMasterVolume", 60) # another edit: the pending brightness stays in effect (Sol review r1)
	check("another edit keeps the brightness preview (gamma 1.6)", is_equal_approx(options.gamma_in_effect, 1.6), str(options.gamma_in_effect))
	await select("Retail.MusicVolume", 30)
	check("Music 30 sets the music volume at once", audio != null and is_equal_approx(audio.get_volume("music"), 0.3), str(audio.get_volume("music") if audio != null else "no audio"))
	await click_tab("Graphics")
	await select("Retail.Detail", 3) # GUI:Low
	await select("Retail.AdvancedOption6", 0) # Texture Quality Low
	check("a custom setting makes the level Custom (the Graphics list shows it)", int(page().rows["Retail.Detail"].value) == 5, str(page().rows["Retail.Detail"]))
	await click_tab("Camera")
	await select("Retail.ScrollSpeed", 70)
	await click_tab("Interface")
	await select("Retail.HealthBars", 1)
	var bars: Dictionary = shell.options_retail_rows()["Retail.HealthBars"]
	check("Show All Health Bars ticks (allowed by the pending custom Decal Detail)", int(bars.value) == 1 and bars.enabled, str(bars))
	await click_bar("accept")
	await game._step(90)
	check("ACCEPT closes Options", not screen.is_open() and top() != "Options.apt", str(shell.shell_stack()))
	var lines := FileAccess.get_file_as_string(file).replace("\r", "").split("\n")
	for want in ["Brightness = 80", "MusicVolume = 30.000000", "StaticGameLOD = Custom", "TextureQualityLOD = Low", "ScrollFactor = 70", "AllHealthBars = yes"]:
		check("Options.ini holds %s (the original game's key and format)" % want, lines.has(want), "")
	var detail: Dictionary = shell.get_detail_state()
	check("the detail level is applied (Custom, texture reduction 2)", int(detail.level) == 5 and int(detail.texture_reduction) == 2
		and ClassDB.class_call_static("GameWorld", "get_texture_reduction") == 2, JSON.stringify(detail).left(200))
	check("the saved brightness stays in effect", is_equal_approx(options.gamma_in_effect, 1.6), str(options.gamma_in_effect))
	# again: the saved values; CANCEL puts back the gamma and the volume, writes nothing
	if not await open_options("OptionsNav.Settings"):
		return
	var after_accept := FileAccess.get_file_as_string(file)
	var retail: Dictionary = shell.options_retail_rows()
	check("the menu opens on the saved values", int(retail["Retail.Brightness"].value) == 80 and int(retail["Retail.MusicVolume"].value) == 30
		and int(retail["Retail.Detail"].value) == 5 and int(retail["Retail.AdvancedOption6"].value) == 0 and int(retail["Retail.HealthBars"].value) == 1, "")
	await click_tab("Display")
	await select("Retail.Brightness", 20)
	await click_tab("Audio")
	await select("Retail.MusicVolume", 90)
	check("the moved values are in effect", is_equal_approx(audio.get_volume("music"), 0.9) and is_equal_approx(options.gamma_in_effect, 1.0 - 30 * 0.008), "")
	await click_bar("cancel")
	await game._step(90)
	check("CANCEL closes Options and puts the gamma and the music volume back", not screen.is_open() and is_equal_approx(options.gamma_in_effect, 1.6)
		and is_equal_approx(audio.get_volume("music"), 0.3), "gamma %.3f music %.2f" % [options.gamma_in_effect, audio.get_volume("music")])
	check("CANCEL writes nothing", FileAccess.get_file_as_string(file) == after_accept, "")
	# RESET TO DEFAULTS: the original game's defaults at once (brightness 50: gamma 1), the level the game takes; CANCEL then undoes it
	if not await open_options("OptionsNav.Settings"):
		return
	await click_bar("reset")
	await game._step(10)
	retail = shell.options_retail_rows()
	# (Show All Health Bars is read from Options.ini again, not cleared: RW 0x91F754 -> RW 0x91EBD1)
	check("RESET TO DEFAULTS: brightness 50 (gamma 1 at once), the level the game takes (Ultra High), health bars as saved", int(retail["Retail.Brightness"].value) == 50
		and is_equal_approx(options.gamma_in_effect, 1.0) and int(retail["Retail.Detail"].value) == 0 and int(retail["Retail.HealthBars"].value) == 1,
		"%s %s %s gamma %.2f" % [retail["Retail.Brightness"].value, retail["Retail.Detail"].value, retail["Retail.HealthBars"].value, options.gamma_in_effect])
	await click_bar("cancel")
	await game._step(90)
	check("CANCEL after RESET: the saved gamma again, nothing written", is_equal_approx(options.gamma_in_effect, 1.6) and FileAccess.get_file_as_string(file) == after_accept, "")


# ---------------------------------------------------------------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------------------------------------------------------------
## the main menu's OptionsNav -> `button`, then the OpenBFME menu (Options.apt opens on it)
func open_options(button: String) -> bool:
	await game._wait_main_ready()
	var main_level: int = shell.shell_top_level()
	for b in ["OptionsNav", button]:
		if not await game._click_button(main_level, b):
			check("open Options (%s)" % button, false, "cannot press " + b)
			return false
		await game._step(60)
	for i in 30:
		if top() == "Options.apt" and screen.is_open():
			break
		await game._step(15)
	await game._step(30)
	check("%s opens Options on the OpenBFME menu" % button, top() == "Options.apt" and screen.is_open(), str(shell.shell_stack()))
	return screen.is_open()


func top() -> String:
	var stack: PackedStringArray = shell.shell_stack()
	return "" if stack.is_empty() else stack[-1]


func page() -> Dictionary:
	return shell.openbfme_page_state()


func select(key: String, value: int) -> void:
	var r: Dictionary = shell.openbfme_page_choose(key, value)
	if not r.get("ok", false):
		check("choose %s = %d" % [key, value], false, str(r.get("error", "?")))
	await game._step(4)


func click_stage(p: Vector2) -> void:
	game._mouse_event(p, false, false)
	await game._step(2)
	game._mouse_event(p, true, true)
	await game._step(2)
	game._mouse_event(p, false, true)
	await game._step(6)


func click_tab(tab: String) -> void:
	var tabs: Array = page().get("tabs", [])
	var i: int = screen.TABS.find(tab)
	if i < 0 or i >= tabs.size():
		check("the tab %s is on the page" % tab, false, str(tabs))
		return
	await click_stage((tabs[i] as Rect2).get_center())
	await game._step(6)
	if screen.current_tab() != tab:
		check("the tab %s shows" % tab, false, screen.current_tab())


## Options.apt's own bottom bar button (cancel, reset, accept): the movie's button, its click taken by the page
func click_bar(which: String) -> void:
	var buttons: Dictionary = page().get("buttons", {})
	if not buttons.has(which):
		check("the bottom bar's %s is on screen" % which, false, str(buttons.keys()))
		return
	await click_stage((buttons[which] as Rect2).get_center())
	await game._step(10)


func key_escape() -> void:
	for pressed in [true, false]:
		var e := InputEventKey.new()
		e.keycode = KEY_ESCAPE
		e.physical_keycode = KEY_ESCAPE
		e.pressed = pressed
		Input.parse_input_event(e)
		await game._step(3)
	await game._step(6)


func shot(name: String) -> void:
	if shots.is_empty() or DisplayServer.get_name() == "headless":
		return
	await game._step(8)
	var image := game.get_viewport().get_texture().get_image()
	DirAccess.make_dir_recursive_absolute(shots)
	var path := shots.path_join("%s-%s.png" % [name, size_tag])
	print("OPTS5 shot %s: %s" % [path.get_file(), error_string(image.save_png(path))])


func check(name: String, ok: bool, why: String) -> void:
	if ok:
		ok_count += 1
		print("OPTS5 %s: ok" % name)
	else:
		fail_count += 1
		print("OPTS5 %s: FAIL %s" % [name, why])


func finish() -> int:
	print("OPTS5 RESULT: %d ok, %d failed" % [ok_count, fail_count])
	return fail_count

## Lane OPTS-2: game.gd --opts2-check (with --options-file: a file of the check's own, never the player's Options.ini), the Options menu as a player
## uses it, with mouse events on the screens' buttons:
##   * RotWK's detail level in effect at start (AptMenuPlayer get_detail_state: GameLODManager::init) and its texture reduction handed to the W3D
##     textures; Options.apt's basic and advanced pages (the proof screenshots);
##   * the OpenBFME page (lane OPTS-3: a page of Options.apt made of RotWK's gadgets, AptMenuPlayer.openbfme_page_*): every tab clicked, each extra
##     changed through its gadget and applied at once (the viewport's anti-aliasing and anisotropy, the look, the master bus), the movie's own ACCEPT
##     writing Options.ini (the retail lines the file had kept byte for byte, every new key "OpenBFME"-prefixed or one of the three retail keys the page
##     mirrors) and going back to the basic page, CANCEL putting back what was in effect, RESET TO DEFAULTS;
##   * no label text is green: the colours the page's text is drawn in (AptMenuPlayer.drawn_texts), and (in a window) the pixels of every text of every tab.
## Prints "OPTS2 <name>: ok" / "OPTS2 <name>: FAIL <why>" and "OPTS2 RESULT: <n> ok, <m> failed"; run() returns the failures. tests/opts2_test.gd runs it
## headless; a windowed run with --opts2-shots=DIR writes the screenshots at the window's size (opts2-<tab>-<w>x<h>.png, opts2-options-*.png).
extends RefCounted

var game: Node
var shell: Node
var screen: CanvasLayer
var options: Node
var shots := ""
var ok_count := 0
var fail_count := 0
var green_pixels_checked := 0


func run(p_game: Node, p_shots: String) -> int:
	game = p_game
	shell = game._shell
	screen = game._display_screen
	options = game._game_options
	shots = p_shots
	var root: Window = game.get_tree().root
	if DisplayServer.get_name() == "headless" and root.size.x < 640:
		root.size = Vector2i(1280, 800) # a headless window is 64 x 64: the stage needs room for the clicks
	if game._opts2_size != Vector2i.ZERO:
		root.mode = Window.MODE_WINDOWED
		root.size = game._opts2_size
	await game._step(10)
	await game._wait_main_ready()
	await game._step(60)
	var file: String = game._options_file
	check("the check has an Options.ini of its own", not file.is_empty() and not file.ends_with("/Options.ini"), file)
	var before_text := FileAccess.get_file_as_string(file)

	# 1. RotWK's detail level in effect from the start
	var detail: Dictionary = shell.get_detail_state()
	check("a detail level is in effect at start (GameLODManager::init)", detail.get("ok", false) and int(detail.level) >= 0, JSON.stringify(detail))
	var want_level: String = str(shell.get_option("StaticGameLOD")) if shell.get_option("StaticGameLOD") != null else ""
	if want_level.is_empty():
		check("without StaticGameLOD the port takes its stand-in for the hardware detection and says so (S-2481)", str(detail.stop).begins_with("[S-2481]"),
			str(detail.stop))
	else:
		check("the level is Options.ini's StaticGameLOD (%s)" % want_level, str(detail.level_name).to_lower() == want_level.to_lower(), str(detail.level_name))
	check("the W3D textures take the level's texture reduction (%d)" % int(detail.texture_reduction),
		ClassDB.class_call_static("GameWorld", "get_texture_reduction") == int(detail.texture_reduction), str(ClassDB.class_call_static("GameWorld", "get_texture_reduction")))

	# 2. Options.apt, both pages, and the OPENBFME button over it
	var main_level: int = shell.shell_top_level()
	for b in ["OptionsNav", "OptionsNav.Settings"]:
		if not await game._click_button(main_level, b):
			check("open Options", false, "cannot press " + b)
			return finish()
		await game._step(60)
	for i in 30:
		if top() == "Options.apt" and screen.entry_visible():
			break
		await game._step(15)
	check("Options.apt is up with the OPENBFME button", top() == "Options.apt" and screen.entry_visible(), str(shell.shell_stack()))
	await game._step(120)
	await shot("opts2-options-basic")
	await _green_image_check("Options.apt's basic page")
	if await game._click_named_button("Advanced"):
		await game._step(150)
		await shot("opts2-options-advanced")
		check("Options.apt's advanced page opens (the detail level's nine settings)", true, "")
		if await game._click_named_button("Done"):
			await game._step(200)
			await game._wait_main_ready()
	else:
		check("Options.apt's advanced page opens", false, "no Advanced button")
	# Done saved and closed Options (RW 0x9205A3 with AdvancedOnly 0 goes back to the basic page, which Save then closes); open it again
	if top() != "Options.apt":
		await game._wait_main_ready()
		main_level = shell.shell_top_level()
		for b in ["OptionsNav", "OptionsNav.Settings"]:
			await game._click_button(main_level, b)
			await game._step(60)
		for i in 30:
			if top() == "Options.apt" and screen.entry_visible():
				break
			await game._step(15)
		await game._step(120)
	var retail_after_apt := FileAccess.get_file_as_string(file)

	# 3. the OpenBFME page: the tabs (lane OPTS-3: a page of Options.apt, its tabs clicked as a player clicks them)
	await click_stage(screen.entry_rect().get_center())
	await game._step(10)
	check("the page is the shell's own drawing (no Godot Control of the former screen)", screen.get_child_count() == 0, str(screen.get_children()))
	check("the OPENBFME button opens the page on its Display tab", screen.is_open() and screen.current_tab() == "Display" and page().get("open", false),
		"open %s tab %s" % [screen.is_open(), screen.current_tab()])
	if not screen.is_open():
		return finish()
	for tab in screen.TABS:
		await click_tab(tab)
		await game._step(20)
		# the texts the shell draws from now on are this tab's: the pointer moves onto another tab (it lights up), so the page is drawn again
		shell.drawn_texts(true)
		var tabs: Array = page().get("tabs", [])
		if tabs.size() == screen.TABS.size():
			game._mouse_event((tabs[(screen.TABS.find(tab) + 1) % tabs.size()] as Rect2).get_center(), false, false)
		await game._step(8)
		check("the %s tab shows" % tab, screen.current_tab() == tab and int(page().get("tab", -1)) == screen.TABS.find(tab), screen.current_tab())
		await shot("opts2-%s-%dx%d" % [tab.replace("+", "plus").to_lower(), root.size.x, root.size.y])
		_text_colours_check(tab)
		await _green_pixels_check(tab)
	check("the Controls tab lists the CommandMap's hot keys (%d) in a list box" % screen.hotkey_count(), screen.hotkey_count() >= 20 and page().has("list"),
		str(screen.hotkey_count()))

	# 4. the extras through their gadgets, applied at once
	await click_tab("Graphics+")
	await select("OpenBFMEAntiAliasing", 3) # MSAA 4x
	check("anti-aliasing MSAA 4x is applied to the window", root.msaa_3d == Viewport.MSAA_4X and screen.extra_value("OpenBFMEAntiAliasing") == "msaa4", str(root.msaa_3d))
	await select("OpenBFMEAntiAliasing", 1) # FXAA
	check("anti-aliasing FXAA is applied", root.screen_space_aa == Viewport.SCREEN_SPACE_AA_FXAA and root.msaa_3d == Viewport.MSAA_DISABLED, str(root.screen_space_aa))
	await select("OpenBFMEAnisotropic", 4) # 16x
	check("anisotropic filtering 16x is applied", root.anisotropic_filtering_level == Viewport.ANISOTROPY_16X, str(root.anisotropic_filtering_level))
	await select("OpenBFMELook", 1)
	var env: Environment = game._env.environment
	check("the enhanced look: AgX tone mapping and glow", env.tonemap_mode == Environment.TONE_MAPPER_AGX and env.glow_enabled, "tonemap %d glow %s" % [env.tonemap_mode, env.glow_enabled])
	await click_tab("Audio")
	await select("OpenBFMEMasterVolume", 50)
	var master := AudioServer.get_bus_index("Master")
	check("master volume 50% is the master bus's", is_equal_approx(AudioServer.get_bus_volume_db(master), linear_to_db(0.5)), str(AudioServer.get_bus_volume_db(master)))
	await click_tab("Interface")
	await select("OpenBFMEHealthBars", 2)
	await click_row("OpenBFMEGameClock")
	check("the game clock box ticks (a click on the retail check box)", screen.extra_value("OpenBFMEGameClock") == "yes", str(screen.extra_value("OpenBFMEGameClock")))
	await click_tab("Camera")
	await select("ScrollFactor", 70)
	await select("OpenBFMEEdgeScroll", 1)
	check("the camera's values are pending", screen.extra_value("ScrollFactor") == "70" and screen.extra_value("OpenBFMEEdgeScroll") == "always",
		"%s %s" % [screen.extra_value("ScrollFactor"), screen.extra_value("OpenBFMEEdgeScroll")])
	await game._step(10)
	await shot("opts2-changed-camera")
	await click_bar("accept")
	check("ACCEPT closes the page, back on Options' basic page", not screen.is_open() and top() == "Options.apt" and not page().get("open", true)
		and shell.options_page() == 1 and screen.entry_visible(), "open %s, stack %s, page %d" % [screen.is_open(), shell.shell_stack(), shell.options_page()])

	# 5. Options.ini: the new entries carry the prefix (or are the mirrored retail keys), the retail lines that were there are unchanged
	var after := FileAccess.get_file_as_string(file)
	var before_lines := _lines(retail_after_apt)
	var after_lines := _lines(after)
	var changed := PackedStringArray()
	for line in after_lines:
		if before_lines.has(line):
			continue
		var key := line.get_slice(" = ", 0)
		if not key.begins_with("OpenBFME") and not options.RETAIL_KEYS.has(key):
			changed.append(line)
	check("every entry the screen wrote is OpenBFME-prefixed or a mirrored retail key", changed.is_empty(), str(changed))
	var lost := PackedStringArray()
	for line in before_lines:
		var key := line.get_slice(" = ", 0)
		if not options.RETAIL_KEYS.has(key) and not after_lines.has(line):
			lost.append(line)
	check("the other retail entries are unchanged (%d)" % before_lines.size(), lost.is_empty(), str(lost))
	for pair in [["OpenBFMEAntiAliasing", "fxaa"], ["OpenBFMEAnisotropic", "16x"], ["OpenBFMELook", "enhanced"], ["OpenBFMEMasterVolume", "50"],
			["OpenBFMEHealthBars", "always"], ["OpenBFMEGameClock", "yes"], ["ScrollFactor", "70"], ["OpenBFMEEdgeScroll", "always"]]:
		check("Options.ini holds %s = %s" % pair, after_lines.has("%s = %s" % pair), "")
	check("the file is sorted ZH-style (\"key = value\" lines in key order)", _sorted(after_lines), "")

	# 6. CANCEL puts back what was in effect; RESET TO DEFAULTS gives retail behaviour
	await click_stage(screen.entry_rect().get_center())
	await click_tab("Graphics+")
	await select("OpenBFMEAntiAliasing", 3)
	await select("OpenBFMELook", 0)
	await click_bar("cancel")
	check("CANCEL restores the anti-aliasing and the look", root.screen_space_aa == Viewport.SCREEN_SPACE_AA_FXAA and root.msaa_3d == Viewport.MSAA_DISABLED
		and env.tonemap_mode == Environment.TONE_MAPPER_AGX, "aa %d msaa %d tonemap %d" % [root.screen_space_aa, root.msaa_3d, env.tonemap_mode])
	check("CANCEL writes nothing", FileAccess.get_file_as_string(file) == after, "")
	await click_stage(screen.entry_rect().get_center())
	await click_bar("reset")
	await click_bar("accept")
	var reset := _lines(FileAccess.get_file_as_string(file))
	check("RESET TO DEFAULTS then ACCEPT: retail behaviour (no anti-aliasing, the original look, the full master volume)", reset.has("OpenBFMEAntiAliasing = off")
		and reset.has("OpenBFMELook = original") and reset.has("OpenBFMEMasterVolume = 100") and root.msaa_3d == Viewport.MSAA_DISABLED
		and root.screen_space_aa == Viewport.SCREEN_SPACE_AA_DISABLED and env.tonemap_mode == Environment.TONE_MAPPER_LINEAR, str(reset))
	print("OPTS2 file before %d bytes, after %d bytes" % [before_text.length(), FileAccess.get_file_as_string(file).length()])
	return finish()


## --opts2-game (with --auto): the settings in a live game; the test's Options.ini holds the values it checks for (opts2_test.gd)
func run_game(p_game: Node, p_shots: String) -> int:
	game = p_game
	shell = game._shell
	screen = game._display_screen
	options = game._game_options
	shots = p_shots
	var root: Window = game.get_tree().root
	if DisplayServer.get_name() == "headless" and root.size.x < 640:
		root.size = Vector2i(1280, 800)
		await game._step(10)
	if game._opts2_size != Vector2i.ZERO:
		root.mode = Window.MODE_WINDOWED
		root.size = game._opts2_size
		await game._step(30)
	var hud: Node = game._hud
	check("a live game with its HUD", hud != null and game._state == game.State.PLAYING, "state %d" % game._state)
	if hud == null:
		return finish()
	var values: Dictionary = options.current()
	var detail: Dictionary = shell.get_detail_state()
	await game._step(20)
	var clock: Label = options._clock
	check("the game clock is shown over the game (%s)" % clock.text, options._clock_layer.visible and clock.text.contains(":"), "visible %s text '%s'" % [options._clock_layer.visible, clock.text])
	var view: Dictionary = hud.set_view_options(options.view_options(values))
	check("the camera scrolls with Options.ini's ScrollFactor %s (factor %.2f)" % [values.ScrollFactor, view.scroll_factor],
		is_equal_approx(float(view.scroll_factor), values.ScrollFactor.to_int() * 0.02), str(view))
	check("edge scrolling: %s" % values.OpenBFMEEdgeScroll, int(view.edge_scroll_mode) == {"retail": 0, "always": 1, "off": 2}[values.OpenBFMEEdgeScroll], str(view))
	check("the interface scale reaches the HUD (%s%%)" % values.OpenBFMEUIScale, is_equal_approx(float(view.ui_scale), values.OpenBFMEUIScale.to_int() / 100.0), str(view))
	var fx = game._world.get_fx_player()
	var stats: Dictionary = fx.get_stats() if fx != null and fx.has_method("get_stats") else {}
	check("the live FX player runs with the level's particle limits (%d, priority %d)" % [int(detail.max_particle_count), int(detail.min_particle_priority)],
		fx != null and int(stats.get("max_particles", -1)) == int(detail.max_particle_count) and int(stats.get("min_particle_priority", -1)) == int(detail.min_particle_priority),
		str(stats))
	check("the level %s is in effect in the game (texture reduction %d)" % [detail.level_name, detail.texture_reduction],
		ClassDB.class_call_static("GameWorld", "get_texture_reduction") == int(detail.texture_reduction), "")
	check("Brightness %s draws the gamma pass (gamma %.2f)" % [str(shell.get_option("Brightness")), float(detail.display_gamma)],
		options._gamma_layer.visible == (float(detail.display_gamma) != 1.0), "visible %s" % options._gamma_layer.visible)
	var icons: Dictionary = hud.get_icon_ui() if hud.has_method("get_icon_ui") else {}
	print("OPTS2 icon ui: ", JSON.stringify(icons).left(400))
	await shot("opts2-game-%dx%d" % [root.size.x, root.size.y])
	return finish()


# ---------------------------------------------------------------------------------------------------------------------------------
# no green label text
# ---------------------------------------------------------------------------------------------------------------------------------
static func is_green(c: Color) -> bool:
	# a clearly green colour: the green channel leads the other two by a quarter and is not dark
	return c.g > 0.3 and c.g > c.r * 1.25 and c.g > c.b * 1.25


## lane OPTS-3: the page's texts as the shell drew them (AptMenuPlayer.drawn_texts: the gadgets' and the page's own), none in a green colour
func _page_texts() -> Array:
	var out := []
	for e in shell.drawn_texts(false):
		if String(e.source) == "gadget" and not String(e.text).strip_edges().is_empty():
			out.append(e)
	return out


func _text_colours_check(tab: String) -> void:
	var bad := PackedStringArray()
	var texts := _page_texts()
	for e in texts:
		if is_green(e.color):
			bad.append("'%s' %s" % [e.text, e.color])
	check("no text colour of the %s tab is green (%d texts)" % [tab, texts.size()], bad.is_empty() and texts.size() >= 3, ", ".join(bad))


## lane OPTS-2 r2: the retail page's text (UI-4's TextField.textColor read, RW 0xB040C9, made its audio / network labels pale): no green anywhere in the picture
func _green_image_check(what: String) -> void:
	if DisplayServer.get_name() == "headless":
		return
	await RenderingServer.frame_post_draw
	var image: Image = game.get_viewport().get_texture().get_image()
	var green := 0
	for y in range(0, image.get_height(), 2):
		for x in range(0, image.get_width(), 2):
			var p := image.get_pixel(x, y)
			if is_green(p) and p.g > 0.45: # text is drawn bright; the page's dim background art has faint green-grey tints (g 0.30 .. 0.35)
				green += 1
	check("no bright green pixel on %s (every second pixel of %dx%d)" % [what, image.get_width(), image.get_height()], green == 0, "%d green" % green)


func _green_pixels_check(tab: String) -> void:
	if DisplayServer.get_name() == "headless":
		return # no picture to read (the colour check above still ran)
	await RenderingServer.frame_post_draw
	var image: Image = game.get_viewport().get_texture().get_image()
	if image == null:
		check("the %s tab's picture can be read" % tab, false, "no image")
		return
	var texts := _page_texts()
	var green := 0
	var pixels := 0
	for e in texts:
		var rect := Rect2i(e.window).intersection(Rect2i(Vector2i.ZERO, image.get_size()))
		for y in range(rect.position.y, rect.end.y):
			for x in range(rect.position.x, rect.end.x):
				var p := image.get_pixel(x, y)
				pixels += 1
				if is_green(p):
					green += 1
	green_pixels_checked += pixels
	check("no green pixel in the %s tab's text (%d texts, %d pixels)" % [tab, texts.size(), pixels], green == 0 and pixels > 0, "%d green" % green)


# ---------------------------------------------------------------------------------------------------------------------------------
func _lines(text: String) -> PackedStringArray:
	var out := PackedStringArray()
	for l in text.split("\n"):
		l = l.trim_suffix("\r") # the file's CR LF (RotWK's text mode stream)
		if not l.is_empty():
			out.append(l)
	return out


func _sorted(lines: PackedStringArray) -> bool:
	for i in range(1, lines.size()):
		# ZH writes a std::map<AsciiString> in strcmp order
		if _strcmp(lines[i - 1].get_slice(" = ", 0), lines[i].get_slice(" = ", 0)) > 0:
			return false
	return true


static func _strcmp(a: String, b: String) -> int:
	var x := a.to_utf8_buffer()
	var y := b.to_utf8_buffer()
	for i in mini(x.size(), y.size()):
		if x[i] != y[i]:
			return x[i] - y[i]
	return x.size() - y.size()


func top() -> String:
	var stack: PackedStringArray = shell.shell_stack()
	return stack[-1] if not stack.is_empty() else ""


## lane OPTS-3: the page's state (AptMenuPlayer.openbfme_page_state: stage rectangles of its tabs, rows and the bottom bar)
func page() -> Dictionary:
	return shell.openbfme_page_state()


## a choice / slider value through the gadget, the messages a click on it sends (the drop-down's geometry is the gadget layer's)
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


func click_row(key: String) -> void:
	var rows: Dictionary = page().get("rows", {})
	if not rows.has(key) or not rows[key].has("rect"):
		check("the row %s is on the page" % key, false, str(rows.keys()))
		return
	await click_stage((rows[key].rect as Rect2).get_center())


## Options.apt's own bottom bar button (cancel, reset, accept): the movie's button, its click taken by the page
func click_bar(which: String) -> void:
	var buttons: Dictionary = page().get("buttons", {})
	if not buttons.has(which):
		check("the bottom bar's %s is on screen" % which, false, str(buttons.keys()))
		return
	await click_stage((buttons[which] as Rect2).get_center())
	await game._step(10)


func shot(name: String) -> void:
	if shots.is_empty() or DisplayServer.get_name() == "headless":
		return
	await RenderingServer.frame_post_draw
	var image: Image = game._capture_image(name)
	if image == null:
		return
	DirAccess.make_dir_recursive_absolute(shots)
	var path := shots.path_join(name + ".png")
	print("OPTS2 screenshot %s -> %s" % [path, error_string(image.save_png(path))])


func check(name: String, ok: bool, why: String) -> void:
	if ok:
		ok_count += 1
		print("OPTS2 %s: ok" % name)
	else:
		fail_count += 1
		print("OPTS2 %s: FAIL %s" % [name, why])


func finish() -> int:
	print("OPTS2 RESULT: %d ok, %d failed" % [ok_count, fail_count])
	return fail_count

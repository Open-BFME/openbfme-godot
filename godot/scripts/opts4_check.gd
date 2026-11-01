## Lane OPTS-4: game.gd --opts4-check (with --options-file: a file of the check's own), three Options bugs the owner found in v0.4.0-alpha.3, checked in
## the running game at the window's size (--opts4-size=WxH; the owner played at 2560x1440):
##   1. an open drop-down list is drawn after everything else of the page and takes the clicks inside it (every drop-down of every OpenBFME tab is opened;
##      "Look" over "Interface scale" is clicked through), Options.apt's own Graphics list too;
##   2. a slider's tick boxes stay inside its window (Options.apt's seven sliders, the OpenBFME Audio page's Master volume), in window pixels;
##   3. a volume slider acts while it moves: the audio manager's volume follows, the request log (OPENBFME_AUDIO_LOG) has the volume lines and the
##      sample's request, CANCEL puts the saved volume back.
## Prints "OPTS4 <name>: ok" / "OPTS4 <name>: FAIL <why>" and "OPTS4 RESULT: <n> ok, <m> failed"; run() returns the failures. tests/opts4_test.gd runs it
## headless at 1024x768 and 2560x1440; a windowed run with --opts4-shots=DIR writes opts4-<bug>-<w>x<h>.png.
## Lane OPTS-5: Options is the OpenBFME menu: the original game's sliders and its Graphics list are rows of the menu (Display, Audio, Camera,
## Graphics), driven by the retail handlers; the checks run on them.
extends RefCounted

const FILL := "AptHSliderOnBar"
const THUMB := "AptHSliderOnBarEnd"
## the open Graphics list's texts, in draw order: the combo box's own (the level in effect) and the six levels (GUI:UltraHigh .. GUI:VeryLow, GUI:Custom)
const GRAPHICS_LIST := ["High", "Ultra High", "High", "Medium", "Low", "Very Low", "Custom"]

var game: Node
var shell: Node
var screen: CanvasLayer
var shots := ""
var ok_count := 0
var fail_count := 0
var size_tag := ""


func run(p_game: Node, p_shots: String) -> int:
	game = p_game
	shell = game._shell
	screen = game._display_screen
	shots = p_shots
	var root: Window = game.get_tree().root
	if game._opts4_size != Vector2i.ZERO:
		root.mode = Window.MODE_WINDOWED
		root.size = game._opts4_size
	elif DisplayServer.get_name() == "headless" and root.size.x < 640:
		root.size = Vector2i(1280, 800)
	await game._step(10)
	size_tag = "%dx%d" % [root.size.x, root.size.y]
	await game._wait_main_ready()
	await game._step(60)
	if not await open_options():
		return finish()
	# the original game's seven sliders and OpenBFME's two, by tab
	for t in [["Display", 2], ["Audio", 6], ["Camera", 1]]:
		await click_tab(t[0])
		await sliders_check("the menu's %s tab" % t[0], t[1])
	await click_tab("Audio")
	await shot("opts4-2-options-sliders")
	await audio_check()
	if not await open_options():
		return finish()
	await retail_list_check()
	await page_check()
	return finish()


func open_options() -> bool:
	await game._wait_main_ready()
	var main_level: int = shell.shell_top_level()
	for b in ["OptionsNav", "OptionsNav.Settings"]:
		if not await game._click_button(main_level, b):
			check("open Options", false, "cannot press " + b)
			return false
		await game._step(60)
	for i in 30:
		if top() == "Options.apt" and screen.is_open():
			break
		await game._step(15)
	check("Options.apt is up on the OpenBFME menu", top() == "Options.apt" and screen.is_open(), str(shell.shell_stack()))
	await game._step(30)
	return screen.is_open()


# ---- 2: the sliders ------------------------------------------------------------------------------------------------------------------------
## the draw list's sliders: root index -> { rect, name, boxes: [Rect2], thumbs: [Rect2] }
func sliders() -> Dictionary:
	var out := {}
	for c in shell.gadget_draw_list():
		if c.kind != "image" or int(c.root) < 0 or (c.image != FILL and c.image != THUMB):
			continue
		if not out.has(c.root):
			out[c.root] = {"rect": c.root_rect, "stage": c.root_stage_rect, "name": "", "boxes": [], "thumbs": []}
		if c.image == FILL:
			out[c.root].boxes.append(c.rect)
			out[c.root].name = c.window
		else:
			out[c.root].thumbs.append(c.rect)
	return out


func sliders_check(what: String, want: int) -> void:
	await game._step(4)
	var found := sliders()
	check("%s shows %d sliders at %s" % [what, want, size_tag], found.size() == want, "%d: %s" % [found.size(), found.values().map(func(s): return s.name)])
	for s in found.values():
		var r: Rect2 = s.rect
		var worst := 0.0
		for b in s.boxes:
			worst = maxf(worst, maxf(r.position.y - b.position.y, b.end.y - r.end.y))
			worst = maxf(worst, maxf(r.position.x - b.position.x, b.end.x - r.end.x))
		check("%s: the %d tick boxes of %s stay inside its window at %s" % [what, s.boxes.size(), s.name, size_tag], worst <= 0.01 and not s.boxes.is_empty(),
			"%.2f px outside (window %s)" % [worst, r])
		var thumb_ok: bool = s.thumbs.size() == 1 and absf(s.thumbs[0].position.y - r.position.y) <= 0.01 and absf(s.thumbs[0].end.y - r.end.y) <= 0.01
		check("%s: %s has its thumb, as tall as the tick row" % [what, s.name], thumb_ok, str(s.thumbs))


## the drawn slider of the page row `key` (its gadget's stage box is the row's)
func slider_named(key: String) -> Dictionary:
	var row = page().get("rows", {}).get(key)
	if row == null or not row.has("rect"):
		return {}
	for s in sliders().values():
		if (s.stage as Rect2).is_equal_approx(row.rect):
			return s
	return {}


# ---- 3: the volumes --------------------------------------------------------------------------------------------------------------------------
func log_lines() -> Array:
	var path := OS.get_environment("OPENBFME_AUDIO_LOG")
	var out := []
	for l in FileAccess.get_file_as_string(path).split("\n"):
		var f := l.split("\t")
		if f.size() >= 12 and f[0] != "ms":
			out.append({"line": f[2], "event": f[6], "origin": f[10], "outcome": f[11]})
	return out


func audio_check() -> void:
	var audio: Node = game._audio
	var path := OS.get_environment("OPENBFME_AUDIO_LOG")
	check("the request log is written from the menus on (OPENBFME_AUDIO_LOG)", not path.is_empty() and FileAccess.file_exists(path), path)
	if audio == null or path.is_empty():
		return
	var sfx := slider_named("Retail.SoundFxVolume")
	var voice := slider_named("Retail.VoiceVolume")
	var movie := slider_named("Retail.MovieVolume")
	check("the Sound FX, Voice and Movie sliders are drawn", not sfx.is_empty() and not voice.is_empty() and not movie.is_empty(), str(sliders().values().map(func(s): return s.name)))
	if sfx.is_empty() or voice.is_empty() or movie.is_empty():
		return
	var saved: float = audio.get_volume("sound")
	var before := log_lines().size()
	# a click on the Sound FX track, far left: the volume drops at once and the sample is asked for
	var r: Rect2 = sfx.stage
	await click_stage(Vector2(r.position.x + r.size.x * 0.1, r.get_center().y))
	await game._step(6)
	var now: float = audio.get_volume("sound")
	var lines := log_lines().slice(before)
	var volumes := lines.filter(func(l): return l.line == "volume" and l.event == "SOUNDFX")
	var samples := lines.filter(func(l): return l.line == "request" and l.origin == "options VolumeSampleSoundFX")
	check("Options.ini's saved volume is in effect from the start (SFXVolume = 80)", is_equal_approx(saved, 0.8), str(saved))
	# a track click pages the thumb a fifth of the track towards the pointer (RW 0x7234EC): 80 -> 58
	check("a click on the Sound FX track lowers the sound volume at once (%.2f -> %.2f)" % [saved, now], is_equal_approx(snappedf(now, 0.01), 0.58), "")
	check("the log has the volume change", volumes.size() == 1 and is_equal_approx(str(volumes[0].outcome).to_float(), snappedf(now, 0.01)), str(volumes))
	check("the log has the sample's request (VolumeSampleSoundFX)", samples.size() == 1, str(lines))
	# the Voice thumb dragged in three steps: a volume line per step while the button is down
	before = log_lines().size()
	var thumb: Vector2 = shell.window_to_stage((voice.thumbs[0] as Rect2).get_center())
	game._mouse_event(thumb, false, false)
	await game._step(2)
	game._mouse_event(thumb, true, true)
	await game._step(2)
	var heard := []
	var shown := []
	for step in 3:
		thumb.x -= 14.0
		game._mouse_event(thumb, false, false)
		await game._step(3)
		heard.append(snappedf(audio.get_volume("voice"), 0.01))
		shown.append(snappedf(int(page().rows["Retail.VoiceVolume"].value) / 100.0, 0.01))
	lines = log_lines().slice(before)
	volumes = lines.filter(func(l): return l.line == "volume" and l.event == "VOICE")
	samples = lines.filter(func(l): return l.line == "request" and l.origin == "options VolumeSampleVoice")
	check("the voice volume follows the thumb while it is dragged (%s, the slider shows %s)" % [str(heard), str(shown)], heard == shown
		and heard[0] > heard[1] and heard[1] > heard[2], "") # tolerance: each step lower than the last (the drag's direction)
	check("the log has a voice volume line per step", volumes.size() == 3, str(volumes))
	check("the voice sample is asked for once while the slider moves (it does not restart while it sounds)", samples.size() == 1 and samples[0].event == "VolumeSampleVoice", str(lines))
	await shot("opts4-3-voice-slider-dragged")
	game._mouse_event(thumb, false, true)
	await game._step(4)
	# the Movie slider is live as well
	before = log_lines().size()
	r = movie.stage
	await click_stage(Vector2(r.position.x + r.size.x * 0.1, r.get_center().y))
	await game._step(6)
	lines = log_lines().slice(before)
	check("the Movie slider sets the movie volume and asks for its sample",
		lines.filter(func(l): return l.line == "volume" and l.event == "MOVIE").size() == 1
		and lines.filter(func(l): return l.line == "request" and l.origin == "options VolumeSampleMovie").size() == 1, str(lines))
	# CANCEL: what was saved is in effect again
	before = log_lines().size()
	await click_bar("cancel")
	await game._step(200)
	check("CANCEL closes Options", top() != "Options.apt" and not screen.is_open(), str(shell.shell_stack()))
	lines = log_lines().slice(before)
	check("CANCEL puts the saved sound volume back (%.2f)" % saved, is_equal_approx(audio.get_volume("sound"), saved), str(audio.get_volume("sound")))
	check("the log has CANCEL's five volume lines", lines.filter(func(l): return l.line == "volume").size() == 5, str(lines))


# ---- 1: the drop-downs -------------------------------------------------------------------------------------------------------------------------
## the open drop-down's commands are the last block of the draw list; returns its texts ([] when no list is open)
func lone_texts(what: String) -> Array:
	var list: Array = shell.gadget_draw_list()
	var first := -1
	var last_other := -1
	var texts := []
	for i in list.size():
		if list[i].lone:
			if first < 0:
				first = i
			if list[i].kind == "text":
				texts.append(list[i])
		else:
			last_other = i
	if first < 0:
		return []
	check("%s: the open list is drawn after everything else at %s" % [what, size_tag], last_other < first, "command %d of %d is drawn after the list's first (%d)" % [last_other, list.size(), first])
	return texts


func retail_list_check() -> void:
	# the original game's Graphics list (the menu's Retail.Detail row): open, on top, closed again by a click beside it
	await click_tab("Graphics")
	var detail := Rect2()
	var row = page().get("rows", {}).get("Retail.Detail")
	if row != null and row.has("rect"):
		detail = row.rect
	check("the Graphics list is drawn", detail.size.x > 0, "")
	if detail.size.x <= 0:
		return
	await click_stage(Vector2(detail.end.x - 12, detail.get_center().y))
	var texts := lone_texts("Options.apt's Graphics list")
	check("Options.apt's Graphics list opens with its six levels", texts.map(func(t): return t.text) == GRAPHICS_LIST, str(texts.map(func(t): return t.text)))
	await shot("opts4-1-options-graphics-list")
	await click_stage(Vector2(detail.position.x - 60, detail.position.y - 40))
	check("a click beside it closes the list", lone_texts("closed").is_empty(), "")


func page_check() -> void:
	check("the OpenBFME page is up", screen.is_open(), "")
	if not screen.is_open():
		return
	await click_tab("Audio")
	await shot("opts4-2-openbfme-master-volume")
	# every drop-down of every tab: opened by its arrow, on top, closed again without a choice
	var opened := 0
	for tab in screen.TABS:
		await click_tab(tab)
		var rows: Dictionary = page().get("rows", {})
		for key in rows.keys():
			var row: Dictionary = rows[key]
			if int(row.get("items", 0)) <= 0 or not row.get("enabled", false) or not row.has("rect"):
				continue
			var r: Rect2 = row.rect
			await click_stage(Vector2(r.end.x - 12, r.get_center().y))
			var texts := lone_texts("%s / %s" % [tab, key])
			if texts.is_empty():
				check("%s / %s: its list opens" % [tab, key], false, "no open list")
				continue
			opened += 1
			await click_stage(Vector2(r.end.x - 12, r.get_center().y))
			if not lone_texts("closed").is_empty():
				await click_stage(Vector2(30, 200)) # beside every gadget
			check("%s / %s: its list closes without a choice" % [tab, key], lone_texts("closed").is_empty() and int(page().rows[key].value) == int(row.value), "")
	# lane OPTS-5: Display 6, Graphics 8 (the level, the four custom settings OpenBFME draws, ShadowLOD's included, anti-aliasing, anisotropic,
	# look), Audio 1, Camera 4, Interface 3, Online 1 (Social.hidden); the Online tab's address list is greyed from the main menu
	check("every drop-down of the menu (23 on its tabs) was opened", opened == 23, str(opened))
	# the owner's case: "Look" open over the row below (Soft particles); its second entry, at a point inside the row below, is chosen
	await click_tab("Graphics")
	var rows: Dictionary = page().get("rows", {})
	if not rows.has("OpenBFMELook") or not rows.has("OpenBFMESoftParticles"):
		check("Graphics has the Look and Soft particles rows", false, str(rows.keys()))
		return
	var look: Rect2 = rows["OpenBFMELook"].rect
	var below: Rect2 = rows["OpenBFMESoftParticles"].rect
	below = Rect2(Vector2(look.position.x, below.position.y), Vector2(look.size.x, below.size.y)) # the row's band under the list
	var scale_before := str(screen.extra_value("OpenBFMESoftParticles"))
	await click_stage(Vector2(look.end.x - 12, look.get_center().y))
	var texts := lone_texts("Graphics / Look over Soft particles")
	var enhanced := Rect2()
	for t in texts:
		if t.text == "Enhanced":
			enhanced = t.rect
	check("Look's list shows Original and Enhanced", texts.map(func(t): return t.text) == ["Original", "Original", "Enhanced"] and enhanced != Rect2(), str(texts.map(func(t): return t.text)))
	await shot("opts4-1-look-list-open")
	var at: Vector2 = shell.window_to_stage(enhanced.position + Vector2(20, 2))
	at.y = maxf(at.y, below.position.y + 2)
	check("the entry Enhanced lies over the Soft particles row", below.has_point(at), "%s not in %s" % [at, below])
	await click_stage(at)
	check("a click on the entry over the row below chooses Enhanced", str(screen.extra_value("OpenBFMELook")) == "enhanced", str(screen.extra_value("OpenBFMELook")))
	check("the row below is untouched", str(screen.extra_value("OpenBFMESoftParticles")) == scale_before and lone_texts("closed").is_empty(), str(screen.extra_value("OpenBFMESoftParticles")))
	await click_bar("cancel")


# ---------------------------------------------------------------------------------------------------------------------------------
func top() -> String:
	var stack: PackedStringArray = shell.shell_stack()
	return stack[-1] if not stack.is_empty() else ""


func page() -> Dictionary:
	return shell.openbfme_page_state()


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
	var path := shots.path_join("%s-%s.png" % [name, size_tag])
	print("OPTS4 screenshot %s -> %s" % [path, error_string(image.save_png(path))])


func check(name: String, ok: bool, why: String) -> void:
	if ok:
		ok_count += 1
		print("OPTS4 %s: ok" % name)
	else:
		fail_count += 1
		print("OPTS4 %s: FAIL %s" % [name, why])


func finish() -> int:
	print("OPTS4 RESULT: %d ok, %d failed" % [ok_count, fail_count])
	return fail_count

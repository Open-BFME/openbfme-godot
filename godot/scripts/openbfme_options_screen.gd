## Lane OPTS-1: the OpenBFME options (owner, 2026-10-10: "the game also needs like modern options"). NOT RETAIL: RotWK's Options.apt stays as
## retail's; these are this rebuild's own settings, reached from it.
##
## Lane OPTS-3 (owner, preview.4 at 2560 x 1440: the former screen "looks low resolution vs the rest of the menus"): the settings are a page of
## Options.apt drawn by the shell itself (AptMenuPlayer openbfme_page_*, engine GameClient/GUI/AptScreens/OpenBFMEOptionsPage.h): RotWK's own APT
## gadgets (window\apt\combobox.wnd, checkbox.wnd, horzslider.wnd, listbox.wnd) and Options.apt's frame, fonts, title and bottom bar, rasterised at
## the window's pixels like every menu. The former screen was Godot Controls laid out on the 1024 x 768 stage and scaled up as a whole (a Control
## scale draws the 16 px fonts at 16 px and stretches the result: the blur the owner saw); nothing of it remains. The OPENBFME button is a copy of
## Options.apt's CUSTOM SETTINGS button in the free corner above CANCEL (it never covers retail content); this script only describes the rows of the
## chosen tab (_spec) and acts on the page's events (_process).
##
## Lane OPTS-2: the tabs Display (OPTS-1's settings, scripts/display_settings.gd, openbfme-display.cfg), Graphics+, Camera, Interface, Audio
## (scripts/game_options.gd: Options.ini's OpenBFME* keys, plus the retail keys ScrollFactor, AllHealthBars and AlternateMouseSetup, which RotWK's own
## Options screen shows too) and Controls (the CommandMap's hot keys, read only). Every change applies at once; ACCEPT keeps them and writes both
## files, CANCEL and Esc put back what was in effect when the page opened, RESET TO DEFAULTS sets the defaults (retail behaviour; applied, kept on
## ACCEPT). ACCEPT and CANCEL go back to Options' basic page (the page is part of the Options screen, like its CUSTOM SETTINGS page).
## Lane OPTS-5 (owner, 2026-10-14: "scrap the original settings options and rework the menu to be our menu for now, but put the settings for graphics
## and options from the original game in places that make sense for it in the new menu"): with AptOptionsScreen::kOpenBFMEMenu on (the switch, in
## engine GameClient/GUI/AptScreens/AptSimpleScreens.h) this page IS the Options screen: every opening of Options.apt shows it at once (event "entry",
## value 1 = the detail settings first), the retail page and the OPENBFME button never show. The original game's settings are rows of the tabs
## (keys "Retail.<gadget>", described by the shell: options_retail_rows; the shell runs their retail handlers itself, live as retail does), and
## ACCEPT / CANCEL / RESET TO DEFAULTS are retail's Save / Cancel / Reset too (openbfme_menu_finish / openbfme_menu_reset) besides this script's
## own rows. Every row's help line: the original game's from its string file (the shell's table, AptOptionsScreen::retailRowTable), ours from HELP.
## The page never calls into the shell's other screens: a window change made through it cannot change a widget's state (the owner's "maximising
## greys out the main menu"; tests/opts1_test.gd checks the main menu's and Options.apt's buttons across every mode change).
extends CanvasLayer

const DisplaySettings := preload("res://scripts/display_settings.gd")
const TABS := ["Display", "Graphics", "Audio", "Camera", "Interface", "Online", "Performance", "Controls"]
## lane OPTS-5: the tab the menu shows first when the main menu's CUSTOM SETTINGS opened it
const DETAIL_TAB := "Graphics"
## lane OPTS-5: the help lines of OpenBFME's own rows and the tabs' notes (NOT RETAIL; one table: the original game's rows take theirs from the game's
## string file, AptOptionsScreen::retailRowTable in the engine). Key: the row's key, or "note:<tab>".
const HELP := {
	"window_mode": "Windowed, a borderless window over the whole screen, or exclusive full screen (Alt+Enter).",
	"resolution_windowed": "The window's size (in a window). The menus and the game scale with it.",
	"resolution_screen": "The window's size. Full screen and the borderless window use the monitor's own resolution; lower the render scale for fewer pixels.",
	"vsync": "Waits for the monitor's refresh: no tearing, a little more input delay.",
	"max_fps": "The most frames drawn a second. The game itself always runs at its own pace.",
	"show_fps": "The frame counter in the top left corner.",
	"render_scale": "The 3D view is drawn at this share of the window's pixels, then scaled up; the menus and the interface stay sharp.",
	"scaler_full": "How a render scale below 100% is scaled up. At 100% the scaler does nothing (FSR 2 still smooths edges).",
	"scaler_scaled": "How a render scale below 100% is scaled up. FSR keeps more detail than bilinear.",
	"OpenBFMEWidescreen": "Original: the original game's view, which shows less from top to bottom on screens wider than 4:3. Wider: the 4:3 view from top to bottom, more to the sides.",
	"OpenBFMEAntiAliasing": "Smooths the edges of the 3D view. MSAA: models; FXAA and TAA: the whole picture.",
	"OpenBFMEAnisotropic": "Anisotropic filtering: keeps the models' textures sharp at a slant.",
	"OpenBFMELook": "Enhanced: AgX tone mapping and a glow on the brightest effects. Original: the game's own picture.",
	"OpenBFMESoftParticles": "Smoke, fire and dust fade where they meet the ground instead of a hard cut. Off: the original look.",
	"OpenBFMEUIScale": "Enlarges the health bars, the frame counter and the game clock for large screens; the menus already scale with the window.",
	"OpenBFMEUberPerformance": "Not compatible with the original game: uses every CPU core; games and replays then only work with players who have it on too. From the next game.",
	"OpenBFMEEdgeScroll": "Scrolling with the mouse at the screen's edge. The original game does it in full screen only.",
	"OpenBFMERotateSpeed": "How fast the camera turns.",
	"OpenBFMEMaxZoomOut": "How many times higher than the original game the camera can rise (a game starts at the original height).",
	"OpenBFMEFreeCamera": "Lets the camera rise up to the size of the map; Ctrl+Z turns it on and off in a game.",
	"OpenBFMEInvertDragScroll": "Dragging with the right button moves the view the other way.",
	"OpenBFMEConfineMouse": "In a game, in a window or a borderless window (exclusive full screen keeps it there anyway).",
	"OpenBFMEHealthBars": "Original: on the units you select or point at. Always: every unit's and building's bar, yours and the other players'.",
	"AlternateMouseSetup": "The original game's option (Options.ini AlternateMouseSetup; its own screen has no box for it): the left button gives orders.",
	"OpenBFMEGameClock": "The game's own time, in the top right corner.",
	"OpenBFMEMasterVolume": "Scales every sound of the game.",
	"OpenBFMEMuteUnfocused": "No sound while another window has the focus.",
	"OpenBFMEAudioDevice": "The speakers or headphones the game plays through.",
	"note:Display": "Renderer: %s (%s). Point at a setting for what it does.",
	"note:Graphics": "Left: the original game's detail settings (a level, or Custom). Right: OpenBFME's own.",
	"note:Audio": "Moving a volume plays a sample of it. The master volume scales them all.",
	"note:Camera": "None of these changes the game itself: the camera and the mouse only.",
	"note:Interface": "\"Selected and damaged\" and \"Always\" health bars are this rebuild's.",
	"note:Online": "The original game's network settings.",
	"note:Performance": "Uber performance mode changes how the game itself runs: read its warning.",
	"note:Controls": "The original game's hot keys (CommandMap.ini) as the game reads them. Changing them here comes later.",
}
## the retail keys this script writes itself (the original game's screen has no control for them); the shell's retail Save writes the others
const OWN_RETAIL_KEYS := ["AlternateMouseSetup"]
const SUBTITLE := "OPENBFME SETTINGS"
const WINDOW_MODE_LABELS := {"windowed": "Windowed", "borderless": "Borderless window", "fullscreen": "Fullscreen"}
const VSYNC_ITEMS := [["on", "On"], ["off", "Off"], ["adaptive", "Adaptive"]]
const SCALER_LABELS := {"bilinear": "Bilinear", "fsr": "AMD FSR 1", "fsr2": "AMD FSR 2"}
const RENDERER_LABELS := {"forward_plus": "Forward+", "mobile": "Mobile", "gl_compatibility": "Compatibility"}
const SCALE_STEP := 5

const AA_ITEMS := [["off", "Off"], ["fxaa", "FXAA"], ["msaa2", "MSAA 2x"], ["msaa4", "MSAA 4x"], ["taa", "TAA (temporal)"]]
const ANISO_ITEMS := [["off", "Off"], ["2x", "2x"], ["4x", "4x"], ["8x", "8x"], ["16x", "16x"]]
const LOOK_ITEMS := [["original", "Original"], ["enhanced", "Enhanced"]]
const UI_SCALE_ITEMS := [["100", "100%"], ["125", "125%"], ["150", "150%"], ["200", "200%"]]
const EDGE_ITEMS := [["retail", "Full screen only (original)"], ["always", "Always"], ["off", "Off"]]
const ROTATE_ITEMS := [["50", "50%"], ["75", "75%"], ["100", "100% (original)"], ["150", "150%"], ["200", "200%"]]
const ZOOM_ITEMS := [["no", "Off"], ["yes", "On: up to the map's size"]]
## lane ZOOM-1: the Max zoom-out (OpenBFMEMaxZoomOut): times the original game's highest camera
const MAX_ZOOM_ITEMS := [["1", "Original (1x)"], ["1.5", "1.5x"], ["2", "2x"], ["2.5", "2.5x"], ["3", "3x"]]
## lane UI-6: the Widescreen view (OpenBFMEWidescreen)
const WIDESCREEN_ITEMS := [["original", "Original"], ["wider", "Wider"]]
const BARS_ITEMS := [["retail", "Selected (original)"], ["damaged", "Selected and damaged"], ["always", "Always (all players)"]]

var shell: Node2D                  # the AptMenuPlayer
var hud_getter: Callable           # () -> the InGameHud node or null (unused since OPTS-3: the page is the shell's own, its input is the shell's)
var settings: RefCounted           # the settings in effect (display_settings.gd), shared with game.gd and the counter
var settings_path := ""
var overlay: CanvasLayer           # scripts/fps_overlay.gd
var skin_errors := PackedStringArray()
var game_options: Node             # lane OPTS-2: scripts/game_options.gd (Options.ini's settings in effect)
var audio_getter: Callable         # lane OPTS-4: () -> the GameAudio node or null (the Master volume's sample)

var _open := false
var _original: RefCounted          # the settings when the page opened (CANCEL)
var _runtime := {}                 # what the game ran with when the page opened (DisplaySettings.snapshot: CLI caps, the window's mode and place)
var _pending: RefCounted           # what the page shows and has applied
var _applied: RefCounted
var _tab := "Display"
var _extras_original := {}         # lane OPTS-2: Options.ini's values when the page opened (CANCEL)
var _extras := {}                  # what the extra tabs show and have applied
var _rows := {}                    # key -> the row (Dictionary) the page shows now
var _hotkeys := []                 # [[action, keys]] of the CommandMap (read once)
var _options_top := false
var _retail := {}                  # lane OPTS-5: the shell's rows of the original game's settings (options_retail_rows), read for each spec
var _menu := false                 # lane OPTS-5: the page is the Options screen (the shell's kOpenBFMEMenu)


func _ready() -> void:
	layer = 80
	process_mode = Node.PROCESS_MODE_ALWAYS
	if overlay != null:
		overlay.screen = self
	if shell != null and shell.has_method("set_openbfme_available"):
		shell.set_openbfme_available(true)
	else:
		skin_errors.append("the shell has no OpenBFME page (AptMenuPlayer.set_openbfme_available)")
	for e in skin_errors:
		printerr("GAME DISPLAY page: ", e)


func is_open() -> bool:
	return _open


## the OPENBFME button is on screen (Options.apt's basic page)
func entry_visible() -> bool:
	return shell != null and shell.openbfme_page_state().has("entry")


## the OPENBFME button's stage rectangle (Rect2() when it is not shown)
func entry_rect() -> Rect2:
	var s: Dictionary = shell.openbfme_page_state()
	return s.get("entry", Rect2())


## the window rectangle of a stage rectangle (tests click through it)
func stage_rect_to_window(r: Rect2) -> Rect2:
	var a: Vector2 = shell.stage_to_window(r.position)
	var b: Vector2 = shell.stage_to_window(r.end)
	return Rect2(a, b - a)


func _process(_delta: float) -> void:
	if shell == null:
		return
	var stack: PackedStringArray = shell.shell_stack()
	_options_top = not stack.is_empty() and stack[-1] == "Options.apt"
	if _open and not _options_top:
		close(false) # the shell left Options.apt (a game ended, the quit menu closed): the page goes with it
		return
	for e in shell.openbfme_page_events():
		match String(e.kind):
			"entry":
				open(int(e.value))
			"value":
				_on_value(String(e.key), int(e.value))
			"tab":
				if int(e.value) >= 0 and int(e.value) < TABS.size():
					_show_tab(TABS[int(e.value)])
			"cancel":
				close(false)
			"reset":
				reset_defaults()
			"accept":
				close(true)


func open(first_tab := 0) -> void:
	if _open:
		return
	var r: Dictionary = shell.openbfme_page_open()
	if not r.get("ok", false):
		printerr("GAME DISPLAY the OpenBFME page cannot open: ", r.get("error", "?"))
		return
	_open = true
	var root := get_tree().root
	_original = settings.copy()
	_runtime = DisplaySettings.snapshot(root)
	# the page shows what is in effect (a --max-fps cap, a window the player maximised or Alt+Enter made full screen), not only the file
	_pending = DisplaySettings.effective(settings, root, overlay.shown() if overlay != null else settings.show_fps)
	_applied = _pending.copy()
	if game_options != null:
		_extras_original = game_options.current()
		_extras = _extras_original.duplicate()
	_menu = bool(shell.openbfme_page_state().get("menu", false))
	if game_options != null:
		game_options.hold_gamma = _menu
	_show_tab(DETAIL_TAB if first_tab == 1 else "Display")
	print("GAME DISPLAY options screen open%s: %s" % [" (the Options menu)" if _menu else "", JSON.stringify(_pending.to_dict())])


## ACCEPT (keep = true): the settings shown become the ones in effect and are written; otherwise the ones of the opening are applied again.
func close(keep: bool) -> void:
	if not _open:
		return
	if game_options != null:
		game_options.hold_gamma = false # the saved brightness is back in effect from here (ACCEPT: the shell's Save; CANCEL: its Cancel)
	if keep:
		settings.from_dict(_pending.to_dict())
		if not settings_path.is_empty() and not settings.save_file(settings_path):
			printerr("GAME DISPLAY ", settings.errors[-1])
		print("GAME DISPLAY accepted and saved: ", JSON.stringify(settings.to_dict()))
		if game_options != null:
			var owned := _owned_extras()
			var w: Dictionary = game_options.commit(owned)
			if not w.get("ok", false):
				printerr("GAME OPTIONS Options.ini: ", w.get("error", "?"))
			print("GAME OPTIONS accepted and saved: ", JSON.stringify(owned))
			game_options.apply_global(game_options.current())
	else:
		# everything the game ran with at the opening comes back (not the file's values: a --max-fps cap or a maximised window stays as it was);
		# the file is untouched (the hotkeys pressed while the page was up changed only the page's settings)
		DisplaySettings.restore(get_tree().root, _runtime)
		settings.from_dict(_original.to_dict())
		print("GAME DISPLAY cancelled: %s" % JSON.stringify(DisplaySettings.snapshot(get_tree().root)))
		if game_options != null:
			game_options.apply_global(_extras_original)
	if overlay != null:
		overlay.preview = null
		overlay.refresh()
	_open = false
	_rows.clear()
	if _options_top and _menu:
		# lane OPTS-5: the original game's settings: RotWK's Save (written, applied) or Cancel (the volumes and the brightness back); the screen closes
		var r: Dictionary = shell.openbfme_menu_finish(keep)
		if not r.get("ok", false):
			printerr("GAME OPTIONS the Options menu: ", r.get("error", "?"))
	elif _options_top:
		shell.openbfme_page_close() # back to Options' basic page (the retail Options.ini values it mirrors are read again)


## lane OPTS-5: what this script writes into Options.ini: the keys the player changed (an unchanged key that the file does not hold stays absent, so an
## ACCEPT with no edit leaves the file's bytes alone); the retail keys the shell's Save writes are the shell's
func _owned_extras() -> Dictionary:
	var out := {}
	for key in _extras:
		if _menu and game_options.RETAIL_KEYS.has(key) and not OWN_RETAIL_KEYS.has(key):
			continue
		if _extras[key] != _extras_original.get(key):
			out[key] = _extras[key]
	return out


## A display setting changed on the page: applied at once
func set_value(key: String, value) -> void:
	if not _open:
		return
	_pending.from_dict({key: value})
	_apply_pending()
	_sync()


func reset_defaults() -> void:
	if not _open:
		return
	_pending = DisplaySettings.new()
	_apply_pending()
	if game_options != null:
		_extras = game_options.defaults()
		if _menu:
			# the retail keys the shell's Reset sets are not this script's (their values in effect stay until ACCEPT / CANCEL)
			for key in game_options.RETAIL_KEYS:
				if not OWN_RETAIL_KEYS.has(key):
					_extras[key] = _extras_original[key]
		_apply_extras()
	if _menu:
		var r: Dictionary = shell.openbfme_menu_reset() # lane OPTS-5: RotWK's Reset of the original game's settings
		if not r.get("ok", false):
			printerr("GAME OPTIONS the Options menu: ", r.get("error", "?"))
	_sync()


## lane OPTS-2: an extra (or mirrored retail) setting changed on the page: applied at once, written by ACCEPT
func set_extra(key: String, value: String) -> void:
	if not _open or game_options == null:
		return
	if not game_options.valid(key, value):
		printerr("GAME OPTIONS %s = '%s' is not a value it takes" % [key, value])
		return
	_extras[key] = value
	_apply_extras()
	_sync()


func extra_value(key: String):
	return _extras.get(key) if _open else null


func current_tab() -> String:
	return _tab


## F11 / Alt+Enter while the page is up (fps_overlay.gd hands them here): a change of the page's settings like a click on its controls, applied at
## once, written only by ACCEPT
func hotkey(key: String, value) -> void:
	set_value(key, value)


func pending_value(key: String):
	return _pending.to_dict()[key] if _open else null


## the row the page shows for `key` (null when the tab does not have it)
func row(key: String):
	return _rows.get(key)


func _apply_extras() -> void:
	var done: PackedStringArray = game_options.apply_global(_extras)
	print("GAME OPTIONS applied: %s%s" % [", ".join(done), "" if game_options.problems.is_empty() else " PROBLEMS " + str(game_options.problems)])


func _apply_pending() -> void:
	var r: Dictionary = _pending.apply(get_tree().root, _applied)
	_applied = _pending.copy()
	if overlay != null:
		overlay.preview = _pending.show_fps # the counter follows the box at once; the file only on ACCEPT
		overlay.refresh()
	print("GAME DISPLAY applied: %s%s" % [", ".join(r.applied), "" if r.problems.is_empty() else " PROBLEMS " + str(r.problems)])


# ---------------------------------------------------------------------------------------------------------------------------------
# the page's rows
# ---------------------------------------------------------------------------------------------------------------------------------
func _show_tab(name: String) -> void:
	_tab = name
	if not _open:
		return
	var spec := _spec()
	_rows.clear()
	for r in spec.rows:
		if not String(r.get("key", "")).is_empty():
			_rows[r.key] = r
	var res: Dictionary = shell.openbfme_page_show(spec)
	if not res.get("ok", false):
		printerr("GAME DISPLAY the OpenBFME page: ", res.get("error", "?"))


## The rows of the shown tab again (after a change: a row may change its value, its items or whether it is enabled)
func _sync() -> void:
	if not _open:
		return
	var spec := _spec()
	for r in spec.rows:
		if String(r.get("key", "")).is_empty():
			continue
		var old = _rows.get(r.key)
		if old != null and old.hash() == r.hash():
			continue
		_rows[r.key] = r
		shell.openbfme_page_set_row(r)


func _spec() -> Dictionary:
	var rows: Array
	var note: String = HELP.get("note:" + _tab, "")
	if _menu:
		_retail = shell.options_retail_rows()
	match _tab:
		"Display":
			rows = _display_rows()
			var method := DisplaySettings.renderer()
			note = note % [RENDERER_LABELS.get(method, method), ", ".join(DisplaySettings.scalers_for(method).map(func(x): return SCALER_LABELS[x]))]
		"Graphics":
			rows = _graphics_rows()
		"Camera":
			rows = _camera_rows()
		"Interface":
			rows = _interface_rows()
		"Audio":
			rows = _audio_rows()
		"Online":
			rows = _online_rows()
		"Performance":
			rows = _performance_rows()
		"Controls":
			rows = []
	var spec := {
		"subtitle": SUBTITLE,
		"tabs_header": "OpenBFME",
		"tabs": TABS,
		"tab": TABS.find(_tab),
		"panel_header": _tab,
		"rows": rows,
		"note": note,
	}
	if _tab == "Controls":
		spec["list"] = _hotkey_list()
	return spec


## lane OPTS-5: a row of the original game's settings as the shell describes it (null: the movie made no such control, the shell said so)
func _retail_row(key: String, column := 0):
	var r = _retail.get(key)
	if r == null:
		return null
	var out: Dictionary = r.duplicate()
	out["column"] = column
	return out


## the rows of `list` that exist (a retail row the movie lacks is left out)
static func _present(list: Array) -> Array:
	return list.filter(func(r): return r != null)


static func _choice(key: String, label: String, items: Array, value: int, help: String, enabled := true) -> Dictionary:
	return {"kind": "choice", "key": key, "label": label, "items": items, "value": value, "help": help, "enabled": enabled}


static func _check(key: String, label: String, on: bool, help: String) -> Dictionary:
	return {"kind": "check", "key": key, "label": label, "value": 1 if on else 0, "help": help}


static func _slider(key: String, label: String, lo: int, hi: int, value: int, text: String, help: String) -> Dictionary:
	return {"kind": "slider", "key": key, "label": label, "min": lo, "max": hi, "value": value, "value_text": text, "help": help}


func _resolution_list() -> Array:
	var screen := DisplayServer.window_get_current_screen()
	return DisplaySettings.resolutions(DisplayServer.screen_get_size(screen), get_tree().root.size)


func _resolution_choices() -> Array:
	var list := _resolution_list()
	var current: Vector2i = _pending.resolution if _pending.resolution != Vector2i.ZERO else get_tree().root.size
	if not list.has(current):
		list.append(current)
	return list


func _frame_limits() -> Array:
	# the presets, and the cap in effect when it is none of them (a --max-fps or a file value: "custom")
	var limits: Array = DisplaySettings.FRAME_LIMITS.duplicate()
	if not limits.has(_pending.max_fps):
		limits.append(_pending.max_fps)
	return limits


func _display_rows() -> Array:
	var p = _pending
	var rows := []
	rows.append(_choice("window_mode", "Window mode", DisplaySettings.WINDOW_MODES.map(func(m): return WINDOW_MODE_LABELS[m]),
		DisplaySettings.WINDOW_MODES.find(p.window_mode), HELP.window_mode))
	var res := _resolution_choices()
	var current: Vector2i = p.resolution if p.resolution != Vector2i.ZERO else get_tree().root.size
	rows.append(_choice("resolution", "Resolution", res.map(func(r): return "%d x %d" % [r.x, r.y]), res.find(current),
		HELP.resolution_screen if p.window_mode != "windowed" else HELP.resolution_windowed, p.window_mode == "windowed"))
	var vsync := VSYNC_ITEMS.map(func(v): return v[1])
	if p.vsync == "mailbox":
		vsync.append("Mailbox")
	rows.append(_choice("vsync", "Vertical sync", vsync, ["on", "off", "adaptive", "mailbox"].find(p.vsync), HELP.vsync))
	var limits := _frame_limits()
	rows.append(_choice("max_fps", "Frame rate limit", limits.map(func(f): return "Off" if f == 0 else ("%d fps" % f if DisplaySettings.FRAME_LIMITS.has(f) else "%d fps (custom)" % f)),
		limits.find(p.max_fps), HELP.max_fps))
	rows.append(_check("show_fps", "Show the frame rate (F11)", p.show_fps, HELP.show_fps))
	var pct := roundi(p.render_scale * 100.0)
	# the slider's positions are the 5% steps (10 .. 20): the gadget's thumb lands on what is applied
	rows.append(_slider("render_scale", "Render scale", roundi(DisplaySettings.SCALE_MIN * 100.0) / SCALE_STEP, roundi(DisplaySettings.SCALE_MAX * 100.0) / SCALE_STEP,
		roundi(float(pct) / SCALE_STEP), "%d%%" % pct, HELP.render_scale))
	var available: Array = DisplaySettings.scalers_for(DisplaySettings.renderer())
	var scalers := []
	for s in DisplaySettings.SCALERS:
		scalers.append({"text": SCALER_LABELS[s], "enabled": available.has(s)})
	rows.append(_choice("scaler", "Upscaling", scalers, DisplaySettings.SCALERS.find(p.scaler), HELP.scaler_full if p.render_scale >= 0.999 else HELP.scaler_scaled))
	# lane UI-6: the Widescreen view (Options.ini, applied to the game's camera at once)
	rows.append(_extra_choice("OpenBFMEWidescreen", "Widescreen view", WIDESCREEN_ITEMS, HELP.OpenBFMEWidescreen))
	# lane OPTS-5: the original game's Brightness (live while it moves, RW 0x91F90D)
	rows.append(_retail_row("Retail.Brightness"))
	return _present(rows)


static func _index_of(items: Array, value: String) -> int:
	for i in items.size():
		if items[i][0] == value:
			return i
	return 0


func _extra_choice(key: String, label: String, items: Array, help: String, disabled := []) -> Dictionary:
	var shown := []
	for it in items:
		shown.append({"text": it[1], "enabled": not disabled.has(it[0])})
	var r := _choice(key, label, shown, _index_of(items, str(_extras.get(key, ""))), help)
	r["values"] = items.map(func(it): return it[0])
	return r


func _extra_check(key: String, label: String, help: String) -> Dictionary:
	return _check(key, label, str(_extras.get(key, "no")) == "yes", help)


## lane OPTS-5: two columns: the original game's detail level and its custom settings (the Options screen's Graphics list and CUSTOM SETTINGS page) on
## the left, OpenBFME's renderer settings on the right ("beside them": the owner's layout)
func _graphics_rows() -> Array:
	var method := DisplaySettings.renderer()
	var no_aa := []
	if method == "gl_compatibility":
		no_aa.append("fxaa")
	if method != "forward_plus":
		no_aa.append("taa")
	var rows := [_retail_row("Retail.Detail")]
	for i in 9:
		rows.append(_retail_row("Retail.AdvancedOption%d" % i))
	for r in [
		_extra_choice("OpenBFMEAntiAliasing", "Anti-aliasing", AA_ITEMS, HELP.OpenBFMEAntiAliasing, no_aa),
		_extra_choice("OpenBFMEAnisotropic", "Anisotropic", ANISO_ITEMS, HELP.OpenBFMEAnisotropic),
		_extra_choice("OpenBFMELook", "Look", LOOK_ITEMS, HELP.OpenBFMELook),
		_extra_check("OpenBFMESoftParticles", "Soft particles", HELP.OpenBFMESoftParticles),
	]:
		r["column"] = 1
		rows.append(r)
	return _present(rows)


func _camera_rows() -> Array:
	return _present([
		_retail_row("Retail.ScrollSpeed"),
		_extra_choice("OpenBFMEEdgeScroll", "Edge scrolling", EDGE_ITEMS, HELP.OpenBFMEEdgeScroll),
		_extra_choice("OpenBFMERotateSpeed", "Rotation speed", ROTATE_ITEMS, HELP.OpenBFMERotateSpeed),
		_extra_choice("OpenBFMEMaxZoomOut", "Max zoom-out", MAX_ZOOM_ITEMS, HELP.OpenBFMEMaxZoomOut),
		_extra_choice("OpenBFMEFreeCamera", "Free camera", ZOOM_ITEMS, HELP.OpenBFMEFreeCamera),
		_extra_check("OpenBFMEInvertDragScroll", "Invert drag scrolling", HELP.OpenBFMEInvertDragScroll),
		_extra_check("OpenBFMEConfineMouse", "Keep the mouse in the window", HELP.OpenBFMEConfineMouse),
	])


func _interface_rows() -> Array:
	return _present([
		_retail_row("Retail.HealthBars"),
		_extra_choice("OpenBFMEHealthBars", "Health bars", BARS_ITEMS, HELP.OpenBFMEHealthBars),
		_extra_check("AlternateMouseSetup", "Alternate mouse setup", HELP.AlternateMouseSetup),
		_extra_check("OpenBFMEGameClock", "Show the game clock", HELP.OpenBFMEGameClock),
		_extra_choice("OpenBFMEUIScale", "Interface scale", UI_SCALE_ITEMS, HELP.OpenBFMEUIScale),
	])


func _audio_rows() -> Array:
	var master := str(_extras.get("OpenBFMEMasterVolume", "100")).to_int()
	var devices := Array(AudioServer.get_output_device_list())
	var dev := str(_extras.get("OpenBFMEAudioDevice", "Default"))
	if not devices.has(dev):
		devices.append(dev)
	var r := _choice("OpenBFMEAudioDevice", "Output device", devices, devices.find(dev), HELP.OpenBFMEAudioDevice)
	r["values"] = devices
	return _present([
		_slider("OpenBFMEMasterVolume", "Master volume", 0, 100, master, "%d%%" % master, HELP.OpenBFMEMasterVolume),
		_retail_row("Retail.MusicVolume"),
		_retail_row("Retail.SoundFxVolume"),
		_retail_row("Retail.VoiceVolume"),
		_retail_row("Retail.AmbientVolume"),
		_retail_row("Retail.MovieVolume"),
		_retail_row("Retail.EAX3"),
		_retail_row("Retail.HighAudioQuality"),
		_extra_check("OpenBFMEMuteUnfocused", "Mute in the background", HELP.OpenBFMEMuteUnfocused),
		r,
	])


func _online_rows() -> Array:
	return _present([
		_retail_row("Retail.OnlineIp"),
		_retail_row("Retail.OnlinePortNum"),
		_retail_row("Retail.SendDelay"),
		_retail_row("Retail.RefreshNat"),
	])


func _performance_rows() -> Array:
	# lane UBER-1 (docs/UBER.md): the opt-in simulation profile; the help line is the warning
	return [_extra_check("OpenBFMEUberPerformance", "Uber performance mode", HELP.OpenBFMEUberPerformance)]


func _hotkey_list() -> Array:
	if _hotkeys.is_empty() and shell != null and shell.has_method("get_command_map"):
		var r: Dictionary = shell.get_command_map()
		if not r.get("ok", false):
			return [["The hot keys could not be read: " + str(r.get("error", "?")), ""]]
		for row in r.rows:
			if not row.game or row.key == "KEY_NONE" or row.category == 7:
				continue # the shell's, the modifier-only and the DEBUG category's records (CommandMap Category, lookup RW 0xBF0B08: ... MISC 6, DEBUG 7)
			var name: String = row.display_name if not str(row.display_name).is_empty() else String(row.name).trim_prefix("META_").capitalize()
			var keys := PackedStringArray()
			if row.ctrl:
				keys.append("Ctrl")
			if row.alt:
				keys.append("Alt")
			if row.shift:
				keys.append("Shift")
			keys.append(String(row.key).trim_prefix("KEY_").capitalize())
			_hotkeys.append([name, " + ".join(keys) + (" (double)" if row.transition == 2 else "")])
	return _hotkeys


func hotkey_count() -> int:
	return _hotkey_list().size()


# ---------------------------------------------------------------------------------------------------------------------------------
# the page's events
# ---------------------------------------------------------------------------------------------------------------------------------
## Lane OPTS-4 (NOT RETAIL: the Master volume is this rebuild's own). It behaves as RotWK's volume sliders do (RW 0x91F90D): the volume changes while
## the slider moves (the master bus, _apply_extras) and a sample plays so the player hears the level: the sound effects' sample (MiscAudio
## VolumeSampleSoundFX, a sword hit), started when none is sounding and left to end by itself, as retail leaves its voice sample.
var _master_sample := 0
func _master_preview(value: int) -> void:
	var audio: Node = audio_getter.call() if audio_getter.is_valid() else null
	if audio == null or not audio.is_booted():
		return
	audio.log_volume("MASTER", value / 100.0)
	if not audio.is_playing(_master_sample):
		_master_sample = audio.play_misc("VolumeSampleSoundFX")


func _on_value(key: String, value: int) -> void:
	if not _open:
		return
	match key:
		"window_mode":
			if value >= 0 and value < DisplaySettings.WINDOW_MODES.size():
				set_value("window_mode", DisplaySettings.WINDOW_MODES[value])
		"resolution":
			var list := _resolution_choices()
			if value >= 0 and value < list.size():
				set_value("resolution", DisplaySettings.resolution_text(list[value]))
		"vsync":
			var modes := ["on", "off", "adaptive", "mailbox"]
			if value >= 0 and value < modes.size():
				set_value("vsync", modes[value])
		"max_fps":
			var limits := _frame_limits()
			if value >= 0 and value < limits.size():
				set_value("max_fps", limits[value])
		"show_fps":
			set_value("show_fps", value != 0)
		"render_scale":
			set_value("render_scale", float(value * SCALE_STEP) / 100.0)
		"scaler":
			if value >= 0 and value < DisplaySettings.SCALERS.size():
				set_value("scaler", DisplaySettings.SCALERS[value])
		"OpenBFMEMasterVolume":
			set_extra(key, str(value))
			_master_preview(value)
		_:
			var r = _rows.get(key)
			if r == null:
				printerr("GAME OPTIONS the page sent a value for '%s', which the %s tab does not show" % [key, _tab])
				return
			if r.kind == "check":
				set_extra(key, "yes" if value != 0 else "no")
			elif r.has("values") and value >= 0 and value < r.values.size():
				set_extra(key, str(r.values[value]))

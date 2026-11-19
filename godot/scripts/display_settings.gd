## Lane OPTS-1: the OpenBFME display options (owner, 2026-10-10). NOT RETAIL: RotWK's own Options screen stays as retail's (its Resolution combo and
## Detail levels are the retail screen's, S-1913 / S-2481); these are OpenBFME extras, kept in the user data folder (the folder of Options.ini) as
## openbfme-display.cfg, read at start and applied live by the OpenBFME options screen (scripts/openbfme_options_screen.gd). The launcher can read the
## same file (a Godot ConfigFile, section [display]).
##
##   window_mode    windowed | borderless | fullscreen   borderless = Godot's WINDOW_MODE_FULLSCREEN (a borderless window over the whole screen);
##                                                       fullscreen = WINDOW_MODE_EXCLUSIVE_FULLSCREEN (exclusive on Windows; the same as borderless on
##                                                       Linux and macOS, Godot's own rule)
##   resolution     "WxH" the window's size in windowed mode, or the viewport's render size in either fullscreen mode (scaled to fit, preserving
##                  aspect ratio). "" keeps the window's size in windowed mode and uses the desktop in fullscreen. The monitor's display mode is unchanged.
##   vsync          on | off | adaptive | mailbox             (DisplayServer vsync modes; the screen offers the first three, mailbox stays a file / --vsync value)
##   max_fps        0 (no limit) or a frame cap in frames per second (Engine.max_fps)
##   show_fps       the frame-rate counter (scripts/fps_overlay.gd; F11 toggles it)
##   render_scale   0.5 .. 1.0, the 3D view's resolution as a fraction of the window's (Viewport.scaling_3d_scale); the menus and the HUD (2D) stay sharp
##   scaler         bilinear | fsr | fsr2   (Viewport.scaling_3d_mode). The game's renderer is Forward+ (project.godot), which has all three; Godot's
##                  Mobile renderer has no FSR 2 and the Compatibility renderer bilinear only, so a scaler the renderer lacks is reported, not applied.
##                  FSR 3 / DLSS / XeSS are not part of Godot 4.7 (docs/ROADMAP.md: later, through plugins).
## The command line wins over the file for the run (--res, --vsync, --max-fps, --fps); it never writes the file.
## A value the file holds that is not one of these is an error in `errors` (the game prints it) and that key keeps its default.
extends RefCounted

const FILE_NAME := "openbfme-display.cfg"
const SECTION := "display"
const WINDOW_MODES := ["windowed", "borderless", "fullscreen"]
const VSYNC_MODES := ["on", "off", "adaptive", "mailbox"]
const FRAME_LIMITS := [0, 30, 60, 120, 144, 240] # the screen's presets; any other positive value is "custom"
const MAX_FPS_LIMIT := 1000
const SCALERS := ["bilinear", "fsr", "fsr2"]
const SCALE_MIN := 0.5
const SCALE_MAX := 1.0
## Graphics preset. auto measures the first match; high is the full picture; low is the toaster tier (70% 3D scale, bilinear, retail Low's
## terrain without a normal map, and Low's detail unless Options.ini names a StaticGameLOD). Upstream has no adapter class, so every first
## start is measured. A median GPU time above PROBE_SLOW_GPU_MS records low. A file that holds no probe but a render scale or a scaler (one from
## before this key) is never measured; a render scale or scaler the player chose meanwhile stays (record_probe).
const PRESETS := ["auto", "high", "low"]
const PROBE_RESULTS := ["", "pending", "low", "high"]
const PROBE_SLOW_GPU_MS := 25.0
const PROBE_WARMUP_US := 20000000
const PROBE_FRAMES := 300
const LOW_PRESET_SCALE := 0.7
## lane OWNERFIX-1: the anti-aliasing option's TAA is FSR 2's temporal anti-aliasing at the render scale in effect (game_options.gd sets this): the
## scaler then is FSR 2 whatever `scaler` says, on the renderers that have it
static var temporal_aa := false
## the window sizes the Resolution list offers when they fit on the monitor (Godot lists no adapter modes: S-1913), with the monitor's own size added
const COMMON_RESOLUTIONS := [Vector2i(800, 600), Vector2i(1024, 768), Vector2i(1152, 864), Vector2i(1280, 720), Vector2i(1280, 800), Vector2i(1280, 960),
	Vector2i(1280, 1024), Vector2i(1366, 768), Vector2i(1440, 900), Vector2i(1600, 900), Vector2i(1600, 1200), Vector2i(1680, 1050), Vector2i(1920, 1080),
	Vector2i(1920, 1200), Vector2i(2560, 1080), Vector2i(2560, 1440), Vector2i(2560, 1600), Vector2i(3440, 1440), Vector2i(3840, 2160)]

var window_mode := "windowed"
var resolution := Vector2i.ZERO
var vsync := "on"
var max_fps := 0
var show_fps := false
var render_scale := 1.0
var scaler := "bilinear"
var preset := "auto"
## "" never measured (an older file); "pending" until the first match; then "low" or "high".
var probe := ""
var errors := PackedStringArray()


static func default_path() -> String:
	return OS.get_user_data_dir().path_join(FILE_NAME)


func to_dict() -> Dictionary:
	return {"window_mode": window_mode, "resolution": resolution_text(resolution), "vsync": vsync, "max_fps": max_fps, "show_fps": show_fps,
		"render_scale": render_scale, "scaler": scaler, "preset": preset, "probe": probe}


func copy():
	var c = get_script().new()
	c.from_dict(to_dict())
	c.errors = PackedStringArray()
	return c


func equals(other) -> bool:
	return other != null and to_dict() == other.to_dict()


static func resolution_text(r: Vector2i) -> String:
	return "" if r == Vector2i.ZERO else "%dx%d" % [r.x, r.y]


static func parse_resolution(text: String) -> Vector2i:
	var p := text.strip_edges().to_lower().split("x")
	if p.size() != 2 or not p[0].is_valid_int() or not p[1].is_valid_int():
		return Vector2i(-1, -1)
	var r := Vector2i(int(p[0]), int(p[1]))
	return r if r.x >= 320 and r.y >= 240 and r.x <= 16384 and r.y <= 16384 else Vector2i(-1, -1)


## Sets the values from a dictionary (the file's keys); every bad value is an error and keeps the current value.
func from_dict(d: Dictionary) -> void:
	for key in d:
		var v = d[key]
		match key:
			"window_mode":
				if v is String and WINDOW_MODES.has(v):
					window_mode = v
				else:
					errors.append("window_mode = %s: not one of %s" % [str(v), ", ".join(WINDOW_MODES)])
			"resolution":
				var r := parse_resolution(str(v)) if not str(v).is_empty() else Vector2i.ZERO
				if r.x >= 0:
					resolution = r
				else:
					errors.append("resolution = %s: not WIDTHxHEIGHT (320x240 .. 16384x16384) or empty" % str(v))
			"vsync":
				if v is String and VSYNC_MODES.has(v):
					vsync = v
				else:
					errors.append("vsync = %s: not one of %s" % [str(v), ", ".join(VSYNC_MODES)])
			"max_fps":
				if (v is int or (v is float and v == floorf(v))) and int(v) >= 0 and int(v) <= MAX_FPS_LIMIT:
					max_fps = int(v)
				else:
					errors.append("max_fps = %s: not a whole number 0 .. %d" % [str(v), MAX_FPS_LIMIT])
			"show_fps":
				if v is bool:
					show_fps = v
				else:
					errors.append("show_fps = %s: not true / false" % str(v))
			"render_scale":
				if (v is float or v is int) and float(v) >= SCALE_MIN - 0.0001 and float(v) <= SCALE_MAX + 0.0001:
					render_scale = clampf(float(v), SCALE_MIN, SCALE_MAX)
				else:
					errors.append("render_scale = %s: not a number %.2f .. %.2f" % [str(v), SCALE_MIN, SCALE_MAX])
			"scaler":
				if v is String and SCALERS.has(v):
					scaler = v
				else:
					errors.append("scaler = %s: not one of %s" % [str(v), ", ".join(SCALERS)])
			"preset":
				if v is String and PRESETS.has(v):
					preset = v
				else:
					errors.append("preset = %s: not one of %s" % [str(v), ", ".join(PRESETS)])
			"probe":
				if v is String and PROBE_RESULTS.has(v):
					probe = v
				else:
					errors.append("probe = %s: not one of %s" % [str(v), ", ".join(PROBE_RESULTS)])
			_:
				errors.append("unknown key %s" % key)


## Reads the file. A missing file is the first run (the defaults, no error); a file that cannot be parsed is an error and leaves the defaults.
func load_file(path: String) -> bool:
	if not FileAccess.file_exists(path):
		_first_start()
		return true
	var cfg := ConfigFile.new()
	var err := cfg.load(path)
	if err != OK:
		errors.append("%s: cannot be read (%s)" % [path, error_string(err)])
		return false
	for section in cfg.get_sections():
		if section != SECTION:
			errors.append("%s: unknown section [%s]" % [path, section])
	var d := {}
	if cfg.has_section(SECTION):
		for key in cfg.get_section_keys(SECTION):
			d[key] = cfg.get_value(SECTION, key)
	var before := errors.size()
	from_dict(d)
	if not d.has("preset") and not (d.has("render_scale") or d.has("scaler")):
		if not d.has("probe"):
			_first_start()
	for i in range(before, errors.size()):
		errors[i] = "%s: %s" % [path, errors[i]]
	return errors.size() == before


static func preset_values(choice: String, probe_result := "") -> Dictionary:
	var tier_name := choice
	if tier_name == "auto":
		tier_name = detected_preset(probe_result)
	var out := {"preset": choice}
	if tier_name == "low":
		out.merge({"render_scale": LOW_PRESET_SCALE, "scaler": "bilinear"}, true)
	else:
		out.merge({"render_scale": 1.0, "scaler": "bilinear"}, true)
	return out


func preset_line() -> String:
	var why := "the file's choice" if preset != "auto" else "auto: %s" % ({"low": "measured slow", "high": "measured fast enough", "pending": "not measured yet: the first match measures it"}.get(probe, "a settings file from before the probe"))
	return "graphics preset %s (%s), 3D %d%%" % [tier(), why, roundi(render_scale * 100.0)]


static func detected_preset(probe_result := "") -> String:
	return "low" if probe_result == "low" else "high"


func tier() -> String:
	return preset if preset != "auto" else detected_preset(probe)


func _first_start() -> void:
	probe = "pending"


func needs_probe() -> bool:
	return preset == "auto" and probe == "pending"


static func probe_verdict(samples_ms: Array) -> String:
	if samples_ms.is_empty():
		return ""
	var sorted := samples_ms.duplicate()
	sorted.sort()
	return "low" if float(sorted[sorted.size() / 2]) > PROBE_SLOW_GPU_MS else "high"


func record_probe(verdict: String) -> Array:
	probe = verdict
	var keys := ["probe"]
	# the probe never overrides a choice: only the defaults (100%, bilinear) step down; a scale or scaler set on the options screen before the
	# first match stays
	if verdict == "low" and is_equal_approx(render_scale, 1.0) and scaler == "bilinear":
		var rows := preset_values("low")
		render_scale = rows.render_scale
		scaler = rows.scaler
		keys.append_array(["render_scale", "scaler"])
	return keys


## Writes only `keys` into the file, keeping every other key the file holds (a hotkey outside the options screen: the launcher or the screen may
## have written the rest). A file that exists but cannot be parsed is an error and is not overwritten.
func save_keys(path: String, keys: Array) -> bool:
	var cfg := ConfigFile.new()
	if FileAccess.file_exists(path):
		var err := cfg.load(path)
		if err != OK:
			errors.append("%s: cannot be read (%s), not written" % [path, error_string(err)])
			return false
	var d := to_dict()
	for key in keys:
		cfg.set_value(SECTION, key, d[key])
	var werr := cfg.save(path)
	if werr != OK:
		errors.append("%s: cannot be written (%s)" % [path, error_string(werr)])
		return false
	return true


## The settings as the game runs them now (the file's values with what the command line, a maximise or the driver changed): what the options
## screen shows when it opens. resolution keeps the file's value (a window's size is not a choice the player made).
static func effective(base, window: Window, counter_shown: bool):
	var e = base.copy()
	e.window_mode = window_mode_now(window, base.window_mode)
	for name in VSYNC_MODES:
		if vsync_mode_of(name) == DisplayServer.window_get_vsync_mode():
			e.vsync = name
	e.max_fps = clampi(Engine.max_fps, 0, MAX_FPS_LIMIT)
	e.render_scale = clampf(window.scaling_3d_scale, SCALE_MIN, SCALE_MAX)
	for name in SCALERS:
		if scaling_mode_of(name) == window.scaling_3d_mode:
			e.scaler = name
	e.show_fps = counter_shown
	return e


## The window mode the window is in (a maximised window is "windowed"); the headless display server keeps no mode, so there it is `fallback`
static func window_mode_now(window: Window, fallback: String) -> String:
	if DisplayServer.get_name() == "headless":
		return fallback
	match window.mode:
		Window.MODE_FULLSCREEN:
			return "borderless"
		Window.MODE_EXCLUSIVE_FULLSCREEN:
			return "fullscreen"
	return "windowed"


## What the window and the driver run with, to put back exactly (the options screen's CANCEL)
static func snapshot(window: Window) -> Dictionary:
	return {"vsync": DisplayServer.window_get_vsync_mode(), "max_fps": Engine.max_fps, "scaling_mode": window.scaling_3d_mode, "scale": window.scaling_3d_scale,
		"mode": window.mode, "size": window.size, "position": window.position, "content_mode": window.content_scale_mode,
		"content_size": window.content_scale_size, "content_aspect": window.content_scale_aspect}


static func restore(window: Window, snap: Dictionary) -> void:
	if DisplayServer.window_get_vsync_mode() != snap.vsync:
		DisplayServer.window_set_vsync_mode(snap.vsync)
	Engine.max_fps = snap.max_fps
	window.scaling_3d_mode = snap.scaling_mode
	window.scaling_3d_scale = snap.scale
	window.content_scale_mode = snap.content_mode
	window.content_scale_size = snap.content_size
	window.content_scale_aspect = snap.content_aspect
	if window.mode != snap.mode:
		# a window comes back to its own size and place first, then is maximised / made full screen again
		if window.mode != Window.MODE_WINDOWED:
			window.mode = Window.MODE_WINDOWED
		if snap.mode != Window.MODE_WINDOWED:
			window.mode = snap.mode
	if snap.mode == Window.MODE_WINDOWED:
		if window.size != snap.size:
			window.size = snap.size
		if window.position != snap.position:
			window.position = snap.position


func save_file(path: String) -> bool:
	var cfg := ConfigFile.new()
	var d := to_dict()
	for key in d:
		cfg.set_value(SECTION, key, d[key])
	var err := cfg.save(path)
	if err != OK:
		errors.append("%s: cannot be written (%s)" % [path, error_string(err)])
		return false
	return true


## The rendering method the game runs with ("forward_plus", "mobile", "gl_compatibility") and the scalers it has
static func renderer() -> String:
	return RenderingServer.get_current_rendering_method()


static func scalers_for(method: String) -> Array:
	match method:
		"forward_plus":
			return ["bilinear", "fsr", "fsr2"]
		"mobile":
			return ["bilinear", "fsr"]
	return ["bilinear"]


## The Resolution list: the common sizes that fit on a screen of `screen_size`, the screen's own size and `current` (the window's), ascending. A screen
## of size 0 (the headless display server knows none) lists every common size.
static func resolutions(screen_size: Vector2i, current: Vector2i) -> Array:
	var out: Array = []
	var unknown := screen_size.x <= 0 or screen_size.y <= 0
	for r in COMMON_RESOLUTIONS:
		if unknown or (r.x <= screen_size.x and r.y <= screen_size.y):
			out.append(r)
	for extra in [screen_size, current]:
		if extra.x > 0 and extra.y > 0 and not out.has(extra):
			out.append(extra)
	out.sort_custom(func(a: Vector2i, b: Vector2i) -> bool: return a.x < b.x or (a.x == b.x and a.y < b.y))
	return out


static func vsync_mode_of(name: String) -> int:
	match name:
		"off":
			return DisplayServer.VSYNC_DISABLED
		"adaptive":
			return DisplayServer.VSYNC_ADAPTIVE
		"mailbox":
			return DisplayServer.VSYNC_MAILBOX
	return DisplayServer.VSYNC_ENABLED


static func scaling_mode_of(name: String) -> int:
	match name:
		"fsr":
			return Viewport.SCALING_3D_MODE_FSR
		"fsr2":
			return Viewport.SCALING_3D_MODE_FSR2
	return Viewport.SCALING_3D_MODE_BILINEAR


static func window_mode_of(name: String) -> int:
	match name:
		"borderless":
			return Window.MODE_FULLSCREEN
		"fullscreen":
			return Window.MODE_EXCLUSIVE_FULLSCREEN
	return Window.MODE_WINDOWED


## Applies the settings to the game's window. `previous` (the settings in effect before, or null at the start: everything is applied) limits the
## changes to the keys that differ, so a hotkey that changes the window mode leaves a --max-fps cap alone and a maximised window stays maximised when
## only the frame cap changes. Returns { applied: [...], problems: [...] } for the log line.
func apply(window: Window, previous = null) -> Dictionary:
	var applied: Array = []
	var problems: Array = []
	if previous == null or previous.vsync != vsync:
		DisplayServer.window_set_vsync_mode(vsync_mode_of(vsync))
		applied.append("vsync %s" % vsync)
	if previous == null or previous.max_fps != max_fps:
		Engine.max_fps = max_fps
		applied.append("max_fps %d" % max_fps)
	var method := renderer()
	if previous == null or previous.scaler != scaler or previous.render_scale != render_scale:
		var use_scaler := scaler
		if not scalers_for(method).has(scaler):
			problems.append("the %s renderer has no %s scaling: bilinear is used" % [method, scaler])
			use_scaler = "bilinear"
		if temporal_aa and scalers_for(method).has("fsr2"):
			use_scaler = "fsr2" # lane OWNERFIX-1: the TAA anti-aliasing
		window.scaling_3d_mode = scaling_mode_of(use_scaler)
		window.scaling_3d_scale = render_scale
		applied.append("3D %d%% %s (%s)" % [roundi(render_scale * 100.0), use_scaler, method])
	var mode_changed: bool = previous == null or previous.window_mode != window_mode
	var res_changed: bool = previous == null or previous.resolution != resolution
	if mode_changed:
		var want := window_mode_of(window_mode)
		if window.mode != want:
			window.mode = want
	if mode_changed or res_changed:
		window.content_scale_size = resolution if window_mode != "windowed" else Vector2i.ZERO
		window.content_scale_mode = Window.CONTENT_SCALE_MODE_VIEWPORT if window_mode != "windowed" and resolution != Vector2i.ZERO else Window.CONTENT_SCALE_MODE_DISABLED
		window.content_scale_aspect = Window.CONTENT_SCALE_ASPECT_KEEP
	if window_mode == "windowed" and resolution.x >= 320 and resolution.y >= 240 and (mode_changed or res_changed):
		if window.mode == Window.MODE_MAXIMIZED or window.mode == Window.MODE_MINIMIZED:
			window.mode = Window.MODE_WINDOWED
		if window.size != resolution:
			window.size = resolution
			var screen := DisplayServer.window_get_current_screen()
			var area := DisplayServer.screen_get_usable_rect(screen)
			if area.size.x > 0 and area.size.y > 0:
				window.position = area.position + (area.size - resolution) / 2
	if mode_changed or res_changed:
		applied.append("window %s %s" % [window_mode, resolution_text(window.size if window_mode == "windowed" else DisplayServer.screen_get_size(window.current_screen))])
		if window_mode != "windowed" and resolution != Vector2i.ZERO:
			applied.append("render %s" % resolution_text(resolution))
	return {"applied": applied, "problems": problems}

## Adapter for the game's shared openbfme-display.cfg (godot/scripts/display_settings.gd).
## Read again before each edit so in-game changes and unrelated display settings are kept.
extends RefCounted

const GameOptions := preload("res://scripts/core/game_options.gd")
const L10n := preload("res://scripts/l10n/l10n.gd")
const MODES := ["windowed", "borderless", "fullscreen"]
const RESOLUTIONS := [Vector2i(800, 600), Vector2i(1024, 768), Vector2i(1152, 864), Vector2i(1280, 720), Vector2i(1280, 800), Vector2i(1280, 960),
	Vector2i(1280, 1024), Vector2i(1366, 768), Vector2i(1440, 900), Vector2i(1600, 900), Vector2i(1600, 1200), Vector2i(1680, 1050), Vector2i(1920, 1080),
	Vector2i(1920, 1200), Vector2i(2560, 1080), Vector2i(2560, 1440), Vector2i(2560, 1600), Vector2i(3440, 1440), Vector2i(3840, 2160)]


static func path() -> String:
	return GameOptions.game_user_dir().path_join("openbfme-display.cfg")


static func parse_resolution(value: String) -> Vector2i:
	var parts := value.strip_edges().to_lower().split("x")
	if parts.size() != 2 or not parts[0].is_valid_int() or not parts[1].is_valid_int():
		return Vector2i.ZERO
	var size := Vector2i(int(parts[0]), int(parts[1]))
	return size if size.x >= 320 and size.y >= 240 and size.x <= 16384 and size.y <= 16384 else Vector2i.ZERO


static func read(screen_size: Vector2i, file: String = "") -> Dictionary:
	var p := file if file != "" else path()
	var cfg := ConfigFile.new()
	var err := cfg.load(p) if FileAccess.file_exists(p) else OK
	var mode = cfg.get_value("display", "window_mode", "windowed")
	var raw = cfg.get_value("display", "resolution", "")
	var size := parse_resolution(raw) if raw is String else Vector2i.ZERO
	var valid: bool = mode is String and MODES.has(mode) and raw is String and (raw == "" or size != Vector2i.ZERO)
	var error := ""
	if err != OK or not valid:
		error = L10n.t("launcher.settings.display_read_error", {"path": p.get_file(), "reason": error_string(err) if err != OK else L10n.t("launcher.settings.display_invalid")})
	return {"window_mode": mode if mode is String and MODES.has(mode) else "windowed", "resolution": size,
		"screen_size": screen_size, "error": error}


static func choices(screen_size: Vector2i, current: Vector2i) -> Array:
	var sizes: Array = []
	for size in RESOLUTIONS:
		if screen_size.x <= 0 or screen_size.y <= 0 or (size.x <= screen_size.x and size.y <= screen_size.y):
			sizes.append(size)
	for size in [screen_size, current]:
		if size.x >= 320 and size.y >= 240 and not sizes.has(size):
			sizes.append(size)
	sizes.sort_custom(func(a: Vector2i, b: Vector2i): return a.x < b.x or (a.x == b.x and a.y < b.y))
	return sizes


## Conventional display families: these common panel sizes round their advertised aspect ratio.
## Other sizes retain their exact reduced ratio, including custom and portrait resolutions.
static func aspect_ratio(size: Vector2i) -> String:
	var a := size.x
	var b := size.y
	while b != 0:
		var remainder := a % b
		a = b
		b = remainder
	var width := size.x / a
	var height := size.y / a
	match Vector2i(width, height):
		Vector2i(8, 5):
			return "16:10"
		Vector2i(683, 384):
			return "16:9" # 1366 x 768 and its multiples
		Vector2i(7, 3), Vector2i(64, 27), Vector2i(43, 18):
			return "21:9" # includes 2560 x 1080, 3440 x 1440 and their multiples
	return "%d:%d" % [width, height]


static func grouped_choices(screen_size: Vector2i, current: Vector2i) -> Array:
	var groups := {}
	for size in choices(screen_size, current):
		var ratio := aspect_ratio(size)
		if not groups.has(ratio):
			groups[ratio] = []
		groups[ratio].append(size)
	var ratios := groups.keys()
	ratios.sort_custom(func(a: String, b: String):
		return float(a.get_slice(":", 0)) / float(a.get_slice(":", 1)) < float(b.get_slice(":", 0)) / float(b.get_slice(":", 1)))
	var out: Array = []
	for ratio in ratios:
		out.append({"aspect": ratio, "sizes": groups[ratio]})
	return out


## Empty resolution means automatic in the game. Explicit edits never reset the other fields.
static func save(key: String, value: String, file: String = "") -> String:
	var p := file if file != "" else path()
	if not ((key == "window_mode" and MODES.has(value)) or (key == "resolution" and (value == "" or parse_resolution(value) != Vector2i.ZERO))):
		return L10n.t("launcher.settings.display_invalid")
	var cfg := ConfigFile.new()
	var err := cfg.load(p) if FileAccess.file_exists(p) else OK
	if err == OK:
		err = DirAccess.make_dir_recursive_absolute(p.get_base_dir())
	if err == OK:
		cfg.set_value("display", key, value)
		var temporary := p + ".tmp-%d" % OS.get_process_id()
		err = cfg.save(temporary)
		if err == OK:
			err = DirAccess.rename_absolute(temporary, p)
		if FileAccess.file_exists(temporary):
			DirAccess.remove_absolute(temporary)
	return "" if err == OK else L10n.t("launcher.settings.display_write_error", {"path": p.get_file(), "reason": error_string(err)})

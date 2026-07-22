class_name OpenBFMEUserSettings
extends RefCounted
## Small, headless-safe user settings store shared by menus and slice audio.

const SETTINGS_PATH := "user://openbfme_settings.cfg"
const AUDIO_SECTION := "audio"
const DISPLAY_SECTION := "display"
const GRAPHICS_SECTION := "graphics"
const CONTROLS_SECTION := "controls"
const DEFAULT_MUSIC_VOLUME := 0.80
const DEFAULT_VOICE_SFX_VOLUME := 0.85
const DEFAULT_MUTED := false
const DEFAULT_WINDOW_MODE := "windowed"
const DEFAULT_RESOLUTION := "1920x1080"
const DEFAULT_GRAPHICS_PRESET := "high"
const DEFAULT_SCROLL_SPEED := 1.0
const DEFAULT_SHOW_ALL_HEALTH_BARS := false
const WINDOW_MODES: Array[String] = ["windowed", "borderless", "fullscreen_exclusive"]
const GRAPHICS_PRESETS: Array[String] = ["low", "medium", "high", "ultra_high", "custom"]
const SILENT_DB := -80.0


static func load_audio() -> Dictionary:
	var config := ConfigFile.new()
	var error := config.load(SETTINGS_PATH)
	if error != OK and error != ERR_FILE_NOT_FOUND:
		push_warning("Could not read OpenBFME user settings: %s" % error_string(error))
	return {
		"music_volume": _normalized(config.get_value(AUDIO_SECTION, "music_volume", DEFAULT_MUSIC_VOLUME), DEFAULT_MUSIC_VOLUME),
		"voice_sfx_volume": _normalized(config.get_value(AUDIO_SECTION, "voice_sfx_volume", DEFAULT_VOICE_SFX_VOLUME), DEFAULT_VOICE_SFX_VOLUME),
		"muted": bool(config.get_value(AUDIO_SECTION, "muted", DEFAULT_MUTED)),
	}


static func save_audio(music_volume: float, voice_sfx_volume: float, muted: bool) -> Error:
	var config := ConfigFile.new()
	var load_error := config.load(SETTINGS_PATH)
	if load_error != OK and load_error != ERR_FILE_NOT_FOUND:
		return load_error
	config.set_value(AUDIO_SECTION, "music_volume", clampf(music_volume, 0.0, 1.0))
	config.set_value(AUDIO_SECTION, "voice_sfx_volume", clampf(voice_sfx_volume, 0.0, 1.0))
	config.set_value(AUDIO_SECTION, "muted", muted)
	return config.save(SETTINGS_PATH)


static func reset_audio() -> Error:
	return save_audio(DEFAULT_MUSIC_VOLUME, DEFAULT_VOICE_SFX_VOLUME, DEFAULT_MUTED)


static func volume_to_db(volume: float, muted: bool = false) -> float:
	if muted or volume <= 0.0001:
		return SILENT_DB
	return maxf(SILENT_DB, linear_to_db(clampf(volume, 0.0, 1.0)))


static func load_display() -> Dictionary:
	var config := ConfigFile.new()
	var error := config.load(SETTINGS_PATH)
	if error != OK and error != ERR_FILE_NOT_FOUND:
		push_warning("Could not read OpenBFME user settings: %s" % error_string(error))
	var window_mode := String(config.get_value(DISPLAY_SECTION, "window_mode", DEFAULT_WINDOW_MODE))
	if window_mode not in WINDOW_MODES:
		window_mode = DEFAULT_WINDOW_MODE
	return {
		"window_mode": window_mode,
		"resolution": _normalized_resolution(config.get_value(DISPLAY_SECTION, "resolution", DEFAULT_RESOLUTION)),
	}


static func save_display(window_mode: String, resolution: String) -> Error:
	if window_mode not in WINDOW_MODES:
		return ERR_INVALID_PARAMETER
	var config := ConfigFile.new()
	var load_error := config.load(SETTINGS_PATH)
	if load_error != OK and load_error != ERR_FILE_NOT_FOUND:
		return load_error
	config.set_value(DISPLAY_SECTION, "window_mode", window_mode)
	config.set_value(DISPLAY_SECTION, "resolution", _normalized_resolution(resolution))
	return config.save(SETTINGS_PATH)


static func load_graphics() -> Dictionary:
	var config := ConfigFile.new()
	var error := config.load(SETTINGS_PATH)
	if error != OK and error != ERR_FILE_NOT_FOUND:
		push_warning("Could not read OpenBFME user settings: %s" % error_string(error))
	var preset := String(config.get_value(GRAPHICS_SECTION, "preset", DEFAULT_GRAPHICS_PRESET))
	if preset not in GRAPHICS_PRESETS:
		preset = DEFAULT_GRAPHICS_PRESET
	return {"preset": preset}


static func save_graphics(preset: String) -> Error:
	if preset not in GRAPHICS_PRESETS:
		return ERR_INVALID_PARAMETER
	var config := ConfigFile.new()
	var load_error := config.load(SETTINGS_PATH)
	if load_error != OK and load_error != ERR_FILE_NOT_FOUND:
		return load_error
	config.set_value(GRAPHICS_SECTION, "preset", preset)
	return config.save(SETTINGS_PATH)


static func load_controls() -> Dictionary:
	var config := ConfigFile.new()
	var error := config.load(SETTINGS_PATH)
	if error != OK and error != ERR_FILE_NOT_FOUND:
		push_warning("Could not read OpenBFME user settings: %s" % error_string(error))
	return {
		"scroll_speed": _normalized_range(config.get_value(CONTROLS_SECTION, "scroll_speed", DEFAULT_SCROLL_SPEED), 0.5, 2.0, DEFAULT_SCROLL_SPEED),
		"show_all_health_bars": bool(config.get_value(CONTROLS_SECTION, "show_all_health_bars", DEFAULT_SHOW_ALL_HEALTH_BARS)),
	}


static func save_controls(scroll_speed: float, show_all_health_bars: bool) -> Error:
	var config := ConfigFile.new()
	var load_error := config.load(SETTINGS_PATH)
	if load_error != OK and load_error != ERR_FILE_NOT_FOUND:
		return load_error
	config.set_value(CONTROLS_SECTION, "scroll_speed", clampf(scroll_speed, 0.5, 2.0))
	config.set_value(CONTROLS_SECTION, "show_all_health_bars", show_all_health_bars)
	return config.save(SETTINGS_PATH)


static func reset_display() -> Error:
	return save_display(DEFAULT_WINDOW_MODE, DEFAULT_RESOLUTION)


static func reset_graphics() -> Error:
	return save_graphics(DEFAULT_GRAPHICS_PRESET)


static func reset_controls() -> Error:
	return save_controls(DEFAULT_SCROLL_SPEED, DEFAULT_SHOW_ALL_HEALTH_BARS)


static func _normalized_resolution(value: Variant) -> String:
	var text := String(value).to_lower().strip_edges()
	var parts := text.split("x", false)
	if parts.size() == 2 and parts[0].is_valid_int() and parts[1].is_valid_int():
		var width := int(parts[0])
		var height := int(parts[1])
		if width >= 640 and height >= 480 and width <= 7680 and height <= 4320:
			return "%dx%d" % [width, height]
	return DEFAULT_RESOLUTION


static func _normalized_range(value: Variant, minimum: float, maximum: float, fallback: float) -> float:
	if value is float or value is int:
		return clampf(float(value), minimum, maximum)
	return fallback


static func _normalized(value: Variant, fallback: float) -> float:
	if value is float or value is int:
		return clampf(float(value), 0.0, 1.0)
	return fallback

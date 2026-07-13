class_name OpenBFMEUserSettings
extends RefCounted
## Small, headless-safe user settings store shared by menus and slice audio.

const SETTINGS_PATH := "user://openbfme_settings.cfg"
const AUDIO_SECTION := "audio"
const DEFAULT_MUSIC_VOLUME := 0.80
const DEFAULT_VOICE_SFX_VOLUME := 0.85
const DEFAULT_MUTED := false
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


static func _normalized(value: Variant, fallback: float) -> float:
	if value is float or value is int:
		return clampf(float(value), 0.0, 1.0)
	return fallback

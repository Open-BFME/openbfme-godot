class_name OpenBFMEUserSettings
extends RefCounted
## Small, headless-safe user settings store shared by menus and slice audio.

const SETTINGS_PATH := "user://openbfme_settings.cfg"
const AUDIO_SECTION := "audio"
const DISPLAY_SECTION := "display"
const GRAPHICS_SECTION := "graphics"
const CONTROLS_SECTION := "controls"
const BINDINGS_SECTION := "bindings"
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

## ------------------------------------------------------------------------------
## THE KEYBOARD, AS ONE TABLE - THE "KEY SETTINGS" THAT DID NOTHING
## ------------------------------------------------------------------------------
##
## The owner's report was "the key settings does nothing". It was right twice: the
## keys this game binds were never written down anywhere a player could read them,
## and the OPTIONS screen's Controls column carried a scroll-speed slider and a
## health-bar toggle and no keyboard section at all.
##
## The table lives HERE, in the settings store, because that is the one file every
## surface that has to state the bindings can reach without dragging a dependency
## behind it: the options screen already preloads this file, and the War of the
## Ring screen's pause shell reads it through the `OpenBFMEUserSettings` class
## name. One table, three readers, and a binding that is added without being added
## here is a binding that goes undiscovered - which is exactly how F1 and ESCAPE
## came to be secrets.
##
## `where` names the script that actually reads the keycode, so a reader can go
## and check rather than trust this list.
##
## `player` IS NOT DECORATION. The War of the Ring HUD is held to a register rule
## (`wotr_screen.gd:IMPLEMENTATION_VOCABULARY`): a shipped game's HUD makes
## statements about the WORLD, never about its own conversion or its own
## diagnosis, and a runner fails the build over any string on that surface that
## breaks the rule. F1 opens the conversion-diagnosis overlay, which is a
## developer surface by design, so naming it in the pause shell's key list would
## put the word on the glass. It is therefore `player: false`: the OPTIONS screen
## lists it (that screen IS where a player goes to find out what a key does, and
## it is not held to the HUD's register), the pause shell does not.
## Remappable InputMap actions. Defaults match project.godot plus the four
## keys that used to be literal KEY_* compares (pause, F1, F2, F11).
## `default_joy` is a JoyButton index, or -1 if no gamepad default.
const REMAPPABLE_ACTIONS: Array[Dictionary] = [
	{"id": "pause_menu", "label": "Pause / resume", "default_key": KEY_ESCAPE, "default_joy": JOY_BUTTON_START, "player": true, "where": "retail_vertical_slice.gd"},
	{"id": "hide_hud", "label": "Hide / show the HUD", "default_key": KEY_F2, "default_joy": JOY_BUTTON_BACK, "player": true, "where": "wotr_screen.gd"},
	{"id": "diagnostics", "label": "Diagnostics overlay", "default_key": KEY_F1, "default_joy": -1, "player": false, "where": "wotr_screen.gd"},
	{"id": "fullscreen", "label": "Fullscreen", "default_key": KEY_F11, "default_joy": -1, "player": true, "where": "main_menu.gd"},
	{"id": "cam_forward", "label": "Camera forward", "default_key": KEY_W, "default_joy": -1, "player": true, "where": "retail_vertical_slice.gd"},
	{"id": "cam_back", "label": "Camera back", "default_key": KEY_S, "default_joy": -1, "player": true, "where": "retail_vertical_slice.gd"},
	{"id": "cam_left", "label": "Camera left", "default_key": KEY_A, "default_joy": -1, "player": true, "where": "retail_vertical_slice.gd"},
	{"id": "cam_right", "label": "Camera right", "default_key": KEY_D, "default_joy": -1, "player": true, "where": "retail_vertical_slice.gd"},
	{"id": "cam_rotate_left", "label": "Rotate camera left", "default_key": KEY_Q, "default_joy": JOY_BUTTON_LEFT_SHOULDER, "player": true, "where": "retail_vertical_slice.gd"},
	{"id": "cam_rotate_right", "label": "Rotate camera right", "default_key": KEY_E, "default_joy": JOY_BUTTON_RIGHT_SHOULDER, "player": true, "where": "retail_vertical_slice.gd"},
	{"id": "attack_move", "label": "Attack-move", "default_key": KEY_F, "default_joy": JOY_BUTTON_X, "player": true, "where": "retail_vertical_slice.gd"},
	{"id": "stop_units", "label": "Stop", "default_key": KEY_H, "default_joy": JOY_BUTTON_B, "player": true, "where": "retail_vertical_slice.gd"},
	{"id": "stance_cycle", "label": "Cycle stance", "default_key": KEY_Z, "default_joy": JOY_BUTTON_Y, "player": true, "where": "retail_vertical_slice.gd"},
]

## Glyph family for the rebind rows: Xbox ABXY or the 2026 Steam Controller
## (Xbox-layout face buttons, twin sticks, square trackpads under the sticks).
const CONTROLLER_FAMILIES: Array[String] = ["xbox", "steam"]
const DEFAULT_CONTROLLER_FAMILY := "xbox"

## Backward-compatible readout used by the pause shell. Keys are the *current*
## stored labels, not the compile-time defaults.
const KEY_BINDINGS: Array[Dictionary] = [
	{"key": "ESC", "action": "Pause / resume", "where": "wotr_screen.gd", "player": true, "id": "pause_menu"},
	{"key": "F1", "action": "Diagnostics overlay", "where": "wotr_screen.gd", "player": false, "id": "diagnostics"},
	{"key": "F2", "action": "Hide / show the HUD", "where": "wotr_screen.gd", "player": true, "id": "hide_hud"},
	{"key": "F11", "action": "Fullscreen (persists)", "where": "main_menu.gd", "player": true, "id": "fullscreen"},
]


static func player_key_bindings() -> Array[Dictionary]:
	var stored := load_bindings()
	var shown: Array[Dictionary] = []
	for binding in KEY_BINDINGS:
		if not bool(binding.get("player", true)):
			continue
		var row := binding.duplicate(true)
		var action_id := String(row.get("id", ""))
		if action_id != "" and stored.has(action_id):
			row["key"] = keycode_label(int((stored[action_id] as Dictionary).get("key", 0)))
		shown.append(row)
	return shown


static func default_bindings() -> Dictionary:
	var out := {}
	for row_value in REMAPPABLE_ACTIONS:
		var row := row_value as Dictionary
		out[String(row["id"])] = {
			"key": int(row["default_key"]),
			"joy": int(row["default_joy"]),
		}
	return out


static func load_bindings() -> Dictionary:
	var config := ConfigFile.new()
	var error := config.load(SETTINGS_PATH)
	if error != OK and error != ERR_FILE_NOT_FOUND:
		push_warning("Could not read OpenBFME user settings: %s" % error_string(error))
	var out := default_bindings()
	for action_id in out.keys():
		var row: Dictionary = out[action_id]
		row["key"] = int(config.get_value(BINDINGS_SECTION, "%s_key" % action_id, row["key"]))
		row["joy"] = int(config.get_value(BINDINGS_SECTION, "%s_joy" % action_id, row["joy"]))
		out[action_id] = row
	return out


static func load_controller_family() -> String:
	var config := ConfigFile.new()
	var error := config.load(SETTINGS_PATH)
	if error != OK and error != ERR_FILE_NOT_FOUND:
		push_warning("Could not read OpenBFME user settings: %s" % error_string(error))
	var family := String(config.get_value(BINDINGS_SECTION, "controller_family", DEFAULT_CONTROLLER_FAMILY))
	if family not in CONTROLLER_FAMILIES:
		return DEFAULT_CONTROLLER_FAMILY
	return family


static func save_bindings(bindings: Dictionary, controller_family: String = "") -> Error:
	var config := ConfigFile.new()
	var load_error := config.load(SETTINGS_PATH)
	if load_error != OK and load_error != ERR_FILE_NOT_FOUND:
		return load_error
	var family := controller_family if controller_family != "" else load_controller_family()
	if family not in CONTROLLER_FAMILIES:
		family = DEFAULT_CONTROLLER_FAMILY
	config.set_value(BINDINGS_SECTION, "controller_family", family)
	var defaults := default_bindings()
	for action_id in defaults.keys():
		var row: Dictionary = bindings.get(action_id, defaults[action_id]) as Dictionary
		config.set_value(BINDINGS_SECTION, "%s_key" % action_id, int(row.get("key", (defaults[action_id] as Dictionary)["key"])))
		config.set_value(BINDINGS_SECTION, "%s_joy" % action_id, int(row.get("joy", (defaults[action_id] as Dictionary)["joy"])))
	return config.save(SETTINGS_PATH)


static func reset_bindings() -> Error:
	return save_bindings(default_bindings(), DEFAULT_CONTROLLER_FAMILY)


const CAMERA_ALT_KEYS := {
	"cam_forward": KEY_UP,
	"cam_back": KEY_DOWN,
	"cam_left": KEY_LEFT,
	"cam_right": KEY_RIGHT,
}


static func apply_bindings_to_input_map(bindings: Dictionary = {}) -> void:
	var stored := bindings if not bindings.is_empty() else load_bindings()
	for row_value in REMAPPABLE_ACTIONS:
		var spec := row_value as Dictionary
		var action_id := String(spec["id"])
		if not InputMap.has_action(action_id):
			InputMap.add_action(action_id)
		InputMap.action_erase_events(action_id)
		var row: Dictionary = stored.get(action_id, {"key": spec["default_key"], "joy": spec["default_joy"]}) as Dictionary
		var keycode := int(row.get("key", spec["default_key"]))
		if keycode > 0:
			var key := InputEventKey.new()
			key.physical_keycode = keycode as Key
			InputMap.action_add_event(action_id, key)
		var alt_key := int(CAMERA_ALT_KEYS.get(action_id, 0))
		if alt_key > 0 and alt_key != keycode:
			var alt := InputEventKey.new()
			alt.physical_keycode = alt_key as Key
			InputMap.action_add_event(action_id, alt)
		var joy := int(row.get("joy", spec["default_joy"]))
		if joy >= 0:
			var button := InputEventJoypadButton.new()
			button.button_index = joy as JoyButton
			InputMap.action_add_event(action_id, button)
	_add_camera_stick_axes()


static func _add_camera_stick_axes() -> void:
	## Left stick pans, right stick rotates. Not remapped per-axis; the rebind
	## list covers the digital camera keys. Stick deadzone is tighter than the
	## 0.5 project default so a light tilt actually pans.
	for item in [
		["cam_left", JOY_AXIS_LEFT_X, -1.0],
		["cam_right", JOY_AXIS_LEFT_X, 1.0],
		["cam_forward", JOY_AXIS_LEFT_Y, -1.0],
		["cam_back", JOY_AXIS_LEFT_Y, 1.0],
		["cam_rotate_left", JOY_AXIS_RIGHT_X, -1.0],
		["cam_rotate_right", JOY_AXIS_RIGHT_X, 1.0],
	]:
		var action := String(item[0])
		if not InputMap.has_action(action):
			InputMap.add_action(action)
		InputMap.action_set_deadzone(action, 0.25)
		var motion := InputEventJoypadMotion.new()
		motion.axis = item[1] as JoyAxis
		motion.axis_value = float(item[2])
		InputMap.action_add_event(action, motion)


static func keycode_label(keycode: int) -> String:
	if keycode == KEY_ESCAPE:
		return "ESC"
	if keycode >= KEY_F1 and keycode <= KEY_F12:
		return "F%d" % (keycode - KEY_F1 + 1)
	if keycode >= KEY_A and keycode <= KEY_Z:
		return char(keycode)
	match keycode:
		KEY_UP:
			return "Up"
		KEY_DOWN:
			return "Down"
		KEY_LEFT:
			return "Left"
		KEY_RIGHT:
			return "Right"
		KEY_SPACE:
			return "Space"
		KEY_TAB:
			return "Tab"
		KEY_SHIFT:
			return "Shift"
		KEY_CTRL:
			return "Ctrl"
	var named := OS.get_keycode_string(keycode as Key)
	return named if named != "" else "?"


static func joy_label(button: int, family: String = "xbox") -> String:
	if button < 0:
		return ""
	var xbox := {
		JOY_BUTTON_A: "A",
		JOY_BUTTON_B: "B",
		JOY_BUTTON_X: "X",
		JOY_BUTTON_Y: "Y",
		JOY_BUTTON_LEFT_SHOULDER: "LB",
		JOY_BUTTON_RIGHT_SHOULDER: "RB",
		JOY_BUTTON_LEFT_STICK: "LS",
		JOY_BUTTON_RIGHT_STICK: "RS",
		JOY_BUTTON_START: "Start",
		JOY_BUTTON_BACK: "View",
		JOY_BUTTON_DPAD_UP: "D-Up",
		JOY_BUTTON_DPAD_DOWN: "D-Down",
		JOY_BUTTON_DPAD_LEFT: "D-Left",
		JOY_BUTTON_DPAD_RIGHT: "D-Right",
	}
	var steam := xbox.duplicate()
	steam[JOY_BUTTON_START] = "Menu"
	steam[JOY_BUTTON_BACK] = "Steam"
	var table: Dictionary = steam if family == "steam" else xbox
	return String(table.get(button, "Btn %d" % button))


static func joy_token(button: int) -> String:
	match button:
		JOY_BUTTON_A:
			return "a"
		JOY_BUTTON_B:
			return "b"
		JOY_BUTTON_X:
			return "x"
		JOY_BUTTON_Y:
			return "y"
		JOY_BUTTON_LEFT_SHOULDER:
			return "lb"
		JOY_BUTTON_RIGHT_SHOULDER:
			return "rb"
		JOY_BUTTON_LEFT_STICK:
			return "ls"
		JOY_BUTTON_RIGHT_STICK:
			return "rs"
		JOY_BUTTON_START:
			return "start"
		JOY_BUTTON_BACK:
			return "back"
		JOY_BUTTON_DPAD_UP:
			return "dup"
		JOY_BUTTON_DPAD_DOWN:
			return "ddown"
		JOY_BUTTON_DPAD_LEFT:
			return "dleft"
		JOY_BUTTON_DPAD_RIGHT:
			return "dright"
	return ""


static func glyph_path(kind: String, token: String, family: String = "xbox") -> String:
	## Icons live under res://assets/ui/input/. Missing files fail closed to "".
	var prefix := kind
	if kind == "joy":
		prefix = "steam" if family == "steam" else "xbox"
	var cleaned := token.to_lower().replace(" ", "").replace("/", "")
	for ext in ["png", "jpg"]:
		var path := "res://assets/ui/input/%s_%s.%s" % [prefix, cleaned, ext]
		if ResourceLoader.exists(path) or FileAccess.file_exists(path):
			return path
	if kind == "key" and cleaned != "blank":
		return glyph_path("key", "blank", family)
	if kind == "joy" and family == "steam":
		return glyph_path("joy", token, "xbox")
	return ""


static func key_glyph_path(keycode: int) -> String:
	return glyph_path("key", keycode_label(keycode).to_lower())


static var _key_pictures: Dictionary = {}


static func key_picture(keycode: int) -> Texture2D:
	## Unique engine-composited keycap per binding. Imagine-painted letters
	## garble; this draws the label onto key_blank with a bitmap font.
	if _key_pictures.has(keycode):
		return _key_pictures[keycode] as Texture2D
	var plate := load_glyph_texture(glyph_path("key", "blank"))
	var image: Image
	if plate != null:
		image = plate.get_image()
		if image != null:
			image.convert(Image.FORMAT_RGBA8)
	if image == null or image.is_empty():
		image = Image.create(128, 128, false, Image.FORMAT_RGBA8)
		image.fill(Color(0.18, 0.14, 0.10, 1.0))
	var label := keycode_label(keycode)
	_stamp_key_label(image, label)
	var texture := ImageTexture.create_from_image(image)
	_key_pictures[keycode] = texture
	return texture


static func _stamp_key_label(image: Image, label: String) -> void:
	var glyphs := label.to_upper()
	if glyphs.is_empty():
		return
	var scale := 4 if glyphs.length() <= 2 else (3 if glyphs.length() <= 4 else 2)
	var glyph_w := 5 * scale
	var glyph_h := 7 * scale
	var gap := scale
	var total_w := glyphs.length() * glyph_w + (glyphs.length() - 1) * gap
	var origin_x := int((image.get_width() - total_w) / 2.0)
	var origin_y := int((image.get_height() - glyph_h) / 2.0)
	var ink := Color(0.97, 0.92, 0.78, 1.0)
	for index in range(glyphs.length()):
		_blit_bitmap_glyph(
			image,
			glyphs.substr(index, 1),
			origin_x + index * (glyph_w + gap),
			origin_y,
			scale,
			ink
		)


static func _blit_bitmap_glyph(image: Image, ch: String, origin_x: int, origin_y: int, scale: int, ink: Color) -> void:
	var rows: PackedStringArray = _bitmap_glyph(ch)
	for row_i in range(rows.size()):
		var row := rows[row_i]
		for col_i in range(row.length()):
			if row[col_i] != "#":
				continue
			for dy in range(scale):
				for dx in range(scale):
					var px := origin_x + col_i * scale + dx
					var py := origin_y + row_i * scale + dy
					if px >= 0 and py >= 0 and px < image.get_width() and py < image.get_height():
						image.set_pixel(px, py, ink)


static func _bitmap_glyph(ch: String) -> PackedStringArray:
	## 5x7 cells. Unique bit pattern per character so W ≠ A on the plate.
	match ch:
		"A":
			return PackedStringArray([".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"])
		"B":
			return PackedStringArray(["####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."])
		"C":
			return PackedStringArray([".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."])
		"D":
			return PackedStringArray(["####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."])
		"E":
			return PackedStringArray(["#####", "#....", "#....", "####.", "#....", "#....", "#####"])
		"F":
			return PackedStringArray(["#####", "#....", "#....", "####.", "#....", "#....", "#...."])
		"G":
			return PackedStringArray([".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".###."])
		"H":
			return PackedStringArray(["#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"])
		"I":
			return PackedStringArray([".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."])
		"J":
			return PackedStringArray(["..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."])
		"K":
			return PackedStringArray(["#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"])
		"L":
			return PackedStringArray(["#....", "#....", "#....", "#....", "#....", "#....", "#####"])
		"M":
			return PackedStringArray(["#...#", "##.##", "#.#.#", "#...#", "#...#", "#...#", "#...#"])
		"N":
			return PackedStringArray(["#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#", "#...#"])
		"O":
			return PackedStringArray([".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."])
		"P":
			return PackedStringArray(["####.", "#...#", "#...#", "####.", "#....", "#....", "#...."])
		"Q":
			return PackedStringArray([".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"])
		"R":
			return PackedStringArray(["####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"])
		"S":
			return PackedStringArray([".###.", "#...#", "#....", ".###.", "....#", "#...#", ".###."])
		"T":
			return PackedStringArray(["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."])
		"U":
			return PackedStringArray(["#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."])
		"V":
			return PackedStringArray(["#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."])
		"W":
			return PackedStringArray(["#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#"])
		"X":
			return PackedStringArray(["#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"])
		"Y":
			return PackedStringArray(["#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."])
		"Z":
			return PackedStringArray(["#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"])
		"0":
			return PackedStringArray([".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."])
		"1":
			return PackedStringArray(["..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."])
		"2":
			return PackedStringArray([".###.", "#...#", "....#", "..##.", ".#...", "#....", "#####"])
		"3":
			return PackedStringArray([".###.", "#...#", "....#", "..##.", "....#", "#...#", ".###."])
		"4":
			return PackedStringArray(["...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."])
		"5":
			return PackedStringArray(["#####", "#....", "####.", "....#", "....#", "#...#", ".###."])
		"6":
			return PackedStringArray([".###.", "#....", "####.", "#...#", "#...#", "#...#", ".###."])
		"7":
			return PackedStringArray(["#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."])
		"8":
			return PackedStringArray([".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."])
		"9":
			return PackedStringArray([".###.", "#...#", "#...#", ".####", "....#", "....#", ".###."])
		"-":
			return PackedStringArray([".....", ".....", ".....", "#####", ".....", ".....", "....."])
		"/":
			return PackedStringArray(["....#", "...#.", "...#.", "..#..", ".#...", ".#...", "#...."])
		" ":
			return PackedStringArray([".....", ".....", ".....", ".....", ".....", ".....", "....."])
	return PackedStringArray(["#####", "#...#", "#.#.#", "#...#", "#.#.#", "#...#", "#####"])


static func joy_glyph_path(button: int, family: String = "xbox") -> String:
	var token := joy_token(button)
	if token == "":
		return ""
	return glyph_path("joy", token, family)


static func load_glyph_texture(path: String) -> Texture2D:
	if path == "":
		return null
	if ResourceLoader.exists(path):
		var imported: Resource = ResourceLoader.load(path)
		if imported is Texture2D:
			return imported as Texture2D
	return null


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
	var controls_error := save_controls(DEFAULT_SCROLL_SPEED, DEFAULT_SHOW_ALL_HEALTH_BARS)
	var bind_error := reset_bindings()
	if controls_error != OK:
		return controls_error
	return bind_error


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

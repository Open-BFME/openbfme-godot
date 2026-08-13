class_name OpenBFMEOptionsScreen
extends Control
## Retail-style OPTIONS screen: three green columns (Display Options / Audio
## Controls / Controls) with CANCEL / RESET TO DEFAULTS / ACCEPT, built once
## and reused by the main menu's options page and the slice's pause menu.
## Edits are pending: ACCEPT applies and persists, CANCEL discards, and RESET
## TO DEFAULTS applies the defaults immediately (retail semantics).

signal closed(applied: bool)

const ThemeScript = preload("res://src/ui/openbfme_theme.gd")
const UserSettingsScript = preload("res://src/ui/user_settings.gd")

const RESOLUTIONS: Array[Vector2i] = [
	Vector2i(1280, 720), Vector2i(1366, 768), Vector2i(1600, 900),
	Vector2i(1920, 1080), Vector2i(1920, 1200), Vector2i(2560, 1440),
]
const WINDOW_MODE_IDS: Array[String] = ["windowed", "borderless", "fullscreen_exclusive"]
const WINDOW_MODE_LABELS := {
	"windowed": "Windowed",
	"borderless": "Borderless",
	"fullscreen_exclusive": "Exclusive Fullscreen",
}
const GRAPHICS_PRESET_IDS: Array[String] = ["low", "medium", "high", "ultra_high", "custom"]
const GRAPHICS_PRESET_LABELS := {
	"low": "Low",
	"medium": "Medium",
	"high": "High",
	"ultra_high": "Ultra High",
	"custom": "Custom",
}
## Only knobs that actually exist in this project are driven: viewport MSAA
## and the directional shadow atlas size.
const GRAPHICS_PRESET_KNOBS := {
	"low": {"msaa": Viewport.MSAA_DISABLED, "shadow_size": 1024},
	"medium": {"msaa": Viewport.MSAA_2X, "shadow_size": 2048},
	"high": {"msaa": Viewport.MSAA_2X, "shadow_size": 4096},
	"ultra_high": {"msaa": Viewport.MSAA_4X, "shadow_size": 4096},
}

var shell_font: Font = null
## Optional live sinks: the retail slice audio object (set_music_volume /
## set_voice_sfx_volume / set_muted) and the slice itself for the camera
## scroll-speed scale. When null the setting still persists; it is simply
## consumed at the next boot/configure.
var audio_system: Object = null
var scroll_target: Object = null

var window_mode_opt: OptionButton
var resolution_opt: OptionButton
var preset_opt: OptionButton
var health_toggle: CheckButton
var scroll_slider: HSlider
var scroll_value_label: Label
var music_slider: HSlider
var sfx_slider: HSlider
var mute_toggle: CheckButton
var accept_btn: Button
var cancel_btn: Button
var reset_btn: Button
var controller_family_opt: OptionButton
var _binding_rows: Dictionary = {}
var _pending_bindings: Dictionary = {}
var _listen_action := ""
var _loading := false


func _ready() -> void:
	theme = ThemeScript.create_theme(shell_font)
	_build()
	_load_from_store()


func configure(context: Dictionary) -> void:
	shell_font = context.get("font")
	audio_system = context.get("audio_system")
	scroll_target = context.get("scroll_target")
	if is_inside_tree():
		theme = ThemeScript.create_theme(shell_font)
		_load_from_store()


func open() -> void:
	_load_from_store()
	visible = true


func reload_from_store() -> void:
	## Re-read the persisted settings into the controls WITHOUT opening the screen.
	## The F11 fullscreen binding writes the window mode straight to the store (see
	## `main_menu.gd:toggle_fullscreen`), and an options screen still showing
	## "Windowed" after the player put the game fullscreen is a settings panel that
	## lies about the state of the machine it is settings for.
	if window_mode_opt == null:
		return
	_load_from_store()


func accept() -> void:
	apply_and_persist()
	closed.emit(true)
	visible = false


func cancel() -> void:
	closed.emit(false)
	visible = false


func reset_to_defaults() -> void:
	var audio_error: Error = UserSettingsScript.reset_audio()
	var display_error: Error = UserSettingsScript.reset_display()
	var graphics_error: Error = UserSettingsScript.reset_graphics()
	var controls_error: Error = UserSettingsScript.reset_controls()
	if audio_error != OK or display_error != OK or graphics_error != OK or controls_error != OK:
		push_warning("OpenBFME options could not persist defaults.")
	_load_from_store()
	_apply_stored()


func apply_stored_settings() -> void:
	## Boot-time seam: applies whatever the store holds (display, graphics,
	## controls, audio) without touching any control state.
	_apply_stored()


func apply_and_persist() -> void:
	var window_mode := WINDOW_MODE_IDS[window_mode_opt.selected]
	var resolution := String(resolution_opt.get_item_metadata(resolution_opt.selected))
	if UserSettingsScript.save_display(window_mode, resolution) == OK:
		_apply_display(window_mode, resolution)
	var preset := GRAPHICS_PRESET_IDS[preset_opt.selected]
	if UserSettingsScript.save_graphics(preset) == OK:
		_apply_graphics(preset)
	var scroll := float(scroll_slider.value)
	var health := health_toggle.button_pressed
	if UserSettingsScript.save_controls(scroll, health) == OK:
		_apply_scroll_speed(scroll)
	var family := UserSettingsScript.CONTROLLER_FAMILIES[controller_family_opt.selected] if controller_family_opt != null else UserSettingsScript.DEFAULT_CONTROLLER_FAMILY
	if UserSettingsScript.save_bindings(_pending_bindings, family) == OK:
		UserSettingsScript.apply_bindings_to_input_map(_pending_bindings)
	if UserSettingsScript.save_audio(float(music_slider.value), float(sfx_slider.value), mute_toggle.button_pressed) == OK:
		_apply_audio_live(float(music_slider.value), float(sfx_slider.value), mute_toggle.button_pressed)


func _apply_stored() -> void:
	var display := UserSettingsScript.load_display()
	_apply_display(String(display["window_mode"]), String(display["resolution"]))
	_apply_graphics(String(UserSettingsScript.load_graphics()["preset"]))
	var controls := UserSettingsScript.load_controls()
	_apply_scroll_speed(float(controls["scroll_speed"]))
	UserSettingsScript.apply_bindings_to_input_map()
	var audio := UserSettingsScript.load_audio()
	_apply_audio_live(float(audio["music_volume"]), float(audio["voice_sfx_volume"]), bool(audio["muted"]))


static func apply_display_settings(window_mode: String, resolution: String) -> void:
	## Static window-mode/resolution applier shared by the menu, the options
	## screen, and the slice's own boot: the user's choice must survive every
	## scene transition and every entry path. Exclusive fullscreen applies the
	## chosen resolution as the display mode; borderless keeps the native one.
	##
	## MULTI-MONITOR: always re-apply against the screen the window is already
	## on. Centering with the no-argument `screen_get_size()` uses the primary
	## display (often the left-most), which is exactly how ACCEPT on OPTIONS
	## used to yank the game off the player's main monitor.
	var parts := resolution.split("x", false)
	if parts.size() != 2:
		return
	var size := Vector2i(int(parts[0]), int(parts[1]))
	var screen_index := DisplayServer.window_get_current_screen()
	if screen_index < 0:
		screen_index = 0
	var screen_size := DisplayServer.screen_get_size(screen_index)
	var screen_origin := DisplayServer.screen_get_position(screen_index)
	var prior_position := DisplayServer.window_get_position()
	match window_mode:
		"borderless":
			DisplayServer.window_set_current_screen(screen_index)
			DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_FULLSCREEN)
		"fullscreen_exclusive":
			DisplayServer.window_set_current_screen(screen_index)
			DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_EXCLUSIVE_FULLSCREEN)
			DisplayServer.window_set_size(size)
		_:
			DisplayServer.window_set_flag(DisplayServer.WINDOW_FLAG_BORDERLESS, false)
			DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_WINDOWED)
			var clamped := Vector2i(
				mini(size.x, maxi(640, screen_size.x)),
				mini(size.y, maxi(480, screen_size.y))
			)
			DisplayServer.window_set_size(clamped)
			DisplayServer.window_set_current_screen(screen_index)
			var next_position := _window_position_on_screen(
				prior_position, clamped, screen_origin, screen_size
			)
			DisplayServer.window_set_position(next_position)


static func _window_position_on_screen(
	prior: Vector2i, window_size: Vector2i, screen_origin: Vector2i, screen_size: Vector2i
) -> Vector2i:
	## Keep the window on the same monitor it was on. If the prior top-left still
	## lands mostly inside that screen after the resize, clamp it there; otherwise
	## re-center on that screen only (never on the primary by accident).
	var screen_rect := Rect2i(screen_origin, screen_size)
	var prior_rect := Rect2i(prior, window_size)
	var overlap := screen_rect.intersection(prior_rect).get_area()
	var area := maxi(1, window_size.x * window_size.y)
	if overlap * 5 >= area * 2:
		var max_x := screen_origin.x + maxi(0, screen_size.x - window_size.x)
		var max_y := screen_origin.y + maxi(0, screen_size.y - window_size.y)
		return Vector2i(clampi(prior.x, screen_origin.x, max_x), clampi(prior.y, screen_origin.y, max_y))
	return screen_origin + (screen_size - window_size) / 2


static func apply_graphics_preset(preset: String, viewport: Viewport) -> void:
	## Static graphics-preset applier shared by the menu and the slice boot.
	var knobs: Variant = GRAPHICS_PRESET_KNOBS.get(preset, null)
	if knobs == null:
		# "custom" leaves whatever knobs the user already has untouched.
		return
	if viewport != null:
		viewport.msaa_3d = int(knobs["msaa"])
	RenderingServer.directional_shadow_atlas_set_size(int(knobs["shadow_size"]), true)


func _apply_display(window_mode: String, resolution: String) -> void:
	apply_display_settings(window_mode, resolution)


func _apply_graphics(preset: String) -> void:
	apply_graphics_preset(preset, get_viewport())


func _apply_scroll_speed(scroll: float) -> void:
	if scroll_target != null:
		scroll_target.set("keyboard_scroll_speed_scale", scroll)


func _apply_audio_live(music: float, sfx: float, muted: bool) -> void:
	if audio_system == null:
		return
	if audio_system.has_method("set_music_volume"):
		audio_system.set_music_volume(music)
	if audio_system.has_method("set_voice_sfx_volume"):
		audio_system.set_voice_sfx_volume(sfx)
	if audio_system.has_method("set_muted"):
		audio_system.set_muted(muted)


func _load_from_store() -> void:
	_loading = true
	var display := UserSettingsScript.load_display()
	_select_option_by_metadata(window_mode_opt, String(display["window_mode"]))
	var resolution := String(display["resolution"])
	if not _select_option_by_metadata(resolution_opt, resolution):
		resolution_opt.add_item(resolution)
		resolution_opt.set_item_metadata(resolution_opt.item_count - 1, resolution)
		resolution_opt.select(resolution_opt.item_count - 1)
	_select_option_by_metadata(preset_opt, String(UserSettingsScript.load_graphics()["preset"]))
	var controls := UserSettingsScript.load_controls()
	scroll_slider.value = float(controls["scroll_speed"])
	health_toggle.button_pressed = bool(controls["show_all_health_bars"])
	var audio := UserSettingsScript.load_audio()
	music_slider.value = float(audio["music_volume"])
	sfx_slider.value = float(audio["voice_sfx_volume"])
	mute_toggle.button_pressed = bool(audio["muted"])
	_pending_bindings = UserSettingsScript.load_bindings()
	if controller_family_opt != null:
		var family := UserSettingsScript.load_controller_family()
		var family_idx := UserSettingsScript.CONTROLLER_FAMILIES.find(family)
		controller_family_opt.select(family_idx if family_idx >= 0 else 0)
	_refresh_binding_rows()
	_loading = false
	_update_slider_labels()


func _select_option_by_metadata(option: OptionButton, metadata_value: String) -> bool:
	for index in range(option.item_count):
		if String(option.get_item_metadata(index)) == metadata_value:
			option.select(index)
			return true
	return false


func _build() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	var dim := ColorRect.new()
	dim.name = "Dim"
	dim.color = Color(0.0, 0.0, 0.0, 0.55)
	dim.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	dim.mouse_filter = Control.MOUSE_FILTER_STOP
	add_child(dim)
	var center := CenterContainer.new()
	center.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	center.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(center)
	var column := VBoxContainer.new()
	column.custom_minimum_size = Vector2(1520, 0)
	column.add_theme_constant_override("separation", 14)
	center.add_child(column)

	var title := Label.new()
	title.name = "Title"
	title.text = "OPTIONS"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.add_theme_color_override("font_color", Color("9ec97e"))
	title.add_theme_font_size_override("font_size", 44)
	column.add_child(title)
	var subtitle := Label.new()
	subtitle.text = "SETTINGS"
	subtitle.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	subtitle.add_theme_color_override("font_color", Color("d8e6da"))
	subtitle.add_theme_font_size_override("font_size", 18)
	column.add_child(subtitle)

	var columns := HBoxContainer.new()
	columns.add_theme_constant_override("separation", 16)
	columns.size_flags_vertical = Control.SIZE_EXPAND_FILL
	column.add_child(columns)
	_build_display_column(columns)
	_build_audio_column(columns)
	_build_controls_column(columns)

	var button_row := HBoxContainer.new()
	button_row.alignment = BoxContainer.ALIGNMENT_CENTER
	button_row.add_theme_constant_override("separation", 24)
	column.add_child(button_row)
	cancel_btn = _make_button(button_row, "CancelButton", "CANCEL", "Discard changes and close")
	reset_btn = _make_button(button_row, "ResetButton", "RESET TO DEFAULTS", "Restore every setting to its default")
	accept_btn = _make_button(button_row, "AcceptButton", "ACCEPT", "Apply and save these settings")
	accept_btn.theme_type_variation = "PrimaryButton"
	cancel_btn.pressed.connect(cancel)
	reset_btn.pressed.connect(reset_to_defaults)
	accept_btn.pressed.connect(accept)


func _build_display_column(parent: Control) -> void:
	var panel := _make_column(parent, "DisplayColumn", "Display Options")
	window_mode_opt = _make_option(panel, "WindowModeOption", "Window Mode", WINDOW_MODE_IDS, WINDOW_MODE_LABELS)
	resolution_opt = _make_option(panel, "ResolutionOption", "Resolution", [], {})
	for resolution in RESOLUTIONS:
		var label := "%dx%d" % [resolution.x, resolution.y]
		resolution_opt.add_item(label)
		resolution_opt.set_item_metadata(resolution_opt.item_count - 1, label)
	preset_opt = _make_option(panel, "GraphicsPresetOption", "Graphics", GRAPHICS_PRESET_IDS, GRAPHICS_PRESET_LABELS)
	health_toggle = CheckButton.new()
	health_toggle.name = "HealthBarsToggle"
	health_toggle.text = "Show All Health Bars"
	health_toggle.tooltip_text = "Persisted now; consumed once the member-health overlay gains a show-all mode"
	panel.add_child(health_toggle)


func _build_audio_column(parent: Control) -> void:
	var panel := _make_column(parent, "AudioColumn", "Audio Controls")
	music_slider = _make_slider(panel, "MusicSlider", "Music", 0.0, 1.0, 0.01)
	sfx_slider = _make_slider(panel, "SoundFxSlider", "Sound FX", 0.0, 1.0, 0.01)
	mute_toggle = CheckButton.new()
	mute_toggle.name = "MuteToggle"
	mute_toggle.text = "Mute all audio"
	panel.add_child(mute_toggle)


func _build_controls_column(parent: Control) -> void:
	var panel := _make_column(parent, "ControlsColumn", "Controls")
	scroll_slider = _make_slider(panel, "ScrollSpeedSlider", "Scroll Speed", 0.5, 2.0, 0.05)
	scroll_value_label = Label.new()
	scroll_value_label.name = "ScrollSpeedValue"
	scroll_value_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	scroll_value_label.add_theme_color_override("font_color", Color("c9d4b8"))
	panel.add_child(scroll_value_label)
	_build_key_bindings(panel)


func _build_key_bindings(panel: Control) -> void:
	var heading_row := HBoxContainer.new()
	heading_row.alignment = BoxContainer.ALIGNMENT_CENTER
	heading_row.add_theme_constant_override("separation", 8)
	panel.add_child(heading_row)
	var heading_icon := TextureRect.new()
	heading_icon.name = "KeyBindingsIcon"
	heading_icon.custom_minimum_size = Vector2(28, 28)
	heading_icon.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	heading_icon.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	var rebind_icon := UserSettingsScript.glyph_path("rebind", "listen")
	if rebind_icon != "":
		heading_icon.texture = UserSettingsScript.load_glyph_texture(rebind_icon)
	heading_row.add_child(heading_icon)
	var heading := Label.new()
	heading.name = "KeyBindingsHeading"
	heading.text = "Key Settings"
	heading.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	heading.add_theme_color_override("font_color", Color("b7dc94"))
	heading.add_theme_font_size_override("font_size", 15)
	heading.mouse_filter = Control.MOUSE_FILTER_IGNORE
	heading_row.add_child(heading)
	var family_row := HBoxContainer.new()
	family_row.name = "ControllerFamilyRow"
	family_row.alignment = BoxContainer.ALIGNMENT_CENTER
	family_row.add_theme_constant_override("separation", 8)
	panel.add_child(family_row)
	var family_icon := TextureRect.new()
	family_icon.name = "ControllerFamilyIcon"
	family_icon.custom_minimum_size = Vector2(32, 32)
	family_icon.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	family_icon.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	family_row.add_child(family_icon)
	controller_family_opt = OptionButton.new()
	controller_family_opt.name = "ControllerFamilyOpt"
	controller_family_opt.add_item("Xbox controller")
	controller_family_opt.add_item("Steam Controller")
	controller_family_opt.item_selected.connect(_on_controller_family_changed)
	family_row.add_child(controller_family_opt)
	var hint := Label.new()
	hint.name = "KeyBindingHint"
	hint.text = "Click a row, then press a key or controller button."
	hint.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	hint.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	hint.add_theme_color_override("font_color", Color("9aa78d"))
	hint.add_theme_font_size_override("font_size", 11)
	hint.mouse_filter = Control.MOUSE_FILTER_IGNORE
	panel.add_child(hint)
	var scroll := ScrollContainer.new()
	scroll.name = "KeyBindingScroll"
	scroll.custom_minimum_size = Vector2(0, 260)
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	panel.add_child(scroll)
	var rows := VBoxContainer.new()
	rows.name = "KeyBindingRows"
	rows.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	rows.add_theme_constant_override("separation", 3)
	scroll.add_child(rows)
	_binding_rows.clear()
	for spec_value in UserSettingsScript.REMAPPABLE_ACTIONS:
		var spec := spec_value as Dictionary
		var action_id := String(spec["id"])
		var row := Button.new()
		row.name = "KeyBinding_%s" % action_id
		row.custom_minimum_size = Vector2(0, 34)
		row.pressed.connect(_begin_rebind.bind(action_id))
		var box := HBoxContainer.new()
		box.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
		box.offset_left = 6
		box.offset_right = -6
		box.mouse_filter = Control.MOUSE_FILTER_IGNORE
		box.add_theme_constant_override("separation", 6)
		row.add_child(box)
		var action_label := Label.new()
		action_label.name = "ActionLabel"
		action_label.text = String(spec["label"])
		action_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		action_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
		action_label.add_theme_color_override("font_color", Color("d8e6da"))
		action_label.add_theme_font_size_override("font_size", 13)
		box.add_child(action_label)
		var key_icon := TextureRect.new()
		key_icon.name = "KeyIcon"
		key_icon.custom_minimum_size = Vector2(28, 28)
		key_icon.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		key_icon.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		key_icon.mouse_filter = Control.MOUSE_FILTER_IGNORE
		box.add_child(key_icon)
		var key_caption := Label.new()
		key_caption.name = "KeyCaption"
		key_caption.custom_minimum_size = Vector2(36, 0)
		key_caption.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		key_caption.mouse_filter = Control.MOUSE_FILTER_IGNORE
		key_caption.add_theme_color_override("font_color", Color("f0e6c8"))
		key_caption.add_theme_font_size_override("font_size", 12)
		box.add_child(key_caption)
		var joy_icon := TextureRect.new()
		joy_icon.name = "JoyIcon"
		joy_icon.custom_minimum_size = Vector2(28, 28)
		joy_icon.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		joy_icon.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		joy_icon.mouse_filter = Control.MOUSE_FILTER_IGNORE
		box.add_child(joy_icon)
		var joy_caption := Label.new()
		joy_caption.name = "JoyCaption"
		joy_caption.custom_minimum_size = Vector2(40, 0)
		joy_caption.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		joy_caption.mouse_filter = Control.MOUSE_FILTER_IGNORE
		joy_caption.add_theme_color_override("font_color", Color("c9d4b8"))
		joy_caption.add_theme_font_size_override("font_size", 11)
		box.add_child(joy_caption)
		rows.add_child(row)
		_binding_rows[action_id] = {
			"button": row,
			"key_icon": key_icon,
			"key_caption": key_caption,
			"joy_icon": joy_icon,
			"joy_caption": joy_caption,
		}
	_pending_bindings = UserSettingsScript.load_bindings()
	_refresh_binding_rows()


func _on_controller_family_changed(_index: int) -> void:
	if _loading:
		return
	_refresh_binding_rows()


func _begin_rebind(action_id: String) -> void:
	_listen_action = action_id
	_refresh_binding_rows()


func _steal_binding(field: String, value: int, keep_action: String) -> void:
	if value < 0:
		return
	for other_id in _pending_bindings.keys():
		if String(other_id) == keep_action:
			continue
		var other: Dictionary = _pending_bindings[other_id] as Dictionary
		if int(other.get(field, -2)) == value:
			other[field] = 0 if field == "key" else -1
			_pending_bindings[other_id] = other


func _refresh_binding_rows() -> void:
	var family := UserSettingsScript.CONTROLLER_FAMILIES[controller_family_opt.selected] if controller_family_opt != null else UserSettingsScript.DEFAULT_CONTROLLER_FAMILY
	var family_icon := find_child("ControllerFamilyIcon", true, false) as TextureRect
	if family_icon != null:
		var icon_path := UserSettingsScript.glyph_path("controller", family)
		family_icon.texture = UserSettingsScript.load_glyph_texture(icon_path)
	var listen_path := UserSettingsScript.glyph_path("rebind", "listen")
	for spec_value in UserSettingsScript.REMAPPABLE_ACTIONS:
		var spec := spec_value as Dictionary
		var action_id := String(spec["id"])
		var widgets: Dictionary = _binding_rows.get(action_id, {}) as Dictionary
		var row: Button = widgets.get("button") as Button
		if row == null:
			continue
		var bind: Dictionary = _pending_bindings.get(action_id, {"key": spec["default_key"], "joy": spec["default_joy"]}) as Dictionary
		var keycode := int(bind.get("key", spec["default_key"]))
		var joy := int(bind.get("joy", spec["default_joy"]))
		var key_icon := widgets.get("key_icon") as TextureRect
		var key_caption := widgets.get("key_caption") as Label
		var joy_icon := widgets.get("joy_icon") as TextureRect
		var joy_caption := widgets.get("joy_caption") as Label
		if _listen_action == action_id:
			row.tooltip_text = "Press a key or controller button. Esc cancels."
			if key_icon != null:
				key_icon.texture = UserSettingsScript.load_glyph_texture(listen_path)
			if key_caption != null:
				key_caption.text = "…"
			if joy_icon != null:
				joy_icon.texture = null
			if joy_caption != null:
				joy_caption.text = ""
			continue
		var key_text := UserSettingsScript.keycode_label(keycode)
		var joy_text := UserSettingsScript.joy_label(joy, family)
		row.tooltip_text = "%s — click to rebind" % String(spec["label"])
		if key_icon != null:
			key_icon.texture = UserSettingsScript.key_picture(keycode)
		if key_caption != null:
			key_caption.text = key_text
		if joy_icon != null:
			var joy_path := UserSettingsScript.joy_glyph_path(joy, family)
			joy_icon.texture = UserSettingsScript.load_glyph_texture(joy_path)
		if joy_caption != null:
			joy_caption.text = joy_text


func _input(event: InputEvent) -> void:
	if _listen_action == "" or not visible:
		return
	if event is InputEventKey and (event as InputEventKey).pressed and not (event as InputEventKey).echo:
		var key := event as InputEventKey
		if key.physical_keycode == KEY_ESCAPE:
			_listen_action = ""
			_refresh_binding_rows()
			get_viewport().set_input_as_handled()
			return
		var row: Dictionary = _pending_bindings.get(_listen_action, {}) as Dictionary
		row["key"] = int(key.physical_keycode)
		if not row.has("joy"):
			row["joy"] = -1
		_steal_binding("key", int(key.physical_keycode), _listen_action)
		_pending_bindings[_listen_action] = row
		_listen_action = ""
		_refresh_binding_rows()
		get_viewport().set_input_as_handled()
	elif event is InputEventJoypadButton and (event as InputEventJoypadButton).pressed:
		var joy := event as InputEventJoypadButton
		var row: Dictionary = _pending_bindings.get(_listen_action, {}) as Dictionary
		row["joy"] = int(joy.button_index)
		if not row.has("key"):
			row["key"] = 0
		_steal_binding("joy", int(joy.button_index), _listen_action)
		_pending_bindings[_listen_action] = row
		_listen_action = ""
		_refresh_binding_rows()
		get_viewport().set_input_as_handled()


func _make_column(parent: Control, node_name: String, heading: String) -> VBoxContainer:
	var panel := Panel.new()
	panel.name = node_name
	panel.theme_type_variation = "OverlayPanel"
	panel.custom_minimum_size = Vector2(480, 560)
	panel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	parent.add_child(panel)
	var box := VBoxContainer.new()
	box.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	box.offset_left = 14
	box.offset_top = 12
	box.offset_right = -14
	box.offset_bottom = -14
	box.add_theme_constant_override("separation", 10)
	panel.add_child(box)
	var header_panel := Panel.new()
	header_panel.theme_type_variation = "ColumnHeader"
	header_panel.mouse_filter = Control.MOUSE_FILTER_IGNORE
	header_panel.custom_minimum_size = Vector2(0, 34)
	box.add_child(header_panel)
	var header := Label.new()
	header.text = heading
	header.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	header.add_theme_color_override("font_color", Color("b7dc94"))
	header.add_theme_font_size_override("font_size", 20)
	header_panel.add_child(header)
	header.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	return box


func _make_option(parent: Control, node_name: String, caption: String, ids: Array, labels: Dictionary) -> OptionButton:
	var label := Label.new()
	label.text = caption
	label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	label.add_theme_color_override("font_color", Color("b7dc94"))
	label.add_theme_font_size_override("font_size", 15)
	parent.add_child(label)
	var option := OptionButton.new()
	option.name = node_name
	option.custom_minimum_size = Vector2(0, 40)
	for id_value in ids:
		option.add_item(String(labels.get(id_value, String(id_value).capitalize())))
		option.set_item_metadata(option.item_count - 1, String(id_value))
	parent.add_child(option)
	return option


func _make_slider(parent: Control, node_name: String, caption: String, minimum: float, maximum: float, step: float) -> HSlider:
	var label := Label.new()
	label.text = caption
	label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	label.add_theme_color_override("font_color", Color("b7dc94"))
	label.add_theme_font_size_override("font_size", 15)
	parent.add_child(label)
	var slider := HSlider.new()
	slider.name = node_name
	slider.min_value = minimum
	slider.max_value = maximum
	slider.step = step
	slider.custom_minimum_size = Vector2(0, 32)
	slider.value_changed.connect(func(_value: float) -> void: _update_slider_labels())
	parent.add_child(slider)
	return slider


func _make_button(parent: Control, node_name: String, text: String, tooltip: String) -> Button:
	var button := Button.new()
	button.name = node_name
	button.text = text
	button.tooltip_text = tooltip
	button.custom_minimum_size = Vector2(260, 52)
	parent.add_child(button)
	return button


func _update_slider_labels() -> void:
	if _loading:
		return
	if scroll_value_label != null:
		scroll_value_label.text = "x%.2f" % float(scroll_slider.value)

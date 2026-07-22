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
	if UserSettingsScript.save_audio(float(music_slider.value), float(sfx_slider.value), mute_toggle.button_pressed) == OK:
		_apply_audio_live(float(music_slider.value), float(sfx_slider.value), mute_toggle.button_pressed)


func _apply_stored() -> void:
	var display := UserSettingsScript.load_display()
	_apply_display(String(display["window_mode"]), String(display["resolution"]))
	_apply_graphics(String(UserSettingsScript.load_graphics()["preset"]))
	var controls := UserSettingsScript.load_controls()
	_apply_scroll_speed(float(controls["scroll_speed"]))
	var audio := UserSettingsScript.load_audio()
	_apply_audio_live(float(audio["music_volume"]), float(audio["voice_sfx_volume"]), bool(audio["muted"]))


static func apply_display_settings(window_mode: String, resolution: String) -> void:
	## Static window-mode/resolution applier shared by the menu, the options
	## screen, and the slice's own boot: the user's choice must survive every
	## scene transition and every entry path. Exclusive fullscreen applies the
	## chosen resolution as the display mode; borderless keeps the native one.
	var parts := resolution.split("x", false)
	if parts.size() != 2:
		return
	var size := Vector2i(int(parts[0]), int(parts[1]))
	match window_mode:
		"borderless":
			DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_FULLSCREEN)
		"fullscreen_exclusive":
			DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_EXCLUSIVE_FULLSCREEN)
			DisplayServer.window_set_size(size)
		_:
			DisplayServer.window_set_flag(DisplayServer.WINDOW_FLAG_BORDERLESS, false)
			DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_WINDOWED)
			var screen := DisplayServer.screen_get_size()
			var clamped := Vector2i(mini(size.x, maxi(640, screen.x)), mini(size.y, maxi(480, screen.y)))
			DisplayServer.window_set_size(clamped)
			if screen.x > clamped.x and screen.y > clamped.y:
				DisplayServer.window_set_position((screen - clamped) / 2)


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


func _make_column(parent: Control, node_name: String, heading: String) -> VBoxContainer:
	var panel := Panel.new()
	panel.name = node_name
	panel.theme_type_variation = "OverlayPanel"
	panel.custom_minimum_size = Vector2(480, 430)
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

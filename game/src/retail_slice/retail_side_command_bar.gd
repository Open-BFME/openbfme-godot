class_name RetailSideCommandBar
extends Control
## Retail 1.06 InGameSideCommandBar: a vertical column of circular build
## buttons pinned to the right screen edge, shown only while the selection
## includes the builder (retail shows the constructible buildings here while the
## palantir keeps unit commands). Each button re-emits the SAME construct order
## as the radial construct buttons; the HUD relays it into construct_requested.
##
## Geometry source: the pack's data/ui/palantir/scene-contract.json
## frameSelection.inGameSideCommandBar.buttonSetTranslation = (1048.3, 361.3)
## in the authored 1024x768 space (authoredResolution). The column therefore
## anchors to the right edge; per-button pitch follows the character-18 button
## bounds (100 authored units ~= the 64 px socket the radial commands already
## use). The sealed local topology (sideCommandTopology.buttonSet.localButtons)
## statically places Button0..Button11 top-to-bottom, so the column grows
## downward from the anchor.
##
## Fade: the converted contract exposes a frame-stepped fade
## (sideCommandFadeRuntime, 0.033 s/frame, fade-in frames 12->22 ~= 0.33 s).
## The runtime frame runner lives in retail_hud_apt_runtime.gd (owned
## elsewhere); this widget performs the equivalent visible alpha transition over
## SIDE_COMMAND_FADE_SECONDS so the on-screen bar honors the documented timing
## without reaching into that runtime. See report note.

signal construct_requested(structure_kind: String)

const AUTHORED_RESOLUTION := Vector2(1024.0, 768.0)
const BUTTON_SET_TRANSLATION := Vector2(1048.3, 361.3)
const BUTTON_DIAMETER := 60.0
const BUTTON_PITCH := 70.0
const RIGHT_EDGE_MARGIN := 14.0
const SIDE_COMMAND_FADE_SECONDS := 0.33

var _buttons: Array[Button] = []
var _socket_texture: Texture2D
var _shown := false
var _tween: Tween


func _ready() -> void:
	if _buttons.is_empty():
		_build()


## kinds: ordered construct structure kinds (e.g. ["farm", ...]). labels/tips
## come from the HUD's already-validated construct commands so no strings are
## invented here.
func _build() -> void:
	name = "RetailSideCommandBar"
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	z_index = 6
	visible = false
	modulate.a = 0.0


func configure_from_constructs(constructs: Array) -> void:
	## constructs: [{"kind": String, "icon": Texture2D, "title": String,
	## "description": String}]. Rebuilds the column each selection so icon
	## bindings stay in lockstep with the HUD's construct commands.
	if _buttons.is_empty() and name != "RetailSideCommandBar":
		_build()
	for button in _buttons:
		button.queue_free()
	_buttons.clear()
	for index in constructs.size():
		var entry: Dictionary = constructs[index]
		var kind := String(entry.get("kind", ""))
		var button := Button.new()
		button.name = "SideBuild%d" % index
		button.custom_minimum_size = Vector2(BUTTON_DIAMETER, BUTTON_DIAMETER)
		button.size = Vector2(BUTTON_DIAMETER, BUTTON_DIAMETER)
		button.mouse_default_cursor_shape = Control.CURSOR_POINTING_HAND
		button.set_meta("construct_kind", kind)
		for state in ["normal", "hover", "pressed", "disabled", "focus"]:
			button.add_theme_stylebox_override(state, StyleBoxEmpty.new())
		if _socket_texture != null:
			var socket := TextureRect.new()
			socket.name = "Socket"
			socket.texture = _socket_texture
			socket.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
			socket.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
			socket.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
			socket.mouse_filter = Control.MOUSE_FILTER_IGNORE
			button.add_child(socket)
		var icon: Texture2D = entry.get("icon")
		if icon != null:
			button.icon = icon
			button.expand_icon = true
			button.add_theme_constant_override("icon_max_width", int(BUTTON_DIAMETER) - 12)
			button.icon_alignment = HORIZONTAL_ALIGNMENT_CENTER
		button.pressed.connect(_on_side_button_pressed.bind(kind))
		add_child(button)
		_buttons.append(button)
	_layout_buttons()


func bind_socket_texture(socket_texture: Texture2D) -> void:
	_socket_texture = socket_texture


func side_buttons() -> Array[Button]:
	return _buttons


func builder_bar_shown() -> bool:
	return _shown


func _layout_buttons() -> void:
	var viewport := get_viewport_rect().size
	if viewport.x <= 0.0 or viewport.y <= 0.0:
		viewport = AUTHORED_RESOLUTION
	# Anchor to the right edge (the authored translation sits at the far right of
	# the 1024-wide authored frame); scale the vertical start from authored space.
	var start_y := BUTTON_SET_TRANSLATION.y / AUTHORED_RESOLUTION.y * viewport.y
	var column_x := viewport.x - BUTTON_DIAMETER - RIGHT_EDGE_MARGIN
	for index in _buttons.size():
		_buttons[index].position = Vector2(column_x, start_y + float(index) * BUTTON_PITCH)


func _on_side_button_pressed(kind: String) -> void:
	construct_requested.emit(kind)


## Shown only while the selection includes the builder; hidden otherwise. The
## visible alpha transition mirrors the documented fade-in/out timing.
func set_builder_visible(builder_selected: bool) -> void:
	if _shown == builder_selected and (visible == builder_selected):
		return
	_shown = builder_selected
	if builder_selected:
		_layout_buttons()
	if not is_inside_tree():
		# No tree (headless bind-time / tests): apply the end state immediately.
		visible = builder_selected
		modulate.a = 1.0 if builder_selected else 0.0
		return
	if _tween != null and _tween.is_valid():
		_tween.kill()
	if builder_selected:
		visible = true
	_tween = create_tween()
	_tween.tween_property(self, "modulate:a", 1.0 if builder_selected else 0.0, SIDE_COMMAND_FADE_SECONDS)
	if not builder_selected:
		_tween.tween_callback(func() -> void: visible = false)

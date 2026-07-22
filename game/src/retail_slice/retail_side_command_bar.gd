class_name RetailSideCommandBar
extends Control
## Retail 1.06 InGameSideCommandBar: a vertical column of circular build
## buttons pinned to the right screen edge, shown only while the selection
## includes the builder (retail shows the constructible buildings here while the
## palantir keeps unit commands). Each button re-emits the SAME construct order
## as the radial construct buttons; the HUD relays it into construct_requested.
##
## Geometry: retail 1.06 (REF-29/32/50, 2560x1440) runs a single column of
## ~110px sockets down the right edge, starting ~130px from the top and
## spanning nearly the full screen height, wrapped in the vine frame. The
## layout below scales socket size/pitch from the viewport height and
## shrink-fits the porter's full authored build set into that one column;
## only an over-long roster wraps into extra columns growing leftward. The
## authored contract anchor (sideCommandTopology.buttonSet) stays documented
## in the constants but the capture-measured span wins, as with the dock.
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
	# The column anchors to the right screen edge; re-pin it when the window
	# size changes so it never drifts off the edge mid-session.
	var viewport := get_viewport()
	if viewport != null and not viewport.size_changed.is_connected(_layout_buttons):
		viewport.size_changed.connect(_layout_buttons)


## kinds: ordered construct structure kinds (e.g. ["farm", ...]). labels/tips
## come from the HUD's already-validated construct commands so no strings are
## invented here.
func _build() -> void:
	name = "RetailSideCommandBar"
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	z_index = 6
	visible = false
	# Buttons are direct children: each is a single CanvasItem (socket art as
	# the button stylebox + icon), so per-button alpha fades cleanly. The old
	# CanvasGroup composite blanked icons and blocked GUI input entirely
	# (Controls under a Node2D never received clicks).
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
		# Socket art lives ONLY in the button stylebox below. A TextureRect child
		# here draws ABOVE the button icon, so at full opacity the opaque socket
		# center covered the icon — icons visibly "faded to black" as the bar's
		# fade-in finished.
		var icon: Texture2D = entry.get("icon")
		if icon != null:
			button.icon = icon
			button.expand_icon = true
			button.add_theme_constant_override("icon_max_width", int(BUTTON_DIAMETER) - 12)
			button.icon_alignment = HORIZONTAL_ALIGNMENT_CENTER
		elif bool(entry.get("text_only", false)):
			# Honest text-only socket for factions whose build art is not
			# converted yet — clearly styled chrome, never borrowed Men art.
			button.text = String(entry.get("title", kind.capitalize()))
			button.add_theme_font_size_override("font_size", 10)
			button.add_theme_color_override("font_color", Color("e6d9ae"))
			button.add_theme_color_override("font_hover_color", Color("fff3c8"))
			button.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
		button.pressed.connect(_on_side_button_pressed.bind(kind))
		if _socket_texture != null:
			var socket_box := StyleBoxTexture.new()
			socket_box.texture = _socket_texture
			for state in ["normal", "hover", "pressed", "disabled", "focus"]:
				button.add_theme_stylebox_override(state, socket_box)
		button.texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS
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
	# Retail (REF-29/32/50, 1440p): a single column of ~110px sockets starting
	# ~130px from the top and spanning nearly the full right edge. Scale the
	# sockets from the viewport height and shrink-to-fit so one column always
	# holds the porter's full authored build set like retail; only an
	# over-long roster wraps into a second column growing leftward.
	var count := maxi(1, _buttons.size())
	var start_y := viewport.y * 0.095
	var bottom_margin := viewport.y * 0.02
	var pitch := viewport.y * 0.077 + 7.0
	var rows := maxi(1, int(floorf((viewport.y - start_y - bottom_margin) / pitch)))
	if rows < count:
		pitch = (viewport.y - start_y - bottom_margin) / float(count)
		rows = count
	var diameter := clampf(pitch - 7.0, 40.0, viewport.y * 0.077)
	for index in _buttons.size():
		var row := index % rows
		var column := index / rows
		var button := _buttons[index]
		button.custom_minimum_size = Vector2(diameter, diameter)
		button.size = Vector2(diameter, diameter)
		button.add_theme_constant_override("icon_max_width", int(diameter) - 12)
		button.position = Vector2(
			viewport.x - RIGHT_EDGE_MARGIN - diameter - float(column) * pitch,
			start_y + float(row) * pitch
		)


func _on_side_button_pressed(kind: String) -> void:
	construct_requested.emit(kind)


## Shown only while the selection includes the builder; hidden otherwise. The
## visible alpha transition mirrors the documented fade-in/out timing.
func set_builder_visible(builder_selected: bool) -> void:
	if _shown == builder_selected and (visible == builder_selected):
		return
	if OS.get_environment("OPENBFME_UI_PROBE") == "1":
		print("[sidebar] set_builder_visible ", builder_selected, " (was shown=", _shown, " visible=", visible, " alpha=", modulate.a, ")")
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

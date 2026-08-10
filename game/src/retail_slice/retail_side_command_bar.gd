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

const FrameScript = preload("res://src/retail_slice/retail_side_command_frame.gd")
const AUTHORED_RESOLUTION := Vector2(1024.0, 768.0)
const BUTTON_SET_TRANSLATION := Vector2(1048.3, 361.3)
const BUTTON_DIAMETER := 60.0
const BUTTON_PITCH := 70.0
const SIDE_COMMAND_FADE_SECONDS := 0.33

var _buttons: Array[Button] = []
var _socket_texture: Texture2D
var _shown := false
var _tween: Tween
## The ornate per-faction container the sockets ride inside (user bug #6).
## Always child index 0 so it paints behind every socket.
var _frame = null


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
	if _frame == null:
		_frame = FrameScript.new()
		add_child(_frame)
	move_child(_frame, 0)
	_layout_frame()


func frame_node():
	return _frame


## Selects the per-faction frame art for `faction`, searching the user drop-in
## directory, every mounted content pack, then the in-repo base assets. Returns
## the resolved path, or "" when the procedural default frame is used.
func bind_faction(faction: String, pack_roots: Array = []) -> String:
	if _frame == null:
		_build()
	return _frame.bind_faction(faction, pack_roots)


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
	layout_for_viewport(_live_viewport_size())


func _live_viewport_size() -> Vector2:
	## Safe before the widget enters the tree (bind-time / headless tests):
	## get_viewport_rect() has no viewport to ask there.
	if is_inside_tree() and get_viewport() != null:
		var live := get_viewport_rect().size
		if live.x > 0.0 and live.y > 0.0:
			return live
	return AUTHORED_RESOLUTION


func _layout_frame(viewport: Vector2 = Vector2.ZERO) -> void:
	if _frame == null:
		return
	var size := viewport
	if size.x <= 0.0 or size.y <= 0.0:
		size = _live_viewport_size()
	var rect: Rect2 = FrameScript.frame_rect(size)
	_frame.position = rect.position
	_frame.size = rect.size
	_frame.queue_redraw()


## Lays the frame and the socket column out for an explicit viewport size. The
## sockets ride the frame's icon column (see retail_side_command_frame.gd for
## the measured contract), so the container and the icons can never drift apart.
func layout_for_viewport(viewport: Vector2) -> void:
	_layout_frame(viewport)
	var rect: Rect2 = FrameScript.frame_rect(viewport)
	var band: Vector2 = FrameScript.icon_band(viewport)
	var band_height := maxf(1.0, band.y - band.x)
	var count := maxi(1, _buttons.size())
	var icon: Vector2 = FrameScript.icon_size(viewport)
	var pitch: float = icon.y * FrameScript.ICON_PITCH_RATIO
	var rows := count
	if count > FrameScript.MAX_SINGLE_COLUMN:
		# Pathological roster: wrap leftward out of the frame rather than
		# shrinking the sockets into illegibility.
		rows = maxi(1, int(floorf((band_height - icon.y) / pitch)) + 1)
	else:
		# Retail keeps the whole build set in one column inside the frame:
		# shrink to fit rather than spilling sockets past the frame ends.
		var span := icon.y + float(count - 1) * pitch
		if span > band_height:
			var shrink := band_height / span
			icon *= shrink
			pitch *= shrink
	var center_x: float = FrameScript.icon_column_center_x(viewport)
	for index in _buttons.size():
		var row := index % rows
		var column := index / rows
		var button := _buttons[index]
		button.custom_minimum_size = icon
		button.size = icon
		button.add_theme_constant_override("icon_max_width", maxi(8, int(icon.x) - 12))
		button.position = Vector2(
			center_x - icon.x * 0.5 - float(column) * (icon.x + 6.0),
			band.x + float(row) * pitch
		)
	# Keep the frame painting behind every socket even after a rebuild.
	if _frame != null and _frame.get_index() != 0:
		move_child(_frame, 0)
	if rect.size.x <= 0.0:
		push_warning("[SideCommandBar] degenerate frame rect for viewport %s" % str(viewport))


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

## Initial controller support through the existing keyboard/mouse input paths.
extends Node
signal input_generated(event: InputEvent)
signal palette_toggled(open: bool)
signal command_action(action: String)
signal camera_rotation(radians: float)
signal camera_zoom(amount: float)
signal camera_pan(amount: Vector2)
signal placement_aim(direction: Vector2)
var formation: Callable
var _formation_held := false
var _formation_latched := false
var _face_buttons: Dictionary = {}
var _back_pending := false
var area_selection: Callable
var _area_pending := false
var _area_elapsed := 0.0
var _area_radius := 0.0
var _area_replay := false
var placement_active: Callable
var placement_grid_spacing := 50.0
var _left_trigger := false
var placement_resume_delay := 0.3
var _construction_held := false
var _pan_resume_remaining := 0.0
var _wait_stick_neutral := false
var primary_button: Callable
var attack_move: Callable
var _last_primary_at := -1000
var _last_primary_order := false
var selection_assist_radius := 48.0
var center_cursor := true
var camera_rotation_speed := 1.5
var camera_zoom_speed := 250.0
var camera_pan_speed := 1200.0
var _primary_mouse := MOUSE_BUTTON_LEFT
var _rotation_keys: Dictionary = {}
var palette_open := false
var keyboard_mode := false
var _keyboard_trigger := false
var _pad_trigger := false
var deadzone := 0.25
var cursor_speed := 900.0
var menu_repeat_delay := 300
var menu_repeat_rate := 150
var gameplay: Callable
var resolve_action: Callable
var device := -1
var _held_keys: Dictionary = {}
var _held_mouse: Dictionary = {}
var _menu_direction := Vector2i.ZERO
var _repeat_at := 0
var _was_gameplay := false
var _suspended := false


func _ready() -> void:
	Input.joy_connection_changed.connect(_connection)

func _connection(id: int, connected: bool) -> void:
	if not connected and id == device:
		_release_all()
		device = -1

func _gameplay() -> bool:
	return gameplay.is_valid() and gameplay.call()

func _key(code: Key, pressed: bool, shift := false) -> void:
	var event := InputEventKey.new()
	event.keycode = code
	event.pressed = pressed
	event.shift_pressed = shift
	input_generated.emit(event)

func _tap(code: Key, shift := false) -> void:
	_key(code, true, shift)
	_key(code, false, shift)

func _mouse(button: MouseButton, pressed: bool) -> void:
	if button == 0:
		return # native selection assistance already handled this A press
	if _gameplay() and center_cursor and not _suspended:
		_pin_cursor()
	var event := InputEventMouseButton.new()
	event.button_index = button
	event.pressed = pressed
	event.position = get_viewport().get_mouse_position()
	event.global_position = event.position
	input_generated.emit(event)

func _release_all(preserve_snap := false) -> void:
	if _formation_held and formation.is_valid(): formation.call("cancel", Vector2.ZERO)
	_formation_held = false
	_formation_latched = false
	_face_buttons.clear()
	_back_pending = false
	if _area_pending and area_selection.is_valid():
		area_selection.call(-1.0, false)
	_area_pending = false
	if not preserve_snap:
		_left_trigger = false
	_last_primary_order = false
	if _construction_held:
		_finish_construction_hold()
	_rotation_keys.clear()
	_keyboard_trigger = false
	_pad_trigger = false
	if palette_open:
		palette_open = false
		palette_toggled.emit(false)
	for code in _held_keys:
		_key(code, false)
	for button in _held_mouse:
		_mouse(button, false)
	_held_keys.clear()
	_held_mouse.clear()
	_menu_direction = Vector2i.ZERO

func _input(event: InputEvent) -> void:
	if event is InputEventKey:
		if event.keycode in [KEY_COMMA, KEY_PERIOD] and not event.ctrl_pressed and not event.alt_pressed:
			if not event.pressed:
				_rotation_keys.erase(event.keycode)
			if _gameplay() and not _suspended:
				var action := ""
				if resolve_action.is_valid():
					action = str(resolve_action.call("Comma" if event.keycode == KEY_COMMA else "Period", true))
				if action in ["rotateLeft", "rotateRight"]:
					get_viewport().set_input_as_handled()
					if event.pressed:
						_rotation_keys[event.keycode] = -1.0 if action == "rotateLeft" else 1.0
					return
		if event.keycode == KEY_BACKSLASH and not event.alt_pressed and not event.ctrl_pressed and not event.shift_pressed:
			get_viewport().set_input_as_handled()
			if event.pressed and not event.echo:
				_release_all()
				keyboard_mode = not keyboard_mode
				_update_keyboard_mode()
			return
		if keyboard_mode and _gameplay() and not _suspended and event.keycode == KEY_TAB:
			get_viewport().set_input_as_handled()
			if not event.echo:
				if event.pressed:
					_release_all()
				_keyboard_trigger = event.pressed
				_update_keyboard_mode()
			return
		if keyboard_mode and palette_open and _gameplay() and not _suspended:
			var actions := {KEY_LEFT:"previousCommand", KEY_RIGHT:"nextCommand", KEY_UP:"shortcutUp", KEY_DOWN:"shortcutDown",
				KEY_ENTER:"pressCommand", KEY_KP_ENTER:"pressCommand", KEY_BACKSPACE:"rightPressCommand", KEY_X:"saveFavorites", KEY_Y:"backPalette"}
			if actions.has(event.keycode) and not event.ctrl_pressed and not event.alt_pressed:
				get_viewport().set_input_as_handled()
				if event.pressed and not event.echo:
					command_action.emit(actions[event.keycode])
				return
	if not (event is InputEventJoypadButton or event is InputEventJoypadMotion):
		return
	if device < 0:
		device = event.device
	if _suspended or event.device != device:
		return
	get_viewport().set_input_as_handled()
	var playing := _gameplay()
	if playing != _was_gameplay:
		_release_all()
		_was_gameplay = playing
	if event is InputEventJoypadMotion:
		if event.axis == JOY_AXIS_TRIGGER_LEFT:
			_left_trigger = playing and event.axis_value > 0.5
		elif event.axis == JOY_AXIS_TRIGGER_RIGHT:
			var down: bool = _gameplay() and event.axis_value > 0.5 and not _suspended
			if down != _pad_trigger:
				if not _construction_held:
					_release_all(true)
				_pad_trigger = down
				_update_keyboard_mode()
		return
	if event.button_index in [JOY_BUTTON_A, JOY_BUTTON_B]:
		if event.pressed: _face_buttons[event.button_index] = true
		else: _face_buttons.erase(event.button_index)
		if _formation_latched:
			if not event.pressed and _formation_held:
				formation.call("commit", Vector2.ZERO)
				_formation_held = false
				_pan_resume_remaining = placement_resume_delay
				_wait_stick_neutral = true
			if _face_buttons.is_empty(): _formation_latched = false
			return
		if event.pressed and playing and not palette_open and not _is_placing() and formation.is_valid() and _face_buttons.has(JOY_BUTTON_A) and _face_buttons.has(JOY_BUTTON_B):
			if formation.call("begin", Vector2.ZERO):
				if area_selection.is_valid(): area_selection.call(-1.0, false)
				_area_pending = false
				_back_pending = false
				_last_primary_order = false
				_formation_held = true
				_formation_latched = true
				return
		if event.button_index == JOY_BUTTON_B and playing and not palette_open and not _is_placing() and formation.is_valid() and not _area_pending:
			if event.pressed: _back_pending = true
			elif _back_pending:
				_back_pending = false
				command_action.emit("deselect")
			return
	var names := {JOY_BUTTON_A:"A", JOY_BUTTON_B:"B", JOY_BUTTON_X:"X", JOY_BUTTON_Y:"Y",
		JOY_BUTTON_LEFT_STICK:"LeftStick", JOY_BUTTON_RIGHT_STICK:"RightStick",
		JOY_BUTTON_START:"Start", JOY_BUTTON_LEFT_SHOULDER:"LeftShoulder", JOY_BUTTON_RIGHT_SHOULDER:"RightShoulder",
		JOY_BUTTON_DPAD_UP:"DpadUp", JOY_BUTTON_DPAD_DOWN:"DpadDown", JOY_BUTTON_DPAD_LEFT:"DpadLeft", JOY_BUTTON_DPAD_RIGHT:"DpadRight"}
	var action := ""
	if resolve_action.is_valid():
		action = str(resolve_action.call(names.get(event.button_index, ""), playing))
	else:
		# Standalone test host; the game supplies its reloadable Lua dispatcher.
		var defaults := {"A":"contextPrimary" if playing else "accept", "B":"deselect" if playing else "back",
			"X":"stop" if playing else "skirmish", "Start":"options",
			"Y":"VIEW_LAST_RADAR_EVENT" if playing else "", "LeftStick":"VIEW_HOME_BASE" if playing else "",
			"RightStick":"CAMERA_RESET" if playing else "", "DpadRight":"right", "DpadLeft":"left", "DpadUp":"up", "DpadDown":"down",
			"LeftShoulder":"zoomOut" if playing else "previous", "RightShoulder":"zoomIn" if playing else "next"}
		action = defaults.get(names.get(event.button_index, ""), "")
	# A confirms a construction on release, including while RT's palette is up.
	if event.button_index == JOY_BUTTON_A and (_construction_held or (not event.pressed and _held_mouse.has(_primary_mouse)) or (action == "pressCommand" and _is_placing())):
		action = "contextPrimary"
	if _area_pending and event.button_index == JOY_BUTTON_B and event.pressed:
		_release_all()
		return
	if event.button_index == JOY_BUTTON_A and _area_pending:
		action = "contextPrimary"
	if action == "contextPrimary" and not _area_replay and area_selection.is_valid():
		if event.pressed and center_cursor:
			_pin_cursor()
		if event.pressed and not _is_placing() and not palette_open and area_selection.call(0.0, false):
			_area_pending = true
			_area_elapsed = 0
			_area_radius = 0
			return
		if not event.pressed and _area_pending:
			_area_pending = false
			if _area_elapsed >= 0.25:
				area_selection.call(_area_radius, true)
				_last_primary_order = false
				return
			area_selection.call(-1.0, false)
			_area_replay = true
			var press := event.duplicate()
			press.pressed = true
			_input(press)
			_input(event)
			_area_replay = false
			return
	if action == "contextPrimary":
		if event.pressed:
			_construction_held = _is_placing()
			if center_cursor:
				_pin_cursor()
			_primary_mouse = int(primary_button.call()) if primary_button.is_valid() else MOUSE_BUTTON_LEFT
			var now := Time.get_ticks_msec()
			var double_order := not _construction_held and _last_primary_order and now - _last_primary_at <= 300
			_last_primary_at = now
			_last_primary_order = not _construction_held and _primary_mouse == MOUSE_BUTTON_RIGHT
			if double_order and _last_primary_order and attack_move.is_valid() and attack_move.call():
				_primary_mouse = 0
				_last_primary_order = false

		_mouse(_primary_mouse, event.pressed)
		if event.pressed and _primary_mouse != 0:
			_held_mouse[_primary_mouse] = true
		else:
			_held_mouse.erase(_primary_mouse)
			if _construction_held:
				_finish_construction_hold()
	elif action in ["pointerLeft", "pointerRight"]:
		var button: MouseButton = MOUSE_BUTTON_LEFT if action == "pointerLeft" else MOUSE_BUTTON_RIGHT
		_mouse(button, event.pressed)
		if event.pressed:
			_held_mouse[button] = true
		else:
			_held_mouse.erase(button)
	elif event.pressed:
		if palette_open and action != "options":
			command_action.emit(action)
			return
		match action:
			"deselect":
				if center_cursor:
					_pin_cursor()
				command_action.emit(action)
				if _construction_held:
					_finish_construction_hold()
			"accept": _tap(KEY_ENTER)
			"back", "options": _tap(KEY_ESCAPE)
			"left": _tap(KEY_LEFT)
			"right": _tap(KEY_RIGHT)
			"up": _tap(KEY_UP)
			"down": _tap(KEY_DOWN)
			"previous": _tap(KEY_TAB, true)
			"next": _tap(KEY_TAB)
			"stop": _tap(KEY_S)
			"VIEW_HOME_BASE": _tap(KEY_H)
			"VIEW_LAST_RADAR_EVENT": _tap(KEY_SPACE)
			"CAMERA_RESET": _tap(KEY_KP_5)
			"zoomIn", "zoomOut":
				var wheel: MouseButton = MOUSE_BUTTON_WHEEL_UP if action == "zoomIn" else MOUSE_BUTTON_WHEEL_DOWN
				_mouse(wheel, true)
				_mouse(wheel, false)
			_: command_action.emit(action)

func _axis(axis: JoyAxis) -> float:
	var value := Input.get_joy_axis(device, axis)
	return signf(value) * (absf(value) - deadzone) / (1.0 - deadzone) if absf(value) > deadzone else 0.0

func _analog_stick(value: Vector2) -> Vector2:
	var strength := value.length()
	if strength <= deadzone:
		return Vector2.ZERO
	return value / strength * ((minf(strength, 1.0) - deadzone) / (1.0 - deadzone))

func _pan_stick(left: Vector2, delta: float) -> void:
	if _formation_held:
		if not formation.call("aim", left): _release_all()
		return
	if _area_pending: return
	if _construction_held:
		if _gameplay() and not _suspended and _is_placing() and left != Vector2.ZERO:
			placement_aim.emit(left.normalized())
		return
	if _pan_resume_remaining > 0:
		_pan_resume_remaining = maxf(0.0, _pan_resume_remaining - delta)
		return
	if _wait_stick_neutral:
		if left == Vector2.ZERO:
			_wait_stick_neutral = false
		return
	if _gameplay() and not _suspended and not palette_open and left != Vector2.ZERO:
		camera_pan.emit(left * camera_pan_speed * delta)

func _is_placing() -> bool:
	return placement_active.is_valid() and bool(placement_active.call())

func _finish_construction_hold() -> void:
	_construction_held = false
	_pan_resume_remaining = placement_resume_delay
	_wait_stick_neutral = true

func _pin_cursor() -> void:
	var position := get_viewport().get_visible_rect().get_center()
	var old := get_viewport().get_mouse_position()
	if old != position:
		Input.warp_mouse(position)
	var motion := InputEventMouseMotion.new()
	motion.position = position
	motion.global_position = position
	motion.relative = position - old
	input_generated.emit(motion)

func _camera_stick(right: Vector2, delta: float) -> void:
	if _formation_held or _area_pending or _construction_held or _pan_resume_remaining > 0 or _wait_stick_neutral:
		return
	if right.x != 0:
		camera_rotation.emit(right.x * camera_rotation_speed * delta)
	if right.y != 0:
		camera_zoom.emit(right.y * camera_zoom_speed * delta)

func _process(delta: float) -> void:
	if _formation_held and (not _gameplay() or _suspended): _release_all()
	if _area_pending:
		if not _gameplay() or _suspended:
			_release_all()
		else:
			_area_elapsed += delta
			if _area_elapsed >= 0.25:
				_area_radius = minf(600, 40 + (_area_elapsed - 0.25) * 200)
				area_selection.call(_area_radius, false)
	_update_keyboard_mode()
	if _gameplay() and not _suspended and center_cursor and (keyboard_mode or device >= 0):
		_pin_cursor()
	if not _gameplay() or _suspended:
		_rotation_keys.clear()
	if not _rotation_keys.is_empty():
		var direction := 0.0
		for value in _rotation_keys.values():
			direction += float(value)
		camera_rotation.emit(direction * 1.5 * delta)
	if device < 0 or _suspended:
		return
	var playing := _gameplay()
	if playing != _was_gameplay:
		_release_all()
		_was_gameplay = playing
	var left := _analog_stick(Vector2(Input.get_joy_axis(device, JOY_AXIS_LEFT_X), Input.get_joy_axis(device, JOY_AXIS_LEFT_Y)))
	_pan_stick(left, delta)
	var right := Vector2(_axis(JOY_AXIS_RIGHT_X), _axis(JOY_AXIS_RIGHT_Y))
	if playing:
		_camera_stick(right, delta)
	elif right.length_squared() > 0:
		var old := get_viewport().get_mouse_position()
		var position := (old + right * cursor_speed * delta).clamp(Vector2.ZERO, get_viewport().get_visible_rect().size - Vector2.ONE)
		Input.warp_mouse(position)
		var motion := InputEventMouseMotion.new()
		motion.position = position
		motion.global_position = position
		motion.relative = position - old
		motion.button_mask = MOUSE_BUTTON_MASK_LEFT if _held_mouse.has(MOUSE_BUTTON_LEFT) else 0
		input_generated.emit(motion)
	if not playing:
		var direction := Vector2i.ZERO
		if left.length() > 0.1:
			direction = Vector2i(int(signf(left.x)), 0) if absf(left.x) > absf(left.y) else Vector2i(0, int(signf(left.y)))
		var now := Time.get_ticks_msec()
		if direction != Vector2i.ZERO and (direction != _menu_direction or now >= _repeat_at):
			_tap(KEY_LEFT if direction.x < 0 else KEY_RIGHT if direction.x > 0 else KEY_UP if direction.y < 0 else KEY_DOWN)
			_repeat_at = now + (menu_repeat_delay if direction != _menu_direction else menu_repeat_rate)
		_menu_direction = direction

func _notification(what: int) -> void:
	if what == NOTIFICATION_APPLICATION_FOCUS_OUT:
		_suspended = true
		_release_all()
	elif what == NOTIFICATION_APPLICATION_FOCUS_IN:
		_suspended = false

func _exit_tree() -> void:
	Engine.set_meta("controller_camera_keys", false)
	_release_all()
	Engine.set_meta("controller_keyboard_active", false)
	Engine.set_meta("controller_keyboard_enabled", false)

func _update_keyboard_mode() -> void:
	var enabled := keyboard_mode and _gameplay() and not _suspended
	if not _gameplay() or _suspended:
		_keyboard_trigger = false
		_pad_trigger = false
	var active := (enabled and _keyboard_trigger or _pad_trigger) and _gameplay() and not _suspended
	Engine.set_meta("controller_camera_keys", _gameplay() and not _suspended)
	Engine.set_meta("controller_keyboard_enabled", enabled)
	Engine.set_meta("controller_keyboard_active", enabled and _keyboard_trigger)
	if active != palette_open:
		palette_open = active
		palette_toggled.emit(active)

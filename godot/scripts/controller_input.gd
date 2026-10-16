## Initial controller support through the existing keyboard/mouse input paths.
extends Node
signal input_generated(event: InputEvent)
signal palette_toggled(open: bool)
signal command_action(action: String)
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
	var event := InputEventMouseButton.new()
	event.button_index = button
	event.pressed = pressed
	event.position = get_viewport().get_mouse_position()
	event.global_position = event.position
	input_generated.emit(event)

func _release_all() -> void:
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
		if event.axis == JOY_AXIS_TRIGGER_RIGHT:
			var down: bool = _gameplay() and event.axis_value > 0.5 and not _suspended
			if down != _pad_trigger:
				_release_all()
				_pad_trigger = down
				_update_keyboard_mode()
		return
	var names := {JOY_BUTTON_A:"A", JOY_BUTTON_B:"B", JOY_BUTTON_X:"X", JOY_BUTTON_Y:"Y",
		JOY_BUTTON_START:"Start", JOY_BUTTON_LEFT_SHOULDER:"LeftShoulder", JOY_BUTTON_RIGHT_SHOULDER:"RightShoulder",
		JOY_BUTTON_DPAD_UP:"DpadUp", JOY_BUTTON_DPAD_DOWN:"DpadDown", JOY_BUTTON_DPAD_LEFT:"DpadLeft", JOY_BUTTON_DPAD_RIGHT:"DpadRight"}
	var action := ""
	if resolve_action.is_valid():
		action = str(resolve_action.call(names.get(event.button_index, ""), playing))
	else:
		# Standalone test host; the game supplies its reloadable Lua dispatcher.
		var defaults := {"A":"pointerLeft" if playing else "accept", "B":"pointerRight" if playing else "back",
			"X":"stop" if playing else "", "Start":"options", "DpadRight":"right", "DpadLeft":"left", "DpadUp":"up", "DpadDown":"down",
			"LeftShoulder":"zoomOut" if playing else "previous", "RightShoulder":"zoomIn" if playing else "next"}
		action = defaults.get(names.get(event.button_index, ""), "")
	if action in ["pointerLeft", "pointerRight"]:
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
			"accept": _tap(KEY_ENTER)
			"back", "options": _tap(KEY_ESCAPE)
			"left": _tap(KEY_LEFT)
			"right": _tap(KEY_RIGHT)
			"up": _tap(KEY_UP)
			"down": _tap(KEY_DOWN)
			"previous": _tap(KEY_TAB, true)
			"next": _tap(KEY_TAB)
			"stop": _tap(KEY_S)
			"zoomIn", "zoomOut":
				var wheel: MouseButton = MOUSE_BUTTON_WHEEL_UP if action == "zoomIn" else MOUSE_BUTTON_WHEEL_DOWN
				_mouse(wheel, true)
				_mouse(wheel, false)
			_: command_action.emit(action)

func _axis(axis: JoyAxis) -> float:
	var value := Input.get_joy_axis(device, axis)
	return signf(value) * (absf(value) - deadzone) / (1.0 - deadzone) if absf(value) > deadzone else 0.0

func _process(delta: float) -> void:
	_update_keyboard_mode()
	if device < 0 or _suspended:
		return
	var playing := _gameplay()
	if playing != _was_gameplay:
		_release_all()
		_was_gameplay = playing
	var left := Vector2(_axis(JOY_AXIS_LEFT_X), _axis(JOY_AXIS_LEFT_Y))
	if playing and not palette_open:
		var desired := {KEY_LEFT: left.x < -0.1, KEY_RIGHT: left.x > 0.1, KEY_UP: left.y < -0.1, KEY_DOWN: left.y > 0.1}
		for code in desired:
			if bool(desired[code]) != _held_keys.has(code):
				_key(code, desired[code])
				if desired[code]:
					_held_keys[code] = true
				else:
					_held_keys.erase(code)
	var right := Vector2(_axis(JOY_AXIS_RIGHT_X), _axis(JOY_AXIS_RIGHT_Y))
	if right.length_squared() > 0:
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
	_release_all()
	Engine.set_meta("controller_keyboard_active", false)
	Engine.set_meta("controller_keyboard_enabled", false)

func _update_keyboard_mode() -> void:
	var enabled := keyboard_mode and _gameplay() and not _suspended
	if not _gameplay() or _suspended:
		_keyboard_trigger = false
		_pad_trigger = false
	var active := (enabled and _keyboard_trigger or _pad_trigger) and _gameplay() and not _suspended
	Engine.set_meta("controller_keyboard_enabled", enabled)
	Engine.set_meta("controller_keyboard_active", enabled and _keyboard_trigger)
	if active != palette_open:
		palette_open = active
		palette_toggled.emit(active)

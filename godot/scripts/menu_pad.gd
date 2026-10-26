## Lane OPTS-6: the game controller in the menus (NOT RETAIL: an OpenBFME improvement; RotWK's menus take the mouse only). Godot's joypad events become
## the keys the menus' keyboard navigation reads (GameClient/GUI/MenuNavigator.h), sent through Godot's input as key events, so the controller and the
## keyboard take one path:
##   D-pad, right stick         the arrows (a held direction repeats: first after 0.4 s, then every 0.12 s)
##   A / B                      Enter / Esc
##   LB / RB                    PageUp / PageDown (the tabs)
##   triggers                   skirmish focus sections; Left / Right elsewhere
##   left stick                  mouse movement; A clicks until D-pad/right-stick navigation resumes
##   Start                      Esc (also over a game without a menu: the in-game menu opens, as Esc opens it)
## Only while a menu has the input (AptMenuPlayer.menu_has_input), Start always. take_log() lists the keys sent ("down KEY_UP" ...), for the tests.
extends Node
signal input_generated(event: InputEvent)

const DEADZONE := 0.5
const REPEAT_FIRST := 0.4
const REPEAT_NEXT := 0.12
const BUTTONS := {
	JOY_BUTTON_DPAD_UP: KEY_UP, JOY_BUTTON_DPAD_DOWN: KEY_DOWN, JOY_BUTTON_DPAD_LEFT: KEY_LEFT, JOY_BUTTON_DPAD_RIGHT: KEY_RIGHT,
	JOY_BUTTON_A: KEY_ENTER, JOY_BUTTON_B: KEY_ESCAPE, JOY_BUTTON_LEFT_SHOULDER: KEY_PAGEUP, JOY_BUTTON_RIGHT_SHOULDER: KEY_PAGEDOWN,
	JOY_BUTTON_START: KEY_ESCAPE,
}
## axis -> [key below -DEADZONE, key above +DEADZONE] (a trigger only goes positive)
const AXES := {
	JOY_AXIS_RIGHT_X: [KEY_LEFT, KEY_RIGHT], JOY_AXIS_RIGHT_Y: [KEY_UP, KEY_DOWN],
	JOY_AXIS_TRIGGER_LEFT: [KEY_NONE, KEY_LEFT], JOY_AXIS_TRIGGER_RIGHT: [KEY_NONE, KEY_RIGHT],
}
const REPEATING := [KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT]

var mouse_mode := false
var cursor_speed := 900.0
var _pointer_stick := Vector2.ZERO
var _wait_pointer_neutral := false
var _disconnected_devices := {}
var _pointer_a := false
var _mouse_down := false
var _device := -1
var _suspended := false
var start_lobby: Callable # returns true when Start was handled by the skirmish lobby
var _start_consumed := false
var _shell: Node
var _section_trigger := {}
var _section_captured := {}
var _axis_key := {}   # axis -> the key it holds now (KEY_NONE: none)
var _captured_buttons := {} # Only consume releases whose press belonged to this menu.
var _pressed_keys := {}
var _held := {}       # key -> the time (s) of its next repeat
var _log := PackedStringArray()


func _init(shell: Node) -> void:
	_shell = shell
	name = "MenuPad"
	process_mode = Node.PROCESS_MODE_ALWAYS
	input_generated.connect(Input.parse_input_event)


func _ready() -> void:
	Input.joy_connection_changed.connect(_connection)


func _connection(id: int, connected: bool) -> void:
	if connected:
		_disconnected_devices.erase(id)
	else:
		_disconnected_devices[id] = true
	if not connected and id == _device:
		_captured_buttons.clear()
		_reset()
		_device = -1


func _mouse(pressed: bool) -> void:
	var event := InputEventMouseButton.new()
	event.button_index = MOUSE_BUTTON_LEFT
	event.pressed = pressed
	event.position = get_viewport().get_mouse_position()
	event.global_position = event.position
	_mouse_down = pressed
	input_generated.emit(event)


func _navigation_mode() -> void:
	if mouse_mode and _pointer_stick.length() > DEADZONE:
		_wait_pointer_neutral = true
	mouse_mode = false
	if _mouse_down:
		_mouse(false)


func _reset() -> void:
	_navigation_mode()
	_pointer_a = false
	_pointer_stick = Vector2.ZERO
	_wait_pointer_neutral = false
	_start_consumed = false
	for key in _pressed_keys.keys():
		_send(key, false, false)
	_axis_key.clear()
	_section_trigger.clear()
	_section_captured.clear()


func _move_pointer(stick: Vector2, delta: float) -> void:
	if _wait_pointer_neutral or stick.length() <= DEADZONE:
		return
	var velocity := stick.normalized() * ((minf(stick.length(), 1.0) - DEADZONE) / (1.0 - DEADZONE))
	var old := get_viewport().get_mouse_position()
	var position := (old + velocity * cursor_speed * delta).clamp(Vector2.ZERO, get_viewport().get_visible_rect().size - Vector2.ONE)
	if position == old:
		return
	mouse_mode = true
	Input.warp_mouse(position)
	var motion := InputEventMouseMotion.new()
	motion.position = position
	motion.global_position = position
	motion.relative = position - old
	motion.button_mask = MOUSE_BUTTON_MASK_LEFT if _mouse_down else 0
	input_generated.emit(motion)


func _notification(what: int) -> void:
	if what == NOTIFICATION_APPLICATION_FOCUS_OUT:
		_suspended = true
		_reset()
	elif what == NOTIFICATION_APPLICATION_FOCUS_IN:
		_suspended = false


func _exit_tree() -> void:
	_reset()


func take_log() -> PackedStringArray:
	var out := _log
	_log = PackedStringArray()
	return out


func _active() -> bool:
	return not _suspended and _shell != null and _shell.menu_has_input()


func _input(event: InputEvent) -> void:
	if not (event is InputEventJoypadButton or event is InputEventJoypadMotion):
		return
	if _suspended or _disconnected_devices.has(event.device):
		return
	# Ignore drift and releases from unclaimed pads. A button press can hand
	# menus to another player after releasing the previous pad's held input.
	var deliberate: bool = (event is InputEventJoypadButton and event.pressed) or (event is InputEventJoypadMotion and absf(event.axis_value) > DEADZONE)
	if event.device != _device:
		if not deliberate or (not _active() and not (event is InputEventJoypadButton and event.button_index == JOY_BUTTON_START and event.pressed)):
			return
		if _device >= 0 and not (event is InputEventJoypadButton and event.pressed):
			return
		_reset()
		_captured_buttons.clear()
		_device = event.device
	if not _active():
		_reset()
		if event is InputEventJoypadButton:
			if not event.pressed and _captured_buttons.has(event.button_index):
				_captured_buttons.erase(event.button_index)
				get_viewport().set_input_as_handled()
				return
			if event.button_index != JOY_BUTTON_START: return
		else:
			return
	if event is InputEventJoypadButton:
		var b := event as InputEventJoypadButton
		if b.pressed and b.button_index in [JOY_BUTTON_DPAD_UP, JOY_BUTTON_DPAD_DOWN, JOY_BUTTON_DPAD_LEFT, JOY_BUTTON_DPAD_RIGHT]:
			_navigation_mode()
		if b.button_index == JOY_BUTTON_A:
			if b.pressed and _active():
				_pointer_a = mouse_mode
				if _pointer_a:
					_captured_buttons[b.button_index] = true
					_mouse(true)
			elif not b.pressed and _pointer_a:
				if _mouse_down:
					_mouse(false)
				_pointer_a = false
				_captured_buttons.erase(b.button_index)
				get_viewport().set_input_as_handled()
				return
			if _pointer_a:
				get_viewport().set_input_as_handled()
				return
		if b.button_index == JOY_BUTTON_START:
			if b.pressed and start_lobby.is_valid() and start_lobby.call():
				_captured_buttons[b.button_index] = true
				_start_consumed = true
				get_viewport().set_input_as_handled()
				return
			if not b.pressed and _start_consumed:
				_start_consumed = false
				_captured_buttons.erase(b.button_index)
				get_viewport().set_input_as_handled()
				return
		if not BUTTONS.has(b.button_index):
			return
		if b.pressed or _captured_buttons.has(b.button_index):
			if b.pressed: _captured_buttons[b.button_index] = true
			else: _captured_buttons.erase(b.button_index)
			_send(BUTTONS[b.button_index], b.pressed, false)
			get_viewport().set_input_as_handled()
	elif event is InputEventJoypadMotion:
		var m := event as InputEventJoypadMotion
		if m.axis in [JOY_AXIS_LEFT_X, JOY_AXIS_LEFT_Y]:
			if _active():
				if m.axis == JOY_AXIS_LEFT_X:
					_pointer_stick.x = m.axis_value
				else:
					_pointer_stick.y = m.axis_value
				if _pointer_stick.length() <= DEADZONE:
					_wait_pointer_neutral = false
				get_viewport().set_input_as_handled()
			return
		if m.axis in [JOY_AXIS_RIGHT_X, JOY_AXIS_RIGHT_Y] and absf(m.axis_value) >= DEADZONE:
			_navigation_mode()
		if m.axis in [JOY_AXIS_TRIGGER_LEFT, JOY_AXIS_TRIGGER_RIGHT]:
			var pressed := m.axis_value >= DEADZONE
			var had: bool = _section_trigger.get(m.axis, false)
			_section_trigger[m.axis] = pressed
			if pressed and not had and _active() and _shell.has_method("menu_controller_section"):
				_section_captured[m.axis] = _shell.menu_controller_section(1 if m.axis == JOY_AXIS_TRIGGER_LEFT else -1)
			if _section_captured.get(m.axis, false):
				if not pressed:
					_section_captured.erase(m.axis)
				get_viewport().set_input_as_handled()
				return
		if not AXES.has(m.axis):
			return
		var keys: Array = AXES[m.axis]
		var want: int = KEY_NONE
		if m.axis_value <= -DEADZONE:
			want = keys[0]
		elif m.axis_value >= DEADZONE:
			want = keys[1]
		var had: int = _axis_key.get(m.axis, KEY_NONE)
		if want == had:
			return
		if had != KEY_NONE:
			_send(had, false, false)
		_axis_key[m.axis] = want
		if want != KEY_NONE and _active():
			_send(want, true, false)
		get_viewport().set_input_as_handled()


func _process(delta: float) -> void:
	if not _active():
		_reset()
		return
	# Navigation wins while a D-pad direction or right-stick direction is held.
	if not _held.is_empty():
		_navigation_mode()
	else:
		_move_pointer(_pointer_stick, delta)
	var now := Time.get_ticks_msec() / 1000.0
	for key in _held.keys():
		if now >= float(_held[key]):
			if not _active():
				_held.erase(key)
				continue
			_send(key, true, true)


func _send(key: int, pressed: bool, echo: bool) -> void:
	var ev := InputEventKey.new()
	ev.keycode = key as Key
	ev.physical_keycode = key as Key
	ev.pressed = pressed
	ev.echo = echo
	ev.set_meta("menu_controller", true)
	_log.append("%s %s%s" % ["down" if pressed else "up", OS.get_keycode_string(key), " (repeat)" if echo else ""])
	if pressed:
		_pressed_keys[key] = true
	else:
		_pressed_keys.erase(key)
	if pressed and REPEATING.has(key):
		_held[key] = Time.get_ticks_msec() / 1000.0 + (REPEAT_NEXT if echo else REPEAT_FIRST)
	elif not pressed:
		_held.erase(key)
	input_generated.emit(ev)

extends SceneTree
## GDScript assert() only logs a SCRIPT ERROR and the run carries on, so a failed one still ended in PASS / exit 0. Every check goes through
## _check: a failure is recorded and printed as FAIL, and _finish quits 1 when any failed.
var _failures: Array[String] = []

func _check(ok: bool, reason := "") -> bool:
	if not ok:
		_failures.append(reason)
		var stack := get_stack()
		printerr("FAIL: ", reason, " (line ", stack[1].line if stack.size() > 1 else 0, ")")
	return ok

func _finish(pass_message: String) -> void:
	if _failures.is_empty():
		print(pass_message)
		quit()
		return
	printerr(_failures.size(), " check(s) failed")
	quit(1)

class FakeShell extends Node2D:
	var stack := PackedStringArray(["MainMenu.apt"])
	var calls: Array = []
	var sections: Array = []
	var active := true
	var start_available := true
	func screen_state() -> Dictionary: return {"start_available": start_available}
	func menu_controller_section(delta: int) -> bool:
		sections.append(delta)
		return true
	func shell_stack() -> PackedStringArray: return stack
	func menu_has_input() -> bool: return active
	func shell_fscommand(command: String, argument: String) -> bool:
		calls.append([command, argument])
		return true

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var game = load("res://scripts/game.gd").new()
	var shell := FakeShell.new()
	game._shell = shell
	game._state = game.State.MENU
	_check(not game._controller_start_lobby())
	_check(shell.calls.is_empty())
	shell.stack = PackedStringArray(["MainMenu.apt", "Skirmish.apt"])
	_check(game._controller_start_lobby())
	_check(shell.calls.back() == ["AptSkirmish::StartGame", ""])
	# The real Start-button routing starts this lobby instead of generating Esc.
	var pad = load("res://scripts/menu_pad.gd").new(shell)
	root.add_child(pad)
	pad.set_process(false)
	pad.start_lobby = game._controller_start_lobby
	var unused := InputEventJoypadMotion.new()
	unused.device = 12
	unused.axis = JOY_AXIS_LEFT_X
	unused.axis_value = 0.2
	pad._input(unused)
	_check(pad._device == -1, "Drift cannot claim an unused controller")

	var start := InputEventJoypadButton.new()
	start.button_index = JOY_BUTTON_START
	start.pressed = true
	pad._input(start)
	start.pressed = false
	pad._input(start)
	_check(shell.calls.size() == 2)
	_check(shell.calls.back() == ["AptSkirmish::StartGame", ""])
	_check(pad.take_log().is_empty(), "Start must not also generate Escape")
	# Every profile popup belongs to Skirmish.apt but must block Start.
	shell.start_available = false
	_check(not game._controller_start_lobby())
	_check(shell.calls.size() == 2)
	shell.start_available = true
	shell.stack.append("Options.apt")
	_check(not game._controller_start_lobby())
	_check(shell.calls.size() == 2)
	shell.stack = PackedStringArray(["MainMenu.apt"])
	game._state = game.State.PLAYING
	_check(not game._controller_start_lobby())
	_check(shell.calls.size() == 2)
	var trigger := InputEventJoypadMotion.new()
	trigger.axis = JOY_AXIS_TRIGGER_LEFT
	for value in [0.8, 0.9, 0.0, 0.8, 0.0]:
		trigger.axis_value = value
		pad._input(trigger)
	trigger.axis = JOY_AXIS_TRIGGER_RIGHT
	trigger.axis_value = 0.8
	pad._input(trigger)
	trigger.axis_value = 0.0
	pad._input(trigger)
	_check(shell.sections == [1, 1, -1], "Triggers switch once per pull")
	_check(pad.take_log().is_empty(), "Section triggers must not also send arrow keys")

	# An unused pad's drift/release cannot claim or steal the menu. A deliberate
	# button press hands over safely and releases the previous held keys.
	var drift := InputEventJoypadMotion.new()
	drift.device = 12
	drift.axis = JOY_AXIS_LEFT_X
	drift.axis_value = 0.2
	pad._input(drift)
	_check(pad._device == 0)
	var handoff := InputEventJoypadButton.new()
	handoff.device = 12
	handoff.button_index = JOY_BUTTON_DPAD_DOWN
	handoff.pressed = true
	pad._input(handoff)
	_check(pad._device == 12)
	handoff.pressed = false
	pad._input(handoff)
	_check(pad.take_log() == PackedStringArray(["down Down", "up Down"]))
	handoff.device = 0
	handoff.pressed = true
	pad._input(handoff)
	handoff.pressed = false
	pad._input(handoff)
	pad.take_log()
	var events: Array = []
	pad.input_generated.connect(func(event: InputEvent) -> void: events.append(event))
	var stick := InputEventJoypadMotion.new()
	stick.axis = JOY_AXIS_LEFT_X
	stick.axis_value = 0.2
	pad._input(stick)
	pad._process(0.01)
	_check(not pad.mouse_mode and events.is_empty(), "Left-stick drift must not enable mouse mode")
	stick.axis_value = 1.0
	pad._input(stick)
	pad._process(0.01)
	_check(pad.mouse_mode and events.size() == 1 and events[0] is InputEventMouseMotion)
	_check(pad.take_log().is_empty(), "Left stick moves the mouse without arrow keys")
	var a := InputEventJoypadButton.new()
	a.button_index = JOY_BUTTON_A
	a.pressed = true
	pad._input(a)
	a.pressed = false
	pad._input(a)
	_check(events.size() == 3)
	_check(events[1] is InputEventMouseButton and events[1].pressed)
	_check(events[2] is InputEventMouseButton and not events[2].pressed)
	_check(pad.take_log().is_empty(), "Mouse-mode A must not also activate the menu")
	# A drag ends safely when navigation takes over, and A's release stays consumed.
	a.pressed = true
	pad._input(a)
	var direction := InputEventJoypadButton.new()
	direction.button_index = JOY_BUTTON_DPAD_DOWN
	direction.pressed = true
	pad._input(direction)
	_check(not pad.mouse_mode and not pad._mouse_down)
	a.pressed = false
	pad._input(a)
	direction.pressed = false
	pad._input(direction)
	_check(pad.take_log() == PackedStringArray(["down Down", "up Down"]))
	events.clear()
	pad._process(0.01)
	_check(not pad.mouse_mode and events.is_empty(), "Held left stick must be centered before returning to mouse mode")
	a.pressed = true
	pad._input(a)
	a.pressed = false
	pad._input(a)
	_check(events.size() == 2 and events[0] is InputEventKey and events[1] is InputEventKey)
	_check(pad.take_log() == PackedStringArray(["down Enter", "up Enter"]))
	# Center/reuse left stick, then switch back using the right stick's vertical axis.
	stick.axis_value = 0.0
	pad._input(stick)
	stick.axis_value = 1.0
	pad._input(stick)
	pad._process(0.01)
	_check(pad.mouse_mode)
	stick.axis = JOY_AXIS_RIGHT_Y
	stick.axis_value = -1.0
	pad._input(stick)
	_check(not pad.mouse_mode)
	stick.axis_value = 0.0
	pad._input(stick)
	_check(pad.take_log() == PackedStringArray(["down Up", "up Up"]))
	stick.axis = JOY_AXIS_RIGHT_X
	stick.axis_value = 1.0
	pad._input(stick)
	pad._held[KEY_RIGHT] = 0.0
	pad._process(0.01)
	stick.axis_value = 0.0
	pad._input(stick)
	_check(pad.take_log() == PackedStringArray(["down Right", "down Right (repeat)", "up Right"]))
	# Focus loss releases nonrepeating accept/back/tab keys as well as directions.
	for index in [JOY_BUTTON_A, JOY_BUTTON_B, JOY_BUTTON_LEFT_SHOULDER]:
		var held_button := InputEventJoypadButton.new()
		held_button.button_index = index
		held_button.pressed = true
		pad._input(held_button)
	pad._notification(Node.NOTIFICATION_APPLICATION_FOCUS_OUT)
	_check(pad._pressed_keys.is_empty() and pad._held.is_empty())
	_check(pad.take_log() == PackedStringArray(["down Enter", "down Escape", "down PageUp", "up Enter", "up Escape", "up PageUp"]))
	pad._notification(Node.NOTIFICATION_APPLICATION_FOCUS_IN)
	# Closing a menu and disconnecting release a mouse press and clear the mode.
	pad._pointer_stick = Vector2.ZERO
	pad._wait_pointer_neutral = false
	pad._move_pointer(Vector2.RIGHT, 0.01)
	a.pressed = true
	pad._input(a)
	shell.active = false
	pad._process(0.01)
	_check(not pad.mouse_mode and not pad._mouse_down)
	shell.active = true
	pad._move_pointer(Vector2.RIGHT, 0.01)
	pad._input(a)
	pad._connection(0, false)
	_check(not pad.mouse_mode and not pad._mouse_down and pad._device == -1)
	pad._input(a)
	_check(pad._device == -1, "Queued events must not revive a disconnected pad")
	pad.queue_free()
	game._shell = null
	game.free()
	shell.free()
	_finish("CONTROLLER MENU PASS")

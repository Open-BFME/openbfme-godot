extends SceneTree
func _initialize() -> void:
	call_deferred("_run")
func _run() -> void:
	var controller = preload("res://scripts/controller_input.gd").new()
	var playing := {"value": false}
	controller.gameplay = func() -> bool: return playing.value
	var generated: Array = []
	controller.input_generated.connect(func(event: InputEvent) -> void: generated.append(event))
	root.add_child(controller)
	controller.set_process(false)
	var button := InputEventJoypadButton.new()
	button.device = 0
	button.button_index = JOY_BUTTON_DPAD_RIGHT
	button.pressed = true
	controller._input(button)
	assert(generated.size() == 2)
	assert(generated[0].keycode == KEY_RIGHT and generated[0].pressed and not generated[1].pressed)
	generated.clear()
	button.button_index = JOY_BUTTON_LEFT_SHOULDER
	controller._input(button)
	assert(generated[0].keycode == KEY_TAB and generated[0].shift_pressed)
	generated.clear()
	playing.value = true
	button.button_index = JOY_BUTTON_A
	controller._input(button)
	assert(generated[0] is InputEventMouseButton and generated[0].button_index == MOUSE_BUTTON_LEFT and generated[0].pressed)
	var trigger := InputEventJoypadMotion.new()
	trigger.device = 0
	trigger.axis = JOY_AXIS_TRIGGER_RIGHT
	trigger.axis_value = 1.0
	controller._input(trigger)
	assert(controller.palette_open)
	assert(not generated.back().pressed) # opening RT releases the held drag
	controller.resolve_action = func(_button: String, _playing: bool) -> String: return "pressCommand"
	var commands: Array = []
	controller.command_action.connect(func(action: String) -> void: commands.append(action))
	controller._input(button)
	assert(commands == ["pressCommand"])
	controller._connection(0, false)
	assert(generated.size() == 2 and not generated[1].pressed)
	assert(controller.device == -1 and controller._held_mouse.is_empty() and not controller.palette_open)
	var key := InputEventKey.new()
	key.keycode = KEY_BACKSLASH
	key.pressed = true
	controller._input(key)
	assert(controller.keyboard_mode and not controller.palette_open and controller.device == -1)
	key.keycode = KEY_TAB
	controller._input(key)
	assert(controller.palette_open)
	key.keycode = KEY_RIGHT
	controller._input(key)
	assert(commands.back() == "nextCommand")
	playing.value = false
	controller._process(0.01)
	assert(not controller.palette_open and not Engine.get_meta("controller_keyboard_active"))
	playing.value = true
	controller._process(0.01)
	assert(not controller.palette_open)
	key.keycode = KEY_TAB
	controller._input(key)
	assert(controller.palette_open)
	key.pressed = false
	controller._input(key)
	assert(not controller.palette_open)
	key.pressed = true
	key.keycode = KEY_BACKSLASH
	controller._input(key)
	assert(not controller.keyboard_mode and not controller.palette_open)
	controller.queue_free()
	await process_frame
	print("CONTROLLER INPUT PASS")
	quit()

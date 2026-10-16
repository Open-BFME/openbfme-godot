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
	for binding in [[JOY_BUTTON_Y, KEY_SPACE], [JOY_BUTTON_LEFT_STICK, KEY_H], [JOY_BUTTON_RIGHT_STICK, KEY_KP_5]]:
		generated.clear()
		button.button_index = binding[0]
		controller._input(button)
		assert(generated.size() == 2 and generated[0].keycode == binding[1] and generated[0].pressed and not generated[1].pressed)
		generated.clear()
		button.pressed = false
		controller._input(button)
		assert(generated.is_empty())
		button.pressed = true
	button.button_index = JOY_BUTTON_A
	controller._input(button)
	assert(generated.back() is InputEventMouseButton and generated.back().button_index == MOUSE_BUTTON_LEFT and generated.back().pressed)
	assert(generated[0] is InputEventMouseMotion and generated[0].position == root.get_visible_rect().get_center())
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
	var mouse_events = generated.filter(func(event): return event is InputEventMouseButton)
	assert(mouse_events.size() == 2 and not mouse_events[1].pressed)
	assert(controller.device == -1 and controller._held_mouse.is_empty() and not controller.palette_open)
	controller._input(trigger)
	controller._input(button)
	assert(controller.device == -1 and not controller.palette_open, "queued pad events cannot reactivate a disconnected controller")
	controller._connection(0, true)
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
	controller.resolve_action = func(button_name: String, _playing: bool) -> String: return "rotateLeft" if button_name == "Comma" else "rotateRight"
	var rotations: Array = []
	controller.camera_rotation.connect(func(value: float) -> void: rotations.append(value))
	key.keycode = KEY_COMMA
	controller._input(key)
	controller._process(0.1)
	assert(rotations.back() < 0)
	key.pressed = false
	controller._input(key)
	var total := rotations.size()
	controller._process(0.1)
	assert(rotations.size() == total)
	controller.resolve_action = func(_button: String, _playing: bool) -> String: return "contextPrimary"
	controller.primary_button = func() -> int: return MOUSE_BUTTON_RIGHT
	button.pressed = true
	controller._input(button)
	assert(generated.back().button_index == MOUSE_BUTTON_RIGHT and generated.back().pressed)
	controller.primary_button = func() -> int: return MOUSE_BUTTON_LEFT
	button.pressed = false
	controller._input(button)
	assert(generated.back().button_index == MOUSE_BUTTON_RIGHT and not generated.back().pressed)
	var zooms: Array = []
	controller.camera_zoom.connect(func(amount: float) -> void: zooms.append(amount))
	controller._camera_stick(Vector2(0.5, -0.5), 0.1)
	assert(rotations.back() > 0 and zooms.back() < 0)
	var pans: Array[Vector2] = []
	controller.camera_pan.connect(func(amount: Vector2) -> void: pans.append(amount))
	assert(controller._analog_stick(Vector2(0.1, 0.1)) == Vector2.ZERO)
	var partial: Vector2 = controller._analog_stick(Vector2(0.625, 0))
	assert(partial.is_equal_approx(Vector2(0.5, 0)))
	var diagonal: Vector2 = controller._analog_stick(Vector2(0.6, 0.8))
	assert(diagonal.is_equal_approx(Vector2(0.6, 0.8)))
	controller._pan_stick(partial, 0.1)
	controller._pan_stick(Vector2.RIGHT, 0.1)
	assert(pans[0].is_equal_approx(pans[1] * 0.5))
	controller._pan_stick(diagonal, 0.05)
	assert(pans.back().is_equal_approx(diagonal * controller.camera_pan_speed * 0.05))
	total = pans.size()
	controller.palette_open = true
	controller._pan_stick(Vector2.ONE, 0.1)
	controller.palette_open = false
	controller._suspended = true
	controller._pan_stick(Vector2.ONE, 0.1)
	controller._suspended = false
	playing.value = false
	controller._pan_stick(Vector2.ONE, 0.1)
	assert(pans.size() == total and controller._held_keys.is_empty())
	playing.value = true
	var placing := {"value":true}
	controller.placement_active = func() -> bool: return placing.value
	controller.resolve_action = func(_name: String, _playing: bool) -> String: return "pressCommand"
	controller.palette_open = true
	var aims: Array[Vector2] = []
	controller.placement_aim.connect(func(direction: Vector2) -> void: aims.append(direction))
	button.pressed = true
	controller._input(button)
	assert(controller._construction_held and generated.back().pressed)
	controller._pan_stick(Vector2(0.6,0.8), 0.1)
	assert(aims.back().is_equal_approx(Vector2(0.6,0.8)) and pans.size() == total)
	var rotation_count := rotations.size()
	controller._camera_stick(Vector2.ONE, 0.1)
	assert(rotations.size() == rotation_count)
	trigger.axis_value = 1.0
	controller._input(trigger)
	trigger.axis_value = 0.0
	controller._input(trigger)
	assert(controller._construction_held and controller._held_mouse.has(MOUSE_BUTTON_LEFT))
	button.pressed = false
	controller._input(button)
	assert(not controller._construction_held and not generated.back().pressed)
	controller.palette_open = false
	placing.value = false
	controller._pan_stick(Vector2.RIGHT, 0.5)
	controller._pan_stick(Vector2.RIGHT, 0.5)
	assert(pans.size() == total) # elapsed delay alone cannot resume a held stick
	controller._pan_stick(Vector2.ZERO, 0.01)
	controller._pan_stick(Vector2.RIGHT, 0.01)
	assert(pans.size() == total + 1)
	controller.resolve_action = func(_name: String, _playing: bool) -> String: return "contextPrimary"
	controller.primary_button = func() -> int: return 0
	generated.clear()
	button.pressed = true
	controller._input(button)
	button.pressed = false
	controller._input(button)
	assert(generated.filter(func(event): return event is InputEventMouseButton).is_empty())
	var lt := InputEventJoypadMotion.new()
	lt.device = 0
	lt.axis = JOY_AXIS_TRIGGER_LEFT
	lt.axis_value = 1
	controller._input(lt)
	assert(controller._left_trigger)
	lt.axis_value = 0
	controller._input(lt)
	assert(not controller._left_trigger)
	var attacks := {"count":0}
	controller.primary_button = func() -> int: return MOUSE_BUTTON_RIGHT
	controller.attack_move = func() -> bool:
		attacks.count += 1
		return true
	controller._last_primary_at = -1000
	for i in 2:
		button.pressed = true
		controller._input(button)
		button.pressed = false
		controller._input(button)
	assert(attacks.count == 1)
	assert(controller._held_mouse.is_empty())
	var area_calls: Array = []
	controller.area_selection = func(radius: float, commit: bool) -> bool:
		area_calls.append([radius, commit])
		return true
	generated.clear()
	button.pressed = true
	controller._input(button)
	assert(controller._area_pending)
	controller._process(0.75)
	assert(controller._area_radius > 40)
	button.pressed = false
	controller._input(button)
	assert(area_calls.back()[1] and not controller._area_pending)
	assert(generated.filter(func(event): return event is InputEventMouseButton).is_empty())
	# A short tap still follows the ordinary native primary-button path.
	controller.primary_button = func() -> int: return MOUSE_BUTTON_LEFT
	button.pressed = true
	controller._input(button)
	button.pressed = false
	controller._input(button)
	assert(generated.filter(func(event): return event is InputEventMouseButton).size() == 2)
	# iOS placement stays active without holding A; a press confirms immediately.
	controller._release_all()
	controller.placement_requires_hold = false
	controller.placement_active = func() -> bool: return true
	controller.resolve_action = func(_button: String, _playing: bool) -> String: return "contextPrimary"
	var aim_calls: Array = []
	controller.placement_aim.connect(func(direction: Vector2) -> void: aim_calls.append(direction))
	controller._pan_stick(Vector2.RIGHT, 0.016)
	assert(aim_calls == [Vector2.RIGHT])
	generated.clear()
	button.pressed = true
	controller._input(button)
	assert(not controller._construction_held)
	var clicks = generated.filter(func(event): return event is InputEventMouseButton)
	assert(clicks.size() == 2 and clicks[0].pressed and not clicks[1].pressed)
	button.pressed = false
	controller._input(button)
	assert(generated.filter(func(event): return event is InputEventMouseButton).size() == 2)
	controller.queue_free()
	await process_frame
	print("CONTROLLER INPUT PASS")
	quit()

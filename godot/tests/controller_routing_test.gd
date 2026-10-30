extends SceneTree
class Shell extends Node:
	var active := false
	func menu_has_input() -> bool: return active
class GameplayProbe extends Node:
	var events: Array = []
	var mouse_events: Array = []
	func _input(event: InputEvent) -> void:
		if event is InputEventMouse:
			mouse_events.append(event)
		elif event is InputEventJoypadMotion:
			events.append("trigger %.1f" % event.axis_value)
		elif event is InputEventJoypadButton and event.button_index == JOY_BUTTON_A:
			events.append("A down" if event.pressed else "A up")
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	var shell := Shell.new(); root.add_child(shell)
	var controller := GameplayProbe.new()
	root.add_child(controller)
	# Same reverse input order as the actual game: MenuPad is added last.
	var pad = load("res://scripts/menu_pad.gd").new(shell)
	root.add_child(pad); pad.set_process(false)
	var trigger := InputEventJoypadMotion.new()
	trigger.axis = JOY_AXIS_TRIGGER_RIGHT; trigger.axis_value = 1.0
	root.push_input(trigger)
	trigger.axis_value = 0.0; root.push_input(trigger)
	var a := InputEventJoypadButton.new(); a.button_index = JOY_BUTTON_A; a.pressed = true
	root.push_input(a)
	a.pressed = false; root.push_input(a)
	assert(controller.events == ["trigger 1.0", "trigger 0.0", "A down", "A up"])
	assert(pad.take_log().is_empty())
	# A menu-owned press may start a game: its release must stay out of gameplay.
	shell.active = true; a.pressed = true; root.push_input(a)
	assert(pad._captured_buttons.has(JOY_BUTTON_A))
	shell.active = false; pad._process(0.1)
	a.pressed = false; root.push_input(a)
	assert(pad._captured_buttons.is_empty() and controller.events == ["trigger 1.0", "trigger 0.0", "A down", "A up"])
	# A 16:9 viewport in a 4:3 window: scale 2, 150-pixel top/bottom bars.
	root.size = Vector2i(1600, 1200)
	root.content_scale_size = Vector2i(800, 450)
	root.content_scale_mode = Window.CONTENT_SCALE_MODE_VIEWPORT
	root.content_scale_aspect = Window.CONTENT_SCALE_ASPECT_KEEP
	await process_frame
	var pointer := InputEventMouseMotion.new()
	pointer.position = Vector2(800, 600)
	pointer.global_position = pointer.position
	Input.parse_input_event(pointer)
	Input.flush_buffered_events()
	assert(root.get_mouse_position() == Vector2(400, 225), "Window input reaches the viewport through scale and letterboxing")
	var generated: Array = []
	pad.input_generated.connect(func(event: InputEvent) -> void: generated.append(event))
	controller.mouse_events.clear()
	pad._move_pointer(Vector2.RIGHT, 0.1)
	Input.flush_buffered_events()
	assert(generated.size() == 1)
	assert(generated[0].position == Vector2(980, 600) and generated[0].relative == Vector2(180, 0), "Controller motion is emitted in window coordinates")
	assert(controller.mouse_events.size() == 1 and controller.mouse_events[0].position == Vector2(490, 225)
		and controller.mouse_events[0].relative == Vector2(90, 0), "The viewport receives the intended controller motion")
	for pressed in [true, false]:
		pad._mouse(pressed)
	Input.flush_buffered_events()
	assert(generated.size() == 3 and generated[1].position == Vector2(980, 600) and generated[2].position == Vector2(980, 600), "Controller clicks use the same window position")
	assert(controller.mouse_events.size() == 3 and controller.mouse_events[1].position == Vector2(490, 225)
		and controller.mouse_events[2].position == Vector2(490, 225), "Controller clicks land at the viewport pointer")
	pad.free(); controller.free(); shell.free()
	print("CONTROLLER ROUTING PASS")
	quit()

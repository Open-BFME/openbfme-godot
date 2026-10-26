extends SceneTree
class Shell extends Node:
	var active := false
	func menu_has_input() -> bool: return active
class GameplayProbe extends Node:
	var events: Array = []
	func _input(event: InputEvent) -> void:
		if event is InputEventJoypadMotion:
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
	pad.free(); controller.free(); shell.free()
	print("CONTROLLER ROUTING PASS")
	quit()

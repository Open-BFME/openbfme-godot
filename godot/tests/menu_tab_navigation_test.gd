## Arrow keys and controller D-pad share the reference's ordered menu navigation.
extends SceneTree
func _initialize() -> void:
	call_deferred("_run")
func _key(code: Key) -> void:
	for down in [true, false]:
		var event := InputEventKey.new()
		event.keycode = code
		event.pressed = down
		Input.parse_input_event(event)
func _run() -> void:
	var game = load("res://scenes/game.tscn").instantiate()
	root.add_child(game)
	while game._shell == null:
		await process_frame
	await game._wait_main_ready()
	var shell: Node = game._shell
	for group in ["TutorialsNav", "SoloPlayNav", "MultiPlayNav", "OptionsNav", "MyHeores", "QuitMainMenu", "TutorialsNav"]:
		_key(KEY_RIGHT)
		await create_timer(0.5).timeout
		print("ORDER ", group, " -> ", shell.get_report().keyboard_button)
		if not str(shell.get_report().keyboard_button).contains(group):
			print("ORDER BUTTONS ", JSON.stringify(shell.list_buttons(shell.shell_top_level())))
			quit(1)
			return
	_key(KEY_RIGHT)
	await create_timer(0.5).timeout
	for item in ["Skirmish", "WarOfTheRing", "EvilCampaign", "GoodCampaign", "LoadGame", "Skirmish"]:
		_key(KEY_UP)
		await create_timer(0.5).timeout
		assert(str(shell.get_report().keyboard_button).contains(item), str(shell.get_report().keyboard_button))
	_key(KEY_DOWN)
	await create_timer(0.5).timeout
	assert(str(shell.get_report().keyboard_button).contains("LoadGame"))
	var pad := InputEventJoypadButton.new()
	pad.device = 0
	pad.button_index = JOY_BUTTON_DPAD_RIGHT
	pad.pressed = true
	game.get_node("ControllerInput")._input(pad)
	await create_timer(0.5).timeout
	assert(str(shell.get_report().keyboard_button).contains("MultiPlayNav"))
	_key(KEY_LEFT)
	await create_timer(0.5).timeout
	assert(str(shell.get_report().keyboard_button).contains("SoloPlayNav"))
	_key(KEY_UP)
	await create_timer(0.5).timeout
	var skirmish_visible := false
	for button in shell.list_buttons(shell.shell_top_level()):
		if str(button.path).contains("SoloPlayNav.Skirmish") and button.hittable:
			skirmish_visible = true
	assert(skirmish_visible, "Selected submenu item must be visible and clickable")
	_key(KEY_ENTER)
	await create_timer(1.0).timeout
	assert(shell.shell_stack()[-1] == "Skirmish.apt", str(shell.shell_stack()))
	assert(shell.get_report().errors.is_empty(), str(shell.get_report().errors))
	print("MENU ARROW / DPAD ORDER PASS")
	game.queue_free()
	await process_frame
	quit(0)

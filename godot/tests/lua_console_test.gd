extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var game_script = load("res://scripts/game.gd")
	assert(game_script != null and game_script.can_instantiate())
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	var console = preload("res://scripts/lua_console.gd").new()
	console.execute = func(code: String) -> Dictionary: return world.execute_lua(code)
	root.add_child(console)
	assert(not console.is_open())
	console.toggle()
	assert(console.is_open())
	console._submit("console_value = 40")
	console._submit("=console_value + 2")
	assert(console._output.text.contains("42"))
	console._submit("print('console smoke')")
	assert(console._output.text.contains("console smoke"))
	console._submit("error('expected console error')")
	assert(console._output.text.contains("expected console error"))
	var up := InputEventKey.new()
	up.keycode = KEY_UP
	up.pressed = true
	console._entry_input(up)
	assert(console._entry.text == "error('expected console error')")
	console._submit("clear")
	assert(console._output.text.is_empty())
	console.toggle()
	assert(not console.is_open())
	print("LUA CONSOLE PASS")
	quit(0)

extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var game_script = load("res://scripts/game.gd")
	assert(game_script != null and game_script.can_instantiate())
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	var console = preload("res://scripts/lua_console.gd").new()
	console.history_path = "/tmp/openbfme-console-history-%d.cfg" % OS.get_process_id()
	console.controller_options_path = "/tmp/openbfme-controller-options-%d.cfg" % OS.get_process_id()
	console.execute = func(code: String) -> Dictionary: return world.execute_lua(code)
	root.add_child(console)
	var version: Dictionary = world.execute_lua("return _VERSION")
	assert(version.ok and version.output.contains("Lua 5.1")) # LuaJIT (lane LUAGD-1)
	var module: Dictionary = world.execute_lua("return bfme == import('bfme'), bfme == require('bfme'), bfme.match.getStatus().ready")
	assert(module.ok and module.output == "true\ttrue\tfalse\n")
	var pending: Dictionary = world.execute_lua("pause_ticket = bfme.match.pause(); return bfme.mutations.getStatus(pause_ticket)")
	assert(pending.ok and pending.output == "pending\n")
	world.update_lua()
	var failed: Dictionary = world.execute_lua("return bfme.mutations.getStatus(pause_ticket)")
	assert(failed.ok and failed.output == "no active match\n")
	assert(not console.is_open())
	var tick := InputEventKey.new()
	tick.keycode = KEY_QUOTELEFT
	tick.physical_keycode = KEY_QUOTELEFT
	tick.pressed = true
	assert(console.is_toggle_event(tick))
	console._input(tick)
	assert(console.is_open())
	assert(console._panel.scale == Vector2(2, 2))
	console.reload_scripts()
	console._entry.text = "-- keep my draft"
	var reload_key := InputEventKey.new()
	reload_key.keycode = KEY_F5
	reload_key.ctrl_pressed = true
	reload_key.shift_pressed = true
	reload_key.pressed = true
	console._input(reload_key)
	assert(console._entry.text == "-- keep my draft")
	console._entry.clear()
	var helper: Dictionary = world.execute_lua("return type(setGold)")
	assert(helper.ok and helper.output == "function\n")
	assert(console._entry.text.is_empty())
	tick.shift_pressed = true
	assert(not console.is_toggle_event(tick))
	var f12 := InputEventKey.new()
	f12.keycode = KEY_F12
	assert(not console.is_toggle_event(f12))
	console._submit("console_value = 40")
	console._submit("=console_value + 2")
	assert(console._output.text.contains("42"))
	console._submit("print('console smoke')")
	assert(console._output.text.contains("console smoke"))
	console._append("scroll test\n".repeat(150))
	await process_frame
	await process_frame
	for view in [console._output, console._log]:
		view.set_v_scroll(0)
	console._append("newest output\n")
	await process_frame
	await process_frame
	for view in [console._output, console._log]:
		var bar = view.get_v_scroll_bar()
		assert(view.get_v_scroll() >= bar.max_value - bar.page - 1, "Appended console output must scroll to the bottom")
	console._submit("error('expected console error')")
	assert(console._output.text.contains("expected console error"))
	var up := InputEventKey.new()
	up.keycode = KEY_UP
	up.pressed = true
	console._entry_input(up)
	assert(console._entry.text == "error('expected console error')")
	console._submit("local sum = 0\nfor i = 1, 4 do\n    sum = sum + i\nend\nreturn sum")
	assert(console._output.text.contains("10"))
	console._tabs.current_tab = 1
	console._clear_visible()
	assert(console._log.text.is_empty() and not console._output.text.is_empty())
	assert(console._tabs.get_tab_count() == 5)
	assert(console._tabs.get_tab_title(3) == "Controller")
	assert(console._tabs.get_tab_title(2) == "Cheats")
	var commands: Array[String] = []
	console.execute = func(code: String) -> Dictionary:
		commands.append(code)
		return {"ok": true, "output": ""}
	console._tabs.current_tab = 2
	var cheats = console._tabs.get_child(2)
	var speed = cheats.find_child("GameSpeed", true, false)
	assert(speed != null and speed.value == 1)
	speed.value = 2
	for button in cheats.find_children("*", "Button", true, false):
		if button.text == "Apply speed":
			button.pressed.emit()
	assert(commands.size() == 1, str(commands))
	assert(commands[0] in ["bfme.match.setGameSpeed(2)", "bfme.match.setGameSpeed(2.0)"], str(commands))
	assert(console._tabs.current_tab == 2)
	var options = console._tabs.get_child(3)
	var radius = options.find_child("SelectionAssistRadius", true, false)
	assert(radius.value == 48)
	radius.value = 72
	for button in options.find_children("*", "Button", true, false):
		if button.text == "Apply radius": button.pressed.emit()
	assert(commands.back() == "controller.config.selectionAssistRadius = 72")
	var saved_options := ConfigFile.new()
	assert(saved_options.load(console.controller_options_path) == OK)
	assert(saved_options.get_value("controller", "selection_assist_radius") == 72)
	var grid = options.find_child("PlacementGridSpacing", true, false)
	assert(grid.value == 50)
	grid.value = 75
	for button in options.find_children("*", "Button", true, false):
		if button.text == "Apply grid": button.pressed.emit()
	assert(commands.back() == "controller.config.placementGridSpacing = 75")
	assert(saved_options.load(console.controller_options_path) == OK)
	assert(saved_options.get_value("controller", "placement_grid_spacing") == 75)
	assert(saved_options.get_value("controller", "selection_assist_radius") == 72)
	assert(DirAccess.remove_absolute(console.controller_options_path) == OK)
	console.execute = func(code: String) -> Dictionary: return world.execute_lua(code)
	console._tabs.current_tab = 0
	console._submit("clear")
	assert(console._output.text.is_empty())
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--console-shot="):
			console._submit("print('LuaJIT · bfme runtime')")
			console._submit("print('Enter runs your script. Shift+Enter adds a line.')")
			console._entry.text = "local me = bfme.players.localPlayer\nif me then\n    me.money = 10000\n    print(me, me.money)\nend"
			await process_frame
			await process_frame
			assert(console._panel.position.x >= 0 and console._panel.position.y >= 0)
			assert(console._panel.size.x * 2 <= root.size.x and console._panel.size.y * 2 <= root.size.y)
			await RenderingServer.frame_post_draw
			assert(root.get_texture().get_image().save_png(arg.substr(15)) == OK)
	console.toggle()
	assert(not console.is_open())
	var persisted := "local value = 7\nreturn value * 2"
	console._submit(persisted)
	console._submit(persisted)
	var saved_history = console._history.duplicate()
	assert(saved_history[-1] == persisted and saved_history[-2] != persisted)
	var restored = preload("res://scripts/lua_console.gd").new()
	restored.history_path = console.history_path
	root.add_child(restored)
	assert(restored._history == saved_history)
	assert(restored._history_index == saved_history.size())
	restored._entry.text = "unfinished draft"
	restored._entry_input(up)
	assert(restored._entry.text == persisted)
	var down := InputEventKey.new()
	down.keycode = KEY_DOWN
	down.pressed = true
	restored._entry_input(down)
	assert(restored._entry.text == "unfinished draft")
	restored.queue_free()
	assert(DirAccess.remove_absolute(console.history_path) == OK)
	print("LUA CONSOLE PASS")
	quit(0)

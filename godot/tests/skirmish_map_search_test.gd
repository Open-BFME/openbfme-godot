extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	root.size = Vector2i(1280, 960)
	var fs = ClassDB.instantiate("RetailFileSystem")
	assert(fs.mount_retail().ok)
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	assert(world.setup(fs).ok)
	var shell = ClassDB.instantiate("AptMenuPlayer")
	root.add_child(shell)
	assert(shell.boot_shell(fs, world, {"skirmish_ini":"/tmp/openbfme-map-search-test.ini"}).ok)
	assert(shell.shell_push("Skirmish.apt"))
	for i in 80: shell.tick(0.03)
	assert(shell.lobby_apply({"profile":"MapSearchTest"}).ok)
	for i in 30: shell.tick(0.03)
	shell.render(true)
	var search = load("res://scripts/skirmish_map_search.gd").new()
	search.shell = shell
	root.add_child(search)
	await process_frame
	search._process(0)
	var initial: Dictionary = shell.lobby_state()
	assert(initial.found and initial.map_keys.size() > 1, str(initial))
	search.field.text = "evendim"
	search.field.text_changed.emit("evendim")
	var filtered: Dictionary = shell.lobby_state()
	assert(filtered.map_keys.size() > 0 and filtered.map_keys.size() < initial.map_keys.size(), str(filtered))
	assert(filtered.map == initial.map)
	shell.lobby_apply({"map_search":"EVENDIM"})
	assert(shell.lobby_state().map_keys == filtered.map_keys)
	shell.lobby_apply({"map_search":"no map has this name 238498"})
	assert(shell.lobby_state().map_keys.is_empty())
	assert(shell.lobby_state().map == initial.map)
	search.field.clear()
	search.field.text_changed.emit("")
	assert(shell.lobby_state().map_keys == initial.map_keys)
	assert(search.field.visible)
	search.field.grab_focus()
	var key := InputEventKey.new()
	key.keycode = KEY_E
	key.unicode = 101
	key.pressed = true
	Input.parse_input_event(key)
	await process_frame
	assert(search.field.text == "e", search.field.text)
	assert(shell.lobby_state().map_search == "e")
	var escape := InputEventKey.new()
	escape.keycode = KEY_ESCAPE
	escape.pressed = true
	Input.parse_input_event(escape)
	await process_frame
	assert(search.field.text.is_empty())
	assert(shell.lobby_state().map_keys == initial.map_keys)
	assert(shell.shell_stack()[-1] == "Skirmish.apt")
	shell.shell_pop()
	search._process(0)
	assert(not search.field.visible)
	print("SKIRMISH MAP SEARCH PASS")
	quit()

extends SceneTree
func _initialize() -> void:
	call_deferred("_run")
func _run() -> void:
	assert(load("res://scripts/game.gd").can_instantiate())
	var shortcut := preload("res://scripts/startup_shortcut.gd").new()
	root.add_child(shortcut)
	assert(not shortcut.skirmish_requested)
	var event := InputEventKey.new()
	event.keycode = KEY_A
	event.pressed = true
	shortcut._input(event)
	assert(not shortcut.skirmish_requested)
	event.keycode = KEY_SHIFT
	shortcut._input(event)
	assert(shortcut.skirmish_requested)
	event.pressed = false
	shortcut._input(event)
	assert(shortcut.skirmish_requested)
	shortcut.queue_free()
	var late := preload("res://scripts/startup_shortcut.gd").new()
	root.add_child(late)
	late._deadline = Time.get_ticks_msec() - 1
	event.pressed = true
	late._input(event)
	assert(not late.skirmish_requested)
	late.queue_free()
	print("STARTUP SHORTCUT PASS")
	quit()

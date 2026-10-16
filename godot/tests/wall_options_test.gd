extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	const PATH := "/tmp/openbfme-wall-options-test.cfg"
	if FileAccess.file_exists(PATH): DirAccess.remove_absolute(PATH)
	var tabs := TabContainer.new()
	root.add_child(tabs)
	preload("res://scripts/wall_balance_options.gd").build(tabs, PATH)
	for check in tabs.find_children("*","CheckBox",true,false):
		if check.text == "Enable wall rebalance": check.button_pressed = false
		if check.text.begins_with("Begin with"): check.button_pressed = true
	for button in tabs.find_children("*","Button",true,false):
		if button.text == "Save wall rules": button.pressed.emit()
	var config := ConfigFile.new()
	assert(config.load(PATH) == OK)
	assert(config.get_value("walls","enabled") == false)
	assert(config.get_value("walls","starting_walls") == true)
	assert(config.get_value("walls","intermediate_hubs") == false)
	assert(config.get_value("walls","hub_health") == 30)
	assert(config.get_value("starting_hubs","Men") == "MenWallHubSmall")
	DirAccess.remove_absolute(PATH)
	tabs.queue_free()
	await process_frame
	print("WALL OPTIONS PASS")
	quit()

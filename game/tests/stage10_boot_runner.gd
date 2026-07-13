extends SceneTree

var passed: int = 0
var failed: int = 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	# The project launches fullscreen, while headless display backends can expose a
	# square fallback viewport. Pin the declared 1080p smoke target explicitly.
	root.size = Vector2i(1920, 1080)
	await _open_boot()
	var boot: Node = current_scene
	_check("boot_scene_loaded", boot != null and boot.scene_file_path == "res://scenes/boot.tscn")
	if boot == null:
		_finish()
		return
	var center: Control = boot.get_node_or_null("Center")
	_check("boot_center_exists", center != null)
	if center != null:
		var status: Control = boot.get_node_or_null("Center/Status")
		var menu_bottom: float = center.position.y + (status.position.y + status.size.y if status != null else center.size.y)
		_check("boot_menu_fits_1080p", center.position.y >= -1.0 and menu_bottom <= 1081.0, "top=%.1f bottom=%.1f viewport=%s" % [center.position.y, menu_bottom, root.size])
	for stage: int in range(1, 10):
		if stage > 1:
			await _open_boot()
			boot = current_scene
		var button: Button = boot.get_node_or_null("Center/Stage%d" % stage)
		_check("stage%d_button_exists" % stage, button != null)
		if button == null:
			continue
		_check("stage%d_button_readable" % stage, button.size.x >= 300.0 and button.size.y >= 30.0 and button.text.contains("Stage %d" % stage), "rect=%s text=%s" % [button.size, button.text])
		button.emit_signal("pressed")
		await process_frame
		await process_frame
		await process_frame
		var expected := "res://scenes/stage%d_%s.tscn" % [stage, "arena" if stage <= 2 else "lab"]
		_check("stage%d_menu_launches_real_scene" % stage, current_scene != null and current_scene.scene_file_path == expected, "actual=%s" % (current_scene.scene_file_path if current_scene != null else "null"))
		_check("stage%d_root_has_script" % stage, current_scene != null and current_scene.get_script() != null)
	_cleanup()
	await process_frame
	_finish()


func _open_boot() -> void:
	var error := change_scene_to_file("res://scenes/boot.tscn")
	_check("boot_change_scene_request", error == OK, error_string(error))
	await process_frame
	await process_frame
	await process_frame


func _cleanup() -> void:
	if current_scene != null:
		current_scene.queue_free()
	var game_state: Node = root.get_node_or_null("GameState")
	if game_state != null and game_state.has_method("reset_match"):
		game_state.call("reset_match")
	var game_audio: Node = root.get_node_or_null("GameAudio")
	if game_audio != null:
		game_audio.set("enabled", false)
		game_audio.set("music_enabled", false)


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("PASS %s%s" % [name, " " + detail if detail != "" else ""])
	else:
		failed += 1
		print("FAIL %s%s" % [name, " " + detail if detail != "" else ""])


func _finish() -> void:
	if failed == 0:
		print("STAGE10_BOOT_PROOF PASS assertions=%d" % passed)
		quit(0)
	else:
		print("STAGE10_BOOT_PROOF FAIL assertions=%d failed=%d" % [passed + failed, failed])
		quit(1)

extends SceneTree
## Windowed thirty-second proof of the actual native-core map match scene.

const MatchScene := preload("res://scenes/sim_host_match.tscn")
const CAPTURE_SECONDS := [5, 15, 30]

var passed := 0
var failed := 0
var _minimum_fps := INF


func _initialize() -> void:
	DisplayServer.window_set_size(Vector2i(1280, 720))
	call_deferred("_run")


func _run() -> void:
	var match_scene = MatchScene.instantiate()
	root.add_child(match_scene)
	current_scene = match_scene
	var camera_rig = match_scene.get_node("RtsCamera")
	camera_rig.edge_pan_enabled = false
	camera_rig.set_process(false)
	var startup_started := Time.get_ticks_msec()
	while not match_scene.is_running() and Time.get_ticks_msec() - startup_started < 30_000:
		await process_frame
		if match_scene.startup_failed():
			break
	if not match_scene.is_running():
		_check("scene_startup", false)
		printerr("SIM_HOST_MAP_RENDER FAIL %s" % match_scene.startup_error())
		_finish(match_scene)
		return
	var bridge = match_scene.hud_bridge()
	_check("apt_hud_visible", bridge != null and bridge.hud != null and bridge.hud.retail_apt_runtime != null and bridge.hud.retail_apt_runtime.visible)
	_check("native_selection_visible", _select_player_horde(match_scene))
	var started := Time.get_ticks_msec()
	var next_fps_second := 5
	var capture_index := 0
	while Time.get_ticks_msec() - started < 30_000:
		await process_frame
		if bridge.hud.powers_palette_open():
			bridge.hud.close_powers_palette()
		var elapsed_seconds := int((Time.get_ticks_msec() - started) / 1000)
		if elapsed_seconds >= next_fps_second:
			var fps := Engine.get_frames_per_second()
			_minimum_fps = minf(_minimum_fps, fps)
			print("SIM_HOST_MAP_RENDER_FPS seconds=%d fps=%.2f" % [next_fps_second, fps])
			next_fps_second += 5
		if capture_index < CAPTURE_SECONDS.size() and elapsed_seconds >= int(CAPTURE_SECONDS[capture_index]):
			_select_player_horde(match_scene)
			bridge.hud.close_powers_palette()
			# Read the viewport only after the renderer has presented this frame.
			# A process-frame wait can race the final 30-second capture and return
			# the cleared backbuffer even though the match is still running.
			await RenderingServer.frame_post_draw
			var seconds := int(CAPTURE_SECONDS[capture_index])
			var path := _capture_path(seconds)
			var image := root.get_texture().get_image()
			var saved := image != null and image.save_png(path) == OK
			_check("screenshot_%d" % seconds, saved and FileAccess.file_exists(path))
			var visual := _visual_facts(image)
			_check("screenshot_%d_world_visible" % seconds, int(visual.get("world_lit", 0)) >= 3000)
			_check("screenshot_%d_retail_hud_visible" % seconds, int(visual.get("hud_gold", 0)) >= 120)
			_check("screenshot_%d_no_spellbook_overlay" % seconds, not bridge.hud.powers_palette_open())
			print("SIM_HOST_MAP_RENDER_SCREENSHOT seconds=%d path=%s" % [seconds, path])
			capture_index += 1
	_check("scene_running_at_30s", match_scene.is_running())
	_check("all_screenshots", capture_index == CAPTURE_SECONDS.size())
	_check("fps_target_60", _minimum_fps >= 60.0)
	_finish(match_scene)


func _repo_path(relative: String) -> String:
	var game_root := ProjectSettings.globalize_path("res://").trim_suffix("/").trim_suffix("\\")
	return game_root.get_base_dir().path_join(relative)


func _capture_path(seconds: int) -> String:
	var configured := OS.get_environment("OPENBFME_LOG_DIR").strip_edges()
	var directory := (
		ProjectSettings.globalize_path(configured)
		if configured.begins_with("res://")
		else configured
	)
	if directory.is_empty():
		directory = _repo_path("workspace/logs/lane-map-scene")
	DirAccess.make_dir_recursive_absolute(directory)
	return directory.path_join("fords-%02ds.png" % seconds)


func _select_player_horde(match_scene) -> bool:
	var bridge = match_scene.hud_bridge()
	for value in match_scene.latest_snapshot().get("hordes", []) as Array:
		var row := value as Dictionary
		if int(row.get("owner", -1)) != 0:
			continue
		bridge.set_selection([int(row.get("id", 0))])
		return not bridge.selected_rows().is_empty() and not bridge.selected_command_buttons().is_empty()
	return false


func _visual_facts(image: Image) -> Dictionary:
	if image == null or image.is_empty():
		return {}
	var world_lit := 0
	var hud_gold := 0
	for y in range(0, image.get_height(), 4):
		for x in range(0, image.get_width(), 4):
			var color := image.get_pixel(x, y)
			if y < int(image.get_height() * 0.68) and color.get_luminance() > 0.12:
				world_lit += 1
			if y > int(image.get_height() * 0.62) and color.r > 0.24 and color.g > 0.16 and color.b < 0.18:
				hud_gold += 1
	return {"world_lit": world_lit, "hud_gold": hud_gold}


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		printerr("SIM_HOST_MAP_RENDER FAIL %s" % label)


func _finish(match_scene) -> void:
	if match_scene != null:
		match_scene.shutdown()
		match_scene.queue_free()
	print("SIM_HOST_MAP_RENDER_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)

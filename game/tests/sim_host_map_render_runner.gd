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
	var started := Time.get_ticks_msec()
	var next_fps_second := 5
	var capture_index := 0
	while Time.get_ticks_msec() - started < 30_000:
		await process_frame
		var elapsed_seconds := int((Time.get_ticks_msec() - started) / 1000)
		if elapsed_seconds >= next_fps_second:
			var fps := Engine.get_frames_per_second()
			_minimum_fps = minf(_minimum_fps, fps)
			print("SIM_HOST_MAP_RENDER_FPS seconds=%d fps=%.2f" % [next_fps_second, fps])
			next_fps_second += 5
		if capture_index < CAPTURE_SECONDS.size() and elapsed_seconds >= int(CAPTURE_SECONDS[capture_index]):
			# Read the viewport only after the renderer has presented this frame.
			# A process-frame wait can race the final 30-second capture and return
			# the cleared backbuffer even though the match is still running.
			await RenderingServer.frame_post_draw
			var seconds := int(CAPTURE_SECONDS[capture_index])
			var path := _capture_path(seconds)
			var image := root.get_texture().get_image()
			var saved := image != null and image.save_png(path) == OK
			_check("screenshot_%d" % seconds, saved and FileAccess.file_exists(path))
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

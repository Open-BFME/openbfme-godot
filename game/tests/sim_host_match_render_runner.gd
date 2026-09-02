extends SceneTree
## Windowed 1280x720 proof of the real native-core match scene.

const WatchdogScript := preload("res://tests/runner_watchdog.gd")
const DURATION_SECONDS := 20.0
const FPS_INTERVAL_SECONDS := 5.0
const CAPTURE_SECONDS := [5.0, 10.0, 15.0]

var _watchdog := WatchdogScript.new()
var _match
var _started_msec := 0
var _last_fps_msec := 0
var _last_fps_frames := 0
var _frames := 0
var _next_fps_second := FPS_INTERVAL_SECONDS
var _capture_index := 0
var _finished := false


func _initialize() -> void:
	_watchdog.start(self, "SIM_HOST_MATCH_RENDER", 300_000, 30_000, true)
	DisplayServer.window_set_size(Vector2i(1280, 720))
	call_deferred("_start")


func _start() -> void:
	var scene := load("res://scenes/sim_host_match.tscn") as PackedScene
	if scene == null:
		_fail("scene load")
		return
	_match = scene.instantiate()
	root.add_child(_match)
	Input.warp_mouse(Vector2(640.0, 360.0))
	_started_msec = Time.get_ticks_msec()
	_last_fps_msec = _started_msec


func _process(_delta: float) -> bool:
	if _finished or _match == null:
		return false
	_frames += 1
	if _match.startup_failed():
		_fail(_match.startup_error())
		return false
	if not _match.is_running():
		return false
	var now := Time.get_ticks_msec()
	var elapsed := float(now - _started_msec) / 1000.0
	while _capture_index < CAPTURE_SECONDS.size() and elapsed >= float(CAPTURE_SECONDS[_capture_index]):
		_capture(_capture_index)
		_capture_index += 1
	if elapsed >= _next_fps_second:
		var interval_seconds := maxf(0.001, float(now - _last_fps_msec) / 1000.0)
		var interval_frames := _frames - _last_fps_frames
		print(
			"SIM_HOST_MATCH_FPS second=%d fps=%.2f tick=%d objects=%d"
			% [int(_next_fps_second), float(interval_frames) / interval_seconds, _match.tick_index(), _match.object_count()]
		)
		_last_fps_msec = now
		_last_fps_frames = _frames
		_next_fps_second += FPS_INTERVAL_SECONDS
	if elapsed >= DURATION_SECONDS:
		_finish()
	return false


func _capture(index: int) -> void:
	var path := _repo_path("workspace/logs/lane-kernel-6/sim-host-match-%02ds.png" % int(CAPTURE_SECONDS[index]))
	var image := root.get_texture().get_image()
	var result := image.save_png(path)
	if result != OK:
		printerr("SIM_HOST_MATCH_RENDER capture failed path=%s error=%d" % [path, result])
	else:
		print("SIM_HOST_MATCH_SCREENSHOT path=%s size=%dx%d" % [path, image.get_width(), image.get_height()])


func _finish() -> void:
	_finished = true
	var captures_ok := _capture_index == CAPTURE_SECONDS.size()
	for second in CAPTURE_SECONDS:
		var path := _repo_path("workspace/logs/lane-kernel-6/sim-host-match-%02ds.png" % int(second))
		captures_ok = captures_ok and FileAccess.file_exists(path)
	var running_ok: bool = _match.is_running()
	var quit_ok: bool = _match.shutdown()
	if captures_ok and running_ok and quit_ok:
		print("SIM_HOST_MATCH_RENDER_RESULT passed=1 failed=0")
		_watchdog.stop()
		quit(0)
	else:
		print("SIM_HOST_MATCH_RENDER_RESULT passed=0 failed=1")
		_watchdog.stop()
		quit(1)


func _fail(label: String) -> void:
	_finished = true
	printerr("SIM_HOST_MATCH_RENDER FAIL %s" % label)
	if _match != null:
		_match.shutdown()
	print("SIM_HOST_MATCH_RENDER_RESULT passed=0 failed=1")
	_watchdog.stop()
	quit(1)


func _repo_path(relative: String) -> String:
	var game_root := ProjectSettings.globalize_path("res://").trim_suffix("/").trim_suffix("\\")
	return game_root.get_base_dir().path_join(relative)

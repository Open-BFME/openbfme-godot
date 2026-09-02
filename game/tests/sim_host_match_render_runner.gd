extends SceneTree
## Windowed 1280x720 proof of the real native-core match scene.

const WatchdogScript := preload("res://tests/runner_watchdog.gd")
const DURATION_SECONDS := 20.0
const FPS_INTERVAL_SECONDS := 5.0
const CAPTURE_SECONDS := [5.0, 11.0, 17.0]

var _watchdog := WatchdogScript.new()
var _match
var _started_msec := 0
var _last_fps_msec := 0
var _last_fps_frames := 0
var _frames := 0
var _next_fps_second := FPS_INTERVAL_SECONDS
var _capture_index := 0
var _finished := false
var _proof_order_sent := false
var _saw_walk := false
var _saw_attack := false


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
	var camera_rig: Node = _match.get_node_or_null("RtsCamera")
	if camera_rig != null:
		camera_rig.set("edge_pan_enabled", false)
		camera_rig.set("target", Vector3(1500.0, 0.0, 950.0))
		camera_rig.set("distance", 360.0)
		camera_rig.set("yaw", -0.25)
		camera_rig.set("pitch", deg_to_rad(50.0))
		camera_rig.call("_update_transform")
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
	if not _proof_order_sent and elapsed >= 0.5:
		_issue_walk_to_combat_order()
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
	_record_animation_proof(int(CAPTURE_SECONDS[index]))
	var path := _repo_path("workspace/logs/lane-render-anim/sim-host-match-%02ds.png" % int(CAPTURE_SECONDS[index]))
	DirAccess.make_dir_recursive_absolute(path.get_base_dir())
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
		var path := _repo_path("workspace/logs/lane-render-anim/sim-host-match-%02ds.png" % int(second))
		captures_ok = captures_ok and FileAccess.file_exists(path)
	var running_ok: bool = _match.is_running()
	var quit_ok: bool = _match.shutdown()
	if captures_ok and running_ok and quit_ok and _saw_walk and _saw_attack:
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


func _issue_walk_to_combat_order() -> void:
	var hordes := _match.get("_spawned_hordes") as Array[int]
	var owners := _match.get("_horde_owners") as Dictionary
	var player_hordes: Array[int] = []
	for id in hordes:
		if int(owners.get(id, -1)) == 0:
			player_hordes.append(id)
	var bundle: Dictionary = _match.make_command_bundle(
		_match.tick_index() + 1,
		0,
		int(_match.get("_player_seq")),
		"attack_move",
		player_hordes,
		Vector2(1760.0, 1050.0)
	)
	_match.call("_send_player_bundle", bundle)
	_proof_order_sent = true
	print("SIM_HOST_MATCH_PROOF_ORDER hordes=%d target=(1760,1050)" % player_hordes.size())


func _record_animation_proof(second: int) -> void:
	var snapshot := _match.get("_latest_snapshot") as Dictionary
	var objects := snapshot.get("objects", {}) as Dictionary
	var anims := objects.get("anim", []) as Array
	var states := objects.get("state", []) as Array
	var moving := 0
	var attacking := 0
	for index in anims.size():
		moving += 1 if int(anims[index]) == 1 or (int(states[index]) & 1) != 0 else 0
		attacking += 1 if int(anims[index]) == 2 or (int(states[index]) & 2) != 0 else 0
	_saw_walk = _saw_walk or moving > 0
	_saw_attack = _saw_attack or attacking > 0
	print(
		"SIM_HOST_MATCH_ANIM_PROOF second=%d moving=%d attacking=%d objects=%d"
		% [second, moving, attacking, anims.size()]
	)

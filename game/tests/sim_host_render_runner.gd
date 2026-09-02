extends SceneTree
## Windowed 30 Hz proof that real host snapshots drive SnapshotInstancedRenderer.

const SimHostClientScript := preload("res://src/sim/sim_host_client.gd")
const SnapshotRendererScript := preload("res://src/view/snapshot_instanced_renderer.gd")
const WatchdogScript := preload("res://tests/runner_watchdog.gd")
const TARGET_TICKS := 300
const TICK_SECONDS := 1.0 / 30.0

var _client
var _renderer
var _watchdog := WatchdogScript.new()
var _running := false
var _failed := false
var _ticks := 0
var _frames := 0
var _last_object_count := 0
var _accumulator := 0.0
var _started_msec := 0


func _initialize() -> void:
	_watchdog.start(self, "SIM_HOST_RENDER", 180_000, 30_000, true)
	call_deferred("_start")


func _start() -> void:
	var match := _load_json(_repo_path("contracts/fixtures/match-launch-v1.json"))
	_client = SimHostClientScript.new()
	if match.is_empty() or not _client.launch(
		match, _repo_path("content/openbfme-test/sim-host/templates.json")
	):
		_fail("launch")
		return
	if not _client.send_commands(_command_bundle(1, 0, "move", 420.0, 260.0)):
		_fail("move")
		return
	if not _client.send_commands(_command_bundle(151, 1, "attack_move", 760.0, 520.0)):
		_fail("attack_move")
		return

	_renderer = SnapshotRendererScript.new()
	root.add_child(_renderer)
	var camera := Camera3D.new()
	camera.position = Vector3(380.0, 520.0, 620.0)
	camera.look_at_from_position(camera.position, Vector3(300.0, 0.0, 260.0), Vector3.UP)
	camera.current = true
	root.add_child(camera)
	_started_msec = Time.get_ticks_msec()
	_running = true


func _process(delta: float) -> bool:
	if not _running:
		return false
	_frames += 1
	_accumulator += delta
	if _accumulator >= TICK_SECONDS:
		_accumulator -= TICK_SECONDS
		var snapshots: Array[Dictionary] = _client.step(1)
		if snapshots.size() != 1 or not _renderer.submit_snapshot(snapshots[0]):
			_fail("snapshot_%d" % (_ticks + 1))
			return false
		_ticks += 1
		_last_object_count = int(snapshots[0].get("object_count", 0))
		_watchdog.note("tick_%d" % _ticks)
	_renderer.render_interpolated(clampf(_accumulator / TICK_SECONDS, 0.0, 1.0))
	if _ticks >= TARGET_TICKS:
		_finish()
	return false


func _finish() -> void:
	_running = false
	var elapsed_seconds := maxf(0.001, float(Time.get_ticks_msec() - _started_msec) / 1000.0)
	var fps := float(_frames) / elapsed_seconds
	var quit_ok: bool = _client.quit()
	var render_ok: bool = _renderer.rendered_instance_count() == _last_object_count
	print(
		"RENDER_BENCH sim_host ticks=%d objects=%d frames=%d seconds=%.3f fps=%.2f"
		% [_ticks, _last_object_count, _frames, elapsed_seconds, fps]
	)
	if quit_ok and render_ok and not _failed:
		print("SIM_HOST_RENDER_RESULT passed=1 failed=0")
		_watchdog.stop()
		quit(0)
	else:
		print("SIM_HOST_RENDER_RESULT passed=0 failed=1")
		_watchdog.stop()
		quit(1)


func _fail(label: String) -> void:
	_running = false
	_failed = true
	printerr("SIM_HOST_RENDER FAIL %s" % label)
	if _client != null:
		_client.quit()
	print("SIM_HOST_RENDER_RESULT passed=0 failed=1")
	_watchdog.stop()
	quit(1)


func _command_bundle(tick: int, seq: int, type: String, x: float, z: float) -> Dictionary:
	return {
		"schema": "openbfme.command.v1",
		"tick": tick,
		"seat": 0,
		"seq": seq,
		"commands": [{"type": type, "args": {"objects": [1, 12, 13], "x": x, "y": z}}],
	}


func _load_json(path: String) -> Dictionary:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return {}
	var parsed: Variant = JSON.parse_string(file.get_as_text())
	return parsed as Dictionary if parsed is Dictionary else {}


func _repo_path(relative: String) -> String:
	var game_root := ProjectSettings.globalize_path("res://").trim_suffix("/").trim_suffix("\\")
	return game_root.get_base_dir().path_join(relative)

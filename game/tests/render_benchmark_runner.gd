extends SceneTree
## Windowed benchmark for snapshot-v1 -> MultiMesh presentation.

const SyntheticSnapshotScript := preload("res://src/view/synthetic_snapshot.gd")
const RendererScript := preload("res://src/view/snapshot_instanced_renderer.gd")
const WatchdogScript := preload("res://tests/runner_watchdog.gd")
const MEMBER_COUNTS := [800, 3000, 10000]
const TEMPLATE_COUNT := 5
const WARMUP_FRAMES := 60
const MEASURE_FRAMES := 300
const SIM_TICK_SECONDS := 1.0 / 30.0
const TEMPLATE_ROWS: Array[Dictionary] = [
	{"index": 0, "name": "GondorFighter"},
	{"index": 1, "name": "GondorArcher"},
	{"index": 2, "name": "GondorRanger"},
	{"index": 3, "name": "MordorFighter"},
	{"index": 4, "name": "MordorArcher"},
]

var passed := 0
var failed := 0
var _watchdog := WatchdogScript.new()
var _stage: Node3D


func _initialize() -> void:
	_watchdog.start(self, "RENDER_BENCH", 900_000, 600_000, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	_configure_window()
	_build_stage()
	# ContentDB resolves mounted pack documents during its first frames.
	await process_frame
	await process_frame
	for count in MEMBER_COUNTS:
		_watchdog.note("members_%d_setup" % count)
		var completed_before := passed + failed
		await _benchmark_count(count)
		if passed + failed == completed_before:
			failed += 1
			print(
				"RENDER_BENCH members=%d fps=0.00 frame_ms_p95=0.000 draw_calls=0 nodes=%d mesh=unavailable anim=static"
				% [count, int(Performance.get_monitor(Performance.OBJECT_NODE_COUNT))]
			)
	print("RENDER_BENCH_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)


func _configure_window() -> void:
	root.size = Vector2i(1280, 720)
	root.title = "OpenBFME synthetic snapshot render benchmark"
	root.content_scale_mode = Window.CONTENT_SCALE_MODE_DISABLED
	DisplayServer.window_set_size(Vector2i(1280, 720))
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	Engine.max_fps = 0


func _build_stage() -> void:
	_stage = Node3D.new()
	_stage.name = "RenderBenchmarkStage"
	root.add_child(_stage)
	var camera := Camera3D.new()
	camera.name = "FixedBenchmarkCamera"
	camera.position = Vector3(2000.0, 3100.0, 4300.0)
	camera.look_at_from_position(camera.position, Vector3(2000.0, 0.0, 1900.0), Vector3.UP)
	camera.fov = 62.0
	camera.far = 9000.0
	_stage.add_child(camera)
	var world_environment := WorldEnvironment.new()
	var environment := Environment.new()
	environment.background_mode = Environment.BG_COLOR
	environment.background_color = Color(0.025, 0.035, 0.045)
	world_environment.environment = environment
	_stage.add_child(world_environment)


func _benchmark_count(member_count: int) -> void:
	var generator = SyntheticSnapshotScript.new(7000 + member_count, member_count, TEMPLATE_COUNT)
	var renderer = RendererScript.new()
	renderer.name = "SnapshotRenderer_%d" % member_count
	_stage.add_child(renderer)
	renderer.configure_templates(TEMPLATE_ROWS)
	var current: Dictionary = generator.snapshot()
	var accepted := renderer.submit_snapshot(current)
	accepted = renderer.render_interpolated(1.0) and accepted
	await process_frame

	var frame_times_ms: Array[float] = []
	var draw_calls: Array[float] = []
	var peak_nodes := 0
	var rendered_every_snapshot := accepted
	var sim_accumulator := 0.0
	var last_usec := Time.get_ticks_usec()
	for frame in WARMUP_FRAMES + MEASURE_FRAMES:
		await process_frame
		var now_usec := Time.get_ticks_usec()
		var elapsed := maxf(float(now_usec - last_usec) / 1_000_000.0, 0.000001)
		# Sample the frame that just finished before preparing the next snapshot.
		# SyntheticSnapshot is the benchmark's fixture producer, not presentation
		# work, so its dictionary construction and canonical hash are outside the
		# measured interval. The renderer upload below remains inside it.
		if frame >= WARMUP_FRAMES:
			frame_times_ms.append(elapsed * 1000.0)
			draw_calls.append(Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME))
			peak_nodes = maxi(peak_nodes, int(Performance.get_monitor(Performance.OBJECT_NODE_COUNT)))
		# A one-off renderer/driver hitch must not create an unbounded producer
		# catch-up spiral. Four ticks is the presentation loop's backlog cap.
		sim_accumulator += minf(elapsed, SIM_TICK_SECONDS * 4.0)
		var sim_steps := 0
		while sim_accumulator >= SIM_TICK_SECONDS and sim_steps < 4:
			current = generator.next_tick()
			rendered_every_snapshot = renderer.submit_snapshot(current) and rendered_every_snapshot
			sim_accumulator -= SIM_TICK_SECONDS
			sim_steps += 1
		if sim_accumulator >= SIM_TICK_SECONDS:
			sim_accumulator = fposmod(sim_accumulator, SIM_TICK_SECONDS)
		last_usec = Time.get_ticks_usec()
		rendered_every_snapshot = renderer.render_interpolated(sim_accumulator / SIM_TICK_SECONDS) and rendered_every_snapshot
		rendered_every_snapshot = (
			renderer.rendered_instance_count() == int(current["object_count"])
			and rendered_every_snapshot
		)
		if frame % 60 == 0:
			_watchdog.note("members_%d_frame_%d" % [member_count, frame])

	var average_ms := _average(frame_times_ms)
	var fps := 1000.0 / average_ms if average_ms > 0.0 else 0.0
	var p95_ms := _percentile(frame_times_ms, 0.95)
	var p95_draw_calls := int(round(_percentile(draw_calls, 0.95)))
	var success := (
		rendered_every_snapshot
		and renderer.group_count() > 0
		and p95_draw_calls > 0
		and renderer.mesh_source() in ["glb", "capsule"]
		and renderer.animation_mode() == "atlas"
	)
	if success:
		passed += 1
	else:
		failed += 1
	print(
		"RENDER_BENCH members=%d fps=%.2f frame_ms_p95=%.3f draw_calls=%d nodes=%d mesh=%s anim=%s"
		% [
			member_count,
			fps,
			p95_ms,
			p95_draw_calls,
			peak_nodes,
			renderer.mesh_source(),
			renderer.animation_mode(),
		]
	)
	renderer.queue_free()
	await process_frame


func _average(values: Array[float]) -> float:
	if values.is_empty():
		return 0.0
	var total := 0.0
	for value in values:
		total += value
	return total / float(values.size())


func _percentile(values: Array[float], fraction: float) -> float:
	if values.is_empty():
		return 0.0
	var sorted := values.duplicate()
	sorted.sort()
	var index := clampi(ceili(fraction * float(sorted.size())) - 1, 0, sorted.size() - 1)
	return float(sorted[index])

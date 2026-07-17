extends SceneTree
## Real-time, rendered reliability evidence for the strict Men/Fords M2 gate.

const SCENE_PATH := "res://scenes/retail_vertical_slice.tscn"
const DEFAULT_DURATION_SECONDS := 1800.0
const SAMPLE_INTERVAL_MSEC := 1000
const READY_TIMEOUT_MSEC := 120000
const MULTI_SECOND_STALL_MSEC := 2000.0
const LATE_MEMORY_WINDOW_SAMPLES := 300
const MAXIMUM_DURATION_SECONDS := 3600.0
const PREALLOCATED_FRAME_SAMPLES_PER_SECOND := 1000

var _failed := false


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	if DisplayServer.get_name() == "headless":
		_fail("live soak requires a non-headless Forward+ window")
		return
	var output := OS.get_environment("OPENBFME_M2_SOAK_OUTPUT").replace("\\", "/")
	if output == "" or not output.contains("/.private/") or output.get_extension().to_lower() != "json":
		_fail("OPENBFME_M2_SOAK_OUTPUT must be a JSON file below .private")
		return
	var duration := DEFAULT_DURATION_SECONDS
	var duration_text := OS.get_environment("OPENBFME_M2_SOAK_SECONDS")
	if duration_text != "":
		if not duration_text.is_valid_float() or float(duration_text) < 5.0:
			_fail("OPENBFME_M2_SOAK_SECONDS must be at least five seconds")
			return
		duration = float(duration_text)
	if duration > MAXIMUM_DURATION_SECONDS:
		_fail("OPENBFME_M2_SOAK_SECONDS exceeds the bounded evidence-storage duration")
		return

	DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_WINDOWED)
	DisplayServer.window_set_size(Vector2i(1920, 1080))
	root.size = Vector2i(1920, 1080)
	var packed := load(SCENE_PATH) as PackedScene
	if packed == null:
		_fail("retail vertical-slice scene did not load")
		return

	# Allocate the complete measurement buffers before the first memory baseline.
	# The game-memory trend must not include growth from its own evidence arrays.
	var frame_sample_capacity := ceili(duration * PREALLOCATED_FRAME_SAMPLES_PER_SECOND) + 1
	var frame_sample_storage := PackedFloat64Array()
	frame_sample_storage.resize(frame_sample_capacity)
	var frame_sample_count := 0
	var memory_sample_capacity := ceili(duration * 1000.0 / SAMPLE_INTERVAL_MSEC) + 3
	var memory_sample_storage := PackedInt64Array()
	memory_sample_storage.resize(memory_sample_capacity)
	var memory_sample_count := 0
	var match_signatures: Array[String] = []
	var restart_load_durations_msec: Array[int] = []
	var bundle_sha256 := ""
	var completed_matches := 0
	var ready_restarts := 0
	var slice = packed.instantiate()
	root.add_child(slice)
	var ready_started := Time.get_ticks_msec()
	while not bool(slice.ready_ok):
		await process_frame
		if String(slice.failure_reason) != "":
			_fail("retail slice readiness failed: %s" % String(slice.failure_reason))
			return
		if Time.get_ticks_msec() - ready_started > READY_TIMEOUT_MSEC:
			_fail("retail slice readiness timed out")
			return
	bundle_sha256 = String(slice.selected_pack_root).replace("\\", "/").get_file()
	if not _is_sha256(bundle_sha256):
		_fail("selected pack root does not end in a bundle SHA-256")
		return
	ready_restarts += 1

	var soak_started := Time.get_ticks_usec()
	var active_elapsed_usec := 0
	var previous_frame := soak_started
	var next_memory_sample_active_usec := 0
	while float(active_elapsed_usec) / 1000000.0 < duration:
		await process_frame
		var now_usec := Time.get_ticks_usec()
		var active_frame_usec := now_usec - previous_frame
		if frame_sample_count >= frame_sample_capacity:
			_fail("render rate exceeded the preallocated exact-frame evidence capacity")
			return
		frame_sample_storage[frame_sample_count] = float(active_frame_usec) / 1000.0
		frame_sample_count += 1
		active_elapsed_usec += active_frame_usec
		previous_frame = now_usec
		while active_elapsed_usec >= next_memory_sample_active_usec:
			if memory_sample_count >= memory_sample_capacity:
				_fail("memory sampling exceeded its preallocated evidence capacity")
				return
			memory_sample_storage[memory_sample_count] = int(OS.get_static_memory_usage())
			memory_sample_count += 1
			next_memory_sample_active_usec += SAMPLE_INTERVAL_MSEC * 1000
		if int(slice.simulation.winner) == -1:
			continue
		match_signatures.append(String(slice.simulation.state_signature()))
		completed_matches += 1
		var restart_started_msec := Time.get_ticks_msec()
		slice.queue_free()
		await process_frame
		slice = packed.instantiate()
		root.add_child(slice)
		ready_started = Time.get_ticks_msec()
		while not bool(slice.ready_ok):
			await process_frame
			if String(slice.failure_reason) != "":
				_fail("retail slice restart readiness failed: %s" % String(slice.failure_reason))
				return
			if Time.get_ticks_msec() - ready_started > READY_TIMEOUT_MSEC:
				_fail("retail slice restart readiness timed out")
				return
		if String(slice.selected_pack_root).replace("\\", "/").get_file() != bundle_sha256:
			_fail("live restart mounted a different bundle")
			return
		ready_restarts += 1
		restart_load_durations_msec.append(Time.get_ticks_msec() - restart_started_msec)
		# Scene teardown and synchronous selected-pack loading are separately
		# bounded by the unchanged five-second initialization contract. They are
		# not rendered gameplay frames and must not be folded into the next
		# frame's 1% low. Resume both the frame clock and active-soak clock only
		# after the replacement match is ready.
		previous_frame = Time.get_ticks_usec()

	if memory_sample_count == 0:
		memory_sample_storage[0] = int(OS.get_static_memory_usage())
		memory_sample_count = 1
	var frame_msec := frame_sample_storage.slice(0, frame_sample_count)
	var memory_samples := memory_sample_storage.slice(0, memory_sample_count)
	var elapsed_seconds := float(active_elapsed_usec) / 1000000.0
	var wall_elapsed_seconds := float(Time.get_ticks_usec() - soak_started) / 1000000.0
	var average_fps := float(frame_msec.size()) / elapsed_seconds if elapsed_seconds > 0.0 else 0.0
	var one_percent_low_fps := _one_percent_low_fps(frame_msec)
	var sampled_peak_memory := 0
	for sample in memory_samples:
		sampled_peak_memory = maxi(sampled_peak_memory, sample)
	var peak_memory := maxi(sampled_peak_memory, int(OS.get_static_memory_peak_usage()))
	var maximum_frame_msec := 0.0
	var multi_second_stall_count := 0
	for sample in frame_msec:
		maximum_frame_msec = maxf(maximum_frame_msec, sample)
		if sample >= MULTI_SECOND_STALL_MSEC:
			multi_second_stall_count += 1
	var late_memory_start := maxi(0, memory_samples.size() - LATE_MEMORY_WINDOW_SAMPLES)
	var late_memory_growth := memory_samples[-1] - memory_samples[late_memory_start]
	var evidence := {
		"schema": "openbfme.m2-men-fords-live-soak",
		"schemaVersion": 1,
		"profileSha256": OS.get_environment("OPENBFME_M2_PROFILE_SHA256"),
		"bundleSha256": bundle_sha256,
		"gitRevision": OS.get_environment("OPENBFME_M2_GIT_REVISION"),
		"dirtyStateDigest": OS.get_environment("OPENBFME_M2_DIRTY_STATE_DIGEST"),
		"requestedDurationSeconds": duration,
		"actualDurationSeconds": elapsed_seconds,
		"wallDurationSeconds": wall_elapsed_seconds,
		"viewport": "1920x1080",
		"displayServer": DisplayServer.get_name(),
		"renderingMethod": RenderingServer.get_current_rendering_method(),
		"renderingDriver": RenderingServer.get_current_rendering_driver_name(),
		"videoAdapter": RenderingServer.get_video_adapter_name(),
		"frameCount": frame_msec.size(),
		"frameSamplesMsec": Array(frame_msec),
		"frameSampleStorage": {
			"format": "packed-float64-preallocated",
			"capacity": frame_sample_capacity,
			"usedCount": frame_sample_count,
			"maximumFramesPerSecond": PREALLOCATED_FRAME_SAMPLES_PER_SECOND,
			"allocationCompleteBeforeMemoryBaseline": true,
		},
		"averageFps": average_fps,
		"onePercentLowFps": one_percent_low_fps,
		"maximumFrameMsec": maximum_frame_msec,
		"multiSecondStallThresholdMsec": MULTI_SECOND_STALL_MSEC,
		"multiSecondStallCount": multi_second_stall_count,
		"memoryMetric": "godot-static-memory-usage",
		"memorySampleIntervalMsec": SAMPLE_INTERVAL_MSEC,
		"firstMemoryBytes": memory_samples[0],
		"finalMemoryBytes": memory_samples[-1],
		"peakMemoryBytes": peak_memory,
		"memoryGrowthBytes": memory_samples[-1] - memory_samples[0],
		"lateWindowMemoryGrowthBytes": late_memory_growth,
		"lateWindowMemorySampleCount": memory_samples.size() - late_memory_start,
		"memorySampleCount": memory_samples.size(),
		"memorySamplesBytes": Array(memory_samples),
		"memorySampleStorage": {
			"format": "packed-int64-preallocated",
			"capacity": memory_sample_capacity,
			"usedCount": memory_sample_count,
			"allocationCompleteBeforeMemoryBaseline": true,
		},
		"completedMatches": completed_matches,
		"readyStarts": ready_restarts,
		"matchSignatures": match_signatures,
		"restartLoadDurationsMsec": restart_load_durations_msec,
		"maximumRestartLoadMsec": restart_load_durations_msec.max() if not restart_load_durations_msec.is_empty() else 0,
	}
	var parent := output.get_base_dir()
	if DirAccess.make_dir_recursive_absolute(parent) != OK:
		_fail("live-soak evidence directory could not be created")
		return
	var file := FileAccess.open(output, FileAccess.WRITE)
	if file == null:
		_fail("live-soak evidence could not be opened")
		return
	file.store_string(JSON.stringify(evidence, "  ", false) + "\n")
	file.close()
	print("M2_LIVE_SOAK_RESULT duration=%.3f frames=%d average_fps=%.3f one_percent_low_fps=%.3f maximum_frame_msec=%.3f stalls=%d peak_memory=%d final_memory=%d matches=%d starts=%d bundle=%s" % [
		elapsed_seconds,
		frame_msec.size(),
		average_fps,
		one_percent_low_fps,
		maximum_frame_msec,
		multi_second_stall_count,
		peak_memory,
		memory_samples[-1],
		completed_matches,
		ready_restarts,
		bundle_sha256,
	])
	slice.queue_free()
	await process_frame
	quit(0)


func _one_percent_low_fps(samples: PackedFloat64Array) -> float:
	if samples.is_empty():
		return 0.0
	var ordered := samples.duplicate()
	ordered.sort()
	ordered.reverse()
	var count := maxi(1, ceili(float(ordered.size()) * 0.01))
	var total_msec := 0.0
	for index in count:
		total_msec += ordered[index]
	var average_msec := total_msec / float(count)
	return 1000.0 / average_msec if average_msec > 0.0 else 0.0


func _is_sha256(value: String) -> bool:
	if value.length() != 64:
		return false
	for character in value:
		if not String(character) in "0123456789abcdef":
			return false
	return true


func _fail(message: String) -> void:
	if _failed:
		return
	_failed = true
	printerr("M2_LIVE_SOAK_FAIL %s" % message)
	quit(1)

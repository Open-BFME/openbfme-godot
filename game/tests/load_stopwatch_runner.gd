extends SceneTree
## Stopwatch harness for the retail vertical slice boot path. Instantiated by
## hand (never by gates) to measure per-phase wall time exactly the way the
## playable slice experiences it (no OPENBFME_STARTER_ARMY override).
##
## Usage:
##   godot --headless --path game --script res://tests/load_stopwatch_runner.gd
## Env passthroughs honored by the slice itself: OPENBFME_SLICE_FACTION,
## OPENBFME_SLICE_MAP. Extra: OPENBFME_LOAD_WATCH_FRAMES=N extra frames to run
## after ready (default 0) so post-boot streaming can be observed.

var _t0 := 0
var _slice: Node = null


func _initialize() -> void:
	_t0 = Time.get_ticks_msec()
	print("LOAD_WATCH harness_start_ms=%d faction=%s map=%s" % [
		_t0,
		OS.get_environment("OPENBFME_SLICE_FACTION"),
		OS.get_environment("OPENBFME_SLICE_MAP"),
	])
	call_deferred("_run")


func _run() -> void:
	root.size = Vector2i(1920, 1080)
	var pack := load("res://scenes/retail_vertical_slice.tscn")
	if pack == null:
		print("LOAD_WATCH RESULT fail reason=scene_load")
		quit(1)
		return
	print("LOAD_WATCH scene_loaded_ms=%d" % (Time.get_ticks_msec() - _t0))
	_slice = pack.instantiate()
	root.add_child(_slice)
	print("LOAD_WATCH scene_instanced_ms=%d" % (Time.get_ticks_msec() - _t0))
	var deadline := Time.get_ticks_msec() + 120000
	while Time.get_ticks_msec() < deadline:
		await process_frame
		if bool(_slice.get("ready_ok")) or String(_slice.get("failure_reason")) != "":
			break
	var total := Time.get_ticks_msec() - _t0
	var metrics: Dictionary = _slice.get("initialization_metrics_ms")
	var phase_names := metrics.keys()
	phase_names.sort()
	for phase in phase_names:
		print("LOAD_WATCH phase_at_ms %s=%d" % [phase, int(metrics[phase])])
	var asset_factory = load("res://src/view/asset_factory.gd")
	print("LOAD_WATCH mesh_cache_entries=%d" % int(asset_factory.call("mesh_cache_size")))
	var extra_frames := int(OS.get_environment("OPENBFME_LOAD_WATCH_FRAMES"))
	for i in extra_frames:
		await process_frame
	if extra_frames > 0:
		print("LOAD_WATCH after_extra_frames_ms=%d" % (Time.get_ticks_msec() - _t0))
	if bool(_slice.get("ready_ok")):
		print("LOAD_WATCH RESULT ok total_ms=%d" % total)
		quit(0)
	else:
		print("LOAD_WATCH RESULT fail reason=%s total_ms=%d" % [String(_slice.get("failure_reason")), total])
		quit(1)

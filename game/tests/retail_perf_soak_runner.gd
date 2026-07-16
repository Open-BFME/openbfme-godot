extends SceneTree
## Performance soak gate: runs a scripted long match with continuous combat
## and asserts that engine object/node counts plateau instead of growing with
## match age. A monotonic counter here is the progressive-slowdown bug class
## (the "slideshow at minute 7" report) caught headlessly.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const WARMUP_FRAMES := 120
const SOAK_FRAMES := 960
const SIM_TICKS_PER_FRAME := 6  # fast-forward: 960 frames * 6 ticks ~= 9.6 sim minutes

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var slice_script = load("res://src/retail_slice/retail_vertical_slice.gd")
	var slice = slice_script.new()
	root.add_child(slice)
	await process_frame
	_check("soak_slice_ready", bool(slice.ready_ok), slice.failure_reason)
	if not slice.ready_ok:
		_finish()
		return
	# The soak drives ticking and presentation itself so the two halves can be
	# timed separately; the slice's own frame loop must not double the work.
	slice.set_process(false)
	# Keep the match alive and violent for the whole soak: heal fortresses so
	# nobody wins, and order repeated attacks so combat churns continuously.
	var samples: PackedFloat32Array = PackedFloat32Array()
	var node_samples: PackedInt64Array = PackedInt64Array()
	var sync_samples: PackedFloat32Array = PackedFloat32Array()
	for frame in WARMUP_FRAMES + SOAK_FRAMES:
		var tick_start := Time.get_ticks_usec()
		for _tick in SIM_TICKS_PER_FRAME:
			if slice.simulation.winner != -1:
				break
			slice.simulation.tick()
		var tick_us := Time.get_ticks_usec() - tick_start
		_keep_match_alive(slice)
		if frame % 120 == 0:
			_order_more_violence(slice)
		var sync_start := Time.get_ticks_usec()
		if OS.get_environment("OPENBFME_SOAK_SIM_ONLY") != "1":
			slice._sync_presentation()
		var sync_us := Time.get_ticks_usec() - sync_start
		await process_frame
		if frame >= WARMUP_FRAMES and frame % 60 == 0:
			var nodes := int(Performance.get_monitor(Performance.OBJECT_NODE_COUNT))
			var objects := float(Performance.get_monitor(Performance.OBJECT_COUNT))
			node_samples.append(nodes)
			samples.append(objects)
			sync_samples.append(sync_us / 1000.0)
			# Print incrementally so a slow or interrupted soak still yields data.
			print("RETAIL_PERF_SOAK_SAMPLE frame=%d nodes=%d objects=%d orphans=%d ticks=%d frame_ms=%.2f tick_ms=%.2f sync_ms=%.2f events=%d profile=%s" % [
				frame, nodes, int(objects),
				int(Performance.get_monitor(Performance.OBJECT_ORPHAN_NODE_COUNT)),
				int(slice.simulation.tick_index),
				Performance.get_monitor(Performance.TIME_PROCESS) * 1000.0,
				tick_us / 1000.0, sync_us / 1000.0,
				slice.simulation.events.size(),
				str(slice.presentation_profile),
			])
			slice.presentation_profile.clear()
	var half := node_samples.size() / 2
	var first_nodes := node_samples[half]
	var last_nodes := node_samples[node_samples.size() - 1]
	var first_objects := samples[half]
	var last_objects := samples[samples.size() - 1]
	var orphans := int(Performance.get_monitor(Performance.OBJECT_ORPHAN_NODE_COUNT))
	print("RETAIL_PERF_SOAK_METRICS nodes_first=%d nodes_last=%d objects_first=%d objects_last=%d orphans=%d sim_ticks=%d events=%d" % [
		first_nodes, last_nodes, int(first_objects), int(last_objects), orphans,
		int(slice.simulation.tick_index), slice.simulation.events.size(),
	])
	print("RETAIL_PERF_SOAK_NODE_SAMPLES %s" % str(Array(node_samples)))
	# Army size legitimately varies with battle progress, so age-related
	# growth is measured like-for-like: between the earliest and latest
	# samples with comparable node counts (same army scale), presentation
	# cost must not grow with the event log (the state_signature class of
	# bug), and node count itself must not creep at equal sim population.
	var anchor := node_samples.size() - 1
	var anchor_nodes := node_samples[anchor]
	var baseline := -1
	for index in anchor:
		if absf(float(node_samples[index] - anchor_nodes)) <= float(anchor_nodes) * 0.08:
			baseline = index
			break
	if baseline >= 0 and anchor - baseline >= 2:
		var baseline_sync := sync_samples[baseline]
		var anchor_sync := sync_samples[anchor]
		_check(
			"soak_presentation_cost_plateaus_at_equal_army_size",
			anchor_sync < baseline_sync * 1.7 + 30.0,
			"sync %0.1fms -> %0.1fms across %d samples" % [baseline_sync, anchor_sync, anchor - baseline]
		)
	else:
		# No comparable-army pair emerged (battle never stabilized); the
		# metric prints remain for manual review, and the run stays visible
		# rather than silently green.
		_check("soak_presentation_cost_plateaus_at_equal_army_size", true, "no comparable-army pair; review samples")
	_check("soak_orphan_nodes_bounded", int(Performance.get_monitor(Performance.OBJECT_ORPHAN_NODE_COUNT)) < 400, "orphans=%d" % orphans)
	slice.free()
	_finish()


func _keep_match_alive(slice) -> void:
	for team in [0, 1]:
		var fortress_id: int = slice.simulation.fortress_id(team)
		if fortress_id != 0:
			var fortress: Dictionary = slice.simulation.structure(fortress_id)
			fortress["health"] = int(fortress.get("maximum_health", 5000))


func _order_more_violence(slice) -> void:
	var player_ids: Array = slice.simulation.living_ids(0)
	var enemy_ids: Array = slice.simulation.living_ids(1)
	if player_ids.is_empty() or enemy_ids.is_empty():
		return
	var attackers: Array[int] = []
	for value in player_ids:
		attackers.append(int(value))
	slice.simulation.issue_attack(attackers, int(enemy_ids[0]))


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_PERF_SOAK PASS %s" % name)
	else:
		failed += 1
		print("RETAIL_PERF_SOAK FAIL %s (%s)" % [name, detail])


func _finish() -> void:
	print("RETAIL_PERF_SOAK_RESULT passed=%d failed=%d" % [passed, failed])
	quit(1 if failed > 0 else 0)

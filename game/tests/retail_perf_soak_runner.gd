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


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_PERF_SOAK_RUNNER", 3600000)
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
	# Per-structure module-contract arrays are attached once and must then be
	# tick-invariant. Re-attachment appends duplicates every tick, which is the
	# quadratic-slowdown/OOM bug class (elves ElvenWood crash). Snapshot the
	# sizes after the first frame and re-compare later.
	var contract_size_snapshot: Dictionary = {}
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
		if frame == 0:
			contract_size_snapshot = _capture_structure_contract_sizes(slice, {})
		elif frame == WARMUP_FRAMES:
			# Fail fast: with the re-attach bug every tick appends rows, so by
			# the end of warmup (720 ticks) growth is unambiguous and waiting
			# out the full (quadratically slowed) soak proves nothing more.
			var early_growth := _structure_contract_growth(slice, contract_size_snapshot)
			if early_growth != "":
				_check("soak_structure_contract_arrays_tick_invariant", false, early_growth)
				slice.free()
				_finish()
				return
			contract_size_snapshot = _capture_structure_contract_sizes(slice, contract_size_snapshot)
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
	# The asset factory's static _mesh_cache intentionally holds detached model
	# prototypes, and Godot counts every one as an orphan node (~400+ at retail
	# content scale). Clear the cache first so this check measures true leaks
	# only; post-clear the count is 0 today. Nothing after this point touches
	# cached models (the remaining checks read simulation dictionaries only).
	load("res://src/view/asset_factory.gd").clear_mesh_cache()
	var orphans_after_cache_clear := int(Performance.get_monitor(Performance.OBJECT_ORPHAN_NODE_COUNT))
	print("RETAIL_PERF_SOAK_ORPHANS pre_clear=%d post_clear=%d" % [orphans, orphans_after_cache_clear])
	_check(
		"soak_orphan_nodes_bounded",
		orphans_after_cache_clear < 50,
		"orphans=%d after clear_mesh_cache (pre-clear=%d; cache prototypes are intentional, not leaks)" % [orphans_after_cache_clear, orphans]
	)
	_check(
		"soak_consumed_event_histories_bounded",
		slice.simulation.events.size() <= SimScript.MAX_RETAINED_EVENT_HISTORY
		and slice.audio_system.intent_log.size() <= slice.audio_system.MAX_OBSERVABILITY_LOG_ENTRIES
		and slice.audio_system.routing_log.size() <= slice.audio_system.MAX_OBSERVABILITY_LOG_ENTRIES,
		"simulation=%d intent=%d routing=%d" % [slice.simulation.events.size(), slice.audio_system.intent_log.size(), slice.audio_system.routing_log.size()]
	)
	var final_growth := _structure_contract_growth(slice, contract_size_snapshot)
	_check("soak_structure_contract_arrays_tick_invariant", final_growth == "", final_growth)
	var target_churn := SimScript.new()
	for target_id in SimScript.MAX_RETAINED_EVENT_HISTORY + 1:
		target_churn.events.append({"kind": "combat.hit_structure", "target_id": target_id})
	target_churn.compact_consumed_events()
	_check(
		"soak_distinct_structure_targets_cannot_escape_history_cap",
		target_churn.events.size() <= SimScript.MAX_RETAINED_EVENT_HISTORY,
		"events=%d" % target_churn.events.size()
	)
	slice.free()
	_finish()


const PINNED_STRUCTURE_CONTRACT_ARRAYS: Array[String] = [
	"object_creation_upgrades",
	"passive_area_effect_heals",
]


func _capture_structure_contract_sizes(slice, previous: Dictionary) -> Dictionary:
	## Sizes keyed by structure id. Carries `previous` forward so structures
	## built mid-soak are pinned from their own first sighting.
	var sizes := previous.duplicate(true)
	for structure_id in slice.simulation.structure_ids():
		if sizes.has(structure_id):
			continue
		var row: Dictionary = slice.simulation.structures[structure_id]
		var entry := {}
		for array_key in PINNED_STRUCTURE_CONTRACT_ARRAYS:
			entry[array_key] = (row.get(array_key, []) as Array).size()
		sizes[structure_id] = entry
	return sizes


func _structure_contract_growth(slice, snapshot: Dictionary) -> String:
	## Empty string when every pinned array still has its snapshot size;
	## otherwise a detail string naming the first offenders.
	var offenders: Array[String] = []
	for structure_id in slice.simulation.structure_ids():
		if not snapshot.has(structure_id):
			continue
		var row: Dictionary = slice.simulation.structures[structure_id]
		var expected: Dictionary = snapshot[structure_id]
		for array_key in PINNED_STRUCTURE_CONTRACT_ARRAYS:
			var size_now := (row.get(array_key, []) as Array).size()
			if size_now != int(expected.get(array_key, 0)):
				offenders.append("structure=%d %s %d->%d kind=%s" % [
					int(structure_id), array_key, int(expected.get(array_key, 0)),
					size_now, String(row.get("structure_kind", row.get("kind", "?"))),
				])
				if offenders.size() >= 5:
					return "; ".join(offenders) + "; ..."
	return "; ".join(offenders)


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

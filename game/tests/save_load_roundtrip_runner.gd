extends SceneTree
## Q84 save/load: a mid-match save reboots into the byte-identical
## authoritative state. Boots the REAL slice, plays 600 ticks, saves through
## the shipping save API, boots a second slice through the shipping restore
## path (GameState.retail_pending_restore_path), and compares state hashes.

const SCENE_PATH := "res://scenes/retail_vertical_slice.tscn"
const SaveGamesScript = preload("res://src/retail_slice/retail_save_games.gd")
const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")

var passed := 0
var failed := 0
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "SAVE_LOAD_ROUNDTRIP_RUNNER")
	create_timer(420.0, true, false, true).timeout.connect(func() -> void:
		push_error("SAVE_LOAD_ROUNDTRIP watchdog timeout")
		quit(1))
	call_deferred("_run")


func _run() -> void:
	var packed := load(SCENE_PATH) as PackedScene
	var first = packed.instantiate()
	root.add_child(first)
	if not await _wait_ready(first, "first boot"):
		return _finish()
	first.step_for_test(600)
	var save_receipt: Dictionary = first.save_match("roundtrip_fixture")
	_check(bool(save_receipt.get("ok", false)), "save_match writes the save (%s)" % String(save_receipt.get("reason", "ok")))
	if not bool(save_receipt.get("ok", false)):
		return _finish()
	var saved_tick := int(first.simulation.tick_index)
	# The slice runs its sim in real time, so the restored boot may be a tick
	# or two past the save by the time this runner looks. Record the first
	# sim's hash at every tick the comparison could land on, plus a far tick —
	# matching ANY of them at the matching tick proves byte-identical restore
	# AND deterministic post-restore advance.
	var reference_hashes: Dictionary = {saved_tick: String(first.simulation.state_hash())}
	for extra_tick in range(saved_tick + 1, saved_tick + 11):
		first.simulation.advance(1)
		reference_hashes[extra_tick] = String(first.simulation.state_hash())
	first.simulation.advance(saved_tick + 300 - int(first.simulation.tick_index))
	var far_tick := int(first.simulation.tick_index)
	var far_hash := String(first.simulation.state_hash())
	var header: Dictionary = SaveGamesScript.read_header(String(save_receipt["path"]))
	_check(bool(header.get("loadable", false)), "save header is loadable")
	_check(int(header.get("tick", -1)) == saved_tick, "header tick matches the sim")
	_check(not (header.get("gameState", {}) as Dictionary).is_empty(), "header carries the launch keys verbatim")
	first.queue_free()
	await process_frame
	await process_frame

	var state = root.get_node_or_null("/root/GameState")
	if state == null:
		_check(false, "GameState autoload is present")
		return _finish()
	for key in header.get("gameState", {}) as Dictionary:
		state.set(String(key), (header["gameState"] as Dictionary)[key])
	state.set("retail_mp_mode", "")
	state.set("retail_pending_restore_path", String(save_receipt["path"]))

	var second = packed.instantiate()
	root.add_child(second)
	if not await _wait_ready(second, "restore boot"):
		return _finish()
	# _consume_pending_restore runs inside the readiness check; give it a frame.
	await process_frame
	var restored_tick := int(second.simulation.tick_index)
	_check(
		restored_tick >= saved_tick and reference_hashes.has(restored_tick),
		"restored sim resumes at/near the saved tick (%d, saved %d)" % [restored_tick, saved_tick]
	)
	if reference_hashes.has(restored_tick):
		_check(
			String(second.simulation.state_hash()) == String(reference_hashes[restored_tick]),
			"restored state hash is byte-identical to the original sim at tick %d" % restored_tick
		)
	second.simulation.advance(far_tick - restored_tick)
	_check(
		String(second.simulation.state_hash()) == far_hash,
		"restored match advances deterministically to tick %d" % far_tick
	)
	_check(String(state.get("retail_pending_restore_path")) == "", "the pending-restore key is consumed once")
	second.queue_free()
	await process_frame
	_finish()


func _wait_ready(slice, label: String) -> bool:
	var started := Time.get_ticks_msec()
	while not bool(slice.ready_ok):
		await process_frame
		if String(slice.failure_reason) != "":
			_check(false, "%s readiness failed: %s" % [label, String(slice.failure_reason)])
			return false
		if Time.get_ticks_msec() - started > 120000:
			_check(false, "%s readiness timed out" % label)
			return false
	return true


func _check(condition: bool, label: String) -> void:
	if condition:
		passed += 1
		print("SAVE_LOAD_ROUNDTRIP PASS %s" % label)
	else:
		failed += 1
		push_error("SAVE_LOAD_ROUNDTRIP FAIL %s" % label)


func _finish() -> void:
	print("SAVE_LOAD_ROUNDTRIP_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)

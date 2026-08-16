extends SceneTree
## FXEvent fires when the clip/frame clock crosses Frame:N.
##
## RANDOMSTART seek past a cue does not fire unless FireWhenSkipped is
## authored. Same-state re-ticks do not re-fire.

const Watchdog := preload("res://tests/runner_watchdog.gd")
const FXEventScript := preload("res://src/retail_slice/fx_event.gd")
const EXPECTED_CHECKS := 8

var passed := 0
var failed := 0
var _watchdog := Watchdog.new()


func _initialize() -> void:
	_watchdog.start(self, "FX_EVENT_RUNTIME", 0, 0, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	await process_frame
	var rows := [
		_typed(["MOVING"], 12, "FX_SplatDust", false),
		_typed(["MOVING"], 5, "FX_TrollRightFootStep", true),
	]
	var cross: Dictionary = FXEventScript.select(rows, ["MOVING"], {
		"frame": 12.4, "previousFrame": 11.2, "lengthFrames": 30.0, "backwards": false,
	})
	_check(
		"FXEvent fires FX_SplatDust when the playhead crosses frame 12",
		int(cross.get("applied", 0)) == 1 and _has_fx(cross, "FX_SplatDust")
	)
	var retick: Dictionary = FXEventScript.select(rows, ["MOVING"], {
		"frame": 13.0, "previousFrame": 12.4, "lengthFrames": 30.0, "backwards": false,
	})
	_check(
		"same-state re-tick does not re-fire FXEvent",
		int(retick.get("applied", 0)) == 0 and not _has_fx(retick, "FX_SplatDust")
	)
	var skipped: Dictionary = FXEventScript.select(rows, ["MOVING"], {
		"frame": 20.0, "previousFrame": -1.0, "lengthFrames": 30.0, "backwards": false,
	})
	_check(
		"RANDOMSTART seek past frame 12 does not fire the default cue",
		not _has_fx(skipped, "FX_SplatDust")
		and String(skipped.get("skippedCuePolicy", "")) == "ignore"
		and bool(skipped.get("primed", false))
	)
	_check(
		"FireWhenSkipped still fires the already-passed footstep",
		_has_fx(skipped, "FX_TrollRightFootStep")
	)
	var idle: Dictionary = FXEventScript.select(rows, [], {
		"frame": 12.4, "previousFrame": 11.2, "lengthFrames": 30.0, "backwards": false,
	})
	_check("FXEvent stays on the selected AnimationState conditions", not _has_fx(idle, "FX_SplatDust"))
	var battalion_script: GDScript = load("res://src/retail_slice/retail_battalion.gd") as GDScript
	var battalion = battalion_script.new()
	battalion.bind_fx_event_contracts({"moduleContracts": rows})
	_check("battalion binds typed FXEvent moduleContracts", battalion.fx_event_contracts.size() == 2)
	battalion.last_animation_state_receipt = {"conditions": ["MOVING"]}
	var receipt: Dictionary = battalion._sync_member_fx_events(0, {"conditions": ["MOVING"]}, {
		"frame": 12.0, "previousFrame": 11.0, "lengthFrames": 30.0, "backwards": false,
	})
	_check(
		"battalion clock tick fires the authored FXEvent",
		_has_fx(receipt, "FX_SplatDust") and String(receipt.get("source", "")) == "typed-fx-event"
	)
	var again: Dictionary = battalion._sync_member_fx_events(0, {"conditions": ["MOVING"]}, {
		"frame": 12.6, "previousFrame": 12.0, "lengthFrames": 30.0, "backwards": false,
	})
	_check("battalion does not re-fire FXEvent on the next tick", not _has_fx(again, "FX_SplatDust"))
	battalion.free()
	_finish()


func _typed(conditions: Array, frame: int, fx_list: String, fire_when_skipped: bool) -> Dictionary:
	return {
		"module": "FXEvent",
		"runtimeStatus": "executable",
		"extraction": "typed",
		"fields": {
			"stateKind": "AnimationState",
			"conditions": {"value": conditions},
			"frame": {"value": frame},
			"fxList": {"value": fx_list},
			"FireWhenSkipped": {"value": fire_when_skipped},
			"skippedCuePolicy": "fire-when-skipped" if fire_when_skipped else "ignore",
		},
	}


func _has_fx(result: Dictionary, fx_id: String) -> bool:
	return (result.get("fxLists", []) as Array).has(fx_id)


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error(label)


func _finish() -> void:
	print("FX_EVENT_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		push_error("check-count mismatch: expected=%d actual=%d" % [EXPECTED_CHECKS, passed + failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)

extends SceneTree

const FxTiming = preload("res://src/retail_slice/fx_timing.gd")

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var timing := FxTiming.new()
	_check("typed_timing_is_accepted", timing.configure({
		"initialDelayFrames": {"minimum": 3.0, "maximum": 3.0},
		"burstDelayFrames": {"minimum": 2.0, "maximum": 2.0},
	}, 7))
	_check("initial_delay_suppresses_early_burst", timing.advance_frames(2.0) == 0)
	_check("initial_delay_emits_exactly_on_authored_frame", timing.advance_frames(1.0) == 1)
	_check("burst_delay_suppresses_early_repeat", timing.advance_frames(1.0) == 0)
	_check("burst_delay_emits_exactly_on_authored_cadence", timing.advance_frames(1.0) == 1)
	_check("BurstDelay_and_InitialDelay_are_runtime_fields", timing.authored_fields() == ["BurstDelay", "InitialDelay"])
	print("FX_TIMING_DELAYS_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		printerr("FX_TIMING_DELAYS FAIL: %s" % label)

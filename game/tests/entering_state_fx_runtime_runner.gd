extends SceneTree
## EnteringStateFX fires the authored FXList when the AnimationState selector
## enters a new condition set. Same-state ticks do not re-fire. FXEvent frame
## cues stay deferred — they need the clip/frame clock.

const Watchdog := preload("res://tests/runner_watchdog.gd")
const EnteringStateFXScript := preload("res://src/retail_slice/entering_state_fx.gd")
const EXPECTED_CHECKS := 8

var passed := 0
var failed := 0
var _watchdog := Watchdog.new()


func _initialize() -> void:
	_watchdog.start(self, "ENTERING_STATE_FX_RUNTIME", 0, 0, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	await process_frame
	var rows := [
		_typed("AnimationState", ["DAMAGED"], "FX_BuildingDamaged"),
		_typed("AnimationState", ["REALLYDAMAGED"], "FX_BuildingReallyDamaged"),
		_typed("AnimationState", ["COLLAPSING"], "FX_StructureMediumCollapse"),
	]
	var enter_damaged: Dictionary = EnteringStateFXScript.select(rows, ["DAMAGED"], [])
	_check(
		"EnteringStateFX fires FX_BuildingDamaged on DAMAGED entry",
		bool(enter_damaged.get("entered", false))
		and int(enter_damaged.get("applied", 0)) == 1
		and _has_fx(enter_damaged, "FX_BuildingDamaged")
	)
	var stay_damaged: Dictionary = EnteringStateFXScript.select(rows, ["DAMAGED"], ["DAMAGED"])
	_check(
		"same selected state does not re-fire EnteringStateFX",
		not bool(stay_damaged.get("entered", true))
		and int(stay_damaged.get("applied", -1)) == 0
		and (stay_damaged.get("fxLists", ["x"]) as Array).is_empty()
	)
	var enter_really: Dictionary = EnteringStateFXScript.select(
		rows, ["REALLYDAMAGED"], ["DAMAGED"]
	)
	_check(
		"REALLYDAMAGED entry replaces the damaged list",
		bool(enter_really.get("entered", false))
		and _has_fx(enter_really, "FX_BuildingReallyDamaged")
		and not _has_fx(enter_really, "FX_BuildingDamaged")
	)
	var leave_idle: Dictionary = EnteringStateFXScript.select(rows, [], ["REALLYDAMAGED"])
	_check(
		"leaving to idle enters without an authored idle FXList",
		bool(leave_idle.get("entered", false))
		and int(leave_idle.get("applied", -1)) == 0
	)
	var moving: Dictionary = EnteringStateFXScript.select(rows, ["MOVING"], [])
	_check(
		"MOVING does not fire a DAMAGED EnteringStateFX",
		bool(moving.get("entered", false)) and not _has_fx(moving, "FX_BuildingDamaged")
	)
	var battalion_script: GDScript = load("res://src/retail_slice/retail_battalion.gd") as GDScript
	var battalion = battalion_script.new()
	battalion.bind_entering_state_fx_contracts({"moduleContracts": rows})
	_check(
		"battalion binds typed EnteringStateFX moduleContracts",
		battalion.entering_state_fx_contracts.size() == 3
	)
	battalion._sync_entering_state_fx({"conditions": ["COLLAPSING"]})
	var first: Dictionary = battalion.last_entering_state_fx_receipt
	_check(
		"first selected state fires even when previous conditions are empty",
		bool(first.get("entered", false)) and _has_fx(first, "FX_StructureMediumCollapse")
	)
	battalion._sync_entering_state_fx({"conditions": ["COLLAPSING"]})
	var second: Dictionary = battalion.last_entering_state_fx_receipt
	_check(
		"battalion does not re-fire EnteringStateFX on the same selected state",
		not bool(second.get("entered", true)) and int(second.get("applied", -1)) == 0
	)
	battalion.free()
	_finish()


func _typed(state_kind: String, conditions: Array, fx_list: String) -> Dictionary:
	return {
		"module": "EnteringStateFX",
		"runtimeStatus": "executable",
		"extraction": "typed",
		"fields": {
			"stateKind": state_kind,
			"conditions": {"value": conditions},
			"fxList": {"value": fx_list},
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
	print("ENTERING_STATE_FX_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		push_error("check-count mismatch: expected=%d actual=%d" % [EXPECTED_CHECKS, passed + failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)

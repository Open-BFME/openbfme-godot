extends SceneTree
## Authored AnimationState best-match selection for the battalion presenter.
##
## Most-specific condition subset wins. IdleAnimationState is specificity 0.
## AnimationBlendTime conversion is frames/30. AnimationPriority stays deferred.

const Watchdog := preload("res://tests/runner_watchdog.gd")
const AnimationStateSelectScript := preload("res://src/retail_slice/animation_state_select.gd")
const EXPECTED_CHECKS := 12

var passed := 0
var failed := 0
var _watchdog := Watchdog.new()


func _initialize() -> void:
	_watchdog.start(self, "ANIMATION_STATE_SELECT_RUNTIME", 0, 0, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	await process_frame
	var contracts := [
		_typed_row("IdleAnimationState", [], "SKL.IDLE", "LOOP", 15, ["RANDOMSTART"]),
		_typed_row("AnimationState", ["MOVING"], "SKL.RUNA", "LOOP", 10, []),
		_typed_row("AnimationState", ["MOVING", "USER_1"], "SKL.RUN_UPGRADE", "LOOP", 8, []),
		_typed_row("AnimationState", ["DYING"], "SKL.DTHA", "ONCE", 0, []),
		_typed_row("AnimationState", ["MOVING"], "SKL.RUNB", "LOOP", 10, [], 8),
		_typed_row("AnimationState", ["DYING", "DEATH_2"], "SKL.DTHB", "ONCE_BACKWARDS", 0, ["MAINTAIN_FRAME_ACROSS_STATES"]),
		_typed_row("AnimationState", ["PACKING_UP"], "SKL.PACK", "MANUAL", 0, []),
	]
	var idle: Dictionary = AnimationStateSelectScript.select(contracts, [])
	_check("idle wins when no model conditions are set", String(idle.get("clip", "")) == "SKL.IDLE" and int(idle.get("specificity", -1)) == 0)
	var moving: Dictionary = AnimationStateSelectScript.select(contracts, ["MOVING"])
	_check("AnimationState MOVING beats idle", String(moving.get("clip", "")) == "SKL.RUNB" and int(moving.get("specificity", -1)) == 1)
	var upgraded: Dictionary = AnimationStateSelectScript.select(contracts, ["MOVING", "USER_1"])
	_check("most-specific MOVING USER_1 subset wins", String(upgraded.get("clip", "")) == "SKL.RUN_UPGRADE" and int(upgraded.get("specificity", -1)) == 2)
	var dying: Dictionary = AnimationStateSelectScript.select(contracts, ["DYING"])
	_check("ONCE mode is retained on the death state", String(dying.get("mode", "")) == "ONCE" and String(dying.get("clip", "")) == "SKL.DTHA")
	_check("RANDOMSTART is visible on the idle row", bool(idle.get("randomStart", false)))
	_check("AnimationBlendTime 15 frames is 0.5s at 30 FPS", is_equal_approx(float(AnimationStateSelectScript.blend_seconds(15)), 0.5))
	_check("AnimationBlendTime 0 is an instant cut", is_equal_approx(float(AnimationStateSelectScript.blend_seconds(0)), 0.0))
	_check("equal-specificity AnimationPriority prefers the higher row", String(moving.get("clip", "")) == "SKL.RUNB" and int(moving.get("priority", -1)) == 8)
	var backwards: Dictionary = AnimationStateSelectScript.select(contracts, ["DYING", "DEATH_2"])
	_check("ONCE_BACKWARDS and MAINTAIN_FRAME are executable", bool(backwards.get("playBackwards", false)) and bool(backwards.get("maintainFrame", false)))
	var manual: Dictionary = AnimationStateSelectScript.select(contracts, ["PACKING_UP"])
	_check("MANUAL stays a deferred receipt", bool(manual.get("manualDeferred", false)) and String(manual.get("clip", "")) == "SKL.PACK")
	var battalion_script: GDScript = load("res://src/retail_slice/retail_battalion.gd") as GDScript
	_check("battalion consumer loads", battalion_script != null and battalion_script.can_instantiate())
	if battalion_script != null and battalion_script.can_instantiate():
		var battalion = battalion_script.new()
		battalion.bind_animation_state_contracts({
			"visual": {
				"authoredAnimationStates": [
					{"identifier": "SKL.RUNA", "conditions": ["MOVING"]},
					{"identifier": "SKL.RUN_UPGRADE", "conditions": ["MOVING", "USER_1"]},
				],
			},
		})
		battalion.model_condition_flags = ["USER_1"]
		battalion.clip_sets["run"] = ["SKL.RUNA"]
		battalion.clip_map["run"] = "SKL.RUNA"
		battalion.member_count = 1
		battalion.member_health_ratios[0] = 1.0
		battalion._play_member_state(0, "run", -1, true)
		_check("live battalion uses the more specific authored clip", String(battalion.member_current_clips.get(0, "")) == "SKL.RUN_UPGRADE")
		battalion.free()
	else:
		_check("live battalion uses the more specific authored clip", false)
	_finish()


func _typed_row(state_kind: String, conditions: Array, clip: String, mode: String, blend: int, flags: Array, priority: int = 0) -> Dictionary:
	return {
		"module": "AnimationState",
		"runtimeStatus": "executable",
		"extraction": "typed",
		"fields": {
			"stateKind": state_kind,
			"conditions": {"value": conditions},
			"Flags": {"value": flags},
			"animations": [{
				"animationName": clip,
				"mode": mode,
				"blendTime": blend,
				"priority": priority,
			}],
		},
	}


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error(label)


func _finish() -> void:
	print("ANIMATION_STATE_SELECT_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		push_error("check-count mismatch: expected=%d actual=%d" % [EXPECTED_CHECKS, passed + failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)

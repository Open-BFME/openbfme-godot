extends SceneTree
## Live battalion path for W3DScriptedModelDraw BeginScript.
##
## The helper `apply_member_drawable_scripts` is not enough: gameplay changes
## clips through `set_selected` / `_play_member_state`. This runner proves that
## path executes authored scripts, honors Prev, and keeps the compiled
## idle→selected map only when no SetTransitionAnimState script is present.

const Watchdog := preload("res://tests/runner_watchdog.gd")
const EXPECTED_CHECKS := 8

var passed := 0
var failed := 0
var _watchdog := Watchdog.new()


func _initialize() -> void:
	_watchdog.start(self, "DRAWABLE_SCRIPT_LIVE_PATH", 0, 0, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	await process_frame
	await process_frame
	var battalion_script: GDScript = load("res://src/retail_slice/retail_battalion.gd") as GDScript
	_check("battalion script loads", battalion_script != null and battalion_script.can_instantiate())
	if battalion_script == null or not battalion_script.can_instantiate():
		_finish()
		return

	var visual := Node3D.new()
	var bow := Node3D.new()
	bow.name = "BOW"
	visual.add_child(bow)

	var live = _make_battalion(battalion_script, visual, [{
		"targetObject": "",
		"conditions": ["SELECTED"],
		"actions": [
			{"operation": "hide-sub-object", "arguments": ["BOW"], "supported": true, "raw": "CurDrawableHideSubObject(\"BOW\")"},
			{"operation": "set-transition-animation-state", "arguments": ["TRANS_IdleToSelected"], "supported": true, "raw": "if Prev == \"STATE_Idle\" then CurDrawableSetTransitionAnimState(\"TRANS_IdleToSelected\") end"},
		],
	}])
	bow.visible = true
	live.member_current_authored_state_labels[0] = ["STATE_Idle"]
	live.set_selected(true)
	_check("live set_selected executes set-transition-animation-state", String((live.last_drawable_script_receipt as Dictionary).get("transition_anim_state", "")) == "TRANS_IdleToSelected")
	_check("live set_selected applies hide-sub-object", not bow.visible)
	_check("live Prev match plays the authored transition", String(live.member_action_states.get(0, "")) == "selectionTransition")
	live.free()

	var mismatch = _make_battalion(battalion_script, visual, [{
		"targetObject": "",
		"conditions": ["SELECTED"],
		"actions": [
			{"operation": "set-transition-animation-state", "arguments": ["TRANS_IdleToSelected"], "supported": true, "raw": "if Prev == \"STATE_Idle\" then CurDrawableSetTransitionAnimState(\"TRANS_IdleToSelected\") end"},
		],
	}])
	mismatch.member_current_authored_state_labels[0] = ["STATE_Attacking"]
	mismatch.set_selected(true)
	_check("live Prev mismatch leaves transition_anim_state empty", String((mismatch.last_drawable_script_receipt as Dictionary).get("transition_anim_state", "")) == "")
	_check("live Prev mismatch does not use the hardcoded ATNA map", String(mismatch.member_action_states.get(0, "")) == "selected")
	mismatch.free()

	var fallback = _make_battalion(battalion_script, visual, [])
	fallback.set_selected(true)
	_check("script-absent select keeps the compiled idle-to-selected map", String(fallback.member_action_states.get(0, "")) == "selectionTransition")
	_check("script-absent path records an empty live receipt", (fallback.last_drawable_script_receipt as Dictionary).is_empty() or String((fallback.last_drawable_script_receipt as Dictionary).get("transition_anim_state", "")) == "")
	fallback.free()
	visual.free()
	_finish()


func _make_battalion(battalion_script: GDScript, visual: Node3D, scripts: Array):
	var battalion = battalion_script.new()
	battalion.member_count = 1
	battalion.health_ratio = 1.0
	battalion.current_state = "idle"
	battalion.selected = false
	battalion.clip_map = {
		"idle": "GUManMocap_IDLA",
		"selected": "GUManMocap_ATNB",
		"selectionTransition": "GUManMocap_ATNA",
	}
	battalion.clip_sets = {
		"idle": ["GUManMocap_IDLA"],
		"selected": ["GUManMocap_ATNB"],
		"selectionTransition": ["GUManMocap_ATNA"],
	}
	battalion.member_visuals[0] = visual
	battalion.member_health_ratios[0] = 1.0
	battalion.member_action_states[0] = "idle"
	battalion.member_animation_players[0] = []
	battalion._drawable_scripts = scripts
	return battalion


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error(label)


func _finish() -> void:
	print("DRAWABLE_SCRIPT_LIVE_PATH_RESULT passed=%d failed=%d" % [passed, failed])
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		push_error("check-count mismatch: expected=%d actual=%d" % [EXPECTED_CHECKS, passed + failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)

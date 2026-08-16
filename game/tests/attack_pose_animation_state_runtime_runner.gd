extends SceneTree
## AttackPose: per-member weapon-cycle tokens union into the condition set
## AnimationStateSelect.select sees, so a windup archer and a firing archer
## in one battalion pose differently. Authored PREATTACK_A / FIRING_A beat
## the semantic clip map (b56c09c specificity); unmatched falls back.

const Watchdog := preload("res://tests/runner_watchdog.gd")
const EXPECTED_CHECKS := 8

var passed := 0
var failed := 0
var _watchdog := Watchdog.new()


func _initialize() -> void:
	_watchdog.start(self, "ATTACK_POSE_ANIMATION_STATE_RUNTIME", 0, 0, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	await process_frame
	var battalion_script: GDScript = load("res://src/retail_slice/retail_battalion.gd") as GDScript
	var battalion = battalion_script.new()
	battalion.bind_animation_state_contracts({
		"moduleContracts": [
			_typed([], "GUArcher_IDLE"),
			_typed(["PREATTACK_A"], "GUArcher_ATKF1"),
			_typed(["FIRING_A"], "GUArcher_ATKF2"),
		],
	})
	battalion.clip_sets["attack"] = ["GUArcher_ATKA"]
	battalion.clip_map["attack"] = "GUArcher_ATKA"
	battalion.member_count = 2
	battalion.member_health_ratios[0] = 1.0
	battalion.member_health_ratios[1] = 1.0

	battalion.sync_member_states(
		[100, 100], 100, [0, 0], "idle", [], [], [], [], 0,
		[
			["PREATTACK_A", "FIRING_OR_PREATTACK_A"],
			["FIRING_A", "FIRING_OR_PREATTACK_A", "FIRING_OR_RELOADING_A"],
		]
	)
	var windup: Array = battalion._drawable_conditions_for_state("attack", 0)
	var firing: Array = battalion._drawable_conditions_for_state("attack", 1)
	_check(
		"per-member tokens union into AttackPose conditions",
		windup.has("ATTACKING") and windup.has("PREATTACK_A") and not windup.has("FIRING_A")
		and firing.has("ATTACKING") and firing.has("FIRING_A") and not firing.has("PREATTACK_A")
	)

	battalion._play_member_state(0, "attack", 1, true)
	_check(
		"PREATTACK_A authored state wins during windup",
		String(battalion.member_current_clips.get(0, "")) == "GUArcher_ATKF1"
		and (battalion.last_animation_state_receipt.get("conditions", []) as Array).has("PREATTACK_A")
	)
	battalion._play_member_state(1, "attack", 1, true)
	_check(
		"FIRING_A authored state wins during fire",
		String(battalion.member_current_clips.get(1, "")) == "GUArcher_ATKF2"
		and (battalion.last_animation_state_receipt.get("conditions", []) as Array).has("FIRING_A")
	)
	_check(
		"two members in one battalion keep distinct AttackPose clips",
		String(battalion.member_current_clips.get(0, "")) == "GUArcher_ATKF1"
		and String(battalion.member_current_clips.get(1, "")) == "GUArcher_ATKF2"
	)

	battalion.sync_member_states([100, 100], 100, [0, 0], "idle", [], [], [], [], 0, [[], []])
	battalion._play_member_state(0, "attack", 2, true)
	_check(
		"unmatched AttackPose falls back to the semantic clip map",
		String(battalion.member_current_clips.get(0, "")) == "GUArcher_ATKA"
		and int(battalion.last_animation_state_receipt.get("specificity", -1)) <= 0
	)

	battalion.set_weapon_condition_deferred_reasons([
		"swapping-to-weaponset-not-modelled",
		"weapon-slot-d-absent-from-retail-corpus",
	])
	_check(
		"fail-loud companion reasons stay on the battalion",
		battalion.last_weapon_condition_deferred_reasons.has("weapon-slot-d-absent-from-retail-corpus")
	)

	var idle_conditions: Array = battalion._drawable_conditions_for_state("run")
	_check(
		"omitting member_index does not invent weapon tokens",
		idle_conditions.has("MOVING") and not idle_conditions.has("PREATTACK_A")
	)
	_check("battalion binds typed AttackPose animation states", battalion.animation_state_contracts.size() == 3)
	battalion.free()
	_finish()


func _typed(conditions: Array, clip: String) -> Dictionary:
	return {
		"module": "AnimationState",
		"runtimeStatus": "executable",
		"extraction": "typed",
		"fields": {
			"stateKind": "IdleAnimationState" if conditions.is_empty() else "AnimationState",
			"conditions": {"value": conditions},
			"animations": [{
				"animationName": clip,
				"mode": "ONCE" if not conditions.is_empty() else "LOOP",
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
	print("ATTACK_POSE_ANIMATION_STATE_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		push_error("check-count mismatch: expected=%d actual=%d" % [EXPECTED_CHECKS, passed + failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)

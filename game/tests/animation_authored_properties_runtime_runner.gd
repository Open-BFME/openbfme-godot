extends SceneTree
## Bounded playable-unit consumption of AnimationBlendTime,
## AnimationPriority, and StateName. Raw numeric values are retained while
## unproved unit-animation selection/conversion semantics remain explicit.

const BATTALION_SCRIPT_PATH := "res://src/retail_slice/retail_battalion.gd"
const RunnerWatchdogScript = preload("res://tests/runner_watchdog.gd")
const EXPECTED_TESTS := 3
const EXPECTED_CHECKS := 32

var _watchdog := RunnerWatchdogScript.new()
var passed := 0
var failed := 0
var completed: Array[String] = []


func _initialize() -> void:
	_watchdog.start(self, "ANIMATION_AUTHORED_PROPERTIES_RUNTIME")
	call_deferred("_run")


func _run() -> void:
	var capability := _projection_test()
	_runtime_receipt_test(capability)
	_state_label_transition_test(capability)
	if completed.size() != EXPECTED_TESTS:
		_fail("liveness tests=%d expected=%d" % [completed.size(), EXPECTED_TESTS])
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		_fail("liveness checks=%d expected=%d" % [ran, EXPECTED_CHECKS])
	print("ANIMATION_AUTHORED_PROPERTIES_RUNTIME_RESULT passed=%d failed=%d tests=%d checks=%d" % [passed, failed, completed.size(), ran])
	quit(0 if failed == 0 else 1)


func _projection_test() -> Dictionary:
	var db = root.get_node_or_null("ContentDB")
	_check(db != null, "content db exists")
	if db == null:
		completed.append("projection")
		return {}
	var idle_provenance := _provenance(10)
	var selected_provenance := _provenance(30)
	var document := {
		"objectId": "FixtureHorde", "_pack_root": "fixture", "_source": "fixture.json",
		"registration": {
			"composition": {"primaryMemberObjectId": "FixtureMember"},
			"visual": {
				"components": [{"default": true, "output": "assets/models/fixture.glb"}],
				"coreAnimations": {
					"idle": [_binding("FIXTURE_IDLE", [], 15, 0, idle_provenance)],
					"move": [_binding("FIXTURE_RUN", ["MOVING"], null, 7, _provenance(20))],
					"attack": [_binding("FIXTURE_FIRE", ["FIRING_OR_RELOADING_A"], 9, 3, _provenance(25))],
					"death": [_binding("FIXTURE_DIE", ["DYING"], null, null, _provenance(26))],
				},
				"authoredAnimationStates": [
					_binding("FIXTURE_SELECTED", ["SELECTED"], 22, 0, selected_provenance),
				],
				"authoredStateLabels": [
					_label("STATE_Idle", "FIXTURE_IDLE", idle_provenance),
					_label("STATE_Selected", "FIXTURE_SELECTED", selected_provenance),
				],
			},
		},
	}
	var projection: Dictionary = db._playable_unit_projection(document)
	_check(not projection.is_empty(), "projection succeeds")
	var capability := projection.get("capability", {}) as Dictionary
	var states := capability.get("states", {}) as Dictionary
	var idle := states.get("idle", {}) as Dictionary
	var idle_properties := (idle.get("clipProperties", {}) as Dictionary).get("FIXTURE_IDLE", {}) as Dictionary
	_check(int(idle_properties.get("animationBlendTimeRaw", -1)) == 15, "core blend remains raw")
	_check(int(idle_properties.get("animationPriority", -1)) == 0, "zero priority is retained")
	_check((idle_properties.get("authoredProperties", []) as Array).size() == 2, "both property provenance rows retained")
	var fire := states.get("attackRangedFire", {}) as Dictionary
	var fire_properties := (fire.get("clipProperties", {}) as Dictionary).get("FIXTURE_FIRE", {}) as Dictionary
	_check(int(fire_properties.get("animationBlendTimeRaw", -1)) == 9, "derived fire route retains blend")
	_check(int(fire_properties.get("animationPriority", -1)) == 3, "derived fire route retains priority")
	var selected := states.get("selected", {}) as Dictionary
	var selected_properties := (selected.get("clipProperties", {}) as Dictionary).get("FIXTURE_SELECTED", {}) as Dictionary
	_check(int(selected_properties.get("animationBlendTimeRaw", -1)) == 22, "selected route retains blend")
	_check(int(selected_properties.get("animationPriority", -1)) == 0, "selected route retains zero priority")
	_check((capability.get("authoredStateLabels", []) as Array).size() == 2, "state labels remain separate from clips")
	_check(String(capability.get("authoredStateLabelRuntimeSupport", "")) == "linked-animation-current-and-previous-label-predicates", "projection names bounded state-label support")
	# Snapshot isolation: projection must not alias the pack document.
	(capability.get("authoredStateLabels", []) as Array)[0]["StateName"] = "MUTATED"
	_check(String(((document.registration.visual.authoredStateLabels as Array)[0] as Dictionary).get("StateName")) == "STATE_Idle", "projected labels are defensive snapshots")
	capability["authoredStateLabels"] = (document.registration.visual.authoredStateLabels as Array).duplicate(true)
	completed.append("projection")
	return capability


func _runtime_receipt_test(capability: Dictionary) -> void:
	var battalion_script: GDScript = load(BATTALION_SCRIPT_PATH)
	_check(battalion_script != null and battalion_script.can_instantiate(), "battalion script compiles for property receipts")
	if battalion_script == null or not battalion_script.can_instantiate():
		completed.append("receipts")
		return
	var battalion = battalion_script.new()
	battalion._build_clip_map(capability)
	var idle_receipt: Dictionary = battalion.authored_animation_property_receipt("idle", "FIXTURE_IDLE")
	_check(int(idle_receipt.get("animationBlendTimeRaw", -1)) == 15, "runtime consumes raw blend")
	_check(idle_receipt.get("animationBlendApplied") == false, "blend is not assigned invented timing")
	_check(String(idle_receipt.get("animationBlendRuntimeSupport", "")).contains("conversion-unproven"), "blend defer reason is explicit")
	_check(int(idle_receipt.get("animationPriority", -1)) == 0, "runtime consumes zero priority without dropping it")
	_check(idle_receipt.get("animationPriorityApplied") == false, "priority is not assigned invented selection semantics")
	_check(String(idle_receipt.get("animationPriorityRuntimeSupport", "")).contains("zero-semantics-unproven"), "priority defer reason is explicit")
	var fire_receipt: Dictionary = battalion.authored_animation_property_receipt("attack_ranged_fire", "FIXTURE_FIRE")
	_check(int(fire_receipt.get("animationBlendTimeRaw", -1)) == 9 and int(fire_receipt.get("animationPriority", -1)) == 3, "derived runtime route consumes properties")
	_check(battalion.authored_animation_property_receipt("idle", "MISSING").is_empty(), "unknown clip fails closed")
	idle_receipt["animationBlendTimeRaw"] = 999
	_check(int(battalion.authored_animation_property_receipt("idle", "FIXTURE_IDLE").get("animationBlendTimeRaw", -1)) == 15, "receipt is a defensive snapshot")
	battalion.free()
	completed.append("receipts")


func _state_label_transition_test(capability: Dictionary) -> void:
	var battalion_script: GDScript = load(BATTALION_SCRIPT_PATH)
	_check(battalion_script != null and battalion_script.can_instantiate(), "battalion script compiles for state labels")
	if battalion_script == null or not battalion_script.can_instantiate():
		completed.append("labels")
		return
	var battalion = battalion_script.new()
	battalion.member_count = 1
	battalion.member_health_ratios[0] = 1.0
	battalion._build_clip_map(capability)
	battalion._play_member_state(0, "idle", -1, true)
	_check(battalion.current_authored_state_labels(0) == ["STATE_Idle"], "idle clip activates exact authored label")
	_check(battalion.previous_authored_state_labels(0).is_empty(), "first entry has empty previous labels")
	_check(battalion.member_has_authored_state_label(0, "STATE_Idle"), "current label predicate is addressable")
	battalion._play_member_state(0, "selected", -1, true)
	_check(battalion.current_authored_state_labels(0) == ["STATE_Selected"], "selected clip activates exact authored label")
	_check(battalion.previous_authored_state_labels(0) == ["STATE_Idle"], "transition retains previous authored labels")
	_check(battalion.member_has_authored_state_label(0, "STATE_Idle", true), "previous label predicate is addressable")
	var copy: Array = battalion.current_authored_state_labels(0)
	copy.append("MUTATED")
	_check(battalion.current_authored_state_labels(0) == ["STATE_Selected"], "label query is a defensive snapshot")
	battalion._sync_member_authored_state_labels(0, "UNLINKED_CLIP")
	_check(battalion.current_authored_state_labels(0).is_empty(), "unlinked clip does not guess a state label")
	_check(battalion.previous_authored_state_labels(0) == ["STATE_Selected"], "unlinked transition still preserves exact prior labels")
	battalion.free()
	completed.append("labels")


func _binding(identifier: String, conditions: Array, blend: Variant, priority: Variant, provenance: Dictionary) -> Dictionary:
	var row := {"identifier": identifier, "conditions": conditions, "runtimeSupport": "generic-core"}
	var receipts: Array = []
	if blend != null:
		row["AnimationBlendTime"] = blend
		receipts.append({"key": "AnimationBlendTime", "value": blend, "provenance": provenance.duplicate(true)})
	if priority != null:
		row["AnimationPriority"] = priority
		receipts.append({"key": "AnimationPriority", "value": priority, "provenance": provenance.duplicate(true)})
	if not receipts.is_empty():
		row["authoredProperties"] = receipts
	return row


func _label(label: String, identifier: String, provenance: Dictionary) -> Dictionary:
	return {"StateName": label, "runtimeSupport": "packaged-unimplemented", "provenance": provenance.duplicate(true), "linkedAnimations": [{"identifier": identifier, "conditions": []}]}


func _provenance(line: int) -> Dictionary:
	return {"definingObject": "FixtureMember", "virtualPath": "fixture.ini", "line": line, "inheritanceDistance": 0}


func _check(condition: bool, message: String) -> void:
	if condition:
		passed += 1
	else:
		_fail(message)


func _fail(message: String) -> void:
	failed += 1
	push_error("ANIMATION_AUTHORED_PROPERTIES_RUNTIME_FAIL " + message)

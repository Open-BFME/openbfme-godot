extends SceneTree
## Legal-safe focused gate for exact Fords animated-prop runtime semantics.

const TYPE_COUNTS := {
	"Bear": 1,
	"CaptureFlag": 2,
	"Duck": 2,
	"Egret": 2,
	"ElkFemale": 1,
	"ElkMale": 1,
	"Fish": 13,
	"Rabbit": 1,
	"Raccoon": 2,
	"Wolf": 1,
}
const TYPE_IDS: Array[String] = [
	"Bear", "CaptureFlag", "Duck", "Egret", "ElkFemale",
	"ElkMale", "Fish", "Rabbit", "Raccoon", "Wolf",
]
const SOURCE_INDICES := {
	"Bear": [231],
	"CaptureFlag": [0, 2],
	"Duck": [233, 234],
	"Egret": [236, 237],
	"ElkFemale": [973],
	"ElkMale": [984],
	"Fish": [529, 530, 531, 532, 533, 534, 535, 536, 537, 538, 539, 540, 541],
	"Rabbit": [545],
	"Raccoon": [239, 240],
	"Wolf": [242],
}

var passed := 0
var failed := 0
var fixture_root := ""
var controller_script


func _initialize() -> void:
	create_timer(20.0, true, false, true).timeout.connect(_watchdog_timeout)
	call_deferred("_run")


func _run() -> void:
	controller_script = load("res://src/retail_slice/retail_animated_prop_controller.gd")
	_check("controller_script_compiles", controller_script != null)
	if controller_script == null:
		_finish()
		return
	fixture_root = ProjectSettings.globalize_path("user://openbfme-animated-prop-runtime-fixture")
	_cleanup_fixture()
	_check("fixture_directory_created", DirAccess.make_dir_recursive_absolute(fixture_root) == OK)
	var document := _contract_document()
	_check("fixture_pack_written", _write_json(fixture_root.path_join("pack.json"), {"id": "animated-prop-runtime-fixture", "files": {"fordsAnimatedProps": "animated-props.json"}}))
	_check("fixture_contract_written", _write_json(fixture_root.path_join("animated-props.json"), document))

	var fixture := _fixture_scene(document)
	var controller = controller_script.new()
	controller.name = "AnimatedPropRuntimeUnderTest"
	root.add_child(fixture.container)
	root.add_child(controller)
	_check("exact_contract_and_26_placements_configure", bool(controller.configure_from_pack(fixture_root, fixture.placements, fixture.container)), String(controller.error))
	_check("exact_closure_is_controlled", bool(controller.contract_ready) and bool(controller.contract_declared) and int(controller.controlled_placement_count) == 26 and controller.controlled_type_ids == TYPE_IDS)
	_check("all_animation_players_were_inspected", bool(controller.actual_animation_sets_inspected))
	_check("missing_actions_are_explicit_parity_failures", not bool(controller.parity_ready) and _has_diagnostic(controller.diagnostics, "Duck", "missing-declared-action") and _has_diagnostic(controller.diagnostics, "Wolf", "no-action-in-state"), str(controller.diagnostics))

	var bear_player: AnimationPlayer = fixture.players[231]
	var bear_before: Dictionary = controller.placement_status(231)
	bear_player.animation_finished.emit(StringName(bear_before.resolvedAnimation))
	var bear_after: Dictionary = controller.placement_status(231)
	_check("idle_once_completion_reselects_and_replays", String(bear_before.action) != "" and String(bear_after.action) != "" and bear_player.is_playing(), "%s -> %s" % [bear_before, bear_after])

	var duck_status: Dictionary = controller.placement_status(233)
	_check("missing_idle_alternative_renormalizes_to_available_action", String(duck_status.action) == "cuduck_idla" and String(duck_status.mode) == "ONCE", str(duck_status))
	var wolf_status: Dictionary = controller.placement_status(242)
	_check("missing_only_action_holds_bind_pose_without_substitution", String(wolf_status.action) == "" and String(wolf_status.resolvedAnimation) == "", str(wolf_status))

	for flag_index in [0, 2]:
		var flag_player: AnimationPlayer = fixture.players[flag_index]
		var flag_status: Dictionary = controller.placement_status(flag_index)
		_check("capture_flag_%d_starts_at_last_frame_and_holds" % flag_index, String(flag_status.action) == "capflag_sdn" and bool(flag_status.holdAtFinal) and is_equal_approx(flag_player.current_animation_position, 1.0), "%s position=%f" % [flag_status, flag_player.current_animation_position])
	_check("capture_scripted_transition_never_randomly_selects_an_alternative", not bool(controller.set_placement_condition(0, "START_CAPTURE")) and _has_diagnostic(controller.diagnostics, "CaptureFlag", "scripted-state-requires-gameplay-controller"))

	var egret_player: AnimationPlayer = fixture.players[236]
	_check("condition_switch_to_authored_loop_succeeds", bool(controller.set_placement_condition(236, "MOVING")))
	var egret_status: Dictionary = controller.placement_status(236)
	var egret_animation := egret_player.get_animation(StringName(egret_status.resolvedAnimation))
	_check("loop_and_randomstart_are_applied_only_when_authored", String(egret_status.action) == "cuegret_wlka" and String(egret_status.mode) == "LOOP" and egret_animation != null and egret_animation.loop_mode == Animation.LOOP_LINEAR and egret_player.current_animation_position >= 0.0 and egret_player.current_animation_position <= 1.0, "%s position=%f" % [egret_status, egret_player.current_animation_position])

	var weighted_rng := RandomNumberGenerator.new()
	weighted_rng.seed = 77
	var low := 0
	var high := 0
	var weighted_alternatives: Array[Dictionary] = [
		{"animationPriority": 1, "action": "low"},
		{"animationPriority": 20, "action": "high"},
	]
	for _sample in range(2000):
		var picked: Dictionary = controller._weighted_alternative(weighted_alternatives, weighted_rng)
		if String(picked.action) == "high":
			high += 1
		else:
			low += 1
	_check("animation_priority_is_used_as_relative_weight", high > low * 10, "high=%d low=%d" % [high, low])

	var corrupt_document: Dictionary = document.duplicate(true)
	(corrupt_document.scope as Dictionary)["placementCount"] = 25
	var corrupt_controller = controller_script.new()
	root.add_child(corrupt_controller)
	_check("changed_exact_scope_fails_closed", not bool(corrupt_controller.configure_document(corrupt_document, fixture_root, fixture.placements, fixture.container)) and String(corrupt_controller.error).contains("exact Fords closure"), String(corrupt_controller.error))

	print("RETAIL_ANIMATED_PROP_RUNTIME_METRICS types=%d placements=%d inspected=%s parity_ready=%s diagnostics=%d weighted_high=%d weighted_low=%d" % [
		controller.controlled_type_ids.size(),
		controller.controlled_placement_count,
		str(controller.actual_animation_sets_inspected),
		str(controller.parity_ready),
		controller.diagnostics.size(),
		high,
		low,
	])
	controller.queue_free()
	corrupt_controller.queue_free()
	fixture.container.queue_free()
	await process_frame
	await process_frame
	_cleanup_fixture()
	_finish()


func _contract_document() -> Dictionary:
	var targets: Array = []
	var placements: Array = []
	for type_id in TYPE_IDS:
		var target := _target(type_id)
		targets.append(target)
		for source_index_value in SOURCE_INDICES[type_id]:
			var source_index := int(source_index_value)
			placements.append({
				"index": source_index,
				"typeName": type_id,
				"uniqueId": "%s %d" % [type_id, source_index],
				"objectEnabled": true,
				"godotPosition": [float(source_index), 300.0, -float(source_index)],
				"godotYawRadians": float(source_index % 7) * 0.1,
			})
	placements.sort_custom(func(a: Dictionary, b: Dictionary) -> bool: return int(a.index) < int(b.index))
	return {
		"schema": "openbfme.animated-prop-runtime-contract",
		"schemaVersion": 0,
		"scope": {"map": "Fords of Isen II", "targetTypeCount": 10, "placementCount": 26, "targetTypes": TYPE_IDS.duplicate()},
		"targets": targets,
		"placements": placements,
	}


func _target(type_id: String) -> Dictionary:
	var actions: Array[Dictionary] = []
	var states: Array[Dictionary] = []
	var initial_state := "IDLE"
	match type_id:
		"Bear":
			actions = [_action("cubear_idla", 30), _action("cubear_idlb", 30)]
			states = [_idle([_alternative("cubear_idla", "ONCE", 10), _alternative("cubear_idlb", "ONCE", 10)])]
		"CaptureFlag":
			actions = [_action("capflag_sdn", 31), _action("capflag_up", 31)]
			states = [
				_idle([_alternative("capflag_sdn", "ONCE", 1)], ["START_FRAME_LAST"], "IdleUncaptured"),
				{"condition": "START_CAPTURE", "stateName": "FlagUp", "animations": [_alternative("capflag_up", "ONCE", 1)], "flags": [], "scriptRule": "predecessor-selects-authored-alternative"},
			]
			initial_state = "IdleUncaptured"
		"Duck":
			actions = [_action("cuduck_idlb", 30), _action("cuduck_idla", 30)]
			states = [_idle([_alternative("cuduck_idlb", "ONCE", 1), _alternative("cuduck_idla", "ONCE", 10)])]
		"Egret":
			actions = [_action("cuegret_idla", 30), _action("cuegret_wlka", 30)]
			states = [_idle([_alternative("cuegret_idla", "ONCE", 1)]), {"condition": "MOVING", "stateName": "MOVING", "animations": [_alternative("cuegret_wlka", "LOOP", 1)], "flags": ["RANDOMSTART"]}]
		"ElkFemale", "ElkMale":
			actions = [_action("nuhorse_grza", 30), _action("nuhorse_grzb", 30)]
			states = [_idle([_alternative("nuhorse_grza", "ONCE", 10), _alternative("nuhorse_grzb", "ONCE", 10)])]
		"Fish":
			actions = [_action("cutuna_swma", 30), _action("cutuna_jmpa", 30)]
			states = [_idle([_alternative("cutuna_swma", "ONCE", 20), _alternative("cutuna_jmpa", "ONCE", 5)])]
		"Rabbit":
			actions = [_action("curabbit1_idla", 30)]
			states = [_idle([_alternative("curabbit1_idla", "ONCE", 10)])]
		"Raccoon":
			actions = [_action("curaccoon_idla", 30)]
			states = [_idle([_alternative("curaccoon_idla", "ONCE", 10)])]
		"Wolf":
			actions = [_action("cuwolf_idla", 30)]
			states = [_idle([_alternative("cuwolf_idla", "ONCE", 10)])]
	return {
		"targetObject": type_id,
		"placementCount": int(TYPE_COUNTS[type_id]),
		"resourceId": "fixture-%s" % type_id.to_lower(),
		"sourceVirtualModel": "art/w3d/test/%s.w3d" % type_id.to_lower(),
		"outputGlb": _glb_relative(type_id),
		"sharedW3dInputResourceIds": [],
		"glbActionMap": actions,
		"runtime": {"initialState": initial_state, "states": states, "provenNoClipTransitions": []},
	}


func _action(name: String, frame_count: int) -> Dictionary:
	return {"glbAction": name, "frameCount": frame_count, "frameRate": 30, "sourceVirtualPath": "art/w3d/test/%s.w3d" % name}


func _alternative(name: String, mode: String, priority: int) -> Dictionary:
	return {"action": name, "sourceAnimationName": name.to_upper(), "mode": mode, "animationPriority": priority}


func _idle(alternatives: Array[Dictionary], flags: Array[String] = [], state_name: String = "IDLE") -> Dictionary:
	return {"condition": "IDLE", "stateName": state_name, "animations": alternatives, "flags": flags, "idleCompletionPolicy": "select-and-play-an-idle-alternative-again", "selectionLaw": {"declaredWeights": "AnimationPriority", "interpretation": "relative-weight"}}


func _fixture_scene(document: Dictionary) -> Dictionary:
	var container := Node3D.new()
	container.name = "SyntheticBoundRetailProps"
	var placements: Array[Dictionary] = []
	var players: Dictionary = {}
	var targets: Dictionary = {}
	for target_value in Array(document.targets):
		var target := target_value as Dictionary
		targets[String(target.targetObject)] = target
	for placement_value in Array(document.placements):
		var contract := placement_value as Dictionary
		var source_index := int(contract.index)
		var type_id := String(contract.typeName)
		var target: Dictionary = targets[type_id]
		var relative := String(target.outputGlb)
		placements.append({
			"source_type": type_id,
			"source_index": source_index,
			"source_position": Vector3(float(source_index), 300.0, -float(source_index)),
			"position": Vector3(float(source_index), 0.0, -float(source_index)),
			"source_yaw": float(contract.godotYawRadians),
			"yaw": float(contract.godotYawRadians),
			"scale": Vector3.ONE,
			"binding_status": "bound",
			"classification": "model",
			"glb_relative": relative,
			"glb_path": fixture_root.path_join(relative),
		})
		var placement_root := Node3D.new()
		placement_root.name = "Fixture_%04d_%s" % [source_index, type_id]
		placement_root.set_meta("source_index", source_index)
		placement_root.set_meta("source_type", type_id)
		var player := AnimationPlayer.new()
		var library := AnimationLibrary.new()
		for action_value in Array(target.glbActionMap):
			var action := String((action_value as Dictionary).glbAction)
			# One missing alternative and one missing-only action exercise both
			# source-preserving fallback paths without substituting another state.
			if (type_id == "Duck" and action == "cuduck_idlb") or type_id == "Wolf":
				continue
			var animation := Animation.new()
			animation.length = 1.0
			library.add_animation(action, animation)
		player.add_animation_library("", library)
		placement_root.add_child(player)
		container.add_child(placement_root)
		players[source_index] = player
	return {"container": container, "placements": placements, "players": players}


func _glb_relative(type_id: String) -> String:
	# Keep the fixture path directly under its root so containment validation can
	# inspect the existing parent without requiring retail-like payload folders.
	return "fixture_%s.glb" % type_id.to_snake_case()


func _has_diagnostic(diagnostics: Array[Dictionary], type_id: String, reason: String) -> bool:
	for row in diagnostics:
		if String(row.get("sourceType", "")) == type_id and String(row.get("reason", "")) == reason:
			return true
	return false


func _write_json(path: String, document: Dictionary) -> bool:
	var directory_error := DirAccess.make_dir_recursive_absolute(path.get_base_dir())
	if directory_error != OK:
		return false
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return false
	file.store_string(JSON.stringify(document, "  ") + "\n")
	file.close()
	return true


func _cleanup_fixture() -> void:
	for filename in ["animated-props.json", "pack.json"]:
		var path := fixture_root.path_join(filename)
		if FileAccess.file_exists(path):
			DirAccess.remove_absolute(path)
	if fixture_root != "" and DirAccess.dir_exists_absolute(fixture_root):
		DirAccess.remove_absolute(fixture_root)


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("PASS %s" % label)
	else:
		failed += 1
		push_error("FAIL %s%s" % [label, " :: %s" % detail if detail != "" else ""])


func _watchdog_timeout() -> void:
	failed += 1
	push_error("FAIL retail_animated_prop_runtime_watchdog")
	_finish()


func _finish() -> void:
	print("RETAIL_ANIMATED_PROP_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)

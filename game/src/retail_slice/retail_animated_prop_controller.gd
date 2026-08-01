class_name RetailAnimatedPropController
extends Node
## Exact runtime animation presenter for the converted Fords animated props.
##
## The private pack owns the source-derived contract. This controller validates
## that contract against cooked map placements and instantiated GLBs before it
## plays any action. Missing actions remain visible as bind/current poses and
## are surfaced as parity failures; clips from unrelated states are never used.

signal parity_failure(diagnostic: Dictionary)

const MAX_DOCUMENT_BYTES := 1024 * 1024
const MAX_TARGETS := 32
const MAX_PLACEMENTS := 128
const MAX_ACTIONS_PER_TARGET := 32
const MAX_STATES_PER_TARGET := 32
const MAX_ALTERNATIVES_PER_STATE := 8
const MAX_GLB_BYTES := 128 * 1024 * 1024
const EXPECTED_PLACEMENT_COUNTS := {
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
const EXPECTED_TYPE_IDS: Array[String] = [
	"Bear",
	"CaptureFlag",
	"Duck",
	"Egret",
	"ElkFemale",
	"ElkMale",
	"Fish",
	"Rabbit",
	"Raccoon",
	"Wolf",
]

var contract_ready := false
var parity_ready := false
var contract_declared := false
var actual_animation_sets_inspected := false
var error := ""
var controlled_placement_count := 0
var controlled_type_ids: Array[String] = []
var diagnostics: Array[Dictionary] = []

var _target_by_type: Dictionary = {}
var _placement_contract_by_index: Dictionary = {}
var _records_by_index: Dictionary = {}
var _visibility_actions_by_path: Dictionary = {}


func configure_from_pack(pack_root: String, bound_placements: Array[Dictionary], prop_container: Node3D) -> bool:
	_reset()
	var pack_path := ModLoader.resolve_pack_path(pack_root, "pack.json")
	if pack_path == "" or not FileAccess.file_exists(pack_path):
		return _fail("animated prop runtime requires a contained pack document")
	var pack_value: Variant = ModLoader._read_json(pack_path)
	if typeof(pack_value) != TYPE_DICTIONARY:
		return _fail("animated prop runtime pack document is invalid")
	var pack_document := pack_value as Dictionary
	var files_value: Variant = pack_document.get("files", {})
	if typeof(files_value) != TYPE_DICTIONARY:
		return _fail("animated prop runtime pack files table is invalid")
	var files := files_value as Dictionary
	if not files.has("fordsAnimatedProps"):
		return true
	contract_declared = true
	var contract_relative := String(files.get("fordsAnimatedProps", ""))
	var contract_path := ModLoader.resolve_pack_path(pack_root, contract_relative)
	if contract_path == "" or contract_path.get_extension().to_lower() != "json" or not FileAccess.file_exists(contract_path):
		return _fail("declared animated prop runtime document is missing or escaped")
	var file := FileAccess.open(contract_path, FileAccess.READ)
	if file == null or file.get_length() <= 0 or file.get_length() > MAX_DOCUMENT_BYTES:
		return _fail("animated prop runtime document size is invalid")
	var payload := file.get_buffer(file.get_length())
	file.close()
	var parsed: Variant = JSON.parse_string(payload.get_string_from_utf8())
	if typeof(parsed) != TYPE_DICTIONARY:
		return _fail("animated prop runtime document is invalid JSON")
	return configure_document(parsed as Dictionary, pack_root, bound_placements, prop_container)


func configure_document(document: Dictionary, pack_root: String, bound_placements: Array[Dictionary], prop_container: Node3D) -> bool:
	_reset()
	contract_declared = true
	if prop_container == null:
		return _fail("animated prop runtime has no retail prop container")
	if not _validate_document(document):
		return false
	if not _validate_placement_closure(bound_placements):
		return false
	if not _attach_players(pack_root, bound_placements, prop_container):
		return false
	contract_ready = true
	controlled_type_ids.assign(EXPECTED_TYPE_IDS)
	controlled_placement_count = _records_by_index.size()
	actual_animation_sets_inspected = controlled_placement_count == 26
	parity_ready = diagnostics.is_empty() and actual_animation_sets_inspected
	set_meta("retail_rng_oracle_unresolved", true)
	set_meta("actual_animation_sets_inspected", actual_animation_sets_inspected)
	set_meta("animated_prop_diagnostics", diagnostics.duplicate(true))
	return true


func set_placement_condition(source_index: int, condition: String) -> bool:
	if not contract_ready or not _records_by_index.has(source_index) or condition == "":
		return false
	var record: Dictionary = _records_by_index[source_index]
	var target: Dictionary = record.get("target", {})
	var state := _state_for_condition(target, condition)
	if state.is_empty():
		return false
	# CaptureFlag state scripts choose named alternatives, sequence transitions,
	# hide subobjects, and fire Lua events. Until the gameplay capture controller
	# supplies those exact inputs, rejecting the transition is safer than random
	# selection or dropping authored side effects.
	if state.has("scriptRule") or state.has("onEnter") or state.has("luaEvents"):
		_diagnostic(
			source_index,
			String(record.get("source_type", "")),
			condition,
			"",
			String(target.get("outputGlb", "")),
			"scripted-state-requires-gameplay-controller"
		)
		return false
	return _play_state(record, state)


func placement_status(source_index: int) -> Dictionary:
	if not _records_by_index.has(source_index):
		return {}
	var record: Dictionary = _records_by_index[source_index]
	return {
		"sourceIndex": source_index,
		"sourceType": String(record.get("source_type", "")),
		"condition": String(record.get("condition", "")),
		"stateName": String(record.get("state_name", "")),
		"action": String(record.get("action", "")),
		"resolvedAnimation": String(record.get("resolved_animation", "")),
		"mode": String(record.get("mode", "")),
		"holdAtFinal": bool(record.get("hold_at_final", false)),
	}


func _validate_document(document: Dictionary) -> bool:
	if String(document.get("schema", "")) != "openbfme.animated-prop-runtime-contract" or int(document.get("schemaVersion", -1)) != 0:
		return _fail("unexpected animated prop runtime schema")
	var scope_value: Variant = document.get("scope", {})
	if typeof(scope_value) != TYPE_DICTIONARY:
		return _fail("animated prop runtime scope is missing")
	var scope := scope_value as Dictionary
	if String(scope.get("map", "")) != "Fords of Isen II" or int(scope.get("targetTypeCount", -1)) != 10 or int(scope.get("placementCount", -1)) != 26:
		return _fail("animated prop runtime scope is not the exact Fords closure")
	var scope_types := _string_array(scope.get("targetTypes", []))
	if scope_types != EXPECTED_TYPE_IDS:
		return _fail("animated prop runtime target ordering changed")

	var targets_value: Variant = document.get("targets", [])
	if typeof(targets_value) != TYPE_ARRAY:
		return _fail("animated prop runtime targets are missing")
	var targets: Array = targets_value
	if targets.size() != 10 or targets.size() > MAX_TARGETS:
		return _fail("animated prop runtime target count changed")
	for target_value in targets:
		if typeof(target_value) != TYPE_DICTIONARY:
			return _fail("animated prop target is not a document")
		var target := target_value as Dictionary
		var type_id := String(target.get("targetObject", ""))
		if not EXPECTED_PLACEMENT_COUNTS.has(type_id) or _target_by_type.has(type_id):
			return _fail("animated prop target type is duplicate or unexpected")
		if int(target.get("placementCount", -1)) != int(EXPECTED_PLACEMENT_COUNTS[type_id]):
			return _fail("animated prop target placement count changed")
		var output_glb := String(target.get("outputGlb", ""))
		if not ModLoader.is_safe_relative_path(output_glb) or output_glb.get_extension().to_lower() != "glb":
			return _fail("animated prop target GLB is unsafe")
		if not _validate_target_actions_and_states(target):
			return false
		_target_by_type[type_id] = target
	if _target_by_type.size() != EXPECTED_TYPE_IDS.size():
		return _fail("animated prop targets do not cover the exact type closure")

	var placements_value: Variant = document.get("placements", [])
	if typeof(placements_value) != TYPE_ARRAY:
		return _fail("animated prop runtime placements are missing")
	var placements: Array = placements_value
	if placements.size() != 26 or placements.size() > MAX_PLACEMENTS:
		return _fail("animated prop runtime placement count changed")
	var observed_counts: Dictionary = {}
	for placement_value in placements:
		if typeof(placement_value) != TYPE_DICTIONARY:
			return _fail("animated prop placement is not a document")
		var placement := placement_value as Dictionary
		var source_index := int(placement.get("index", -1))
		var type_id := String(placement.get("typeName", ""))
		var unique_id := String(placement.get("uniqueId", ""))
		var position := _vector3(placement.get("godotPosition", []))
		var yaw := float(placement.get("godotYawRadians", NAN))
		if (
			source_index < 0
			or _placement_contract_by_index.has(source_index)
			or not EXPECTED_PLACEMENT_COUNTS.has(type_id)
			or unique_id == ""
			or not unique_id.begins_with(type_id + " ")
			or not bool(placement.get("objectEnabled", false))
			or not _finite_vector3(position)
			or not _finite_number(yaw)
		):
			return _fail("animated prop placement is unsafe or incomplete")
		_placement_contract_by_index[source_index] = placement
		observed_counts[type_id] = int(observed_counts.get(type_id, 0)) + 1
	if observed_counts != EXPECTED_PLACEMENT_COUNTS:
		return _fail("animated prop placement types do not match the exact Fords closure")
	return true


func _validate_target_actions_and_states(target: Dictionary) -> bool:
	var action_map_value: Variant = target.get("glbActionMap", [])
	if typeof(action_map_value) != TYPE_ARRAY:
		return _fail("animated prop action map is missing")
	var action_map: Array = action_map_value
	if action_map.is_empty() or action_map.size() > MAX_ACTIONS_PER_TARGET:
		return _fail("animated prop action map size is invalid")
	var actions: Dictionary = {}
	for mapping_value in action_map:
		if typeof(mapping_value) != TYPE_DICTIONARY:
			return _fail("animated prop action mapping is invalid")
		var mapping := mapping_value as Dictionary
		var action := String(mapping.get("glbAction", ""))
		if (
			action == ""
			or action != action.to_lower()
			or actions.has(action)
			or int(mapping.get("frameCount", 0)) <= 0
			or int(mapping.get("frameRate", 0)) <= 0
		):
			return _fail("animated prop action mapping is incomplete")
		actions[action] = mapping
	target["_action_by_name"] = actions

	var runtime_value: Variant = target.get("runtime", {})
	if typeof(runtime_value) != TYPE_DICTIONARY:
		return _fail("animated prop runtime target has no state contract")
	var runtime := runtime_value as Dictionary
	var initial_state := String(runtime.get("initialState", ""))
	var states_value: Variant = runtime.get("states", [])
	if initial_state == "" or typeof(states_value) != TYPE_ARRAY:
		return _fail("animated prop initial state is missing")
	var states: Array = states_value
	if states.is_empty() or states.size() > MAX_STATES_PER_TARGET:
		return _fail("animated prop state count is invalid")
	var state_keys: Dictionary = {}
	var initial_matches := 0
	for state_value in states:
		if typeof(state_value) != TYPE_DICTIONARY:
			return _fail("animated prop state is invalid")
		var state := state_value as Dictionary
		var condition := String(state.get("condition", ""))
		var state_name := String(state.get("stateName", condition))
		if condition == "" or state_name == "" or state_keys.has(condition):
			return _fail("animated prop state condition is duplicate or empty")
		state_keys[condition] = state
		if state_name == initial_state or (condition == "IDLE" and initial_state == "IDLE"):
			initial_matches += 1
		var alternatives_value: Variant = state.get("animations", [])
		if typeof(alternatives_value) != TYPE_ARRAY:
			return _fail("animated prop state animations are missing")
		var alternatives: Array = alternatives_value
		if alternatives.is_empty() or alternatives.size() > MAX_ALTERNATIVES_PER_STATE:
			return _fail("animated prop state alternative count is invalid")
		for alternative_value in alternatives:
			if typeof(alternative_value) != TYPE_DICTIONARY:
				return _fail("animated prop state alternative is invalid")
			var alternative := alternative_value as Dictionary
			var action := String(alternative.get("action", ""))
			var mode := String(alternative.get("mode", ""))
			var priority := int(alternative.get("animationPriority", 0))
			if not actions.has(action) or not ["ONCE", "LOOP"].has(mode) or priority <= 0 or priority > 10000:
				return _fail("animated prop state references an invalid action contract")
		var flags := _string_array(state.get("flags", []))
		for flag in flags:
			if not ["RANDOMSTART", "START_FRAME_LAST"].has(flag):
				return _fail("animated prop state contains an unsupported playback flag")
	if initial_matches != 1:
		return _fail("animated prop initial state is ambiguous")
	target["_state_by_condition"] = state_keys
	return true


func _validate_placement_closure(bound_placements: Array[Dictionary]) -> bool:
	var animated_bound_by_index: Dictionary = {}
	for placement_value in bound_placements:
		var placement: Dictionary = placement_value
		var type_id := String(placement.get("source_type", ""))
		if not EXPECTED_PLACEMENT_COUNTS.has(type_id):
			continue
		var source_index := int(placement.get("source_index", -1))
		if source_index < 0 or animated_bound_by_index.has(source_index):
			return _fail("animated bound placement index is invalid")
		animated_bound_by_index[source_index] = placement
	if animated_bound_by_index.size() != 26 or animated_bound_by_index.size() != _placement_contract_by_index.size():
		return _fail("animated bound placements do not cover the runtime contract")
	for source_index_value in _placement_contract_by_index:
		var source_index := int(source_index_value)
		if not animated_bound_by_index.has(source_index):
			return _fail("animated runtime placement is not map-bound")
		var contract: Dictionary = _placement_contract_by_index[source_index]
		var placement: Dictionary = animated_bound_by_index[source_index]
		var type_id := String(contract.get("typeName", ""))
		var target: Dictionary = _target_by_type.get(type_id, {})
		if (
			String(placement.get("binding_status", "")) != "bound"
			or String(placement.get("source_type", "")) != type_id
			or String(placement.get("glb_relative", "")).replace("\\", "/") != String(target.get("outputGlb", "")).replace("\\", "/")
			or not Vector3(placement.get("source_position", Vector3.INF)).is_equal_approx(_vector3(contract.get("godotPosition", [])))
			or not is_equal_approx(float(placement.get("source_yaw", NAN)), float(contract.get("godotYawRadians", NAN)))
		):
			return _fail("animated map placement disagrees with its runtime contract")
	return true


func _attach_players(pack_root: String, bound_placements: Array[Dictionary], prop_container: Node3D) -> bool:
	var placement_by_index: Dictionary = {}
	for placement_value in bound_placements:
		var placement: Dictionary = placement_value
		if EXPECTED_PLACEMENT_COUNTS.has(String(placement.get("source_type", ""))):
			placement_by_index[int(placement.get("source_index", -1))] = placement
	var root_by_index: Dictionary = {}
	for child_value in prop_container.get_children():
		if child_value is not Node3D:
			continue
		var child := child_value as Node3D
		var source_index := int(child.get_meta("source_index", -1))
		if _placement_contract_by_index.has(source_index):
			if root_by_index.has(source_index):
				return _fail("animated prop scene root is duplicate")
			root_by_index[source_index] = child
	if root_by_index.size() != 26:
		return _fail("animated prop scene roots do not cover the runtime contract")

	for source_index_value in _placement_contract_by_index:
		var source_index := int(source_index_value)
		var placement: Dictionary = placement_by_index[source_index]
		var placement_root := root_by_index[source_index] as Node3D
		var source_type := String(placement.get("source_type", ""))
		var target: Dictionary = _target_by_type[source_type]
		var declared_path := ModLoader.resolve_pack_path(pack_root, String(target.get("outputGlb", "")))
		if declared_path == "" or declared_path.replace("\\", "/") != String(placement.get("glb_path", "")).replace("\\", "/"):
			return _fail("animated prop scene GLB path disagrees with its target")
		var player := _first_animation_player(placement_root)
		var visibility_actions: Dictionary = {}
		if player != null:
			var action_by_name: Dictionary = target.get("_action_by_name", {})
			for action_value in action_by_name:
				if _resolve_animation_name(player, String(action_value)) == StringName():
					visibility_actions = _visibility_only_actions(declared_path)
					break
			if error != "":
				return false
		var record := {
			"source_index": source_index,
			"source_type": source_type,
			"placement_root": placement_root,
			"player": player,
			"target": target,
			"visibility_actions": visibility_actions,
			"rng": _placement_rng(source_index, source_type),
			"condition": "",
			"state_name": "",
			"action": "",
			"resolved_animation": "",
			"mode": "",
			"hold_at_final": false,
		}
		_records_by_index[source_index] = record
		if player == null:
			_diagnostic(source_index, source_type, "", "", String(target.get("outputGlb", "")), "missing-animation-player")
			continue
		_isolate_animation_libraries(player)
		var action_by_name: Dictionary = target.get("_action_by_name", {})
		for action_value in action_by_name:
			var action := String(action_value)
			if _resolve_animation_name(player, action) == StringName() and not visibility_actions.has(action):
				_diagnostic(source_index, source_type, "declared-action-set", action, String(target.get("outputGlb", "")), "missing-declared-action")
		var callback := Callable(self, "_on_animation_finished").bind(source_index)
		if not player.animation_finished.is_connected(callback):
			player.animation_finished.connect(callback)
		var initial_state := _initial_state(target)
		if initial_state.is_empty():
			return _fail("animated prop target lost its validated initial state")
		_play_state(record, initial_state)
	return true


func _play_state(record: Dictionary, state: Dictionary) -> bool:
	var player: AnimationPlayer = record.get("player")
	var source_index := int(record.get("source_index", -1))
	var source_type := String(record.get("source_type", ""))
	var target: Dictionary = record.get("target", {})
	var condition := String(state.get("condition", ""))
	var available: Array[Dictionary] = []
	var visibility_actions: Dictionary = record.get("visibility_actions", {})
	for alternative_value in Array(state.get("animations", [])):
		var alternative := alternative_value as Dictionary
		var action := String(alternative.get("action", ""))
		if player != null and (_resolve_animation_name(player, action) != StringName() or visibility_actions.has(action)):
			available.append(alternative)
	if available.is_empty():
		var requested := String((Array(state.get("animations", []))[0] as Dictionary).get("action", ""))
		_diagnostic(source_index, source_type, condition, requested, String(target.get("outputGlb", "")), "no-action-in-state")
		record["condition"] = condition
		record["state_name"] = String(state.get("stateName", condition))
		record["action"] = ""
		record["resolved_animation"] = ""
		record["mode"] = ""
		record["hold_at_final"] = false
		return false
	var alternative := _weighted_alternative(available, record.get("rng") as RandomNumberGenerator)
	var action := String(alternative.get("action", ""))
	if visibility_actions.has(action):
		player.stop()
		var placement_root: Node3D = record.get("placement_root")
		placement_root.visible = true
		record["condition"] = condition
		record["state_name"] = String(state.get("stateName", condition))
		record["action"] = action
		record["resolved_animation"] = ""
		record["mode"] = "VISIBILITY_ONLY_BIND_POSE"
		record["hold_at_final"] = _string_array(state.get("flags", [])).has("START_FRAME_LAST")
		return true
	var resolved := _resolve_animation_name(player, action)
	var animation := player.get_animation(resolved)
	if animation == null:
		_diagnostic(source_index, source_type, condition, action, String(target.get("outputGlb", "")), "resolved-action-unavailable")
		return false
	var mode := String(alternative.get("mode", ""))
	animation.loop_mode = Animation.LOOP_LINEAR if mode == "LOOP" else Animation.LOOP_NONE
	var speed_factor := float(alternative.get("speedFactor", 1.0))
	if not _finite_number(speed_factor) or speed_factor <= 0.0:
		speed_factor = 1.0
	player.play(resolved, -1.0, speed_factor)
	record["condition"] = condition
	record["state_name"] = String(state.get("stateName", condition))
	record["action"] = action
	record["resolved_animation"] = String(resolved)
	record["mode"] = mode
	record["hold_at_final"] = false
	var flags := _string_array(state.get("flags", []))
	if flags.has("START_FRAME_LAST"):
		player.seek(animation.length, true)
		player.pause()
		record["hold_at_final"] = true
	elif flags.has("RANDOMSTART"):
		var mapping: Dictionary = (target.get("_action_by_name", {}) as Dictionary).get(action, {})
		var frame_count := int(mapping.get("frameCount", 0))
		var frame_rate := float(mapping.get("frameRate", 0))
		if frame_count > 0 and frame_rate > 0.0:
			var rng := record.get("rng") as RandomNumberGenerator
			var frame := rng.randi_range(0, frame_count - 1)
			player.seek(min(float(frame) / frame_rate, animation.length), true)
	return true


func _on_animation_finished(_animation_name: StringName, source_index: int) -> void:
	if not _records_by_index.has(source_index):
		return
	var record: Dictionary = _records_by_index[source_index]
	if bool(record.get("hold_at_final", false)):
		return
	var target: Dictionary = record.get("target", {})
	var state := _state_for_condition(target, String(record.get("condition", "")))
	if state.is_empty():
		return
	if String(record.get("mode", "")) == "LOOP" or String(state.get("idleCompletionPolicy", "")) == "select-and-play-an-idle-alternative-again":
		_play_state(record, state)


func _initial_state(target: Dictionary) -> Dictionary:
	var runtime: Dictionary = target.get("runtime", {})
	var initial := String(runtime.get("initialState", ""))
	for state_value in Array(runtime.get("states", [])):
		var state := state_value as Dictionary
		if String(state.get("stateName", state.get("condition", ""))) == initial or (initial == "IDLE" and String(state.get("condition", "")) == "IDLE"):
			return state
	return {}


func _state_for_condition(target: Dictionary, condition: String) -> Dictionary:
	return (target.get("_state_by_condition", {}) as Dictionary).get(condition, {})


func _weighted_alternative(alternatives: Array[Dictionary], rng: RandomNumberGenerator) -> Dictionary:
	var total := 0
	for alternative in alternatives:
		total += int(alternative.get("animationPriority", 1))
	var pick := rng.randi_range(1, total)
	for alternative in alternatives:
		pick -= int(alternative.get("animationPriority", 1))
		if pick <= 0:
			return alternative
	return alternatives.back()


func _placement_rng(source_index: int, source_type: String) -> RandomNumberGenerator:
	var rng := RandomNumberGenerator.new()
	# This is stable for multiplayer/runtime tests, but is deliberately marked as
	# an unresolved oracle point until BFME2's exact client RNG stream is recovered.
	rng.seed = (source_index * 0x9E3779B1) ^ source_type.hash()
	return rng


func _resolve_animation_name(player: AnimationPlayer, action: String) -> StringName:
	var matches: Array[StringName] = []
	for name in player.get_animation_list():
		var text := String(name)
		if text == action or text.get_slice("/", text.get_slice_count("/") - 1) == action:
			matches.append(name)
	return matches[0] if matches.size() == 1 else StringName()


func _visibility_only_actions(glb_path: String) -> Dictionary:
	if _visibility_actions_by_path.has(glb_path):
		return (_visibility_actions_by_path[glb_path] as Dictionary).duplicate(true)
	var result: Dictionary = {}
	if not FileAccess.file_exists(glb_path):
		_visibility_actions_by_path[glb_path] = result
		return result
	var file := FileAccess.open(glb_path, FileAccess.READ)
	if file == null or file.get_length() < 20 or file.get_length() > MAX_GLB_BYTES:
		_fail("animated prop GLB sidecar source is invalid")
		return {}
	if file.get_buffer(4).get_string_from_ascii() != "glTF" or file.get_32() != 2:
		_fail("animated prop GLB sidecar header is invalid")
		return {}
	var declared_length := file.get_32()
	var json_length := file.get_32()
	var json_type := file.get_32()
	if declared_length != file.get_length() or json_length <= 0 or json_length > declared_length - 20 or json_type != 0x4E4F534A:
		_fail("animated prop GLB sidecar chunk is invalid")
		return {}
	var parsed: Variant = JSON.parse_string(file.get_buffer(json_length).get_string_from_utf8().strip_edges())
	file.close()
	if typeof(parsed) != TYPE_DICTIONARY:
		_fail("animated prop GLB sidecar JSON is invalid")
		return {}
	var extras_value: Variant = (parsed as Dictionary).get("extras", {})
	if typeof(extras_value) != TYPE_DICTIONARY:
		_fail("animated prop GLB root extras are invalid")
		return {}
	var contract_value: Variant = (extras_value as Dictionary).get("openbfme_w3d_visibility_only_animations", null)
	if contract_value == null:
		_visibility_actions_by_path[glb_path] = result
		return result
	if typeof(contract_value) != TYPE_DICTIONARY:
		_fail("animated prop visibility-only sidecar is invalid")
		return {}
	var contract := contract_value as Dictionary
	if contract.size() != 3 or String(contract.get("schema", "")) != "openbfme.w3d-visibility-only-animations" or int(contract.get("version", -1)) != 1 or typeof(contract.get("animations", null)) != TYPE_ARRAY:
		_fail("animated prop visibility-only sidecar schema changed")
		return {}
	for animation_value in contract.get("animations", []) as Array:
		if typeof(animation_value) != TYPE_DICTIONARY:
			_fail("animated prop visibility-only action is invalid")
			return {}
		var animation := animation_value as Dictionary
		var action := String(animation.get("name", ""))
		var channels_value: Variant = animation.get("channels", null)
		if animation.size() != 3 or action == "" or result.has(action) or String(animation.get("shape", "")) != "visibility-only" or typeof(channels_value) != TYPE_ARRAY or (channels_value as Array).is_empty():
			_fail("animated prop visibility-only action contract changed")
			return {}
		for channel_value in channels_value as Array:
			if typeof(channel_value) != TYPE_DICTIONARY:
				_fail("animated prop visibility-only channel is invalid")
				return {}
			var channel := channel_value as Dictionary
			var path := String(channel.get("data_path", ""))
			var keys_value: Variant = channel.get("keys", null)
			if channel.size() != 4 or String(channel.get("owner", "")) not in ["object", "armature"] or (path != "hide_viewport" and not (path.begins_with('bones["') and path.ends_with('"].visibility'))) or typeof(keys_value) != TYPE_ARRAY or (keys_value as Array).is_empty():
				_fail("animated prop visibility-only channel contract changed")
				return {}
			for key_value in keys_value as Array:
				if typeof(key_value) != TYPE_DICTIONARY:
					_fail("animated prop visibility-only key is invalid")
					return {}
				var key := key_value as Dictionary
				if key.size() != 3 or not is_equal_approx(float(key.get("frame", NAN)), 0.0) or not is_equal_approx(float(key.get("value", NAN)), 1.0) or String(key.get("interpolation", "")) == "":
					_fail("animated prop visibility-only bind-pose proof changed")
					return {}
		result[action] = animation.duplicate(true)
	_visibility_actions_by_path[glb_path] = result.duplicate(true)
	return result


func _isolate_animation_libraries(player: AnimationPlayer) -> void:
	for library_name in player.get_animation_library_list():
		var library := player.get_animation_library(library_name)
		if library == null:
			continue
		var isolated := library.duplicate(true) as AnimationLibrary
		player.remove_animation_library(library_name)
		player.add_animation_library(library_name, isolated)


func _first_animation_player(node: Node) -> AnimationPlayer:
	if node is AnimationPlayer:
		return node as AnimationPlayer
	for child in node.get_children():
		var found := _first_animation_player(child)
		if found != null:
			return found
	return null


func _diagnostic(source_index: int, source_type: String, state: String, action: String, glb: String, reason: String) -> void:
	var row := {
		"sourceIndex": source_index,
		"sourceType": source_type,
		"state": state,
		"requestedAction": action,
		"glb": glb,
		"reason": reason,
	}
	if not diagnostics.has(row):
		diagnostics.append(row)
		parity_failure.emit(row)
	parity_ready = false
	set_meta("animated_prop_diagnostics", diagnostics.duplicate(true))


func _string_array(value: Variant) -> Array[String]:
	var result: Array[String] = []
	if typeof(value) != TYPE_ARRAY:
		return result
	for item in value as Array:
		if typeof(item) != TYPE_STRING:
			return []
		result.append(String(item))
	return result


func _vector3(value: Variant) -> Vector3:
	if typeof(value) != TYPE_ARRAY:
		return Vector3.INF
	var values := value as Array
	if values.size() != 3:
		return Vector3.INF
	return Vector3(float(values[0]), float(values[1]), float(values[2]))


func _finite_number(value: float) -> bool:
	return not is_nan(value) and not is_inf(value)


func _finite_vector3(value: Vector3) -> bool:
	return _finite_number(value.x) and _finite_number(value.y) and _finite_number(value.z)


func _fail(message: String) -> bool:
	contract_ready = false
	parity_ready = false
	if error == "":
		error = message
	return false


func _reset() -> void:
	contract_ready = false
	parity_ready = false
	contract_declared = false
	actual_animation_sets_inspected = false
	error = ""
	controlled_placement_count = 0
	controlled_type_ids.clear()
	diagnostics.clear()
	_target_by_type.clear()
	_placement_contract_by_index.clear()
	_records_by_index.clear()
	_visibility_actions_by_path.clear()

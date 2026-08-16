extends SceneTree

const Watchdog := preload("res://tests/runner_watchdog.gd")
const EXPECTED_CHECKS := 42

var passed := 0
var failed := 0
var _watchdog := Watchdog.new()


func _initialize() -> void:
	_watchdog.start(self, "DRAWABLE_SCRIPT_RUNTIME", 0, 0, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	await process_frame
	await process_frame
	var asset_factory: GDScript = load("res://src/view/asset_factory.gd") as GDScript
	_check("asset factory loads", asset_factory != null)
	if asset_factory == null:
		_finish()
		return
	var db := root.get_node_or_null("ContentDB")
	var projection: Dictionary = db._playable_unit_projection({
		"objectId": "RetailUnit",
		"registration": {
			"composition": {"primaryMemberObjectId": "RetailMember"},
			"visual": {
				"components": [{"default": true, "output": "assets/unit.glb"}],
				"coreAnimations": {},
				"drawableScripts": [{"targetObject": "RetailMember", "actions": []}],
			},
		},
	})
	_check("content projection carries drawable scripts", ((projection.get("member", {}) as Dictionary).get("drawableScripts", []) as Array).size() == 1)
	var visual_root := Node3D.new()
	var bow := Node3D.new()
	bow.name = "BOW"
	visual_root.add_child(bow)
	var sword := Node3D.new()
	sword.name = "Sword01"
	visual_root.add_child(sword)
	var definition := {
		"sourceObjectId": "RetailUnit",
		"drawableScripts": [
			{"targetObject": "RetailUnit", "conditions": [], "actions": [
				{"operation": "hide-sub-object", "arguments": ["BOW"], "supported": true},
			]},
			{"targetObject": "RetailUnit", "conditions": ["USER_1"], "actions": [
				{"operation": "show-sub-object", "arguments": ["SWORD"], "supported": true},
				{"operation": "play-sound", "arguments": ["ArrowDrawBow"], "supported": true},
			]},
		],
	}

	var default_result: Dictionary = asset_factory.apply_drawable_scripts(visual_root, definition, [])
	_check("default script hides BOW", not bow.visible)
	_check("conditioned script waits", sword.visible)
	_check("default action counted", int(default_result.get("applied", 0)) == 1)
	_check("default has no hidden gap", (default_result.get("unhandled", []) as Array).is_empty())
	_check("default emits no audio intent", (default_result.get("audio_intents", []) as Array).is_empty())

	sword.visible = false
	var user_result: Dictionary = asset_factory.apply_drawable_scripts(visual_root, definition, ["USER_1"])
	_check("USER_1 reveals sword", sword.visible)
	_check("default and conditioned actions execute", int(user_result.get("applied", 0)) == 3)
	var unhandled := user_result.get("unhandled", []) as Array
	_check("play-sound is no longer a static gap", unhandled.is_empty())
	var audio_intents := user_result.get("audio_intents", []) as Array
	_check("play-sound emits authored logical id", audio_intents.size() == 1 and String((audio_intents[0] as Dictionary).get("event_id", "")) == "ArrowDrawBow")
	_check("audio intent retains source ordering", int((audio_intents[0] as Dictionary).get("script_index", -1)) == 1 and int((audio_intents[0] as Dictionary).get("action_index", -1)) == 1)
	var malformed: Dictionary = asset_factory.apply_drawable_scripts(visual_root, {"drawableScripts": [{"actions": [{"operation": "play-sound", "arguments": [], "supported": true}]}]}, [])
	_check("malformed play-sound fails closed", (malformed.get("audio_intents", []) as Array).is_empty() and (malformed.get("unhandled", []) as Array).size() == 1)
	var ordered: Dictionary = asset_factory.apply_drawable_scripts(visual_root, {
		"sourceObjectId": "RetailUnit",
		"drawableScripts": [
			{"targetObject": "RetailUnit", "conditions": [], "actions": [{"operation": "play-sound", "arguments": ["ArrowDrawBow"], "supported": true}]},
			{"targetObject": "RetailUnit", "conditions": ["USER_1"], "actions": [{"operation": "play-sound", "arguments": ["ImpactHorse"], "supported": true}]},
		],
	}, ["USER_1"])
	var ordered_intents := ordered.get("audio_intents", []) as Array
	_check("matching model-condition scripts preserve source order", ordered_intents.size() == 2 and String((ordered_intents[0] as Dictionary).get("event_id", "")) == "ArrowDrawBow" and String((ordered_intents[1] as Dictionary).get("event_id", "")) == "ImpactHorse")
	_check("audio intents retain authored condition provenance", ((ordered_intents[1] as Dictionary).get("conditions", []) as Array) == ["USER_1"])
	var wrong_source: Dictionary = asset_factory.apply_drawable_scripts(visual_root, {"sourceObjectId": "OtherUnit", "drawableScripts": definition["drawableScripts"]}, ["USER_1"])
	_check("target object mismatch emits no audio", (wrong_source.get("audio_intents", []) as Array).is_empty())
	var control_flow: Dictionary = asset_factory.apply_drawable_scripts(visual_root, {"drawableScripts": [{"actions": [{"operation": "set-transition", "arguments": ["A", "B"], "supported": true}]}]}, [])
	_check("unimplemented control flow remains explicit", (control_flow.get("unhandled", []) as Array).size() == 1 and String(((control_flow.get("unhandled", []) as Array)[0] as Dictionary).get("reason", "")) == "runtime-unsupported")

	bow.visible = true
	var permanent_hide: Dictionary = asset_factory.apply_drawable_scripts(visual_root, {"drawableScripts": [{"actions": [
		{"operation": "hide-sub-object-permanently", "arguments": ["BOW"], "supported": true, "raw": "CurDrawableHideSubObjectPermanently(\"BOW\")"},
	]}]}, [])
	_check("permanent hide conceals BOW", not bow.visible and int(permanent_hide.get("applied", 0)) == 1)
	var blocked_show: Dictionary = asset_factory.apply_drawable_scripts(visual_root, {"drawableScripts": [{"actions": [
		{"operation": "show-sub-object", "arguments": ["BOW"], "supported": true, "raw": "CurDrawableShowSubObject(\"BOW\")"},
	]}]}, [])
	_check("show-sub-object cannot clear a permanent hide", not bow.visible and int(blocked_show.get("applied", 0)) == 1)
	var permanent_show: Dictionary = asset_factory.apply_drawable_scripts(visual_root, {"drawableScripts": [{"actions": [
		{"operation": "show-sub-object-permanently", "arguments": ["BOW"], "supported": true, "raw": "CurDrawableShowSubObjectPermanently(\"BOW\")"},
	]}]}, [])
	_check("show-sub-object-permanently clears the permanent hide", bow.visible and int(permanent_show.get("applied", 0)) == 1)

	var trebuchet := Node3D.new()
	trebuchet.name = "Draw_Trebuchet"
	visual_root.add_child(trebuchet)
	var hide_module: Dictionary = asset_factory.apply_drawable_scripts(visual_root, {"drawableScripts": [{"actions": [
		{"operation": "hide-module", "arguments": ["Draw_Trebuchet"], "supported": true, "raw": "CurDrawableHideModule(\"Draw_Trebuchet\")"},
	]}]}, [])
	_check("hide-module conceals the named draw module", not trebuchet.visible and int(hide_module.get("applied", 0)) == 1)
	var show_module: Dictionary = asset_factory.apply_drawable_scripts(visual_root, {"drawableScripts": [{"actions": [
		{"operation": "show-module", "arguments": ["Draw_Trebuchet"], "supported": true, "raw": "CurDrawableShowModule(\"Draw_Trebuchet\")"},
	]}]}, [])
	_check("show-module reveals the named draw module", trebuchet.visible and int(show_module.get("applied", 0)) == 1)
	var missing_module: Dictionary = asset_factory.apply_drawable_scripts(visual_root, {"drawableScripts": [{"actions": [
		{"operation": "hide-module", "arguments": ["Draw_Missing"], "supported": true, "raw": "CurDrawableHideModule(\"Draw_Missing\")"},
	]}]}, [])
	_check("missing draw module fails closed", (missing_module.get("unhandled", []) as Array).size() == 1)

	var matching_prev: Dictionary = asset_factory.apply_drawable_scripts(visual_root, {
		"previousStateLabels": ["STATE_Idle"],
		"drawableScripts": [{"actions": [
			{"operation": "unsupported-script-statement", "arguments": [], "supported": false, "raw": "Prev = CurDrawablePrevAnimationState()"},
			{"operation": "unsupported-script-control-flow", "arguments": [], "supported": false, "raw": "if Prev == \"STATE_Idle\""},
			{"operation": "unsupported-script-control-flow", "arguments": [], "supported": false, "raw": "then"},
			{"operation": "set-transition-animation-state", "arguments": ["TRANS_IdleToSelected"], "supported": true, "raw": "CurDrawableSetTransitionAnimState(\"TRANS_IdleToSelected\")"},
			{"operation": "unsupported-script-control-flow", "arguments": [], "supported": false, "raw": "end"},
		]}],
	}, [])
	_check("matching Prev executes set-transition-animation-state", String(matching_prev.get("transition_anim_state", "")) == "TRANS_IdleToSelected" and int(matching_prev.get("applied", 0)) >= 1)
	_check("matching Prev does not leave the transition as a gap", (matching_prev.get("unhandled", []) as Array).is_empty())
	var mismatch_prev: Dictionary = asset_factory.apply_drawable_scripts(visual_root, {
		"previousStateLabels": ["STATE_Selected"],
		"drawableScripts": [{"actions": [
			{"operation": "set-transition-animation-state", "arguments": ["TRANS_IdleToSelected"], "supported": true, "raw": "if Prev == \"STATE_Idle\" then CurDrawableSetTransitionAnimState(\"TRANS_IdleToSelected\") end"},
		]}],
	}, [])
	_check("non-matching Prev skips the authored transition", String(mismatch_prev.get("transition_anim_state", "")) == "" and (mismatch_prev.get("unhandled", []) as Array).is_empty())
	var continue_ok: Dictionary = asset_factory.apply_drawable_scripts(visual_root, {
		"previousStateLabels": ["STATE_Firing"],
		"drawableScripts": [{"actions": [
			{"operation": "allow-to-continue", "arguments": [], "supported": true, "raw": "if CurDrawablePrevAnimationState() == \"STATE_Firing\" then CurDrawableAllowToContinue() end"},
		]}],
	}, [])
	_check("matching previous animation allows continuation", bool(continue_ok.get("allow_to_continue", false)) and int(continue_ok.get("applied", 0)) == 1)
	var continue_skip: Dictionary = asset_factory.apply_drawable_scripts(visual_root, {
		"previousStateLabels": ["STATE_Idle"],
		"drawableScripts": [{"actions": [
			{"operation": "allow-to-continue", "arguments": [], "supported": true, "raw": "if CurDrawablePrevAnimationState() == \"STATE_Firing\" then CurDrawableAllowToContinue() end"},
		]}],
	}, [])
	_check("non-matching previous animation does not continue", not bool(continue_skip.get("allow_to_continue", false)))
	sword.visible = false
	var unknown_if: Dictionary = asset_factory.apply_drawable_scripts(visual_root, {"drawableScripts": [{"actions": [
		{"operation": "unsupported-script-control-flow", "arguments": [], "supported": false, "raw": "if (condition)"},
		{"operation": "show-sub-object", "arguments": ["SWORD"], "supported": true, "raw": "CurDrawableShowSubObject(\"SWORD\")"},
		{"operation": "unsupported-script-control-flow", "arguments": [], "supported": false, "raw": "end"},
	]}]}, [])
	_check("unknown if fails closed and skips its body", not sword.visible)
	_check("unknown if remains an explicit gap", (unknown_if.get("unhandled", []) as Array).size() == 1)

	var battalion_script: GDScript = load("res://src/retail_slice/retail_battalion.gd") as GDScript
	_check("battalion consumer loads", battalion_script != null and battalion_script.can_instantiate())
	if battalion_script != null and battalion_script.can_instantiate():
		var battalion = battalion_script.new()
		battalion.member_count = 1
		battalion.member_health_ratios[0] = 1.0
		battalion.member_visuals[0] = visual_root
		battalion._drawable_scripts = [{
			"targetObject": "",
			"conditions": ["SELECTED"],
			"actions": [
				{"operation": "set-transition-animation-state", "arguments": ["TRANS_IdleToSelected"], "supported": true, "raw": "if Prev == \"STATE_Idle\" then CurDrawableSetTransitionAnimState(\"TRANS_IdleToSelected\") end"},
			],
		}]
		battalion.member_current_authored_state_labels[0] = ["STATE_Idle"]
		var receipt: Dictionary = battalion.apply_member_drawable_scripts(0, ["SELECTED"])
		_check("battalion consumes set-transition-animation-state from Prev", String(receipt.get("transition_anim_state", "")) == "TRANS_IdleToSelected")
		battalion.free()

	var structure_script: GDScript = load("res://src/retail_slice/retail_structure.gd") as GDScript
	_check("structure consumer loads", structure_script != null)
	if structure_script != null:
		bow.visible = true
		var structure = structure_script.new()
		structure.entity_id = 41
		structure._bundle_object_id = "RetailUnit"
		structure._drawable_scripts = definition["drawableScripts"]
		var routed_requests: Array[Dictionary] = []
		structure.lifecycle_route_requested.connect(func(request: Dictionary) -> void: routed_requests.append(request))
		root.add_child(structure)
		structure._apply_drawable_scripts_for_phase(
			visual_root, {"sourceConditionSets": [["USER_1"]]}, "intact"
		)
		_check("structure intact phase applies default drawable script", not bow.visible)
		_check("structure exposes drawable diagnostics", structure.drawable_actions_applied == 3 and structure.drawable_action_gaps.is_empty())
		_check("structure emits one routed audio activation", routed_requests.size() == 1 and String(routed_requests[0].get("audioEvent", "")) == "ArrowDrawBow")
		structure._apply_drawable_scripts_for_phase(visual_root, {"sourceConditionSets": [["USER_1"]]}, "intact")
		_check("same activation does not replay sound", routed_requests.size() == 1)
		structure._apply_drawable_scripts_for_phase(visual_root, {"sourceConditionSets": [[]]}, "damaged")
		structure._apply_drawable_scripts_for_phase(visual_root, {"sourceConditionSets": [["USER_1"]]}, "intact")
		_check("condition reactivation plays exactly once", routed_requests.size() == 2)

		var audio_script: GDScript = load("res://src/retail_slice/retail_slice_audio.gd") as GDScript
		var audio = audio_script.new()
		root.add_child(audio)
		var audio_pack_root := ""
		for pack_root_value in db.pack_roots:
			var candidate_root := String(pack_root_value)
			if FileAccess.file_exists(candidate_root.path_join("data/audio_events.json")):
				audio_pack_root = candidate_root
				break
		var configured: bool = audio_pack_root != "" and audio.configure(audio_pack_root, false)
		audio.observability_enabled = true
		var accepted: Dictionary = audio.play_declared_structure_event("ArrowDrawBow", 1, 41, "drawable-script")
		_check("authored drawable id reaches audio registry", configured and bool(accepted.get("ok", false)) and String(accepted.get("event_id", "")) == "ArrowDrawBow")
		var rejected: Dictionary = audio.play_declared_structure_event("DefinitelyMissingDrawableEvent", 2, 41, "drawable-script")
		_check("unresolved drawable id remains fail closed", not bool(rejected.get("ok", false)))
		_check("audio route records accepted then rejected exactly once", audio.routing_log.size() == 2)
		var live_results: Array[Dictionary] = []
		var presentation_sequence := [20]
		structure.lifecycle_route_requested.connect(func(request: Dictionary) -> void:
			presentation_sequence[0] = int(presentation_sequence[0]) + 1
			live_results.append(audio.play_declared_structure_event(
				String(request.get("audioEvent", "")), int(presentation_sequence[0]),
				int(request.get("entityId", 0)), String(request.get("phase", ""))
			))
		)
		structure._apply_drawable_scripts_for_phase(visual_root, {"sourceConditionSets": [[]]}, "damaged")
		structure._apply_drawable_scripts_for_phase(visual_root, {"sourceConditionSets": [["USER_1"]]}, "intact")
		_check("activation signal reaches RetailSliceAudio once", live_results.size() == 1 and bool(live_results[0].get("ok", false)) and audio.routing_log.size() == 3)
		audio.free()
		structure.free()
	visual_root.free()
	_finish()


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error(label)


func _finish() -> void:
	print("DRAWABLE_SCRIPT_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		push_error("check-count mismatch: expected=%d actual=%d" % [EXPECTED_CHECKS, passed + failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)

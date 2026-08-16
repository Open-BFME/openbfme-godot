extends SceneTree
## DrawableFxList expands an authored FXList id through the shared ability FX
## controller. EnteringStateFX / FXEvent prove WHICH list to fire; this runner
## proves the same expansion abilities already use produces particle/audio
## intents. No second player.

const Watchdog := preload("res://tests/runner_watchdog.gd")
const AbilityFxControllerScript := preload("res://src/retail_slice/retail_ability_fx_controller.gd")
const EXPECTED_CHECKS := 8

var passed := 0
var failed := 0
var _watchdog := Watchdog.new()


func _initialize() -> void:
	_watchdog.start(self, "DRAWABLE_FX_LIST_RUNTIME", 0, 0, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	await process_frame
	var registry: Dictionary = AbilityFxControllerScript.collect_fx_registry(
		[{"registration": {"fxBindings": _wizard_blast_bindings()}}]
	)
	_check(
		"shared registry copies sealed audioEventIds",
		(registry.get("FX_TelekinesisAtBone", {}) as Dictionary).get("audio_event_ids", [])
		== ["GandalfWizardBlast"]
	)

	var controller = AbilityFxControllerScript.new()
	controller.configure(Callable(), registry)
	var expansion: Dictionary = controller.expand_fx_lists(["FX_TelekinesisAtBone"])
	_check(
		"shared expansion names converted particles and audio",
		String(expansion.get("source", "")) == "typed-drawable-fx-list"
		and (expansion.get("particleSystemIds", []) as Array) == [
			"GandalfWaveBlastProxy", "GandalfWaveBlastWave"
		]
		and (expansion.get("audioEventIds", []) as Array) == ["GandalfWizardBlast"]
		and (expansion.get("unresolvedFxLists", []) as Array).is_empty()
	)
	var sound_only: Dictionary = controller.expand_fx_lists(["FX_EomerSpearThrow"])
	_check(
		"sound-only FXList stays unresolved under the existing ability law",
		(sound_only.get("unresolvedFxLists", []) as Array) == ["FX_EomerSpearThrow"]
		and (sound_only.get("particleSystemIds", ["x"]) as Array).is_empty()
	)

	var presented: Dictionary = controller.present_drawable_fx_lists(
		["FX_TelekinesisAtBone"], Vector2.ZERO, "EnteringStateFX"
	)
	_check(
		"present_drawable_fx_lists uses the shared cue path",
		int(presented.get("applied", 0)) == 1
		and int(controller.cues_presented) == 1
		and String((controller.cue_log[0] as Dictionary).get("source", "")) == "EnteringStateFX"
		and (presented.get("particleSystemIds", []) as Array).has("GandalfWaveBlastWave")
		and (presented.get("audioEventIds", []) as Array).has("GandalfWizardBlast")
	)

	var battalion_script: GDScript = load("res://src/retail_slice/retail_battalion.gd") as GDScript
	var battalion = battalion_script.new()
	battalion.drawable_fx_controller = controller
	battalion.bind_entering_state_fx_contracts({
		"moduleContracts": [_entering(["DAMAGED"], "FX_TelekinesisAtBone")],
	})
	battalion._sync_entering_state_fx({"conditions": ["DAMAGED"]})
	var enter: Dictionary = battalion.last_drawable_fx_receipt
	_check(
		"EnteringStateFX state entry expands through the shared controller",
		String(enter.get("source", "")) == "typed-drawable-fx-list"
		and _has(enter, "particleSystemIds", "GandalfWaveBlastWave")
		and _has(enter, "audioEventIds", "GandalfWizardBlast")
		and int(controller.cues_presented) == 2
	)
	battalion._sync_entering_state_fx({"conditions": ["DAMAGED"]})
	_check(
		"same-state tick does not re-present the expanded list",
		int(battalion.last_entering_state_fx_receipt.get("applied", -1)) == 0
		and int(controller.cues_presented) == 2
	)

	battalion.bind_fx_event_contracts({
		"moduleContracts": [_fx_event(["MOVING"], 12, "FX_TelekinesisAtBone")],
	})
	battalion._sync_member_fx_events(0, {"conditions": ["MOVING"]}, {
		"frame": 12.0, "previousFrame": 11.0, "lengthFrames": 30.0, "backwards": false,
	})
	var cue: Dictionary = battalion.last_drawable_fx_receipt
	_check(
		"FXEvent clock cross expands through the same controller",
		_has(cue, "particleSystemIds", "GandalfWaveBlastProxy")
		and _has(cue, "audioEventIds", "GandalfWizardBlast")
		and int(controller.cues_presented) == 3
	)

	var unbound = battalion_script.new()
	unbound.bind_entering_state_fx_contracts({
		"moduleContracts": [_entering(["DAMAGED"], "FX_TelekinesisAtBone")],
	})
	unbound._sync_entering_state_fx({"conditions": ["DAMAGED"]})
	_check(
		"unbound controller records the shared-player deferral",
		String(unbound.last_drawable_fx_receipt.get("deferred", ""))
		== "shared-fx-controller-unbound"
		and int(unbound.last_drawable_fx_receipt.get("applied", -1)) == 0
	)
	unbound.free()
	battalion.free()
	controller.free()
	_finish()


func _wizard_blast_bindings() -> Dictionary:
	return {
		"schema": "openbfme.ability-fx-bindings",
		"schemaVersion": 0,
		"authoredFxListIds": ["FX_TelekinesisAtBone"],
		"fxLists": [
			{
				"fxListId": "FX_TelekinesisAtBone",
				"particleSystemIds": [],
				"nestedFxListIds": ["FX_Telekinesis"],
				"audioEventIds": ["GandalfWizardBlast"],
				"resolvedParticleSystemIds": ["GandalfWaveBlastProxy", "GandalfWaveBlastWave"],
			},
		],
		"definitionRegistry": [
			{
				"kind": "FXParticleSystem",
				"definitionId": "GandalfWaveBlastProxy",
				"authoredScalars": {"color1": "R:255 G:255 B:255 0", "lifetime": "35 35"},
			},
			{
				"kind": "FXParticleSystem",
				"definitionId": "GandalfWaveBlastWave",
				"authoredScalars": {
					"isgroundaligned": "Yes",
					"color1": "R:82 G:139 B:235 0",
					"sizerate": "5 10",
				},
			},
		],
		"familyResolution": {"duplicateIdentifierSystemIds": []},
		"presentableFxListIds": ["FX_TelekinesisAtBone"],
		"unresolved": [],
	}


func _entering(conditions: Array, fx_list: String) -> Dictionary:
	return {
		"module": "EnteringStateFX",
		"runtimeStatus": "executable",
		"extraction": "typed",
		"fields": {
			"stateKind": "AnimationState",
			"conditions": {"value": conditions},
			"fxList": {"value": fx_list},
		},
	}


func _fx_event(conditions: Array, frame: int, fx_list: String) -> Dictionary:
	return {
		"module": "FXEvent",
		"runtimeStatus": "executable",
		"extraction": "typed",
		"fields": {
			"stateKind": "AnimationState",
			"conditions": {"value": conditions},
			"frame": {"value": frame},
			"fxList": {"value": fx_list},
			"FireWhenSkipped": {"value": false},
			"skippedCuePolicy": "ignore",
		},
	}


func _has(result: Dictionary, key: String, value: String) -> bool:
	return (result.get(key, []) as Array).has(value)


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error(label)


func _finish() -> void:
	print("DRAWABLE_FX_LIST_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		push_error("check-count mismatch: expected=%d actual=%d" % [EXPECTED_CHECKS, passed + failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)

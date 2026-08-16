extends SceneTree
## Typed TransitionDamageFX fires authored FXList / PSys ids on a stage crossing.
##
## Retail trebuchet / building shape: ReallyDamagedFXList1 = Loc: X:0 Y:0 Z:0
## FXList:FX_GondorTrebuchetTransitionMedium. OCL debris stays deferred.

const Watchdog := preload("res://tests/runner_watchdog.gd")
const TransitionDamageFXScript := preload("res://src/retail_slice/transition_damage_fx.gd")
const EXPECTED_CHECKS := 8

var passed := 0
var failed := 0
var _watchdog := Watchdog.new()


func _initialize() -> void:
	_watchdog.start(self, "TRANSITION_DAMAGE_FX_RUNTIME", 0, 0, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	await process_frame
	_check("TransitionDamageFX consumer loads", TransitionDamageFXScript != null)
	var contracts := [_typed_row([
		{"stage": "Damaged", "kind": "FXList", "fxList": "FX_BasicSevereScreenShake", "loc": {"x": 0.0, "y": 0.0, "z": 0.0}},
		{"stage": "ReallyDamaged", "kind": "FXList", "fxList": "FX_GondorTrebuchetTransitionMedium", "loc": {"x": 0.0, "y": 0.0, "z": 0.0}},
		{"stage": "ReallyDamaged", "kind": "ParticleSystem", "particleSystem": "FireBuildingLarge", "bone": "None", "randomBone": false},
		{"stage": "Rubble", "kind": "FXList", "fxList": "FX_HelmsDeepGateRubble", "loc": {"x": 0.0, "y": 0.0, "z": 0.0}},
	])]
	var intact: Dictionary = TransitionDamageFXScript.select_crossing(contracts, "intact", "intact")
	_check("same-stage crossing is a no-op", int(intact.get("applied", -1)) == 0)
	var damaged: Dictionary = TransitionDamageFXScript.select_crossing(contracts, "intact", "damaged")
	_check("TransitionDamageFX DamagedFXList1 is the intact-to-damaged fire", (damaged.get("fxLists", []) as Array).size() == 1 and String((damaged.get("fxLists", []) as Array)[0].get("id", "")) == "FX_BasicSevereScreenShake")
	_check("typed contract is the source", String(damaged.get("source", "")) == "typed-transition-damage-fx")
	var really: Dictionary = TransitionDamageFXScript.select_crossing(contracts, "damaged", "really-damaged")
	_check("really-damaged crossing fires the authored FXList", String((really.get("fxLists", []) as Array)[0].get("id", "")) == "FX_GondorTrebuchetTransitionMedium")
	_check("really-damaged crossing also names the persistent PSys", String((really.get("particleSystems", []) as Array)[0].get("id", "")) == "FireBuildingLarge")
	var rubble: Dictionary = TransitionDamageFXScript.select_crossing(contracts, "really-damaged", "rubble")
	_check("rubble crossing fires RubbleFXList1", String((rubble.get("fxLists", []) as Array)[0].get("id", "")) == "FX_HelmsDeepGateRubble")
	var deferred := [{
		"module": "TransitionDamageFX",
		"runtimeStatus": "deferred",
		"extraction": "typed",
		"fields": {"deferredFields": [{"name": "ReallyDamagedOCL1"}]},
	}]
	var skipped: Dictionary = TransitionDamageFXScript.select_crossing(deferred, "intact", "really-damaged")
	_check("OCL-only deferred rows do not invent an FXList", int(skipped.get("applied", -1)) == 0)
	_finish()


func _typed_row(effects: Array) -> Dictionary:
	return {
		"module": "TransitionDamageFX",
		"runtimeStatus": "executable",
		"extraction": "typed",
		"fields": {"effects": effects},
	}


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error(label)


func _finish() -> void:
	print("TRANSITION_DAMAGE_FX_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		push_error("check-count mismatch: expected=%d actual=%d" % [EXPECTED_CHECKS, passed + failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)

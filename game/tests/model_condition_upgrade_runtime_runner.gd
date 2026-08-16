extends SceneTree
## Typed ModelConditionUpgrade flags, consumed by the battalion presenter.
##
## Retail gondorfarmer.ini / similar: TriggeredBy + AddConditionFlags.
## AddTempConditionFlag / TempConditionTime / RemoveConditionFlagsInRange stay deferred.

const Watchdog := preload("res://tests/runner_watchdog.gd")
const EXPECTED_CHECKS := 10

var passed := 0
var failed := 0
var _watchdog := Watchdog.new()


func _initialize() -> void:
	_watchdog.start(self, "MODEL_CONDITION_UPGRADE_RUNTIME", 0, 0, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	await process_frame
	await process_frame
	var battalion_script: GDScript = load("res://src/retail_slice/retail_battalion.gd") as GDScript
	_check("battalion consumer loads", battalion_script != null and battalion_script.can_instantiate())
	if battalion_script == null or not battalion_script.can_instantiate():
		_finish()
		return
	var battalion = battalion_script.new()
	battalion.model_condition_upgrade_contracts = [_typed_row(["Upgrade_GondorForgedBlades"], ["USER_1"], [])]
	var applied: Dictionary = battalion.apply_model_condition_upgrades({"Upgrade_GondorForgedBlades": 1})
	_check("ModelConditionUpgrade grants authored USER_1", battalion.model_condition_flags.has("USER_1"))
	_check("typed contract is the source", String(applied.get("source", "")) == "typed-model-condition-upgrade")
	battalion.apply_model_condition_upgrades({})
	_check("removing a non-permanent upgrade clears the flag", not battalion.model_condition_flags.has("USER_1"))
	var permanent_row := _typed_row(["Upgrade_GondorForgedBlades"], ["USER_2"], [])
	permanent_row["fields"]["Permanent"] = {"value": true}
	battalion.model_condition_upgrade_contracts = [permanent_row]
	battalion.apply_model_condition_upgrades({"Upgrade_GondorForgedBlades": 1})
	battalion.apply_model_condition_upgrades({})
	_check("Permanent flags survive upgrade removal", battalion.model_condition_flags.has("USER_2"))
	battalion.model_condition_upgrade_contracts = [_typed_row(["Upgrade_A", "Upgrade_B"], ["USER_3"], [])]
	battalion.model_condition_upgrade_contracts[0]["fields"]["RequiresAllTriggers"] = {"value": true}
	var partial: Dictionary = battalion.apply_model_condition_upgrades({"Upgrade_A": 1})
	_check("RequiresAllTriggers fails closed when only one trigger is present", not (partial.get("flags", []) as Array).has("USER_3"))
	battalion.model_condition_upgrade_contracts = [{
		"module": "ModelConditionUpgrade",
		"runtimeStatus": "deferred",
		"extraction": "typed",
		"fields": {
			"TriggeredBy": {"value": ["Upgrade_TempBanner"]},
			"AddConditionFlags": {"value": ["USER_4"]},
			"deferredFields": [{"name": "AddTempConditionFlag"}],
		},
	}]
	var deferred: Dictionary = battalion.apply_model_condition_upgrades({"Upgrade_TempBanner": 1})
	_check("deferred temp/range rows do not grant flags", not (deferred.get("flags", []) as Array).has("USER_4"))
	var remover = battalion_script.new()
	remover.model_condition_upgrade_contracts = [
		_typed_row(["Upgrade_Grant"], ["USER_5"], []),
		_typed_row(["Upgrade_Clear"], [], ["USER_5"]),
	]
	remover.apply_model_condition_upgrades({"Upgrade_Grant": 1, "Upgrade_Clear": 1})
	_check("RemoveConditionFlags clears a granted flag", not remover.model_condition_flags.has("USER_5"))
	var bound = battalion_script.new()
	bound.bind_model_condition_upgrade_contracts({
		"moduleContracts": [_typed_row(["Upgrade_GondorForgedBlades"], ["USER_1"], [])],
	})
	var from_doc: Dictionary = bound.apply_model_condition_upgrades({"Upgrade_GondorForgedBlades": 1})
	_check("live bind from moduleContracts executes the typed row", String(from_doc.get("source", "")) == "typed-model-condition-upgrade" and bound.model_condition_flags.has("USER_1"))
	bound.model_condition_flags = ["USER_1"]
	var conditions: Array = bound._drawable_conditions_for_state("run")
	_check("upgrade flags union into drawable conditions", conditions.has("MOVING") and conditions.has("USER_1"))
	bound.free()
	remover.free()
	battalion.free()
	_finish()


func _typed_row(triggers: Array, add_flags: Array, remove_flags: Array) -> Dictionary:
	var fields := {
		"TriggeredBy": {"value": triggers},
	}
	if not add_flags.is_empty():
		fields["AddConditionFlags"] = {"value": add_flags}
	if not remove_flags.is_empty():
		fields["RemoveConditionFlags"] = {"value": remove_flags}
	return {
		"module": "ModelConditionUpgrade",
		"runtimeStatus": "executable",
		"extraction": "typed",
		"fields": fields,
	}


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error(label)


func _finish() -> void:
	print("MODEL_CONDITION_UPGRADE_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		push_error("check-count mismatch: expected=%d actual=%d" % [EXPECTED_CHECKS, passed + failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)

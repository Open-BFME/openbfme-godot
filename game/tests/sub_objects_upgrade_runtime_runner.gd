extends SceneTree
## Typed SubObjectsUpgrade show/hide tokens, consumed by the battalion presenter.
##
## Retail trebuchet.ini:525-531 is the closed shape: TriggeredBy + ShowSubObjects.
## HideOnRemove / UpgradeTexture rows stay deferred.

const Watchdog := preload("res://tests/runner_watchdog.gd")
const EXPECTED_CHECKS := 10

var passed := 0
var failed := 0
var _watchdog := Watchdog.new()


func _initialize() -> void:
	_watchdog.start(self, "SUB_OBJECTS_UPGRADE_RUNTIME", 0, 0, true)
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
	var visual := Node3D.new()
	var fire := Node3D.new()
	fire.name = "FirePlane"
	visual.add_child(fire)
	var banner := Node3D.new()
	banner.name = "Banner01"
	visual.add_child(banner)
	var battalion = battalion_script.new()
	battalion.member_count = 1
	battalion.member_visuals[0] = visual
	battalion.sub_object_upgrade_contracts = [_typed_row(["Upgrade_GondorFireStones"], ["FirePlane"], ["Banner01"])]
	fire.visible = false
	banner.visible = true
	var applied: Dictionary = battalion.apply_sub_object_upgrades({"Upgrade_GondorFireStones": 1})
	_check("SubObjectsUpgrade shows authored FirePlane", fire.visible)
	_check("SubObjectsUpgrade hides authored Banner01", not banner.visible)
	_check("typed contract is the source, not the FirePlane name guess", String(applied.get("source", "")) == "typed-sub-objects-upgrade")
	_check("matched tokens preserve authored spelling", (applied.get("show", []) as Array) == ["FirePlane"])
	battalion.apply_sub_object_upgrades({})
	_check("removing the upgrade restores the hidden FirePlane", not fire.visible)
	_check("removing the upgrade restores the shown Banner01", banner.visible)
	battalion.sub_object_upgrade_contracts = [{
		"module": "SubObjectsUpgrade",
		"runtimeStatus": "deferred",
		"extraction": "typed",
		"fields": {
			"TriggeredBy": {"value": ["Upgrade_TextureOnly"]},
			"ShowSubObjects": {"value": ["Helm"]},
			"deferredFields": [{"name": "UpgradeTexture"}],
		},
	}]
	var deferred: Dictionary = battalion.apply_sub_object_upgrades({"Upgrade_TextureOnly": 1})
	_check("deferred HideOnRemove/texture rows do not flip meshes", int(deferred.get("applied", 0)) == 0)
	var bound = battalion_script.new()
	bound.member_count = 1
	bound.member_visuals[0] = visual
	bound.bind_sub_object_upgrade_contracts({
		"moduleContracts": [_typed_row(["Upgrade_GondorFireStones"], ["FirePlane"], ["Banner01"])],
	})
	fire.visible = false
	banner.visible = true
	var from_doc: Dictionary = bound.apply_sub_object_upgrades({"Upgrade_GondorFireStones": 1})
	_check("live bind from moduleContracts executes the typed row", String(from_doc.get("source", "")) == "typed-sub-objects-upgrade" and fire.visible)
	bound.sub_object_upgrade_contracts = [_typed_row(["Upgrade_A", "Upgrade_B"], ["FirePlane"], [])]
	bound.sub_object_upgrade_contracts[0]["fields"]["RequiresAllTriggers"] = {"value": true}
	fire.visible = false
	var partial: Dictionary = bound.apply_sub_object_upgrades({"Upgrade_A": 1})
	_check("RequiresAllTriggers fails closed when only one trigger is present", not fire.visible and int(partial.get("applied", 0)) == 0)
	bound.free()
	battalion.free()
	visual.free()
	_finish()


func _typed_row(triggers: Array, show: Array, hide: Array) -> Dictionary:
	return {
		"module": "SubObjectsUpgrade",
		"runtimeStatus": "executable",
		"extraction": "typed",
		"fields": {
			"TriggeredBy": {"value": triggers},
			"ShowSubObjects": {"value": show},
			"HideSubObjects": {"value": hide},
		},
	}


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error(label)


func _finish() -> void:
	print("SUB_OBJECTS_UPGRADE_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		push_error("check-count mismatch: expected=%d actual=%d" % [EXPECTED_CHECKS, passed + failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)

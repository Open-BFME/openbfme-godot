extends SceneTree
const GAME := "bfme2"
const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var content_db = root.get_node_or_null("ContentDB")
	if content_db == null:
		_fail("ContentDB autoload is missing")
		_finish()
		return
	content_db.scenario_prop_runtimes.clear()
	content_db.scenario_prop_runtimes[GAME] = {}
	var regular_units_before: Dictionary = content_db.playable_unit_runtimes.duplicate(true)
	var regular_structures_before: Dictionary = content_db.playable_structure_runtimes.duplicate(true)
	var unit_index_before: Dictionary = content_db.playable_unit_runtime_member_index.duplicate(true)
	var bundle_before: Dictionary = content_db.bundle_objects.duplicate(true)
	var animation_before: Dictionary = content_db.animation_capabilities.duplicate(true)

	var prop := _prop_document("RockBigTroll")
	_check(content_db._validate_scenario_prop_runtime(prop), "valid neutral prop descriptor validates")
	var projected := Adapter.module_contracts(prop)
	_check(projected.size() == 1 and String((projected[0] as Dictionary).get("module", "")) == "BezierProjectileBehavior", "adapter projects top-level neutral prop contract")
	var trajectory := Adapter.bezier_trajectory_contract(prop)
	_check(String(trajectory.get("activation", "")) == "conditional-explicit-runtime-request", "bezier trajectory requires explicit activation")
	_check(String((trajectory.get("trajectory", {}) as Dictionary).get("progressAuthority", "")) == "external-authored-projectile-flight", "bezier trajectory preserves external flight authority")
	_check(not bool((projected[0] as Dictionary).get("executable", true)), "deferred Bezier row is not promoted executable")
	var drifted_graph := prop.duplicate(true)
	var drifted_contract := (drifted_graph.moduleContracts as Array)[0] as Dictionary
	var drifted_effect_graph := drifted_contract.effectGraph as Dictionary
	var drifted_trajectory := drifted_effect_graph.trajectory as Dictionary
	drifted_trajectory.firstHeight = 9.0
	_check(not content_db._validate_scenario_prop_runtime(drifted_graph), "Bezier graph tamper fails closed")
	var missing_evidence := prop.duplicate(true)
	missing_evidence.runtimeModuleEvidence = []
	_check(not content_db._validate_scenario_prop_runtime(missing_evidence), "Bezier lineage evidence omission fails closed")
	(content_db.scenario_prop_runtimes[GAME] as Dictionary)[prop.objectId] = prop
	_check(String(content_db.get_scenario_prop_runtime(GAME, "RockBigTroll", "map-placement").get("objectId", "")) == "RockBigTroll", "prop resolves by exact identity and authored surface")
	_check(String(content_db.get_scenario_prop_runtime(GAME, "rockbigtroll", "script-spawn").get("objectId", "")) == "RockBigTroll", "prop identity lookup is case insensitive")
	_check(not content_db.get_scenario_prop_runtime(GAME, "RockBigTroll", "object-creation-list").is_empty(), "prop resolves through authored object creation surface")
	_check(content_db.get_scenario_prop_runtime(GAME, "RockBigTroll", "lair-spawn").is_empty(), "prop rejects non-prop scenario surface")
	_check(content_db.get_scenario_prop_runtime(GAME, "RockBigTroll", "construct").is_empty(), "prop rejects production surface")

	var malformed := prop.duplicate(true)
	malformed.objectId = "SpiderWebs01"
	malformed.requestedObjectId = "SpiderWebs01"
	(malformed.scenarioAdmission as Dictionary).surfaces = ["map-placement", "map-placement", "object-creation-list"]
	(content_db.scenario_prop_runtimes[GAME] as Dictionary)[malformed.objectId] = malformed
	_check(content_db.get_scenario_prop_runtime(GAME, "SpiderWebs01", "map-placement").is_empty(), "malformed prop fails closed at lookup")
	var active := _prop_document("SpiderWebs02")
	(active.kindOf as Dictionary).effective.append("INFANTRY")
	_check(not content_db._validate_scenario_prop_runtime(active), "active unit KindOf cannot enter passive prop registry")

	var fixture_root := "user://scenario-prop-runtime-registry-fixture"
	content_db.neutral_pack_receipts[GAME] = {"game": GAME, "_pack_root": fixture_root}
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(fixture_root + "/data/neutral-props"))
	var loaded := _prop_document("SpiderWebs03")
	var invalid := _prop_document("SpiderWebs04")
	invalid.production = [{"surface": "construct"}]
	_write_json(fixture_root + "/data/neutral-props/valid.json", loaded)
	_write_json(fixture_root + "/data/neutral-props/invalid.json", invalid)
	_check(content_db._load_scenario_prop_runtimes(fixture_root, {
		"neutralProp.valid": "data/neutral-props/valid.json",
		"neutralProp.invalid": "data/neutral-props/invalid.json",
	}), "prop loader accepts mixed valid/invalid delta")
	_check(not content_db.get_scenario_prop_runtime(GAME, "SpiderWebs03", "map-placement").is_empty(), "prop loader publishes valid descriptor")
	_check(content_db.get_scenario_prop_runtime(GAME, "SpiderWebs04", "map-placement").is_empty(), "prop loader skips malformed descriptor")
	_check(content_db.playable_unit_runtimes == regular_units_before, "prop loader does not leak into playable unit registry")
	_check(content_db.playable_structure_runtimes == regular_structures_before, "prop loader does not leak into playable structure registry")
	_check(content_db.playable_unit_runtime_member_index == unit_index_before, "prop loader does not leak into member projection")
	_check(content_db.bundle_objects == bundle_before and content_db.animation_capabilities == animation_before, "prop loader does not leak into presentation or HUD projections")
	content_db.scenario_prop_runtimes.clear()
	_finish()


func _prop_document(object_id: String) -> Dictionary:
	var document := {
		"schema": "openbfme.neutral-prop-descriptor",
		"schemaVersion": 0,
		"game": "bfme2",
		"requestedObjectId": object_id,
		"objectId": object_id,
		"declarationKind": "Object",
		"parentObjectId": "",
		"inheritance": [{"objectId": object_id, "virtualPath": "data/ini/object/nature/props.ini", "semanticSha256": "1".repeat(64)}],
		"kindOf": {"authored": ["IMMOBILE", "INERT"], "effective": ["IMMOBILE", "INERT"], "defineProvenance": []},
		"moduleContracts": [],
		"geometry": null,
		"geometryContactPoints": [],
		"publicBones": [],
		"presentation": {
			"drawModules": [{"moduleKind": "W3DModelDraw", "moduleTag": "ModuleTag_Draw"}],
			"sourceReferences": {"model": [{"id": "MURockTroll"}]},
		},
		"production": [],
		"scenarioAdmission": {
			"kind": "authored-passive-prop",
			"surfaces": ["map-placement", "script-spawn", "object-creation-list"],
			"buildCommandExposed": false,
			"evidence": "bounded-retail-neutral-prop-family",
		},
		"sourceDocuments": [{"virtualPath": "data/ini/object/nature/props.ini", "semanticSha256": "2".repeat(64)}],
		"descriptorSha256": "3".repeat(64),
	}
	if object_id == "RockBigTroll":
		document.moduleContracts = [_bezier_contract()]
		document.runtimeModuleEvidence = [{
			"kind": "BezierProjectileBehavior",
			"instanceTag": "ModuleTag_03",
			"sourceIni": "data/ini/object/nature/props.ini",
			"line": 378,
			"semanticSha256": "4".repeat(64),
			"consumed": false,
		}]
		document.runtimeCapabilities = [{
			"kind": "projectile-capable",
			"activation": "authored-projectile-launch",
			"runtimeStatus": "deferred",
			"moduleEvidence": document.runtimeModuleEvidence[0].duplicate(true),
		}]
	return document


func _bezier_contract() -> Dictionary:
	var source := "data/ini/object/nature/props.ini"
	var fields := {
		"FirstHeight": {"authored": "8", "value": 8, "sourceIni": source, "line": 379},
		"SecondHeight": {"authored": "0", "value": 0, "sourceIni": source, "line": 380},
		"FirstPercentIndent": {"authored": "43%", "percent": 43.0, "ratio": 0.43, "sourceIni": source, "line": 381},
		"SecondPercentIndent": {"authored": "86%", "percent": 86.0, "ratio": 0.86, "sourceIni": source, "line": 382},
		"DetonateCallsKill": {"authored": "Yes", "value": true, "sourceIni": source, "line": 383},
		"PreLandingStateTime": {"authored": "1000", "value": 1000, "sourceIni": source, "line": 384},
	}
	return {
		"module": "BezierProjectileBehavior",
		"fields": fields,
		"runtimeStatus": "deferred",
		"extraction": "typed",
		"carrier": "Behavior",
		"sourceIni": source,
		"line": 378,
		"tag": "ModuleTag_03",
		"effectGraph": {
			"kind": "bezier-projectile",
			"trajectory": {
				"kind": "cubic-bezier-envelope",
				"runtimeStatus": "executable",
				"firstHeight": 8.0,
				"secondHeight": 0.0,
				"firstIndentRatio": 0.43,
				"secondIndentRatio": 0.86,
				"progressAuthority": "external-authored-projectile-flight",
			},
			"executionEligibility": {
				"runtimeStatus": "deferred",
				"blockers": ["impact", "kill", "prelanding"],
			},
		},
	}


func _write_json(path: String, value: Dictionary) -> void:
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		_fail("cannot write synthetic fixture %s" % path)
		return
	file.store_string(JSON.stringify(value, "  ") + "\n")
	file.close()


func _check(condition: bool, label: String) -> void:
	if condition:
		passed += 1
	else:
		_fail(label)


func _fail(label: String) -> void:
	failed += 1
	push_error("SCENARIO_PROP_RUNTIME_REGISTRY_FAIL: %s" % label)


func _finish() -> void:
	if failed == 0:
		print("SCENARIO_PROP_RUNTIME_REGISTRY_OK passed=%d" % passed)
		quit(0)
	else:
		print("SCENARIO_PROP_RUNTIME_REGISTRY_FAIL passed=%d failed=%d" % [passed, failed])
		quit(1)

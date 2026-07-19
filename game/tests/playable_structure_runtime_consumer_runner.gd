extends SceneTree
## Focused fail-closed gate for the generic playable-structure runtime registry.
## Mirrors playable_unit_runtime_consumer_runner.gd: synthetic fixture packs
## only, no retail content, and every malformed delta must reject atomically.

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
	var pack_root := ProjectSettings.globalize_path("user://playable-structure-runtime-consumer-fixture")
	_build_fixture(pack_root)
	content_db.playable_structure_runtimes.clear()
	if not content_db.pack_roots.has(pack_root):
		content_db.pack_roots.append(pack_root)

	var declared := {"playableStructure.fixturemonsterpen": "data/playable-structures/fixturemonsterpen.json"}
	_check(content_db._load_playable_structure_runtimes(pack_root, declared), "generic structure declaration loads")
	var all_runtimes: Dictionary = content_db.get_playable_structure_runtimes()
	_check(all_runtimes.size() == 1 and all_runtimes.has("FixtureMonsterPen"), "runtime indexes by source object id")
	var document: Dictionary = content_db.get_playable_structure_runtime("FixtureMonsterPen")
	_check(String(document.get("_pack_file_key", "")) == "playableStructure.fixturemonsterpen", "pack declaration identity retained")
	_check(String(document.get("_pack_root", "")) == pack_root, "pack root identity retained")
	var lifecycle: Dictionary = ((document.get("registration", {}) as Dictionary).get("presentation", {}) as Dictionary).get("buildingLifecycle", {}) as Dictionary
	_check(int((lifecycle.get("simulationFacts", {}) as Dictionary).get("maxHealth", 0)) == 3000, "simulation facts survive the load")

	# Atomicity: one malformed doc rejects the pack's whole structure delta.
	var malformed := _fixture_document("BrokenPen", "brokenpen")
	malformed["registration"] = {}
	_write_json(pack_root.path_join("data/playable-structures/broken.json"), malformed)
	var before: Dictionary = content_db.get_playable_structure_runtimes()
	_check(not content_db._load_playable_structure_runtimes(pack_root, {
		"playableStructure.second": "data/playable-structures/fixturemonsterpen2.json",
		"playableStructure.broken": "data/playable-structures/broken.json",
	}), "malformed pack delta fails closed")
	_check(content_db.get_playable_structure_runtimes() == before, "failed delta is atomic")

	# Cross-pack casefolded id collision with a different spelling is rejected.
	var collision := _fixture_document("FIXTUREMonsterPen", "fixturemonsterpen")
	_write_json(pack_root.path_join("data/playable-structures/collision.json"), collision)
	_check(not content_db._load_playable_structure_runtimes(pack_root, {
		"playableStructure.collision": "data/playable-structures/collision.json",
	}), "casefolded object-id collision fails closed")
	_check(content_db.get_playable_structure_runtimes() == before, "collision rejection is atomic")

	_check_rejected_variant(content_db, pack_root, "digest_format", func(broken: Dictionary) -> void:
		broken["runtimeSha256"] = "zz".repeat(32))
	_check_rejected_variant(content_db, pack_root, "wrong_slug", func(broken: Dictionary) -> void:
		broken["slug"] = "fixture-monster-pen")
	_check_rejected_variant(content_db, pack_root, "missing_visual_asset", func(broken: Dictionary) -> void:
		var phases: Array = _lifecycle_of(broken).get("phases", [])
		(phases[0] as Dictionary)["visual"] = "assets/models/structures/fixturemonsterpen/absent.glb")
	_check_rejected_variant(content_db, pack_root, "phase_order", func(broken: Dictionary) -> void:
		var broken_lifecycle := _lifecycle_of(broken)
		var phases: Array = broken_lifecycle.get("phases", [])
		phases.reverse()
		(phases[0] as Dictionary)["nextPhase"] = "intact"
		var last := phases[phases.size() - 1] as Dictionary
		last.erase("nextPhase")
		broken_lifecycle["phaseCoverage"] = {
			"covered": ["damaged", "intact"],
			"missing": ["construction", "really-damaged", "rubble", "post-rubble"],
		})
	_check_rejected_variant(content_db, pack_root, "coverage_mismatch", func(broken: Dictionary) -> void:
		_lifecycle_of(broken)["phaseCoverage"] = {"covered": ["intact"], "missing": []})
	_check_rejected_variant(content_db, pack_root, "facts_health_drift", func(broken: Dictionary) -> void:
		(_lifecycle_of(broken).get("simulationFacts", {}) as Dictionary)["maxHealth"] = 4000)
	_check_rejected_variant(content_db, pack_root, "construct_evidence_without_routes", func(broken: Dictionary) -> void:
		((broken.get("registration", {}) as Dictionary).get("production", {}) as Dictionary)["routes"] = [])
	_check_rejected_variant(content_db, pack_root, "damage_rule_drift", func(broken: Dictionary) -> void:
		((_lifecycle_of(broken).get("simulationFacts", {}) as Dictionary).get("damageStateRule", {}) as Dictionary)["damagedThreshold"] = 2500)

	_finish()


func _lifecycle_of(document: Dictionary) -> Dictionary:
	return ((document.get("registration", {}) as Dictionary).get("presentation", {}) as Dictionary).get("buildingLifecycle", {}) as Dictionary


func _check_rejected_variant(content_db, pack_root: String, label: String, mutate: Callable) -> void:
	var broken := _fixture_document("VariantPen", "variantpen")
	mutate.call(broken)
	_write_json(pack_root.path_join("data/playable-structures/variant.json"), broken)
	var before: Dictionary = content_db.get_playable_structure_runtimes()
	_check(not content_db._load_playable_structure_runtimes(pack_root, {
		"playableStructure.variant": "data/playable-structures/variant.json",
	}), "%s variant fails closed" % label)
	_check(content_db.get_playable_structure_runtimes() == before, "%s rejection is atomic" % label)


func _build_fixture(pack_root: String) -> void:
	DirAccess.make_dir_recursive_absolute(pack_root.path_join("data/playable-structures"))
	DirAccess.make_dir_recursive_absolute(pack_root.path_join("assets/models/structures/fixturemonsterpen"))
	DirAccess.make_dir_recursive_absolute(pack_root.path_join("assets/models/structures/variantpen"))
	for relative in [
		"assets/models/structures/fixturemonsterpen/intact.glb",
		"assets/models/structures/fixturemonsterpen/damaged.glb",
		"assets/models/structures/variantpen/intact.glb",
		"assets/models/structures/variantpen/damaged.glb",
	]:
		_write_bytes(pack_root.path_join(relative), PackedByteArray([7, 8, 9]))
	_write_json(pack_root.path_join("data/playable-structures/fixturemonsterpen.json"), _fixture_document("FixtureMonsterPen", "fixturemonsterpen"))
	_write_json(pack_root.path_join("data/playable-structures/fixturemonsterpen2.json"), _fixture_document("SecondPen", "secondpen", "fixturemonsterpen"))


func _fixture_document(object_id: String, slug: String, asset_slug: String = "") -> Dictionary:
	var model_slug := asset_slug if asset_slug != "" else ("variantpen" if slug == "variantpen" else "fixturemonsterpen")
	return {
		"schema": "openbfme.playable-structure-runtime",
		"schemaVersion": 0,
		"objectId": object_id,
		"slug": slug,
		"descriptorSha256": "1".repeat(64),
		"recipeSha256": "2".repeat(64),
		"runtimeSha256": "3".repeat(64),
		"registration": {
			"production": {
				"evidence": "authored-construct-command",
				"routes": [{
					"surface": "construct",
					"commandId": "Command_Construct%s" % object_id,
					"commandKind": "DOZER_CONSTRUCT",
					"builderObjectId": "FixturePorter",
					"commandSetId": "FixturePorterCommandSet",
					"slot": 2,
					"prerequisites": [],
				}],
			},
			"gameplay": {
				"health": {
					"primary": {
						"module": "StructureBody ModuleTag_01",
						"sourceIni": "data/ini/object/fixture.ini",
						"line": 10,
						"maxHealth": {"authored": "3000", "value": 3000},
						"maxHealthDamaged": {"authored": "2000", "value": 2000},
						"maxHealthReallyDamaged": {"authored": "1000", "value": 1000},
					},
					"evidence": [],
				},
				"trainedCommandSets": [{
					"id": "%sCommandSet" % object_id,
					"kind": "direct",
					"slots": [{"slot": 1, "commandId": "Command_ConstructFixtureMonster"}],
				}],
				"scalarFields": {
					"BuildCost": {"expression": "300", "sourceIni": "data/ini/object/fixture.ini", "line": 5},
					"BuildTime": {"expression": "30.0", "sourceIni": "data/ini/object/fixture.ini", "line": 6},
				},
			},
			"presentation": {
				"buildingLifecycle": {
					"schema": "openbfme.building-lifecycle-presentation",
					"schemaVersion": 1,
					"phases": [
						{
							"phase": "intact",
							"visual": "assets/models/structures/%s/intact.glb" % model_slug,
							"resourceId": "structure:%s:intact" % slug,
							"animations": [],
							"nextPhase": "damaged",
						},
						{
							"phase": "damaged",
							"visual": "assets/models/structures/%s/damaged.glb" % model_slug,
							"resourceId": "structure:%s:damaged" % slug,
							"animations": ["fixture_dmg.w3d"],
						},
					],
					"phaseCoverage": {
						"covered": ["intact", "damaged"],
						"missing": ["construction", "really-damaged", "rubble", "post-rubble"],
					},
					"simulationFacts": {
						"maxHealth": 3000,
						"damageStateRule": {"damagedThreshold": 2000, "reallyDamagedThreshold": 1000},
					},
				},
				"ui": {"DisplayName": {"expression": "OBJECT:%s" % object_id, "sourceIni": "data/ini/object/fixture.ini", "line": 2}},
				"audioRoutes": {},
			},
			"unsupportedVisualReferences": [],
		},
	}


func _write_json(path: String, value: Dictionary) -> void:
	var file := FileAccess.open(path, FileAccess.WRITE)
	file.store_string(JSON.stringify(value, "  ") + "\n")
	file.close()


func _write_bytes(path: String, value: PackedByteArray) -> void:
	var file := FileAccess.open(path, FileAccess.WRITE)
	file.store_buffer(value)
	file.close()


func _check(condition: bool, label: String) -> void:
	if condition:
		passed += 1
	else:
		_fail(label)


func _fail(label: String) -> void:
	failed += 1
	push_error("PLAYABLE_STRUCTURE_RUNTIME_CONSUMER_FAIL %s" % label)


func _finish() -> void:
	if failed == 0:
		print("PLAYABLE_STRUCTURE_RUNTIME_CONSUMER_OK passed=%d failed=0" % passed)
		quit(0)
	else:
		print("PLAYABLE_STRUCTURE_RUNTIME_CONSUMER_RESULT passed=%d failed=%d" % [passed, failed])
		quit(1)

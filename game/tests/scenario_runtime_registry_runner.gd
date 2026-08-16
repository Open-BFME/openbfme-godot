extends SceneTree
const GAME := "rotwk"

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
	var source_unit: Dictionary = _first_document(content_db.playable_unit_runtimes)
	var source_structure: Dictionary = _first_document(content_db.playable_structure_runtimes)
	_check(not source_unit.is_empty(), "loaded content provides a validated unit fixture")
	_check(not source_structure.is_empty(), "loaded content provides a validated structure fixture")
	content_db.playable_unit_runtimes.clear()
	content_db.playable_unit_runtime_pack_index.clear()
	content_db.playable_unit_runtime_member_index.clear()
	content_db.playable_structure_runtimes.clear()
	content_db.bundle_objects.clear()
	content_db.animation_capabilities.clear()
	content_db.scenario_unit_runtimes.clear()
	content_db.scenario_structure_runtimes.clear()
	content_db.scenario_unit_runtimes[GAME] = {}
	content_db.scenario_structure_runtimes[GAME] = {}

	var unit := _unit_document()
	var unit_admission := (unit.registration as Dictionary).scenarioAdmission as Dictionary
	for role in ["inheritance-template", "scenario-only", "creature", "horde", "summoned-hero"]:
		var role_admission := unit_admission.duplicate(true)
		role_admission.role = role
		_check(content_db._validate_playable_unit_scenario_admission(role_admission, "monster"), "unit role %s validates" % role)
	_check(content_db._validate_playable_unit_scenario_admission(unit_admission, "monster"), "non-naval scenario unit validates")
	(content_db.scenario_unit_runtimes[GAME] as Dictionary)[unit.objectId] = unit
	_check(String(content_db.get_scenario_unit_runtime(GAME, "NeutralWolf", "lair-spawn").get("objectId", "")) == "NeutralWolf", "scenario unit resolves by exact identity and authored surface")
	_check(String(content_db.get_scenario_unit_runtime(GAME, "neutralwolf", "map-placement").get("objectId", "")) == "NeutralWolf", "scenario unit identity lookup is case insensitive")
	_check(content_db.get_scenario_unit_runtime(GAME, "NeutralWolf", "tutorial-script").is_empty(), "scenario unit rejects an allowed but unauthored surface")
	_check(content_db.get_scenario_unit_runtime(GAME, "NeutralWolf", "command-socket").is_empty(), "scenario unit rejects a production surface")
	_check(((unit.registration as Dictionary).production as Array).is_empty(), "scenario unit production stays empty")
	_check(content_db.get_playable_unit_runtime("NeutralWolf").is_empty(), "scenario unit does not leak into playable registry")
	_check(content_db.playable_unit_runtime_member_index.is_empty() and content_db.bundle_objects.is_empty() and content_db.animation_capabilities.is_empty(), "scenario unit does not leak into projection registries")

	var malformed_unit := unit.duplicate(true)
	malformed_unit.objectId = "MalformedWolf"
	((malformed_unit.registration as Dictionary).scenarioAdmission as Dictionary).surfaces = ["lair-spawn", "lair-spawn"]
	(content_db.scenario_unit_runtimes[GAME] as Dictionary)["MalformedWolf"] = malformed_unit
	_check(content_db.get_scenario_unit_runtime(GAME, "MalformedWolf", "lair-spawn").is_empty(), "malformed scenario unit fails closed")
	var produced_unit := unit.duplicate(true)
	produced_unit.objectId = "ProducedWolf"
	(produced_unit.registration as Dictionary).production = [{"surface": "command-socket"}]
	(content_db.scenario_unit_runtimes[GAME] as Dictionary)["ProducedWolf"] = produced_unit
	_check(content_db.get_scenario_unit_runtime(GAME, "ProducedWolf", "lair-spawn").is_empty(), "scenario unit with production fails closed")

	var structure := _structure_document()
	var structure_registration := structure.registration as Dictionary
	_check(content_db._validate_playable_structure_production(structure_registration.production), "authored-neutral-map production evidence validates only with empty routes")
	_check(content_db._validate_playable_structure_scenario_admission(structure_registration.scenarioAdmission), "neutral structure admission validates")
	(content_db.scenario_structure_runtimes[GAME] as Dictionary)[structure.objectId] = structure
	_check(String(content_db.get_scenario_structure_runtime(GAME, "NeutralLair", "lair-spawn").get("objectId", "")) == "NeutralLair", "scenario structure resolves by exact identity and authored surface")
	_check(String(content_db.get_scenario_structure_runtime(GAME, "neutrallair", "map-placement").get("objectId", "")) == "NeutralLair", "scenario structure identity lookup is case insensitive")
	_check(content_db.get_scenario_structure_runtime(GAME, "NeutralLair", "object-creation-list").is_empty(), "scenario structure rejects an allowed but unauthored surface")
	_check(content_db.get_scenario_structure_runtime(GAME, "NeutralLair", "construct").is_empty(), "scenario structure rejects production surface")
	_check((structure_registration.production as Dictionary).routes.is_empty(), "scenario structure production routes stay empty")
	_check(content_db.get_playable_structure_runtime("NeutralLair").is_empty(), "scenario structure does not leak into playable registry")

	var malformed_structure := structure.duplicate(true)
	malformed_structure.objectId = "MalformedLair"
	((malformed_structure.registration as Dictionary).scenarioAdmission as Dictionary).role = "fortress"
	(content_db.scenario_structure_runtimes[GAME] as Dictionary)["MalformedLair"] = malformed_structure
	_check(content_db.get_scenario_structure_runtime(GAME, "MalformedLair", "lair-spawn").is_empty(), "malformed scenario structure fails closed")
	var routed_production := (structure_registration.production as Dictionary).duplicate(true)
	routed_production.routes = [{"surface": "construct"}]
	_check(not content_db._validate_playable_structure_production(routed_production), "authored-neutral-map rejects construct routes")

	if not source_unit.is_empty() and not source_structure.is_empty():
		_run_loader_seam(content_db, source_unit, source_structure)

	content_db.scenario_unit_runtimes.clear()
	content_db.scenario_structure_runtimes.clear()
	_finish()


func _run_loader_seam(content_db: Node, source_unit: Dictionary, source_structure: Dictionary) -> void:
	var fixture_root := "user://scenario-runtime-registry-fixture"
	content_db.neutral_pack_receipts[GAME] = {"game": GAME, "_pack_root": fixture_root}
	var source_unit_root := String(source_unit.get("_pack_root", ""))
	var source_structure_root := String(source_structure.get("_pack_root", ""))
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(fixture_root + "/data/playable-units"))
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(fixture_root + "/data/playable-structures"))

	var loaded_unit := source_unit.duplicate(true)
	loaded_unit.erase("_source")
	loaded_unit.erase("_pack_root")
	loaded_unit.erase("_pack_file_key")
	var loaded_unit_registration := loaded_unit.registration as Dictionary
	loaded_unit_registration.production = []
	loaded_unit_registration.scenarioAdmission = (_unit_document().registration as Dictionary).scenarioAdmission
	_check(content_db._validate_playable_unit_runtime(source_unit_root, loaded_unit), "synthetic scenario unit remains a complete runtime document")
	_stage_document_assets(content_db, loaded_unit, source_unit_root, fixture_root)
	var invalid_unit := loaded_unit.duplicate(true)
	invalid_unit.objectId = "MalformedSyntheticNeutralUnit"
	((invalid_unit.registration as Dictionary).scenarioAdmission as Dictionary).surfaces = ["map-placement", "construct"]
	_write_json(fixture_root + "/data/playable-units/valid.json", loaded_unit)
	_write_json(fixture_root + "/data/playable-units/invalid.json", invalid_unit)
	_check(content_db._load_playable_unit_runtimes(fixture_root, {
		"playableUnit.valid": "data/playable-units/valid.json",
		"playableUnit.invalid": "data/playable-units/invalid.json",
	}), "scenario unit loader accepts a mixed valid/invalid delta")
	var loaded_unit_id := String(loaded_unit.objectId)
	_check(not content_db.get_scenario_unit_runtime(GAME, loaded_unit_id, "lair-spawn").is_empty(), "unit loader publishes valid scenario document")
	_check(content_db.get_playable_unit_runtime(loaded_unit_id).is_empty(), "unit loader keeps scenario document out of playable registry")
	_check(content_db.get_scenario_unit_runtime(GAME, "MalformedSyntheticNeutralUnit", "map-placement").is_empty(), "unit loader skips malformed scenario document")
	_check(content_db.playable_unit_runtime_member_index.is_empty() and content_db.bundle_objects.is_empty() and content_db.animation_capabilities.is_empty(), "unit loader creates no regular projection for scenario document")

	var loaded_structure := source_structure.duplicate(true)
	loaded_structure.erase("_source")
	loaded_structure.erase("_pack_root")
	loaded_structure.erase("_pack_file_key")
	var loaded_structure_registration := loaded_structure.registration as Dictionary
	loaded_structure_registration.production = {"evidence": "authored-neutral-map", "routes": []}
	loaded_structure_registration.scenarioAdmission = (_structure_document().registration as Dictionary).scenarioAdmission
	_check(content_db._validate_playable_structure_runtime(source_structure_root, loaded_structure), "synthetic scenario structure remains a complete runtime document")
	_stage_document_assets(content_db, loaded_structure, source_structure_root, fixture_root)
	var invalid_structure := loaded_structure.duplicate(true)
	invalid_structure.objectId = "MalformedSyntheticNeutralStructure"
	invalid_structure.slug = "malformed-synthetic-neutral-structure"
	((invalid_structure.registration as Dictionary).scenarioAdmission as Dictionary).role = "fortress"
	_write_json(fixture_root + "/data/playable-structures/valid.json", loaded_structure)
	_write_json(fixture_root + "/data/playable-structures/invalid.json", invalid_structure)
	_check(content_db._load_playable_structure_runtimes(fixture_root, {
		"playableStructure.valid": "data/playable-structures/valid.json",
		"playableStructure.invalid": "data/playable-structures/invalid.json",
	}), "scenario structure loader accepts a mixed valid/invalid delta")
	var loaded_structure_id := String(loaded_structure.objectId)
	_check(not content_db.get_scenario_structure_runtime(GAME, loaded_structure_id, "lair-spawn").is_empty(), "structure loader publishes valid scenario document")
	_check(content_db.get_playable_structure_runtime(loaded_structure_id).is_empty(), "structure loader keeps scenario document out of playable registry")
	_check(content_db.get_scenario_structure_runtime(GAME, "MalformedSyntheticNeutralStructure", "map-placement").is_empty(), "structure loader skips malformed scenario document")


func _first_document(registry: Dictionary) -> Dictionary:
	var keys := registry.keys()
	keys.sort_custom(func(a: Variant, b: Variant) -> bool: return String(a).naturalnocasecmp_to(String(b)) < 0)
	for key_value in keys:
		var value: Variant = registry[key_value]
		if typeof(value) == TYPE_DICTIONARY:
			return (value as Dictionary).duplicate(true)
	return {}


func _write_json(path: String, value: Dictionary) -> void:
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		_fail("cannot write synthetic fixture %s" % path)
		return
	file.store_string(JSON.stringify(value, "  ") + "\n")
	file.close()


func _stage_document_assets(content_db: Node, value: Variant, source_root: String, fixture_root: String) -> void:
	if typeof(value) == TYPE_DICTIONARY:
		for child in (value as Dictionary).values():
			_stage_document_assets(content_db, child, source_root, fixture_root)
		return
	if typeof(value) == TYPE_ARRAY:
		for child in value as Array:
			_stage_document_assets(content_db, child, source_root, fixture_root)
		return
	if typeof(value) != TYPE_STRING:
		return
	var relative := String(value).replace("\\", "/")
	if not relative.begins_with("assets/"):
		return
	var source: String = content_db.resolve_asset(relative, source_root)
	if source == "":
		return
	var target := fixture_root + "/" + relative
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(target.get_base_dir()))
	var error := DirAccess.copy_absolute(ProjectSettings.globalize_path(source), ProjectSettings.globalize_path(target))
	if error != OK:
		_fail("cannot stage synthetic fixture asset %s" % relative)


func _unit_document() -> Dictionary:
	return {
		"objectId": "NeutralWolf",
		"category": "monster",
		"registration": {
			"production": [],
			"scenarioAdmission": {
				"kind": "authored-non-buildable",
				"role": "creature",
				"surfaces": ["map-placement", "script-spawn", "object-creation-list", "lair-spawn"],
				"buildCommandExposed": false,
				"evidence": "no-authored-unit-build-route",
				"sourceIni": "data/ini/object/neutral/wolf.ini",
				"line": 17,
				"declarationKind": "Object",
			},
		},
	}


func _structure_document() -> Dictionary:
	return {
		"objectId": "NeutralLair",
		"registration": {
			"production": {"evidence": "authored-neutral-map", "routes": []},
			"scenarioAdmission": {
				"kind": "authored-neutral-non-buildable",
				"role": "lair",
				"surfaces": ["map-placement", "script-spawn", "lair-spawn"],
				"buildCommandExposed": false,
				"evidence": "no-authored-construct-route",
				"sourceIni": "data/ini/object/neutral/lair.ini",
				"line": 31,
				"declarationKind": "Object",
			},
		},
	}


func _check(condition: bool, label: String) -> void:
	if condition:
		passed += 1
	else:
		_fail(label)


func _fail(label: String) -> void:
	failed += 1
	push_error("SCENARIO_RUNTIME_REGISTRY_FAIL: %s" % label)


func _finish() -> void:
	if failed == 0:
		print("SCENARIO_RUNTIME_REGISTRY_OK passed=%d" % passed)
		quit(0)
	else:
		print("SCENARIO_RUNTIME_REGISTRY_FAIL passed=%d failed=%d" % [passed, failed])
		quit(1)

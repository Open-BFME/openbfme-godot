extends SceneTree
## Focused fail-closed gate for the generic playable-structure runtime registry
## and the data-driven faction manifest built on top of it.
## Mirrors playable_unit_runtime_consumer_runner.gd: synthetic fixture packs
## only, no retail content, and every malformed delta must reject atomically.

const FactionManifest = preload("res://src/retail_slice/retail_faction_manifest.gd")
const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")

var passed := 0
var failed := 0


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "PLAYABLE_STRUCTURE_RUNTIME_CONSUMER_RUNNER")
	call_deferred("_run")


func _run() -> void:
	if OS.get_cmdline_user_args().has("--manifest-only"):
		_run_faction_manifest_checks()
		_finish()
		return
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
	_check(int((lifecycle.get("simulationFacts", {}) as Dictionary).get("maximumHealth", 0)) == 3000, "simulation facts survive the load")
	# retail_structure.gd resolves autoload identifiers, so it must be loaded
	# after the autoload registration instead of via a bootstrap preload.
	var structure_script: GDScript = load("res://src/retail_slice/retail_structure.gd")
	var presenter_error: String = structure_script.validate_lifecycle_contract(
		lifecycle, "monsterpen", "", 3000, _runtime_id("FixtureMonsterPen")
	)
	_check(presenter_error == "", "loaded composed lifecycle passes the RetailStructure v1 contract: %s" % presenter_error)

	# Reduced chains: no authored damage thresholds and a never-constructed
	# composite must load and pass the presenter contract too.
	var reduced := _fixture_document("ReducedPen", "reducedpen", "fixturemonsterpen", 3000, false, false)
	_write_json(pack_root.path_join("data/playable-structures/reducedpen.json"), reduced)
	_check(content_db._load_playable_structure_runtimes(pack_root, {
		"playableStructure.reducedpen": "data/playable-structures/reducedpen.json",
	}), "reduced-chain structure declaration loads")
	var reduced_lifecycle: Dictionary = _lifecycle_of(content_db.get_playable_structure_runtime("ReducedPen"))
	var reduced_phases: Array = []
	for row_value in reduced_lifecycle.get("phases", []) as Array:
		reduced_phases.append(String((row_value as Dictionary).get("phase", "")))
	_check(reduced_phases == ["intact", "collapsing", "rubble", "post-rubble", "post-collapse"], "reduced chain omits construction and damage phases")
	var reduced_error: String = structure_script.validate_lifecycle_contract(
		reduced_lifecycle, "reducedpen", "", 3000, _runtime_id("ReducedPen")
	)
	_check(reduced_error == "", "reduced composed lifecycle passes the RetailStructure v1 contract: %s" % reduced_error)

	# Invalid documents are skipped with a diagnostic; the well-formed entries
	# of the same delta still load, and nothing malformed enters the registry.
	var malformed := _fixture_document("BrokenPen", "brokenpen")
	malformed["registration"] = {}
	_write_json(pack_root.path_join("data/playable-structures/broken.json"), malformed)
	var before: Dictionary = content_db.get_playable_structure_runtimes()
	_write_json(pack_root.path_join("data/playable-structures/fixturemonsterpen2.json"), _fixture_document("FixtureMonsterPenTwo", "fixturemonsterpentwo"))
	var malformed_loaded: bool = content_db._load_playable_structure_runtimes(pack_root, {
		"playableStructure.second": "data/playable-structures/fixturemonsterpen2.json",
		"playableStructure.broken": "data/playable-structures/broken.json",
	})
	var after_malformed: Dictionary = content_db.get_playable_structure_runtimes()
	_check(
		malformed_loaded
			and after_malformed.has("FixtureMonsterPenTwo")
			and not after_malformed.has("BrokenPen"),
		"malformed document is skipped while the valid delta entry loads"
	)
	_check(not after_malformed.has("BrokenPen"), "skipped document leaves no registry trace")

	# Cross-pack casefolded id collision with a different spelling is skipped.
	var collision := _fixture_document("FIXTUREMonsterPen", "fixturemonsterpen")
	_write_json(pack_root.path_join("data/playable-structures/collision.json"), collision)
	var before_collision: Dictionary = content_db.get_playable_structure_runtimes()
	var collision_loaded: bool = content_db._load_playable_structure_runtimes(pack_root, {
		"playableStructure.collision": "data/playable-structures/collision.json",
	})
	_check(collision_loaded and content_db.get_playable_structure_runtimes() == before_collision, "casefolded object-id collision is skipped atomically")

	_check_skipped_variant(content_db, pack_root, "digest_format", func(broken: Dictionary) -> void:
		broken["runtimeSha256"] = "zz".repeat(32))
	_check_skipped_variant(content_db, pack_root, "wrong_slug", func(broken: Dictionary) -> void:
		broken["slug"] = "fixture-monster-pen")
	_check_skipped_variant(content_db, pack_root, "missing_visual_asset", func(broken: Dictionary) -> void:
		var phases: Array = _lifecycle_of(broken).get("phases", [])
		((phases[0] as Dictionary).get("visual", {}) as Dictionary)["glb"] = "assets/models/structures/variantpen/absent.glb")
	_check_skipped_variant(content_db, pack_root, "phase_order", func(broken: Dictionary) -> void:
		var phases: Array = _lifecycle_of(broken).get("phases", [])
		phases.reverse())
	_check_skipped_variant(content_db, pack_root, "coverage_mismatch", func(broken: Dictionary) -> void:
		_lifecycle_of(broken)["phaseCoverage"] = {"covered": ["intact"], "missing": []})
	_check_skipped_variant(content_db, pack_root, "facts_health_drift", func(broken: Dictionary) -> void:
		(_lifecycle_of(broken).get("simulationFacts", {}) as Dictionary)["maximumHealth"] = 4000)
	_check_skipped_variant(content_db, pack_root, "construct_evidence_without_routes", func(broken: Dictionary) -> void:
		((broken.get("registration", {}) as Dictionary).get("production", {}) as Dictionary)["routes"] = [])
	_check_skipped_variant(content_db, pack_root, "damage_rule_drift", func(broken: Dictionary) -> void:
		((_lifecycle_of(broken).get("simulationFacts", {}) as Dictionary).get("damageStateRule", {}) as Dictionary)["damagedThreshold"] = 2500)
	_check_skipped_variant(content_db, pack_root, "wrong_lifecycle_object_id", func(broken: Dictionary) -> void:
		_lifecycle_of(broken)["objectId"] = "bfme2.object.someone-else")
	_check_skipped_variant(content_db, pack_root, "construction_not_manual", func(broken: Dictionary) -> void:
		var phases: Array = _lifecycle_of(broken).get("phases", [])
		((phases[0] as Dictionary).get("animation", {}) as Dictionary)["mode"] = "loop")
	_check_skipped_variant(content_db, pack_root, "evidence_profile_drift", func(broken: Dictionary) -> void:
		_lifecycle_of(broken).erase("evidenceProfile"))
	_check_skipped_variant(content_db, pack_root, "damage_rule_hidden_despite_thresholds", func(broken: Dictionary) -> void:
		var broken_facts: Dictionary = _lifecycle_of(broken).get("simulationFacts", {}) as Dictionary
		broken_facts.erase("damageStateRule")
		broken_facts["damageStateRuleStatus"] = "no-authored-damage-thresholds")
	_check_skipped_variant(content_db, pack_root, "construction_marker_without_evidence", func(broken: Dictionary) -> void:
		var broken_facts: Dictionary = _lifecycle_of(broken).get("simulationFacts", {}) as Dictionary
		broken_facts["construction"] = {"status": "never-constructed-engine-spawned-composite"})

	_run_faction_manifest_checks()
	_run_cross_pack_producer_checks(content_db)
	_finish()


func _run_faction_manifest_checks() -> void:
	var deferred_production_exit := [{
		"module": "QueueProductionExitUpdate",
		"unitCreatePoint": {
			"authored": "X:14 Y:0 Z:0",
			"value": {"x": 14.0, "y": 0.0, "z": 0.0},
			"sourceIni": "data/ini/object/fixture.ini",
			"line": 50,
		},
		"naturalRallyPoint": {
			"authored": "X:75 Y:0 Z:0",
			"value": {"x": 75.0, "y": 0.0, "z": 0.0},
			"sourceIni": "data/ini/object/fixture.ini",
			"line": 51,
		},
		"exitDelay": {
			"authored": "50",
			"value": 50,
			"unit": "milliseconds",
			"sourceIni": "data/ini/object/fixture.ini",
			"line": 52,
		},
		"allowAirborneCreation": {
			"authored": "No",
			"value": false,
			"sourceIni": "data/ini/object/fixture.ini",
			"line": 53,
		},
		"initialBurst": {
			"authored": "0",
			"value": 0,
			"defaulted": true,
		},
		"deferredFields": [{
			"name": "PlacementViewAngle",
			"authored": "45",
			"sourceIni": "data/ini/object/fixture.ini",
			"line": 54,
			"reason": "bfme-field-without-local-runtime-oracle",
		}],
		"runtimeStatus": "deferred",
		"sourceIni": "data/ini/object/fixture.ini",
		"line": 49,
	}]
	# Retail-shaped citadel: an engine-spawned fortress composite which carries
	# the fortress command set (the porter construct button lives there).
	var fixture_citadel := _fixture_document("FixtureCitadel", "fixturecitadel", "", 3000, true, false)
	fixture_citadel["compositeRole"] = "fortress-composite-citadel"
	var citadel_gameplay := (
		(fixture_citadel.get("registration", {}) as Dictionary)
		.get("gameplay", {}) as Dictionary
	)
	citadel_gameplay["trainedCommandSets"] = [{
		"id": "FixtureFortressCommandSet",
		"kind": "direct",
		"slots": [{"slot": 1, "commandId": "Command_ConstructFixturePorter"}],
	}]
	citadel_gameplay["productionExitUpdates"] = deferred_production_exit.duplicate(true)
	var fixture_porter := _fixture_unit_document("FixturePorter", "FixtureCitadel", 1)
	var fixture_porter_production: Array = (fixture_porter.get("registration", {}) as Dictionary).get("production", []) as Array
	(fixture_porter_production[0] as Dictionary)["commandSetId"] = "FixtureFortressCommandSet"
	var fixture_monster_pen := _fixture_document(
		"FixtureMonsterPen", "fixturemonsterpen"
	)
	(
		(fixture_monster_pen.get("registration", {}) as Dictionary)
		.get("gameplay", {}) as Dictionary
	)["inheritUpgradesOnCreate"] = [{
		"radius": {"authored": "400", "value": 400.0},
		"upgradeId": "Upgrade_FixtureStonework",
		"upgradeType": "OBJECT",
		"objectFilter": "ANY +FixtureCitadel",
		"sourceObjectId": "FixtureCitadel",
		"module": "InheritUpgradeCreate",
		"sourceIni": "data/ini/object/fixture.ini",
		"line": 42,
	}]
	(
		(fixture_monster_pen.get("registration", {}) as Dictionary)
		.get("gameplay", {}) as Dictionary
	)["productionExitUpdates"] = deferred_production_exit.duplicate(true)
	var structures := {
		"FixtureFortress": _fixture_document("FixtureFortress", "fixturefortress", "fixturemonsterpen", 5000),
		"FixtureMonsterPen": fixture_monster_pen,
		"FixtureCitadel": fixture_citadel,
	}
	var units := {
		"FixtureMonster": _fixture_unit_document("FixtureMonster", "FixtureMonsterPen", 200),
		"FixturePorter": fixture_porter,
	}

	var manifest := FactionManifest.from_registries("fixture", units, structures)
	_check(not manifest.has("_error"), "fixture faction manifest builds: %s" % String(manifest.get("_error", "")))
	if manifest.has("_error"):
		return
	_check(Array(manifest.get("structure_kinds", [])) == ["fortress", "monsterpen"], "structure kinds derive fortress-first from document slugs")
	_check(int((manifest.get("structure_max_health", {}) as Dictionary).get("fortress", 0)) == 5000, "fortress health comes from simulationFacts")
	_check(int((manifest.get("structure_max_health", {}) as Dictionary).get("monsterpen", 0)) == 3000, "producer health comes from simulationFacts")
	var build_rule: Dictionary = (manifest.get("structure_build_rules", {}) as Dictionary).get("monsterpen", {}) as Dictionary
	_check(int(build_rule.get("cost", -1)) == 300 and is_equal_approx(float(build_rule.get("seconds", 0.0)), 30.0), "build rules parse BuildCost/BuildTime scalars")
	_check((manifest.get("producer_kind_registry", {}) as Dictionary) == {"FixtureFortress": "fortress", "FixtureMonsterPen": "monsterpen", "FixtureCitadel": "fortress"}, "producer registry maps source objects to kinds, folding the proven citadel into the fortress")
	_check(
		(manifest.get("fortress_composite_object_ids", {}) as Dictionary)
		== {"fortress-composite-citadel": "FixtureCitadel"},
		"manifest preserves exact fortress composite roles for presentation"
	)
	_check(
		(manifest.get("structure_source_object_ids", {}) as Dictionary).get(
			"fortress", []
		) == ["FixtureCitadel", "FixtureFortress"],
		"manifest preserves the proven composite citadel as an exact fortress source identity"
	)
	var inherit_rules: Array = (
		manifest.get("structure_inherit_upgrades", {}) as Dictionary
	).get("monsterpen", [])
	_check(
		inherit_rules.size() == 1
			and String((inherit_rules[0] as Dictionary).get("sourceObjectId", ""))
			== "FixtureCitadel"
			and String((inherit_rules[0] as Dictionary).get("sourceKind", ""))
			== "fortress"
			and float(
				(
					(inherit_rules[0] as Dictionary).get("radius", {})
					as Dictionary
				).get("value", 0.0)
			) == 400.0,
		"manifest projects the exact InheritUpgradeCreate contract"
	)
	var deferred_exit_rules: Dictionary = (
		manifest.get("deferred_structure_production_exit_updates", {})
		as Dictionary
	)
	var deferred_exit_ids := deferred_exit_rules.keys()
	deferred_exit_ids.sort()
	_check(
		deferred_exit_ids == ["FixtureCitadel", "FixtureMonsterPen"],
		"manifest preserves QueueProductionExitUpdate for materialized and excluded structures"
	)
	var monster_pen_exit_rules: Array = deferred_exit_rules.get(
		"FixtureMonsterPen", []
	) as Array
	_check(
		monster_pen_exit_rules.size() == 1
			and String(
				(monster_pen_exit_rules[0] as Dictionary).get("runtimeStatus", "")
			) == "deferred"
			and int(
				(
					(monster_pen_exit_rules[0] as Dictionary).get("exitDelay", {})
					as Dictionary
				).get("value", -1)
			) == 50,
		"manifest keeps the exact deferred production-exit evidence without promoting it"
	)
	_check(
		(manifest.get("structure_production_exit_updates", {}) as Dictionary).is_empty(),
		"legacy deferred QueueProductionExitUpdate is not promoted"
	)
	var canonical_exit := [{
		"carrier": "Behavior",
		"extraction": "typed",
		"fields": {
			"UnitCreatePoint": [{
				"authored": "X:14 Y:0 Z:0",
				"components": {"x": "14", "y": "0", "z": "0"},
				"value": {"x": 14.0, "y": 0.0, "z": 0.0},
				"validNumeric": true,
				"sourceIni": "data/ini/object/fixture.ini",
				"line": 50,
			}],
			"NaturalRallyPoint": [{
				"authored": "X:40 Y:0 Z:0",
				"components": {"x": "40", "y": "0", "z": "0"},
				"value": {"x": 40.0, "y": 0.0, "z": 0.0},
				"validNumeric": true,
				"sourceIni": "data/ini/object/fixture.ini",
				"line": 51,
			}],
		},
		"line": 49,
		"module": "QueueProductionExitUpdate",
		"runtimeStatus": "executable",
		"sourceIni": "data/ini/object/fixture.ini",
		"tag": "ModuleTag_Exit",
	}]
	_check(
		FactionManifest._validate_structure_production_exit_updates(
			"FixtureMonsterPen", canonical_exit, canonical_exit
		) == "",
		"manifest accepts the exact compiler-authoritative executable QueueProductionExitUpdate projection"
	)
	var drifted_canonical := canonical_exit.duplicate(true)
	(
		((drifted_canonical[0] as Dictionary).get("fields", {}) as Dictionary)
		.get("UnitCreatePoint", [])[0] as Dictionary
	)["value"] = {"x": 99.0, "y": 0.0, "z": 0.0}
	_check(
		FactionManifest._validate_structure_production_exit_updates(
			"FixtureMonsterPen", drifted_canonical, canonical_exit
		).contains("projection drifted"),
		"manifest rejects a canonical compatibility projection that drifts from moduleContracts"
	)
	var invalid_status_canonical := canonical_exit.duplicate(true)
	(invalid_status_canonical[0] as Dictionary)["runtimeStatus"] = "simulation-backed"
	_check(
		FactionManifest._validate_structure_production_exit_updates(
			"FixtureMonsterPen", invalid_status_canonical, invalid_status_canonical
		).contains("invalid canonical"),
		"manifest rejects an unrecognized canonical QueueProductionExitUpdate status"
	)
	var invalid_numeric_canonical := canonical_exit.duplicate(true)
	var invalid_coordinate: Dictionary = (
		((invalid_numeric_canonical[0] as Dictionary).get("fields", {}) as Dictionary)
		.get("UnitCreatePoint", [])[0] as Dictionary
	)
	invalid_coordinate["validNumeric"] = false
	invalid_coordinate["value"] = null
	_check(
		FactionManifest._validate_structure_production_exit_updates(
			"FixtureMonsterPen", invalid_numeric_canonical, invalid_numeric_canonical
		).contains("invalid numeric coordinates"),
		"manifest rejects an invalid coordinate promoted to executable even when both projections match"
	)
	var promoted_exit_structures := structures.duplicate(true)
	var promoted_exit_pen: Dictionary = (
		promoted_exit_structures["FixtureMonsterPen"] as Dictionary
	).duplicate(true)
	promoted_exit_structures["FixtureMonsterPen"] = promoted_exit_pen
	var promoted_exit_registration: Dictionary = (
		promoted_exit_pen.get("registration", {}) as Dictionary
	).duplicate(true)
	promoted_exit_pen["registration"] = promoted_exit_registration
	var promoted_exit_gameplay: Dictionary = (
		promoted_exit_registration.get("gameplay", {}) as Dictionary
	).duplicate(true)
	promoted_exit_registration["gameplay"] = promoted_exit_gameplay
	var promoted_exit_rules: Array = (
		promoted_exit_gameplay.get("productionExitUpdates", []) as Array
	).duplicate(true)
	promoted_exit_gameplay["productionExitUpdates"] = promoted_exit_rules
	var promoted_exit_rule: Dictionary = (
		promoted_exit_rules[0] as Dictionary
	).duplicate(true)
	promoted_exit_rules[0] = promoted_exit_rule
	promoted_exit_rule["runtimeStatus"] = "simulation-backed"
	var promoted_exit_manifest := FactionManifest.from_registries(
		"fixture", units, promoted_exit_structures
	)
	_check(
		String(promoted_exit_manifest.get("_error", "")).contains(
			"invalid deferred QueueProductionExitUpdate"
		),
		"manifest rejects QueueProductionExitUpdate promoted without runtime support"
	)
	var reordered_exit := deferred_production_exit.duplicate(true)
	var reordered_exit_point := (
		(reordered_exit[0] as Dictionary).get("unitCreatePoint", {}) as Dictionary
	)
	reordered_exit_point["authored"] = "Y:0 X:14 Z:0"
	_check(
		FactionManifest._validate_structure_production_exit_updates(
			"FixtureMonsterPen", reordered_exit
		).contains("invalid QueueProductionExitUpdate coordinate"),
		"manifest rejects a re-signed production-exit coordinate with out-of-order axes"
	)
	var drifted_exit := deferred_production_exit.duplicate(true)
	var drifted_exit_point := (
		((drifted_exit[0] as Dictionary).get("unitCreatePoint", {}) as Dictionary)
		.get("value", {}) as Dictionary
	)
	drifted_exit_point["x"] = 15.0
	_check(
		FactionManifest._validate_structure_production_exit_updates(
			"FixtureMonsterPen", drifted_exit
		).contains("invalid QueueProductionExitUpdate coordinate"),
		"manifest rejects a re-signed production-exit coordinate whose authored and value forms drift"
	)
	var aliased_boolean_exit := deferred_production_exit.duplicate(true)
	var aliased_boolean := (
		((aliased_boolean_exit[0] as Dictionary).get(
			"allowAirborneCreation", {}
		) as Dictionary)
	)
	aliased_boolean["authored"] = "False"
	_check(
		FactionManifest._validate_structure_production_exit_updates(
			"FixtureMonsterPen", aliased_boolean_exit
		).contains("invalid QueueProductionExitUpdate boolean"),
		"manifest rejects non-INI boolean aliases in re-signed production-exit evidence"
	)
	var drifted_boolean_exit := deferred_production_exit.duplicate(true)
	var drifted_boolean := (
		((drifted_boolean_exit[0] as Dictionary).get(
			"allowAirborneCreation", {}
		) as Dictionary)
	)
	drifted_boolean["authored"] = "Yes"
	_check(
		FactionManifest._validate_structure_production_exit_updates(
			"FixtureMonsterPen", drifted_boolean_exit
		).contains("invalid QueueProductionExitUpdate boolean"),
		"manifest rejects re-signed production-exit booleans whose authored and value forms drift"
	)
	var mixed_case_boolean_exit := deferred_production_exit.duplicate(true)
	var mixed_case_boolean := (
		((mixed_case_boolean_exit[0] as Dictionary).get(
			"allowAirborneCreation", {}
		) as Dictionary)
	)
	mixed_case_boolean["authored"] = "nO"
	_check(
		FactionManifest._validate_structure_production_exit_updates(
			"FixtureMonsterPen", mixed_case_boolean_exit
		) == "",
		"manifest accepts case-insensitive authored Yes/No when the stored boolean agrees"
	)
	for scalar_drift in [
		{"field": "exitDelay", "member": "authored", "value": "51"},
		{"field": "exitDelay", "member": "value", "value": 51},
		{"field": "initialBurst", "member": "authored", "value": "1"},
		{"field": "initialBurst", "member": "value", "value": 1},
	]:
		var drifted_scalar_exit := deferred_production_exit.duplicate(true)
		var drifted_scalar := (
			((drifted_scalar_exit[0] as Dictionary).get(
				String(scalar_drift["field"]), {}
			) as Dictionary)
		)
		drifted_scalar[String(scalar_drift["member"])] = scalar_drift["value"]
		_check(
			FactionManifest._validate_structure_production_exit_updates(
				"FixtureMonsterPen", drifted_scalar_exit
			).contains("invalid QueueProductionExitUpdate integer"),
			"manifest rejects re-signed %s %s drift"
			% [scalar_drift["field"], scalar_drift["member"]]
		)
	var resolved_define_exit := deferred_production_exit.duplicate(true)
	var resolved_define_delay := (
		((resolved_define_exit[0] as Dictionary).get("exitDelay", {}) as Dictionary)
	)
	resolved_define_delay["authored"] = "FIXTURE_EXIT_DELAY"
	resolved_define_delay["resolvedDefine"] = {
		"name": "FIXTURE_EXIT_DELAY",
		"value": 50,
	}
	_check(
		FactionManifest._validate_structure_production_exit_updates(
			"FixtureMonsterPen", resolved_define_exit
		) == "",
		"manifest accepts internally consistent define-resolved unsigned evidence"
	)
	var drifted_define_exit := resolved_define_exit.duplicate(true)
	(
		((drifted_define_exit[0] as Dictionary).get("exitDelay", {}) as Dictionary)
		.get("resolvedDefine", {}) as Dictionary
	)["name"] = "OTHER_EXIT_DELAY"
	_check(
		FactionManifest._validate_structure_production_exit_updates(
			"FixtureMonsterPen", drifted_define_exit
		).contains("invalid QueueProductionExitUpdate integer"),
		"manifest rejects re-signed define-resolution identity drift"
	)
	for unknown_field_attack in [
		"row",
		"coordinate",
		"coordinate-value",
		"integer",
		"resolved-define",
		"boolean",
		"deferred",
	]:
		var unknown_exit := resolved_define_exit.duplicate(true)
		var unknown_row := unknown_exit[0] as Dictionary
		var unknown_target: Dictionary
		match unknown_field_attack:
			"row":
				unknown_target = unknown_row
			"coordinate":
				unknown_target = unknown_row["unitCreatePoint"] as Dictionary
			"coordinate-value":
				unknown_target = (
					(unknown_row["unitCreatePoint"] as Dictionary)["value"]
					as Dictionary
				)
			"integer":
				unknown_target = unknown_row["initialBurst"] as Dictionary
			"resolved-define":
				unknown_target = (
					(unknown_row["exitDelay"] as Dictionary)["resolvedDefine"]
					as Dictionary
				)
			"boolean":
				unknown_target = (
					unknown_row["allowAirborneCreation"] as Dictionary
				)
			_:
				unknown_target = (
					(unknown_row["deferredFields"] as Array)[0] as Dictionary
				)
		unknown_target["ignoredProjectionField"] = true
		_check(
			FactionManifest._validate_structure_production_exit_updates(
				"FixtureMonsterPen", unknown_exit
			) != "",
			"manifest refuses unknown-field projection in QueueProductionExitUpdate %s schema"
			% unknown_field_attack
		)
	var mistyped_default_exit := deferred_production_exit.duplicate(true)
	var mistyped_default_row := mistyped_default_exit[0] as Dictionary
	mistyped_default_row["initialBurst"] = {
		"authored": "0",
		"value": 0,
		"defaulted": "true",
	}
	_check(
		FactionManifest._validate_structure_production_exit_updates(
			"FixtureMonsterPen", mistyped_default_exit
		).contains("invalid QueueProductionExitUpdate integer"),
		"manifest rejects a non-boolean defaulted marker"
	)
	var unresolved_structures := structures.duplicate(true)
	var unresolved_pen: Dictionary = (
		unresolved_structures["FixtureMonsterPen"] as Dictionary
	).duplicate(true)
	unresolved_structures["FixtureMonsterPen"] = unresolved_pen
	var unresolved_registration: Dictionary = (
		unresolved_pen.get("registration", {}) as Dictionary
	).duplicate(true)
	unresolved_pen["registration"] = unresolved_registration
	var unresolved_gameplay: Dictionary = (
		unresolved_registration.get("gameplay", {}) as Dictionary
	).duplicate(true)
	unresolved_registration["gameplay"] = unresolved_gameplay
	var unresolved_rules: Array = (
		unresolved_gameplay.get("inheritUpgradesOnCreate", []) as Array
	).duplicate(true)
	unresolved_gameplay["inheritUpgradesOnCreate"] = unresolved_rules
	var unresolved_rule: Dictionary = (unresolved_rules[0] as Dictionary).duplicate(true)
	unresolved_rules[0] = unresolved_rule
	unresolved_rule["sourceObjectId"] = "MissingCitadel"
	unresolved_rule["objectFilter"] = "ANY +MissingCitadel"
	var unresolved_manifest := FactionManifest.from_registries(
		"fixture", units, unresolved_structures
	)
	_check(
		String(unresolved_manifest.get("_error", "")).contains(
			"resolves to 0 live structure kinds"
		),
		"manifest rejects an unresolved InheritUpgradeCreate source alias"
	)
	var fixture_rules: Dictionary = manifest.get("unit_production_rules", {}) as Dictionary
	var fixture_monster_rule: Dictionary = fixture_rules.get("bfme2.object.fixture-monster", {}) as Dictionary
	_check(
		fixture_rules.size() == 1
			and String(fixture_monster_rule.get("producer_kind", "")) == "monsterpen"
			and String(fixture_monster_rule.get("object_id", "")) == "bfme2.object.fixture-monster"
			and int(fixture_monster_rule.get("default_cost", -1)) == 700
			and int(fixture_monster_rule.get("default_build_ticks", -1)) == 450
			and int(fixture_monster_rule.get("default_command_points", -1)) == 35
			and String(fixture_monster_rule.get("command_id", "")) == "Command_ConstructFixtureMonster",
		"manifest auto-populates the trainable unit's production rule from its document"
	)
	_check(Array(manifest.get("excluded_units", [])).is_empty(), "fixture roster has no exclusions")
	_check(Array(manifest.get("ai_production_plan", [])) == ["bfme2.object.fixture-monster"], "AI plan is one trainable unit per producer in fixed order")
	_check(Array(manifest.get("builder_unit_ids", [])) == ["bfme2.object.fixture-porter"], "builder derives from authored construct routes")
	var roster: Array = manifest.get("spawn_roster", [])
	var roster_ids: Array = []
	for entry in roster:
		roster_ids.append(int((entry as Dictionary).get("id", 0)))
	_check(roster_ids == [1, 2, 101, 102, 103, 3, 104], "spawn roster covers the deterministic anchor slots")

	var sim = Sim.new()
	sim._apply_gameplay_rules({
		"enable_base_loop": true,
		"faction_manifest": manifest,
		"playable_unit_runtimes": units,
		"producer_kind_by_source_object": manifest.get("producer_kind_registry", {}),
		"unit_rules": {},
		"starting_resources": 2000,
		"source_map_transform_scale": 0.1,
	})
	_check(sim.configuration_error == "", "fixture faction simulation configures: %s" % sim.configuration_error)
	sim.setup({}, sim._rules)
	_check(sim.configuration_error == "", "fixture faction setup stays configured: %s" % sim.configuration_error)
	# Retail start seeds fortresses only; the remaining constructable kinds stay
	# in the builder's build rules until the porter places them.
	_check(sim.structure_ids(0).size() == 1 and sim.structure_ids(1).size() == 1, "base loop seeds fortresses only at match start")
	_check(sim.fortress_id(0) != 0 and sim.fortress_id(1) != 0, "fortress kind is normalized so the base anchor resolves")
	_check(sim.structure_maximum_health("monsterpen") == 3000, "structure health flows from the manifest")
	_check(sim.initial_battalion_count() == 7 and sim.entity_ids().size() == 7, "faction spawn roster fills every anchor slot")
	_check(bool(sim.entity(3).get("is_builder", false)) and bool(sim.entity(104).get("is_builder", false)), "builder flag rides the manifest builder unit ids")
	_check(String(sim.entity(1).get("object_id", "")) == "bfme2.object.fixture-monster", "player spawn identity is descriptor-driven")
	sim.ai_enabled = false
	var monster_pen_id := 0
	# The site must sit outside the enemy's auto-acquire vision so the walking
	# builder survives construction; the fixture units see far (scale 0.1).
	for candidate in [Vector2(-30.0, -20.0), Vector2(-25.0, -5.0), Vector2(-30.0, 10.0), Vector2(-20.0, -25.0), Vector2(-44.0, -24.0)]:
		var construct_result: Dictionary = sim.issue_construct([3], "monsterpen", candidate)
		if bool(construct_result.get("ok", false)):
			monster_pen_id = int(construct_result.get("structure_id", 0))
			break
	_check(monster_pen_id != 0, "porter constructs the declared producer kind")
	var pen_built := false
	for _step in range(600):
		if monster_pen_id != 0 and float(sim.structure(monster_pen_id).get("construction_progress", 0.0)) >= 1.0:
			pen_built = true
			break
		sim.tick()
	_check(pen_built, "constructed producer completes")
	var producer: int = sim.producer_id(0, "monsterpen")
	var queued: Dictionary = sim.queue_unit(0, producer, "bfme2.object.fixture-monster")
	_check(bool(queued.get("ok", false)), "faction producer trains its declared unit: %s" % String(queued.get("reason", "")))
	var wrong: Dictionary = sim.queue_unit(0, sim.fortress_id(0), "bfme2.object.fixture-monster")
	_check(not bool(wrong.get("ok", true)) and String(wrong.get("reason", "")) == "unsupported-unit", "fortress rejects units it does not train")
	var porter_queued: Dictionary = sim.queue_unit(0, sim.fortress_id(0), "bfme2.object.fixture-porter")
	_check(bool(porter_queued.get("ok", false)), "citadel-bound porter trains at the fortress: %s" % String(porter_queued.get("reason", "")))

	var no_porter := units.duplicate(true)
	no_porter.erase("FixturePorter")
	var missing_builder := FactionManifest.from_registries("fixture", no_porter, structures)
	_check(String(missing_builder.get("_error", "")).contains("FixturePorter"), "missing porter fails closed naming the builder")
	var no_fortress := structures.duplicate(true)
	no_fortress.erase("FixtureFortress")
	var missing_fortress := FactionManifest.from_registries("fixture", units, no_fortress)
	_check(String(missing_fortress.get("_error", "")).contains("fortress"), "missing fortress fails closed naming the gap")
	var foreign_units := units.duplicate(true)
	foreign_units["FixtureStray"] = _fixture_unit_document("FixtureStray", "GondorBarracks", 50)
	var missing_producer := FactionManifest.from_registries("fixture", foreign_units, structures)
	var missing_producer_excluded := false
	for exclusion_value in missing_producer.get("excluded_units", []) as Array:
		var exclusion := exclusion_value as Dictionary
		if (
			String(exclusion.get("object_id", "")) == "FixtureStray"
			and String(exclusion.get("reason", "")) == "producer-not-loaded:GondorBarracks"
		):
			missing_producer_excluded = true
			break
	_check(
		not missing_producer.has("_error") and missing_producer_excluded,
		"unknown producer route fails closed by excluding the unit with the exact structure identity"
	)
	var empty_faction := FactionManifest.from_registries("rohan", units, structures)
	_check(String(empty_faction.get("_error", "")).contains("rohan"), "unconverted faction fails closed naming the faction")
	# Men with empty registries keeps the legacy default manifest; with full
	# registries it takes the same data-driven path as other factions.
	var men_default := FactionManifest.from_registries("men", {}, {})
	_check(not men_default.has("_error") and String(men_default.get("faction", "")) == "men" and Array(men_default.get("structure_kinds", [])).has("barracks"), "men empty registries uses default_manifest")
	var men_full := FactionManifest.from_registries("men", units, structures)
	# Fixture objects use Fixture* ids, not men/gondor prefixes, so full path fails closed.
	_check(String(men_full.get("_error", "")).contains("men") or String(men_full.get("_error", "")).contains("playableStructure"), "men full path with foreign fixtures fails closed: %s" % String(men_full.get("_error", "")))

	# Canonical UI faction names map to the retail Object prefixes, and
	# engine-spawned fortress parts remain presentation-only resources. Retail
	# binds the porter to the citadel: the fortress spawns the citadel, and the
	# citadel carries the fortress command set with the porter construct button.
	var elven_fortress := _fixture_document("ElvenFortress", "elvenfortress", "elvenmonsterpen", 5000)
	var elven_pen := _fixture_document("ElvenMonsterPen", "elvenmonsterpen")
	for document_value in [elven_fortress, elven_pen]:
		var production: Dictionary = ((document_value as Dictionary).get("registration", {}) as Dictionary).get("production", {}) as Dictionary
		for route_value in production.get("routes", []) as Array:
			(route_value as Dictionary)["builderObjectId"] = "ElvenPorter"
	var elven_citadel := _fixture_document("ElvenCitadel", "elvencitadel", "", 3000, true, false)
	((elven_citadel.get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary)["trainedCommandSets"] = [{
		"id": "ElvenFortressCommandSet",
		"kind": "direct",
		"slots": [{"slot": 1, "commandId": "Command_ConstructElvenPorter"}],
	}]
	var elven_porter := _fixture_unit_document("ElvenPorter", "ElvenCitadel", 1)
	var elven_porter_production: Array = (elven_porter.get("registration", {}) as Dictionary).get("production", []) as Array
	(elven_porter_production[0] as Dictionary)["commandSetId"] = "ElvenFortressCommandSet"
	var elven_structures := {"ElvenFortress": elven_fortress, "ElvenCitadel": elven_citadel, "ElvenMonsterPen": elven_pen}
	var elven_units := {
		"ElvenMonster": _fixture_unit_document("ElvenMonster", "ElvenMonsterPen", 200),
		"ElvenPorter": elven_porter,
	}
	var elves := FactionManifest.from_registries("elves", elven_units, elven_structures)
	_check(not elves.has("_error"), "elves alias resolves Elven Object prefixes: %s" % String(elves.get("_error", "")))
	_check(Array(elves.get("structure_kinds", [])) == ["fortress", "monsterpen"], "engine-spawned fortress composites are not independent structures")
	_check(String((elves.get("producer_kind_registry", {}) as Dictionary).get("ElvenCitadel", "")) == "fortress", "citadel-bound porter registers the citadel as a fortress producer component")
	_check(Array(elves.get("builder_unit_ids", [])) == ["bfme2.object.elven-porter"], "citadel-bound porter still resolves as the faction builder")

	# The fold stays fail-closed: a composite which does not record the cited
	# command cannot produce, and a non-composite structure never can.
	var unproven_citadel := _fixture_document("ElvenCitadel", "elvencitadel", "", 3000, true, false)
	var unproven_structures := {"ElvenFortress": elven_fortress, "ElvenCitadel": unproven_citadel, "ElvenMonsterPen": elven_pen}
	var unproven := FactionManifest.from_registries("elves", elven_units, unproven_structures)
	_check(String(unproven.get("_error", "")).contains("ElvenCitadel") and String(unproven.get("_error", "")).contains("Command_ConstructElvenPorter"), "composite without the authored command fails closed: %s" % String(unproven.get("_error", "")))
	var wall_citadel := _fixture_document("ElvenCitadel", "elvencitadel", "", 3000, true, false)
	((wall_citadel.get("registration", {}) as Dictionary).get("production", {}) as Dictionary)["evidence"] = "wall-template"
	var wall_structures := {"ElvenFortress": elven_fortress, "ElvenCitadel": wall_citadel, "ElvenMonsterPen": elven_pen}
	var walled := FactionManifest.from_registries("elves", elven_units, wall_structures)
	_check(String(walled.get("_error", "")).contains("wall-template"), "producer with wall-template evidence fails closed: %s" % String(walled.get("_error", "")))


func _run_cross_pack_producer_checks(content_db) -> void:
	# Retail authors some units at several factions' structures (MordorWorker
	# is built at the isengard, mordor, and wild lumber mills), so each faction
	# pack ships its own same-name document bound to its own producer. The
	# foreign copy loads last and takes the flat registry slot; the manifest
	# must still scope the faction's own pack copy for producer resolution.
	var home_root := ProjectSettings.globalize_path("user://cross-pack-home-fixture")
	var foreign_root := ProjectSettings.globalize_path("user://cross-pack-foreign-fixture")
	for fixture_root in [home_root, foreign_root]:
		DirAccess.make_dir_recursive_absolute(fixture_root.path_join("data/playable-units"))
		DirAccess.make_dir_recursive_absolute(fixture_root.path_join("assets/models"))
		DirAccess.make_dir_recursive_absolute(fixture_root.path_join("assets/ui"))
		DirAccess.make_dir_recursive_absolute(fixture_root.path_join("assets/audio"))
		_write_bytes(fixture_root.path_join("assets/models/fixture.glb"), PackedByteArray([7, 8, 9]))
		_write_bytes(fixture_root.path_join("assets/ui/fixture.png"), PackedByteArray([1, 2, 3]))
		_write_bytes(fixture_root.path_join("assets/audio/fixture.wav"), _silent_wav())
	DirAccess.make_dir_recursive_absolute(home_root.path_join("data/playable-structures"))
	DirAccess.make_dir_recursive_absolute(home_root.path_join("assets/models/structures/fixturemonsterpen"))
	for stem in ["construction", "intact", "damaged", "rubble", "bib"]:
		_write_bytes(home_root.path_join("assets/models/structures/fixturemonsterpen/%s.glb" % stem), PackedByteArray([7, 8, 9]))
	_write_json(home_root.path_join("data/playable-structures/fixturefortress.json"), _fixture_document("FixtureFortress", "fixturefortress", "fixturemonsterpen", 5000))
	_write_json(home_root.path_join("data/playable-structures/fixturemill.json"), _fixture_document("FixtureMill", "fixturemill"))
	_write_json(home_root.path_join("data/playable-units/fixtureporter.json"), _loadable_unit_document("FixturePorter", "FixtureFortress"))
	_write_json(home_root.path_join("data/playable-units/fixtureworker.json"), _loadable_unit_document("FixtureWorker", "FixtureMill"))
	_write_json(foreign_root.path_join("data/playable-units/fixtureworker.json"), _loadable_unit_document("FixtureWorker", "ForeignMill"))
	for fixture_root in [home_root, foreign_root]:
		if not content_db.pack_roots.has(fixture_root):
			content_db.pack_roots.append(fixture_root)

	_check(content_db._load_playable_structure_runtimes(home_root, {
		"playableStructure.fixturefortress": "data/playable-structures/fixturefortress.json",
		"playableStructure.fixturemill": "data/playable-structures/fixturemill.json",
	}), "home pack structures load")
	_check(content_db._load_playable_unit_runtimes(home_root, {
		"playableUnit.fixtureporter": "data/playable-units/fixtureporter.json",
		"playableUnit.fixtureworker": "data/playable-units/fixtureworker.json",
	}), "home pack units load")
	_check(content_db._load_playable_unit_runtimes(foreign_root, {
		"playableUnit.fixtureworker": "data/playable-units/fixtureworker.json",
	}), "foreign pack same-name unit loads")
	var pack_index: Dictionary = content_db.get_playable_unit_runtime_pack_index()
	_check((pack_index.get("fixtureworker", []) as Array).size() == 2, "shared unit records every admitted pack copy")
	var flat_worker: Dictionary = content_db.get_playable_unit_runtime("FixtureWorker")
	var flat_production: Array = (flat_worker.get("registration", {}) as Dictionary).get("production", [])
	_check(not flat_production.is_empty() and String((flat_production[0] as Dictionary).get("producerObjectId", "")) == "ForeignMill", "flat registry keeps the last-loaded foreign copy")

	var manifest := FactionManifest.from_registries("fixture", content_db.get_playable_unit_runtimes(), content_db.get_playable_structure_runtimes())
	_check(not manifest.has("_error"), "cross-pack faction manifest builds: %s" % String(manifest.get("_error", "")))
	var worker_rule: Dictionary = (manifest.get("unit_production_rules", {}) as Dictionary).get("bfme2.object.fixture-worker", {}) as Dictionary
	_check(String(worker_rule.get("producer_source_object_id", "")) == "FixtureMill", "manifest scopes the faction's own pack copy for a shared unit")
	_check(String(worker_rule.get("producer_kind", "")) == "mill", "scoped shared unit resolves its own producer kind")

	# A supplemental pack can also own other same-prefix structures. That broad
	# root membership must not let its older copy of a unit win over the pack
	# that owns the exact producer structure and command-set layout.
	var mixed_roots_structures: Dictionary = content_db.get_playable_structure_runtimes().duplicate(true)
	mixed_roots_structures["FixtureLegacyFortress"] = {"_pack_root": foreign_root}
	# Adversarial selected-pack shape: every faction's structures are present in
	# the global registry, so the foreign variant's exact producer also exists.
	# Producer existence/root equality alone must not let a non-Fixture producer
	# win the Fixture faction's same-name unit slot.
	mixed_roots_structures["ForeignMill"] = {"_pack_root": foreign_root}
	var reversed_variants: Array = (pack_index.get("fixtureworker", []) as Array).duplicate(true)
	reversed_variants.reverse()
	var mixed_pack_index: Dictionary = pack_index.duplicate(true)
	mixed_pack_index["fixtureworker"] = reversed_variants
	var producer_scoped: Dictionary = FactionManifest.faction_scoped_unit_runtimes(
		["fixture"], content_db.get_playable_unit_runtimes(), mixed_roots_structures, mixed_pack_index
	)
	var producer_scoped_routes: Array = (
		(producer_scoped.get("FixtureWorker", {}) as Dictionary).get("registration", {}) as Dictionary
	).get("production", [])
	_check(
		not producer_scoped_routes.is_empty()
		and String((producer_scoped_routes[0] as Dictionary).get("producerObjectId", "")) == "FixtureMill",
		"exact producer pack outranks supplemental same-prefix root and variant load order"
	)

	# With no provable faction pack root the registry document stands, so the
	# foreign producer still fails closed instead of being silently adopted.
	var unscoped := FactionManifest.faction_scoped_unit_runtimes(["fixture"], content_db.get_playable_unit_runtimes(), {"FixtureFortress": {}}, pack_index)
	var unscoped_production: Array = ((unscoped.get("FixtureWorker", {}) as Dictionary).get("registration", {}) as Dictionary).get("production", [])
	_check(not unscoped_production.is_empty() and String((unscoped_production[0] as Dictionary).get("producerObjectId", "")) == "ForeignMill", "unprovable pack scope keeps the registry copy and fails closed downstream")


func _loadable_unit_document(object_id: String, producer_object_id: String) -> Dictionary:
	## Full ContentDB-loadable unit shape (mirrors the unit consumer fixture):
	## the manifest-only fixture above is too thin for pack validation.
	return {
		"schema": "openbfme.playable-unit-runtime",
		"schemaVersion": 0,
		"objectId": object_id,
		"category": "infantry",
		"descriptorSha256": "1".repeat(64),
		"recipeSha256": "2".repeat(64),
		"resourceIds": ["fixture-model", "fixture-ui", "fixture-audio"],
		"registration": {
			"production": [{
				"producerObjectId": producer_object_id,
				"commandSetId": "%sCommandSet" % producer_object_id,
				"commandId": "Command_Construct%s" % object_id,
				"surface": "command-socket",
				"slot": 1,
				"prerequisites": [],
				"commandSetTransition": [],
			}],
			"composition": {
				"containerObjectId": object_id,
				"primaryMemberObjectId": object_id,
				"members": [{"objectId": object_id, "count": 1}],
			},
			"gameplay": {
				"armor": {
					"setId": "FixtureUnitArmor",
					"semantic": "authored-armor-set",
					"table": {
						"default": {"percent": 100.0},
						"damageScalar": {"percent": 100.0},
						"scalars": {},
					},
				},
			},
			"simulation": {
				"displayName": "%s Display" % object_id,
				"buildCost": 700,
				"buildTimeSeconds": 45.0,
				"commandPoints": 35,
				"memberCount": 1,
				"memberHealth": 2500,
				"speed": 50.0,
				"visionRange": 400.0,
				"combat": {
					"attackRange": 30.0, "minimumAttackRange": 0.0,
					"delayBetweenShotsMs": 1000.0, "preAttackDelayMs": 250.0,
					"firingDurationMs": 250.0, "damage": 200,
				},
				"movement": {"acceleration": 100.0, "braking": 100.0, "turnRateDegreesPerSecond": 360.0},
				"formation": {"memberCount": 1, "positions": [{"x": 0.0, "y": 0.0}]},
				"resolved": {
					"armor": {
						"setId": "FixtureUnitArmor",
						"semantic": "authored-armor-set",
						"table": {
							"default": {"percent": 100.0},
							"damageScalar": {"percent": 100.0},
							"scalars": {},
						},
						"upgrades": [],
					},
					"combat": {"damageType": "slash", "upgrades": []},
				},
			},
			"capabilities": [{"id": "move"}],
			"visual": {
				"components": [{
					"default": true,
					"output": "assets/models/fixture.glb",
					"resourceId": "fixture-model",
					"sourceW3d": "art/w3d/fixture.w3d",
				}],
				"coreAnimations": {
					"idle": [{"identifier": "fixture_idle"}],
					"move": [{"identifier": "fixture_move"}],
				},
			},
			"ui": {
				"portraitImageIds": ["UPFixture"],
				"commands": [{
					"commandId": "Command_Construct%s" % object_id,
					"fields": {
						"ButtonImage": ["BIFixture"],
						"TextLabel": ["CONTROLBAR:%s" % object_id],
						"DescriptLabel": ["CONTROLBAR:ToolTip%s" % object_id],
					},
				}],
			},
			"imageBindings": {
				"BIFixture": "assets/ui/fixture.png",
				"UPFixture": "assets/ui/fixture.png",
			},
			"audioRoutes": {"container": {}, "primaryMember": {}},
			"audioBindings": {},
			"audioResolution": {},
			"unsupportedCapabilities": [],
		},
	}


func _silent_wav() -> PackedByteArray:
	return PackedByteArray([
		82, 73, 70, 70, 38, 0, 0, 0, 87, 65, 86, 69,
		102, 109, 116, 32, 16, 0, 0, 0, 1, 0, 1, 0,
		64, 31, 0, 0, 128, 62, 0, 0, 2, 0, 16, 0,
		100, 97, 116, 97, 2, 0, 0, 0, 0, 0,
	])


func _fixture_unit_document(object_id: String, producer_object_id: String, damage: int) -> Dictionary:
	var slug := object_id.to_lower()
	return {
		"schema": "openbfme.playable-unit-runtime",
		"schemaVersion": 0,
		"objectId": object_id,
		"category": "infantry",
		"descriptorSha256": "1".repeat(64),
		"recipeSha256": "2".repeat(64),
		"resourceIds": ["%s-model" % slug],
		"registration": {
			"production": [{
				"producerObjectId": producer_object_id,
				"commandSetId": "%sCommandSet" % producer_object_id,
				"commandId": "Command_Construct%s" % object_id,
				"surface": "command-socket",
				"slot": 1,
				"prerequisites": [],
				"commandSetTransition": [],
			}],
			"composition": {
				"containerObjectId": object_id,
				"primaryMemberObjectId": object_id,
				"members": [{"objectId": object_id, "count": 1}],
			},
			"gameplay": {},
			"simulation": {
				"displayName": "%s Display" % object_id,
				"buildCost": 700,
				"buildTimeSeconds": 45.0,
				"commandPoints": 35,
				"memberCount": 1,
				"memberHealth": 2500,
				"speed": 50.0,
				"visionRange": 400.0,
				"combat": {
					"attackRange": 30.0, "minimumAttackRange": 0.0,
					"delayBetweenShotsMs": 1000.0, "preAttackDelayMs": 250.0,
					"firingDurationMs": 250.0, "damage": damage,
					"damageType": "slash",
				},
				"movement": {"acceleration": 100.0, "braking": 100.0, "turnRateDegreesPerSecond": 360.0},
				"formation": {"memberCount": 1, "positions": [{"x": 0.0, "y": 0.0}]},
				"resolved": {
					"armor": {
						"setId": "FixtureUnitArmor",
						"semantic": "authored-armor-set",
						"table": {
							"default": {"percent": 100.0},
							"damageScalar": {"percent": 100.0},
							"scalars": {},
						},
						"upgrades": [],
					},
					"combat": {"damageType": "slash", "upgrades": []},
				},
			},
			"capabilities": [{"id": "move"}],
			"visual": {"components": [], "coreAnimations": {}},
			"ui": {"portraitImageIds": [], "commands": []},
			"imageBindings": {},
			"audioRoutes": {"container": {}, "primaryMember": {}},
			"audioBindings": {},
			"audioResolution": {},
			"unsupportedCapabilities": [],
		},
	}


func _lifecycle_of(document: Dictionary) -> Dictionary:
	return ((document.get("registration", {}) as Dictionary).get("presentation", {}) as Dictionary).get("buildingLifecycle", {}) as Dictionary


func _check_skipped_variant(content_db, pack_root: String, label: String, mutate: Callable) -> void:
	var broken := _fixture_document("VariantPen", "variantpen")
	mutate.call(broken)
	_write_json(pack_root.path_join("data/playable-structures/variant.json"), broken)
	var before: Dictionary = content_db.get_playable_structure_runtimes()
	var loaded: bool = content_db._load_playable_structure_runtimes(pack_root, {
		"playableStructure.variant": "data/playable-structures/variant.json",
	})
	_check(loaded and not content_db.get_playable_structure_runtimes().has("VariantPen"), "%s variant is skipped" % label)
	_check(content_db.get_playable_structure_runtimes() == before, "%s skip is atomic" % label)


func _build_fixture(pack_root: String) -> void:
	DirAccess.make_dir_recursive_absolute(pack_root.path_join("data/playable-structures"))
	for model_slug in ["fixturemonsterpen", "variantpen"]:
		DirAccess.make_dir_recursive_absolute(pack_root.path_join("assets/models/structures/%s" % model_slug))
		for stem in ["construction", "intact", "damaged", "rubble", "bib"]:
			_write_bytes(
				pack_root.path_join("assets/models/structures/%s/%s.glb" % [model_slug, stem]),
				PackedByteArray([7, 8, 9])
			)
	_write_json(pack_root.path_join("data/playable-structures/fixturemonsterpen.json"), _fixture_document("FixtureMonsterPen", "fixturemonsterpen"))
	_write_json(pack_root.path_join("data/playable-structures/fixturemonsterpen2.json"), _fixture_document("SecondPen", "secondpen", "fixturemonsterpen"))


func _runtime_id(source_id: String) -> String:
	## Mirrors ContentDB._playable_runtime_id (camel-splitting bundle id rule).
	var output := ""
	var previous_dash := false
	for index in source_id.length():
		var code := source_id.unicode_at(index)
		var is_upper := code >= 65 and code <= 90
		var is_lower := code >= 97 and code <= 122
		var is_digit := code >= 48 and code <= 57
		if is_upper and index > 0 and not previous_dash:
			var previous := source_id.unicode_at(index - 1)
			if (previous >= 97 and previous <= 122) or (previous >= 48 and previous <= 57):
				output += "-"
		if is_upper or is_lower or is_digit:
			output += String.chr(code).to_lower()
			previous_dash = false
		elif not previous_dash and output != "":
			output += "-"
			previous_dash = true
	return "bfme2.object." + output.trim_suffix("-")


func _fixture_phase(phase: String, visual: Dictionary, animation: Dictionary, next_phase: Variant, conditions: Array) -> Dictionary:
	return {
		"phase": phase,
		"sourceConditionSets": conditions,
		"transitionAuthority": "deterministic-simulation",
		"visual": visual,
		"animation": animation,
		"nextPhase": next_phase,
	}


func _fixture_document(object_id: String, slug: String, asset_slug: String = "", maximum_health: int = 3000, with_thresholds: bool = true, with_construction: bool = true) -> Dictionary:
	var model_slug := asset_slug if asset_slug != "" else ("variantpen" if slug == "variantpen" else "fixturemonsterpen")
	var damaged := maximum_health - 1000
	var really_damaged := maximum_health - 2000
	var glb := func(stem: String) -> Dictionary:
		return {
			"mode": "glb",
			"glb": "assets/models/structures/%s/%s.glb" % [model_slug, stem],
			"modelResourceId": "structure-%s-%s" % [slug, stem],
		}
	var none_animation := {"clip": null, "mode": "none"}
	var no_render := {"mode": "no-render", "sourceIdentifier": "None"}
	var health_primary := {
		"module": "StructureBody ModuleTag_01",
		"sourceIni": "data/ini/object/fixture.ini",
		"line": 10,
		"maxHealth": {"authored": str(maximum_health), "value": maximum_health},
	}
	if with_thresholds:
		health_primary["maxHealthDamaged"] = {"authored": str(damaged), "value": damaged}
		health_primary["maxHealthReallyDamaged"] = {"authored": str(really_damaged), "value": really_damaged}
	var phases: Array = []
	if with_construction:
		phases.append(_fixture_phase("construction", glb.call("construction"), {"clip": "fixture_abld", "mode": "manual-progress"}, "intact", [["ACTIVELY_BEING_CONSTRUCTED", "PARTIALLY_CONSTRUCTED"]]))
	var intact_next := "damaged" if with_thresholds else "collapsing"
	phases.append(_fixture_phase("intact", glb.call("intact"), none_animation.duplicate(), intact_next, [[]]))
	if with_thresholds:
		phases.append(_fixture_phase("damaged", glb.call("damaged"), none_animation.duplicate(), "really-damaged", [["DAMAGED"]]))
		phases.append(_fixture_phase("really-damaged", glb.call("damaged"), none_animation.duplicate(), "collapsing", [["REALLYDAMAGED"]]))
	phases.append(_fixture_phase("collapsing", glb.call("rubble"), {"clip": "fixture_dies", "mode": "once"}, "rubble", [["COLLAPSING"]]))
	phases.append(_fixture_phase("rubble", glb.call("rubble"), none_animation.duplicate(), "post-rubble", [["RUBBLE"]]))
	phases.append(_fixture_phase("post-rubble", no_render.duplicate(), none_animation.duplicate(), null, [["POST_RUBBLE"]]))
	phases.append(_fixture_phase("post-collapse", no_render.duplicate(), none_animation.duplicate(), null, [["POST_COLLAPSE"]]))
	var facts := {
		"maximumHealth": maximum_health,
		"collapse": {"module": null, "status": "no-authored-structure-collapse-update"},
		"postRubble": {"terminalDuration": "retained-until-explicit-destruction"},
	}
	if with_thresholds:
		facts["damageStateRule"] = {"damagedThreshold": damaged, "reallyDamagedThreshold": really_damaged}
	else:
		facts["damageStateRuleStatus"] = "no-authored-damage-thresholds"
	if with_construction:
		facts["construction"] = {"buildTimeSeconds": 30.0, "animationMode": "MANUAL", "animation": "fixture_abld"}
	else:
		facts["construction"] = {"status": "never-constructed-engine-spawned-composite"}
	var production := {
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
	}
	if not with_construction:
		production = {"evidence": "engine-spawned-composite", "routes": []}
	return {
		"schema": "openbfme.playable-structure-runtime",
		"schemaVersion": 0,
		"objectId": object_id,
		"slug": slug,
		"descriptorSha256": "1".repeat(64),
		"recipeSha256": "2".repeat(64),
		"runtimeSha256": "3".repeat(64),
		"lifecycleEvidenceSha256": "4".repeat(64),
		"registration": {
			"production": production,
			"gameplay": {
				"armor": {
					"setId": "FixtureStructureArmor",
					"semantic": "authored-armor-set",
					"table": {
						"default": {"percent": 100.0},
						"damageScalar": {"percent": 100.0},
						"scalars": {},
					},
				},
				"health": {
					"primary": health_primary,
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
					"evidenceProfile": "composed-structure-runtime",
					"objectId": _runtime_id(object_id),
					"initialPhase": "intact",
					"phases": phases,
					"phaseCoverage": {
						"covered": ["construction", "intact", "damaged", "rubble"] if with_construction and with_thresholds else ["intact", "rubble"],
						"missing": ["really-damaged", "post-rubble"] if with_construction and with_thresholds else ["construction", "damaged", "really-damaged", "post-rubble"],
					},
					"bib": {
						"drawModule": "W3DFloorDraw",
						"duringConstruction": false,
						"hideIfModelConditions": ["AWAITING_CONSTRUCTION", "PARTIALLY_CONSTRUCTED"],
						"sourceConditions": [],
						"startHiddenAuthored": false,
						"visibility": "condition-driven-authored-floor-draw",
						"visual": glb.call("bib"),
					},
					"audioEvents": {"collapse": null, "construction": null},
					"audioBindings": [],
					"effects": {
						"collapseUpdateFx": {},
						"definitionTranslationStatus": "requires-exact-definition-runtime-binding",
						"enteringStateFx": {},
						"enteringStateBindings": [],
						"particleAttachments": [],
					},
					"simulationFacts": facts,
					"rebuildHole": null,
					"compositionExclusions": [],
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

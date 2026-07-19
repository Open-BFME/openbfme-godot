extends SceneTree
## Focused fail-closed gate for the generic playable-structure runtime registry
## and the data-driven faction manifest built on top of it.
## Mirrors playable_unit_runtime_consumer_runner.gd: synthetic fixture packs
## only, no retail content, and every malformed delta must reject atomically.

const FactionManifest = preload("res://src/retail_slice/retail_faction_manifest.gd")
const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")

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

	_run_faction_manifest_checks()
	_finish()


func _run_faction_manifest_checks() -> void:
	var structures := {
		"FixtureFortress": _fixture_document("FixtureFortress", "fixturefortress", "fixturemonsterpen", 5000),
		"FixtureMonsterPen": _fixture_document("FixtureMonsterPen", "fixturemonsterpen"),
	}
	var units := {
		"FixtureMonster": _fixture_unit_document("FixtureMonster", "FixtureMonsterPen", 200),
		"FixturePorter": _fixture_unit_document("FixturePorter", "FixtureFortress", 1),
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
	_check((manifest.get("producer_kind_registry", {}) as Dictionary) == {"FixtureFortress": "fortress", "FixtureMonsterPen": "monsterpen"}, "producer registry maps source objects to kinds")
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
	_check(sim.structure_ids(0).size() == 2 and sim.structure_ids(1).size() == 2, "base loop seeds both teams from structure documents")
	_check(sim.fortress_id(0) != 0 and sim.fortress_id(1) != 0, "fortress kind is normalized so the base anchor resolves")
	_check(sim.structure_maximum_health("monsterpen") == 3000, "structure health flows from the manifest")
	_check(sim.initial_battalion_count() == 7 and sim.entity_ids().size() == 7, "faction spawn roster fills every anchor slot")
	_check(bool(sim.entity(3).get("is_builder", false)) and bool(sim.entity(104).get("is_builder", false)), "builder flag rides the manifest builder unit ids")
	_check(String(sim.entity(1).get("object_id", "")) == "bfme2.object.fixture-monster", "player spawn identity is descriptor-driven")
	var producer: int = sim.producer_id(0, "monsterpen")
	var queued: Dictionary = sim.queue_unit(0, producer, "bfme2.object.fixture-monster")
	_check(bool(queued.get("ok", false)), "faction producer trains its declared unit: %s" % String(queued.get("reason", "")))
	var wrong: Dictionary = sim.queue_unit(0, sim.fortress_id(0), "bfme2.object.fixture-monster")
	_check(not bool(wrong.get("ok", true)) and String(wrong.get("reason", "")) == "unsupported-unit", "fortress rejects units it does not train")

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
	_check(String(missing_producer.get("_error", "")).contains("GondorBarracks"), "unknown producer fails closed naming the structure")
	var empty_faction := FactionManifest.from_registries("rohan", units, structures)
	_check(String(empty_faction.get("_error", "")).contains("rohan"), "unconverted faction fails closed naming the faction")

	# Canonical UI faction names map to the retail Object prefixes, and
	# engine-spawned fortress parts remain presentation-only resources.
	var elven_fortress := _fixture_document("ElvenFortress", "elvenfortress", "elvenmonsterpen", 5000)
	var elven_pen := _fixture_document("ElvenMonsterPen", "elvenmonsterpen")
	for document_value in [elven_fortress, elven_pen]:
		var production: Dictionary = ((document_value as Dictionary).get("registration", {}) as Dictionary).get("production", {}) as Dictionary
		for route_value in production.get("routes", []) as Array:
			(route_value as Dictionary)["builderObjectId"] = "ElvenPorter"
	var elven_citadel := _fixture_document("ElvenCitadel", "elvencitadel")
	((elven_citadel.get("registration", {}) as Dictionary).get("production", {}) as Dictionary)["evidence"] = "engine-spawned-composite"
	((elven_citadel.get("registration", {}) as Dictionary).get("production", {}) as Dictionary)["routes"] = []
	var elven_structures := {"ElvenFortress": elven_fortress, "ElvenCitadel": elven_citadel, "ElvenMonsterPen": elven_pen}
	var elven_units := {
		"ElvenMonster": _fixture_unit_document("ElvenMonster", "ElvenMonsterPen", 200),
		"ElvenPorter": _fixture_unit_document("ElvenPorter", "ElvenFortress", 1),
	}
	var elves := FactionManifest.from_registries("elves", elven_units, elven_structures)
	_check(not elves.has("_error"), "elves alias resolves Elven Object prefixes: %s" % String(elves.get("_error", "")))
	_check(Array(elves.get("structure_kinds", [])) == ["fortress", "monsterpen"], "engine-spawned fortress composites are not independent structures")


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
				},
				"movement": {"acceleration": 100.0, "braking": 100.0, "turnRateDegreesPerSecond": 360.0},
				"formation": {"memberCount": 1, "positions": [{"x": 0.0, "y": 0.0}]},
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


func _fixture_document(object_id: String, slug: String, asset_slug: String = "", maximum_health: int = 3000) -> Dictionary:
	var model_slug := asset_slug if asset_slug != "" else ("variantpen" if slug == "variantpen" else "fixturemonsterpen")
	var damaged := maximum_health - 1000
	var really_damaged := maximum_health - 2000
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
						"maxHealth": {"authored": str(maximum_health), "value": maximum_health},
						"maxHealthDamaged": {"authored": str(damaged), "value": damaged},
						"maxHealthReallyDamaged": {"authored": str(really_damaged), "value": really_damaged},
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
						"maxHealth": maximum_health,
						"damageStateRule": {"damagedThreshold": damaged, "reallyDamagedThreshold": really_damaged},
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

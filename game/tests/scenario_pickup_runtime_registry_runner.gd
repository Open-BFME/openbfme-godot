extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const GAME := "bfme2"

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
	content_db.scenario_pickup_runtimes.clear()
	content_db.scenario_pickup_runtimes[GAME] = {}
	var units_before: Dictionary = content_db.playable_unit_runtimes.duplicate(true)
	var scenario_units_before: Dictionary = content_db.scenario_unit_runtimes.duplicate(true)
	var structures_before: Dictionary = content_db.playable_structure_runtimes.duplicate(true)
	var scenario_structures_before: Dictionary = content_db.scenario_structure_runtimes.duplicate(true)
	var props_before: Dictionary = content_db.scenario_prop_runtimes.duplicate(true)
	var member_index_before: Dictionary = content_db.playable_unit_runtime_member_index.duplicate(true)
	var bundle_before: Dictionary = content_db.bundle_objects.duplicate(true)
	var animation_before: Dictionary = content_db.animation_capabilities.duplicate(true)

	var pickup := _pickup_document("TreasureChest1")
	_check(content_db._validate_scenario_pickup_runtime(pickup), "exact pickup runtime validates")
	var deferred := pickup.duplicate(true)
	deferred.runtimeStatus = "deferred"
	_check(not content_db._validate_scenario_pickup_runtime(deferred), "deferred pickup is not shipping-admitted")
	var faction_leak := pickup.duplicate(true)
	faction_leak.registration = {"production": [{"surface": "construct"}]}
	_check(not content_db._validate_scenario_pickup_runtime(faction_leak), "faction registration field fails closed")

	var fixture_root := "user://scenario-pickup-runtime-registry-fixture"
	content_db.neutral_pack_receipts[GAME] = {"game": GAME, "_pack_root": fixture_root}
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(fixture_root + "/data/neutral-pickups"))
	_write_json(fixture_root + "/data/neutral-pickups/treasurechest1.json", pickup)
	_write_json(fixture_root + "/data/neutral-pickups/treasurechest2.json", _pickup_document("TreasureChest2"))
	_check(content_db._load_scenario_pickup_runtimes(fixture_root, {
		"neutralPickup.treasurechest1": "data/neutral-pickups/treasurechest1.json",
		"neutralPickup.wrongslug": "data/neutral-pickups/treasurechest2.json",
		"playableUnit.treasurechest1": "data/neutral-pickups/treasurechest1.json",
	}), "pickup loader accepts a mixed manifest delta")
	_check(String(content_db.get_scenario_pickup_runtime(GAME, "TreasureChest1", "object-creation-list").get("objectId", "")) == "TreasureChest1", "pickup resolves by exact id and OCL surface")
	_check(String(content_db.get_scenario_pickup_runtime(GAME, "treasurechest1", "object-creation-list").get("objectId", "")) == "TreasureChest1", "pickup lookup is case insensitive")
	_check(content_db.get_scenario_pickup_runtime(GAME, "TreasureChest1", "map-placement").is_empty(), "pickup rejects map placement")
	_check(content_db.get_scenario_pickup_runtime(GAME, "TreasureChest1", "construct").is_empty(), "pickup rejects production surface")
	_check(content_db.get_scenario_pickup_runtime(GAME, "TreasureChest2", "object-creation-list").is_empty(), "manifest key identity drift is skipped")
	_check(content_db.get_scenario_pickup_runtimes(GAME).size() == 1, "pickup registry snapshot contains only admitted runtime")
	var sim: RetailSliceSim = Sim.new()
	sim._rules = {"game": GAME, "enable_scenario_map_placements": true}
	sim._snapshot_scenario_runtime_tables()
	_check((sim._rules.get("scenario_pickup_runtimes", {}) as Dictionary).size() == 1, "sim snapshots the shipping pickup registry")
	_check(String(sim.scenario_spawn_contract("TreasureChest1", "object-creation-list").get("kind", "")) == "pickup", "shipping pickup reaches the authoritative scenario admission seam")
	_check(content_db.playable_unit_runtimes == units_before and content_db.scenario_unit_runtimes == scenario_units_before, "pickup loader does not leak into unit registries")
	_check(content_db.playable_structure_runtimes == structures_before and content_db.scenario_structure_runtimes == scenario_structures_before, "pickup loader does not leak into structure registries")
	_check(content_db.scenario_prop_runtimes == props_before, "pickup loader does not leak into passive props")
	_check(content_db.playable_unit_runtime_member_index == member_index_before, "pickup loader does not leak into member projection")
	_check(content_db.bundle_objects == bundle_before and content_db.animation_capabilities == animation_before, "pickup loader does not leak into presentation or HUD projections")
	content_db.scenario_pickup_runtimes.clear()
	_finish()


func _pickup_document(object_id: String) -> Dictionary:
	var fields := {
		"ForbiddenKindOf": {"value": ["PROJECTILE", "ENVIRONMENT"]},
		"ExecuteFX": {"value": "FX_GoldChestPickup"},
		"BannerChance": {"ratio": 0.0, "percent": 0.0},
		"LevelUpChance": {"ratio": 0.0, "percent": 0.0},
		"ResourceChance": {"ratio": 1.0, "percent": 100.0},
		"LevelUpRadius": {"value": 100.0},
		"MinResource": {"value": 160},
		"MaxResource": {"value": 200},
		"AllowAIPickup": {"value": true},
	}
	return {
		"schema": "openbfme.neutral-pickup-runtime",
		"schemaVersion": 0,
		"game": "bfme2",
		"objectId": object_id,
		"runtimeDomain": "active-pickup",
		"runtimeStatus": "executable",
		"descriptorSha256": "1".repeat(64),
		"recipeSha256": "2".repeat(64),
		"resourceIds": ["neutral-pickup-%s-intact-pchesttreasure" % object_id.to_lower()],
		"production": [],
		"scenarioAdmission": {
			"kind": "authored-ocl-pickup-leaf",
			"surfaces": ["object-creation-list"],
			"buildCommandExposed": false,
			"evidence": "reachable-neutral-lair-treasure-ocl",
		},
		"kindOf": {"effective": ["CRATE", "IMMOBILE", "UNATTACKABLE"], "defineProvenance": []},
		"geometry": {"footprint": {"radius": 12.0}},
		"pickupContract": {
			"module": "SalvageCrateCollide",
			"extraction": "typed",
			"runtimeStatus": "executable",
			"fields": fields,
		},
		"binaryOracleReceipt": {
			"domain": "active-collision-pickup",
			"activeWhenAuthored": ["AllowAIPickup", "LevelUpChance", "MaxResource", "MinResource", "Upgrade"],
			"deadBranchWhenAuthored": ["LevelUpRadius"],
			"parsedIgnoredWhenAuthored": ["BannerChance", "PorterChance", "ResourceChance"],
			"authoredFields": ["AllowAIPickup", "BannerChance", "ExecuteFX", "ForbiddenKindOf", "LevelUpChance", "LevelUpRadius", "MaxResource", "MinResource", "ResourceChance"],
		},
		"presentation": {"lifecycleStates": [], "bibStates": []},
		"runtimeSha256": "3".repeat(64),
	}


func _write_json(path: String, value: Dictionary) -> void:
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		_fail("cannot write fixture %s" % path)
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
	push_error("SCENARIO_PICKUP_RUNTIME_REGISTRY_FAIL: %s" % label)


func _finish() -> void:
	if failed == 0:
		print("SCENARIO_PICKUP_RUNTIME_REGISTRY_OK passed=%d" % passed)
		quit(0)
	else:
		print("SCENARIO_PICKUP_RUNTIME_REGISTRY_FAIL passed=%d failed=%d" % [passed, failed])
		quit(1)

extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var unit := _unit_document()
	var structure := _structure_document()
	var prop := _prop_document()
	var sim := Sim.new()
	sim.setup({}, {
		"game": "rotwk",
		"source_map_transform_scale": 0.1,
		"spawn_initial_battalions": false,
		"scenario_unit_runtimes": {unit.objectId: unit},
		"scenario_structure_runtimes": {structure.objectId: structure},
		"scenario_prop_runtimes": {prop.objectId: prop},
	})

	for surface in ["map-placement", "script-spawn", "object-creation-list", "lair-spawn"]:
		var contract := sim.scenario_spawn_contract("neutralwolf", surface)
		_check(String(contract.get("kind", "")) == "unit", "unit resolves on authored %s surface" % surface)
		_check(String((contract.get("presentation", {}) as Dictionary).get("fixture", "")) == "wolf-visual", "unit presentation reaches sim on %s surface" % surface)
		_check(int((contract.get("unit_rule", {}) as Dictionary).get("member_health", 0)) == 321, "unit simulation rule reaches sim on %s surface" % surface)
	_check(sim.scenario_spawn_contract("NeutralWolf", "tutorial-script").is_empty(), "unit rejects allowed but unauthored surface")
	_check(sim.scenario_spawn_contract("NeutralWolf", "command-socket").is_empty(), "unit rejects production surface")

	var structure_contract := sim.scenario_spawn_contract("neutrallair", "lair-spawn")
	_check(String(structure_contract.get("kind", "")) == "structure", "lair descriptor reaches lair-spawn lookup")
	_check(String((structure_contract.get("presentation", {}) as Dictionary).get("fixture", "")) == "lair-visual", "lair presentation reaches sim lookup")
	_check(sim.scenario_spawn_contract("NeutralLair", "object-creation-list").is_empty(), "lair rejects unauthored OCL surface")
	var prop_contract := sim.scenario_spawn_contract("spiderwebs01", "object-creation-list")
	_check(String(prop_contract.get("kind", "")) == "prop", "prop descriptor reaches OCL lookup")
	_check(String((prop_contract.get("presentation", {}) as Dictionary).get("fixture", "")) == "web-visual", "prop presentation reaches sim lookup")
	_check(sim.scenario_spawn_contract("SpiderWebs01", "lair-spawn").is_empty(), "prop rejects non-prop surface")

	var map_id := sim.spawn_scenario_unit("NeutralWolf", Sim.CREEP_TEAM, Vector2(1, 2), "map-placement")
	# Creep's dynamic id space is normally enabled by the ring path. This runner
	# uses an ordinary rostered team for real spawn surfaces below; the refusal
	# proves an unseeded team does not invent an id range.
	_check(map_id == -1, "scenario spawn refuses an unseeded team id space")
	var spawned_map := sim.spawn_scenario_unit("NeutralWolf", Sim.PLAYER_TEAM, Vector2(1, 2), "map-placement")
	_check(spawned_map > 0 and sim.entities.has(spawned_map), "map placement spawns descriptor-backed unit")
	_check(String((sim.entities[spawned_map] as Dictionary).get("scenario_spawn_surface", "")) == "map-placement", "map spawn records exact admitted surface")
	var spawned_lair := sim.spawn_scenario_unit("NeutralWolf", Sim.PLAYER_TEAM, Vector2(3, 4), "lair-spawn")
	_check(spawned_lair > 0, "lair payload spawns descriptor-backed unit")
	var script_result := sim.script_spawn_entity("NeutralWolf", Sim.PLAYER_TEAM, Vector2(5, 6))
	_check(bool(script_result.get("ok", false)), "script spawn consumes scenario descriptor")
	var script_row := sim.entities.get(int(script_result.get("entity_id", -1)), {}) as Dictionary
	_check(String(script_row.get("scenario_spawn_surface", "")) == "script-spawn", "script spawn records exact surface")

	sim.register_ocl_leaf("OCL_NeutralWolf", {
		"id": "OCL_NeutralWolf",
		"createObjects": [{"objects": ["NeutralWolf"], "fields": []}],
	})
	var hatch := sim.hatch_create_object_die_entry({
		"creation_list": "OCL_NeutralWolf",
		"team": Sim.PLAYER_TEAM,
		"position": Vector2(7, 8),
		"source_entity": 0,
	})
	_check(bool(hatch.get("ok", false)), "OCL hatch consumes scenario descriptor")
	var ocl_ids := hatch.get("spawned", []) as Array
	_check(not ocl_ids.is_empty() and String((sim.entities[int(ocl_ids[0])] as Dictionary).get("scenario_spawn_surface", "")) == "object-creation-list", "OCL spawn records OCL surface")

	var before_wrong := sim.entities.size()
	var wrong := sim.script_spawn_entity("NeutralWolf", Sim.PLAYER_TEAM, Vector2.ZERO, "tutorial-script")
	_check(not bool(wrong.get("ok", false)) and String(wrong.get("reason", "")).begins_with("scenario-admission-rejected"), "known scenario identity fails closed on wrong surface")
	_check(sim.entities.size() == before_wrong, "wrong-surface refusal creates no fallback entity")
	var missing := sim.script_spawn_entity("MissingScenarioObject", Sim.PLAYER_TEAM, Vector2.ZERO)
	_check(not bool(missing.get("ok", false)) and String(missing.get("reason", "")).begins_with("scenario-document-missing"), "configured scenario lane fails closed when descriptor is absent")
	_check(sim.entities.size() == before_wrong, "missing-descriptor refusal creates no fallback entity")
	_check(not (sim._rules.get("unit_rules", {}) as Dictionary).has("NeutralWolf"), "scenario unit never enters ordinary unit rules")
	_check(not sim._unit_production_rules.has("NeutralWolf") and not sim._unit_production_rules.has("bfme2.object.neutral-wolf"), "scenario unit never enters production rules")
	_check((unit.registration as Dictionary).production.is_empty(), "scenario unit carries no producer route")
	_check(not (unit.registration as Dictionary).has("ui"), "scenario unit exposes no HUD registration")

	var malformed := unit.duplicate(true)
	((malformed.registration as Dictionary).scenarioAdmission as Dictionary).surfaces = ["map-placement", "map-placement"]
	sim._rules.scenario_unit_runtimes["MalformedWolf"] = malformed
	malformed.objectId = "MalformedWolf"
	_check(sim.scenario_spawn_contract("MalformedWolf", "map-placement").is_empty(), "directly mutated malformed descriptor fails closed")

	_finish()


func _unit_document() -> Dictionary:
	return {
		"objectId": "NeutralWolf",
		"category": "monster",
		"registration": {
			"production": [],
			"composition": {"containerObjectId": "NeutralWolf", "primaryMemberObjectId": "NeutralWolf"},
			"scenarioAdmission": {
				"kind": "authored-non-buildable", "role": "creature",
				"surfaces": ["map-placement", "script-spawn", "object-creation-list", "lair-spawn"],
				"buildCommandExposed": false, "evidence": "no-authored-unit-build-route",
				"sourceIni": "data/ini/object/neutral/wolf.ini", "line": 17,
			},
			"presentation": {"fixture": "wolf-visual"},
			"simulation": {
				"displayName": "Neutral Wolf", "buildCost": 0, "buildTimeSeconds": 1.0,
				"commandPoints": 1, "memberCount": 1, "memberHealth": 321,
				"speed": 90.0, "visionRange": 200.0,
				"movement": {"acceleration": 90.0, "braking": 90.0, "turnRateDegreesPerSecond": 360.0},
				"formation": {"positions": [{"x": 0.0, "y": 0.0}]},
				"combat": {"attackRange": 20.0, "delayBetweenShotsMs": 1500.0, "preAttackDelayMs": 500.0, "firingDurationMs": 500.0, "damage": 45},
			},
		},
	}


func _structure_document() -> Dictionary:
	return {
		"objectId": "NeutralLair",
		"registration": {
			"production": {"evidence": "authored-neutral-map", "routes": []},
			"scenarioAdmission": {
				"kind": "authored-neutral-non-buildable", "role": "lair",
				"surfaces": ["map-placement", "script-spawn", "lair-spawn"],
				"buildCommandExposed": false,
			},
			"visual": {"fixture": "lair-visual"},
		},
	}


func _prop_document() -> Dictionary:
	return {
		"objectId": "SpiderWebs01", "production": [],
		"scenarioAdmission": {
			"kind": "authored-passive-prop",
			"surfaces": ["map-placement", "script-spawn", "object-creation-list"],
			"buildCommandExposed": false,
		},
		"presentation": {"fixture": "web-visual"},
	}


func _check(condition: bool, label: String) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("SCENARIO_RUNTIME_SIM_WIRING_FAIL: %s" % label)


func _finish() -> void:
	if failed == 0:
		print("SCENARIO_RUNTIME_SIM_WIRING_OK passed=%d" % passed)
		quit(0)
	else:
		print("SCENARIO_RUNTIME_SIM_WIRING_FAIL passed=%d failed=%d" % [passed, failed])
		quit(1)

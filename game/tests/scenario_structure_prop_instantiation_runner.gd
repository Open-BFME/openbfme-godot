extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var structure := _structure_document()
	var prop := _prop_document()
	var sim := Sim.new()
	sim.setup({}, {
		"game": "rotwk",
		"spawn_initial_battalions": false,
		"source_map_transform_scale": 0.1,
		"scenario_structure_runtimes": {structure.objectId: structure},
		"scenario_prop_runtimes": {prop.objectId: prop},
	})
	sim.ai_enabled = false
	sim.base_loop_enabled = false

	var map_id := sim.spawn_scenario_structure("NeutralLair", -1, Vector2(10, 20), "map-placement")
	_check(map_id > 0 and sim.structures.has(map_id), "map placement enters authoritative structure table")
	var map_row := sim.structures.get(map_id, {}) as Dictionary
	_check(int(map_row.get("health", 0)) == 777 and int(map_row.get("maximum_health", 0)) == 777, "authored maximum health enters exact structure body")
	_check(int(map_row.get("team", 0)) == -1 and not map_row.has("scenario_authored_owner"), "unowned placement does not invent ownership")
	_check((map_row.get("production", []) as Array).is_empty(), "scenario structure exposes no production")
	_check(String((map_row.get("scenario_admission_receipt", {}) as Dictionary).get("role", "")) == "lair", "lair role receipt is exact")
	_check(String((map_row.get("scenario_lifecycle_receipt", {}) as Dictionary).get("fixture", "")) == "exact-lifecycle", "lifecycle receipt reaches authoritative row")
	_check(String((map_row.get("scenario_presentation", {}) as Dictionary).get("fixture", "")) == "lair-visual", "presentation receipt reaches authoritative row")
	_check(int(map_row.get("bounty_value", -1)) == 55, "authored structure bounty reaches row without defaulting")

	var lair_id := sim.spawn_scenario_structure("NeutralLair", Sim.CREEP_TEAM, Vector2(30, 40), "lair-spawn")
	_check(lair_id > 0 and int((sim.structures[lair_id] as Dictionary).get("scenario_authored_owner", -1)) == Sim.CREEP_TEAM, "lair-spawn preserves caller-authored owner")
	var structures_before_wrong := sim.structures.size()
	_check(sim.spawn_scenario_structure("NeutralLair", -1, Vector2.ZERO, "object-creation-list") == -1, "unauthored structure OCL surface fails closed")
	_check(sim.structures.size() == structures_before_wrong, "wrong structure surface mutates no state")

	var script_structure := sim.script_spawn_entity("NeutralLair", Sim.PLAYER_TEAM, Vector2(50, 60))
	_check(bool(script_structure.get("ok", false)) and String(script_structure.get("object_kind", "")) == "structure", "script surface instantiates scenario structure")
	var script_structure_id := int(script_structure.get("structure_id", -1))
	_check(sim.structures.has(script_structure_id) and not sim.entities.has(script_structure_id), "script structure stays out of unit entities")

	var prop_id := sim.spawn_scenario_prop("SpiderWebs01", Vector2(70, 80), "map-placement")
	_check(prop_id > 0 and sim.scenario_props.has(prop_id), "map placement enters passive prop registry")
	var prop_row := sim.scenario_props.get(prop_id, {}) as Dictionary
	_check(Vector2(prop_row.get("position", Vector2.ZERO)) == Vector2(70, 80), "prop placement transform is exact")
	_check(String((prop_row.get("scenario_presentation", {}) as Dictionary).get("fixture", "")) == "web-visual", "prop presentation receipt is exact")
	_check(not prop_row.has("team") and not prop_row.has("health"), "passive prop has no invented ownership or body")
	_check(not sim.entities.has(prop_id) and not sim.structures.has(prop_id), "passive prop cannot enter combat registries")
	var props_before_wrong := sim.scenario_props.size()
	_check(sim.spawn_scenario_prop("SpiderWebs01", Vector2.ZERO, "lair-spawn") == -1, "prop rejects unauthored lair surface")
	_check(sim.scenario_props.size() == props_before_wrong, "wrong prop surface mutates no state")

	var script_prop := sim.script_spawn_entity("SpiderWebs01", Sim.PLAYER_TEAM, Vector2(90, 100))
	_check(bool(script_prop.get("ok", false)) and String(script_prop.get("object_kind", "")) == "prop", "script surface instantiates passive prop")
	_check(sim.scenario_props.has(int(script_prop.get("prop_id", -1))), "script prop returns its registry identity")

	# OCL is admitted for props and must use the same generic boundary.
	sim.register_ocl_leaf("OCL_Web", {
		"id": "OCL_Web",
		"createObjects": [{"objects": ["SpiderWebs01"], "fields": []}],
	})
	var hatch := sim.hatch_create_object_die_entry({
		"creation_list": "OCL_Web", "team": Sim.PLAYER_TEAM,
		"position": Vector2(110, 120), "source_entity": 0,
	})
	_check(bool(hatch.get("ok", false)) and (hatch.get("spawned", []) as Array).size() == 1, "OCL instantiates admitted prop")
	_check(sim.scenario_props.has(int((hatch.get("spawned", []) as Array)[0])), "OCL prop remains in passive registry")

	_check(not sim._structure_kinds.has("lair") and not sim._unit_production_rules.has("NeutralLair"), "scenario placements do not register faction production")
	var hash_before := sim.state_hash()
	var snapshot := sim.snapshot()
	var restored := Sim.new()
	restored.setup({}, {"spawn_initial_battalions": false})
	_check(restored.restore(snapshot), "scenario placement snapshot restores")
	_check(restored.state_hash() == hash_before, "scenario structure and prop state hash round-trips")
	_check(restored.structures.has(map_id) and restored.scenario_props.has(prop_id), "restored registries retain both placement kinds")

	# Direct-injection corruption still fails at instantiation, even though lookup
	# admission itself is syntactically valid.
	var malformed := structure.duplicate(true)
	(((malformed.registration as Dictionary).presentation as Dictionary).buildingLifecycle as Dictionary).simulationFacts.maximumHealth = 778
	malformed.objectId = "MalformedLair"
	sim._rules.scenario_structure_runtimes[malformed.objectId] = malformed
	_check(sim.spawn_scenario_structure("MalformedLair", -1, Vector2.ZERO, "map-placement") == -1, "health/lifecycle mismatch fails closed")

	_finish()


func _structure_document() -> Dictionary:
	return {
		"schema": "openbfme.playable-structure-runtime",
		"objectId": "NeutralLair",
		"registration": {
			"production": {"evidence": "authored-neutral-map", "routes": []},
			"scenarioAdmission": {
				"kind": "authored-neutral-non-buildable", "role": "lair",
				"surfaces": ["map-placement", "script-spawn", "lair-spawn"],
				"buildCommandExposed": false,
			},
			"gameplay": {
				"health": {"primary": {"module": "ActiveBody", "maxHealth": {"authored": "777", "value": 777}}},
				"scalarFields": {"BountyValue": {"authored": "55", "value": 55}},
				"moduleContracts": [],
			},
			"presentation": {
				"fixture": "lair-visual",
				"buildingLifecycle": {"fixture": "exact-lifecycle", "simulationFacts": {"maximumHealth": 777}},
			},
		},
	}


func _prop_document() -> Dictionary:
	return {
		"schema": "openbfme.neutral-prop-descriptor",
		"objectId": "SpiderWebs01",
		"production": [],
		"scenarioAdmission": {
			"kind": "authored-passive-prop",
			"surfaces": ["map-placement", "script-spawn", "object-creation-list"],
			"buildCommandExposed": false,
		},
		"kindOf": {"effective": ["IMMOBILE", "INERT"]},
		"moduleContracts": [],
		"geometry": null,
		"geometryContactPoints": [],
		"publicBones": [],
		"presentation": {"fixture": "web-visual"},
	}


func _check(condition: bool, label: String) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("SCENARIO_STRUCTURE_PROP_INSTANTIATION_FAIL: %s" % label)


func _finish() -> void:
	if failed == 0:
		print("SCENARIO_STRUCTURE_PROP_INSTANTIATION_OK passed=%d" % passed)
		quit(0)
	else:
		print("SCENARIO_STRUCTURE_PROP_INSTANTIATION_FAIL passed=%d failed=%d" % [passed, failed])
		quit(1)

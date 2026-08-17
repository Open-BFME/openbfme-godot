extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const Watchdog = preload("res://tests/runner_watchdog.gd")

var passed := 0
var failed := 0
var watchdog = Watchdog.new()


func _initialize() -> void:
	watchdog.start(self, "SCENARIO_STRUCTURE_ARMOR_PROJECTION_RUNNER")
	call_deferred("_run")


func _run() -> void:
	var capture := _structure("CaptureFlag", "captureflag", "neutral-structure", {"setId": null, "semantic": "no authored ArmorSet; unmodified damage"})
	var signal_fire := _structure("SignalFire", "signalfire", "neutral-structure", _typed_armor("StructureArmor", 60.0, 40.0, 100.0))
	var outpost := _structure("Outpost", "outpost", "neutral-structure", _typed_armor("StructureArmor", 60.0, 40.0, 50.0))
	var lair := _structure("CaveTrollLair", "cavetrolllair", "lair", _typed_armor("MonsterLair", 50.0, 25.0, 100.0))
	var sim := Sim.new()
	sim.setup({}, {
		"game": "rotwk",
		"spawn_initial_battalions": false,
		"faction_manifest": {"structure_armor": _fixture_structure_armor(), "spawn_roster": []},
		"scenario_structure_runtimes": {
			capture.objectId: capture,
			signal_fire.objectId: signal_fire,
			outpost.objectId: outpost,
			lair.objectId: lair,
		},
	})
	_check(sim.configuration_error == "", "valid scenario armor projection configures", sim.configuration_error)
	_check((sim._structure_armor.get("capture_flag", {}) as Dictionary).get("scalars", {}) == {"default": 1.0}, "capture flag projects into the rich capturable runtime kind")
	_check(String((sim._structure_armor.get("signal_fire", {}) as Dictionary).get("set_id", "")) == "StructureArmor", "signal fire projects into the rich capturable runtime kind")
	var outpost_table := sim._structure_armor.get("outpost", {}) as Dictionary
	_check(
		String(outpost_table.get("set_id", "")) == "StructureArmor"
		and is_equal_approx(float(outpost_table.get("damage_scalar", 0.0)), 0.5)
		and is_equal_approx(float((outpost_table.get("scalars", {}) as Dictionary).get("default", 0.0)), 0.6)
		and is_equal_approx(float((outpost_table.get("scalars", {}) as Dictionary).get("slash", 0.0)), 0.4),
		"typed armor projects exact fractions",
	)
	_check(String((sim._structure_armor.get("lair", {}) as Dictionary).get("set_id", "")) == "MonsterLair", "authored lairs share the preserved lair kind")
	var capture_id := sim.spawn_scenario_structure("CaptureFlag", Sim.NEUTRAL_TEAM, Vector2.ZERO, "map-placement")
	var outpost_id := sim.spawn_scenario_structure("Outpost", Sim.NEUTRAL_TEAM, Vector2.ONE, "map-placement")
	var lair_id := sim.spawn_scenario_structure("CaveTrollLair", Sim.CREEP_TEAM, Vector2(2, 2), "map-placement")
	_check(capture_id > 0 and String((sim.structures[capture_id] as Dictionary).get("structure_kind", "")) == "capture_flag", "spawn reuses canonical capturable structure kind")
	_check(outpost_id > 0 and String((sim.structures[outpost_id] as Dictionary).get("structure_kind", "")) == "outpost", "typed spawn reuses projected kind")
	_check(lair_id > 0 and String((sim.structures[lair_id] as Dictionary).get("structure_kind", "")) == "lair", "lair spawn preserves lair kind")

	var conflict_a := _structure("FirstOutpost", "shared-kind", "neutral-structure", _typed_armor("FirstArmor", 60.0, 40.0, 100.0))
	var conflict_b := _structure("SecondOutpost", "shared_kind", "neutral-structure", _typed_armor("SecondArmor", 30.0, 20.0, 100.0))
	var conflict := Sim.new()
	conflict.setup({}, {
		"game": "rotwk",
		"spawn_initial_battalions": false,
		"faction_manifest": {"structure_armor": _fixture_structure_armor(), "spawn_roster": []},
		"scenario_structure_runtimes": {conflict_a.objectId: conflict_a, conflict_b.objectId: conflict_b},
	})
	_check(conflict.configuration_error.begins_with("Scenario structure kind collision 'shared_kind'"), "unequal normalized-kind collisions fail closed")

	for invalid_case in [
		{"label": "empty typed table", "armor": {"setId": "StructureArmor", "table": {}}},
		{"label": "missing damage scalar", "armor": {"setId": "StructureArmor", "table": {"default": {"percent": 60.0}, "scalars": {}}}},
		{"label": "negative default", "armor": _typed_armor("StructureArmor", -1.0, 40.0, 100.0)},
		{"label": "nonfinite scalar", "armor": _typed_armor("StructureArmor", 60.0, NAN, 100.0)},
		{"label": "malformed scalar row", "armor": {"setId": "StructureArmor", "table": {"damageScalar": {"percent": 100.0}, "default": {"percent": 60.0}, "scalars": {"slash": 40.0}}}},
	]:
		var invalid := Sim.new()
		var invalid_document := _structure("InvalidArmor", "invalidarmor", "neutral-structure", (invalid_case as Dictionary).armor)
		invalid.setup({}, {
			"game": "rotwk",
			"spawn_initial_battalions": false,
			"faction_manifest": {"structure_armor": _fixture_structure_armor(), "spawn_roster": []},
			"scenario_structure_runtimes": {invalid_document.objectId: invalid_document},
		})
		_check(invalid.configuration_error.contains("armor is invalid"), "%s fails closed" % String((invalid_case as Dictionary).label), invalid.configuration_error)

	watchdog.stop()
	print("SCENARIO_STRUCTURE_ARMOR_PROJECTION_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _structure(object_id: String, slug: String, role: String, armor: Dictionary) -> Dictionary:
	return {
		"schema": "openbfme.playable-structure-runtime",
		"objectId": object_id,
		"slug": slug,
		"registration": {
			"production": {"evidence": "authored-neutral-map", "routes": []},
			"scenarioAdmission": {"kind": "authored-neutral-non-buildable", "role": role, "surfaces": ["map-placement"], "buildCommandExposed": false},
			"gameplay": {
				"armor": armor,
				"health": {"primary": {"module": "ActiveBody", "maxHealth": {"authored": "1000", "value": 1000}}},
				"moduleContracts": [],
				"scalarFields": {},
			},
			"presentation": {"buildingLifecycle": {"simulationFacts": {"maximumHealth": 1000}}},
		},
	}


func _typed_armor(set_id: String, default_percent: float, slash_percent: float, damage_percent: float) -> Dictionary:
	return {"setId": set_id, "table": {"damageScalar": {"percent": damage_percent}, "default": {"percent": default_percent}, "scalars": {"slash": {"percent": slash_percent}}}}


func _fixture_structure_armor() -> Dictionary:
	var armor := {}
	for kind_value in Sim.STRUCTURE_KINDS:
		armor[String(kind_value)] = {
			"set_id": "FixtureArmor",
			"damage_scalar": 1.0,
			"scalars": {"default": 1.0},
		}
	return armor


func _check(condition: bool, label: String, detail: String = "") -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("SCENARIO_STRUCTURE_ARMOR_PROJECTION_FAIL: %s%s" % [label, " :: " + detail if detail != "" else ""])

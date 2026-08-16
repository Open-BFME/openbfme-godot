extends SceneTree

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
	var document := _scenario_ship_document()
	var registration := document["registration"] as Dictionary
	var admission := registration["scenarioAdmission"] as Dictionary
	_check(
		content_db._validate_playable_unit_scenario_admission(admission, "naval"),
		"authored scenario admission validates",
	)
	_check(
		content_db._validate_playable_unit_core_presentations(
			registration["visual"] as Dictionary,
			(registration["visual"] as Dictionary)["components"] as Array,
			"naval",
		),
		"naval core presentations validate",
	)
	_check(Adapter.producer_bindings(document).is_empty(), "scenario ship exposes no build command")
	_check(
		Adapter.scenario_admission(document, "map-placement") == admission,
		"adapter exposes map placement admission",
	)
	_check(
		Adapter.scenario_admission(document, "ordinary-build").is_empty(),
		"adapter rejects an ordinary build surface",
	)
	content_db.scenario_unit_runtimes.clear()
	content_db.scenario_unit_runtimes["rotwk"] = {document["objectId"]: document}
	_check(
		not content_db.get_scenario_unit_runtime("rotwk", "TutorialElvenBattleShip", "tutorial-script").is_empty(),
		"content registry resolves tutorial ship by authored object id",
	)
	_check(
		content_db.get_scenario_unit_runtime("rotwk", "TutorialElvenBattleShip", "ordinary-build").is_empty(),
		"content registry never admits an ordinary build lookup",
	)
	var projection: Dictionary = content_db._playable_unit_projection(document)
	var states := (projection.get("capability", {}) as Dictionary).get("states", {}) as Dictionary
	_check(
		String((states.get("move", {}) as Dictionary).get("presentationBinding", ""))
		== "transform-locomotion",
		"runtime projection consumes transform locomotion presentation",
	)
	_check(
		String((states.get("attack", {}) as Dictionary).get("presentationBinding", ""))
		== "weapon-effect",
		"runtime projection consumes weapon effect presentation",
	)
	var death := states.get("death", {}) as Dictionary
	_check(
		String(death.get("presentationBinding", "")) == "ship-sink"
		and String((death.get("contract", {}) as Dictionary).get("module", ""))
		== "ShipSlowDeathBehavior",
		"runtime projection consumes the typed ship sink presentation",
	)
	var forged := document.duplicate(true)
	(forged["registration"] as Dictionary)["production"] = [{"surface": "command-socket"}]
	_check(
		Adapter.scenario_admission(forged, "script-spawn").is_empty(),
		"scenario admission cannot coexist with production",
	)
	content_db.scenario_unit_runtimes.clear()
	_finish()


func _scenario_ship_document() -> Dictionary:
	var source := "art/w3d/au/tutorialship.w3d"
	return {
		"objectId": "TutorialElvenBattleShip",
		"category": "naval",
		"registration": {
			"production": [],
			"scenarioAdmission": {
				"kind": "authored-non-buildable",
				"role": "scenario-only",
				"surfaces": ["map-placement", "script-spawn", "tutorial-script"],
				"buildCommandExposed": false,
				"evidence": "no-authored-unit-build-route",
				"sourceIni": "data/ini/object/goodfaction/units/elven/tutorialship.ini",
				"line": 10,
				"declarationKind": "ChildObject",
			},
			"composition": {
				"containerObjectId": "TutorialElvenBattleShip",
				"primaryMemberObjectId": "TutorialElvenBattleShip",
				"members": [{"objectId": "TutorialElvenBattleShip", "count": 1}],
			},
			"visual": {
				"components": [{
					"sourceW3d": source,
					"output": "assets/models/ships/tutorial.glb",
					"default": true,
				}],
				"coreAnimations": {},
				"corePresentations": {
					"idle": {"binding": "static-hull", "modelSourceW3d": source, "evidence": "authored-unconditional-hull-model"},
					"move": {"binding": "transform-locomotion", "modelSourceW3d": source, "evidence": "authored-movement-without-hull-clip"},
					"attack": {"binding": "weapon-effect", "modelSourceW3d": source, "evidence": "authored-combat-without-hull-clip"},
					"death": {
						"binding": "ship-sink",
						"modelSourceW3d": source,
						"contract": {
							"module": "ShipSlowDeathBehavior",
							"extraction": "typed",
							"sourceIni": "data/ini/object/goodfaction/units/elven/tutorialship.ini",
							"line": 42,
						},
					},
				},
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
	push_error("SHIP_SCENARIO_ADMISSION_FAIL: %s" % label)


func _finish() -> void:
	if failed == 0:
		print("SHIP_SCENARIO_ADMISSION_OK passed=%d" % passed)
		quit(0)
	else:
		print("SHIP_SCENARIO_ADMISSION_FAIL passed=%d failed=%d" % [passed, failed])
		quit(1)

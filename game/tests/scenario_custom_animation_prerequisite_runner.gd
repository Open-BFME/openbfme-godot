extends SceneTree

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
	var pack_root := "user://custom-animation-prerequisite-fixture"
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(pack_root + "/assets/fx"))
	_write(pack_root + "/assets/fx/untamed.png", PackedByteArray([1]))
	_write(pack_root + "/assets/fx/untamed2.png", PackedByteArray([2]))
	_write(pack_root + "/assets/fx/untamed.json", "{}".to_utf8_buffer())
	_write(pack_root + "/assets/fx/untamed2.json", "{}".to_utf8_buffer())
	content_db.neutral_pack_receipts["rotwk"] = {"_pack_root": pack_root}
	var request := _request()
	var receipt := {
		"customAnimationPresentationSha256": request.requestSha256,
		"customAnimationEdgeCount": 7,
	}
	_check(content_db._validate_deferred_custom_animation_request(pack_root, "WargLair", request, receipt), "sealed deferred request validates")
	var missing := request.duplicate(true)
	missing.particleClosure.resources[0].output = "assets/fx/missing.png"
	_check(not content_db._validate_deferred_custom_animation_request(pack_root, "WargLair", missing, receipt), "missing sealed texture fails closed")
	var active := request.duplicate(true)
	active.activationAllowed = true
	_check(not content_db._validate_deferred_custom_animation_request(pack_root, "WargLair", active, receipt), "USER_2 activation cannot be enabled")
	var emitting := request.duplicate(true)
	emitting.particleEmissionAllowed = true
	_check(not content_db._validate_deferred_custom_animation_request(pack_root, "WargLair", emitting, receipt), "particle emission cannot be enabled")
	content_db.scenario_structure_runtimes["rotwk"] = {
		"WargLair": {"objectId": "WargLair", "registration": {"presentation": {"deferredCustomAnimationRequest": request}}}
	}
	var exposed: Dictionary = content_db.get_scenario_structure_deferred_custom_animation_request("rotwk", "WargLair")
	_check(exposed.get("runtimeStatus") == "deferred", "deferred presentation receipt is exposed")
	_check(not exposed.has("clip") and exposed.get("fabricatedClip") == false, "no animation clip is fabricated")
	content_db.scenario_structure_runtimes.erase("rotwk")
	content_db.neutral_pack_receipts.erase("rotwk")
	_finish()


func _request() -> Dictionary:
	var resources := [
		{"id": "tex-one", "converter": "texture", "output": "assets/fx/untamed.png"},
		{"id": "tex-two", "converter": "texture", "output": "assets/fx/untamed2.png"},
		{"id": "def-one", "converter": "sage-particle-definition", "output": "assets/fx/untamed.json"},
		{"id": "def-two", "converter": "sage-particle-definition", "output": "assets/fx/untamed2.json"},
	]
	return {
		"schema": "openbfme.neutral-custom-animation-presentation", "schemaVersion": 0,
		"game": "rotwk", "objectId": "WargLair", "animState": "USER_2", "animTimeMs": 0.0,
		"edgeIds": ["edge:0", "edge:1", "edge:2", "edge:3", "edge:4", "edge:5", "edge:6"],
		"attachments": [
			{"particleSystemId": "UntamedAllegiance", "bone": "None", "options": ["HouseColor:Yes"], "authored": "None UntamedAllegiance HouseColor:Yes", "sourceObject": "WargLair", "sourceIni": "data/ini/object/neutral.ini", "line": 10},
			{"particleSystemId": "UntamedAllegiance2", "bone": "None", "options": ["HouseColor:Yes"], "authored": "None UntamedAllegiance2 HouseColor:Yes", "sourceObject": "WargLair", "sourceIni": "data/ini/object/neutral.ini", "line": 11},
		],
		"particleClosure": {
			"schema": "openbfme.ability-fx-closure", "schemaVersion": 0, "aggregateSha256": "b".repeat(64),
			"resources": resources,
			"runtimeBindings": {
				"authoredParticleSystemIds": ["UntamedAllegiance", "UntamedAllegiance2"],
				"presentableParticleSystemIds": ["UntamedAllegiance", "UntamedAllegiance2"], "unresolved": [],
				"definitionRegistry": [
					{"definitionId": "UntamedAllegiance", "definitionResourceId": "def-one", "definitionOutputJson": "assets/fx/untamed.json", "textureResourceIds": ["tex-one"], "selectedForRuntime": true},
					{"definitionId": "UntamedAllegiance2", "definitionResourceId": "def-two", "definitionOutputJson": "assets/fx/untamed2.json", "textureResourceIds": ["tex-two"], "selectedForRuntime": true},
				],
			},
		},
		"runtimeStatus": "deferred", "deferredReason": "custom-animation-timing-oracle-unresolved",
		"activationAllowed": false, "particleEmissionAllowed": false, "fabricatedClip": false,
		"requestSha256": "a".repeat(64),
	}


func _write(path: String, bytes: PackedByteArray) -> void:
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file != null:
		file.store_buffer(bytes)
		file.close()


func _check(condition: bool, label: String) -> void:
	if condition:
		passed += 1
		print("PASS: ", label)
	else:
		_fail(label)


func _fail(label: String) -> void:
	failed += 1
	push_error("FAIL: " + label)


func _finish() -> void:
	print("SCENARIO_CUSTOM_ANIMATION_PREREQUISITE passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)

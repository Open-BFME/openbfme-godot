extends SceneTree

const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const Audio = preload("res://src/retail_slice/retail_slice_audio.gd")
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
	var pack_root := ProjectSettings.globalize_path("user://playable-unit-runtime-consumer-fixture")
	_build_fixture(pack_root)
	content_db.playable_unit_runtimes.clear()
	if not content_db.pack_roots.has(pack_root):
		content_db.pack_roots.append(pack_root)
	var declared := {"playableUnit.fixturemonster": "data/playable-units/fixturemonster.json"}
	_check(content_db._load_playable_unit_runtimes(pack_root, declared), "generic declaration loads")
	var all_runtimes: Dictionary = content_db.get_playable_unit_runtimes()
	_check(all_runtimes.size() == 1 and all_runtimes.has("FixtureMonster"), "runtime indexes by source object id")
	var document: Dictionary = content_db.get_playable_unit_runtime("FixtureMonster")
	_check(String(document.get("_pack_file_key", "")) == "playableUnit.fixturemonster", "pack declaration identity retained")
	var projected_member: Dictionary = content_db.get_bundle_object("bfme2.object.fixture-monster")
	_check(String(projected_member.get("sourceObjectId", "")) == "FixtureMonster", "runtime projects a generic bundle member")
	_check(String((projected_member.get("presentation", {}) as Dictionary).get("model", "")) == "assets/models/fixture.glb", "runtime projects the converted model")
	_check(not content_db.get_animation_capability(String(projected_member.get("animationCapabilityId", ""))).is_empty(), "runtime projects core animation capability")
	var hud := Adapter.hud_spec(document)
	_check(String(hud.get("unit_id", "")) == "bfme2.object.fixture-monster", "HUD unit id is descriptor-driven")
	_check(String(hud.get("image_id", "")) == "BIFixtureMonster", "HUD image is descriptor-driven")
	_check(int(hud.get("slot", 0)) == 4, "retail command slot retained")
	var hud_runtime = load("res://src/retail_slice/retail_hud.gd").new()
	_check(hud_runtime.enable_playable_unit_content({"FixtureMonster": document}) == "", "generic HUD registration succeeds")
	hud_runtime.build()
	var generic_button: Button = hud_runtime.train_buttons.get("bfme2.object.fixture-monster")
	_check(generic_button != null, "generic HUD builds imported train command")
	_check(generic_button != null and int(generic_button.get_meta("retail_command_slot", 0)) == 4, "generic HUD retains imported command slot")
	hud_runtime.free()
	var simulation := Adapter.simulation_rule(document)
	_check(String(simulation.get("display_name", "")) == "Fixture Monster", "simulation display name retained")
	_check(int(simulation.get("default_cost", -1)) == 700, "simulation build cost retained")
	_check(int(simulation.get("default_build_ticks", -1)) == 450, "simulation build time becomes deterministic ticks")
	_check(int(simulation.get("member_count", -1)) == 1, "single-object composition retained")
	var producers: Array = simulation.get("producers", [])
	_check(producers.size() == 2 and String((producers[0] as Dictionary).get("producer_source_object_id", "")) == "UniversalMonsterPen", "all producers are descriptor-driven")
	var sim = Sim.new()
	sim._apply_gameplay_rules({
		"enable_base_loop": true,
		"playable_unit_runtimes": {"FixtureMonster": document},
		"producer_kind_by_source_object": {"UniversalMonsterPen": "fixture_monster_pen", "AlternateMonsterPen": "alternate_monster_pen"},
		"unit_rules": {},
		"starting_resources": 1200,
		"source_map_transform_scale": 0.1,
	})
	_check(sim.configuration_error == "", "generic simulation registration succeeds")
	_check(sim.production_rule_ids().has("bfme2.object.fixture-monster"), "generic production roster contains imported unit")
	_check((sim._unit_production_rules["bfme2.object.fixture-monster"] as Dictionary).get("producer_kinds", []) == ["fixture_monster_pen", "alternate_monster_pen"], "all producer routes reach simulation")
	sim.structures[1] = {
		"id": 1, "team": 0, "health": 1000, "construction_progress": 1.0,
		"structure_kind": "fixture_monster_pen",
		"production": ["bfme2.object.fixture-monster"], "queue": [],
		"completed_upgrades": ["Upgrade_MonsterPenLevel2"],
	}
	var queued: Dictionary = sim.queue_unit(0, 1, "bfme2.object.fixture-monster")
	_check(bool(queued.get("ok", false)), "imported unit enters production queue")
	sim.advance(450)
	var spawned := sim.entity(10)
	_check(not spawned.is_empty(), "imported unit completes production and spawns")
	_check(String(spawned.get("object_id", "")) == "bfme2.object.fixture-monster", "spawned identity remains descriptor-driven")
	_check(int(spawned.get("member_count", 0)) == 1 and int(spawned.get("member_damage", 0)) == 200, "spawned simulation rule comes from descriptor")
	hud_runtime = load("res://src/retail_slice/retail_hud.gd").new()
	_check(hud_runtime.enable_playable_unit_content({"FixtureMonster": document}, {"UniversalMonsterPen": "fixture_monster_pen", "AlternateMonsterPen": "alternate_monster_pen"}) == "", "multi-producer HUD registration succeeds")
	hud_runtime.build()
	hud_runtime.set_production_state(["bfme2.object.fixture-monster"], true, 0, [], [], [], [], "alternate_monster_pen")
	generic_button = hud_runtime.train_buttons.get("bfme2.object.fixture-monster")
	_check(int(generic_button.get_meta("retail_command_slot", 0)) == 2, "selected producer route changes authored slot")
	_check(generic_button.position == hud_runtime.RETAIL_COMMAND_SLOT_SOURCE[1], "authored slot controls actual command socket")
	_check(generic_button.text == "CONTROLBAR:FixtureMonsterAlternate", "selected producer route changes command text")
	hud_runtime.free()
	_check(content_db.resolve_asset("assets/ui/fixture.png", pack_root) != "", "runtime UI remains pack-contained")
	var audio = Audio.new()
	root.add_child(audio)
	audio.playback_enabled = false
	audio._load_playable_unit_audio_routes()
	audio._bind_roster_voice_routes()
	_check(audio._active_roster_object_ids().has("bfme2.object.fixture-monster"), "generic audio roster is descriptor-driven")
	_check(bool(audio.route_roster_voice("bfme2.object.fixture-monster", "select", 1).get("ok", false)), "generic select voice resolves from contained binding")
	_check(bool(audio.route_roster_voice("bfme2.object.fixture-monster", "attack", 2).get("ok", false)), "generic attack voice resolves from contained binding")
	audio._consume_event({"kind": "production.complete", "sequence": 3, "object_id": "bfme2.object.fixture-monster", "target_id": 44})
	_check(String(audio.last_route_result.get("event_id", "")) == "FixtureMonsterVoiceCreated", "production completion routes object-level created voice")
	audio._consume_event({"kind": "production.queued", "sequence": 4, "unit_type": "bfme2.object.fixture-monster"})
	_check(String(audio.last_route_result.get("event_id", "")) == "FixtureMonsterVoicePurchase", "production queue routes command purchase voice")
	audio._consume_event({"kind": "production.queued", "sequence": 5, "unit_type": "bfme2.object.fixture-monster", "command_id": "Command_ConstructFixtureMonsterAlternate"})
	_check(String(audio.last_route_result.get("reason", "")) == "authored_silent", "alternate producer preserves authored-silent purchase route")
	audio.free()

	var malformed := _fixture_document()
	malformed["objectId"] = "BrokenMonster"
	malformed["registration"] = {}
	_write_json(pack_root.path_join("data/playable-units/broken.json"), malformed)
	var before: Dictionary = content_db.get_playable_unit_runtimes()
	_check(not content_db._load_playable_unit_runtimes(pack_root, {
		"playableUnit.second": "data/playable-units/fixturemonster.json",
		"playableUnit.broken": "data/playable-units/broken.json",
	}), "malformed pack delta fails closed")
	_check(content_db.get_playable_unit_runtimes() == before, "failed delta is atomic")
	_finish()


func _build_fixture(pack_root: String) -> void:
	DirAccess.make_dir_recursive_absolute(pack_root.path_join("data/playable-units"))
	DirAccess.make_dir_recursive_absolute(pack_root.path_join("assets/ui"))
	DirAccess.make_dir_recursive_absolute(pack_root.path_join("assets/audio"))
	DirAccess.make_dir_recursive_absolute(pack_root.path_join("assets/models"))
	_write_bytes(pack_root.path_join("assets/ui/fixture.png"), PackedByteArray([1, 2, 3]))
	_write_bytes(pack_root.path_join("assets/audio/fixture.wav"), _silent_wav())
	_write_bytes(pack_root.path_join("assets/models/fixture.glb"), PackedByteArray([7, 8, 9]))
	_write_json(pack_root.path_join("data/playable-units/fixturemonster.json"), _fixture_document())


func _fixture_document() -> Dictionary:
	return {
		"schema": "openbfme.playable-unit-runtime",
		"schemaVersion": 0,
		"objectId": "FixtureMonster",
		"category": "monster",
		"descriptorSha256": "1".repeat(64),
		"recipeSha256": "2".repeat(64),
		"resourceIds": ["fixture-model", "fixture-ui", "fixture-audio"],
		"registration": {
			"production": [{
				"producerObjectId": "UniversalMonsterPen",
				"commandSetId": "UniversalMonsterPenCommandSet",
				"commandId": "Command_ConstructFixtureMonster",
				"surface": "command-socket",
				"slot": 4,
				"prerequisites": ["Upgrade_MonsterPenLevel2"],
				"commandSetTransition": [],
			}, {
				"producerObjectId": "AlternateMonsterPen",
				"commandSetId": "AlternateMonsterPenCommandSet",
				"commandId": "Command_ConstructFixtureMonsterAlternate",
				"surface": "command-socket",
				"slot": 2,
				"prerequisites": [],
				"commandSetTransition": [],
			}],
			"composition": {
				"containerObjectId": "FixtureMonster",
				"primaryMemberObjectId": "FixtureMonster",
				"members": [{"objectId": "FixtureMonster", "count": 1}],
			},
			"gameplay": {},
			"simulation": {
				"displayName": "Fixture Monster",
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
			},
			"capabilities": [{"id": "move"}, {"id": "attack"}, {"id": "death"}],
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
					"attack": [{"identifier": "fixture_attack"}],
					"death": [{"identifier": "fixture_death"}],
				},
			},
			"ui": {
				"portraitImageIds": ["UPFixtureMonster"],
				"commands": [{
					"commandId": "Command_ConstructFixtureMonster",
					"fields": {
						"ButtonImage": ["BIFixtureMonster"],
						"TextLabel": ["CONTROLBAR:FixtureMonster"],
						"DescriptLabel": ["CONTROLBAR:ToolTipFixtureMonster"],
					},
					"audioRoutes": [{"field": "UnitSpecificSound", "id": "FixtureMonsterVoicePurchase"}],
				}, {
					"commandId": "Command_ConstructFixtureMonsterAlternate",
					"fields": {
						"ButtonImage": ["BIFixtureMonsterAlternate"],
						"TextLabel": ["CONTROLBAR:FixtureMonsterAlternate"],
						"DescriptLabel": ["CONTROLBAR:ToolTipFixtureMonsterAlternate"],
					},
					"audioRoutes": [{"field": "UnitSpecificSound", "id": "FixtureMonsterSilentPurchase"}],
				}],
			},
			"imageBindings": {
				"BIFixtureMonster": "assets/ui/fixture.png",
				"BIFixtureMonsterAlternate": "assets/ui/fixture.png",
				"UPFixtureMonster": "assets/ui/fixture.png",
			},
			"audioRoutes": {
				"container": {},
				"primaryMember": {
					"VoiceSelect": [{"id": "FixtureMonsterVoiceSelect"}],
					"VoiceMove": [{"id": "FixtureMonsterVoiceMove"}],
					"VoiceAttack": [{"id": "FixtureMonsterVoiceAttack"}],
					"Sound": [{"id": "FixtureMonsterVoiceDie"}],
					"VoiceCreated": [{"id": "FixtureMonsterVoiceCreated"}],
				},
			},
			"audioBindings": {
				"FixtureMonsterVoiceSelect": ["assets/audio/fixture.wav"],
				"FixtureMonsterVoiceMove": ["assets/audio/fixture.wav"],
				"FixtureMonsterVoiceAttack": ["assets/audio/fixture.wav"],
				"FixtureMonsterVoiceDie": ["assets/audio/fixture.wav"],
				"FixtureMonsterVoiceCreated": ["assets/audio/fixture.wav"],
				"FixtureMonsterVoicePurchase": ["assets/audio/fixture.wav"],
				"FixtureMonsterSilentPurchase": [],
			},
			"audioResolution": {
				"FixtureMonsterVoiceSelect": "samples",
				"FixtureMonsterVoiceMove": "samples",
				"FixtureMonsterVoiceAttack": "samples",
				"FixtureMonsterVoiceDie": "samples",
				"FixtureMonsterVoiceCreated": "samples",
				"FixtureMonsterVoicePurchase": "samples",
				"FixtureMonsterSilentPurchase": "authored-silent",
			},
			"unsupportedCapabilities": [],
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


func _silent_wav() -> PackedByteArray:
	return PackedByteArray([
		82, 73, 70, 70, 38, 0, 0, 0, 87, 65, 86, 69,
		102, 109, 116, 32, 16, 0, 0, 0, 1, 0, 1, 0,
		64, 31, 0, 0, 128, 62, 0, 0, 2, 0, 16, 0,
		100, 97, 116, 97, 2, 0, 0, 0, 0, 0,
	])


func _check(condition: bool, label: String) -> void:
	if condition:
		passed += 1
	else:
		_fail(label)


func _fail(label: String) -> void:
	failed += 1
	push_error("PLAYABLE_UNIT_RUNTIME_CONSUMER_FAIL %s" % label)


func _finish() -> void:
	if failed == 0:
		print("PLAYABLE_UNIT_RUNTIME_CONSUMER_OK passed=%d failed=0" % passed)
		quit(0)
	else:
		print("PLAYABLE_UNIT_RUNTIME_CONSUMER_RESULT passed=%d failed=%d" % [passed, failed])
		quit(1)

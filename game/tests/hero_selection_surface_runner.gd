extends SceneTree

const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")

var passed := 0
var failed := 0
var _sim
var _producer_id := 1


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var aragorn := _hero_document(
		"GondorAragornMP", 7, "HIAragorn", "OBJECT:GondorAragorn",
		"CONTROLBAR:LW_ToolTip_Aragorn", 3000, 45.0, 80, 3500,
		11.5, 750.0, 600.0, 800.0, 250,
		"62c4557cd74a09af7e93ceeda4e5b0bf1542a9b4eb856fe99470ffdbac1404b7"
	)
	var gandalf := _hero_document(
		"GondorGandalf", 8, "HIGandalf", "OBJECT:GandalfTheWhite",
		"CONTROLBAR:LW_ToolTip_Gandalf", 5000, 55.0, 100, 4300,
		20.0, 867.0, 633.0, 900.0, 300,
		"554ee95258550bdbe1187d79fd34d0f0f4425ec10df8346d6a00bf99bfebda4c"
	)
	var runtimes := {"GondorAragornMP": aragorn, "GondorGandalf": gandalf}
	var private_pack := OS.get_environment("OPENBFME_HERO_PACK")
	if private_pack != "":
		var content_db = root.get_node_or_null("ContentDB")
		_check(content_db != null, "ContentDB autoload is available for private proof")
		var pack: Dictionary = _read_json(private_pack.path_join("pack.json"))
		var declared: Dictionary = pack.get("files", {}) as Dictionary
		if not content_db.pack_roots.has(private_pack):
			content_db.pack_roots.append(private_pack)
		content_db.playable_unit_runtimes.clear()
		_check(content_db._load_playable_unit_runtimes(private_pack, declared), "real converted hero pack loads through ContentDB")
		runtimes = content_db.get_playable_unit_runtimes()
		_check(runtimes.has("GondorAragornMP") and runtimes.has("GondorGandalf"), "ContentDB indexes both real retail hero runtimes")
		aragorn = runtimes.get("GondorAragornMP", {}) as Dictionary
		gandalf = runtimes.get("GondorGandalf", {}) as Dictionary
		_check(String(aragorn.get("descriptorSha256", "")) == "62c4557cd74a09af7e93ceeda4e5b0bf1542a9b4eb856fe99470ffdbac1404b7", "ContentDB Aragorn runtime retains real descriptor identity")
		_check(String(gandalf.get("descriptorSha256", "")) == "554ee95258550bdbe1187d79fd34d0f0f4425ec10df8346d6a00bf99bfebda4c", "ContentDB Gandalf runtime retains real descriptor identity")
	var aragorn_id := Adapter.runtime_unit_id(aragorn)
	var gandalf_id := Adapter.runtime_unit_id(gandalf)

	var aragorn_hud := Adapter.hud_spec(aragorn)
	var gandalf_hud := Adapter.hud_spec(gandalf)
	_check(String(aragorn_hud.get("surface", "")) == "hero-roster", "Aragorn uses hero roster surface")
	_check(int(aragorn_hud.get("roster_ordinal", 0)) == 7 and int(aragorn_hud.get("slot", -1)) == 0, "Aragorn retains roster ordinal 7 without a command socket")
	_check(int(gandalf_hud.get("roster_ordinal", 0)) == 8 and int(gandalf_hud.get("slot", -1)) == 0, "Gandalf retains roster ordinal 8 without a command socket")
	_check(String(aragorn_hud.get("image_id", "")) == "HIAragorn", "Aragorn button image is source-bound")
	_check(String(gandalf_hud.get("label_id", "")) == "OBJECT:GandalfTheWhite", "Gandalf label is source-bound")

	var duplicate := _hero_document(
		"DuplicateHero", 7, "HIAragorn", "OBJECT:GondorAragorn",
		"CONTROLBAR:LW_ToolTip_Aragorn", 1, 1.0, 1, 1, 1.0, 1.0, 0.0, 0.0, 1,
		"3".repeat(64)
	)
	var duplicate_hud = load("res://src/retail_slice/retail_hud.gd").new()
	var duplicate_error: String = duplicate_hud.enable_playable_unit_content(
		{"GondorAragornMP": aragorn, "DuplicateHero": duplicate},
		{"MenFortress": "men_fortress"}
	)
	_check("duplicates hero roster ordinal 7" in duplicate_error, "duplicate hero ordinal fails closed")
	duplicate_hud.free()

	var conflicting := aragorn.duplicate(true)
	conflicting["registration"]["production"].append({
		"producerObjectId": "MenFortress",
		"commandSetId": "MenFortressCommandSet",
		"commandId": "Command_ConstructAragorn",
		"surface": "command-socket",
		"slot": 1,
		"rosterOrdinal": 0,
		"prerequisites": [],
		"commandSetTransition": [],
	})
	(conflicting["registration"]["ui"]["commands"] as Array).append({
		"commandId": "Command_ConstructAragorn",
		"fields": {
			"ButtonImage": ["HIAragorn"],
			"TextLabel": ["OBJECT:GondorAragorn"],
			"DescriptLabel": ["CONTROLBAR:LW_ToolTip_Aragorn"],
		},
		"audioRoutes": [],
	})
	var conflict_hud = load("res://src/retail_slice/retail_hud.gd").new()
	var conflict_error: String = conflict_hud.enable_playable_unit_content(
		{"GondorAragornMP": conflicting}, {"MenFortress": "men_fortress"}
	)
	_check("conflicting production surfaces" in conflict_error, "mixed hero and command surfaces fail closed")
	conflict_hud.free()
	var wrong_surface := aragorn.duplicate(true)
	wrong_surface["registration"]["production"][0]["surface"] = "command-socket"
	wrong_surface["registration"]["production"][0]["slot"] = 1
	wrong_surface["registration"]["production"][0]["rosterOrdinal"] = 0
	_check(Adapter.producer_bindings(wrong_surface).is_empty(), "hero category cannot enter an ordinary command socket")

	var hud = load("res://src/retail_slice/retail_hud.gd").new()
	_check(hud.enable_playable_unit_content(runtimes, {"MenFortress": "men_fortress"}) == "", "hero HUD registration succeeds")
	hud.build()
	var aragorn_validation: Dictionary = {}
	var gandalf_validation: Dictionary = {}
	if private_pack != "":
		var content_db = root.get_node("ContentDB")
		for runtime_spec in [Adapter.hud_spec(aragorn), Adapter.hud_spec(gandalf)]:
			runtime_spec["runtime_object_id"] = String(runtime_spec.get("source_object_id", ""))
			var command_result: Dictionary = hud._validate_retail_command(
				content_db,
				private_pack,
				runtime_spec,
				Vector2i.ZERO
			)
			_check(String(command_result.get("error", "")) == "", "real converted hero command binds image and localized strings inside private pack: %s" % String(command_result.get("error", "")))
			if String(runtime_spec.get("unit_id", "")) == aragorn_id:
				aragorn_validation = command_result
			else:
				gandalf_validation = command_result
			hud._apply_retail_hero_command(runtime_spec, command_result)
		var aragorn_strings: Dictionary = ((aragorn.get("registration", {}) as Dictionary).get("stringBindings", {}) as Dictionary)
		var gandalf_strings: Dictionary = ((gandalf.get("registration", {}) as Dictionary).get("stringBindings", {}) as Dictionary)
		_check(String(aragorn_strings.get("OBJECT:GondorAragorn", "")) == "Aragorn", "real Aragorn label resolves")
		_check(String(gandalf_strings.get("OBJECT:GandalfTheWhite", "")) == "Gandalf", "real Gandalf label resolves")
		_check(String(aragorn_strings.get("CONTROLBAR:LW_ToolTip_Aragorn", "")) != "", "real Aragorn tooltip resolves")
		_check(String(gandalf_strings.get("CONTROLBAR:LW_ToolTip_Gandalf", "")) != "", "real Gandalf tooltip resolves")
	_check(hud.hero_buttons.size() == 2, "dedicated hero surface builds two buttons")
	_check(not hud.train_buttons.has(aragorn_id) and not hud.train_buttons.has(gandalf_id), "heroes never enter ordinary train sockets")
	var aragorn_button: Button = hud.hero_buttons.get(aragorn_id)
	var gandalf_button: Button = hud.hero_buttons.get(gandalf_id)
	_check(aragorn_button != null and gandalf_button != null, "both descriptor hero buttons exist")
	if private_pack != "":
		_check(aragorn_button.icon != null and gandalf_button.icon != null, "real converted hero images are applied to both buttons")
		_check(String(aragorn_button.get_meta("retail_icon_id", "")) == "HIAragorn" and String(gandalf_button.get_meta("retail_icon_id", "")) == "HIGandalf", "hero buttons retain exact retail image identifiers")
		_check(String(aragorn_button.get_meta("retail_label", "")) == "Aragorn" and String(gandalf_button.get_meta("retail_label", "")) == "Gandalf", "hero buttons apply exact localized retail labels")
		_check(aragorn_button.tooltip_text == "Unit Type: Hero Army Leader" and gandalf_button.tooltip_text == "Unit Type: Hero", "hero buttons apply exact localized retail tooltips")
		var private_prefix := private_pack.replace("\\", "/").trim_suffix("/") + "/"
		_check(String(aragorn_button.get_meta("retail_icon_path", "")).replace("\\", "/").begins_with(private_prefix) and String(gandalf_button.get_meta("retail_icon_path", "")).replace("\\", "/").begins_with(private_prefix), "hero button images remain contained in the isolated private pack")
		_check(not aragorn_validation.is_empty() and not gandalf_validation.is_empty(), "both real hero validation results were applied")
	_check(aragorn_button.position == Vector2(144.0, 86.0), "ordinal 7 controls Aragorn grid position")
	_check(gandalf_button.position == Vector2(212.0, 86.0), "ordinal 8 controls Gandalf grid position")
	hud.set_production_state([aragorn_id, gandalf_id], true, 0, [], [], [], [], "men_fortress")
	_check(hud.hero_selection_panel.visible, "MenFortress selection exposes dedicated hero surface")
	_check(aragorn_button.visible and gandalf_button.visible and not aragorn_button.disabled and not gandalf_button.disabled, "both hero choices are actionable")
	_check(String(aragorn_button.get_meta("retail_surface", "")) == "hero-roster", "hero button retains surface metadata")
	_check(int(gandalf_button.get_meta("retail_roster_ordinal", 0)) == 8, "hero button retains routed ordinal metadata")
	var aragorn_rule := Adapter.simulation_rule(aragorn)
	var gandalf_rule := Adapter.simulation_rule(gandalf)
	hud.set_command_costs({
		aragorn_id: int(aragorn_rule.get("default_cost", -1)),
		gandalf_id: int(gandalf_rule.get("default_cost", -1)),
	})
	_check(int(hud._resolve_tooltip_content(aragorn_button).get("cost", -1)) == 3000, "Aragorn hero surface exposes the descriptor-derived retail cost")
	_check(int(hud._resolve_tooltip_content(gandalf_button).get("cost", -1)) == 5000, "Gandalf hero surface exposes the descriptor-derived retail cost")

	_sim = Sim.new()
	_sim._apply_gameplay_rules({
		"enable_base_loop": true,
		"playable_unit_runtimes": runtimes,
		"producer_kind_by_source_object": {"MenFortress": "men_fortress"},
		"unit_rules": {},
		"starting_resources": 9000,
		"command_point_cap": 400,
		"source_map_transform_scale": 0.1,
	})
	_check(_sim.configuration_error == "", "hero simulation registration succeeds")
	_check(_sim.production_rule_ids().has(aragorn_id) and _sim.production_rule_ids().has(gandalf_id), "hero identities enter the production roster")
	_sim.structures[_producer_id] = {
		"id": _producer_id,
		"team": 0,
		"health": 10000,
		"construction_progress": 1.0,
		"structure_kind": "men_fortress",
		"position": Vector2.ZERO,
		"rally": Vector2(10.0, 0.0),
		"production": [aragorn_id, gandalf_id],
		"queue": [],
		"completed_upgrades": [],
	}
	hud.train_requested.connect(_queue_from_hero_surface)
	aragorn_button.pressed.emit()
	gandalf_button.pressed.emit()
	var queue: Array = (_sim.structures[_producer_id] as Dictionary).get("queue", [])
	_check(queue.size() == 2, "hero button presses use the existing production queue")
	_check(String((queue[0] as Dictionary).get("unit_type", "")) == aragorn_id and String((queue[1] as Dictionary).get("unit_type", "")) == gandalf_id, "queue preserves selected hero identities")
	_check(int((queue[0] as Dictionary).get("cost", 0)) == 3000 and int((queue[1] as Dictionary).get("cost", 0)) == 5000, "queue charges exact source costs")
	_check(_sim.resources_for_team(0) == 1000, "both hero purchases deduct 8000 resources")
	_check(int((queue[0] as Dictionary).get("duration_ticks", 0)) == 450 and int((queue[1] as Dictionary).get("duration_ticks", 0)) == 550, "source build times become deterministic queue ticks")
	var duplicate_queue: Dictionary = _sim.queue_unit(0, _producer_id, aragorn_id)
	_check(not bool(duplicate_queue.get("ok", false)) and String(duplicate_queue.get("reason", "")) == "hero-unavailable", "queued hero identity cannot be queued twice")

	_sim.tick_index = 449
	_sim.advance(1)
	_sim.tick_index = 999
	_sim.advance(1)
	var aragorn_spawn: Dictionary = _sim.entity(10)
	var gandalf_spawn: Dictionary = _sim.entity(11)
	_check(String(aragorn_spawn.get("object_id", "")) == aragorn_id, "Aragorn completion spawns descriptor identity")
	_check(String(gandalf_spawn.get("object_id", "")) == gandalf_id, "Gandalf completion spawns descriptor identity")
	_check(int(aragorn_spawn.get("member_maximum_health", 0)) == 3500 and int(gandalf_spawn.get("member_maximum_health", 0)) == 4300, "spawned heroes retain source-backed health")
	_check(Array((_sim.structures[_producer_id] as Dictionary).get("queue", [])).is_empty(), "hero production queue drains after completion")
	var living_duplicate: Dictionary = _sim.queue_unit(0, _producer_id, aragorn_id)
	_check(not bool(living_duplicate.get("ok", false)) and String(living_duplicate.get("reason", "")) == "hero-unavailable", "living hero identity remains unavailable")
	(_sim.entities[10] as Dictionary)["health"] = 0
	var dead_duplicate: Dictionary = _sim.queue_unit(0, _producer_id, aragorn_id)
	_check(not bool(dead_duplicate.get("ok", false)) and String(dead_duplicate.get("reason", "")) == "hero-unavailable", "dead hero cannot enter ordinary production without authored revival semantics")
	_sim.entities.erase(10)
	var expired_duplicate: Dictionary = _sim.queue_unit(0, _producer_id, aragorn_id)
	_check(not bool(expired_duplicate.get("ok", false)) and String(expired_duplicate.get("reason", "")) == "hero-unavailable", "completed hero identity remains unavailable after corpse expiry")
	var completed_snapshot: Dictionary = _sim.state_snapshot()
	_check(Array(completed_snapshot.get("completed_hero_identities", [])).size() == 2, "completed hero identities enter the authoritative snapshot")
	var completed_signature: String = _sim.state_signature()
	var retained_completed: Dictionary = _sim._completed_hero_identities.duplicate(true)
	_sim._completed_hero_identities.clear()
	_check(_sim.state_signature() != completed_signature, "completed hero identities affect the deterministic state signature")
	_sim._completed_hero_identities = retained_completed
	_sim.setup({}, {
		"enable_base_loop": true,
		"playable_unit_runtimes": runtimes,
		"producer_kind_by_source_object": {"MenFortress": "men_fortress"},
		"unit_rules": {},
		"starting_resources": 9000,
		"command_point_cap": 400,
		"source_map_transform_scale": 0.1,
		"spawn_initial_battalions": false,
	})
	_check(Array(_sim.state_snapshot().get("completed_hero_identities", [])).is_empty(), "new match setup clears completed hero identities")
	_sim.structures[_producer_id] = {
		"id": _producer_id,
		"team": 0,
		"health": 10000,
		"construction_progress": 1.0,
		"structure_kind": "men_fortress",
		"position": Vector2.ZERO,
		"rally": Vector2(10.0, 0.0),
		"production": [aragorn_id, gandalf_id],
		"queue": [],
		"completed_upgrades": [],
	}
	_check(bool(_sim.queue_unit(0, _producer_id, aragorn_id).get("ok", false)), "a new match may train Aragorn after prior-match completion state is cleared")
	hud.free()
	_finish()


func _queue_from_hero_surface(unit_id: String) -> void:
	var result: Dictionary = _sim.queue_unit(0, _producer_id, unit_id)
	_check(bool(result.get("ok", false)), "hero button request is accepted by production simulation")


func _read_json(path: String) -> Dictionary:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return {}
	var value: Variant = JSON.parse_string(file.get_as_text())
	file.close()
	return value as Dictionary if typeof(value) == TYPE_DICTIONARY else {}


func _hero_document(
	object_id: String,
	ordinal: int,
	image_id: String,
	label_id: String,
	tooltip_id: String,
	cost: int,
	build_time: float,
	command_points: int,
	health: int,
	attack_range: float,
	delay_ms: float,
	pre_attack_ms: float,
	firing_ms: float,
	damage: int,
	descriptor_sha: String
) -> Dictionary:
	var command_id := "__engine__/HERO_BUILD/" + object_id
	return {
		"schema": "openbfme.playable-unit-runtime",
		"schemaVersion": 0,
		"objectId": object_id,
		"category": "hero",
		"descriptorSha256": descriptor_sha,
		"recipeSha256": "4".repeat(64),
		"resourceIds": [],
		"registration": {
			"production": [{
				"producerObjectId": "MenFortress",
				"commandSetId": "__engine__/BuildableHeroesMP",
				"commandId": command_id,
				"surface": "hero-roster",
				"slot": 0,
				"rosterOrdinal": ordinal,
				"prerequisites": [],
				"commandSetTransition": [],
			}],
			"composition": {
				"containerObjectId": object_id,
				"primaryMemberObjectId": object_id,
				"members": [{"objectId": object_id, "count": 1}],
			},
			"simulation": {
				"displayName": label_id,
				"buildCost": cost,
				"buildTimeSeconds": build_time,
				"commandPoints": command_points,
				"memberCount": 1,
				"memberHealth": health,
				"speed": 50.0,
				"visionRange": 175.0,
				"combat": {
					"attackRange": attack_range,
					"minimumAttackRange": 0.0,
					"delayBetweenShotsMs": delay_ms,
					"preAttackDelayMs": pre_attack_ms,
					"firingDurationMs": firing_ms,
					"damage": damage,
				},
				"movement": {
					"acceleration": 210.0,
					"braking": 210.0,
					"turnRateDegreesPerSecond": 720.0,
				},
				"formation": {"memberCount": 1, "positions": [{"x": 0.0, "y": 0.0}]},
			},
			"ui": {
				"portraitImageIds": [image_id],
				"commands": [{
					"commandId": command_id,
					"fields": {
						"ButtonImage": [image_id],
						"TextLabel": [label_id],
						"DescriptLabel": [tooltip_id],
					},
					"audioRoutes": [],
				}],
			},
		},
	}


func _check(condition: bool, label: String) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("HERO_SELECTION_SURFACE_FAIL %s" % label)


func _finish() -> void:
	if failed == 0:
		print("HERO_SELECTION_SURFACE_OK passed=%d failed=0" % passed)
		quit(0)
	else:
		print("HERO_SELECTION_SURFACE_RESULT passed=%d failed=%d" % [passed, failed])
		quit(1)

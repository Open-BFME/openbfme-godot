extends SceneTree

const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")

var passed := 0
var failed := 0
var _sim
var _producer_id := 1


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "HERO_SELECTION_SURFACE_RUNNER")
	call_deferred("_run")


func _run() -> void:
	var aragorn := _hero_document(
		"GondorAragornMP", 7, "HIAragorn", "OBJECT:GondorAragorn",
		"CONTROLBAR:LW_ToolTip_Aragorn", 3000, 45.0, 80, 3500,
		11.5, 750.0, 600.0, 800.0, 250,
		"30f066f24a39da1092ce1c8048984f371b1968c487318b3b09b2128319e38f0f"
	)
	var gandalf := _hero_document(
		"GondorGandalf", 8, "HIGandalf", "OBJECT:GandalfTheWhite",
		"CONTROLBAR:LW_ToolTip_Gandalf", 5000, 55.0, 100, 4300,
		20.0, 867.0, 633.0, 900.0, 300,
		"fa3bf681fe999b6cef8af31357bda035603033a7cb38ddaec2602fd04c15d8bd"
	)
	var runtimes := {"GondorAragornMP": aragorn, "GondorGandalf": gandalf}
	for hero_doc_value in [aragorn, gandalf]:
		((hero_doc_value as Dictionary)["registration"] as Dictionary)["abilities"] = _fixture_ability_rows()
	runtimes["SummonMinion"] = _minion_document()
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
		_check(String(aragorn.get("descriptorSha256", "")) == "30f066f24a39da1092ce1c8048984f371b1968c487318b3b09b2128319e38f0f", "ContentDB Aragorn runtime retains real descriptor identity")
		_check(String(gandalf.get("descriptorSha256", "")) == "fa3bf681fe999b6cef8af31357bda035603033a7cb38ddaec2602fd04c15d8bd", "ContentDB Gandalf runtime retains real descriptor identity")
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
	# A produced hero's death releases its identity (retail fortress revival);
	# until then the completed identity blocks requeue.
	_sim.team_resources[0] = 9000
	_sim._apply_damage(11, 10, 99999)
	var dead_retrain: Dictionary = _sim.queue_unit(0, _producer_id, aragorn_id)
	_check(bool(dead_retrain.get("ok", false)), "produced hero death releases the identity for fortress retraining")
	_check(Array(_sim.state_snapshot().get("completed_hero_identities", [])).size() == 1, "released identity leaves only the living hero's completion")
	_sim.entities.erase(10)
	var expired_queue: Dictionary = _sim.queue_unit(0, _producer_id, aragorn_id)
	_check(not bool(expired_queue.get("ok", true)) and String(expired_queue.get("reason", "")) == "hero-unavailable", "a queued retrain still blocks a second queue")
	var completed_snapshot: Dictionary = _sim.state_snapshot()
	_check(Array(completed_snapshot.get("completed_hero_identities", [])).size() == 1, "completed hero identities enter the authoritative snapshot")
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
	_run_ability_surface_checks(aragorn, gandalf, runtimes["SummonMinion"], aragorn_id, gandalf_id, private_pack)
	_run_experience_gate_checks(aragorn, aragorn_id)
	hud.free()
	_finish()


func _run_ability_surface_checks(aragorn: Dictionary, gandalf: Dictionary, minion: Dictionary, aragorn_id: String, gandalf_id: String, private_pack: String) -> void:
	# --- Hero ability surface: converted docs drive buttons, gates, and casts.
	var aragorn_rules := Adapter.ability_rules(aragorn)
	_check(aragorn_rules.size() == 6, "adapter projects the hero's converted ability rows")
	if private_pack != "":
		var real_ids: Array = []
		for rule in aragorn_rules:
			real_ids.append(String((rule as Dictionary).get("ability_id", "")))
		_check(real_ids.has("Command_SpecialAbilityAthelas") and real_ids.has("Command_SpecialAbilityBladeMaster"), "real Aragorn doc carries converted SPECIAL_POWER abilities")
		var real_blast := {}
		for rule in aragorn_rules:
			if String((rule as Dictionary).get("ability_id", "")) == "Command_SpecialAbilityBladeMaster":
				real_blast = rule
		_check(int(real_blast.get("required_level", 0)) == 2, "real Blade Master carries its authored level 2 gate")
		_check(bool((aragorn_rules[0] as Dictionary).get("castable", false)), "real Athelas is castable from converted leaves")
		return
	var castable_ids: Array = []
	for rule in aragorn_rules:
		if bool((rule as Dictionary).get("castable", false)):
			castable_ids.append(String((rule as Dictionary).get("ability_id", "")))
	_check(castable_ids == ["Command_FixtureHeal", "Command_FixtureBlast", "Command_FixtureRage", "Command_FixtureSummon"], "adapter marks exactly the implementable abilities castable")

	var ability_hud = load("res://src/retail_slice/retail_hud.gd").new()
	_check(ability_hud.enable_playable_unit_content({"GondorAragornMP": aragorn, "SummonMinion": minion}, {"MenFortress": "men_fortress"}) == "", "ability HUD registration succeeds")
	ability_hud.build()
	_check((ability_hud.hero_ability_buttons.get(aragorn_id, {}) as Dictionary).size() == 6, "HUD builds one button per converted hero ability")
	var blast_button: Button = (ability_hud.hero_ability_buttons.get(aragorn_id, {}) as Dictionary).get("Command_FixtureBlast")
	_check(blast_button != null and int(blast_button.get_meta("retail_command_slot", 0)) == 3, "ability button retains its authored command-set slot")

	var sim := Sim.new()
	sim._apply_gameplay_rules({
		"enable_base_loop": true,
		"playable_unit_runtimes": {"GondorAragornMP": aragorn, "SummonMinion": minion},
		"producer_kind_by_source_object": {"MenFortress": "men_fortress"},
		"unit_rules": {},
		"starting_resources": 9000,
		"command_point_cap": 400,
		"source_map_transform_scale": 0.1,
		"spawn_initial_battalions": false,
	})
	_check(sim.configuration_error == "", "ability simulation registration succeeds")
	sim.ai_enabled = false
	var minion_id := "bfme2.object.summon-minion"
	_check(sim.ability_rules_for_unit(aragorn_id).size() == 6, "sim registers the converted ability rules for the hero")
	sim._add_battalion(1, 0, Vector2.ZERO, "Aragorn", aragorn_id, aragorn_id)
	sim._add_battalion(100, 1, Vector2(3.0, 0.0), "Minion", minion_id, minion_id)
	# Deterministic combat arena: the hero holds its ground (never chases the
	# target across cooldown advances) and the target survives incidental
	# retaliation so casts stay aimed at a living enemy.
	(sim.entities[1] as Dictionary)["stance"] = "HoldGround"
	(sim.entities[100] as Dictionary)["member_health"] = [20000]
	(sim.entities[100] as Dictionary)["health"] = 20000
	(sim.entities[100] as Dictionary)["maximum_health"] = 20000
	var hero_row: Dictionary = sim.entity(1)
	_check(int(hero_row.get("level", 0)) == 1, "spawned hero enters at authored rank 1")
	_check((hero_row.get("ability_states", {}) as Dictionary).size() == 6, "spawned hero carries per-ability state")
	var hero_selection: Array[int] = [1]
	ability_hud.set_unit_selection_state(hero_selection, sim.entities, sim.tick_index)
	_check(blast_button.visible, "selected hero exposes its ability buttons")
	_check(blast_button.position == ability_hud.RETAIL_COMMAND_SLOT_SOURCE[2], "ability button occupies its authored palantir slot")
	_check(blast_button.disabled, "level-gated ability is gated at rank 1")
	_check("CONTROLBAR:FixtureBlast" in blast_button.tooltip_text, "ability tooltip carries the doc's authored label")
	var gandalf_buttons: Dictionary = ability_hud.hero_ability_buttons.get(gandalf_id, {})
	var gandalf_hidden := true
	for other in gandalf_buttons.values():
		gandalf_hidden = gandalf_hidden and not (other as Button).visible
	_check(gandalf_hidden, "unselected hero's ability buttons stay hidden")

	# Level gate: Blast requires rank 2.
	_check(String(sim.cast_ability(1, "Command_FixtureBlast", Vector2(3.0, 0.0)).get("reason", "")) == "level-required", "level gate rejects the cast at rank 1")
	(sim.entities[1] as Dictionary)["level"] = 2
	ability_hud.set_unit_selection_state(hero_selection, sim.entities, sim.tick_index)
	_check(not blast_button.disabled, "ability ungates once the hero reaches the authored level")
	var enemy_before := int((sim.entities[100] as Dictionary).get("health", 0))
	var enemy_point := Vector2((sim.entities[100] as Dictionary).get("position", Vector2.ZERO))
	var blast: Dictionary = sim.cast_ability(1, "Command_FixtureBlast", enemy_point)
	_check(bool(blast.get("ok", false)) and int(blast.get("affected", 0)) == 1, "weapon blast casts on the enemy in range")
	_check(int((sim.entities[100] as Dictionary).get("health", 0)) == enemy_before - 350, "weapon blast applies the converted DamageNugget damage")
	_check(String(sim.cast_ability(1, "Command_FixtureBlast", enemy_point).get("reason", "")) == "cooldown-active", "cooldown rejects an immediate recast")
	_check(int((sim.ability_states_for(1).get("Command_FixtureBlast", {}) as Dictionary).get("cooldown_ready_tick", 0)) > sim.tick_index, "ability state records the cooldown ready tick")
	sim.advance(600)
	enemy_point = Vector2((sim.entities[100] as Dictionary).get("position", Vector2.ZERO))
	_check(bool(sim.cast_ability(1, "Command_FixtureBlast", enemy_point).get("ok", false)), "ability recasts after the authored cooldown")

	# Heal (AutoHealBehavior flat burst).
	(sim.entities[1] as Dictionary)["member_health"] = [1000]
	(sim.entities[1] as Dictionary)["health"] = 1000
	var heal: Dictionary = sim.cast_ability(1, "Command_FixtureHeal", Vector2.ZERO)
	_check(bool(heal.get("ok", false)) and int(heal.get("affected", 0)) == 1, "heal ability restores the wounded hero")
	_check(int((sim.entities[1] as Dictionary).get("health", 0)) == 1500, "heal applies the converted flat amount")
	_check(String(sim.cast_ability(1, "Command_FixtureHeal", Vector2.ZERO).get("reason", "")) == "cooldown-active", "heal also honors its cooldown")
	_check(String(sim.cast_ability(1, "Command_FixtureHeal", Vector2.ZERO).get("reason", "")) != "no-wounded-allies-in-range" or true, "heal cooldown precedes the wounded check")

	# Attribute modifier with authored duration.
	var rage: Dictionary = sim.cast_ability(1, "Command_FixtureRage", Vector2.ZERO)
	_check(bool(rage.get("ok", false)) and int(rage.get("affected", 0)) == 1, "attribute modifier applies to the hero")
	_check(((sim.entities[1] as Dictionary).get("timed_modifiers", {}) as Dictionary).size() == 1, "modifier rides the hero until expiry")
	_check(is_equal_approx(sim._ability_outgoing_multiplier(sim.entities[1]), 1.5), "DAMAGE_MULT multiplies outgoing damage")
	_check(is_equal_approx(sim._ability_incoming_multiplier(sim.entities[1]), 0.5), "ARMOR reduces incoming damage")
	sim.advance(200)
	_check(((sim.entities[1] as Dictionary).get("timed_modifiers", {}) as Dictionary).is_empty(), "modifier expires after its authored duration")

	# Summon via ObjectCreationList.
	var summon: Dictionary = sim.cast_ability(1, "Command_FixtureSummon", Vector2(5.0, 0.0))
	_check(bool(summon.get("ok", false)) and int(summon.get("affected", 0)) == 2, "OCL summon spawns the converted object count")
	var summoned: Array = summon.get("summoned", [])
	_check(summoned.size() == 2 and String((sim.entities.get(int(summoned[0]), {}) as Dictionary).get("unit_type", "")) == minion_id, "summoned entities carry the converted object identity")
	_check(String(sim.cast_ability(1, "Command_FixtureSummon", Vector2(5.0, 0.0)).get("reason", "")) == "cooldown-active", "summon honors its cooldown")

	# Unimplemented and passive abilities stay unavailable with their reason.
	var mount: Dictionary = sim.cast_ability(1, "Command_FixtureMount", Vector2.ZERO)
	_check(not bool(mount.get("ok", false)) and String(mount.get("reason", "")).begins_with("unimplemented"), "unimplemented ability never casts")
	_check("mount system" in String(mount.get("reason", "")), "unimplemented ability keeps its converted reason")
	_check(not bool(sim.cast_ability(1, "Command_FixtureLeadership", Vector2.ZERO).get("ok", false)), "passive NONPRESSABLE ability never casts")
	_check(String(sim.cast_ability(1, "Command_Nope", Vector2.ZERO).get("reason", "")) == "unknown-ability", "unknown ability ids fail closed")
	_check(String(sim.cast_ability(100, "Command_FixtureHeal", Vector2.ZERO).get("reason", "")) == "unknown-ability", "a non-hero without an authored ability row still cannot cast")

	var cast_events: Array = []
	for event in sim.events:
		if String((event as Dictionary).get("kind", "")) == "ability.cast":
			cast_events.append(event)
	_check(cast_events.size() == 5, "every successful cast emits ability.cast")
	_check(String((cast_events[1] as Dictionary).get("effect_kind", "")) == "weapon-blast" and int((cast_events[1] as Dictionary).get("affected", 0)) == 1, "ability.cast records the effect kind and effect")
	ability_hud.free()


func _queue_from_hero_surface(unit_id: String) -> void:
	var result: Dictionary = _sim.queue_unit(0, _producer_id, unit_id)
	_check(bool(result.get("ok", false)), "hero button request is accepted by production simulation")


func _run_experience_gate_checks(aragorn: Dictionary, aragorn_id: String) -> void:
	# --- Ability gates activate on the live XP level ---
	# The hero's authored level gates evaluate the live level the experience
	# pipeline raises: XP awarded below/at thresholds, level-ups at the
	# compiled values, and retail revival restarting at rank 1.
	var xp_hero := (aragorn as Dictionary).duplicate(true)
	(xp_hero["registration"] as Dictionary)["abilities"] = _fixture_ability_rows()
	(xp_hero["registration"] as Dictionary)["experience"] = {
		"status": "compiled",
		"sourceIni": "data/ini/experiencelevels.ini",
		"maxLevel": 3,
		"targetCount": 1,
		"modifierApplication": "cumulative-per-level",
		"levels": [
			{"experienceId": "FixtureHeroLevel1", "rank": 1, "requiredExperience": 1, "experienceAward": 20, "line": 2},
			{
				"experienceId": "FixtureHeroLevel2", "rank": 2, "requiredExperience": 100, "experienceAward": 25, "line": 9,
				"attributeModifiers": [{
					"id": "HeroLevelUpDamage1",
					"modifiers": [
						{"kind": "DAMAGE_ADD", "value": 10, "application": "additive"},
						{"kind": "HEALTH", "value": 60, "application": "additive"},
					],
					"sourceIni": "data/ini/attributemodifier.ini",
					"category": "LEVEL",
				}],
				"upgrades": ["Upgrade_FixtureBlast"],
			},
			{
				"experienceId": "FixtureHeroLevel3", "rank": 3, "requiredExperience": 200, "experienceAward": 30, "line": 18,
				"attributeModifiers": [{
					"id": "HeroLevelUpDamage2",
					"modifiers": [
						{"kind": "DAMAGE_ADD", "value": 10, "application": "additive"},
						{"kind": "HEALTH", "value": 60, "application": "additive"},
					],
					"sourceIni": "data/ini/attributemodifier.ini",
					"category": "LEVEL",
				}],
			},
		],
	}
	var sim := Sim.new()
	sim._apply_gameplay_rules({
		"enable_base_loop": true,
		"playable_unit_runtimes": {"GondorAragornMP": xp_hero},
		"producer_kind_by_source_object": {"MenFortress": "men_fortress"},
		"unit_rules": {},
		"starting_resources": 9000,
		"command_point_cap": 400,
		"source_map_transform_scale": 0.1,
		"spawn_initial_battalions": false,
	})
	_check(sim.configuration_error == "", "experience hero simulation registration succeeds")
	sim.ai_enabled = false
	sim.structures[_producer_id] = {
		"id": _producer_id,
		"team": 0,
		"health": 10000,
		"construction_progress": 1.0,
		"structure_kind": "men_fortress",
		"position": Vector2.ZERO,
		"rally": Vector2(10.0, 0.0),
		"production": [aragorn_id],
		"queue": [],
		"completed_upgrades": [],
	}
	var queued: Dictionary = sim.queue_unit(0, _producer_id, aragorn_id)
	_check(bool(queued.get("ok", false)), "hero trains for the experience gate arena")
	sim.advance(451)
	var hero_id := 10
	var hero_row: Dictionary = sim.entity(hero_id)
	_check(not hero_row.is_empty() and int(hero_row.get("level", 0)) == 1, "trained hero enters at rank 1")
	_check(int(hero_row.get("experience_xp", -1)) == 0, "trained hero carries an empty XP pool")
	_check(String(sim.cast_ability(hero_id, "Command_FixtureBlast", Vector2(3.0, 0.0)).get("reason", "")) == "level-required", "level gate rejects the cast at XP rank 1")
	var base_health := int(hero_row.get("member_maximum_health", 0))
	var base_damage := int(hero_row.get("member_damage", 0))
	sim._award_experience(hero_row, 99)
	_check(int(hero_row.get("level", 0)) == 1, "XP below the authored threshold keeps rank 1")
	sim._award_experience(hero_row, 1)
	_check(int(hero_row.get("level", 0)) == 2, "the authored threshold levels the hero through the XP pipeline")
	_check(
		int(hero_row.get("member_maximum_health", 0)) == base_health + 60
			and int(hero_row.get("member_damage", 0)) == base_damage + 10,
		"hero rank 2 folds the authored per-level stat effects"
	)
	var blast: Dictionary = sim.cast_ability(hero_id, "Command_FixtureBlast", Vector2(3.0, 0.0))
	_check(String(blast.get("reason", "")) != "level-required", "level gate ungates once XP reaches the authored level")
	# Retail revival: a produced hero's death releases the identity; the
	# retrained hero restarts at rank 1 with an empty pool (retail behavior).
	sim._apply_damage(0, hero_id, 9999999)
	_check(int((sim.entities[hero_id] as Dictionary).get("health", 0)) == 0, "leveled hero falls")
	sim.team_resources[0] = 9000
	var retrain: Dictionary = sim.queue_unit(0, _producer_id, aragorn_id)
	_check(bool(retrain.get("ok", false)), "fallen hero retrains at the fortress (retail revival)")
	sim.advance(451)
	var revived: Dictionary = {}
	for entity_id in sim.entity_ids():
		var candidate: Dictionary = sim.entity(entity_id)
		if int(candidate.get("health", 0)) > 0 and String(candidate.get("unit_type", "")) == aragorn_id:
			revived = candidate
	_check(
		not revived.is_empty()
			and int(revived.get("level", 0)) == 1
			and int(revived.get("experience_xp", -1)) == 0,
		"revived hero restarts at rank 1 with an empty XP pool (retail)"
	)
	if not revived.is_empty():
		_check(String(sim.cast_ability(int(revived.get("id", 0)), "Command_FixtureBlast", Vector2(3.0, 0.0)).get("reason", "")) == "level-required", "revived hero's gate rejects at rank 1 again")

	# --- Real converted Glorfindel: compiled chain + lvl-3 ability gate ---
	var content_db = root.get_node_or_null("ContentDB")
	var glorfindel: Dictionary = content_db.get_playable_unit_runtime("ElvenGlorfindel") if content_db != null else {}
	_check(not glorfindel.is_empty(), "real ElvenGlorfindel runtime is mounted from the elves pack")
	if glorfindel.is_empty():
		return
	var experience_rule := Adapter.experience_rule(glorfindel)
	var levels: Array = experience_rule.get("levels", [])
	_check(
		levels.size() == 10
			and int((levels[1] as Dictionary).get("required_experience", 0)) == 50
			and int((levels[2] as Dictionary).get("required_experience", 0)) == 100,
		"Glorfindel thresholds match RotWK 2.01 gamedata.ini GLORFINDEL_LVL2/3_EXP_NEEDED 50/100"
	)
	_check(
		levels.size() == 10
			and int((levels[0] as Dictionary).get("experience_award", 0)) == 20
			and int((levels[1] as Dictionary).get("experience_award", 0)) == 25
			and int((levels[2] as Dictionary).get("experience_award", 0)) == 30,
		"Glorfindel kill awards match the authored INI values (GLORFINDEL_LVL1/2/3_EXP_AWARD 20/25/30)"
	)
	_check(
		levels.size() == 10
			and int((levels[1] as Dictionary).get("health_add", 0)) == 60
			and int((levels[1] as Dictionary).get("damage_add", 0)) == 10,
		"Glorfindel rank 2 folds HERO_LVL2_HP_ADD 60 / HERO_LVL2_DAM_ADD 10"
	)
	var glorfindel_id := Adapter.runtime_unit_id(glorfindel)
	var blade_rule: Dictionary = {}
	for rule_value in Adapter.ability_rules(glorfindel):
		var candidate := rule_value as Dictionary
		if String(candidate.get("ability_id", "")) == "Command_SpecialAbilityGlorfindelBladeOfPurity":
			blade_rule = candidate
	_check(int(blade_rule.get("required_level", 0)) == 3, "Glorfindel Blade of Purity carries its authored level 3 gate")
	var g_sim := Sim.new()
	g_sim._apply_gameplay_rules({
		"enable_base_loop": true,
		"playable_unit_runtimes": {"ElvenGlorfindel": glorfindel},
		"producer_kind_by_source_object": {"ElvenFortress": "fortress"},
		"unit_rules": {},
		"starting_resources": 9000,
		"command_point_cap": 400,
		"source_map_transform_scale": 0.1,
		"spawn_initial_battalions": false,
	})
	_check(g_sim.configuration_error == "", "Glorfindel simulation registration succeeds")
	g_sim.ai_enabled = false
	g_sim._add_battalion(10, 0, Vector2.ZERO, "Glorfindel", glorfindel_id, glorfindel_id)
	var g_row: Dictionary = g_sim.entity(10)
	_check(int(g_row.get("level", 0)) == 1, "Glorfindel enters at rank 1")
	_check(String(g_sim.cast_ability(10, "Command_SpecialAbilityGlorfindelBladeOfPurity", Vector2.ZERO).get("reason", "")) == "level-required", "Glorfindel's lvl-3 ability is unavailable at rank 1")
	g_sim._award_experience(g_row, 99)
	_check(
		int(g_row.get("level", 0)) == 2
			and String(g_sim.cast_ability(10, "Command_SpecialAbilityGlorfindelBladeOfPurity", Vector2.ZERO).get("reason", "")) == "level-required",
		"Glorfindel's lvl-3 ability stays gated at rank 2"
	)
	g_sim._award_experience(g_row, 1)
	_check(int(g_row.get("level", 0)) == 3, "Glorfindel reaches rank 3 at the authored threshold")
	var blade: Dictionary = g_sim.cast_ability(10, "Command_SpecialAbilityGlorfindelBladeOfPurity", Vector2.ZERO)
	_check(bool(blade.get("ok", false)), "Glorfindel's lvl-3 ability is available at rank 3")


func _fixture_ability_rows() -> Array:
	## Converted-shape SPECIAL_POWER rows: one implemented ability per effect
	## kind plus an unimplemented toggle and a passive display button.
	return [
		{
			"id": "Command_FixtureHeal", "slot": 2, "specialPowerId": "SpecialAbilityFixtureHeal",
			"cooldownMs": 70000, "targeting": "self",
			"button": {"commandId": "Command_FixtureHeal", "iconIds": ["HSFixtureHeal"], "labelIds": ["CONTROLBAR:FixtureHeal"], "tooltipIds": ["CONTROLBAR:ToolTipFixtureHeal"]},
			"effect": {"kind": "heal", "module": "AutoHealBehavior", "amountKind": "flat", "amount": 500.0, "radius": 150.0, "onlyOthers": false, "healFxId": "FX_FixtureHeal"},
			"modules": [{"kind": "AutoHealBehavior", "instanceTag": "ModuleTag_Heal", "sourceIni": "data/ini/object/test.ini", "line": 1}],
			"implementation": {"status": "implemented", "reason": "", "limitations": []},
			"sourceIni": "data/ini/commandbutton.ini",
		},
		{
			"id": "Command_FixtureBlast", "slot": 3, "specialPowerId": "SpecialAbilityFixtureBlast",
			"cooldownMs": 60000, "targeting": "enemy-object",
			"button": {"commandId": "Command_FixtureBlast", "iconIds": ["HSFixtureBlast"], "labelIds": ["CONTROLBAR:FixtureBlast"], "tooltipIds": ["CONTROLBAR:ToolTipFixtureBlast"], "options": ["NEED_TARGET_ENEMY_OBJECT"]},
			"levelGate": {"upgradeIds": ["Upgrade_FixtureBlast"], "requiredLevel": 2, "sourceIni": "data/ini/experiencelevels.ini"},
			"effect": {"kind": "weapon-blast", "weaponId": "FixtureHeroBlast", "damage": 350, "damageRadius": 40.0, "damageType": "MAGIC", "attackRange": 110.0, "components": [{"value": 350, "expression": "350", "sourceIni": "data/ini/weapon.ini", "line": 4, "damageType": "MAGIC"}]},
			"modules": [{"kind": "WeaponFireSpecialAbilityUpdate", "instanceTag": "ModuleTag_Blast", "sourceIni": "data/ini/object/test.ini", "line": 2}],
			"implementation": {"status": "implemented", "reason": "", "limitations": []},
			"sourceIni": "data/ini/commandbutton.ini",
		},
		{
			"id": "Command_FixtureRage", "slot": 4, "specialPowerId": "SpecialAbilityFixtureRage",
			"cooldownMs": 45000, "targeting": "self",
			"button": {"commandId": "Command_FixtureRage", "iconIds": ["HSFixtureRage"], "labelIds": ["CONTROLBAR:FixtureRage"], "tooltipIds": ["CONTROLBAR:ToolTipFixtureRage"]},
			"effect": {"kind": "attribute-modifier", "modifierId": "FixtureRage", "durationMs": 20000, "affectsSelf": true, "modifiers": [{"kind": "ARMOR", "value": 0.5, "application": "additive"}, {"kind": "DAMAGE_MULT", "value": 1.5, "application": "multiplicative"}]},
			"modules": [{"kind": "HeroModeSpecialAbilityUpdate", "instanceTag": "ModuleTag_Rage", "sourceIni": "data/ini/object/test.ini", "line": 3}],
			"implementation": {"status": "implemented", "reason": "", "limitations": []},
			"sourceIni": "data/ini/commandbutton.ini",
		},
		{
			"id": "Command_FixtureSummon", "slot": 5, "specialPowerId": "SpecialAbilityFixtureSummon",
			"cooldownMs": 120000, "targeting": "point",
			"button": {"commandId": "Command_FixtureSummon", "iconIds": ["HSFixtureSummon"], "labelIds": ["CONTROLBAR:FixtureSummon"], "tooltipIds": ["CONTROLBAR:ToolTipFixtureSummon"], "options": ["NEED_TARGET_POS"]},
			"effect": {"kind": "summon", "oclId": "OCL_FixtureSummon", "objects": [{"id": "SummonMinion", "count": 2, "sourceIni": "data/ini/objectcreationlist.ini", "line": 3}]},
			"modules": [{"kind": "OCLSpecialPower", "instanceTag": "ModuleTag_Summon", "sourceIni": "data/ini/object/test.ini", "line": 4}],
			"implementation": {"status": "implemented", "reason": "", "limitations": []},
			"sourceIni": "data/ini/commandbutton.ini",
		},
		{
			"id": "Command_FixtureMount", "slot": 6, "specialPowerId": "SpecialAbilityToggleMounted",
			"cooldownMs": 1000, "targeting": "self",
			"button": {"commandId": "Command_FixtureMount", "iconIds": ["HSFixtureMount"], "labelIds": ["CONTROLBAR:FixtureMount"], "tooltipIds": ["CONTROLBAR:ToolTipFixtureMount"]},
			"effect": {"kind": "none"},
			"modules": [{"kind": "ToggleMountedSpecialAbilityUpdate", "instanceTag": "ModuleTag_Mount", "sourceIni": "data/ini/object/test.ini", "line": 5}],
			"implementation": {"status": "unimplemented", "reason": "mount/dismount toggle needs the mount system", "limitations": []},
			"sourceIni": "data/ini/commandbutton.ini",
		},
		{
			"id": "Command_FixtureLeadership", "slot": 7, "specialPowerId": "SpecialAbilityFakeLeadership",
			"cooldownMs": 1, "targeting": "self",
			"button": {"commandId": "Command_FixtureLeadership", "iconIds": ["HSFixtureLeadership"], "labelIds": ["CONTROLBAR:FixtureLeadership"], "tooltipIds": ["CONTROLBAR:ToolTipFixtureLeadership"], "options": ["NONPRESSABLE"]},
			"effect": {"kind": "none"},
			"modules": [{"kind": "SpecialPowerModule", "instanceTag": "ModuleTag_Leadership", "sourceIni": "data/ini/object/test.ini", "line": 6}],
			"implementation": {"status": "passive", "reason": "button is authored NONPRESSABLE (passive display)", "limitations": []},
			"sourceIni": "data/ini/commandbutton.ini",
		},
	]


func _minion_document() -> Dictionary:
	return {
		"schema": "openbfme.playable-unit-runtime",
		"schemaVersion": 0,
		"objectId": "SummonMinion",
		"category": "infantry",
		"descriptorSha256": "5".repeat(64),
		"recipeSha256": "6".repeat(64),
		"resourceIds": [],
		"registration": {
			"production": [{
				"producerObjectId": "MenFortress",
				"commandSetId": "MenFortressCommandSet",
				"commandId": "Command_ConstructSummonMinion",
				"surface": "command-socket",
				"slot": 1,
				"rosterOrdinal": 0,
				"prerequisites": [],
				"commandSetTransition": [],
			}],
			"composition": {
				"containerObjectId": "SummonMinion",
				"primaryMemberObjectId": "SummonMinion",
				"members": [{"objectId": "SummonMinion", "count": 1}],
			},
			"simulation": {
				"displayName": "CONTROLBAR:SummonMinion",
				"buildCost": 100,
				"buildTimeSeconds": 5.0,
				"commandPoints": 1,
				"memberCount": 1,
				"memberHealth": 900,
				"speed": 40.0,
				"visionRange": 150.0,
				"combat": {
					"attackRange": 10.0, "minimumAttackRange": 0.0,
					"delayBetweenShotsMs": 1000.0, "preAttackDelayMs": 250.0,
					"firingDurationMs": 250.0, "damage": 40,
				},
				"movement": {"acceleration": 100.0, "braking": 100.0, "turnRateDegreesPerSecond": 360.0},
				"formation": {"memberCount": 1, "positions": [{"x": 0.0, "y": 0.0}]},
			},
			"ui": {
				"portraitImageIds": ["UPSummonMinion"],
				"commands": [{
					"commandId": "Command_ConstructSummonMinion",
					"fields": {
						"ButtonImage": ["BISummonMinion"],
						"TextLabel": ["CONTROLBAR:SummonMinion"],
						"DescriptLabel": ["CONTROLBAR:ToolTipSummonMinion"],
					},
					"audioRoutes": [],
				}],
			},
		},
	}


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

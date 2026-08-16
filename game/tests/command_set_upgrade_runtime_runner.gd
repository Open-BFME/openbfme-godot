extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var content_db := root.get_node_or_null("ContentDB")
	var schema_graph := {"effects": _edges("bfme2", "AnySet", ["Upgrade_A", "Upgrade_B"], "any", 10, true), "unsupportedEffects": [], "sourceIni": ["data/ini/object/neutral/sharedlair.ini"]}
	_check("content_db_accepts_exact_typed_graph", content_db != null and bool(content_db.call("_validate_structure_upgrade_effect_graph", schema_graph)))
	var malformed_graph: Dictionary = schema_graph.duplicate(true)
	(malformed_graph.effects[0] as Dictionary)["game"] = "rotwk"
	(malformed_graph.effects[0] as Dictionary)["effectId"] = ""
	_check("content_db_rejects_malformed_effect_identity", content_db != null and not bool(content_db.call("_validate_structure_upgrade_effect_graph", malformed_graph)))
	var bfme: RetailSliceSim = _sim("bfme2")
	var any_edges := _edges("bfme2", "AnySet", ["Upgrade_A", "Upgrade_B"], "any", 10, true)
	_seed(bfme, 700, Sim.PLAYER_TEAM, "SharedLair", "EmptySet", any_edges)
	var row := bfme.structures[700] as Dictionary
	_check("any_waits_without_trigger", not bool(bfme._reconcile_structure_command_set_upgrades(row).get("ok", false)) and String(row.command_set_id) == "EmptySet")
	bfme.set_team_upgrade_state(Sim.PLAYER_TEAM, "Upgrade_A", true)
	_check("any_activates_on_one_trigger", String(row.command_set_id) == "AnySet")
	_check("receipt_is_edition_and_provenance_bound", row.command_set_upgrade_receipt == {
		"game": "bfme2", "effectId": "bfme2:SharedLair:10", "commandSetId": "AnySet",
		"triggerUpgradeIds": ["Upgrade_A", "Upgrade_B"], "triggerSemantics": "any",
		"commandSetProvenance": {"authored": "AnySet", "sourceIni": "data/ini/object/neutral/sharedlair.ini", "line": 12},
		"customAnimationStatus": "deferred",
	})
	_check("custom_animation_not_fabricated", not row.has("custom_animation") and not row.has("presentation_animation"))
	var active_hash: String = bfme.state_hash()
	var restored: RetailSliceSim = _sim("bfme2")
	_check("snapshot_restores", restored.restore(bfme.snapshot()))
	_check("snapshot_hash_round_trips", restored.state_hash() == active_hash)
	bfme.set_team_upgrade_state(Sim.PLAYER_TEAM, "Upgrade_A", false)
	_check("trigger_removal_restores_base", String(row.command_set_id) == "EmptySet" and String(row.command_set_upgrade_active_effect) == "")
	var live_document := _scenario_document("bfme2", any_edges)
	var live_sim: RetailSliceSim = Sim.new()
	live_sim.setup({}, {"game": "bfme2", "spawn_initial_battalions": false, "scenario_structure_runtimes": {"SharedLair": live_document}})
	var live_id := live_sim.spawn_scenario_structure("SharedLair", Sim.PLAYER_TEAM, Vector2.ZERO, "map-placement")
	_check("scenario_spawn_attaches_accepted_graph", live_id > 0 and (live_sim.structures[live_id] as Dictionary).get("scenario_command_set_upgrade_effects", []).size() == 2)
	live_sim.set_team_upgrade_state(Sim.PLAYER_TEAM, "Upgrade_A", true)
	_check("scenario_spawn_consumes_generic_graph", String((live_sim.structures[live_id] as Dictionary).command_set_id) == "AnySet")
	_check("scenario_graph_stays_out_of_faction_registry", not live_sim._structure_kinds.has("lair"))

	var all_sim: RetailSliceSim = _sim("rotwk")
	_seed(all_sim, 701, Sim.PLAYER_TEAM, "SharedLair", "EmptySet", _edges("rotwk", "AllSet", ["Upgrade_A", "Upgrade_B"], "all", 20, false))
	var all_row := all_sim.structures[701] as Dictionary
	all_sim.set_team_upgrade_state(Sim.PLAYER_TEAM, "Upgrade_A", true)
	_check("all_waits_for_every_trigger", String(all_row.command_set_id) == "EmptySet")
	all_sim.set_team_upgrade_state(Sim.PLAYER_TEAM, "upgrade_b", true)
	_check("all_activates_after_last_trigger", String(all_row.command_set_id) == "AllSet")

	var mismatch: RetailSliceSim = _sim("bfme2")
	_seed(mismatch, 702, Sim.PLAYER_TEAM, "SharedLair", "EmptySet", _edges("rotwk", "WrongSet", ["Upgrade_A"], "any", 30, false))
	var mismatch_result: Dictionary = mismatch._reconcile_structure_command_set_upgrades(mismatch.structures[702] as Dictionary)
	_check("cross_edition_shared_id_fails_closed", bool(mismatch_result.get("accepted_graph", false)) and String(mismatch_result.get("reason", "")) == "command-set-upgrade-edition-mismatch" and String((mismatch.structures[702] as Dictionary).command_set_id) == "EmptySet")
	var incomplete := _sim("bfme2")
	var incomplete_edges := _edges("bfme2", "BrokenSet", ["Upgrade_A", "Upgrade_B"], "any", 31, false)
	incomplete_edges.pop_back()
	_seed(incomplete, 705, Sim.PLAYER_TEAM, "SharedLair", "EmptySet", incomplete_edges)
	var incomplete_result: Dictionary = incomplete._reconcile_structure_command_set_upgrades(incomplete.structures[705] as Dictionary)
	_check("incomplete_duplicate_edge_graph_fails_closed", bool(incomplete_result.get("accepted_graph", false)) and String(incomplete_result.get("reason", "")) == "malformed-command-set-upgrade-graph")

	var faction_structure: RetailSliceSim = _sim("bfme2")
	_seed(faction_structure, 703, Sim.PLAYER_TEAM, "MenBarracks", "MenBarracksSet", [])
	faction_structure.set_team_upgrade_state(Sim.PLAYER_TEAM, "Upgrade_MenFaction", true)
	_check("structure_without_authored_effect_is_unchanged", String((faction_structure.structures[703] as Dictionary).command_set_id) == "MenBarracksSet")
	var neutral: RetailSliceSim = _sim("bfme2")
	_seed(neutral, 704, Sim.CREEP_TEAM, "SharedLair", "EmptySet", _edges("bfme2", "MenLairSet", ["Upgrade_MenFaction"], "any", 40, false))
	_check("neutral_lair_does_not_infer_faction_upgrade", not bool(neutral._reconcile_structure_command_set_upgrades(neutral.structures[704] as Dictionary).get("ok", false)) and String((neutral.structures[704] as Dictionary).command_set_id) == "EmptySet")

	print("COMMAND_SET_UPGRADE_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _sim(game: String) -> RetailSliceSim:
	var sim = Sim.new()
	sim.setup({}, {"game": game, "spawn_initial_battalions": false, "enable_base_loop": false})
	sim.ai_enabled = false
	sim.base_loop_enabled = false
	sim.entities.clear()
	sim.structures.clear()
	return sim


func _seed(sim, id: int, team: int, source_id: String, base_set: String, effects: Array) -> void:
	sim.structures[id] = {
		"id": id, "team": team, "kind": "structure", "structure_kind": "lair",
		"source_object_id": source_id, "scenario_source_object_id": source_id,
		"scenario_game": String(sim._rules.game), "health": 100, "maximum_health": 100,
		"position": Vector2.ZERO, "completed_upgrades": [], "command_set_id": base_set,
		"default_command_set_id": base_set, "scenario_command_set_upgrade_effects": effects,
	}


func _edges(game: String, command_set: String, triggers: Array, semantics: String, ordinal: int, custom_anim: bool) -> Array:
	var rows: Array = []
	for trigger_value in triggers:
		var row := {
			"effectId": "%s:SharedLair:%d" % [game, ordinal], "game": game,
			"upgradeId": String(trigger_value), "triggerUpgradeIds": triggers.duplicate(),
			"triggerSemantics": semantics, "kind": "command-set-transition",
			"module": "CommandSetUpgrade", "moduleTag": "ModuleTag_%d" % ordinal,
			"moduleOrdinal": ordinal, "commandSetId": command_set,
			"commandSetProvenance": {"authored": command_set, "sourceIni": "data/ini/object/neutral/sharedlair.ini", "line": ordinal + 2},
			"descriptorStatus": "resolved", "runtimeStatus": "executable",
			"sourceIni": "data/ini/object/neutral/sharedlair.ini", "line": ordinal,
		}
		if custom_anim:
			row["customAnimation"] = {"animState": "USER_2", "animTimeMs": 0.0, "authored": "AnimState:USER_2 AnimTime:0", "sourceIni": row.sourceIni, "line": ordinal + 3, "runtimeStatus": "deferred", "deferredReason": "presentation-runtime-not-accepted"}
		rows.append(row)
	return rows


func _scenario_document(game: String, effects: Array) -> Dictionary:
	return {
		"schema": "openbfme.playable-structure-runtime", "objectId": "SharedLair", "game": game,
		"registration": {
			"production": {"evidence": "authored-neutral-map", "routes": []},
			"scenarioAdmission": {"kind": "authored-neutral-non-buildable", "role": "lair", "surfaces": ["map-placement"], "buildCommandExposed": false},
			"gameplay": {
				"health": {"primary": {"module": "ActiveBody", "maxHealth": {"authored": "100", "value": 100}}},
				"scalarFields": {}, "moduleContracts": [],
				"trainedCommandSets": [
					{"id": "EmptySet", "kind": "direct", "slots": []},
					{"id": "AnySet", "kind": "upgraded", "triggeredBy": ["Upgrade_A", "Upgrade_B"], "slots": []},
				],
				"upgradeEffects": {"effects": effects, "unsupportedEffects": [], "sourceIni": ["data/ini/object/neutral/sharedlair.ini"]},
			},
			"presentation": {"buildingLifecycle": {"simulationFacts": {"maximumHealth": 100}}},
		},
	}


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("COMMAND_SET_UPGRADE_RUNTIME_FAIL %s" % label)

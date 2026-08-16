extends SceneTree

## Selected-pack proof that a canonical neutral lair consumes its authored
## faction CommandSetUpgrade when Untamed Allegiance changes ownership. Spawn
## orphan assertions live here too because the selected CaveTrollLair and its
## exact CaveTroll_Slaved child are the retail graph that authors the behavior.

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const Watchdog = preload("res://tests/runner_watchdog.gd")

var passed := 0
var failed := 0
var watchdog = Watchdog.new()


func _initialize() -> void:
	watchdog.start(self, "SELECTED_LAIR_ALLEGIANCE_RUNNER")
	OS.set_environment("OPENBFME_STARTER_ARMY", "1")
	OS.set_environment("OPENBFME_SCENARIO_MAP_PLACEMENTS", "1")
	call_deferred("_run")


func _run() -> void:
	root.size = Vector2i(1920, 1080)
	var packed: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	_check(packed != null, "scene_parses")
	if packed == null:
		_finish()
		return
	var slice = packed.instantiate()
	root.add_child(slice)
	await process_frame
	await process_frame
	_check(bool(slice.ready_ok), "selected_slice_ready", String(slice.failure_reason))
	if not bool(slice.ready_ok):
		slice.cleanup_for_test()
		slice.queue_free()
		await process_frame
		_finish()
		return
	var rules: Dictionary = slice.gameplay_rules.duplicate(true)
	rules["enable_base_loop"] = false
	rules["spawn_initial_battalions"] = false
	rules["enable_scenario_map_placements"] = true
	var sim = Sim.new()
	sim.setup(slice.source_map_data.simulation_configuration().duplicate(true), rules)
	sim.ai_enabled = false
	sim.advance(1)
	var lair_id := 0
	for id in sim.structure_ids(Sim.CREEP_TEAM):
		if String((sim.structures[id] as Dictionary).get("source_object_id", "")) == "CaveTrollLair":
			lair_id = id
			break
	_check(lair_id > 0, "selected_cave_lair_present")
	if lair_id <= 0:
		slice.cleanup_for_test()
		slice.queue_free()
		await process_frame
		_finish()
		return
	var lair := sim.structures[lair_id] as Dictionary
	_check(String(lair.get("command_set_id", "")) == "EmptyCommandSet", "authored_direct_command_set_initial")
	var children := (lair.get("spawn_behavior", {}) as Dictionary).get("spawned_ids", []) as Array
	_check(children.size() == 1, "authored_cave_initial_burst", str(children))
	_check(bool((lair.get("spawn_behavior", {}) as Dictionary).get("can_reclaim_orphans", false)), "authored_reclaim_field_preserved")
	_check(String((lair.get("spawn_behavior", {}) as Dictionary).get("can_reclaim_runtime", "")) == "bfme2-rotwk-binary-proven", "reclaim_binary_runtime_proven")
	var result: Dictionary = sim._cast_spellbook_creep_allegiance(Sim.PLAYER_TEAM, {
		"range_source": 1.0,
		"filter": "ENEMIES +CREEP",
		"lair_types": ["CaveTrollLair"],
	}, Vector2(lair.get("position", Vector2.ZERO)))
	_check(bool(result.get("ok", false)), "selected_lair_converts", str(result))
	_check(int(lair.get("team", -1)) == Sim.PLAYER_TEAM, "lair_owner_changes")
	_check((lair.get("completed_upgrades", []) as Array).has("Upgrade_MenFaction"), "retail_side_upgrade_installed", str(lair.get("completed_upgrades", [])))
	_check(String(lair.get("command_set_id", "")) == "NeutralTrollCaveCommandSet", "authored_upgraded_command_set_selected", String(lair.get("command_set_id", "")))
	_check(sim.structure_command_slot(lair_id, "Command_ConstructGoblinTrollFromDefectedLair") == 1, "defected_command_surface_exact_slot")
	if not children.is_empty() and sim.entities.has(int(children[0])):
		var child := sim.entities[int(children[0])] as Dictionary
		_check(int(child.get("team", -1)) == Sim.PLAYER_TEAM, "spawn_child_owner_follows_lair")
		_check(int((child.get("slaved_update", {}) as Dictionary).get("master_id", 0)) == lair_id, "spawn_child_master_preserved")
	var warg_id := 0
	for id in sim.structure_ids(Sim.CREEP_TEAM):
		if String((sim.structures[id] as Dictionary).get("source_object_id", "")) == "WargLair":
			warg_id = id
			break
	_check(warg_id > 0, "selected_warg_lair_present")
	if warg_id > 0:
		var warg_lair := sim.structures[warg_id] as Dictionary
		var warg_result: Dictionary = sim._cast_spellbook_creep_allegiance(Sim.PLAYER_TEAM, {
			"range_source": 1.0,
			"filter": "ENEMIES +CREEP",
			"lair_types": ["WargLair"],
		}, Vector2(warg_lair.get("position", Vector2.ZERO)))
		_check(bool(warg_result.get("ok", false)), "selected_warg_lair_converts", str(warg_result))
		_check(String(warg_lair.get("command_set_id", "")) == "NeutralWargLairCommandSet", "warg_authored_upgraded_command_set")
		_check(sim.structure_command_slot(warg_id, "Command_ConstructWargFromDefectedLair") == 1, "warg_defected_command_exact_slot")
	# TargetEnemy + ENEMIES permits the opposing roster team to counter-cast a
	# previously defected lair, but never permits recasting an already-friendly
	# lair. The child keeps its exact producer/master identity while ownership
	# follows the explicit allegiance system.
	var counter_result: Dictionary = sim._cast_spellbook_creep_allegiance(Sim.ENEMY_TEAM, {
		"range_source": 1.0,
		"filter": "ENEMIES +CREEP",
		"lair_types": ["CaveTrollLair"],
	}, Vector2(lair.get("position", Vector2.ZERO)))
	_check(bool(counter_result.get("ok", false)), "opponent_can_counter_cast_defected_lair", str(counter_result))
	_check(int(lair.get("team", -1)) == Sim.ENEMY_TEAM, "counter_cast_transfers_lair_to_opponent")
	var counter_children_ok := true
	for child_value in children:
		var child_id := int(child_value)
		if not sim.entities.has(child_id):
			counter_children_ok = false
			continue
		var child := sim.entities[child_id] as Dictionary
		counter_children_ok = counter_children_ok and int(child.get("team", -1)) == Sim.ENEMY_TEAM and int((child.get("slaved_update", {}) as Dictionary).get("master_id", 0)) == lair_id
	_check(counter_children_ok, "counter_cast_transfers_children_and_preserves_master")
	var friendly_repeat: Dictionary = sim._cast_spellbook_creep_allegiance(Sim.ENEMY_TEAM, {
		"range_source": 1.0,
		"filter": "ENEMIES +CREEP",
		"lair_types": ["CaveTrollLair"],
	}, Vector2(lair.get("position", Vector2.ZERO)))
	_check(String(friendly_repeat.get("reason", "")) == "no-valid-targets", "friendly_lair_recast_refused")
	if not children.is_empty() and sim.entities.has(int(children[0])):
		var orphan_id := int(children[0])
		var orphan := sim.entities[orphan_id] as Dictionary
		var preserved_health := int(orphan.get("health", 0))
		var preserved_position := Vector2(orphan.get("position", Vector2.ZERO))
		lair["health"] = 0
		sim._step_spawn_behaviors()
		_check(int(orphan.get("health", 0)) == preserved_health and Vector2(orphan.get("position", Vector2.ZERO)) == preserved_position, "nonrequired_child_survives_in_place")
		_check(not orphan.has("spawn_behavior_parent_id") and not orphan.has("spawn_behavior_parent_kind"), "dead_spawner_clears_producer_edge")
		_check(int((orphan.get("slaved_update", {}) as Dictionary).get("master_id", -1)) == 0, "dead_spawner_clears_slaved_master")
		_check((lair.get("spawn_behavior", {}) as Dictionary).get("spawned_ids", []) == [], "dead_spawner_releases_roster")
	var restored = Sim.new()
	_check(restored.restore(sim.snapshot()), "snapshot_restores")
	_check(restored.state_hash() == sim.state_hash(), "snapshot_hash_equal")
	slice.cleanup_for_test()
	slice.queue_free()
	await process_frame
	_finish()


func _check(condition: bool, label: String, detail: String = "") -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("%s%s" % [label, " :: " + detail if detail != "" else ""])


func _finish() -> void:
	watchdog.stop()
	if failed == 0:
		print("SELECTED_LAIR_ALLEGIANCE_OK passed=%d" % passed)
		quit(0)
	else:
		print("SELECTED_LAIR_ALLEGIANCE_FAILED passed=%d failed=%d" % [passed, failed])
		quit(1)

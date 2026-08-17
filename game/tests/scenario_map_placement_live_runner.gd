extends SceneTree

const BFME2_NEUTRAL_DIGEST := "ccc75c1d6e3272581f6a98ca0d8d56f4040b0ae68d14cda8c1afc6152c8819ce"

## Live selected-pack proof for descriptor-backed map placement on Fords. The
## Source-index ownership proves the same six source rows can never be seeded
## twice when castle and capturable lanes are also enabled.

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const Watchdog = preload("res://tests/runner_watchdog.gd")

var passed := 0
var failed := 0
var watchdog = Watchdog.new()


func _initialize() -> void:
	watchdog.start(self, "SCENARIO_MAP_PLACEMENT_LIVE_RUNNER")
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
	var content_db = root.get_node_or_null("ContentDB")
	_check(content_db != null and content_db.get_scenario_structure_runtimes("rotwk").size() == 28, "selected_rotwk_structure_registry_live")
	_check(content_db != null and content_db.get_scenario_unit_runtimes("rotwk").size() == 48, "selected_rotwk_unit_registry_live")
	_check(content_db != null and content_db.get_scenario_prop_runtimes("rotwk").size() == 12, "selected_rotwk_prop_registry_live")
	_check(content_db != null and content_db.get_scenario_pickup_runtimes("rotwk").size() == 1, "selected_rotwk_pickup_registry_live")
	_check(content_db != null and content_db.get_scenario_structure_runtimes("bfme2").size() == 19, "selected_bfme2_structure_registry_live")
	_check(content_db != null and content_db.get_scenario_unit_runtimes("bfme2").size() == 42, "selected_bfme2_unit_registry_live")
	_check(content_db != null and content_db.get_scenario_prop_runtimes("bfme2").size() == 12, "selected_bfme2_prop_registry_live")
	_check(content_db != null and content_db.get_scenario_pickup_runtimes("bfme2").size() == 2, "selected_bfme2_pickup_registry_live")

	var configuration: Dictionary = slice.source_map_data.simulation_configuration()
	var source_rows := configuration.get("scenario_object_placements", []) as Array
	_check(source_rows.size() == int(slice.source_map_data.nonroad_object_count), "complete_nonroad_source_stream", "%d != %d" % [source_rows.size(), int(slice.source_map_data.nonroad_object_count)])
	var rules: Dictionary = slice.gameplay_rules.duplicate(true)
	rules["enable_base_loop"] = false
	rules["spawn_initial_battalions"] = false
	rules["enable_scenario_map_placements"] = true
	var sim = Sim.new()
	_check(String(rules.get("game", "")) == "rotwk", "live_match_selects_rotwk_edition")
	sim.setup(configuration.duplicate(true), rules)
	sim.ai_enabled = false
	var capture_armor := sim._structure_armor.get("capture_flag", {}) as Dictionary
	var signal_fire_armor := sim._structure_armor.get("signal_fire", {}) as Dictionary
	var inn_armor := sim._structure_armor.get("inn", {}) as Dictionary
	_check(bool(capture_armor.get("passthrough", false)) and String(capture_armor.get("set_id", "")) == "", "capture_flag_uses_selected_explicit_null_armor")
	_check(String(signal_fire_armor.get("set_id", "")) == "StructureArmor" and is_equal_approx(float((signal_fire_armor.get("scalars", {}) as Dictionary).get("default", 0.0)), 0.6), "signal_fire_uses_selected_structure_armor")
	_check(String(inn_armor.get("set_id", "")) == "NeutralInn-provisional", "inn_armor_gap_remains_explicit")
	var capture_flags: Array[Dictionary] = []
	var linked_inns: Array[Dictionary] = []
	for structure_id in sim.structure_ids():
		var placed := sim.structures[structure_id] as Dictionary
		if String(placed.get("structure_kind", "")) == "capture_flag":
			capture_flags.append(placed)
		elif String(placed.get("structure_kind", "")) == "inn" and bool(placed.get("linked_to_flag", false)):
			linked_inns.append(placed)
	_check(capture_flags.size() == 2 and linked_inns.size() == 2, "fords_keeps_two_rich_capture_pairs", "flags=%d inns=%d" % [capture_flags.size(), linked_inns.size()])
	var capture_contracts_ok := true
	for flag in capture_flags:
		var linked_id := int(flag.get("linked_structure_id", 0))
		capture_contracts_ok = capture_contracts_ok and int(flag.get("team", -1)) == Sim.NEUTRAL_TEAM and bool(flag.get("capturable", false)) and sim.structures.has(linked_id)
		if sim.structures.has(linked_id):
			var linked := sim.structures[linked_id] as Dictionary
			capture_contracts_ok = capture_contracts_ok and int(linked.get("linked_structure_id", 0)) == int(flag.get("id", 0)) and bool(linked.get("linked_to_flag", false))
	_check(capture_contracts_ok, "fords_capture_flags_retain_neutral_link_contract")
	var capture_probe = Sim.new()
	capture_probe.setup(configuration.duplicate(true), rules.duplicate(true))
	var capture_transfer_ok := false
	for probe_id in capture_probe.structure_ids():
		var probe_flag := capture_probe.structures[probe_id] as Dictionary
		if String(probe_flag.get("structure_kind", "")) != "capture_flag":
			continue
		var linked_id := int(probe_flag.get("linked_structure_id", 0))
		var probe_type := "scenario.capture.probe"
		(capture_probe._rules.get("unit_rules", {}) as Dictionary)[probe_type] = _capture_probe_rule()
		capture_probe._unit_ability_rules[probe_type] = capture_probe._scaled_ability_rules([{
			"ability_id": "Command_CaptureBuilding", "slot": 12, "targeting": "enemy-object", "cooldown_ticks": 0,
			"required_level": 1, "level_gate_resolved": true, "castable": true,
			"effect": {"kind": "capture-building", "startAbilityRange": 15.0, "unpackMs": 1.0, "preparationMs": 100.0, "packMs": 1.0},
		}], 1.0)
		capture_probe._add_battalion(59001, Sim.PLAYER_TEAM, Vector2(probe_flag.get("position", Vector2.ZERO)), "Capture Probe", probe_type, probe_type, 0)
		var cast := capture_probe.cast_ability(59001, "Command_CaptureBuilding", Vector2(probe_flag.get("position", Vector2.ZERO)))
		capture_probe.advance(3)
		capture_transfer_ok = bool(cast.get("ok", false)) and int(probe_flag.get("team", -1)) == Sim.PLAYER_TEAM and capture_probe.structures.has(linked_id) and int((capture_probe.structures[linked_id] as Dictionary).get("team", -1)) == Sim.PLAYER_TEAM
		break
	_check(capture_transfer_ok, "fords_real_capture_channel_transfers_linked_pair")

	var expected_indices := [42, 43, 249, 250, 499, 500]
	var observed_indices: Array[int] = []
	var lair_ids: Array[int] = []
	for id in sim.structure_ids(Sim.CREEP_TEAM):
		var row := sim.structures[id] as Dictionary
		var source_id := String(row.get("source_object_id", ""))
		if source_id not in ["CaveTrollLair", "WargLair"]:
			continue
		lair_ids.append(id)
		observed_indices.append(int(row.get("scenario_source_index", -1)))
		_check(String(row.get("scenario_spawn_surface", "")) == "map-placement", "surface_%d" % id)
		_check(String(row.get("scenario_game", "")) == "rotwk", "edition_receipt_%d" % id)
		_check(typeof(row.get("scenario_source_position")) == TYPE_VECTOR3, "source_transform_%d" % id)
		var authored_yaw := INF
		for source_value in source_rows:
			var source := source_value as Dictionary
			if int(source.get("source_index", -1)) == int(row.get("scenario_source_index", -1)):
				authored_yaw = float(source.get("yaw", INF))
				break
		_check(is_equal_approx(float(row.get("yaw", NAN)), authored_yaw), "yaw_%d" % id)
	observed_indices.sort()
	lair_ids.sort()
	_check(observed_indices == expected_indices, "exact_fords_lair_source_indices", str(observed_indices))
	_check(lair_ids == [60001, 60002, 60003, 60004, 60005, 60006], "deterministic_scenario_structure_ids", str(lair_ids))
	_check((sim._scenario_map_seeded_source_indices as Dictionary).size() == 7, "registry_claims_exact_fords_runtime_sources")
	_check(not configuration.has("creep_lair_placements"), "map_configuration_has_no_provisional_lair_stream")
	_check(not sim._rules.has("enable_creep_lairs"), "match_rules_have_no_provisional_lair_gate")
	var retired_state := sim._authoritative_state()
	_check(not retired_state.has("next_creep_guard_id") and not retired_state.has("next_creep_structure_id"), "authoritative_state_has_no_provisional_cursors")
	# The re-sealed profile closes Wolf's gameplay and visual contracts. Its map
	# owner is civilian, but it remains a descriptor-backed scenario unit and
	# consumes the same deterministic entity-id stream as hostile lair children.
	var wolf_ids: Array[int] = []
	for id in sim.entity_ids():
		var row := sim.entities[id] as Dictionary
		if String(row.get("scenario_source_object_id", "")) != "Wolf":
			continue
		wolf_ids.append(id)
		_check(int(row.get("team", -1)) == Sim.CASTLE_CIVILIAN_TEAM, "wolf_retains_civilian_map_owner")
		_check(String(row.get("scenario_spawn_surface", "")) == "map-placement", "wolf_uses_map_placement_surface")
		_check(int(row.get("scenario_source_index", -1)) >= 0, "wolf_retains_source_index")
	wolf_ids.sort()
	_check(wolf_ids == [70001], "deterministic_civilian_wolf_id", str(wolf_ids))

	var seeded_events := 0
	var seeded_structure_events := 0
	for event_value in sim.events:
		var event := event_value as Dictionary
		if String(event.get("kind", "")) != "scenario.map_placement_seeded":
			continue
		seeded_events += 1
		if int(event.get("target_id", 0)) > 0:
			seeded_structure_events += 1
	_check(seeded_events == lair_ids.size() + wolf_ids.size(), "one_seed_event_per_lair_and_wolf", "%d != %d" % [seeded_events, lair_ids.size() + wolf_ids.size()])
	_check(seeded_structure_events == lair_ids.size(), "structure_target_receipts_remain_lair_only", "%d != %d" % [seeded_structure_events, lair_ids.size()])

	# Initial SpawnBehavior bursts resolve children from the same selected unit
	# registry on the next deterministic simulation step.
	sim.advance(1)
	var children := 0
	var child_ids: Array[int] = []
	for id in sim.entity_ids():
		var row := sim.entities[id] as Dictionary
		if int(row.get("team", -1)) != Sim.CREEP_TEAM or not row.has("spawn_behavior_parent_id"):
			continue
		children += 1
		child_ids.append(id)
		_check(String(row.get("scenario_spawn_surface", "")) == "lair-spawn", "child_surface_%d" % id)
	child_ids.sort()
	_check(children == 10, "exact_fords_initial_burst", str(children))
	_check(not child_ids.is_empty() and child_ids[0] == 70002, "deterministic_creep_child_ids_after_wolf", str(child_ids))

	# Reusing setup must clear and reseed, not append a second copy.
	var first_signature := sim.state_signature()
	var restored = Sim.new()
	_check(restored.restore(sim.snapshot()), "registry_map_snapshot_restores")
	_check(restored.state_hash() == sim.state_hash(), "registry_map_snapshot_hash_equal", _first_diff(sim._authoritative_state(), restored._authoritative_state()))
	sim.setup(configuration.duplicate(true), rules.duplicate(true))
	sim.ai_enabled = false
	sim.advance(1)
	_check(sim.state_signature() == first_signature, "reset_no_double_seed")

	# The selected registry's executable unit and passive-prop domains traverse
	# the identical map-placement seeding function (isolated source rows avoid
	# conflating this contract with whether Fords happens to author the type).
	var domain_configuration := configuration.duplicate(true)
	domain_configuration["scenario_object_placements"] = [
		{"type_name": "NeutralWarg", "source_index": 7, "source_position": Vector3(10, 20, 30), "position": Vector2(1, 2), "yaw": 0.75, "properties": {"originalOwner": "PlyrCreeps/teamPlyrCreeps"}},
		{"type_name": "SpiderWebs01", "source_index": 8, "source_position": Vector3(40, 50, 60), "position": Vector2(3, 4), "yaw": 1.25, "properties": {"originalOwner": "PlyrNeutral/teamPlyrNeutral"}},
	]
	domain_configuration["capturable_placements"] = [{"type_name": "NeutralWarg", "structure_kind": "inn", "source_index": 7, "position": Vector2(1, 2), "yaw": 0.75, "maximum_health": 100, "capturable": false, "linked_to_flag": false, "unattackable": false}]
	domain_configuration["castle_fixture_placements"] = [{"type_name": "SpiderWebs01", "role": "wall", "source_index": 8, "position": Vector2(3, 4), "elevation": 0.0, "yaw": 1.25, "owner": "PlyrNeutral/teamPlyrNeutral", "maximum_health": 100.0, "armor": "NoArmor", "indestructible": false}]
	rules["enable_capturable_neutrals"] = true
	rules["enable_castle_fixtures"] = true
	var domains = Sim.new()
	domains.setup(domain_configuration, rules.duplicate(true))
	_check(domains.entities.has(70001) and String((domains.entities[70001] as Dictionary).get("scenario_source_object_id", "")) == "NeutralWarg", "selected_unit_map_domain")
	_check(domains.scenario_props.has(400000) and String((domains.scenario_props[400000] as Dictionary).get("source_object_id", "")) == "SpiderWebs01", "selected_prop_map_domain")
	_check(Vector3((domains.scenario_props[400000] as Dictionary).get("scenario_source_position", Vector3.ZERO)) == Vector3(40, 50, 60), "selected_prop_source_transform")
	_check(not domains.structures.has(90001) and not domains.structures.has(80001), "registry_source_indices_not_double_seeded")

	# The same selected graph carries both sealed neutral editions. An explicit
	# BFME2 match must snapshot only BFME2 leaves even for the 67 shared IDs.
	var bfme2_rules := rules.duplicate(true)
	bfme2_rules["game"] = "bfme2"
	var bfme2 = Sim.new()
	bfme2.setup(configuration.duplicate(true), bfme2_rules)
	bfme2.ai_enabled = false
	_check((bfme2._rules.get("scenario_unit_runtimes", {}) as Dictionary).size() == 42, "bfme2_match_snapshots_42_units")
	_check((bfme2._rules.get("scenario_structure_runtimes", {}) as Dictionary).size() == 19, "bfme2_match_snapshots_19_structures")
	_check((bfme2._rules.get("scenario_prop_runtimes", {}) as Dictionary).size() == 12, "bfme2_match_snapshots_12_props")
	_check((bfme2._rules.get("scenario_pickup_runtimes", {}) as Dictionary).size() == 2, "bfme2_match_snapshots_2_pickups")
	var shared_cave := (bfme2._rules.get("scenario_structure_runtimes", {}) as Dictionary).get("CaveTrollLair", {}) as Dictionary
	_check(String(shared_cave.get("_scenario_game", "")) == "bfme2" and String(shared_cave.get("_pack_root", "")).replace("\\", "/").ends_with("/bfme2-neutral-vslice/%s" % BFME2_NEUTRAL_DIGEST), "shared_cave_uses_exact_bfme2_receipt_provenance")
	_check(bfme2.structure_ids(Sim.CREEP_TEAM).size() == 6, "bfme2_fords_shared_lairs_seed_once")
	bfme2.advance(1)
	var bfme2_children := 0
	for id in bfme2.entity_ids():
		if (bfme2.entities[id] as Dictionary).has("spawn_behavior_parent_id"): bfme2_children += 1
	_check(bfme2_children == 10, "bfme2_shared_spawn_graph_live")

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


func _first_diff(a: Variant, b: Variant, path: String = "$") -> String:
	if typeof(a) != typeof(b):
		return "%s type %d != %d" % [path, typeof(a), typeof(b)]
	if typeof(a) == TYPE_DICTIONARY:
		var ad := a as Dictionary
		var bd := b as Dictionary
		var keys: Array = ad.keys()
		for key in bd.keys():
			if not keys.has(key): keys.append(key)
		keys.sort_custom(func(x, y): return str(x) < str(y))
		for key in keys:
			if not ad.has(key) or not bd.has(key): return "%s.%s presence" % [path, str(key)]
			var diff := _first_diff(ad[key], bd[key], "%s.%s" % [path, str(key)])
			if diff != "": return diff
		return ""
	if typeof(a) == TYPE_ARRAY:
		var aa := a as Array
		var ba := b as Array
		if aa.size() != ba.size(): return "%s size %d != %d" % [path, aa.size(), ba.size()]
		for index in aa.size():
			var diff := _first_diff(aa[index], ba[index], "%s[%d]" % [path, index])
			if diff != "": return diff
		return ""
	return "" if a == b else "%s %s != %s" % [path, str(a), str(b)]


func _capture_probe_rule() -> Dictionary:
	return {
		"horde_id": "scenario.capture.probe", "member_count": 1, "member_health": 100, "member_damage": 10,
		"speed": 20.0, "speed_source": 20.0, "acceleration": 20.0, "acceleration_source": 20.0,
		"turn_rate_degrees_per_second": 180.0, "braking": 20.0, "braking_source": 20.0,
		"attack_range": 20.0, "attack_range_source": 20.0, "minimum_attack_range": 0.0, "minimum_attack_range_source": 0.0,
		"vision_range": 40.0, "vision_range_source": 40.0, "delay_between_shots_ms": 600.0,
		"pre_attack_delay_ms": 200.0, "firing_duration_ms": 200.0, "attack_period_ticks": 10,
		"pre_attack_ticks": 2, "firing_duration_ticks": 2, "formation_positions": [Vector3.ZERO],
		"provenance": {}, "category": "infantry",
	}


func _finish() -> void:
	watchdog.stop()
	if failed == 0:
		print("SCENARIO_MAP_PLACEMENT_LIVE_OK passed=%d" % passed)
		quit(0)
	else:
		print("SCENARIO_MAP_PLACEMENT_LIVE_FAILED passed=%d failed=%d" % [passed, failed])
		quit(1)

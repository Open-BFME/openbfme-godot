extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED := 23
var passed := 0
var failed := 0

func _initialize() -> void: call_deferred("_run")

func _run() -> void:
	var sim := _sim([_contract("OPTION_ONE", 100.0), _contract("OPTION_TWO", 80.0)])
	var hub := sim.structures[1000] as Dictionary; var policies := hub.get("wall_hub_behaviors", []) as Array
	_check("WallHubBehavior_repeated_contracts_attach", policies.size() == 2)
	_check("option_order_is_descriptor_order", String((policies[0] as Dictionary).get("option", "")) == "OPTION_ONE" and String((policies[1] as Dictionary).get("option", "")) == "OPTION_TWO")
	_check("segment_template_order_and_duplicates_preserved", (policies[0] as Dictionary).get("segment_templates", []) == ["WallSegment", "WallSegment", "WallHub"])
	_check("all_distance_rows_preserved", ((policies[0] as Dictionary).get("max_buildout_distances", []) as Array).size() == 2)
	_check("effective_distance_uses_authored_last_row", is_equal_approx(float((policies[0] as Dictionary).get("effective_distance_source", 0.0)), 100.0))
	_check("distance_scales_to_world", is_equal_approx(float((policies[0] as Dictionary).get("effective_distance", 0.0)), 10.0))
	_check("builder_radius_preserved_and_scaled", is_equal_approx(float((policies[0] as Dictionary).get("builder_radius_source", 0.0)), 25.0) and is_equal_approx(float((policies[0] as Dictionary).get("builder_radius", 0.0)), 2.5))
	var plan: Dictionary = sim.request_wall_hub_plan(1000, "OPTION_ONE", Vector2(6, 0))
	_check("in_range_plan_is_accepted", bool(plan.get("ok", false)))
	_check("plan_preserves_segment_order", plan.get("segment_templates", []) == ["WallSegment", "WallSegment", "WallHub"])
	_check("plan_preserves_cap_templates", String(plan.get("hub_cap_template", "")) == "WallHub" and String(plan.get("default_segment_template", "")) == "WallSegment" and String(plan.get("cliff_cap_template", "")) == "WallCliff")
	_check("materialization_deferral_is_explicit", String(plan.get("materialization_status", "")) == "deferred-segment-spacing-and-build-economy")
	_check("plan_receipt_is_snapshot_state", (hub.get("wall_hub_plan_receipts", []) as Array).size() == 1)
	_check("plan_event_is_emitted", _event_count(sim, "wall_hub.plan_resolved") == 1)
	_check("out_of_range_is_refused", String(sim.request_wall_hub_plan(1000, "OPTION_ONE", Vector2(10.01, 0)).get("reason", "")) == "max-buildout-distance-exceeded")
	_check("unknown_option_is_refused", String(sim.request_wall_hub_plan(1000, "OPTION_THREE", Vector2.ONE).get("reason", "")) == "wall-hub-option-missing")
	var digest := sim.state_hash(); var snapshot := sim.snapshot(); var restored := _sim([_contract("OPTION_ONE", 100.0), _contract("OPTION_TWO", 80.0)])
	_check("snapshot_restores", restored.restore(snapshot))
	_check("snapshot_hash_round_trips", restored.state_hash() == digest)
	_check("plan_receipt_restores", ((restored.structures[1000] as Dictionary).get("wall_hub_plan_receipts", []) as Array).size() == 1)
	var define_sim := _sim([_define_contract()], {"WALL_RADIUS": 120.0}); var define_policy := _first_policy(define_sim)
	_check("explicit_define_table_resolves", bool(define_policy.get("executable", false)) and is_equal_approx(float(define_policy.get("effective_distance_source", 0.0)), 120.0))
	var unresolved := _sim([_define_contract()]); var unresolved_policy := _first_policy(unresolved)
	_check("unresolved_define_fails_closed", not bool(unresolved_policy.get("executable", true)) and (unresolved_policy.get("unsupported_semantics", []) as Array).has("unresolved-max-buildout-distance:WALL_RADIUS"))
	_check("unresolved_plan_is_refused", String(unresolved.request_wall_hub_plan(1000, "OPTION_ONE", Vector2.ONE).get("reason", "")) == "max-buildout-distance-unresolved")
	var opaque := _contract("OPTION_ONE", 100.0); opaque["extraction"] = "opaque"
	_check("opaque_contract_is_ignored", not (_sim([opaque]).structures[1000] as Dictionary).has("wall_hub_behaviors"))
	_check("typed_provenance_preserved", String((policies[0] as Dictionary).get("tag", "")) == "ModuleTag_OPTION_ONE")
	if passed + failed != EXPECTED: failed += 1; printerr("WALL_HUB_BEHAVIOR_RUNTIME_FAIL liveness")
	print("WALL_HUB_BEHAVIOR_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed]); quit(0 if failed == 0 else 1)

func _contract(option: String, effective: float) -> Dictionary:
	var distance_rows := [{"value": effective - 10.0, "sourceIni": "fixture/wall.ini", "line": 12}, {"value": effective, "sourceIni": "fixture/wall.ini", "line": 13}]
	return {"module": "WallHubBehavior", "runtimeStatus": "deferred", "extraction": "typed", "tag": "ModuleTag_%s" % option, "sourceIni": "fixture/wall.ini", "line": 10, "fields": {"Options": {"value": option}, "MaxBuildoutDistance": distance_rows, "EffectiveMaxBuildoutDistance": distance_rows[-1], "SegmentTemplateName": [{"value": "WallSegment"}, {"value": "WallSegment"}, {"value": "WallHub"}], "HubCapTemplateName": {"value": "WallHub"}, "DefaultSegmentTemplateName": {"value": "WallSegment"}, "CliffCapTemplateName": {"value": "WallCliff"}, "BuilderRadius": {"value": 25.0}, "StaggeredBuildFactor": {"value": "WALL_BUILD_FACTOR"}}}

func _define_contract() -> Dictionary:
	var row := {"define": "WALL_RADIUS", "sourceIni": "fixture/wall.ini", "line": 12}; var contract := _contract("OPTION_ONE", 100.0); (contract["fields"] as Dictionary)["MaxBuildoutDistance"] = [row]; (contract["fields"] as Dictionary)["EffectiveMaxBuildoutDistance"] = row; return contract

func _sim(contracts: Array, defines: Dictionary = {}) -> RetailSliceSim:
	var rule := {"horde_id": Sim.SOLDIER_HORDE_ID, "category": "infantry", "speed": 1.0, "speed_source": 10.0, "acceleration": 1.0, "acceleration_source": 10.0, "turn_rate_degrees_per_second": 180.0, "braking": 1.0, "braking_source": 10.0, "attack_range": 0.1, "attack_range_source": 1.0, "minimum_attack_range": 0.0, "minimum_attack_range_source": 0.0, "vision_range": 1.0, "vision_range_source": 10.0, "delay_between_shots_ms": 1000.0, "pre_attack_delay_ms": 0.0, "firing_duration_ms": 0.0, "attack_period_ticks": 10, "pre_attack_ticks": 0, "firing_duration_ticks": 0, "member_damage": 1, "member_health": 100, "member_count": 1, "formation_positions": [Vector3.ZERO], "provenance": {}}
	var rules := {}; for id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]: rules[id] = rule
	var sim: RetailSliceSim = Sim.new(); sim.setup({}, {"unit_rules": rules, "source_map_transform_scale": 0.1, "wall_hub_distance_defines": defines}); sim.ai_enabled = false; sim.base_loop_enabled = false; sim.structures.clear(); sim.register_structure_module_contracts("FixtureWallHub", contracts); sim.structures[1000] = {"id": 1000, "team": 0, "source_object_id": "FixtureWallHub", "structure_kind": "wall_hub", "position": Vector2.ZERO, "health": 100, "maximum_health": 100}; sim._attach_structure_module_contracts(sim.structures[1000] as Dictionary); return sim

func _event_count(sim: RetailSliceSim, kind: String) -> int:
	var count := 0; for value in sim.events: if String((value as Dictionary).get("kind", "")) == kind: count += 1
	return count

func _first_policy(sim: RetailSliceSim) -> Dictionary:
	var rows := (sim.structures[1000] as Dictionary).get("wall_hub_behaviors", []) as Array
	return rows[0] as Dictionary if not rows.is_empty() else {}

func _check(name: String, condition: bool) -> void:
	if condition: passed += 1
	else: failed += 1; push_error("WALL_HUB_BEHAVIOR_RUNTIME_FAIL " + name)

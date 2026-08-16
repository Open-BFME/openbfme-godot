extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED := 26
var passed := 0
var failed := 0

func _initialize() -> void: call_deferred("_run")

func _run() -> void:
	var sim := _sim(_contract(true, false, true))
	var child := sim.entities[1] as Dictionary; var policy := child.get("attach_update", {}) as Dictionary
	_check("AttachUpdate_typed_contract_attaches", not policy.is_empty())
	_check("filter_tokens_preserved", policy.get("object_filter", []) == ["NONE", "+MONSTER"])
	_check("scan_range_preserved_and_scaled", is_equal_approx(float(policy.get("scan_range_source", 0.0)), 100.0) and is_equal_approx(float(policy.get("scan_range", 0.0)), 10.0))
	sim._step_attach_updates()
	_check("nearest_matching_parent_selected", int(child.get("attach_parent_id", 0)) == 3)
	_check("parent_kind_recorded", String(child.get("attach_parent_kind", "")) == "entity")
	_check("always_teleport_moves_to_parent", Vector2(child.get("position", Vector2.ZERO)) == Vector2(3, 0))
	_check("parent_status_applied", bool(((sim.entities[3] as Dictionary).get("object_status", {}) as Dictionary).get("RIDER_ATTACHED", false)))
	(sim.entities[3] as Dictionary)["position"] = Vector2(4, 1); sim._step_attach_updates()
	_check("attached_child_follows_parent", Vector2(child.get("position", Vector2.ZERO)) == Vector2(4, 1))
	_check("attach_event_once", _event_count(sim, "module.attach_update_attached") == 1)
	var digest := sim.state_hash(); var snapshot := sim.snapshot(); var restored := _sim(_contract(true, false, true))
	_check("snapshot_restores", restored.restore(snapshot))
	_check("snapshot_hash_round_trips", restored.state_hash() == digest)
	_check("attachment_restores", int((restored.entities[1] as Dictionary).get("attach_parent_id", 0)) == 3)
	(sim.entities[2] as Dictionary)["health"] = 0; (sim.entities[3] as Dictionary)["health"] = 0; sim._step_attach_updates()
	_check("parent_death_detaches", int(child.get("attach_parent_id", 0)) == 0)
	_check("parent_status_cleared_on_detach", not ((sim.entities[3] as Dictionary).get("object_status", {}) as Dictionary).has("RIDER_ATTACHED"))
	_check("parent_death_event_receipted", _event_count(sim, "module.attach_update_parent_died") == 1)
	var child_death := _sim(_contract(true, false, true)); child_death._step_attach_updates(); var dead_child := child_death.entities[1] as Dictionary; var prior_parent := child_death.entities[3] as Dictionary; dead_child["health"] = 0; child_death._step_attach_updates()
	_check("child_death_detaches", int(dead_child.get("attach_parent_id", 0)) == 0)
	_check("child_death_releases_parent_status", not (prior_parent.get("object_status", {}) as Dictionary).has("RIDER_ATTACHED"))
	var manual := _sim(_contract(false, false, false)); var manual_child := manual.entities[1] as Dictionary
	manual._step_attach_updates()
	_check("missing_scan_range_does_not_auto_target", int(manual_child.get("attach_parent_id", 0)) == 0)
	_check("manual_request_still_validates_and_attaches", bool(manual.request_attach_update(1, 2, "entity").get("ok", false)))
	var far := _sim(_contract(true, false, true)); (far.entities[2] as Dictionary)["position"] = Vector2(20, 0); (far.entities[3] as Dictionary)["position"] = Vector2(30, 0)
	_check("manual_out_of_range_refused", String(far.request_attach_update(1, 2, "entity").get("reason", "")) == "attach-target-out-of-range")
	var wrong := _sim(_contract(true, false, true)); (wrong.entities[2] as Dictionary)["kind_of"] = ["INFANTRY"]; (wrong.entities[2] as Dictionary)["category"] = "infantry"
	_check("filter_refuses_wrong_target", String(wrong.request_attach_update(1, 2, "entity").get("reason", "")) == "attach-target-filter-refused")
	var anchored := _sim(_contract(true, true, true)); anchored._step_attach_updates(); var anchored_child := anchored.entities[1] as Dictionary; var anchored_policy := anchored_child.get("attach_update", {}) as Dictionary
	_check("anchor_top_gap_is_explicit", (anchored_policy.get("unsupported_semantics", []) as Array).has("model-geometry-top-transform-unresolved"))
	_check("anchor_top_does_not_invent_position", Vector2(anchored_child.get("position", Vector2.ZERO)) == Vector2.ZERO)
	var opaque := _contract(true, false, true); opaque["extraction"] = "opaque"
	_check("opaque_contract_fails_closed", not (_sim(opaque).entities[1] as Dictionary).has("attach_update"))
	var malformed := _contract(true, false, true); (malformed["fields"] as Dictionary).erase("ObjectFilter")
	_check("missing_required_filter_fails_closed", not (_sim(malformed).entities[1] as Dictionary).has("attach_update"))
	_check("eva_fields_are_presentation_receipts", (policy.get("unsupported_semantics", []) as Array).has("presentation-eva-event:ParentOwnerAttachmentEvaEvent"))
	if passed + failed != EXPECTED: failed += 1; printerr("ATTACH_UPDATE_RUNTIME_FAIL liveness")
	print("ATTACH_UPDATE_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed]); quit(0 if failed == 0 else 1)

func _contract(with_range: bool, anchor_top: bool, teleport: bool) -> Dictionary:
	var fields := {"ObjectFilter": {"value": ["NONE", "+MONSTER"]}, "ParentStatus": {"value": ["RIDER_ATTACHED"]}, "AlwaysTeleport": {"value": teleport}, "AnchorToTopOfGeometry": {"value": anchor_top}, "ParentOwnerAttachmentEvaEvent": {"value": "EVA_Attached"}, "ParentEnemyAttachmentEvaEvent": {"value": "EVA_EnemyAttached"}, "ParentOwnerDiedEvaEvent": {"value": "EVA_ParentDied"}}
	if with_range: fields["ScanRange"] = {"value": 100.0}
	return {"module": "AttachUpdate", "runtimeStatus": "deferred", "extraction": "typed", "tag": "ModuleTag_Attach", "sourceIni": "fixture/attach.ini", "line": 10, "fields": fields}

func _sim(contract: Dictionary) -> RetailSliceSim:
	var rule := {"horde_id": Sim.SOLDIER_HORDE_ID, "category": "infantry", "speed": 1.0, "speed_source": 10.0, "acceleration": 1.0, "acceleration_source": 10.0, "turn_rate_degrees_per_second": 180.0, "braking": 1.0, "braking_source": 10.0, "attack_range": 0.1, "attack_range_source": 1.0, "minimum_attack_range": 0.0, "minimum_attack_range_source": 0.0, "vision_range": 1.0, "vision_range_source": 10.0, "delay_between_shots_ms": 1000.0, "pre_attack_delay_ms": 0.0, "firing_duration_ms": 0.0, "attack_period_ticks": 10, "pre_attack_ticks": 0, "firing_duration_ticks": 0, "member_damage": 1, "member_health": 100, "member_count": 1, "formation_positions": [Vector3.ZERO], "provenance": {}}
	var rules := {}; for id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]: rules[id] = rule
	var sim: RetailSliceSim = Sim.new(); sim.setup({}, {"unit_rules": rules, "source_map_transform_scale": 0.1}); sim.ai_enabled = false; sim.base_loop_enabled = false; sim.entities.clear(); sim.structures.clear(); sim._unit_module_contracts["FixtureAttach"] = [contract]
	sim.entities[1] = {"id": 1, "unit_type": "FixtureAttach", "team": 0, "position": Vector2.ZERO, "health": 10, "kind_of": ["ATTACHMENT"], "category": "attachment"}; sim._attach_module_contracts(sim.entities[1] as Dictionary)
	sim.entities[2] = {"id": 2, "team": 1, "position": Vector2(5, 0), "health": 100, "kind_of": ["MONSTER"], "category": "monster"}
	sim.entities[3] = {"id": 3, "team": 0, "position": Vector2(3, 0), "health": 100, "kind_of": ["MONSTER"], "category": "monster"}
	return sim

func _event_count(sim: RetailSliceSim, kind: String) -> int:
	var count := 0; for value in sim.events: if String((value as Dictionary).get("kind", "")) == kind: count += 1
	return count

func _check(name: String, condition: bool) -> void:
	if condition: passed += 1
	else: failed += 1; push_error("ATTACH_UPDATE_RUNTIME_FAIL " + name)

extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED := 25
var passed := 0
var failed := 0

func _initialize() -> void: call_deferred("_run")

func _run() -> void:
	var sim := _sim(_contract(true, "", 0))
	var portal := (sim.structures[1000] as Dictionary).get("dynamic_portal", {}) as Dictionary
	_check("typed_contract_attaches", not portal.is_empty())
	_check("generate_now_activates", bool(portal.get("active", false)))
	_check("ordered_topology_preserved", (portal.get("waypoints", []) as Array).size() == 6 and (portal.get("links", []) as Array).size() == 2)
	_check("bone_transform_gap_receipted", (portal.get("unsupported_semantics", []) as Array).has("model-bone-world-transforms"))
	var route := sim.request_dynamic_portal_route(1000, 1, 0, 3)
	_check("allied_infantry_admitted", bool(route.get("ok", false)))
	var points := route.get("route", []) as Array
	_check("link_order_is_exact", points.size() == 4 and int((points[1] as Dictionary).get("ordinal", -1)) == 4 and int((points[2] as Dictionary).get("ordinal", -1)) == 5)
	_check("bone_names_derive_from_authored_indices", String((points[0] as Dictionary).get("bone", "")) == "Post1" and String((points[3] as Dictionary).get("bone", "")) == "Post4")
	_check("movement_stays_explicitly_deferred", String(route.get("movement_status", "")) == "deferred-model-bone-world-transforms")
	_check("route_receipt_is_entity_state", (sim.entities[1] as Dictionary).has("dynamic_portal_route_receipt"))
	_check("route_event_emitted", _event_count(sim, "portal.route_resolved") == 1)
	_check("missing_link_refused", String(sim.request_dynamic_portal_route(1000, 1, 1, 4).get("reason", "")) == "portal-link-missing")
	var enemy := _spawn(sim, 2, Sim.ENEMY_TEAM, ["INFANTRY"])
	_check("enemy_refused_when_not_authored", String(sim.request_dynamic_portal_route(1000, 2, 0, 3).get("reason", "")) == "enemy-refused")
	enemy["team"] = Sim.PLAYER_TEAM
	enemy["kind_of"] = ["MONSTER"]
	_check("object_filter_refuses_monster", String(sim.request_dynamic_portal_route(1000, 2, 0, 3).get("reason", "")) == "object-filter-refused")
	var snapshot := sim.snapshot()
	var digest := sim.state_hash()
	var restored := _sim(_contract(true, "", 0))
	_check("snapshot_restores", restored.restore(snapshot))
	_check("snapshot_hash_round_trips", restored.state_hash() == digest)
	_check("route_receipt_restores", (restored.entities[1] as Dictionary).has("dynamic_portal_route_receipt"))
	var delayed := _sim(_contract(false, "Upgrade_PosternGate", 7000))
	_check("triggered_portal_starts_inactive", not bool(((delayed.structures[1000] as Dictionary).get("dynamic_portal", {}) as Dictionary).get("active", true)))
	(delayed.structures[1000] as Dictionary)["applied_upgrades"] = {"Upgrade_PosternGate": true}
	delayed._step_dynamic_portals()
	_check("authored_delay_schedules_seventy_ticks", int(((delayed.structures[1000] as Dictionary).get("dynamic_portal", {}) as Dictionary).get("activation_ready_tick", -1)) == 70)
	delayed.tick_index = 69
	delayed._step_dynamic_portals()
	_check("portal_waits_full_delay", not bool(((delayed.structures[1000] as Dictionary).get("dynamic_portal", {}) as Dictionary).get("active", true)))
	delayed.tick_index = 70
	delayed._step_dynamic_portals()
	_check("portal_activates_on_due_tick", bool(((delayed.structures[1000] as Dictionary).get("dynamic_portal", {}) as Dictionary).get("active", false)))
	(delayed.structures[1000] as Dictionary)["applied_upgrades"] = {"Upgrade_PosternGate": true, "Upgrade_OpenGarrison": true}
	delayed._step_dynamic_portals()
	_check("conflicting_upgrade_deactivates", not bool(((delayed.structures[1000] as Dictionary).get("dynamic_portal", {}) as Dictionary).get("active", true)))
	var opaque := _contract(true, "", 0); opaque["extraction"] = "opaque"
	_check("opaque_contract_fails_closed", (_sim(opaque).structures[1000] as Dictionary).get("dynamic_portal", {}).is_empty())
	var malformed := _contract(true, "", 0); ((malformed["fields"] as Dictionary)["Link"] as Array)[0] = {"from": 0, "via": [9], "to": 3}
	_check("out_of_bounds_topology_fails_closed", (_sim(malformed).structures[1000] as Dictionary).get("dynamic_portal", {}).is_empty())
	var unresolved := _contract(false, "Upgrade_PosternGate", 0); (unresolved["fields"] as Dictionary)["ActivationDelaySeconds"] = {"define": "PORTAL_DELAY", "expression": "PORTAL_DELAY"}
	var unresolved_sim := _sim(unresolved); (unresolved_sim.structures[1000] as Dictionary)["applied_upgrades"] = {"Upgrade_PosternGate": true}; unresolved_sim._step_dynamic_portals()
	_check("unresolved_delay_fails_closed", not bool(((unresolved_sim.structures[1000] as Dictionary).get("dynamic_portal", {}) as Dictionary).get("active", true)))
	_check("unresolved_delay_receipted", (((unresolved_sim.structures[1000] as Dictionary).get("dynamic_portal", {}) as Dictionary).get("unsupported_semantics", []) as Array).has("unresolved-expression:ActivationDelaySeconds"))
	if passed + failed != EXPECTED: failed += 1; printerr("DYNAMIC_PORTAL_RUNTIME_FAIL liveness")
	print("DYNAMIC_PORTAL_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)

func _contract(generate_now: bool, triggered_by: String, delay_ms: int) -> Dictionary:
	var fields := {"ObjectFilter": {"value": ["ANY", "+INFANTRY", "-MONSTER"]}, "BonePrefix": {"value": "Post"}, "NumberOfBones": {"value": 4}, "WayPoint": [{"index": 0, "type": "Walk"}, {"index": 1, "type": "Walk"}, {"index": 2, "type": "Walk"}, {"index": 3, "type": "Walk"}, {"index": 2, "type": "Walk"}, {"index": 1, "type": "Walk"}], "Link": [{"from": 0, "via": [4, 5], "to": 3}, {"from": 3, "via": [1, 2], "to": 0}], "GenerateNow": {"value": generate_now}, "AllowEnemies": {"value": false}, "ConflictsWith": {"value": ["Upgrade_OpenGarrison"]}, "CustomAnimAndDuration": {"animState": "UPGRADE_POSTERN_GATE", "animTimeMilliseconds": 0}}
	if triggered_by != "": fields["TriggeredBy"] = {"value": triggered_by}
	if delay_ms > 0: fields["ActivationDelaySeconds"] = {"milliseconds": delay_ms, "seconds": float(delay_ms) / 1000.0}
	return {"module": "DynamicPortalBehaviour", "runtimeStatus": "deferred", "extraction": "typed", "tag": "PosternGatePortal", "line": 10, "fields": fields}

func _sim(contract: Dictionary) -> RetailSliceSim:
	var rule := {"horde_id": Sim.SOLDIER_HORDE_ID, "category": "infantry", "speed": 1.0, "speed_source": 10.0, "acceleration": 1.0, "acceleration_source": 10.0, "turn_rate_degrees_per_second": 180.0, "braking": 1.0, "braking_source": 10.0, "attack_range": 0.1, "attack_range_source": 1.0, "minimum_attack_range": 0.0, "minimum_attack_range_source": 0.0, "vision_range": 1.0, "vision_range_source": 10.0, "delay_between_shots_ms": 1000.0, "pre_attack_delay_ms": 0.0, "firing_duration_ms": 0.0, "attack_period_ticks": 10, "pre_attack_ticks": 0, "firing_duration_ticks": 0, "member_damage": 1, "member_health": 100, "member_count": 1, "formation_positions": [Vector3.ZERO], "provenance": {}}
	var rules := {}; for id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]: rules[id] = rule
	var sim: RetailSliceSim = Sim.new(); sim.setup({}, {"unit_rules": rules}); sim.ai_enabled = false; sim.base_loop_enabled = false; sim.entities.clear(); sim.structures.clear()
	sim.register_structure_module_contracts("FixturePortal", [contract])
	sim.structures[1000] = {"id": 1000, "team": Sim.PLAYER_TEAM, "source_object_id": "FixturePortal", "structure_kind": "wall", "position": Vector2.ZERO, "health": 100, "max_health": 100, "applied_upgrades": {}}
	sim._attach_structure_module_contracts(sim.structures[1000] as Dictionary)
	_spawn(sim, 1, Sim.PLAYER_TEAM, ["INFANTRY"])
	return sim

func _spawn(sim: RetailSliceSim, id: int, team: int, kind_of: Array) -> Dictionary:
	var row := {"id": id, "team": team, "position": Vector2.ZERO, "health": 100, "member_health": [100], "kind_of": kind_of, "category": "infantry"}; sim.entities[id] = row; return row

func _event_count(sim: RetailSliceSim, kind: String) -> int:
	var count := 0; for value in sim.events: if String((value as Dictionary).get("kind", "")) == kind: count += 1
	return count

func _check(name: String, condition: bool) -> void:
	if condition: passed += 1
	else: failed += 1; push_error("DYNAMIC_PORTAL_RUNTIME_FAIL " + name)

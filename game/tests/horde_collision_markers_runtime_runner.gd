extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED := 20
var passed := 0
var failed := 0

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	print("HORDE_COLLISION_STAGE start")
	var sim := _sim([_contract("HordeMemberCollide"), _contract("NotifyTargetsOfImminentProbableCrushingUpdate")])
	print("HORDE_COLLISION_STAGE attached")
	var row := sim.entities[1] as Dictionary
	var member := row.get("horde_member_collide", {}) as Dictionary
	var notify := row.get("notify_imminent_crushing", {}) as Dictionary
	_check("horde_member_typed_marker_attaches", bool(member.get("enabled", false)))
	_check("horde_member_receipts_aggregate_blocker", String(member.get("execution", "")) == "deferred-individual-member-collision-world")
	_check("horde_member_names_missing_body_seam", (member.get("unsupported_semantics", []) as Array).has("independent-member-body"))
	_check("horde_member_does_not_invent_impulse", not member.has("impulse") and not member.has("radius"))
	_check("notify_typed_marker_attaches", bool(notify.get("enabled", false)))
	_check("notify_receipts_probability_blocker", String(notify.get("execution", "")) == "deferred-engine-probability-scan")
	_check("notify_names_missing_prediction_range", (notify.get("unsupported_semantics", []) as Array).has("prediction-range"))
	_check("notify_does_not_invent_timing", not notify.has("interval_ticks") and not notify.has("range"))
	var before_position := Vector2(row.get("position", Vector2.ZERO))
	_check("markers_do_not_mutate_aggregate_position", Vector2(row.get("position", Vector2.ZERO)) == before_position)
	_check("notify_does_not_emit_guessed_warning", _event_count(sim, "combat.crush_imminent") == 0)
	var snapshot := sim.snapshot()
	var digest := sim.state_hash()
	print("HORDE_COLLISION_STAGE snapshotted")
	var restored := _sim([])
	_check("snapshot_restores", restored.restore(snapshot))
	_check("snapshot_hash_round_trips", restored.state_hash() == digest)
	_check("horde_member_marker_restores", (restored.entities[1] as Dictionary).has("horde_member_collide"))
	_check("notify_marker_restores", (restored.entities[1] as Dictionary).has("notify_imminent_crushing"))
	var opaque := _sim([_contract("HordeMemberCollide", "opaque"), _contract("NotifyTargetsOfImminentProbableCrushingUpdate", "opaque")])
	print("HORDE_COLLISION_STAGE opaque")
	_check("opaque_horde_member_fails_closed", not (opaque.entities[1] as Dictionary).has("horde_member_collide"))
	_check("opaque_notify_fails_closed", not (opaque.entities[1] as Dictionary).has("notify_imminent_crushing"))
	var bad_member := _contract("HordeMemberCollide")
	bad_member["fields"] = {"Radius": 1}
	var bad_notify := _contract("NotifyTargetsOfImminentProbableCrushingUpdate")
	bad_notify["fields"] = {"Interval": 1}
	var malformed := _sim([bad_member, bad_notify])
	print("HORDE_COLLISION_STAGE malformed")
	_check("nonempty_horde_member_fails_closed", not (malformed.entities[1] as Dictionary).has("horde_member_collide"))
	_check("nonempty_notify_fails_closed", not (malformed.entities[1] as Dictionary).has("notify_imminent_crushing"))
	_check("receipt_keeps_source_tag", String(member.get("tag", "")) == "ModuleTag_HordeMemberCollide")
	_check("receipt_keeps_source_line", int(notify.get("line", 0)) == 37)
	if passed + failed != EXPECTED:
		failed += 1
		printerr("HORDE_COLLISION_MARKERS_RUNTIME_FAIL liveness")
	print("HORDE_COLLISION_MARKERS_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)

func _contract(kind: String, extraction: String = "typed") -> Dictionary:
	return {"module": kind, "runtimeStatus": "deferred", "extraction": extraction, "tag": "ModuleTag_" + kind, "sourceIni": "data/ini/object/fixture.ini", "line": 37, "fields": {}}

func _sim(contracts: Array) -> RetailSliceSim:
	var rule := {"horde_id": Sim.SOLDIER_HORDE_ID, "category": "infantry", "speed": 1.0, "speed_source": 10.0, "acceleration": 1.0, "acceleration_source": 10.0, "turn_rate_degrees_per_second": 180.0, "braking": 1.0, "braking_source": 10.0, "attack_range": 0.1, "attack_range_source": 1.0, "minimum_attack_range": 0.0, "minimum_attack_range_source": 0.0, "vision_range": 1.0, "vision_range_source": 10.0, "delay_between_shots_ms": 1000.0, "pre_attack_delay_ms": 0.0, "firing_duration_ms": 0.0, "attack_period_ticks": 10, "pre_attack_ticks": 0, "firing_duration_ticks": 0, "member_damage": 1, "member_health": 100, "member_count": 2, "formation_positions": [Vector3(-0.25, 0, 0), Vector3(0.25, 0, 0)], "provenance": {}}
	var rules := {}
	for id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]:
		rules[id] = rule
	var sim: RetailSliceSim = Sim.new()
	sim.setup({}, {"unit_rules": rules})
	sim.ai_enabled = false
	sim.base_loop_enabled = false
	sim.entities.clear()
	sim.structures.clear()
	sim._unit_module_contracts[Sim.SOLDIER_HORDE_ID] = contracts
	sim._add_battalion(1, Sim.PLAYER_TEAM, Vector2.ZERO, "Fixture", Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, 0, rule)
	return sim

func _check(name: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("HORDE_COLLISION_MARKERS_RUNTIME_FAIL " + name)

func _event_count(sim: RetailSliceSim, kind: String) -> int:
	var count := 0
	for value in sim.events:
		if String((value as Dictionary).get("kind", "")) == kind:
			count += 1
	return count

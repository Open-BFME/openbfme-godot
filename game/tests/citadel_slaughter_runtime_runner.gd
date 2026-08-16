extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED := 25
var passed := 0
var failed := 0

func _initialize() -> void: call_deferred("_run")

func _run() -> void:
	var sim := _sim(); var citadel := sim.structures[50] as Dictionary
	sim._attach_citadel_slaughter_contract(citadel, _contract(2.0, true, "TheDroppedRing"))
	_check("typed_attaches", citadel.has("citadel_slaughter"))
	_check("retail_macro_expands", ((citadel["citadel_slaughter"] as Dictionary).get("passenger_filter", []) as Array) == ["ANY", "+INFANTRY", "+CAVALRY", "-HERO", "-DOZER", "-SUMMONED"])
	_check("coordinates_exact", Vector2((citadel["citadel_slaughter"] as Dictionary).get("entry_position_source", Vector2.ZERO)) == Vector2(1, 2) and Vector2((citadel["citadel_slaughter"] as Dictionary).get("exit_offset_source", Vector2.ZERO)) == Vector2(40, 50))
	_spawn(sim, 1, 0, ["INFANTRY"], 100)
	var before := sim.resources_for_team(0); var slaughter := sim.enter_citadel_slaughter(50, 1)
	_check("same_owner_admitted", bool(slaughter.get("ok", false)))
	_check("two_hundred_percent_cashback", int(slaughter.get("cashback", 0)) == 200 and sim.resources_for_team(0) == before + 200)
	_check("slaughter_removes_entity", not sim.entities.has(1) and not sim.entity_container.has(1))
	_check("slaughter_counter", int((citadel["citadel_slaughter"] as Dictionary).get("slaughter_count", 0)) == 1)
	_spawn(sim, 2, 0, ["HERO", "INFANTRY"], 100); _check("hero_filter_refused", String(sim.enter_citadel_slaughter(50, 2).get("reason", "")) == "passenger-filter-refused")
	_spawn(sim, 3, 1, ["INFANTRY"], 100); _check("enemy_refused", String(sim.enter_citadel_slaughter(50, 3).get("reason", "")) == "ownership-refused")
	_spawn(sim, 4, 0, ["INFANTRY"], 100); (sim.entities[4] as Dictionary)["object_status"] = {"HOLDING_THE_RING": true}
	var ring := sim.enter_citadel_slaughter(50, 4)
	_check("ring_entry_detected", String(ring.get("result", "")) == "ring-entry")
	_check("paired_team_upgrade", bool((sim.team_upgrades[0] as Dictionary).get("Upgrade_RingHero", false)))
	_check("paired_fortress_upgrade", (citadel.get("completed_upgrades", []) as Array).has("Upgrade_FortressRingHero"))
	_check("carrier_survives_and_ejects", sim.entities.has(4) and not sim.entity_container.has(4) and Vector2((sim.entities[4] as Dictionary).get("position", Vector2.ZERO)) == Vector2(40, 50))
	_check("fx_receipted_and_evented", ((citadel["citadel_slaughter"] as Dictionary).get("unsupported_semantics", []) as Array).has("ring_fx_requires_presentation_binding") and String((sim.events[-1] as Dictionary).get("fx", "")) == "FX_OneRingFlare")
	var snapshot := sim.snapshot(); var hash := sim.state_hash(); var restored := _sim()
	_check("snapshot_restores", restored.restore(snapshot)); _check("hash_round_trips", restored.state_hash() == hash)
	_check("economy_state_round_trips", restored.resources_for_team(0) == sim.resources_for_team(0) and int(((restored.structures[50] as Dictionary)["citadel_slaughter"] as Dictionary).get("cashback_total", 0)) == 200)

	var low := _sim(); low._attach_citadel_slaughter_contract(low.structures[50] as Dictionary, _contract(0.2, true, "TheDroppedRing")); _spawn(low, 5, 0, ["CAVALRY"], 250); var low_before := low.resources_for_team(0); var low_result := low.enter_citadel_slaughter(50, 5)
	_check("twenty_percent_cashback", int(low_result.get("cashback", 0)) == 50 and low.resources_for_team(0) == low_before + 50)

	var shard := _sim(); shard._attach_citadel_slaughter_contract(shard.structures[50] as Dictionary, _contract(2.0, false, "PalantirShard")); _spawn(shard, 6, 0, ["INFANTRY", "PalantirShard"], 10); (shard.entities[6] as Dictionary)["object_status"] = {"HOLDING_THE_RING": true}; var shard_result := shard.enter_citadel_slaughter(50, 6)
	_check("palantir_no_upgrade_variant", String(shard_result.get("result", "")) == "ring-entry" and (shard_result.get("upgrades", []) as Array).is_empty())
	_check("palantir_destroy_filter", bool(shard_result.get("destroyed", false)) and not shard.entities.has(6))
	_check("palantir_grants_nothing", (shard.team_upgrades[0] as Dictionary).is_empty() and ((shard.structures[50] as Dictionary).get("completed_upgrades", []) as Array).is_empty())

	var full := _sim(); full._attach_citadel_slaughter_contract(full.structures[50] as Dictionary, _contract(2.0, true, "TheDroppedRing", 1)); _spawn(full, 7, 0, ["INFANTRY"], 100); _spawn(full, 8, 0, ["INFANTRY"], 100); full.containment[50] = [7]; full.entity_container[7] = 50
	_check("capacity_refused", String(full.enter_citadel_slaughter(50, 8).get("reason", "")) == "capacity-full")
	(full.structures[50] as Dictionary)["health"] = 0; _check("dead_citadel_refused", String(full.enter_citadel_slaughter(50, 8).get("reason", "")) == "citadel-dead"); full._resolve_citadel_slaughter_death(50, full.structures[50] as Dictionary); _check("death_ejects_existing_passenger", not full.entity_container.has(7) and Vector2((full.entities[7] as Dictionary).get("position", Vector2.ZERO)) == Vector2(40, 50))
	var opaque := _contract(2.0, true, "TheDroppedRing"); opaque["extraction"] = "opaque"; var other := _sim(); other._attach_citadel_slaughter_contract(other.structures[50] as Dictionary, opaque)
	_check("opaque_fails_closed", not (other.structures[50] as Dictionary).has("citadel_slaughter"))
	print("CITADEL_SLAUGHTER_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed]); quit(0 if failed == 0 and passed == EXPECTED else 1)

func _contract(ratio: float, paired: bool, destroy_kind: String, capacity: int = 99) -> Dictionary:
	var fields := {"PassengerFilter": {"value": ["GENERIC_FACTION_SLAUGHTERABLE"]}, "ObjectStatusOfContained": {"value": ["UNSELECTABLE", "ENCLOSED"]}, "CashBackPercent": {"ratio": ratio, "percent": ratio * 100.0}, "ContainMax": {"value": capacity}, "AllowEnemiesInside": {"value": false}, "AllowAlliesInside": {"value": false}, "AllowNeutralInside": {"value": false}, "AllowOwnPlayerInsideOverride": {"value": true}, "EnterSound": {"value": "GUI_RingReturned"}, "EntryOffset": {"value": {"x": 10.0, "y": 20.0, "z": 30.0}}, "EntryPosition": {"value": {"x": 1.0, "y": 2.0, "z": 3.0}}, "ExitOffset": {"value": {"x": 40.0, "y": 50.0, "z": 60.0}}, "StatusForRingEntry": {"value": "HOLDING_THE_RING"}, "ObjectToDestroyForRingEntry": {"value": ["NONE", "+" + destroy_kind]}}
	if paired: fields["UpgradeForRingEntry"] = {"value": ["Upgrade_RingHero", "Upgrade_FortressRingHero"]}; fields["FXForRingEntry"] = {"value": "FX_OneRingFlare"}
	return {"module": "CitadelSlaughterHordeContain", "extraction": "typed", "fields": fields}

func _sim() -> RetailSliceSim:
	var unit_rules := {}; for object_id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]: unit_rules[object_id] = _rule()
	var sim: RetailSliceSim = Sim.new(); sim.setup({}, {"unit_rules": unit_rules, "source_map_transform_scale": 1.0}); sim.ai_enabled = false; sim.base_loop_enabled = false; sim.entities.clear(); sim.structures.clear(); sim.structures[50] = {"id": 50, "team": 0, "kind": "structure", "health": 1000, "maximum_health": 1000, "position": Vector2.ZERO, "completed_upgrades": []}; return sim
func _spawn(sim: RetailSliceSim, id: int, team: int, kinds: Array, cost: int) -> void: sim.entities[id] = {"id": id, "team": team, "unit_type": "Fixture%d" % id, "health": 100, "maximum_health": 100, "member_health": [100], "position": Vector2.ZERO, "kind_of": kinds, "category": String(kinds[0]), "build_cost": cost, "object_status": {}}
func _rule() -> Dictionary: return {"horde_id": Sim.SOLDIER_HORDE_ID, "category": "infantry", "speed": 1.0, "speed_source": 10.0, "acceleration": 1.0, "acceleration_source": 10.0, "turn_rate_degrees_per_second": 180.0, "braking": 1.0, "braking_source": 10.0, "attack_range": 1.0, "attack_range_source": 1.0, "minimum_attack_range": 0.0, "minimum_attack_range_source": 0.0, "vision_range": 10.0, "vision_range_source": 10.0, "delay_between_shots_ms": 1000.0, "pre_attack_delay_ms": 0.0, "firing_duration_ms": 0.0, "attack_period_ticks": 10, "pre_attack_ticks": 0, "firing_duration_ticks": 0, "member_damage": 1, "member_health": 100, "member_count": 1, "formation_positions": [Vector3.ZERO], "provenance": {}}
func _check(label: String, condition: bool) -> void:
	if condition: passed += 1
	else: failed += 1; push_error("CITADEL_SLAUGHTER_RUNTIME_FAIL " + label)

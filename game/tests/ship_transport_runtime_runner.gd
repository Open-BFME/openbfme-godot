extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED_CHECKS := 38

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var sim := _make_sim()
	sim.register_structure_module_contracts("ElvenTransportShip", [_transport_contract(false, true), _slow_death_contract()])
	_add_ship(sim, 500, "ElvenTransportShip")
	_spawn(sim, 10, Sim.PLAYER_TEAM, "infantry", ["INFANTRY"])
	_spawn(sim, 11, Sim.PLAYER_TEAM, "siege", ["SIEGE"])
	_spawn(sim, 90, Sim.ENEMY_TEAM, "infantry", ["INFANTRY"])

	var load := sim.load_transport_entity(500, 10)
	_check("typed_transport_accepts_own_filtered_passenger", bool(load.get("ok", false)))
	var first_enter := _last_event(sim, "transport.enter")
	_check("bfme2_elven_entry_preserves_specific_then_generic_candidates", first_enter.get("voice_candidates", []) == ["BFME2ElfEnterSpecificA", "BFME2ElfEnterSpecificB", "BFME2TransportEnterGeneric"])
	_check("accepted_entry_event_carries_exact_passenger_and_carrier_identity", String(first_enter.get("passenger_object_id", "")) == "BFME2Passenger" and String(first_enter.get("carrier_object_id", "")) == "ElvenTransportShip")
	_check("passenger_uses_matching_authored_bone", String(load.get("bone", "")) == "B_CARGO0")
	var passenger := sim.entities[10] as Dictionary
	_check("contained_statuses_are_applied", bool((passenger.get("object_status", {}) as Dictionary).get("UNSELECTABLE", false)) and bool((passenger.get("object_status", {}) as Dictionary).get("ENCLOSED", false)))
	_check("force_orientation_and_position_follow_carrier_on_entry", Vector2(passenger.get("facing", Vector2.ZERO)).is_equal_approx(Vector2(0.0, 1.0)) and Vector2(passenger.get("position", Vector2.ZERO)).is_equal_approx(Vector2(4.0, 5.0)))
	_check("enter_fade_timer_is_deterministic", int(passenger.get("transport_fade_until_tick", -1)) == sim.tick_index + 3)
	var events_after_accept := sim.events.size()
	_check("filter_refuses_siege", String(sim.load_transport_entity(500, 11).get("reason", "")) == "passenger-filter-refused")
	_check("relationship_refuses_enemy", String(sim.load_transport_entity(500, 90).get("reason", "")).begins_with("relationship-refused"))
	_check("rejected_and_enemy_entry_attempts_are_audio_silent", sim.events.size() == events_after_accept)
	_spawn(sim, 12, Sim.PLAYER_TEAM, "infantry", ["INFANTRY"])
	_check("second_slot_loads", bool(sim.load_transport_entity(500, 12).get("ok", false)))
	_spawn(sim, 13, Sim.PLAYER_TEAM, "infantry", ["INFANTRY"])
	var events_before_full := sim.events.size()
	_check("capacity_is_enforced", String(sim.load_transport_entity(500, 13).get("reason", "")) == "capacity-full")
	_check("full_transport_attempt_is_audio_silent", sim.events.size() == events_before_full)
	var before := int((sim.entities[10] as Dictionary).get("health", 0))
	sim._apply_structure_damage(90, 500, 40)
	_check("damage_percent_propagates_to_passengers", int((sim.entities[10] as Dictionary).get("health", 0)) == before - 20)

	var exit := sim.request_transport_exit(10)
	_check("exit_delay_is_scheduled", int(exit.get("exit_tick", -1)) == sim.tick_index + 2)
	var snapshot := sim.snapshot()
	var state_hash := sim.state_hash()
	var restored := _make_sim()
	_check("transport_state_snapshot_restores", restored.restore(snapshot))
	_check("transport_state_hash_round_trips", restored.state_hash() == state_hash)
	sim.tick()
	restored.tick()
	_check("passenger_stays_contained_before_exit_delay", sim.entity_container.has(10))
	sim.tick()
	restored.tick()
	_check("passenger_exits_on_authored_delay", not sim.entity_container.has(10))
	_check("contained_statuses_restore_after_exit", not bool(((sim.entities[10] as Dictionary).get("object_status", {}) as Dictionary).get("ENCLOSED", false)))
	_check("exit_fade_timer_is_applied", int((sim.entities[10] as Dictionary).get("transport_fade_until_tick", -1)) == sim.tick_index + 1)
	_check("restored_exit_continues_deterministically", restored.state_hash() == sim.state_hash())

	# The remaining passenger is ejected immediately when the matching slow
	# death begins; the hull then waits, sinks, and is erased on authored time.
	sim._apply_structure_damage(90, 500, 5000)
	var ship := sim.structures[500] as Dictionary
	_check("matching_slow_death_enters_sinking_and_ejects", String(ship.get("ship_death_phase", "")) == "sinking" and not sim.entity_container.has(12))
	_check("initial_sink_sound_is_emitted_from_contract", _has_event_sound(sim, "ship.sinking", "GoodShipTransportSinkMS"))
	var height_before := float(ship.get("height_source", 0.0))
	sim.tick()
	_check("sink_delay_holds_height", is_equal_approx(float((sim.structures[500] as Dictionary).get("height_source", 0.0)), height_before))
	sim.tick()
	_check("sink_rate_advances_after_delay", float((sim.structures[500] as Dictionary).get("height_source", 0.0)) < height_before)
	sim.advance(3)
	_check("destruction_delay_erases_ship", not sim.structures.has(500))

	var mux := _make_sim()
	mux.register_structure_module_contracts("FixtureTransportShip", [_slow_death_contract()])
	_add_ship(mux, 600)
	mux._attach_structure_module_contracts(mux.structures[600] as Dictionary)
	_check("excluded_death_type_does_not_sink", not mux._begin_ship_slow_death(600, mux.structures[600] as Dictionary, "FADED"))

	var opaque := _make_sim()
	var opaque_transport := _transport_contract(false, true)
	opaque_transport["extraction"] = "opaque"
	opaque.register_structure_module_contracts("FixtureTransportShip", [opaque_transport])
	_add_ship(opaque, 700)
	_spawn(opaque, 20, Sim.PLAYER_TEAM, "infantry", ["INFANTRY"])
	_check("opaque_transport_fails_closed", String(opaque.load_transport_entity(700, 20).get("reason", "")) == "typed-horde-transport-contract-missing")

	var killer := _make_sim()
	killer.register_structure_module_contracts("FixtureTransportShip", [_transport_contract(false, false), _slow_death_contract()])
	_add_ship(killer, 750)
	_spawn(killer, 21, Sim.PLAYER_TEAM, "infantry", ["INFANTRY"])
	var kill_fields := ((_structure_contract(killer, 750, "HordeTransportContain").get("fields", {}) as Dictionary))
	kill_fields["KillPassengersOnDeath"] = {"value": true}
	(killer.structures[750] as Dictionary).erase("horde_transport")
	(killer.structures[750] as Dictionary).erase("structure_module_contracts_attached")
	# This fixture deliberately replaces the registered contract after the first
	# lazy attach. Clear both idempotence receipts so the replacement is actually
	# re-read; shipping structures never rewrite their sealed registry this way.
	(killer.structures[750] as Dictionary).erase("structure_module_contracts_attempted")
	killer._structure_module_contracts["FixtureTransportShip"] = [{"module": "HordeTransportContain", "fields": kill_fields, "extraction": "typed", "tag": "Kill", "line": 1}, _slow_death_contract()]
	_check("kill_policy_passenger_loads", bool(killer.load_transport_entity(750, 21).get("ok", false)))
	killer._apply_structure_damage(0, 750, 1000)
	_check("kill_passengers_on_death_is_consumed", int((killer.entities[21] as Dictionary).get("health", 1)) == 0 and not killer.entity_container.has(21))

	var receipt := _make_sim()
	receipt.register_structure_module_contracts("FixtureTransportShip", [_transport_contract(true, false)])
	_add_ship(receipt, 800)
	receipt._attach_structure_module_contracts(receipt.structures[800] as Dictionary)
	var unsupported := (((receipt.structures[800] as Dictionary).get("horde_transport", {}) as Dictionary).get("unsupported_semantics", []) as Array)
	_check("unsupported_geometry_and_presentation_are_receipted", unsupported.has("multiple_exit_path_geometry") and unsupported.has("initial_payload_requires_object_factory") and unsupported.has("show_pips_requires_hud_binding") and unsupported.has("fade_timers_require_presentation_binding") and unsupported.has("passenger_bone_requires_model_attachment"))

	_run_edition_snapshot_oracle("bfme2", "ElvenTransportShip", "BFME2Passenger", ["BFME2ElfEnterSpecificA", "BFME2ElfEnterSpecificB", "BFME2TransportEnterGeneric"])
	_run_edition_snapshot_oracle("rotwk", "EvilMenTransportShip", "RotWKPassenger", ["RotWKEvilEnterSpecific", "RotWKTransportEnterGeneric"])

	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("SHIP_TRANSPORT_RUNTIME_FAIL liveness ran=%d expected=%d" % [ran, EXPECTED_CHECKS])
	print("SHIP_TRANSPORT_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _transport_contract(with_payload: bool, eject: bool) -> Dictionary:
	var fields := {
		"ObjectStatusOfContained": {"value": ["UNSELECTABLE", "ENCLOSED"]},
		"Slots": {"value": 2}, "EnterSound": {"value": "GarrisonEnter"}, "ExitSound": {"value": "GarrisonExit"},
		"DamagePercentToUnits": {"percent": 50.0, "ratio": 0.5},
		"PassengerFilter": {"value": ["ANY", "+INFANTRY", "-SIEGE"]},
		"AllowOwnPlayerInsideOverride": {"value": true}, "AllowAlliesInside": {"value": false},
		"AllowEnemiesInside": {"value": false}, "AllowNeutralInside": {"value": false},
		"ExitDelay": {"milliseconds": 200.0}, "NumberOfExitPaths": {"value": 2},
		"ForceOrientationContainer": {"value": true}, "ShowPips": {"value": true},
		"KillPassengersOnDeath": {"value": false}, "EjectPassengersOnDeath": {"value": eject},
		"FadeFilter": {"value": ["ALL"]}, "FadePassengerOnEnter": {"value": true},
		"EnterFadeTime": {"milliseconds": 300.0}, "FadePassengerOnExit": {"value": true},
		"ExitFadeTime": {"milliseconds": 100.0},
		"PassengerBonePrefix": [{"passengerBone": "B_CARGO0", "kindOf": "INFANTRY"}],
	}
	if with_payload:
		fields["InitialPayload"] = {"objectId": "InternalShipGoodArcher", "count": 2}
	return {"module": "HordeTransportContain", "fields": fields, "extraction": "typed", "runtimeStatus": "deferred", "tag": "ModuleTag_Transport", "line": 10}


func _slow_death_contract() -> Dictionary:
	return {"module": "ShipSlowDeathBehavior", "fields": {
		"deathTypes": "ALL", "includedDeathTypes": [], "excludedDeathTypes": ["FADED"],
		"SinkDelay": {"milliseconds": 200.0}, "SinkRate": {"value": 10.0},
		"DestructionDelay": {"milliseconds": 500.0},
		"Sound": {"phase": "INITIAL", "event": "GoodShipTransportSinkMS"},
	}, "extraction": "typed", "runtimeStatus": "deferred", "tag": "ModuleTag_Sink", "line": 30}


func _structure_contract(sim: RetailSliceSim, id: int, module_name: String) -> Dictionary:
	sim._attach_structure_module_contracts(sim.structures[id] as Dictionary)
	for contract_value in (sim.structures[id] as Dictionary).get("module_contracts", []) as Array:
		if String((contract_value as Dictionary).get("module", "")) == module_name:
			return (contract_value as Dictionary).duplicate(true)
	return {}


func _has_event_sound(sim: RetailSliceSim, kind: String, sound: String) -> bool:
	for event_value in sim.events:
		var event := event_value as Dictionary
		if String(event.get("kind", "")) == kind and String(event.get("sound", "")) == sound:
			return true
	return false


func _add_ship(sim: RetailSliceSim, id: int, source_object_id: String = "FixtureTransportShip") -> void:
	sim.structures[id] = {"id": id, "team": Sim.PLAYER_TEAM, "source_object_id": source_object_id, "structure_kind": "ship", "kind": "ship", "position": Vector2(4, 5), "facing": Vector2(0, 1), "height_source": 3.0, "health": 100, "maximum_health": 100, "damage_remainders": {}, "queue": [], "upgrade_queue": []}


func _spawn(sim: RetailSliceSim, id: int, team: int, category: String, kind_of: Array, object_id: String = "BFME2Passenger") -> void:
	sim._add_battalion(id, team, Vector2.ZERO, "Fixture", Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, 0, _unit_rule())
	var row := sim.entities[id] as Dictionary
	row["category"] = category
	row["kind_of"] = kind_of
	row["object_id"] = object_id


func _make_sim() -> RetailSliceSim:
	var rules := {}
	for object_id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]:
		rules[object_id] = _unit_rule().duplicate(true)
	var sim: RetailSliceSim = Sim.new()
	sim.setup({}, {"unit_rules": rules, "faction_manifest": {"structure_armor": {"ship": {"damage_scalar": 1.0, "scalars": {"default": 1.0}}}}})
	sim.ai_enabled = false
	sim.base_loop_enabled = false
	sim.entities.clear()
	sim.structures.clear()
	sim._structure_armor["ship"] = {"damage_scalar": 1.0, "scalars": {"default": 1.0}}
	sim._rules["playable_unit_runtimes"] = {
		"BFME2Passenger": _passenger_audio_document("BFME2Passenger", {
			"VoiceEnterUnitElvenTransportShip": ["BFME2ElfEnterSpecificA", "BFME2ElfEnterSpecificB"],
			"VoiceEnterUnitTransportShip": ["BFME2TransportEnterGeneric"],
		}),
		"RotWKPassenger": _passenger_audio_document("RotWKPassenger", {
			"VoiceEnterUnitEvilMenTransportShip": ["RotWKEvilEnterSpecific"],
			"VoiceEnterUnitTransportShip": ["RotWKTransportEnterGeneric"],
		}),
	}
	return sim


func _passenger_audio_document(object_id: String, fields: Dictionary) -> Dictionary:
	var owner := {}
	for field_value in fields.keys():
		var rows: Array = []
		for event_id in fields[field_value] as Array:
			rows.append({"id": String(event_id), "sourceIni": "data/ini/object/fixture.ini", "line": rows.size() + 1})
		owner[String(field_value)] = rows
	return {"objectId": object_id, "registration": {"composition": {"containerObjectId": object_id, "primaryMemberObjectId": object_id}, "audioRoutes": {"container": owner}}}


func _run_edition_snapshot_oracle(edition: String, carrier_object_id: String, passenger_object_id: String, expected_candidates: Array) -> void:
	var original := _make_sim()
	original.register_structure_module_contracts(carrier_object_id, [_transport_contract(false, true)])
	_add_ship(original, 900, carrier_object_id)
	_spawn(original, 901, Sim.PLAYER_TEAM, "infantry", ["INFANTRY"], passenger_object_id)
	original._attach_structure_module_contracts(original.structures[900] as Dictionary)
	var restored := _make_sim()
	_check("%s_pre_entry_snapshot_restores" % edition, restored.restore(original.snapshot()))
	var first := original.load_transport_entity(900, 901)
	var second := restored.load_transport_entity(900, 901)
	var original_event := _last_event(original, "transport.enter")
	var restored_event := _last_event(restored, "transport.enter")
	_check("%s_snapshot_entry_acceptance_matches" % edition, first == second and bool(first.get("ok", false)))
	# setup emits the deterministic initial music event at sequence 1; accepted
	# containment must follow it at sequence 2 on both the live and restored sim.
	var events_match: bool = original_event == restored_event and original_event.get("voice_candidates", []) == expected_candidates and int(original_event.get("sequence", 0)) == 2
	_check("%s_snapshot_entry_event_order_and_candidates_match" % edition, events_match)


func _last_event(sim: RetailSliceSim, kind: String) -> Dictionary:
	for index in range(sim.events.size() - 1, -1, -1):
		var event := sim.events[index] as Dictionary
		if String(event.get("kind", "")) == kind:
			return event
	return {}


func _unit_rule() -> Dictionary:
	return {"horde_id": Sim.SOLDIER_HORDE_ID, "category": "infantry", "speed": 1.0, "speed_source": 10.0, "acceleration": 1.0, "acceleration_source": 10.0, "turn_rate_degrees_per_second": 180.0, "braking": 1.0, "braking_source": 10.0, "attack_range": 0.1, "attack_range_source": 1.0, "minimum_attack_range": 0.0, "minimum_attack_range_source": 0.0, "vision_range": 1.0, "vision_range_source": 10.0, "delay_between_shots_ms": 1000.0, "pre_attack_delay_ms": 0.0, "firing_duration_ms": 0.0, "attack_period_ticks": 10, "pre_attack_ticks": 0, "firing_duration_ticks": 0, "member_damage": 1, "member_health": 100, "member_count": 1, "formation_positions": [Vector3.ZERO], "provenance": {}}


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("SHIP_TRANSPORT_RUNTIME_FAIL %s" % label)

extends SceneTree
## ShipSlowDeathBehavior on the values RotWK actually authors.
##
## Every fixture below carries the numbers the compiled RotWK ship catalog
## emits, not invented ones. All twelve sinking ships author the same triple —
## SinkDelay 0, SinkRate 12.0, DestructionDelay 10000 — so the hull starts
## settling on the tick after the kill and is erased a full ten seconds later.
## The three troop transports additionally exclude FADED.
##
## EvilFireShip is the one effective SHIP object that neither authors nor
## inherits the module (evilship.ini has no ShipSlowDeathBehavior block). It
## must die as a razed hull with no sink phase; fabricating one for uniformity
## is exactly the invention this runner exists to refuse.

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const EXPECTED_CHECKS := 19
const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")

# data/ini/object/goodfaction/units/elven/elvenship.ini:637-642
const AUTHORED_SINK_DELAY_MS := 0.0
const AUTHORED_SINK_RATE := 12.0
const AUTHORED_DESTRUCTION_DELAY_MS := 10000.0
# TICK_SECONDS is 0.1, so the authored destruction delay is 100 sim ticks and
# the hull loses 1.2 source units of height per tick once settling starts.
const DESTRUCTION_DELAY_TICKS := 100
const SINK_PER_TICK := 1.2

var _watchdog := RunnerWatchdogScript.new()
var passed := 0
var failed := 0


func _initialize() -> void:
	_watchdog.start(self, "SHIP_SINK_DEATH")
	call_deferred("_run")


func _run() -> void:
	_test_authored_contract_projects()
	_test_authored_timeline_erases_the_hull()
	_test_transport_excludes_faded()
	_test_fire_ship_has_no_sink_to_inherit()
	_test_mid_sink_snapshot_finishes_on_the_same_tick()
	_finish()


func _test_authored_contract_projects() -> void:
	var sim := _sim()
	sim.register_structure_module_contracts("ElvenBattleShip", [_sink_contract("GoodShipBattleSinkMS", [])])
	_add_ship(sim, 100, "ElvenBattleShip")
	sim._attach_structure_module_contracts(sim.structures[100] as Dictionary)
	var policy := (sim.structures[100] as Dictionary).get("ship_slow_death", {}) as Dictionary
	_check("authored_sink_delay_is_zero_ticks", int(policy.get("sink_delay_ticks", -1)) == 0,
		"ticks=%s" % policy.get("sink_delay_ticks", null))
	_check("authored_destruction_delay_is_one_hundred_ticks",
		int(policy.get("destruction_delay_ticks", -1)) == DESTRUCTION_DELAY_TICKS,
		"ticks=%s" % policy.get("destruction_delay_ticks", null))
	_check("authored_sink_rate_is_carried_in_source_units",
		is_equal_approx(float(policy.get("sink_rate_source_per_second", 0.0)), AUTHORED_SINK_RATE),
		"rate=%s" % policy.get("sink_rate_source_per_second", null))
	_check("initial_phase_sound_is_the_authored_event",
		String(policy.get("sound_event", "")) == "GoodShipBattleSinkMS",
		"event=%s" % policy.get("sound_event", ""))


func _test_authored_timeline_erases_the_hull() -> void:
	var sim := _sim()
	sim.register_structure_module_contracts("ElvenBattleShip", [_sink_contract("GoodShipBattleSinkMS", [])])
	_add_ship(sim, 100, "ElvenBattleShip")
	var start_height := float((sim.structures[100] as Dictionary).get("height_source", 0.0))
	sim._apply_structure_damage(0, 100, 5000)
	var ship := sim.structures[100] as Dictionary
	_check("lethal_damage_begins_the_authored_sink",
		String(ship.get("ship_death_phase", "")) == "sinking" and int(ship.get("health", -1)) == 0,
		"phase=%s health=%s" % [ship.get("ship_death_phase", ""), ship.get("health", null)])
	_check("sinking_announces_the_authored_initial_sound",
		_has_event_sound(sim, "ship.sinking", "GoodShipBattleSinkMS"))
	sim.tick()
	_check("zero_sink_delay_settles_on_the_first_tick",
		sim.structures.has(100)
		and is_equal_approx(
			float((sim.structures[100] as Dictionary).get("height_source", 0.0)),
			start_height - SINK_PER_TICK,
		),
		"height=%s expected=%f" % [
			(sim.structures[100] as Dictionary).get("height_source", null) if sim.structures.has(100) else null,
			start_height - SINK_PER_TICK,
		])
	sim.advance(DESTRUCTION_DELAY_TICKS - 2)
	_check("hull_survives_the_whole_authored_destruction_delay",
		sim.structures.has(100) and sim.tick_index == DESTRUCTION_DELAY_TICKS - 1,
		"tick=%d present=%s" % [sim.tick_index, sim.structures.has(100)])
	var final_height := float((sim.structures[100] as Dictionary).get("height_source", 0.0))
	sim.tick()
	_check("hull_is_erased_on_the_authored_destruction_tick",
		not sim.structures.has(100) and sim.tick_index == DESTRUCTION_DELAY_TICKS,
		"tick=%d present=%s" % [sim.tick_index, sim.structures.has(100)])
	_check("destruction_is_announced", _has_event(sim, "ship.destroyed"))
	# 99 settling ticks at the authored rate before the erasing tick.
	_check("settling_used_only_the_authored_rate",
		is_equal_approx(final_height, start_height - SINK_PER_TICK * float(DESTRUCTION_DELAY_TICKS - 1)),
		"height=%f expected=%f" % [final_height, start_height - SINK_PER_TICK * float(DESTRUCTION_DELAY_TICKS - 1)])


func _test_transport_excludes_faded() -> void:
	var sim := _sim()
	sim.register_structure_module_contracts(
		"EvilMenTransportShip", [_sink_contract("EvilShipTransportSinkMS", ["FADED"])]
	)
	_add_ship(sim, 200, "EvilMenTransportShip")
	sim._attach_structure_module_contracts(sim.structures[200] as Dictionary)
	var ship := sim.structures[200] as Dictionary
	_check("authored_faded_exclusion_refuses_the_sink",
		not sim._begin_ship_slow_death(200, ship, "FADED"))
	_check("refused_death_type_leaves_no_sink_phase",
		String(ship.get("ship_death_phase", "")) == "",
		"phase=%s" % ship.get("ship_death_phase", ""))
	_check("ordinary_death_still_sinks_the_transport",
		sim._begin_ship_slow_death(200, ship, "NORMAL")
		and String(ship.get("ship_death_phase", "")) == "sinking")


func _test_fire_ship_has_no_sink_to_inherit() -> void:
	## EvilFireShip authors a weapon and no slow death. Killing it must not
	## borrow a sibling's sink contract.
	var sim := _sim()
	_add_ship(sim, 300, "EvilFireShip")
	sim._apply_structure_damage(0, 300, 5000)
	var ship := sim.structures[300] as Dictionary
	_check("fire_ship_dies_without_a_sink_phase",
		int(ship.get("health", -1)) == 0 and String(ship.get("ship_death_phase", "")) == "",
		"health=%s phase=%s" % [ship.get("health", null), ship.get("ship_death_phase", "")])
	_check("fire_ship_emits_no_sinking_event", not _has_event(sim, "ship.sinking"))
	sim.advance(DESTRUCTION_DELAY_TICKS + 1)
	_check("fire_ship_hull_is_never_erased_by_an_invented_clock",
		sim.structures.has(300) and not _has_event(sim, "ship.destroyed"))


func _test_mid_sink_snapshot_finishes_on_the_same_tick() -> void:
	## A resync taken while a hull is settling must land on the same authored
	## destruction tick, or two peers disagree about when the wreck leaves.
	var sim := _sim()
	sim.register_structure_module_contracts("ElvenBattleShip", [_sink_contract("GoodShipBattleSinkMS", [])])
	_add_ship(sim, 400, "ElvenBattleShip")
	sim._apply_structure_damage(0, 400, 5000)
	sim.advance(10)
	var restored := _sim()
	_check("mid_sink_snapshot_restores", restored.restore(sim.snapshot()))
	restored.advance(DESTRUCTION_DELAY_TICKS - 10)
	sim.advance(DESTRUCTION_DELAY_TICKS - 10)
	_check("restored_hull_is_erased_on_the_same_authored_tick",
		not restored.structures.has(400) and not sim.structures.has(400)
		and restored.tick_index == sim.tick_index,
		"restored=%s live=%s tick=%d/%d" % [
			restored.structures.has(400), sim.structures.has(400),
			restored.tick_index, sim.tick_index,
		])


func _sink_contract(sound_event: String, excluded: Array) -> Dictionary:
	return {
		"module": "ShipSlowDeathBehavior",
		"extraction": "typed",
		"runtimeStatus": "executable",
		"sourceIni": "data/ini/object/goodfaction/units/elven/elvenship.ini",
		"line": 637,
		"tag": "ModuleTag_ShipSink",
		"fields": {
			"deathTypes": "ALL",
			"includedDeathTypes": [],
			"excludedDeathTypes": excluded,
			"SinkDelay": {"milliseconds": AUTHORED_SINK_DELAY_MS},
			"SinkRate": {"value": AUTHORED_SINK_RATE},
			"DestructionDelay": {"milliseconds": AUTHORED_DESTRUCTION_DELAY_MS},
			"Sound": {"phase": "INITIAL", "event": sound_event},
		},
	}


func _add_ship(sim: RetailSliceSim, id: int, source_object_id: String) -> void:
	sim.structures[id] = {
		"id": id, "team": Sim.PLAYER_TEAM, "source_object_id": source_object_id,
		"structure_kind": "ship", "kind": "ship", "category": "naval",
		"kind_of": ["SHIP", "SELECTABLE"], "position": Vector2(4, 5),
		"facing": Vector2(0, 1), "height_source": 12.0,
		"health": 500, "maximum_health": 500,
		"damage_remainders": {}, "queue": [], "upgrade_queue": [],
	}


func _has_event(sim: RetailSliceSim, kind: String) -> bool:
	for event_value in sim.events:
		if String((event_value as Dictionary).get("kind", "")) == kind:
			return true
	return false


func _has_event_sound(sim: RetailSliceSim, kind: String, sound: String) -> bool:
	for event_value in sim.events:
		var event := event_value as Dictionary
		if String(event.get("kind", "")) == kind and String(event.get("sound", "")) == sound:
			return true
	return false


func _sim() -> RetailSliceSim:
	var sim: RetailSliceSim = Sim.new()
	sim.setup({}, {
		"unit_rules": {},
		"faction_manifest": {"structure_armor": {"ship": {"damage_scalar": 1.0, "scalars": {"default": 1.0}}}},
	})
	sim.ai_enabled = false
	sim.base_loop_enabled = false
	sim.entities.clear()
	sim.structures.clear()
	sim._structure_armor["ship"] = {"damage_scalar": 1.0, "scalars": {"default": 1.0}}
	return sim


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("SHIP_SINK_DEATH PASS %s" % label)
	else:
		failed += 1
		printerr("SHIP_SINK_DEATH FAIL %s%s" % [label, "" if detail == "" else " (%s)" % detail])


func _finish() -> void:
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		printerr("SHIP_SINK_DEATH FAIL liveness ran=%d expected=%d" % [passed + failed - 1, EXPECTED_CHECKS])
	print("SHIP_SINK_DEATH_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)

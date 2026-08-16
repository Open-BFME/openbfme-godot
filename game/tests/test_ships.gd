extends SceneTree

const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")

const SHIP_IDS := [
	"EGH_SlowTransport", "ElvenBattleShip", "ElvenFireShip",
	"ElvenShip_Interface", "ElvenShoreBombardShip", "ElvenTransportShip",
	"EvilFireShip", "EvilMenCorsairShip", "EvilMenTestCorsairShip",
	"EvilMenTransportShip", "EvilShip_Interface", "EvilShoreBombardShip",
	"TutorialElvenBattleShip",
]

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var sim: RetailSliceSim = _sim()
	for index in SHIP_IDS.size():
		var object_id: String = SHIP_IDS[index]
		var document := _document(object_id)
		var rule := Adapter.simulation_rule(document, false)
		_check("%s descriptor projects health and naval movement" % object_id,
			int(rule.get("member_health", 0)) == 500
			and String((rule.get("movement", {}) as Dictionary).get("locomotorId", "")) == "LargeShipLocomotor"
			and String(rule.get("category", "")) == "naval")
		var resolved := ((document["registration"] as Dictionary)["simulation"] as Dictionary)["resolved"] as Dictionary
		_check("%s descriptor preserves weapon and armor" % object_id,
			String((resolved["combat"] as Dictionary).get("weaponId", "")) == "ShipWeapon"
			and String((resolved["armor"] as Dictionary).get("setId", "")) == "LargeShipArmor")
		var sites: Array[Dictionary] = Adapter.ship_particle_attachments(document)
		_check("%s ParticleSysBone sites project" % object_id,
			sites.size() == 2 and String((sites[0] as Dictionary).get("anchor_bone", "")) == "WakeFront")
		var damaged: Array[Dictionary] = Adapter.ship_particle_attachments_for_conditions(document, ["REALLYDAMAGED"])
		_check("%s damage-state particles select deterministically" % object_id,
			damaged.size() == 1 and String((damaged[0] as Dictionary).get("particle_system_id", "")) == "FireBoatBeam")
		sim.register_structure_module_contracts(object_id, Adapter.module_contracts(document))
		sim.structures[1000 + index] = {
			"id": 1000 + index, "team": Sim.PLAYER_TEAM,
			"source_object_id": object_id, "structure_kind": "ship",
			"category": "naval", "kind_of": ["SHIP"],
			"position": Vector2.ZERO, "health": int(rule.member_health),
			"maximum_health": int(rule.member_health), "height_source": 2.0,
			"damage_remainders": {}, "queue": [], "upgrade_queue": [],
		}
		sim._attach_structure_module_contracts(sim.structures[1000 + index] as Dictionary)
		_check("%s instantiates as a water-domain ship" % object_id,
			sim._is_naval_row(sim.structures[1000 + index] as Dictionary))

	var provider := _WaterProvider.new()
	sim.route_provider = provider
	var moving_ship := sim.structures[1000] as Dictionary
	_check("ship movement uses water-only route query",
		sim._assign_route(moving_ship, Vector2(20, 0)) and provider.water_queries == 1 and provider.land_queries == 0)
	var ambiguous := _document("FixtureAmbiguousShip")
	var ambiguous_sites := (((ambiguous["registration"] as Dictionary)["visual"] as Dictionary)["particleAttachments"] as Array)
	var moving_site := (ambiguous_sites[1] as Dictionary).duplicate(true)
	moving_site["modelConditions"] = ["MOVING"]
	ambiguous_sites.append(moving_site)
	_check("equal-specificity particle states fail closed",
		Adapter.ship_particle_attachments_for_conditions(ambiguous, ["REALLYDAMAGED", "MOVING"]).is_empty())

	var transport := sim.structures[1005] as Dictionary
	_check("transport contract attaches slot and sinking handlers",
		transport.has("horde_transport") and transport.has("ship_slow_death"))
	_add_passenger(sim, 10)
	_check("transport enter handler garrisons passenger", bool(sim.load_transport_entity(1005, 10).get("ok", false)))
	_check("transport exit handler schedules passenger", bool(sim.request_transport_exit(10).get("ok", false)))
	sim._apply_structure_damage(0, 1005, 5000)
	_check("ShipSlowDeathBehavior starts authored sinking", String(transport.get("ship_death_phase", "")) == "sinking")

	print("SHIPS_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _document(object_id: String) -> Dictionary:
	return {
		"objectId": object_id, "category": "naval",
		"registration": {
			"production": [],
			"composition": {"containerObjectId": object_id, "primaryMemberObjectId": object_id, "members": [{"objectId": object_id, "count": 1}]},
			"kindOf": {"container": ["SHIP", "SELECTABLE"], "primaryMember": ["SHIP", "SELECTABLE"]},
			"simulation": {"status": "ready", "missing": [], "resolved": {
				"displayNameId": {"value": "OBJECT:%s" % object_id},
				"buildCost": {"value": 100}, "buildTimeSeconds": {"value": 10},
				"commandPoints": {"value": 25}, "memberCount": {"value": 1},
				"memberHealth": {"value": 500}, "speed": {"value": 70},
				"visionRange": {"value": 500},
				"movement": {"locomotorId": "LargeShipLocomotor", "turnRateDegreesPerSecond": {"value": 36.0}},
				"combat": {"weaponId": "ShipWeapon", "weaponSlot": "PRIMARY", "attackRange": {"value": 300}, "damage": {"value": 50}, "delayBetweenShotsMs": {"value": 1000}, "preAttackDelayMs": {"value": 0}, "firingDurationMs": {"value": 0}},
				"armor": {"setId": "LargeShipArmor", "table": {"default": 1.0, "damageScalar": 1.0, "scalars": {}}},
				"formation": {"memberCount": 1, "positions": [{"x": 0, "y": 0}]},
				"moduleContracts": [_transport_contract(), _sink_contract()],
			}},
			"visual": {"particleAttachments": [
				{"field": "ParticleSysBone", "anchorBone": "WakeFront", "particleSystemId": "WakeBack3", "options": ["FollowBone:Yes"], "followBone": true, "modelConditions": [], "drawModuleKind": "W3DScriptedModelDraw", "sourceIni": "data/ini/object/ships.ini", "line": 10},
				{"field": "ParticleSysBone", "anchorBone": "FireBeam01", "particleSystemId": "FireBoatBeam", "options": ["FollowBone:Yes"], "followBone": true, "modelConditions": ["REALLYDAMAGED"], "drawModuleKind": "W3DScriptedModelDraw", "sourceIni": "data/ini/object/ships.ini", "line": 11},
			]},
		},
	}


func _transport_contract() -> Dictionary:
	return {"module": "HordeTransportContain", "extraction": "typed", "runtimeStatus": "executable", "sourceIni": "data/ini/object/ships.ini", "line": 20, "fields": {
		"ObjectStatusOfContained": {"value": ["UNSELECTABLE", "ENCLOSED"]}, "Slots": {"value": 2},
		"DamagePercentToUnits": {"percent": 0.0, "ratio": 0.0}, "PassengerFilter": {"value": ["ANY", "+INFANTRY"]},
		"AllowOwnPlayerInsideOverride": {"value": true}, "AllowAlliesInside": {"value": false}, "AllowEnemiesInside": {"value": false}, "AllowNeutralInside": {"value": false},
		"ExitDelay": {"milliseconds": 100.0}, "NumberOfExitPaths": {"value": 1}, "ForceOrientationContainer": {"value": false}, "ShowPips": {"value": true},
		"KillPassengersOnDeath": {"value": false}, "EjectPassengersOnDeath": {"value": true}, "FadeFilter": {"value": ["ALL"]},
		"FadePassengerOnEnter": {"value": false}, "EnterFadeTime": {"milliseconds": 0.0}, "FadePassengerOnExit": {"value": false}, "ExitFadeTime": {"milliseconds": 0.0},
		"PassengerBonePrefix": [{"passengerBone": "B_CARGO0", "kindOf": "INFANTRY"}],
	}}


func _sink_contract() -> Dictionary:
	return {"module": "ShipSlowDeathBehavior", "extraction": "typed", "runtimeStatus": "executable", "sourceIni": "data/ini/object/ships.ini", "line": 30, "fields": {
		"deathTypes": "ALL", "includedDeathTypes": [], "excludedDeathTypes": [],
		"SinkDelay": {"milliseconds": 200.0}, "SinkRate": {"value": 10.0}, "DestructionDelay": {"milliseconds": 500.0},
	}}


func _add_passenger(sim, id: int) -> void:
	var rule := {"horde_id": Sim.SOLDIER_HORDE_ID, "category": "infantry", "speed": 1.0, "speed_source": 10.0, "acceleration": 1.0, "acceleration_source": 10.0, "turn_rate_degrees_per_second": 180.0, "braking": 1.0, "braking_source": 10.0, "attack_range": 0.1, "attack_range_source": 1.0, "minimum_attack_range": 0.0, "minimum_attack_range_source": 0.0, "vision_range": 1.0, "vision_range_source": 10.0, "delay_between_shots_ms": 1000.0, "pre_attack_delay_ms": 0.0, "firing_duration_ms": 0.0, "attack_period_ticks": 10, "pre_attack_ticks": 0, "firing_duration_ticks": 0, "member_damage": 1, "member_health": 100, "member_count": 1, "formation_positions": [Vector3.ZERO], "provenance": {}}
	sim._add_battalion(id, Sim.PLAYER_TEAM, Vector2.ZERO, "Fixture", Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, 0, rule)
	(sim.entities[id] as Dictionary)["kind_of"] = ["INFANTRY"]


func _sim() -> RetailSliceSim:
	var rules := {}
	for object_id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]:
		rules[object_id] = {"horde_id": Sim.SOLDIER_HORDE_ID, "category": "infantry", "speed": 1.0, "speed_source": 10.0, "acceleration": 1.0, "acceleration_source": 10.0, "turn_rate_degrees_per_second": 180.0, "braking": 1.0, "braking_source": 10.0, "attack_range": 0.1, "attack_range_source": 1.0, "minimum_attack_range": 0.0, "minimum_attack_range_source": 0.0, "vision_range": 1.0, "vision_range_source": 10.0, "delay_between_shots_ms": 1000.0, "pre_attack_delay_ms": 0.0, "firing_duration_ms": 0.0, "attack_period_ticks": 10, "pre_attack_ticks": 0, "firing_duration_ticks": 0, "member_damage": 1, "member_health": 100, "member_count": 1, "formation_positions": [Vector3.ZERO], "provenance": {}}
	var sim: RetailSliceSim = Sim.new()
	sim.setup({}, {"unit_rules": rules, "faction_manifest": {"structure_armor": {"ship": {"damage_scalar": 1.0, "scalars": {"default": 1.0}}}}})
	sim.ai_enabled = false; sim.base_loop_enabled = false; sim.entities.clear(); sim.structures.clear()
	sim._structure_armor["ship"] = {"damage_scalar": 1.0, "scalars": {"default": 1.0}}
	return sim


func _check(label: String, condition: bool) -> void:
	if condition: passed += 1
	else: failed += 1; push_error("SHIPS_RUNTIME_FAIL %s" % label)


class _WaterProvider:
	var water_queries := 0
	var land_queries := 0
	func query_route(_from: Vector2, to: Vector2) -> Dictionary: land_queries += 1; return {"valid": true, "points": [to]}
	func query_water_route(_from: Vector2, to: Vector2) -> Dictionary: water_queries += 1; return {"valid": true, "points": [to], "surface": "water"}

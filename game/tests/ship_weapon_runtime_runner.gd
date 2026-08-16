extends SceneTree
## Naval weapon firing from the authored descriptor, and every refusal beside it.
##
## Only four of RotWK's thirteen effective SHIP objects compile a weapon the
## runtime can execute. The two shore bombard ships are the real naval
## artillery (range 1000, min range 400, damage 500 SIEGE, 5000ms fire rate,
## a projectile object with an authored speed); EvilFireShip authors a 1000000
## damage contact weapon at range 5. The rest are refusals worth pinning: the
## corsair/battleship ShipMissileRangefinder compiles a weaponId with neither
## damage nor range, ElvenFireShip authors AttackRange = 0, and the four troop
## transports author no weapon at all.
##
## Every number below is quoted from the compiled RotWK ship catalog
## (data/ini/weapon.ini lines cited per fixture), and the firing half drives
## the sim's existing structure weapon engine unmodified.

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const EXPECTED_CHECKS := 20
const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")

## The SAGE source -> local transform this sim uses for retail distances.
const SOURCE_SCALE := 0.1
const TICK_SECONDS := 0.1

var _watchdog := RunnerWatchdogScript.new()
var passed := 0
var failed := 0


func _initialize() -> void:
	_watchdog.start(self, "SHIP_WEAPON")
	call_deferred("_run")


func _run() -> void:
	_test_authored_weapons_project()
	_test_unexecutable_weapons_are_refused_by_name()
	_test_bombard_ship_fires_on_authored_numbers()
	_test_range_band_is_honored()
	_finish()


func _test_authored_weapons_project() -> void:
	var elven := Adapter.ship_weapon_attack(
		_ship_document("ElvenShoreBombardShip", _elven_bombard_combat()), SOURCE_SCALE, TICK_SECONDS
	)
	_check("elven_bombard_admits_the_authored_weapon",
		bool(elven.get("ok", false))
		and String((elven.get("attack", {}) as Dictionary).get("weapon_id", ""))
		== "GoodShipRangedBombardWeapon",
		"reason=%s" % elven.get("reason", ""))
	var attack := elven.get("attack", {}) as Dictionary
	_check("elven_bombard_range_band_scales_from_source",
		is_equal_approx(float(attack.get("range", 0.0)), 100.0)
		and is_equal_approx(float(attack.get("minimum_range", 0.0)), 40.0)
		and is_equal_approx(float(attack.get("range_source", 0.0)), 1000.0),
		"range=%s min=%s" % [attack.get("range", null), attack.get("minimum_range", null)])
	_check("elven_bombard_fire_rate_is_the_authored_fifty_ticks",
		int(attack.get("period_ticks", 0)) == 50 and is_equal_approx(float(attack.get("damage", 0.0)), 500.0),
		"period=%s damage=%s" % [attack.get("period_ticks", null), attack.get("damage", null)])
	_check("elven_bombard_projectile_and_speed_travel_together",
		String(attack.get("projectile_object_id", "")) == "GoodShipBombardProjectile"
		and is_equal_approx(float(attack.get("projectile_speed", 0.0)), 40.1),
		"id=%s speed=%s" % [attack.get("projectile_object_id", ""), attack.get("projectile_speed", null)])
	_check("elven_bombard_delay_is_recorded_as_authored",
		String(attack.get("delay_between_shots_source", "")) == "authored")

	var evil := Adapter.ship_weapon_attack(
		_ship_document("EvilShoreBombardShip", _evil_bombard_combat()), SOURCE_SCALE, TICK_SECONDS
	)
	_check("evil_bombard_pre_attack_delay_is_three_ticks",
		bool(evil.get("ok", false))
		and int((evil.get("attack", {}) as Dictionary).get("pre_attack_ticks", -1)) == 3,
		"reason=%s ticks=%s" % [
			evil.get("reason", ""),
			(evil.get("attack", {}) as Dictionary).get("pre_attack_ticks", null),
		])

	var fire := Adapter.ship_weapon_attack(
		_ship_document("EvilFireShip", _evil_fire_combat()), SOURCE_SCALE, TICK_SECONDS
	)
	var fire_attack := fire.get("attack", {}) as Dictionary
	_check("fire_ship_ram_admits_at_the_authored_range",
		bool(fire.get("ok", false))
		and is_equal_approx(float(fire_attack.get("range", 0.0)), 0.5)
		and is_equal_approx(float(fire_attack.get("damage", 0.0)), 1000000.0)
		and not fire_attack.has("projectile_object_id"),
		"reason=%s" % fire.get("reason", ""))
	_check("unwritten_delay_is_marked_a_sage_default_not_an_authored_rate",
		String(fire_attack.get("delay_between_shots_source", "")) == "sage-default"
		and int(fire_attack.get("period_ticks", 0)) == 1,
		"source=%s period=%s" % [
			fire_attack.get("delay_between_shots_source", ""),
			fire_attack.get("period_ticks", null),
		])


func _test_unexecutable_weapons_are_refused_by_name() -> void:
	var zero_range := Adapter.ship_weapon_attack(
		_ship_document("ElvenFireShip", _elven_fire_combat()), SOURCE_SCALE, TICK_SECONDS
	)
	_check("zero_attack_range_is_refused_by_name",
		not bool(zero_range.get("ok", true))
		and String(zero_range.get("reason", "")) == "attack-range-not-positive:GoodShipFireWeapon",
		"reason=%s" % zero_range.get("reason", ""))
	var rangefinder := Adapter.ship_weapon_attack(
		_ship_document("ElvenBattleShip", _rangefinder_combat()), SOURCE_SCALE, TICK_SECONDS
	)
	_check("rangefinder_without_damage_is_refused_by_name",
		not bool(rangefinder.get("ok", true))
		and String(rangefinder.get("reason", "")) == "damage-not-authored:ShipMissileRangefinder",
		"reason=%s" % rangefinder.get("reason", ""))
	var transport := Adapter.ship_weapon_attack(
		_ship_document("ElvenTransportShip", {}), SOURCE_SCALE, TICK_SECONDS
	)
	_check("transport_without_a_combat_block_is_refused_by_name",
		not bool(transport.get("ok", true))
		and String(transport.get("reason", "")) == "no-authored-weapon",
		"reason=%s" % transport.get("reason", ""))
	var infantry := _ship_document("GondorFighterHorde", _elven_bombard_combat())
	infantry["category"] = "infantry"
	_check("a_land_document_never_reaches_the_naval_weapon_projection",
		String(Adapter.ship_weapon_attack(infantry, SOURCE_SCALE, TICK_SECONDS).get("reason", ""))
		== "not-a-naval-document")


func _test_bombard_ship_fires_on_authored_numbers() -> void:
	var sim := _sim()
	var projection := Adapter.ship_weapon_attack(
		_ship_document("ElvenShoreBombardShip", _elven_bombard_combat()), SOURCE_SCALE, TICK_SECONDS
	)
	_add_ship(sim, 500, "ElvenShoreBombardShip", (projection.get("attack", {}) as Dictionary).duplicate(true))
	# Both teams keep a living battalion so the match never resolves mid-test.
	_spawn(sim, 10, Sim.PLAYER_TEAM, Vector2(-400, 0))
	_spawn(sim, 90, Sim.ENEMY_TEAM, Vector2(50, 0))
	sim.tick()
	var launch := _last_event(sim, "combat.structure_projectile_launched")
	# 50 sim units at the authored 40.1 units/second is 13 ticks of flight.
	var expected_impact := sim.tick_index + 13
	_check("authored_weapon_launches_at_the_enemy_in_band",
		String(launch.get("weapon_id", "")) == "GoodShipRangedBombardWeapon"
		and String(launch.get("projectile_object_id", "")) == "GoodShipBombardProjectile"
		and int(launch.get("target_id", -1)) == 90,
		"launch=%s" % launch)
	_check("impact_tick_comes_from_the_authored_projectile_speed",
		int(launch.get("impact_tick", -1)) == expected_impact,
		"impact=%s expected=%d" % [launch.get("impact_tick", null), expected_impact])
	var before := int((sim.entities[90] as Dictionary).get("health", 0))
	sim.advance(12)
	_check("shell_is_still_in_flight_before_the_authored_impact_tick",
		int((sim.entities[90] as Dictionary).get("health", 0)) == before
		and not _has_event(sim, "power.structure_weapon_hit"),
		"health=%d" % int((sim.entities[90] as Dictionary).get("health", 0)))
	sim.tick()
	_check("impact_applies_the_authored_damage",
		int((sim.entities[90] as Dictionary).get("health", 0)) == before - 500
		and _has_event(sim, "power.structure_weapon_hit"),
		"health=%d before=%d" % [int((sim.entities[90] as Dictionary).get("health", 0)), before])
	var launches := _event_count(sim, "combat.structure_projectile_launched")
	# The cooldown was stamped at the launch tick, so the 50th tick after it is
	# the earliest the authored fire rate permits a second shell.
	sim.advance(50 - 14)
	_check("authored_fire_rate_gates_the_second_shell",
		_event_count(sim, "combat.structure_projectile_launched") == launches,
		"launches=%d" % _event_count(sim, "combat.structure_projectile_launched"))
	sim.tick()
	_check("second_shell_launches_once_the_authored_rate_elapses",
		_event_count(sim, "combat.structure_projectile_launched") == launches + 1,
		"launches=%d" % _event_count(sim, "combat.structure_projectile_launched"))


func _test_range_band_is_honored() -> void:
	var projection := Adapter.ship_weapon_attack(
		_ship_document("ElvenShoreBombardShip", _elven_bombard_combat()), SOURCE_SCALE, TICK_SECONDS
	)
	var attack := (projection.get("attack", {}) as Dictionary)

	var close := _sim()
	_add_ship(close, 500, "ElvenShoreBombardShip", attack.duplicate(true))
	_spawn(close, 10, Sim.PLAYER_TEAM, Vector2(-400, 0))
	# 20 sim units is inside the authored 400-source minimum range.
	_spawn(close, 90, Sim.ENEMY_TEAM, Vector2(20, 0))
	close.advance(3)
	_check("target_inside_the_authored_minimum_range_is_not_engaged",
		not _has_event(close, "combat.structure_projectile_launched"))

	var far := _sim()
	_add_ship(far, 500, "ElvenShoreBombardShip", attack.duplicate(true))
	_spawn(far, 10, Sim.PLAYER_TEAM, Vector2(-400, 0))
	# 150 sim units is beyond the authored 1000-source attack range.
	_spawn(far, 90, Sim.ENEMY_TEAM, Vector2(150, 0))
	far.advance(3)
	_check("target_beyond_the_authored_attack_range_is_not_engaged",
		not _has_event(far, "combat.structure_projectile_launched"))


# --- authored combat fixtures (compiled RotWK ship catalog) ------------------


func _elven_bombard_combat() -> Dictionary:
	## data/ini/weapon.ini:13848-13892, GoodShipRangedBombardWeapon.
	return {
		"weaponId": "GoodShipRangedBombardWeapon",
		"weaponSlot": "PRIMARY",
		"damageType": "SIEGE",
		"damage": {"value": 500, "sourceIni": "data/ini/weapon.ini", "line": 13892},
		"attackRange": {"value": 1000, "sourceIni": "data/ini/weapon.ini", "line": 13848},
		"minimumAttackRange": {"value": 400, "sourceIni": "data/ini/weapon.ini", "line": 13849},
		"delayBetweenShotsMs": {"value": 5000, "sourceIni": "data/ini/weapon.ini", "line": 13860},
		"preAttackDelayMs": {"value": 0, "sourceIni": "data/ini/weapon.ini", "line": 13858},
		"projectileObjectId": "GoodShipBombardProjectile",
		"projectileSpeed": {"value": 401, "sourceIni": "data/ini/weapon.ini", "line": 13851},
	}


func _evil_bombard_combat() -> Dictionary:
	## data/ini/weapon.ini:13994-14042, EvilShipRangedBombardWeapon.
	var combat := _elven_bombard_combat()
	combat["weaponId"] = "EvilShipRangedBombardWeapon"
	combat["projectileObjectId"] = "EvilShipBombardProjectile"
	combat["projectileSpeed"] = {"value": 301, "sourceIni": "data/ini/weapon.ini", "line": 13997}
	combat["preAttackDelayMs"] = {"value": 333, "sourceIni": "data/ini/weapon.ini", "line": 14004}
	return combat


func _evil_fire_combat() -> Dictionary:
	## data/ini/weapon.ini:14099-14112, EvilShipFireWeapon. DelayBetweenShots is
	## not authored, so the importer carries the engine default with a semantic.
	return {
		"weaponId": "EvilShipFireWeapon",
		"weaponSlot": "PRIMARY",
		"damageType": "FORCE",
		"damage": {"value": 1000000, "sourceIni": "data/ini/weapon.ini", "line": 14112},
		"attackRange": {"value": 5, "sourceIni": "data/ini/weapon.ini", "line": 14099},
		"minimumAttackRange": {"value": 0, "sourceIni": "data/ini/weapon.ini", "line": 14100},
		"delayBetweenShotsMs": {
			"value": 0,
			"semantic": "DelayBetweenShots is not authored; the SAGE engine default is 0 ms",
		},
		"preAttackDelayMs": {"value": 0, "sourceIni": "data/ini/weapon.ini", "line": 14105},
	}


func _elven_fire_combat() -> Dictionary:
	## data/ini/weapon.ini:13914-13927, GoodShipFireWeapon: AttackRange = 0.
	var combat := _evil_fire_combat()
	combat["weaponId"] = "GoodShipFireWeapon"
	combat["attackRange"] = {"value": 0, "sourceIni": "data/ini/weapon.ini", "line": 13914}
	return combat


func _rangefinder_combat() -> Dictionary:
	## ShipMissileRangefinder compiles an id, a slot and a fire rate and no
	## damage or range at all.
	return {
		"weaponId": "ShipMissileRangefinder",
		"weaponSlot": "PRIMARY",
		"delayBetweenShotsMs": {"value": 1000, "sourceIni": "data/ini/weapon.ini", "line": 13359},
		"minimumAttackRange": {"value": 25, "sourceIni": "data/ini/weapon.ini", "line": 13358},
	}


func _ship_document(object_id: String, combat: Dictionary) -> Dictionary:
	var resolved := {
		"movement": {"locomotorId": "LargeShipLocomotor"},
		"moduleContracts": [],
	}
	if not combat.is_empty():
		resolved["combat"] = combat
	return {
		"objectId": object_id,
		"category": "naval",
		"registration": {
			"production": [],
			"composition": {"containerObjectId": object_id, "primaryMemberObjectId": object_id},
			"simulation": {"status": "ready", "missing": [], "resolved": resolved},
		},
	}


# --- harness ----------------------------------------------------------------


func _add_ship(sim: RetailSliceSim, id: int, source_object_id: String, attack: Dictionary) -> void:
	sim.structures[id] = {
		"id": id, "team": Sim.PLAYER_TEAM, "source_object_id": source_object_id,
		"structure_kind": "ship", "kind": "ship", "category": "naval",
		"kind_of": ["SHIP", "SELECTABLE"], "position": Vector2.ZERO,
		"facing": Vector2(0, 1), "height_source": 12.0,
		"health": 500, "maximum_health": 500,
		"damage_remainders": {}, "queue": [], "upgrade_queue": [],
		"attack": attack,
	}


func _spawn(sim: RetailSliceSim, id: int, team: int, position: Vector2) -> void:
	sim._add_battalion(
		id, team, position, "Fixture", Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, 0, _unit_rule()
	)
	(sim.entities[id] as Dictionary)["kind_of"] = ["INFANTRY"]


func _last_event(sim: RetailSliceSim, kind: String) -> Dictionary:
	for index in range(sim.events.size() - 1, -1, -1):
		var event := sim.events[index] as Dictionary
		if String(event.get("kind", "")) == kind:
			return event
	return {}


func _has_event(sim: RetailSliceSim, kind: String) -> bool:
	return not _last_event(sim, kind).is_empty()


func _event_count(sim: RetailSliceSim, kind: String) -> int:
	var total := 0
	for event_value in sim.events:
		if String((event_value as Dictionary).get("kind", "")) == kind:
			total += 1
	return total


func _unit_rule() -> Dictionary:
	return {"horde_id": Sim.SOLDIER_HORDE_ID, "category": "infantry", "speed": 1.0, "speed_source": 10.0, "acceleration": 1.0, "acceleration_source": 10.0, "turn_rate_degrees_per_second": 180.0, "braking": 1.0, "braking_source": 10.0, "attack_range": 0.1, "attack_range_source": 1.0, "minimum_attack_range": 0.0, "minimum_attack_range_source": 0.0, "vision_range": 1.0, "vision_range_source": 10.0, "delay_between_shots_ms": 1000.0, "pre_attack_delay_ms": 0.0, "firing_duration_ms": 0.0, "attack_period_ticks": 10, "pre_attack_ticks": 0, "firing_duration_ticks": 0, "member_damage": 1, "member_health": 4000, "member_count": 1, "formation_positions": [Vector3.ZERO], "provenance": {}}


func _sim() -> RetailSliceSim:
	var rules := {}
	for object_id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]:
		rules[object_id] = _unit_rule().duplicate(true)
	var sim: RetailSliceSim = Sim.new()
	sim.setup({}, {
		"unit_rules": rules,
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
		print("SHIP_WEAPON PASS %s" % label)
	else:
		failed += 1
		printerr("SHIP_WEAPON FAIL %s%s" % [label, "" if detail == "" else " (%s)" % detail])


func _finish() -> void:
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("SHIP_WEAPON FAIL liveness ran=%d expected=%d" % [ran, EXPECTED_CHECKS])
	print("SHIP_WEAPON_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)

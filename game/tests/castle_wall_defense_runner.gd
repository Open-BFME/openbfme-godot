extends SceneTree

const Sim := preload("res://src/retail_slice/retail_slice_sim.gd")
const MAP_ID := "rotwk.map.wor-ang-carn-dum"
const TOWER_UPGRADE := "Upgrade_MenWallTower"
const TREBUCHET_UPGRADE := "Upgrade_MenWallTrebuchet"
const EXPECTED := 13
var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	OS.set_environment("OPENBFME_SLICE_FACTION", "men")
	OS.set_environment("OPENBFME_SLICE_MAP", MAP_ID)
	var scene: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	_check("real_slice_scene_loads", scene != null)
	if scene == null:
		_finish()
		return
	var slice = scene.instantiate()
	root.add_child(slice)
	var deadline := Time.get_ticks_msec() + 300000
	while Time.get_ticks_msec() < deadline and not bool(slice.ready_ok) and String(slice.failure_reason) == "":
		await process_frame
	_check("carn_dum_real_slice_ready", bool(slice.ready_ok), String(slice.failure_reason))
	if not bool(slice.ready_ok):
		_finish()
		return
	var live: RetailSliceSim = slice.simulation
	var wall_rows: Array = []
	var mounted_rows: Array = []
	for structure_id in live.structure_ids():
		var row: Dictionary = live.structure(structure_id)
		if String(row.get("structure_kind", "")) != "castle_fixture":
			continue
		if String(row.get("castle_fixture_role", "")) == "wall":
			wall_rows.append(row)
		elif String(row.get("castle_fixture_role", "")) == "wall-mounted":
			mounted_rows.append(row)
	_check("carn_dum_fixture_walls_seeded", not wall_rows.is_empty())
	_check("carn_dum_wall_defenses_seeded", mounted_rows.size() == 34, "mounted=%d" % mounted_rows.size())
	var stale := live.events.filter(func(event: Dictionary) -> bool: return String(event.get("kind", "")) == "castle.fixture_wall_defense_stale")
	_check("stale_selected_pack_fails_loudly", stale.size() == mounted_rows.size(), "stale=%d mounted=%d" % [stale.size(), mounted_rows.size()])
	root.remove_child(slice)
	slice.free()
	await process_frame

	var first := _exercise_fixture_defense()
	_check("isolated_wall_hub_fixture_found", bool(first.get("hub_found", false)))
	_check("tower_upgrade_submitted", bool(first.get("submitted", false)), String(first.get("submit_result", "")))
	_check("tower_upgrade_completed", bool(first.get("upgraded", false)))
	_check("conflicts_with_blocks_second_upgrade", bool(first.get("conflict_blocked", false)))
	_check("defense_acquires_and_fires", bool(first.get("fired", false)))
	_check("defense_damages_enemy", bool(first.get("damaged", false)))
	_check("defense_is_destructible", bool(first.get("destructible", false)))
	var second := _exercise_fixture_defense()
	_check("identical_commands_are_deterministic", String(first.get("signature", "")) == String(second.get("signature", "")), "%s != %s" % [first.get("signature", ""), second.get("signature", "")])
	_finish()


func _exercise_fixture_defense() -> Dictionary:
	# This sealed fixture uses only the authored Gondor rows cited beside each
	# value. It is the future-recook shape; the live Carn Dum assertion above
	# independently proves today's selected maps pack is stale and loud.
	var sim: RetailSliceSim = Sim.new()
	var fixture_placements := [{
			"type_name": "GondorCastleWallHub", "role": "wall-mounted",
			"kind_of": ["STRUCTURE", "WALL_HUB", "CAN_ATTACK"],
			"source_index": 39, "position": Vector2.ZERO, "elevation": 0.0,
			"yaw": 0.0, "owner": "Player_1", "maximum_health": 3000.0,
			"armor": "CastleWallArmor", "indestructible": false,
		}]
	var rules := {
		"enable_base_loop": true,
		"enable_castle_fixtures": true,
		"source_map_transform_scale": 0.1,
		"unit_rules": _unit_rules(),
		"faction_manifest": {"structure_armor": _structure_armor()},
		"playable_structure_runtimes": _runtime_documents(),
	}
	sim.setup({}, rules)
	sim.register_structure_module_contracts("GondorCastleWallHub", [_replace_contract()])
	# setup's intentionally minimal headless map falls back before fixture
	# seeding; install the already-validated fixture projection at its explicit
	# simulation seam, then invoke the same production seeder.
	sim._castle_fixture_placements = fixture_placements
	sim._seed_castle_fixtures()
	sim.ai_enabled = false
	sim.team_resources[Sim.PLAYER_TEAM] = 10000
	var hub_id := 0
	for structure_id in sim.structure_ids():
		if String(sim.structure(structure_id).get("source_object_id", "")) == "GondorCastleWallHub":
			hub_id = structure_id
			break
	if hub_id == 0:
		return {"hub_found": false, "signature": sim.state_signature()}
	# The production Carn Dum boot supplies roster start assignments. This
	# minimal isolated fixture has none, so bind the authored Player_1 owner to
	# the player team explicitly before exercising the command surface.
	(sim.structures[hub_id] as Dictionary)["team"] = Sim.PLAYER_TEAM
	var contracts := {
		TOWER_UPGRADE: _upgrade_contract(TOWER_UPGRADE),
		TREBUCHET_UPGRADE: _upgrade_contract(TREBUCHET_UPGRADE),
	}
	sim._structure_upgrade_contracts = contracts
	sim._team_structure_upgrade_contracts[Sim.PLAYER_TEAM] = contracts
	var command := {"tick": sim.tick_index + 1, "team": Sim.PLAYER_TEAM, "seq": 1, "type": "queue_structure_upgrade", "args": {"structure_id": hub_id, "upgrade_id": TOWER_UPGRADE}}
	var command_submitted := sim.submit_command(command)
	sim.tick()
	var submit_result: Variant = sim.last_command_result
	var submitted := command_submitted and typeof(submit_result) == TYPE_DICTIONARY and bool((submit_result as Dictionary).get("ok", false))
	sim.tick()
	sim.tick()
	var tower: Dictionary = sim.structure(hub_id)
	var upgraded := String(tower.get("source_object_id", "")) == "GondorCastleWallTower"
	var second_command := {"tick": sim.tick_index + 1, "team": Sim.PLAYER_TEAM, "seq": 2, "type": "queue_structure_upgrade", "args": {"structure_id": hub_id, "upgrade_id": TREBUCHET_UPGRADE}}
	var second_submitted := sim.submit_command(second_command)
	sim.tick()
	var conflict_blocked := second_submitted and typeof(sim.last_command_result) == TYPE_DICTIONARY and not bool((sim.last_command_result as Dictionary).get("ok", true))
	var enemy_id := 9001
	sim.entities[enemy_id] = _enemy(enemy_id, Vector2(10.0, 0.0))
	var enemy_before := int((sim.entities[enemy_id] as Dictionary).get("health", 0))
	for unused in 12:
		sim.tick_index += 1
		sim._step_structure_weapons()
	var fired := sim.events.any(func(event: Dictionary) -> bool: return String(event.get("kind", "")) == "combat.structure_projectile_launched" and int(event.get("entity_id", 0)) == hub_id)
	var enemy_after := int((sim.entities[enemy_id] as Dictionary).get("health", 0))
	var health_before := int(tower.get("health", 0))
	sim._apply_structure_damage(enemy_id, hub_id, health_before + 1, "unresistable")
	sim.entities.erase(enemy_id)
	return {
		"hub_found": true, "submitted": submitted, "submit_result": str(submit_result), "upgraded": upgraded,
		"conflict_blocked": conflict_blocked, "fired": fired,
		"damaged": enemy_after < enemy_before,
		"destructible": int(sim.structure(hub_id).get("health", -1)) == 0,
		"signature": sim.state_signature(),
	}


func _runtime_documents() -> Dictionary:
	var conflicts := ["Upgrade_MenWallHub", "Upgrade_MenWallRegularGate", "Upgrade_MenWallPosternGate", "Upgrade_MenWallTrebuchet"]
	return {
		"GondorCastleWallHub": {"objectId": "GondorCastleWallHub", "slug": "gondorcastlewallhub", "registration": {"gameplay": {"moduleContracts": [{"module": "ReplaceSelfUpgrade", "extraction": "typed", "runtimeStatus": "executable", "fields": {"ReplaceWith": {"value": "GondorCastleWallTower"}, "TriggeredBy": {"value": TOWER_UPGRADE}, "ConflictsWith": {"value": conflicts}}}]}}},
		"GondorCastleWallTower": {"objectId": "GondorCastleWallTower", "slug": "gondorcastlewalltower", "registration": {"gameplay": {"combat": _combat_contract(), "health": {"primary": {"maxHealth": {"value": 3000}}}}}},
	}


func _replace_contract() -> Dictionary:
	return {"module": "ReplaceSelfUpgrade", "extraction": "typed", "runtime_status": "executable", "executable": true, "fields": {"ReplaceWith": {"value": "GondorCastleWallTower"}, "TriggeredBy": {"value": TOWER_UPGRADE}, "ConflictsWith": {"value": ["Upgrade_MenWallHub", "Upgrade_MenWallRegularGate", "Upgrade_MenWallPosternGate", TREBUCHET_UPGRADE]}}}


func _combat_contract() -> Dictionary:
	# weapon.ini:4392 (5..10ms), :4405 (unconditional nugget),
	# :4449-4461 + gamedata.ini:1733 (75 damage), :1744 (250 range).
	return {"weaponId": "CastleWallUpgradeBow", "attackRange": {"value": 250}, "minimumAttackRange": {"value": 0}, "delayBetweenShotsMs": {"minimumValue": 5, "maximumValue": 10, "distribution": "uniform-inclusive-integer"}, "preAttackDelayMs": {"value": 0}, "damage": {"value": 75}, "projectileSpeed": {"value": 0}, "projectileObjectId": "GoodFactionArrow"}


func _upgrade_contract(upgrade_id: String) -> Dictionary:
	return {"upgrade_id": upgrade_id, "structure_kind": "castle_fixture", "cost": 0, "duration_ticks": 1, "level_cap": 2, "levels_to_gain": 0, "castle_upgrade": true}


func _enemy(id: int, position: Vector2) -> Dictionary:
	return {"id": id, "team": Sim.ENEMY_TEAM, "object_id": Sim.SOLDIER_HORDE_ID, "state": "idle", "position": position, "destination": position, "speed": 0.0, "member_health": [300], "health": 300, "maximum_health": 300, "alive_members": 1, "member_count": 1, "formation_positions": [Vector3.ZERO], "member_positions": [Vector3(position.x, 0.0, position.y)], "kind_of": ["INFANTRY"], "damage_remainders": {}, "object_status": {}, "applied_upgrades": {}}


func _unit_rules() -> Dictionary:
	var rules := {}
	for object_id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]:
		rules[object_id] = {"horde_id": Sim.SOLDIER_HORDE_ID, "category": "infantry", "speed": 1.0, "speed_source": 10.0, "acceleration": 1.0, "acceleration_source": 10.0, "turn_rate_degrees_per_second": 180.0, "braking": 1.0, "braking_source": 10.0, "attack_range": 1.0, "attack_range_source": 1.0, "minimum_attack_range": 0.0, "minimum_attack_range_source": 0.0, "vision_range": 10.0, "vision_range_source": 10.0, "delay_between_shots_ms": 1000.0, "pre_attack_delay_ms": 0.0, "firing_duration_ms": 0.0, "attack_period_ticks": 10, "pre_attack_ticks": 0, "firing_duration_ticks": 0, "member_damage": 1, "member_health": 100, "member_count": 1, "formation_positions": [Vector3.ZERO], "provenance": {}}
	return rules


func _structure_armor() -> Dictionary:
	var armor := {}
	for kind_value in Sim.STRUCTURE_KINDS:
		armor[String(kind_value)] = {"set_id": "FixtureArmor", "damage_scalar": 1.0, "scalars": {"default": 1.0, "unresistable": 1.0}}
	armor["gondorcastlewalltower"] = {"set_id": "CastleWallArmor", "damage_scalar": 1.0, "scalars": {"default": 1.0, "unresistable": 1.0}}
	return armor


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("CASTLE_WALL_DEFENSE PASS " + label)
	else:
		failed += 1
		push_error("CASTLE_WALL_DEFENSE FAIL %s%s" % [label, (" (%s)" % detail) if detail != "" else ""])


func _finish() -> void:
	print("CASTLE_WALL_DEFENSE_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 and passed == EXPECTED else 1)

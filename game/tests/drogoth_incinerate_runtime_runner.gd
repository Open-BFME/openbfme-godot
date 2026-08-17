extends SceneTree
## ability:Command_DrogothIncinerate (SpecialAbilityDrogothIncinerate)
##
## Drogoth's Incinerate is the RotWK Wild hero ability whose only conversion
## blocker was `Flags = WATER_OK`: the effect leaf (a WeaponFireSpecialAbility
## blast) already compiled. This runner reads the authored numbers out of the
## RotWK oracle, feeds the converter-shaped ability row through the real
## runtime adapter, and proves the sim spends the ability at those magnitudes:
##
##   * damage 1000 + centre 1000 inside the authored 210 radius, nothing outside
##   * a target beyond the authored 60 attack range is refused
##   * the authored 180000 ms recharge blocks the second cast
##   * WATER_OK admits a water target cell that the same power without the
##     flag refuses, and the oracle never co-authors WATER_OK with PATHABLE_ONLY

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const ORACLE_ROOT := "res://../workspace/retail-work/editions/rotwk/cache/effective-assets/data/ini/"
const SOURCE_SCALE := 0.1
const WATCHDOG_FRAMES := 1800

var passed := 0
var failed := 0
var _frames := 0
var _reported := false


func _initialize() -> void:
	call_deferred("_run")


func _process(_delta: float) -> bool:
	_frames += 1
	if _frames > WATCHDOG_FRAMES and not _reported:
		failed += 1
		push_error("DROGOTH_INCINERATE_FAIL watchdog: the runner aborted before reporting")
		_report()
	return false


func _run() -> void:
	var gamedata := _read(ORACLE_ROOT + "gamedata.ini")
	var specialpower := _read(ORACLE_ROOT + "specialpower.ini")
	var weapon := _read(ORACLE_ROOT + "weapon.ini")
	if gamedata.is_empty() or specialpower.is_empty() or weapon.is_empty():
		failed += 1
		push_error("DROGOTH_INCINERATE_FAIL oracle ini files are unreadable")
		_report()
		return

	# --- authored magnitudes, straight out of the oracle -------------------
	var damage := _define(gamedata, "DROGOTH_INCINERATE_DAMAGE")
	var centre_damage := _define(gamedata, "DROGOTH_INCINERATE_CENTRE_DAMAGE")
	var blast_radius := _define(gamedata, "DROGOTH_INCINERATE_RADIUS")
	var attack_range := _define(gamedata, "DROGOTH_INCINERATE_RANGE")
	_check("oracle_damage_1000", is_equal_approx(damage, 1000.0))
	_check("oracle_centre_damage_1000", is_equal_approx(centre_damage, 1000.0))
	_check("oracle_blast_radius_210", is_equal_approx(blast_radius, 210.0))
	_check("oracle_attack_range_60", is_equal_approx(attack_range, 60.0))

	var power := _block(specialpower, "SpecialPower SpecialAbilityDrogothIncinerate")
	_check("oracle_power_is_water_ok", power.contains("WATER_OK"))
	_check("oracle_power_reload_180000", _field(power, "ReloadTime") == "180000")
	_check("oracle_power_enum_balrog_breath", _field(power, "Enum") == "SPECIAL_BALROG_BREATH")
	var incinerate_weapon := _block(weapon, "Weapon DrogothIncinerate")
	_check("oracle_weapon_authors_two_damage_nuggets", incinerate_weapon.count("DamageNugget") == 2)
	_check("water_ok_never_pairs_with_pathable_only", not _has_water_ok_and_pathable_only(specialpower))

	var reload_ms := 180000.0
	var cooldown_ticks := roundi(reload_ms / (Sim.TICK_SECONDS * 1000.0))

	# --- converter shape -> real adapter -> sim rule -----------------------
	var document := _document(damage + centre_damage, blast_radius, attack_range, reload_ms, ["WATER_OK"])
	var rules := Adapter.ability_rules(document)
	if rules.size() != 1:
		failed += 1
		push_error("DROGOTH_INCINERATE_FAIL the adapter refused the WATER_OK ability row")
		_report()
		return
	var rule := rules[0]
	_check("adapter_keeps_water_ok_flag", ((rule.get("special_power_contract", {}) as Dictionary).get("flags", []) as Array).has("WATER_OK"))
	_check("adapter_ability_is_castable", bool(rule.get("castable", false)))
	_check("adapter_cooldown_is_1800_ticks", int(rule.get("cooldown_ticks", 0)) == cooldown_ticks)

	# --- magnitudes the sim actually spends --------------------------------
	var scaled_radius := blast_radius * SOURCE_SCALE
	var scaled_range := attack_range * SOURCE_SCALE
	var sim: RetailSliceSim = _fixture(rule)
	# One victim just inside the authored blast radius, one just outside it.
	_spawn(sim, 2, 1, Vector2(scaled_range, 0.0))
	_spawn(sim, 3, 1, Vector2(scaled_range + scaled_radius - 0.2, 0.0))
	_spawn(sim, 4, 1, Vector2(scaled_range + scaled_radius + 0.5, 0.0))
	var out_of_range: Dictionary = sim.cast_ability(1, "Command_DrogothIncinerate", Vector2(scaled_range + 1.0, 0.0), 0)
	_check("beyond_attack_range_refused", String(out_of_range.get("reason", "")) == "out-of-range")

	var inside_health := int((sim.entities[3] as Dictionary).get("health", 0))
	var outside_health := int((sim.entities[4] as Dictionary).get("health", 0))
	var cast: Dictionary = sim.cast_ability(1, "Command_DrogothIncinerate", Vector2(scaled_range, 0.0), 0)
	_check("cast_admitted_at_authored_range", bool(cast.get("ok", false)))
	_check("blast_hit_two_victims_inside_radius", int(cast.get("affected", 0)) == 2)
	_check("victim_inside_radius_took_2000", inside_health - int((sim.entities[3] as Dictionary).get("health", 0)) == int(damage + centre_damage))
	_check("victim_outside_radius_untouched", int((sim.entities[4] as Dictionary).get("health", 0)) == outside_health)
	_check("recharge_blocks_second_cast", String(sim.cast_ability(1, "Command_DrogothIncinerate", Vector2(scaled_range, 0.0), 0).get("reason", "")) == "cooldown-active")

	# --- WATER_OK: the same water cell, both directions --------------------
	var water_point := Vector2(scaled_range, 0.0)
	var admitted: RetailSliceSim = _fixture(rule)
	admitted.route_provider = WaterCells.new()
	_spawn(admitted, 2, 1, water_point)
	var water_cast: Dictionary = admitted.cast_ability(1, "Command_DrogothIncinerate", water_point, 0)
	_check("water_ok_admits_a_water_target", bool(water_cast.get("ok", false)))

	var land_only_document := _document(damage + centre_damage, blast_radius, attack_range, reload_ms, [])
	var land_only_rules := Adapter.ability_rules(land_only_document)
	if land_only_rules.size() != 1:
		failed += 1
		push_error("DROGOTH_INCINERATE_FAIL the adapter refused the unflagged control row")
		_report()
		return
	var refused_sim: RetailSliceSim = _fixture(land_only_rules[0])
	refused_sim.route_provider = WaterCells.new()
	_spawn(refused_sim, 2, 1, water_point)
	var refused: Dictionary = refused_sim.cast_ability(1, "Command_DrogothIncinerate", water_point, 0)
	_check("without_water_ok_the_same_cell_is_refused", String(refused.get("reason", "")) == "target-over-water")

	var dry_sim: RetailSliceSim = _fixture(land_only_rules[0])
	dry_sim.route_provider = WaterCells.new()
	_spawn(dry_sim, 2, 1, Vector2(-scaled_range, 0.0))
	var dry: Dictionary = dry_sim.cast_ability(1, "Command_DrogothIncinerate", Vector2(-scaled_range, 0.0), 0)
	_check("without_water_ok_a_land_cell_is_admitted", bool(dry.get("ok", false)))
	_report()


# ---------------------------------------------------------------------------


func _document(total_damage: float, radius: float, range_source: float, reload_ms: float, flags: Array) -> Dictionary:
	## The converter-emitted ability row for Command_DrogothIncinerate, with the
	## magnitudes read out of the oracle above rather than restated here.
	var contract := {"sourceIni": "data/ini/specialpower.ini"}
	if not flags.is_empty():
		contract["flags"] = flags
	return {
		"registration": {
			"abilities": [{
				"id": "Command_DrogothIncinerate",
				"slot": 5,
				"targeting": "point",
				"specialPowerId": "SpecialAbilityDrogothIncinerate",
				"enum": "SPECIAL_BALROG_BREATH",
				"cooldownMs": reload_ms,
				"radiusCursorRadius": radius,
				"button": {
					"commandId": "Command_DrogothIncinerate",
					"iconIds": ["HSDrogothIncinerate"],
					"labelIds": ["CONTROLBAR:SpecialAbilityIncinerate"],
					"options": ["CONTEXTMODE_COMMAND", "NEED_TARGET_POS"],
					"radiusCursorType": "FireBreathRadiusCursor",
				},
				"effect": {
					"kind": "weapon-blast",
					"weaponId": "DrogothIncinerate",
					"damage": total_damage,
					"damageRadius": radius,
					"damageType": "FLAME",
					"attackRange": range_source,
				},
				"implementation": {"status": "implemented", "reason": "", "limitations": []},
				"levelGate": {"requiredLevel": 1, "upgradeIds": []},
				"specialPowerContract": contract,
			}],
			"stringBindings": {},
		},
	}


func _fixture(rule: Dictionary) -> RetailSliceSim:
	var unit_rules := {}
	for object_id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]:
		unit_rules[object_id] = _unit_rule()
	var sim: RetailSliceSim = Sim.new()
	sim.setup({}, {"unit_rules": unit_rules, "source_map_transform_scale": SOURCE_SCALE})
	sim.ai_enabled = false
	sim.base_loop_enabled = false
	sim.entities.clear()
	sim.structures.clear()
	sim._unit_ability_rules[Sim.SOLDIER_HORDE_ID] = sim._scaled_ability_rules([rule], SOURCE_SCALE)
	_spawn(sim, 1, 0, Vector2.ZERO)
	return sim


func _spawn(sim: RetailSliceSim, id: int, team: int, point: Vector2) -> void:
	sim._add_battalion(id, team, point, "Fixture", Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, 0, _unit_rule())
	sim._attach_hero_ability_state(sim.entities[id] as Dictionary)


func _unit_rule() -> Dictionary:
	# A single 4000-health member, so a 2000 blast is a visible, non-lethal
	# delta instead of a death that hides the magnitude.
	return {
		"horde_id": Sim.SOLDIER_HORDE_ID, "category": "hero",
		"speed": 1.0, "speed_source": 10.0,
		"acceleration": 1.0, "acceleration_source": 10.0,
		"turn_rate_degrees_per_second": 180.0,
		"braking": 1.0, "braking_source": 10.0,
		"attack_range": 1.0, "attack_range_source": 1.0,
		"minimum_attack_range": 0.0, "minimum_attack_range_source": 0.0,
		"vision_range": 10.0, "vision_range_source": 10.0,
		"delay_between_shots_ms": 1000.0, "pre_attack_delay_ms": 0.0,
		"firing_duration_ms": 0.0, "attack_period_ticks": 10,
		"pre_attack_ticks": 0, "firing_duration_ticks": 0,
		"member_damage": 10, "member_health": 4000, "member_count": 1,
		"formation_positions": [Vector3.ZERO], "provenance": {},
	}


func _read(path: String) -> String:
	return FileAccess.get_file_as_string(ProjectSettings.globalize_path(path))


func _define(text: String, name: String) -> float:
	for line in text.split("\n"):
		var trimmed := String(line).strip_edges()
		if not trimmed.begins_with("#define"):
			continue
		var parts := trimmed.split(" ", false)
		var tokens: Array = []
		for part in parts:
			for piece in String(part).split("\t", false):
				var token := String(piece).strip_edges()
				if token != "":
					tokens.append(token)
		if tokens.size() >= 3 and String(tokens[1]) == name:
			return String(tokens[2]).to_float()
	return -1.0


func _block(text: String, header: String) -> String:
	## Retail INI blocks close with a column-0 `End`; nested nuggets and states
	## are indented. Terminating on the raw unindented line keeps a weapon's
	## DamageNugget bodies inside the block instead of cutting at the first one.
	var collecting := false
	var body := ""
	for line in text.split("\n"):
		var raw := String(line).replace("\r", "")
		var trimmed := raw.strip_edges()
		if not collecting:
			if _normalized(trimmed) == _normalized(header):
				collecting = true
				body = ""
			continue
		if raw.strip_edges(false, true) == "End":
			return body
		body += trimmed + "\n"
	return body


func _field(block: String, key: String) -> String:
	for line in block.split("\n"):
		var trimmed := _normalized(String(line))
		if trimmed.begins_with(key + " ="):
			return trimmed.substr((key + " =").length()).strip_edges()
	return ""


func _normalized(value: String) -> String:
	var text := value.replace("\t", " ")
	while text.contains("  "):
		text = text.replace("  ", " ")
	return text.strip_edges()


func _has_water_ok_and_pathable_only(specialpower: String) -> bool:
	for line in specialpower.split("\n"):
		var trimmed := _normalized(String(line))
		if not trimmed.begins_with("Flags ="):
			continue
		if trimmed.contains("WATER_OK") and trimmed.contains("PATHABLE_ONLY"):
			return true
	return false


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("DROGOTH_INCINERATE_FAIL %s" % label)


func _report() -> void:
	if _reported:
		return
	_reported = true
	var ran := passed + failed
	if ran < 20:
		failed += 1
		push_error("DROGOTH_INCINERATE_FAIL liveness ran=%d expected>=20" % ran)
	print("DROGOTH_INCINERATE_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


class WaterCells:
	extends RefCounted
	## Everything at x >= 0 is a water cell; everything west of the origin is
	## dry land. Walkability is unrelated: WATER_OK is not PATHABLE_ONLY.
	func is_local_inside_navigation(_position: Vector2) -> bool: return true
	func local_to_grid_cell(position: Vector2) -> Vector2i: return Vector2i(roundi(position.x * 10.0), roundi(position.y * 10.0))
	func is_navigation_walkable(_cell: Vector2i) -> bool: return true
	func is_water_cell(cell: Vector2i) -> bool: return cell.x >= 0

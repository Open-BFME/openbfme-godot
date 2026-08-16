extends SceneTree

## Lane L2b proof: castle map fixtures become sim structures (gated).
##
## Loads the LOCALLY COOKED Erebor and Carn Dum documents (the mounted packs
## predate L2a and ship no fixtures.json; lane policy forbids a republish) —
## see tools/cook_castle_fixture_test_pack.py, which cooks both maps from the
## pure RotWK oracle with the L2b lifecycle-structure reclassification and
## header-valid GLB stubs. The pack dir comes from
## OPENBFME_CASTLE_FIXTURE_PACK, defaulting to %TEMP%/kimi-L2b-castle-pack.
##
## Covers, against the production loader and the production sim:
##   * item 2 — the objects.json properties bag survives _load_objects;
##   * item 3 — seeded fixture types classify lifecycle-structure and carry
##     their bfme2.object.map-fixture.* object ids into the bindings;
##   * item 5 — placements route through simulation_configuration() and seed
##     as sim structures with authored health/owner, behind
##     rules["enable_castle_fixtures"] (default off, byte-identical off);
##   * the fixture/objects cross-check (L2a follow-up F5) refuses a fixtures
##     document that disagrees with the cooked objects.
## Loader counts (609/589 Erebor, 403/260 Carn Dum) are pinned by the oracle pytest;
## the live combined registry path claims two FireDrake rows before the generic
## castle lane and therefore materializes 587 castle fixtures plus two lairs.
## suite in importer/tests/test_castle_fixtures_oracle.py.

const Watchdog := preload("res://tests/runner_watchdog.gd")
# Runtime-loaded, not preloaded: both scripts reference the ModLoader/ContentDB
# autoloads, whose identifiers only resolve once autoloads are registered —
# preload compiles them too early (the L2a loader runner does the same).
var MapDataScript: GDScript
var SimScript: GDScript

const EXPECTED_CHECKS := 32
const EREBOR_SLUG := "wor-erebor"
const CARN_DUM_SLUG := "wor-ang-carn-dum"

var passed := 0
var failed := 0
var _watchdog := Watchdog.new()


func _initialize() -> void:
	_watchdog.start(self, "CASTLE_FIXTURE_SPAWN", 0, 0, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _check(name: String, condition: bool, detail: String = "") -> void:
	_watchdog.note(name)
	if condition:
		passed += 1
		print("CASTLE_FIXTURE_SPAWN PASS %s" % name)
	else:
		failed += 1
		printerr("CASTLE_FIXTURE_SPAWN FAIL %s %s" % [name, detail])


func _finish() -> void:
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("CASTLE_FIXTURE_SPAWN FAIL liveness ran %d checks, expected %d" % [ran, EXPECTED_CHECKS])
	print("CASTLE_FIXTURE_SPAWN_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)


func _pack_root() -> String:
	var configured := OS.get_environment("OPENBFME_CASTLE_FIXTURE_PACK").strip_edges()
	if configured != "":
		return configured
	return OS.get_temp_dir() + "/kimi-L2b-castle-pack"


func _load_map(slug: String) -> Object:
	var pack_root := _pack_root()
	var map_path := pack_root + "/" + slug + "/map.json"
	if not FileAccess.file_exists(map_path):
		printerr("CASTLE_FIXTURE_SPAWN missing cooked test pack at %s — run: python tools/cook_castle_fixture_test_pack.py --output %s" % [pack_root, pack_root])
		return null
	var text := FileAccess.get_file_as_string(map_path)
	var parsed: Variant = JSON.parse_string(text)
	if typeof(parsed) != TYPE_DICTIONARY:
		return null
	var definition: Dictionary = (parsed as Dictionary).duplicate(true)
	definition["_source"] = map_path
	var data: Object = MapDataScript.new()
	if not data.load_from_pack(pack_root, definition):
		printerr("CASTLE_FIXTURE_SPAWN %s load refused: %s" % [slug, String(data.error)])
		return null
	return data


func _seed_row_for(data: Object, source_index: int) -> Dictionary:
	for row in data.castle_fixture_placements:
		if int((row as Dictionary).get("source_index", -1)) == source_index:
			return row
	return {}


func _fixture_row_for(data: Object, type_name: String, role: String = "") -> Dictionary:
	for record in data.map_fixtures:
		var row: Dictionary = record
		if String(row.get("typeName", "")) == type_name and (role == "" or String(row.get("role", "")) == role):
			return row
	return {}


func _sim_rules(flag: bool) -> Dictionary:
	var rules := {
		# Base loop on: without roster structures team 1 is eliminated at tick
		# 0, the match enters victory state, and the attack proof never moves.
		"enable_base_loop": true,
		"spawn_initial_battalions": false,
		"unit_rules": {
			SimScript.SOLDIER_OBJECT_ID: {
				"horde_id": SimScript.SOLDIER_HORDE_ID,
				"speed": 1.0,
				"speed_source": 10.0,
				"acceleration": 1.0,
				"acceleration_source": 10.0,
				"turn_rate_degrees_per_second": 180.0,
				"braking": 1.0,
				"braking_source": 10.0,
				"attack_range": 3.0,
				"attack_range_source": 30.0,
				"minimum_attack_range": 0.0,
				"minimum_attack_range_source": 0.0,
				"vision_range": 40.0,
				"vision_range_source": 400.0,
				"delay_between_shots_ms": 600.0,
				"pre_attack_delay_ms": 200.0,
				"firing_duration_ms": 200.0,
				"attack_period_ticks": 10,
				"pre_attack_ticks": 2,
				"firing_duration_ticks": 2,
				"member_damage": 10,
				"member_health": 200,
				"member_count": 1,
				"formation_positions": [Vector3.ZERO],
				"provenance": {},
				"is_builder": false,
			},
		},
	}
	if flag:
		rules["enable_castle_fixtures"] = true
	return rules


func _with_rotwk_scenario_rules(rules: Dictionary) -> Dictionary:
	var selected := rules.duplicate(true)
	var content_db = root.get_node_or_null("ContentDB")
	selected["game"] = "rotwk"
	selected["enable_scenario_map_placements"] = true
	if content_db != null:
		selected["scenario_unit_runtimes"] = content_db.get_scenario_unit_runtimes("rotwk")
		selected["scenario_structure_runtimes"] = content_db.get_scenario_structure_runtimes("rotwk")
		selected["scenario_prop_runtimes"] = content_db.get_scenario_prop_runtimes("rotwk")
		selected["scenario_pickup_runtimes"] = content_db.get_scenario_pickup_runtimes("rotwk")
	return selected


func _run() -> void:
	MapDataScript = load("res://src/retail_slice/retail_map_data.gd")
	SimScript = load("res://src/retail_slice/retail_slice_sim.gd")
	var erebor: Object = _load_map(EREBOR_SLUG)
	_check("erebor_loads_through_production_loader", erebor != null)
	if erebor == null:
		_finish()
		return

	# --- item 2 + item 3, Erebor loader side --------------------------------
	_check("erebor_fixtures_count_609", erebor.map_fixtures.size() == 609, "got %d" % erebor.map_fixtures.size())
	var gate_fixture := _fixture_row_for(erebor, "EreborGateDoors", "gate")
	var gate_index := int(gate_fixture.get("index", -1))
	var gate_placement := {}
	for placement in erebor.bound_structure_placements:
		if int((placement as Dictionary).get("source_index", -1)) == gate_index:
			gate_placement = placement
			break
	var gate_properties: Dictionary = gate_placement.get("properties", {})
	_check(
		"erebor_gate_placement_preserves_properties",
		String(gate_properties.get("originalOwner", "")) == "Player_1/teamPlayer_1" and gate_properties.has("objectIndestructible"),
		"properties=%s" % str(gate_properties)
	)
	_check(
		"erebor_gate_is_lifecycle_structure",
		erebor.bound_structure_type_ids.has("EreborGateDoors")
			and String(gate_placement.get("classification", "")) == "lifecycle-structure"
			and String(gate_placement.get("object_id", "")) == "bfme2.object.map-fixture.ereborgatedoors",
		"classification=%s object_id=%s" % [String(gate_placement.get("classification", "")), String(gate_placement.get("object_id", ""))]
	)
	var lair_rows := 0
	for lair in erebor.scenario_object_placements:
		if String((lair as Dictionary).get("type_name", "")) == "FireDrakeLair":
			lair_rows += 1
	_check(
		"erebor_deferred_types_stay_off_the_structure_route",
		not erebor.bound_structure_type_ids.has("FireDrakeLair")
			and not erebor.bound_structure_type_ids.has("CaptureFlag")
			and erebor.unresolved_prop_type_ids.has("EBMineCartD")
			and lair_rows == 2,
		"lair_rows=%d" % lair_rows
	)

	# --- item 5, config derivation -------------------------------------------
	_check("erebor_seed_count_589", erebor.castle_fixture_placements.size() == 589, "got %d" % erebor.castle_fixture_placements.size())
	_check(
		"erebor_deferred_tally_named",
		erebor.castle_fixture_deferred == {"capturable-flag": 4, "inert-scenery": 16},
		"got %s" % str(erebor.castle_fixture_deferred)
	)
	var gate_seed := _seed_row_for(erebor, gate_index)
	var gate_position: Array = gate_fixture.get("position", [])
	var expected_local: Vector3 = erebor.source_to_local(Vector3(float(gate_position[0]), float(gate_position[1]), float(gate_position[2])))
	_check(
		"erebor_gate_seed_row_carries_authored_contract",
		String(gate_seed.get("role", "")) == "gate"
			and String(gate_seed.get("owner", "")) == "Player_1/teamPlayer_1"
			and float(gate_seed.get("maximum_health", 0.0)) == 20000.0
			and String(gate_seed.get("armor", "")) == "DefaultWallArmor"
			and float(gate_seed.get("initial_health", 0.0)) == 100.0
			and Vector2(gate_seed.get("position", Vector2.ZERO)).is_equal_approx(Vector2(expected_local.x, expected_local.z)),
		"row=%s" % str(gate_seed)
	)
	var erebor_config: Dictionary = erebor.simulation_configuration()
	_check(
		"erebor_simulation_configuration_carries_fixtures",
		(erebor_config.get("castle_fixture_placements", []) as Array).size() == 589
			and erebor_config.get("castle_fixture_deferred", {}) == {"capturable-flag": 4, "inert-scenery": 16},
		"keys=%s" % str(erebor_config.keys())
	)

	# --- Carn Dum loader side -------------------------------------------------
	var carndum: Object = _load_map(CARN_DUM_SLUG)
	_check("carndum_loads_through_production_loader", carndum != null)
	if carndum == null:
		_finish()
		return
	_check("carndum_seed_count_260", carndum.castle_fixture_placements.size() == 260, "got %d" % carndum.castle_fixture_placements.size())
	_check(
		"carndum_deferred_tally_named",
		carndum.castle_fixture_deferred == {"capturable-flag": 3, "inert-scenery": 140},
		"got %s" % str(carndum.castle_fixture_deferred)
	)
	var carndum_wall_placements := 0
	for placement in carndum.bound_structure_placements:
		if String((placement as Dictionary).get("source_type", "")) == "AngmarWallCarnDum":
			carndum_wall_placements += 1
	_check(
		"carndum_125_walls_are_lifecycle_structures",
		carndum.bound_structure_type_ids.has("AngmarWallCarnDum") and carndum_wall_placements == 125,
		"placements=%d" % carndum_wall_placements
	)

	# --- malformed properties refused (item 2, synthetic documents) -----------
	var malformed_cases := [
		["properties_non_dictionary_refused", []],
		["properties_bad_value_refused", {"originalOwner": ["not", "a", "scalar"]}],
	]
	for case_value in malformed_cases:
		var synthetic: Object = MapDataScript.new()
		var objects_doc := {
			"count": 1,
			"objects": [{
				"typeName": "SyntheticRow",
				"index": 0,
				"godotPosition": [0.0, 0.0, 0.0],
				"sagePosition": [0.0, 0.0, 0.0],
				"godotYawRadians": 0.0,
				"roadType": 0,
				"properties": case_value[1],
			}],
		}
		var refused: bool = not synthetic._load_objects(objects_doc, {}, {}, {})
		_check(String(case_value[0]), refused and String(synthetic.error) == "invalid cooked object placement properties", "error=%s" % String(synthetic.error))
	var oversized: Object = MapDataScript.new()
	var big_bag := {}
	for index in range(513):
		big_bag["p%d" % index] = index
	var oversized_doc := {
		"count": 1,
		"objects": [{
			"typeName": "SyntheticRow",
			"index": 0,
			"godotPosition": [0.0, 0.0, 0.0],
			"sagePosition": [0.0, 0.0, 0.0],
			"godotYawRadians": 0.0,
			"roadType": 0,
			"properties": big_bag,
		}],
	}
	_check(
		"properties_over_bound_refused",
		not oversized._load_objects(oversized_doc, {}, {}, {}) and String(oversized.error) == "cooked object placement properties exceed their bound",
		"error=%s" % String(oversized.error)
	)

	# --- fixture/objects cross-check (F5) --------------------------------------
	var fixtures_path := _pack_root() + "/" + EREBOR_SLUG + "/fixtures.json"
	var original_text := FileAccess.get_file_as_string(fixtures_path)
	var tampered: Variant = JSON.parse_string(original_text)
	(tampered as Dictionary)["fixtures"][0]["typeName"] = "NoSuchObjectOnThisMap"
	var rewrite := FileAccess.open(fixtures_path, FileAccess.WRITE)
	rewrite.store_string(JSON.stringify(tampered))
	rewrite.close()
	var refused_map: Object = MapDataScript.new()
	var refused_definition: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(_pack_root() + "/" + EREBOR_SLUG + "/map.json"))
	refused_definition["_source"] = _pack_root() + "/" + EREBOR_SLUG + "/map.json"
	var refused_ok: bool = refused_map.load_from_pack(_pack_root(), refused_definition)
	var restore := FileAccess.open(fixtures_path, FileAccess.WRITE)
	restore.store_string(original_text)
	restore.close()
	_check(
		"fixtures_disagreeing_with_objects_fail_closed",
		not refused_ok and String(refused_map.error) == "castle fixture does not cross-match its cooked object",
		"error=%s" % String(refused_map.error)
	)

	# --- sim: gate off ---------------------------------------------------------
	var erebor_rules_off := _sim_rules(false)
	erebor_rules_off["source_map_transform_scale"] = float(erebor.local_transform_scale)
	var sim_off = SimScript.new()
	sim_off.setup(erebor_config, erebor_rules_off)
	var castle_rows_off := 0
	for structure_id in sim_off.structure_ids():
		if String((sim_off.structures[structure_id] as Dictionary).get("structure_kind", "")) == "castle_fixture":
			castle_rows_off += 1
	_check("gate_off_seeds_no_castle_structures", castle_rows_off == 0, "castle_rows=%d (total structures %d)" % [castle_rows_off, sim_off.structure_ids().size()])
	var config_without_key := erebor_config.duplicate(true)
	config_without_key.erase("castle_fixture_placements")
	config_without_key.erase("castle_fixture_deferred")
	var sim_off_bare = SimScript.new()
	sim_off_bare.setup(config_without_key, erebor_rules_off)
	_check(
		"gate_off_is_byte_identical_without_the_key",
		String(sim_off.state_hash()) == String(sim_off_bare.state_hash()),
		"with=%s without=%s" % [String(sim_off.state_hash()), String(sim_off_bare.state_hash())]
	)
	# Hashed-rules MP contract: an explicit false must hash like the absent
	# key (absent-unless-enabled). Two peers cannot desync because one wrote
	# false and the other omitted the flag.
	var rules_explicit_false := _sim_rules(false)
	rules_explicit_false["enable_castle_fixtures"] = false
	rules_explicit_false["source_map_transform_scale"] = float(erebor.local_transform_scale)
	var sim_off_false = SimScript.new()
	sim_off_false.setup(erebor_config, rules_explicit_false)
	_check(
		"gate_off_explicit_false_hashes_like_absent",
		String(sim_off_false.state_hash()) == String(sim_off.state_hash())
			and not sim_off_false._rules.has("enable_castle_fixtures"),
		"explicit=%s absent=%s" % [String(sim_off_false.state_hash()), String(sim_off.state_hash())]
	)

	# --- sim: gate on -----------------------------------------------------------
	var erebor_rules_on := _with_rotwk_scenario_rules(_sim_rules(true))
	erebor_rules_on["source_map_transform_scale"] = float(erebor.local_transform_scale)
	var sim_on = SimScript.new()
	sim_on.setup(erebor_config, erebor_rules_on)
	var castle_rows: Array[Dictionary] = []
	for structure_id in sim_on.structure_ids():
		var row: Dictionary = sim_on.structures[structure_id]
		if String(row.get("structure_kind", "")) == "castle_fixture":
			castle_rows.append(row)
	_check("gate_on_seeds_587_castle_structures", castle_rows.size() == 587, "got %d" % castle_rows.size())
	_check(
		"gate_on_flag_lives_in_hashed_rules",
		bool(sim_on._rules.get("enable_castle_fixtures", false))
			and String(sim_on.state_hash()) != String(sim_off.state_hash()),
		"on=%s off=%s" % [String(sim_on.state_hash()), String(sim_off.state_hash())]
	)
	var gate_structure := {}
	for row in castle_rows:
		if String(row.get("castle_fixture_type", "")) == "EreborGateDoors":
			gate_structure = row
			break
	_check(
		"gate_on_gate_has_authored_health_and_owner",
		int(gate_structure.get("health", 0)) == 20000
			and int(gate_structure.get("maximum_health", 0)) == 20000
			and int(gate_structure.get("team", -1)) == 1
			and String(gate_structure.get("castle_fixture_role", "")) == "gate"
			and String(gate_structure.get("castle_fixture_armor", "")) == "DefaultWallArmor",
		"row=%s" % str(gate_structure)
	)
	var tower_structure := {}
	for row in castle_rows:
		if String(row.get("castle_fixture_type", "")) == "EBGarrisonableTower":
			tower_structure = row
			break
	_check(
		"gate_on_civilian_owner_maps_to_civilian_team",
		int(tower_structure.get("team", -1)) == SimScript.CASTLE_CIVILIAN_TEAM
			and String(tower_structure.get("castle_fixture_owner", "")) == "PlyrCivilian/teamPlyrCivilian",
		"team=%d" % int(tower_structure.get("team", -1))
	)
	var indestructible_row := {}
	for row in castle_rows:
		if bool(row.get("indestructible", false)):
			indestructible_row = row
			break
	var health_before := int(indestructible_row.get("health", 0))
	sim_on._apply_structure_damage(0, int(indestructible_row.get("id", 0)), 500)
	_check(
		"gate_on_indestructible_refuses_damage",
		health_before > 0 and int(indestructible_row.get("health", 0)) == health_before,
		"before=%d after=%d" % [health_before, int(indestructible_row.get("health", 0))]
	)
	var destructible_row := {}
	for row in castle_rows:
		if not bool(row.get("indestructible", false)) and int(row.get("health", 0)) == int(row.get("maximum_health", 0)) and int(row.get("maximum_health", 0)) > 1000:
			destructible_row = row
			break
	var destructible_before := int(destructible_row.get("health", 0))
	sim_on._apply_structure_damage(0, int(destructible_row.get("id", 0)), 500)
	_check(
		"gate_on_destructible_takes_provisional_scalar_damage",
		int(destructible_row.get("health", 0)) == destructible_before - 500,
		"before=%d after=%d" % [destructible_before, int(destructible_row.get("health", 0))]
	)

	# Carn Dum initialHealth is an authored percent (75 on three rows).
	var carndum_config: Dictionary = carndum.simulation_configuration()
	var percent_seed := {}
	for row in carndum_config.get("castle_fixture_placements", []):
		if float((row as Dictionary).get("initial_health", 100.0)) == 75.0:
			percent_seed = row
			break
	var sim_percent = SimScript.new()
	sim_percent.setup(carndum_config, _sim_rules(true))
	var percent_structure := {}
	for structure_id in sim_percent.structure_ids():
		var row: Dictionary = sim_percent.structures[structure_id]
		if int(row.get("source_index", -1)) == int(percent_seed.get("source_index", -2)):
			percent_structure = row
			break
	var expected_health := maxi(1, roundi(float(percent_seed.get("maximum_health", 1.0)) * 0.75))
	_check(
		"gate_on_initial_health_is_an_authored_percent",
		not percent_seed.is_empty()
			and int(percent_structure.get("maximum_health", 0)) == roundi(float(percent_seed.get("maximum_health", 1.0)))
			and int(percent_structure.get("health", 0)) == expected_health,
		"seed=%s structure_health=%d expected=%d" % [str(percent_seed), int(percent_structure.get("health", -1)), expected_health]
	)
	# Broad-phase exactness on Carn Dum: every structure whose blocking disc
	# can overlap a probe point (centre inside the gather box) must be
	# returned, in ascending id order.
	var gather := float(SimScript.STRUCTURE_DEFLECT_GATHER_RADIUS)
	var probe := Vector2(percent_structure.get("position", Vector2.ZERO))
	var near_ids: Array[int] = sim_percent._structure_ids_near(probe)
	var missing_ids: Array[int] = []
	for structure_id in sim_percent.structure_ids():
		var center := Vector2((sim_percent.structures[structure_id] as Dictionary).get("position", Vector2.ZERO))
		if absf(center.x - probe.x) <= gather and absf(center.y - probe.y) <= gather:
			if near_ids.find(structure_id) < 0:
				missing_ids.append(structure_id)
	var ordered := true
	for order_index in range(1, near_ids.size()):
		if near_ids[order_index] < near_ids[order_index - 1]:
			ordered = false
			break
	_check(
		"carndum_broadphase_is_a_complete_superset",
		missing_ids.is_empty() and ordered and not near_ids.is_empty(),
		"missing=%s near=%d" % [str(missing_ids), near_ids.size()]
	)

	# Targetability through the public command path: a team-0 battalion ordered
	# onto a team-1 (Player_1) wall, ticked until the blow lands. The spawn is
	# snapped to the cooked navigation mask (a raw offset lands on a blocked
	# cell inside the wall footprint and the route refuses it: blocked-origin).
	var attack_target := {}
	for row in castle_rows:
		if String(row.get("castle_fixture_role", "")) in ["wall", "gate"] and int(row.get("team", -1)) == 1 and not bool(row.get("indestructible", false)) and int(row.get("health", 0)) == int(row.get("maximum_health", 0)):
			attack_target = row
			break
	if attack_target.is_empty():
		for row in castle_rows:
			if int(row.get("team", -1)) == 1 and not bool(row.get("indestructible", false)) and int(row.get("health", 0)) == int(row.get("maximum_health", 0)):
				attack_target = row
				break
	var target_position := Vector2(attack_target.get("position", Vector2.ZERO))
	var spawn_position: Vector2 = erebor.resolve_walkable_position(target_position + Vector2(6.0, 0.0))
	sim_on._add_battalion(42, 0, spawn_position, "attacker", SimScript.SOLDIER_OBJECT_ID, SimScript.SOLDIER_HORDE_ID, 0)
	var attack_ids: Array[int] = [42]
	var accepted: int = sim_on.issue_attack(attack_ids, int(attack_target.get("id", 0)), 0)
	var attacker: Dictionary = sim_on.entities.get(42, {})
	_check(
		"gate_on_wall_accepts_attack_command",
		accepted == 1
			and int(attacker.get("target_id", 0)) == int(attack_target.get("id", 0))
			and String(attacker.get("target_kind", "")) == "structure",
		"accepted=%d" % accepted
	)
	var wall_health_before := int(attack_target.get("health", 0))
	for _tick in range(400):
		sim_on.tick()
		if int(attack_target.get("health", 0)) < wall_health_before:
			break
	_check(
		"gate_on_wall_takes_damage_in_live_ticks",
		int(attack_target.get("health", 0)) < wall_health_before,
		"before=%d after=%d" % [wall_health_before, int(attack_target.get("health", 0))]
	)

	# Registry ownership precedence: scenario admission claims the exact source
	# indices before the generic castle-fixture lane can seed them.
	var sim_both = SimScript.new()
	var both_rules := _with_rotwk_scenario_rules(_sim_rules(true))
	sim_both.setup(erebor_config, both_rules)
	var lair_structures := 0
	var lair_fixtures := 0
	for structure_id in sim_both.structure_ids():
		var row: Dictionary = sim_both.structures[structure_id]
		if String(row.get("structure_kind", "")) == "lair" and String(row.get("source_object_id", "")) == "FireDrakeLair":
			lair_structures += 1
		if String(row.get("castle_fixture_type", "")) == "FireDrakeLair":
			lair_fixtures += 1
	_check(
		"scenario_lairs_own_source_indices_under_both_lanes",
		lair_structures == 2 and lair_fixtures == 0,
		"lairs=%d lair_fixtures=%d" % [lair_structures, lair_fixtures]
	)

	# Snapshot/restore carries the seeded structures and the flag state.
	var sim_restore = SimScript.new()
	sim_restore.setup(erebor_config, _with_rotwk_scenario_rules(_sim_rules(true)))
	var snapshot: PackedByteArray = sim_restore.snapshot()
	var hash_at_snapshot := String(sim_restore.state_hash())
	for _tick in range(10):
		sim_restore.tick()
	var hash_after_ticks := String(sim_restore.state_hash())
	var restored_ok: bool = sim_restore.restore(snapshot)
	var hash_after_restore := String(sim_restore.state_hash())
	for _tick in range(10):
		sim_restore.tick()
	_check(
		"gate_on_snapshot_restore_roundtrip",
		restored_ok and hash_after_restore == hash_at_snapshot and String(sim_restore.state_hash()) == hash_after_ticks,
		"snapshot=%s restored=%s" % [hash_at_snapshot, hash_after_restore]
	)

	_finish()

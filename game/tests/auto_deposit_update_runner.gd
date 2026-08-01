extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const Manifest = preload("res://src/retail_slice/retail_faction_manifest.gd")

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var valid_rules := [
		_auto_deposit_rule(true, 10, 7),
		_auto_deposit_rule(false, 100, 0),
	]
	_check(
		"manifest_accepts_closed_auto_deposit_schema",
		Manifest._validate_structure_auto_deposit_updates(
			"FixtureIncome", valid_rules
		) == ""
	)
	var malformed_rules := valid_rules.duplicate(true)
	(malformed_rules[0] as Dictionary)["invented"] = true
	_check(
		"manifest_rejects_schema_extension",
		Manifest._validate_structure_auto_deposit_updates(
			"FixtureIncome", malformed_rules
		) != ""
	)
	var invented_default := [_auto_deposit_rule(true, 10, 7)]
	(invented_default[0] as Dictionary)["depositAmount"] = {
		"authored": "1",
		"value": 1,
		"defaulted": true,
	}
	_check(
		"manifest_rejects_non_cpp_default",
		Manifest._validate_structure_auto_deposit_updates(
			"FixtureIncome", invented_default
		) != ""
	)
	var mismatched_boost := [_auto_deposit_rule(true, 10, 7)]
	(
		((mismatched_boost[0] as Dictionary)["upgradedBoosts"] as Array)[0]
		as Dictionary
	)["boost"] = 6
	_check(
		"manifest_rejects_authored_boost_projection_mismatch",
		Manifest._validate_structure_auto_deposit_updates(
			"FixtureIncome", mismatched_boost
		) != ""
	)
	var coordinated_object_boost := [_auto_deposit_rule(true, 10, 7)]
	var object_boost := (
		(
			(coordinated_object_boost[0] as Dictionary)["upgradedBoosts"]
			as Array
		)[0] as Dictionary
	)
	object_boost["upgradeType"] = "OBJECT"
	(object_boost["upgradeAttestation"] as Dictionary)["upgradeType"] = "OBJECT"
	_check(
		"manifest_rejects_coordinated_object_upgrade_mutation",
		Manifest._validate_structure_auto_deposit_updates(
			"FixtureIncome", coordinated_object_boost
		) != ""
	)
	var sim := _make_sim()
	var structure_id := sim._team_structure_base(Sim.PLAYER_TEAM) + 1
	var structure := sim.structures[structure_id] as Dictionary
	_check("descriptor_state_materialized", (structure.get("auto_deposit_state", []) as Array).size() == 2)
	_check("module_data_bound_to_object", (structure.get("auto_deposit_rules", []) as Array).size() == 2)
	structure["income_per_payout"] = 999
	sim._initialize_structure_auto_deposit(structure)
	_check("legacy_income_clock_suppressed_when_module_bound", int(structure.get("income_per_payout", -1)) == 0)
	_check("capture_bonus_not_armed_at_create", sim._award_auto_deposit_capture(structure, Sim.PLAYER_TEAM) == 0)
	sim.advance(2)
	_check("no_early_payment", sim.resources_for_team(Sim.PLAYER_TEAM) == 0)
	structure["construction_progress"] = 0.5
	sim.advance(1)
	_check(
		"construction_suppresses_due_payment",
		sim.resources_for_team(Sim.PLAYER_TEAM) == 0
	)
	structure["construction_progress"] = 1.0
	sim.advance(3)
	_check(
		"exact_repeat_cadence_and_actual_money",
		sim.resources_for_team(Sim.PLAYER_TEAM) == 10
	)
	structure["team"] = Sim.NEUTRAL_TEAM
	sim.advance(3)
	_check("neutral_suppresses_due_payment", sim.resources_for_team(Sim.PLAYER_TEAM) == 10)
	structure["team"] = Sim.PLAYER_TEAM
	_check(
		"capture_bonus_awarded_once",
		sim._award_auto_deposit_capture(structure, Sim.PLAYER_TEAM) == 7
			and sim.resources_for_team(Sim.PLAYER_TEAM) == 17
	)
	_check(
		"capture_bonus_cannot_repeat",
		sim._award_auto_deposit_capture(structure, Sim.PLAYER_TEAM) == 0
			and sim.resources_for_team(Sim.PLAYER_TEAM) == 17
	)
	(sim.team_upgrades[Sim.PLAYER_TEAM] as Dictionary)["Upgrade_TestIncome"] = sim.tick_index
	sim.advance(3)
	_check(
		"first_matching_player_upgrade_boost",
		sim.resources_for_team(Sim.PLAYER_TEAM) == 32
	)
	var runtime_object_rule := _auto_deposit_rule(true, 10, 7)
	(
		(runtime_object_rule["upgradedBoosts"] as Array)[0]
		as Dictionary
	)["upgradeType"] = "OBJECT"
	sim.configuration_error = ""
	_check(
		"runtime_explicitly_refuses_object_upgrade_semantics",
		sim._auto_deposit_upgrade_boost(
			Sim.PLAYER_TEAM, runtime_object_rule
		) == 0
			and sim.configuration_error.contains("is not a PLAYER upgrade")
	)
	sim.configuration_error = ""
	var saved := sim.snapshot()
	var saved_hash := sim.state_hash()
	var restored := _make_sim()
	_check("snapshot_restore_accepts_auto_deposit_state", restored.restore(saved))
	_check("snapshot_hash_round_trips", restored.state_hash() == saved_hash)
	sim.advance(3)
	restored.advance(3)
	_check(
		"replay_after_restore_is_deterministic",
		restored.state_hash() == sim.state_hash()
			and restored.resources_for_team(Sim.PLAYER_TEAM) == 47
	)
	var false_money_events := 0
	var legacy_payout_events := 0
	for event_value in sim.events:
		var event := event_value as Dictionary
		if (
			String(event.get("kind", "")) == "economy.auto_deposit"
			and int(event.get("module_index", -1)) == 1
		):
			false_money_events += 1
		if (
			String(event.get("kind", "")) == "economy.payout"
			and int(event.get("entity_id", 0)) == structure_id
		):
			legacy_payout_events += 1
	_check("actual_money_false_never_deposited", false_money_events == 0)
	_check("bound_module_never_double_pays_legacy_clock", legacy_payout_events == 0)
	_test_actual_neutral_capture_uses_creation_descriptor()
	print("AUTO_DEPOSIT_UPDATE_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _make_sim() -> RetailSliceSim:
	var sim: RetailSliceSim = Sim.new()
	sim.setup(
		{},
		{
			"enable_base_loop": true,
			"spawn_initial_battalions": false,
			"starting_resources": 0,
			"farm_income": 0,
			"farm_payout_ticks": 3,
			"faction_manifest": {
				"faction": "fixture",
				"structure_kinds": ["income"],
				"seed_structure_kinds": ["income"],
				"structure_max_health": {"income": 1000},
				"structure_build_rules": {
					"income": {"cost": 0, "seconds": 1.0},
				},
				"structure_armor": {
					"income": {
						"default": 1.0,
						"damageTypes": {},
						"flanked": 1.0,
						"upgrades": {},
					},
				},
				"structure_auto_deposit_updates": {
					"income": [
						_auto_deposit_rule(true, 10, 7),
						_auto_deposit_rule(false, 100, 0),
					],
				},
				"unit_production_rules": {},
				"ai_production_plan": [],
				"spawn_roster": [],
				"builder_unit_ids": [],
			},
		}
	)
	sim.ai_enabled = false
	return sim


func _auto_deposit_rule(
	actual_money: bool, amount: int, capture_bonus: int
) -> Dictionary:
	return {
		"module": "AutoDepositUpdate",
		"depositTiming": {
			"authored": "300",
			"value": 300,
			"sourceIni": "data/ini/object/fixture.ini",
			"line": 3,
			"unit": "milliseconds",
			"simulationTicks": 3,
		},
		"depositAmount": {
			"authored": str(amount),
			"value": amount,
			"sourceIni": "data/ini/object/fixture.ini",
			"line": 4,
		},
		"initialCaptureBonus": {
			"authored": str(capture_bonus),
			"value": capture_bonus,
			"sourceIni": "data/ini/object/fixture.ini",
			"line": 5,
		},
		"actualMoney": {
			"authored": "Yes" if actual_money else "No",
			"value": actual_money,
			"sourceIni": "data/ini/object/fixture.ini",
			"line": 6,
		},
		"upgradedBoosts": [
			{
				"upgradeId": "Upgrade_TestIncome",
				"upgradeType": "PLAYER",
				"upgradeAttestation": {
					"upgradeId": "Upgrade_TestIncome",
					"upgradeType": "PLAYER",
					"sourceIni": "data/ini/upgrade.ini",
					"sourceSha256": "1111111111111111111111111111111111111111111111111111111111111111",
				},
				"boost": 5,
				"authored": "UpgradeType:Upgrade_TestIncome Boost:5",
				"sourceIni": "data/ini/object/fixture.ini",
				"line": 7,
			},
			{
				"upgradeId": "Upgrade_TestIncomeSecond",
				"upgradeType": "PLAYER",
				"upgradeAttestation": {
					"upgradeId": "Upgrade_TestIncomeSecond",
					"upgradeType": "PLAYER",
					"sourceIni": "data/ini/upgrade.ini",
					"sourceSha256": "1111111111111111111111111111111111111111111111111111111111111111",
				},
				"boost": 99,
				"authored": "UpgradeType:Upgrade_TestIncomeSecond Boost:99",
				"sourceIni": "data/ini/object/fixture.ini",
				"line": 8,
			},
		],
		"deferredFields": [],
		"runtimeStatus": "executable",
		"sourceIni": "data/ini/object/fixture.ini",
		"line": 2,
	}


func _test_actual_neutral_capture_uses_creation_descriptor() -> void:
	var player_manifest := _fixture_manifest(7, 10)
	var enemy_manifest := _fixture_manifest(23, 40)
	enemy_manifest["faction"] = "fixture_enemy"
	var sim: RetailSliceSim = Sim.new()
	sim.setup(
		{},
		{
			"enable_base_loop": true,
			"spawn_initial_battalions": false,
			"starting_resources": 0,
			"farm_income": 99,
			"faction_manifest": player_manifest,
			"team_faction_manifests": {
				Sim.PLAYER_TEAM: player_manifest,
				Sim.ENEMY_TEAM: enemy_manifest,
			},
		}
	)
	sim.ai_enabled = false
	# This focused scenario owns exactly one map-created neutral structure.
	# Remove setup's normal faction seeds so their income cannot pollute it.
	sim.structures.clear()
	var ambiguous_neutral := {
		"id": 9900,
		"team": Sim.NEUTRAL_TEAM,
		"structure_kind": "income",
	}
	sim._initialize_structure_auto_deposit(ambiguous_neutral)
	_check(
		"neutral_cross_faction_descriptor_ambiguity_refuses",
		sim.configuration_error.contains("descriptor is ambiguous")
			and not ambiguous_neutral.has("auto_deposit_rules")
	)
	sim.configuration_error = ""
	var structure_id := 9901
	var neutral_structure := {
		"id": structure_id,
		"team": Sim.NEUTRAL_TEAM,
		"kind": "structure",
		"structure_kind": "income",
		"health": 1000,
		"maximum_health": 1000,
		"construction_progress": 1.0,
		"income_per_payout": 99,
		"auto_deposit_descriptor_team": Sim.ENEMY_TEAM,
	}
	sim.structures[structure_id] = neutral_structure
	sim._initialize_structure_auto_deposit(neutral_structure)
	_check(
		"neutral_creation_binds_explicit_cross_faction_descriptor",
		int(neutral_structure.get("auto_deposit_descriptor_team", -1))
			== Sim.ENEMY_TEAM
			and int(neutral_structure.get("income_per_payout", -1)) == 0
	)
	sim.advance(3)
	var capturer := {
		"id": 8801,
		"team": Sim.PLAYER_TEAM,
		"route": [],
		"target_id": 0,
		"state": "capture",
		"capture_channel": {
			"structure_id": structure_id,
			"complete_tick": sim.tick_index,
		},
	}
	_check("actual_capture_channel_completes", not sim._step_capture_channel(capturer))
	_check(
		"capture_uses_object_attached_bonus_not_new_owner_manifest",
		int(neutral_structure.get("team", -1)) == Sim.PLAYER_TEAM
			and sim.resources_for_team(Sim.PLAYER_TEAM) == 23
	)
	sim.advance(3)
	_check(
		"post_capture_income_uses_object_attached_cross_faction_amount",
		sim.resources_for_team(Sim.PLAYER_TEAM) == 63
	)


func _fixture_manifest(capture_bonus: int, amount: int) -> Dictionary:
	return {
		"faction": "fixture",
		"structure_kinds": ["income"],
		"seed_structure_kinds": [],
		"structure_max_health": {"income": 1000},
		"structure_build_rules": {
			"income": {"cost": 0, "seconds": 1.0},
		},
		"structure_armor": {
			"income": {
				"default": 1.0,
				"damageTypes": {},
				"flanked": 1.0,
				"upgrades": {},
			},
		},
		"structure_auto_deposit_updates": {
			"income": [_auto_deposit_rule(true, amount, capture_bonus)],
		},
		"unit_production_rules": {},
		"ai_production_plan": [],
		"spawn_roster": [],
		"builder_unit_ids": [],
	}


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
		return
	failed += 1
	push_error("AUTO_DEPOSIT_UPDATE_FAIL %s" % label)

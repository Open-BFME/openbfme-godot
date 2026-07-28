extends SceneTree

## War of the Ring strategic data + rules layer.
##
## Covers the contract the rest of the port will build on: the territory graph
## loads fail-closed from the importer's `openbfme.living-world` document,
## ownership transfers and army movement obey the graph, turn advance is
## deterministic, the authoritative state round-trips through
## snapshot/restore with an unchanged hash, and everything the importer could
## not model is still visible as a recorded gap rather than quietly missing.
##
## The fixture below is authored HERE, not extracted from retail: it is the same
## document SHAPE the importer emits, with invented region names. No retail
## payload is packaged with the test.

const WorldScript = preload("res://src/wotr/wotr_world.gd")
const StateScript = preload("res://src/wotr/wotr_state.gd")
const HandoffScript = preload("res://src/wotr/wotr_handoff.gd")

## LIVENESS. A GDScript runtime error aborts the enclosing function on the spot
## without propagating, so every `_check` after the error site never runs and
## `failed` never increments - an inert runner prints a zero-failure result and
## exits 0. Pinning the number of checks a healthy run makes turns that silent
## abort into a loud failure. Raise it deliberately when tests are added; never
## lower it to make a run go green.
## 58 + 1: the closed `battle_outcome_report` gap is asserted GONE as well as
## the new `tactical_battle_outcome_report` gap being present, because a
## capability list that only grows is a list nobody believes.
## + 1: the campaign's battle rules are inside the hash. Mutation M7 removed
## them from `authoritative_state()` and every existing check stayed green,
## which meant two peers could have run different resolution paths and still
## agreed on a hash - the exact failure this whole layer exists to prevent.
const EXPECTED_CHECKS := 60

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_world_load()
	_test_world_fails_closed()
	_test_ownership()
	_test_army_movement()
	_test_turn_order_determinism()
	_test_snapshot_round_trip()
	_test_handoff_request()
	_test_import_gaps_are_carried_through()
	_finish()


# --- fixture -----------------------------------------------------------------

## Four regions in a line plus one island, so the tests can prove both that
## adjacency permits a move and that a missing edge forbids one:
##
##     Ashfall -- Bramblewold -- Cinderfen -- Dunmarch        Emberisle
func _document() -> Dictionary:
	return {
		"format": 1,
		"schema": WorldScript.SCHEMA,
		"schemaVersion": WorldScript.SCHEMA_VERSION,
		"game": "bfme2",
		"sources": [],
		"regionCampaigns": [
			{
				"name": "LegacyCampaign",
				"kind": "RegionCampain",
				"regions": [_region("Loneheath", [], 1000)],
				"territoryBonuses": [],
			},
			{
				"name": "TestCampaign",
				"kind": "LivingWorldRegionCampaign",
				"regionEffectsManagerName": "TestRegionEffects",
				"regions": [
					_region("Ashfall", ["Bramblewold"], 600),
					_region("Bramblewold", ["Ashfall", "Cinderfen"], 480),
					_region("Cinderfen", ["Bramblewold", "Dunmarch"], 360),
					_region("Dunmarch", ["Cinderfen"], -1),
					_region("Emberisle", [], 360),
				],
				"territoryBonuses": [
					{
						"territory": "LW:TerritoryTest",
						"effectName": "TestTerritory",
						"regions": ["Ashfall", "Bramblewold"],
						"bonuses": {"experience": 20},
					}
				],
			},
		],
		"territoryBonuses": [],
		"regionEffects": [],
		"cities": [],
		"defaultArmies": [
			{
				"scriptingName": "HeroArmy1",
				"spawnForTemplates": ["PlayerAlpha"],
				"heroTemplateName": "TestHero",
				"playerArmy": "TestHeroArmy",
				"icon": "HeroTestIcon",
			},
			{
				"scriptingName": "GarrisonArmy1",
				"spawnForTemplates": ["PlayerAlpha", "PlayerBeta"],
				"heroTemplateName": "",
				"playerArmy": "TestGarrisonArmy",
				"icon": "GarrisonIcon",
			},
		],
		"playerArmies": [
			{
				"name": "TestHeroArmy",
				"displayNameTag": "LWA:TestHero",
				"entries": [{"thingTemplate": "TestHero", "quantity": 1}],
			},
			{
				"name": "TestGarrisonArmy",
				"displayNameTag": "LWA:TestGarrison",
				"entries": [
					{"thingTemplate": "TestArcherHorde", "quantity": 2},
					{"thingTemplate": "TestFighterHorde", "quantity": 1},
				],
			},
		],
		"scenarios": [
			{
				"name": "TestScenario",
				"regionCampaign": "TestCampaign",
				"isEvilCampaign": false,
				"isHistoricalScenario": false,
				"minPlayers": 2,
				# Authored above the retail six-player cap on purpose: the world
				# must clamp it rather than trust the document.
				"maxPlayers": 8,
				"ownershipSets": [
					{
						"regions": ["Ashfall", "Bramblewold"],
						"startRegion": "Ashfall",
						"spawnArmies": [{"armies": ["HeroArmy1"], "region": "Ashfall"}],
						"spawnBuildings": [],
					},
					{
						"regions": ["Cinderfen", "Dunmarch"],
						"startRegion": "Dunmarch",
						"spawnArmies": [{"armies": ["GarrisonArmy1"], "region": "Dunmarch"}],
						"spawnBuildings": [],
					},
				],
			}
		],
		"victoryTypes": [],
		"rtsSettings": {
			"secondsPerReinforcement": 900,
			"startingCashRts": 6000,
			"startingCashRtsWithFort": 1000,
			"initialRevivalCostMilli": 2000,
			"initialRevivalTimeMilli": 1000,
		},
		"playerTemplates": [
			{
				"name": "PlayerAlpha",
				"faction": "FactionAlpha",
				"startingWorldCp": 1500,
				"maxWorldCp": 4500,
				"startingHeroCp": 450,
				"maxHeroCp": 450,
				"scenarioStartResources": -1,
			},
			{
				"name": "PlayerBeta",
				"faction": "FactionBeta",
				"startingWorldCp": 1500,
				"maxWorldCp": 4500,
				"startingHeroCp": 450,
				"maxHeroCp": 450,
				"scenarioStartResources": -1,
			},
		],
		"gaps": [
			{
				"virtualPath": "data/ini/campaigns/scenarios/test.inc",
				"line": 42,
				"scope": "Act",
				"reason": "presentation-only",
				"detail": "EyeTowerPoints",
			}
		],
	}


func _region(id: String, links: Array, cp_limit: int) -> Dictionary:
	var connections: Array = []
	for target in links:
		connections.append({"region": target, "detourPoints": []})
	return {
		"id": id,
		"displayName": "LW:DisplayName%s" % id,
		"mapName": "MAP TEST %s" % id,
		"subObject": id,
		"regionPortrait": "LWP%s" % id,
		"skirmishStillImage": "%s_Loadscreen" % id,
		"skirmishMusicTrack": "TestLoadMusic",
		"conqueredNotice": "APT:TestNotice",
		"bonuses": {"experience": 5},
		"bonusMacros": {},
		"cpLimit": cp_limit,
		"allyCpLimit": 360,
		"createAutoFort": id == "Cinderfen",
		"customCenterPoint": true,
		"centerPoint": {"x": 0, "y": 0},
		"heroArmySpots": [],
		"garrisonArmySpots": [],
		"buildingSpots": [],
		"fortress": null,
		"connections": connections,
		"restrictBuildings": [],
	}


func _world() -> WorldScript:
	var world := WorldScript.new()
	if not world.load_from_dict(_document(), "TestCampaign"):
		printerr("WOTR_STRATEGIC FAIL fixture failed to load: %s" % str(world.errors))
	return world


func _state() -> StateScript:
	var state := StateScript.new()
	state.setup(_world(), [
		{"template": "PlayerAlpha", "team": 1},
		{"template": "PlayerBeta", "team": 2},
	])
	state.apply_ownership_sets("TestScenario")
	return state


# --- world -------------------------------------------------------------------

func _test_world_load() -> void:
	var world := _world()
	_check("world_loads_the_named_campaign",
		world.errors.is_empty() and world.campaign_name == "TestCampaign",
		str(world.errors))
	_check("region_ids_are_sorted",
		Array(world.region_ids) == ["Ashfall", "Bramblewold", "Cinderfen", "Dunmarch", "Emberisle"],
		str(world.region_ids))
	_check("adjacency_is_symmetric_and_sorted",
		Array(world.neighbours("Bramblewold")) == ["Ashfall", "Cinderfen"]
			and Array(world.neighbours("Ashfall")) == ["Bramblewold"]
			and world.are_adjacent("Ashfall", "Bramblewold")
			and world.are_adjacent("Bramblewold", "Ashfall"),
		str(world.neighbours("Bramblewold")))
	_check("island_region_has_no_neighbours",
		world.neighbours("Emberisle").is_empty() and not world.are_adjacent("Dunmarch", "Emberisle"))
	_check("unauthored_cp_limit_falls_back_to_the_retail_default",
		world.region_cp_limit("Dunmarch") == WorldScript.DEFAULT_CP_LIMIT
			and world.region_cp_limit("Ashfall") == 600,
		"%d" % world.region_cp_limit("Dunmarch"))
	_check("rts_handoff_settings_are_integers",
		world.rts_settings["seconds_per_reinforcement"] == 900
			and world.rts_settings["starting_cash_rts_with_fort"] == 1000
			and typeof(world.rts_settings["initial_revival_cost_milli"]) == TYPE_INT
			and world.rts_settings["initial_revival_cost_milli"] == 2000,
		str(world.rts_settings))
	_check("command_point_economy_is_present",
		int((world.player_templates["PlayerAlpha"] as Dictionary)["starting_world_cp"]) == 1500
			and int((world.player_templates["PlayerAlpha"] as Dictionary)["max_world_cp"]) == 4500
			and int((world.player_templates["PlayerAlpha"] as Dictionary)["starting_hero_cp"]) == 450)
	_check("scenario_max_players_is_clamped_to_the_wotr_cap",
		int(world.scenario("TestScenario")["max_players"]) == WorldScript.MAX_PLAYERS
			and int(world.scenario("TestScenario")["authored_max_players"]) == 8,
		str(world.scenario("TestScenario")))
	_check("unified_territory_requires_every_member_region",
		Array(world.unified_territories({"Ashfall": 0, "Bramblewold": 0}, 0)) == ["TestTerritory"]
			and world.unified_territories({"Ashfall": 0, "Bramblewold": 1}, 0).is_empty())

	# With no explicit campaign the world must pick the one that actually has a
	# graph, not the first row: retail ships connection-less legacy campaigns.
	var auto_world := WorldScript.new()
	_check("campaign_selection_prefers_a_connected_graph",
		auto_world.load_from_dict(_document()) and auto_world.campaign_name == "TestCampaign",
		auto_world.campaign_name)


func _test_world_fails_closed() -> void:
	var cases := {
		"wrong_schema": {"schema": "evil.world"},
		"wrong_schema_version": {"schemaVersion": 99},
		"no_game": {"game": ""},
		"no_campaigns": {"regionCampaigns": []},
	}
	for name in cases:
		var document := _document()
		for key in cases[name] as Dictionary:
			document[key] = (cases[name] as Dictionary)[key]
		var world := WorldScript.new()
		_check("world_rejects_%s" % name,
			not world.load_from_dict(document) and not world.errors.is_empty())

	var missing := _document()
	var campaigns: Array = missing["regionCampaigns"]
	var broken := (campaigns[1] as Dictionary).duplicate(true)
	var regions: Array = broken["regions"]
	(regions[0] as Dictionary)["connections"] = [{"region": "Nowhere", "detourPoints": []}]
	campaigns[1] = broken
	var world := WorldScript.new()
	_check("world_rejects_a_connection_to_an_unknown_region",
		not world.load_from_dict(missing, "TestCampaign"),
		str(world.errors))

	var unnamed := _document()
	var unnamed_campaigns: Array = unnamed["regionCampaigns"]
	var unnamed_campaign := (unnamed_campaigns[1] as Dictionary).duplicate(true)
	var unnamed_regions: Array = unnamed_campaign["regions"]
	(unnamed_regions[0] as Dictionary)["id"] = ""
	unnamed_campaigns[1] = unnamed_campaign
	var unnamed_world := WorldScript.new()
	_check("world_rejects_a_region_without_an_id",
		not unnamed_world.load_from_dict(unnamed, "TestCampaign"))


# --- rules -------------------------------------------------------------------

func _test_ownership() -> void:
	var state := _state()
	_check("ownership_sets_seat_players_in_order",
		Array(state.regions_owned_by(0)) == ["Ashfall", "Bramblewold"]
			and Array(state.regions_owned_by(1)) == ["Cinderfen", "Dunmarch"]
			and state.owner_of("Emberisle") == StateScript.NEUTRAL,
		str(state.region_owner))
	_check("ownership_sets_place_the_authored_armies",
		state.armies_in_region("Ashfall").size() == 1
			and state.armies_in_region("Dunmarch").size() == 1
			and String((state.armies[state.armies_in_region("Ashfall")[0]] as Dictionary)["kind"])
				== StateScript.ARMY_HERO
			and String((state.armies[state.armies_in_region("Dunmarch")[0]] as Dictionary)["kind"])
				== StateScript.ARMY_GARRISON)
	_check("garrison_command_points_come_from_the_roster",
		state.command_points_in_region("Dunmarch", 1) == 3,
		"%d" % state.command_points_in_region("Dunmarch", 1))

	_check("transfer_moves_a_region_between_players",
		state.transfer_region("Cinderfen", 0) and state.owner_of("Cinderfen") == 0)
	_check("transfer_to_the_current_owner_is_rejected",
		not state.transfer_region("Cinderfen", 0))
	_check("transfer_of_an_unknown_region_is_rejected",
		not state.transfer_region("Atlantis", 0))
	_check("transfer_to_an_unseated_player_is_rejected",
		not state.transfer_region("Emberisle", 7))
	_check("transfer_to_neutral_is_allowed",
		state.transfer_region("Cinderfen", StateScript.NEUTRAL)
			and state.owner_of("Cinderfen") == StateScript.NEUTRAL)

	_check("attack_requires_an_adjacent_owned_region_with_an_army",
		state.can_attack(0, "Cinderfen") == false
			and state.can_attack(0, "Emberisle") == false)
	# Player 0 owns Bramblewold but has no army there yet.
	var reinforcement := state.place_army(0, "Bramblewold", "GarrisonArmy1")
	_check("army_can_be_placed_into_an_owned_region", reinforcement > 0)
	_check("attack_becomes_legal_once_an_army_is_staged",
		state.can_attack(0, "Cinderfen") and not state.can_attack(0, "Ashfall"))


func _test_army_movement() -> void:
	var state := _state()
	var hero := state.armies_in_region("Ashfall")[0]
	_check("army_moves_along_a_graph_edge",
		state.move_army(hero, "Bramblewold")
			and String((state.armies[hero] as Dictionary)["region"]) == "Bramblewold")
	_check("army_cannot_jump_a_non_edge",
		not state.move_army(hero, "Dunmarch")
			and String((state.armies[hero] as Dictionary)["region"]) == "Bramblewold")
	_check("army_cannot_move_to_where_it_already_is",
		not state.move_army(hero, "Bramblewold"))
	_check("unknown_army_is_rejected", not state.move_army(9999, "Ashfall"))

	# Command-point caps are enforced on arrival, not on departure: Cinderfen
	# caps at 360 here, so a stack that would exceed it is refused whole.
	var crowded := StateScript.new()
	var world := _world()
	crowded.setup(world, [{"template": "PlayerAlpha", "team": 1}])
	crowded.transfer_region("Ashfall", 0)
	crowded.transfer_region("Bramblewold", 0)
	var big := crowded.place_army(0, "Ashfall", "GarrisonArmy1")
	(crowded.armies[big] as Dictionary)["command_points"] = 1000
	_check("move_is_refused_when_it_would_break_the_regions_cp_cap",
		not crowded.move_army(big, "Bramblewold")
			and String((crowded.armies[big] as Dictionary)["region"]) == "Ashfall",
		"cap=%d" % world.region_cp_limit("Bramblewold"))


func _test_turn_order_determinism() -> void:
	var state := _state()
	_check("first_turn_belongs_to_the_first_seat",
		state.active_player() == 0 and state.turn_index == 0 and state.round_index() == 0)
	var visited: Array[int] = []
	for _turn in range(6):
		visited.append(state.advance_turn())
	_check("turn_order_cycles_deterministically",
		visited == [1, 0, 1, 0, 1, 0] and state.round_index() == 3,
		str(visited))

	# Same commands from the same setup must produce the same hash, twice.
	var a := _state()
	var b := _state()
	_check("identical_setups_hash_identically", a.state_hash() == b.state_hash())
	for state_under_test in [a, b]:
		state_under_test.transfer_region("Emberisle", 1)
		state_under_test.advance_turn()
		state_under_test.place_army(1, "Emberisle", "GarrisonArmy1")
		state_under_test.advance_turn()
	_check("identical_command_sequences_hash_identically",
		a.state_hash() == b.state_hash(), "%s vs %s" % [a.state_hash(), b.state_hash()])

	# The hash must actually be sensitive: a single differing turn changes it.
	var c := _state()
	c.transfer_region("Emberisle", 1)
	c.advance_turn()
	c.place_army(1, "Emberisle", "GarrisonArmy1")
	_check("a_missing_turn_changes_the_hash", a.state_hash() != c.state_hash())

	# Rejected commands must not move the state: a refused order is a no-op.
	var d := _state()
	var before := d.state_hash()
	d.transfer_region("Atlantis", 0)
	d.move_army(9999, "Ashfall")
	d.place_army(0, "Atlantis", "HeroArmy1")
	_check("rejected_commands_do_not_change_the_hash", d.state_hash() == before)

	# The event log is presentation-facing and must stay out of the hash.
	_check("events_are_recorded_but_unhashed", not d.events.is_empty())


func _test_snapshot_round_trip() -> void:
	var state := _state()
	state.transfer_region("Emberisle", 0)
	state.advance_turn()
	var hero := state.armies_in_region("Ashfall")[0]
	state.move_army(hero, "Bramblewold")
	var expected := state.state_hash()
	var bytes := state.snapshot()
	_check("snapshot_is_not_empty", not bytes.is_empty())

	# Restore into a DIFFERENT instance: a snapshot has to be self-contained.
	var restored := StateScript.new()
	restored.setup(_world(), [
		{"template": "PlayerAlpha", "team": 1},
		{"template": "PlayerBeta", "team": 2},
	])
	_check("restore_reproduces_the_hash_exactly",
		restored.restore(bytes) and restored.state_hash() == expected,
		"%s vs %s" % [restored.state_hash(), expected])
	_check("restore_reproduces_the_observable_state",
		restored.turn_index == state.turn_index
			and restored.active_player() == state.active_player()
			and Array(restored.regions_owned_by(0)) == Array(state.regions_owned_by(0))
			and Array(restored.armies_in_region("Bramblewold"))
				== Array(state.armies_in_region("Bramblewold")))
	_check("restore_clears_the_unhashed_event_log", restored.events.is_empty())

	# Diverge the source AFTER snapshotting: the restored copy must be immune.
	state.transfer_region("Cinderfen", 0)
	_check("restored_state_is_a_deep_copy",
		restored.state_hash() == expected and restored.state_hash() != state.state_hash())

	_check("restore_rejects_empty_and_non_dictionary_payloads",
		not restored.restore(PackedByteArray())
			and not restored.restore(var_to_bytes([1, 2, 3])))
	var wrong_schema := state.authoritative_state()
	wrong_schema["schema"] = "evil.state"
	_check("restore_rejects_a_foreign_schema",
		not restored.restore(var_to_bytes(wrong_schema)))
	var truncated := state.authoritative_state()
	truncated.erase("armies")
	_check("restore_rejects_a_truncated_payload",
		not restored.restore(var_to_bytes(truncated)))


func _test_handoff_request() -> void:
	var world := _world()
	var state := StateScript.new()
	state.setup(world, [
		{"template": "PlayerAlpha", "team": 1},
		{"template": "PlayerBeta", "team": 2},
	])
	state.apply_ownership_sets("TestScenario")
	state.place_army(0, "Bramblewold", "GarrisonArmy1")

	_check("handoff_refuses_an_illegal_attack",
		HandoffScript.build_request(world, state, 0, "Ashfall").is_empty()
			and HandoffScript.build_request(world, state, 0, "Emberisle").is_empty()
			and HandoffScript.build_request(world, state, 0, "Atlantis").is_empty())

	var request := HandoffScript.build_request(world, state, 0, "Cinderfen")
	_check("handoff_names_the_map_and_the_region",
		String(request.get("schema", "")) == HandoffScript.SCHEMA
			and String((request["region"] as Dictionary)["id"]) == "Cinderfen"
			and String((request["region"] as Dictionary)["map_name"]) == "MAP TEST Cinderfen",
		str(request.get("region", {})))
	_check("handoff_carries_both_sides",
		int((request["attacker"] as Dictionary)["player"]) == 0
			and String((request["attacker"] as Dictionary)["staging_region"]) == "Bramblewold"
			and (request["attacker"] as Dictionary)["armies"].size() == 1
			and int((request["defender"] as Dictionary)["player"]) == 1
			and String((request["defender"] as Dictionary)["faction"]) == "FactionBeta",
		str(request.get("attacker", {})))
	_check("handoff_reports_an_undefended_region_as_empty_not_missing",
		(request["defender"] as Dictionary).has("armies")
			and (request["defender"] as Dictionary)["armies"].size() == 0,
		"player 1 owns Cinderfen but keeps no army there in this fixture")
	# Cinderfen is the CreateAutoFort region, so the reduced with-fort purse and
	# the fort flag must both come through.
	_check("handoff_uses_the_with_fort_purse_for_a_self_defending_region",
		bool((request["region"] as Dictionary)["has_fort"])
			and int((request["settings"] as Dictionary)["starting_cash"]) == 1000
			and int((request["settings"] as Dictionary)["seconds_per_reinforcement"]) == 900,
		str(request.get("settings", {})))

	_check("handoff_is_deterministic",
		HandoffScript.build_request(world, state, 0, "Cinderfen") == request)

	# Resolve that battle by hand (there is no tactical sim to do it) and press
	# on to Dunmarch, which has no fort and IS defended.
	state.transfer_region("Cinderfen", 0)
	state.move_army(state.armies_in_region("Bramblewold")[0], "Cinderfen")
	var open_field := HandoffScript.build_request(world, state, 0, "Dunmarch")
	_check("handoff_uses_the_open_field_purse_elsewhere",
		not open_field.is_empty()
			and not bool((open_field["region"] as Dictionary)["has_fort"])
			and int((open_field["settings"] as Dictionary)["starting_cash"]) == 6000,
		str(open_field.get("settings", {})))
	_check("handoff_lists_the_defenders_armies_when_they_are_present",
		(open_field["defender"] as Dictionary)["armies"].size() == 1
			and int((open_field["defender"] as Dictionary)["command_points"]) == 3
			and ((open_field["defender"] as Dictionary)["armies"][0] as Dictionary)["entries"].size() == 2,
		str(open_field.get("defender", {})))

	# The brief must keep naming what the tactical simulation still owes it, so
	# this contract can never rot silently into "looks finished".
	#
	# `battle_outcome_report` HAS LEFT THE LIST and its going is checked here as
	# hard as its presence was, because a capability list that only ever grows is
	# a list nobody believes. The gap it named - the strategic layer needs the
	# surviving roster back, not just a winner - is exactly what auto-resolve now
	# returns and `apply_attrition()` now writes. What is still missing is the
	# same report from a TACTICAL battle, so the list now names that instead,
	# under a name that cannot be confused with the closed one.
	var unsupported: Array = request["unsupported"]
	_check("handoff_names_every_missing_tactical_capability",
		unsupported == HandoffScript.UNSUPPORTED_BY_TACTICAL_SIM
			and unsupported.has("reinforcement_schedule")
			and unsupported.has("carried_hero_state")
			and unsupported.has("tactical_battle_outcome_report")
			and unsupported.has("prebuilt_fortress")
			and unsupported.has("region_bonus_modifiers"),
		str(unsupported))
	_check("the_closed_outcome_report_gap_is_gone_rather_than_renamed_beside_itself",
		not unsupported.has("battle_outcome_report") and unsupported.size() == 5,
		str(unsupported))

	# THE CAMPAIGN'S BATTLE RULES ARE HASHED STATE. `battle_type` selects which
	# of two entirely different resolution paths a battle takes, so two peers
	# holding different values are not playing the same campaign - and if the
	# hash did not cover them, they would agree anyway. Checked by MOVING the
	# value and requiring the hash to move with it, not by reading the
	# dictionary, because a key present but never hashed would pass that.
	var rules_world := _world()
	var first := StateScript.new()
	first.setup(rules_world, [{"template": "PlayerMen"}, {"template": "PlayerMordor"}],
		{"battle_type": "auto_resolve", "battle_type_priority": "auto_resolve"})
	var second := StateScript.new()
	second.setup(rules_world, [{"template": "PlayerMen"}, {"template": "PlayerMordor"}],
		{"battle_type": "rts", "battle_type_priority": "auto_resolve"})
	var same := StateScript.new()
	same.setup(rules_world, [{"template": "PlayerMen"}, {"template": "PlayerMordor"}],
		{"battle_type": "auto_resolve", "battle_type_priority": "auto_resolve"})
	_check("the_campaigns_battle_rules_are_inside_the_strategic_hash",
		first.state_hash() != second.state_hash()
			and first.state_hash() == same.state_hash()
			and first.battle_type == "auto_resolve" and second.battle_type == "rts",
		"%s vs %s" % [first.state_hash().substr(0, 12), second.state_hash().substr(0, 12)])


func _test_import_gaps_are_carried_through() -> void:
	var world := _world()
	_check("importer_gaps_survive_into_the_strategic_layer",
		world.import_gaps.size() == 1
			and String((world.import_gaps[0] as Dictionary)["reason"]) == "presentation-only"
			and String((world.import_gaps[0] as Dictionary)["detail"]) == "EyeTowerPoints",
		str(world.import_gaps))

	# A document with no gap list at all must produce an empty list, never a
	# missing key a consumer would have to guess at.
	var document := _document()
	document.erase("gaps")
	var bare := WorldScript.new()
	_check("a_document_without_gaps_yields_an_empty_list",
		bare.load_from_dict(document, "TestCampaign") and bare.import_gaps.is_empty())


# --- harness -----------------------------------------------------------------

func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("WOTR_STRATEGIC PASS %s" % name)
	else:
		failed += 1
		printerr("WOTR_STRATEGIC FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("WOTR_STRATEGIC FAIL liveness: ran %d checks, expected %d - a function aborted before its assertions" % [ran, EXPECTED_CHECKS])
	print("WOTR_STRATEGIC_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)

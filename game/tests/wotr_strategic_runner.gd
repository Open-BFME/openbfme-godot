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
const GapsScript = preload("res://src/wotr/wotr_strategic_gaps.gd")

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
## 60 -> 97: the strategic side of three register gaps closed and asserted -
## spawn resolution + the hero ledger (+12), standing strategic buildings (+3),
## the version 3 brief's territory bonuses and hero levels (+6 +1 data gaps),
## victory conditions (+10), the strategic gap register (+2), and the snapshot
## round trip of all of it (+3).
## 97 -> 99: the false `strategic_ai_turns` gap is asserted GONE and the AI gap
## that replaced it is asserted to name the retail weights it cannot spend.
## 133 -> 141: build plots became real. Standing buildings stopped being a bare
## token list and became plot-indexed, owned, typed structure records (+4: the
## plot/owner/token survive a snapshot, a pre-build-plot snapshot MIGRATES, an
## unresolved LW_* token stands typeless and earns nothing, a plotless region
## refuses one), the treasury entered the hash (+2), and the register's two false
## construction/treasury gaps are asserted GONE with the income rule they were
## replaced by naming all three of retail's addends (+2).
const EXPECTED_CHECKS := 141

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_world_load()
	_test_world_fails_closed()
	_test_ownership()
	_test_neutral_region_claim()
	_test_army_movement()
	_test_turn_order_determinism()
	_test_snapshot_round_trip()
	_test_handoff_request()
	_test_spawn_resolution_and_hero_ledger()
	_test_region_buildings()
	_test_handoff_carries_bonuses_and_heroes()
	_test_victory_conditions()
	_test_a_self_referential_roster_row_is_refused()
	_test_strategic_gap_register()
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
				"regions": [_region("Loneheath", [], 1000, 0)],
				"territoryBonuses": [],
			},
			{
				"name": "TestCampaign",
				"kind": "LivingWorldRegionCampaign",
				"regionEffectsManagerName": "TestRegionEffects",
				"regions": [
					# The plot counts and restrictions mirror retail's own shapes:
					# two plots is the commonest region, three is the largest, and a
					# region that already carries a permanent stronghold authors
					# `RestrictBuildings Fortress 0` (which on the shipped map is
					# always a ONE-plot region).
					_region("Ashfall", ["Bramblewold"], 600, 2),
					_region("Bramblewold", ["Ashfall", "Cinderfen"], 480, 2,
						[{"buildings": ["Barracks"], "numberAllowed": 1}]),
					_region("Cinderfen", ["Bramblewold", "Dunmarch"], 360, 1,
						[{"buildings": ["Fortress"], "numberAllowed": 0}]),
					_region("Dunmarch", ["Cinderfen"], -1, 3),
					_region("Emberisle", [], 360, 0),
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
				# A scenario-scoped act army REUSING the campaign-wide scripting
				# name, exactly as retail's shipped scenarios do (ten HeroArmy1
				# rows ship in RotWK): resolution must prefer this row for
				# PlayerBeta and the default row for PlayerAlpha.
				"actArmies": [
					{
						"scriptingName": "HeroArmy1",
						"spawnForTemplates": ["PlayerBeta"],
						"heroTemplateName": "BetaHero",
						"playerArmy": "TestHeroArmy",
						"icon": "HeroBetaIcon",
					},
				],
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
						# A pre-placed strategic building, authored with the same
						# LW_* macro token retail's Angmar scenario uses. The token
						# must reach authoritative state and the handoff VERBATIM,
						# and must NOT flip the region's fort flag - the macro's
						# expansion is an importer gap, not a fact.
						"spawnBuildings": [
							{"buildings": ["LW_FORT"], "region": "Dunmarch"},
						],
					},
				],
				# Three of retail's victory-type shapes: Elimination (defeat on
				# zero regions, no explicit victory row), Capital Assault
				# (LoseIfCapitalLost), and a territory ladder (team victory at
				# four regions) - transcribed from the shipped rows' fields.
				"victoryTypes": [
					{
						"displayTags": {"displayGameType": "LWScenario:TestElimination"},
						"playerDefeatConditions": [
							{
								"kind": "PlayerDefeatCondition",
								"teams": [1, 2],
								"controlledRegions": [],
								"loseIfCapitalLost": false,
								"numControlledRegionsGreaterOrEqualTo": -1,
								"numControlledRegionsLessOrEqualTo": 0,
							},
						],
						"teamDefeatConditions": [],
						"teamVictoryConditions": [],
					},
					{
						"displayTags": {"displayGameType": "LWScenario:TestCapitalAssault"},
						"playerDefeatConditions": [
							{
								"kind": "PlayerDefeatCondition",
								"teams": [1, 2],
								"controlledRegions": [],
								"loseIfCapitalLost": true,
								"numControlledRegionsGreaterOrEqualTo": -1,
								"numControlledRegionsLessOrEqualTo": 0,
							},
						],
						"teamDefeatConditions": [],
						"teamVictoryConditions": [],
					},
					{
						"displayTags": {"displayGameType": "LWScenario:TestTerritory"},
						"playerDefeatConditions": [
							{
								"kind": "PlayerDefeatCondition",
								"teams": [1, 2],
								"controlledRegions": [],
								"loseIfCapitalLost": false,
								"numControlledRegionsGreaterOrEqualTo": -1,
								"numControlledRegionsLessOrEqualTo": 0,
							},
						],
						"teamDefeatConditions": [],
						"teamVictoryConditions": [
							{
								"kind": "TeamVictoryCondition",
								"teams": [1, 2],
								"controlledRegions": [],
								"loseIfCapitalLost": false,
								"numControlledRegionsGreaterOrEqualTo": 4,
								"numControlledRegionsLessOrEqualTo": -1,
							},
						],
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
				"scenarioStartResources": 2000,
			},
			{
				"name": "PlayerBeta",
				"faction": "FactionBeta",
				"startingWorldCp": 1500,
				"maxWorldCp": 4500,
				"startingHeroCp": 450,
				"maxHeroCp": 450,
				"scenarioStartResources": 2000,
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


## `plots` is how many `BuildingSpot` lines the region authors and `restrict` is
## its `RestrictBuildings` rows, both shaped exactly as the importer emits them.
## They default to retail's commonest case (two plots, no restriction) rather
## than to zero, because a region with no plot cannot host the `SpawnBuildings`
## row retail authors on it, and a fixture that authored one anyway would be
## testing a board retail cannot produce.
func _region(
	id: String, links: Array, cp_limit: int, plots: int = 2, restrict: Array = []
) -> Dictionary:
	var connections: Array = []
	for target in links:
		connections.append({"region": target, "detourPoints": []})
	var building_spots: Array = []
	for index in range(plots):
		building_spots.append({"x": index * 40, "y": 0})
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
		"buildingSpots": building_spots,
		"fortress": null,
		"connections": connections,
		"restrictBuildings": restrict,
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

	# Cinderfen is back with seat 1 after the neutral round-trip above, so the
	# checks below are about ATTACKING AN ENEMY, which is the only thing
	# `can_attack` now answers yes to.
	_check("the_neutral_round_trip_can_be_undone", state.transfer_region("Cinderfen", 1))
	_check("attack_requires_an_adjacent_owned_region_with_an_army",
		state.can_attack(0, "Cinderfen") == false
			and state.can_attack(0, "Emberisle") == false)
	# Player 0 owns Bramblewold but has no army there yet.
	var reinforcement := state.place_army(0, "Bramblewold", "GarrisonArmy1")
	_check("army_can_be_placed_into_an_owned_region", reinforcement > 0)
	_check("attack_becomes_legal_once_an_army_is_staged",
		state.can_attack(0, "Cinderfen") and not state.can_attack(0, "Ashfall"))
	# AND UNOWNED GROUND IS NOT A FIGHT. This is the correction the whole claim
	# path rests on: `can_attack` used to say yes here, the battle bridge then
	# refused to configure a battle with no defending faction, and the region was
	# offered forever and never taken.
	state.transfer_region("Cinderfen", StateScript.NEUTRAL)
	_check("unowned_ground_is_never_attackable",
		not state.can_attack(0, "Cinderfen"),
		"a neutral region has no defending seat and therefore no defending faction")


## TAKING NEUTRAL GROUND, which is the primary verb of the whole strategic layer
## and did not work at all before this pass: `can_attack()` said yes to an unowned
## region, `wotr_battle.gd` then refused to configure a battle with no defending
## faction, and 49 of retail's 52 regions stayed neutral through fourteen played
## turns.
##
## The rule is retail's, quoted at `CLAIM_RECORD_SCHEMA` in `wotr_state.gd`: a
## HERO army marching into unowned ground takes it, a GARRISON army cannot. What
## is pinned here is that the rule works, that it is refused when it should be,
## and that the hash still round-trips across the transaction - a claim admitted
## outside the hash would be a border two peers disagreed about.
func _test_neutral_region_claim() -> void:
	var state := _state()
	var hero := state.armies_in_region("Ashfall")[0]
	# Cinderfen borders Bramblewold, which seat 0 owns. Make it unowned.
	state.transfer_region("Cinderfen", StateScript.NEUTRAL)

	_check("no_claim_without_an_army_on_the_border",
		not state.can_claim(0, "Cinderfen") and state.claiming_army(0, "Cinderfen") == -1,
		"seat 0 owns Bramblewold but has nothing standing in it")
	# A GARRISON ARMY CANNOT TAKE NEUTRAL GROUND. Retail's own words:
	# `WOTRSCRIPT:WOTR_Tutorial066subtitle`.
	var garrison := state.place_army(0, "Bramblewold", "GarrisonArmy1")
	_check("a_garrison_army_on_the_border_still_cannot_claim",
		garrison > 0 and not state.can_claim(0, "Cinderfen"),
		"garrison armies cannot take over neutral territories on their own")
	_check("a_garrison_only_border_mints_no_claim_record",
		state.build_claim(0, "Cinderfen").is_empty())

	# THE HERO ARMY MAKES IT LEGAL.
	_check("the_hero_army_marches_to_the_border", state.move_army(hero, "Bramblewold"))
	_check("a_hero_army_on_the_border_can_claim",
		state.can_claim(0, "Cinderfen") and state.claiming_army(0, "Cinderfen") == hero,
		"claiming army %d" % state.claiming_army(0, "Cinderfen"))
	_check("an_owned_region_is_never_claimable",
		not state.can_claim(0, "Bramblewold") and not state.can_claim(0, "Dunmarch"),
		"claims are for unowned ground only")
	_check("a_region_with_no_edge_to_the_seat_is_not_claimable",
		not state.can_claim(0, "Emberisle"),
		"Emberisle has no connections at all")

	# THE TRANSACTION. Two steps, exactly like a battle, because between them the
	# campaign is half-way through a strategic action a peer must be able to see.
	var record := state.build_claim(0, "Cinderfen")
	_check("the_claim_record_names_the_army_the_region_and_the_seat",
		int(record.get("army", -1)) == hero
			and String(record.get("region", "")) == "Cinderfen"
			and String(record.get("from_region", "")) == "Bramblewold"
			and int(record.get("player", -1)) == 0
			and String(record.get("schema", "")) == StateScript.CLAIM_RECORD_SCHEMA,
		str(record))
	_check("a_claim_record_with_an_extra_field_is_refused",
		not state.begin_claim(_with(record, "smuggled", 1)))
	_check("a_claim_record_naming_another_regions_army_is_refused",
		not state.begin_claim(_with(record, "army", 9999)))
	_check("a_claim_record_from_the_wrong_schema_is_refused",
		not state.begin_claim(_with(record, "schema", "openbfme.not-a-claim")))
	_check("the_refused_records_left_nothing_in_flight", state.pending_claim.is_empty())

	var before := state.state_hash()
	_check("the_claim_opens", state.begin_claim(record))
	_check("an_open_claim_is_inside_the_strategic_hash", state.state_hash() != before)
	_check("a_second_claim_is_refused_while_one_is_in_flight",
		not state.begin_claim(record))
	_check("the_border_has_not_moved_yet",
		state.owner_of("Cinderfen") == StateScript.NEUTRAL,
		"opening a claim must not take the region; applying it does")

	# THE HASH ROUND-TRIPS ACROSS THE OPEN TRANSACTION.
	var mid := StateScript.new()
	mid.setup(_world(), [
		{"template": "PlayerAlpha", "team": 1},
		{"template": "PlayerBeta", "team": 2},
	])
	_check("a_snapshot_taken_mid_claim_round_trips",
		mid.restore(state.snapshot()) and mid.state_hash() == state.state_hash()
			and not mid.pending_claim.is_empty(),
		"a peer adopting mid-claim must see the claim")

	var applied: Dictionary = state.apply_claim()
	_check("the_claim_applies", bool(applied.get("ok", false)), str(applied.get("refusals", [])))
	_check("the_neutral_region_changed_hands",
		state.owner_of("Cinderfen") == 0,
		"owner %d" % state.owner_of("Cinderfen"))
	_check("the_claiming_army_marched_in",
		String((state.armies[hero] as Dictionary).get("region", "")) == "Cinderfen")
	_check("the_garrison_did_not_follow",
		String((state.armies[garrison] as Dictionary).get("region", "")) == "Bramblewold",
		"a claim moves the claiming army and nothing else")
	_check("nothing_is_left_in_flight", state.pending_claim.is_empty())
	var claimed_event := false
	for event in state.events:
		if String((event as Dictionary).get("kind", "")) == "region_claimed":
			claimed_event = true
	_check("the_claim_is_recorded_as_its_own_event", claimed_event,
		"retail raises APT:LivingWorldRegionTakenNotice, not the conquered one")
	_check("applying_a_claim_that_is_not_in_flight_is_refused",
		not bool((state.apply_claim() as Dictionary).get("ok", true)))

	# AND THE HASH ROUND-TRIPS AFTER THE CLAIM.
	var after := StateScript.new()
	after.setup(_world(), [
		{"template": "PlayerAlpha", "team": 1},
		{"template": "PlayerBeta", "team": 2},
	])
	_check("the_board_round_trips_after_a_claim",
		after.restore(state.snapshot()) and after.state_hash() == state.state_hash())


## `record` with one field replaced - the malformed-record fixtures above, built
## by mutation so they stay in step with the real shape rather than restating it.
func _with(record: Dictionary, field: String, value: Variant) -> Dictionary:
	var copy := record.duplicate(true)
	copy[field] = value
	return copy


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
	# The hero ledger, the standing buildings, the capital and the scenario all
	# ride the snapshot - they are hashed state, and a restore that dropped any
	# of them would resurrect fallen heroes or demolish standing forts.
	_check("the_ledger_buildings_and_capital_ride_the_snapshot",
		Array(restored.buildings_in_region("Dunmarch")) == ["LW_FORT"]
			and restored.hero_record("TestHero") == state.hero_record("TestHero")
			and String((restored.players[1] as Dictionary).get("capital", "")) == "Dunmarch"
			and restored.scenario_name == "TestScenario",
		str(restored.heroes))
	# THE PLOT, THE OWNER AND THE PROVENANCE ride it too. Before build plots
	# existed the standing-building record was a bare token list, and a restore
	# that produced one of those would put the fort back on the board with no
	# foundation under it and nobody holding it.
	var standing := restored.structures_in_region("Dunmarch")
	_check("a_restored_structure_keeps_its_plot_owner_and_authored_token",
		standing.size() == 1
			and int((standing[0] as Dictionary)["plot"]) == 0
			and int((standing[0] as Dictionary)["owner"]) == 1
			and String((standing[0] as Dictionary)["token"]) == "LW_FORT",
		str(standing))
	# THE TREASURY IS HASHED AND RESTORED. Two peers that disagree about what a
	# seat can afford disagree about the board one turn later.
	_check("the_treasury_rides_the_snapshot",
		restored.treasure(0) == state.treasure(0)
			and restored.treasure(1) == state.treasure(1)
			and state.treasure(0) > 0,
		"%d/%d vs %d/%d" % [restored.treasure(0), restored.treasure(1),
			state.treasure(0), state.treasure(1)])
	var spent := StateScript.new()
	spent.restore(bytes)
	spent.treasury[0] = spent.treasure(0) - 1
	_check("a_treasury_that_differs_by_one_coin_hashes_apart",
		spent.state_hash() != expected)
	# A snapshot minted BEFORE the ledger existed carries none of the new keys
	# and must still restore, to empty defaults - absent and empty mean the same
	# thing to the hash, so they must mean the same thing to a restore.
	var legacy := state.authoritative_state().duplicate(true)
	legacy.erase("heroes")
	legacy.erase("region_structures")
	legacy.erase("treasury")
	legacy.erase("scenario_name")
	legacy.erase("victory_type")
	var adopter := StateScript.new()
	adopter.setup(_world(), [{"template": "PlayerAlpha", "team": 1}])
	_check("a_pre_ledger_snapshot_still_restores_with_empty_defaults",
		adopter.restore(var_to_bytes(legacy))
			and adopter.heroes.is_empty()
			and adopter.region_structures.is_empty()
			and adopter.treasury.is_empty()
			and adopter.scenario_name == "")
	# A snapshot minted before BUILD PLOTS existed carries the OLD field,
	# `region_buildings`: a bare sorted token list with no plot, no owner and no
	# type. It must MIGRATE rather than be dropped - a saved campaign losing its
	# fortresses is worse than a saved campaign refusing to load.
	var pre_plots := state.authoritative_state().duplicate(true)
	pre_plots.erase("region_structures")
	pre_plots["region_buildings"] = {"Dunmarch": PackedStringArray(["LW_FARM", "LW_FORT"])}
	var migrator := StateScript.new()
	migrator.setup(_world(), [
		{"template": "PlayerAlpha", "team": 1}, {"template": "PlayerBeta", "team": 2},
	])
	var migrated_rows: Array = []
	var migrated_ok := migrator.restore(var_to_bytes(pre_plots))
	if migrated_ok:
		migrated_rows = migrator.structures_in_region("Dunmarch")
	_check("a_pre_build_plot_snapshot_migrates_its_tokens_onto_plots",
		migrated_ok and migrated_rows.size() == 2
			and Array(migrator.buildings_in_region("Dunmarch")) == ["LW_FARM", "LW_FORT"]
			and int((migrated_rows[0] as Dictionary)["plot"]) == 0
			and int((migrated_rows[1] as Dictionary)["plot"]) == 1
			and int((migrated_rows[0] as Dictionary)["owner"]) == 1,
		str(migrated_rows))
	# PRESENT-BUT-MALFORMED refuses the whole restore, exactly as a malformed
	# pending battle does: a ledger that is not a dictionary is a corrupt
	# snapshot, not an empty campaign.
	var mangled := state.authoritative_state().duplicate(true)
	mangled["heroes"] = 7
	_check("a_malformed_hero_ledger_refuses_the_whole_restore",
		not adopter.restore(var_to_bytes(mangled)))

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
	# a list nobody believes. `carried_hero_state`, `prebuilt_fortress` and
	# `region_bonus_modifiers` have now left it the same way: the brief CARRIES
	# each of them (hero levels and fallen heroes, standing buildings and the
	# fort purse, region and territory bonuses), so what remains is consumption,
	# named with a `tactical_` prefix that cannot be confused with the closed
	# strategic half.
	var unsupported: Array = request["unsupported"]
	_check("handoff_names_every_missing_tactical_capability",
		unsupported == HandoffScript.UNSUPPORTED_BY_TACTICAL_SIM
			and unsupported.has("reinforcement_schedule")
			and unsupported.has("tactical_battle_outcome_report")
			and unsupported.has("tactical_carried_hero_state")
			and unsupported.has("tactical_prebuilt_fortress")
			and unsupported.has("tactical_region_bonus_modifiers"),
		str(unsupported))
	_check("the_closed_gaps_are_gone_rather_than_renamed_beside_themselves",
		not unsupported.has("battle_outcome_report")
			and not unsupported.has("carried_hero_state")
			and not unsupported.has("prebuilt_fortress")
			and not unsupported.has("region_bonus_modifiers")
			and unsupported.size() == 5,
		str(unsupported))
	# And the brief must name the retail data that is ABSENT, so the tactical
	# side reads a recorded hole rather than assuming a default.
	var data_gaps: Array = request["data_gaps"]
	_check("handoff_names_the_unrecorded_retail_data",
		data_gaps == HandoffScript.UNRECORDED_BY_LIVING_WORLD_DATA
			and data_gaps.has("early_battle_hero_level_cap")
			and data_gaps.has("fortress_object_template")
			and data_gaps.has("lw_building_macro_expansion")
			and data_gaps.has("strategic_hero_revival"),
		str(data_gaps))

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


# --- spawn resolution and the hero ledger --------------------------------------

func _test_spawn_resolution_and_hero_ledger() -> void:
	var world := _world()
	# Retail reuses one scripting name across factions; the row that applies is
	# the scenario's own for the seat's template, then the campaign default.
	var alpha: Dictionary = world.resolve_army_spawn("TestScenario", "HeroArmy1", "PlayerAlpha")
	var beta: Dictionary = world.resolve_army_spawn("TestScenario", "HeroArmy1", "PlayerBeta")
	_check("spawn_resolution_is_scoped_by_scenario_then_template",
		bool(alpha["ok"])
			and String((alpha["spawn"] as Dictionary)["hero_template_name"]) == "TestHero"
			and bool(beta["ok"])
			and String((beta["spawn"] as Dictionary)["hero_template_name"]) == "BetaHero",
		"%s / %s" % [str(alpha), str(beta)])
	# Outside the scenario, no HeroArmy1 row names PlayerBeta: that is a refusal
	# with a reason, never a fall-through to somebody else's hero.
	var mismatch: Dictionary = world.resolve_army_spawn("", "HeroArmy1", "PlayerBeta")
	_check("a_template_no_row_names_is_refused_not_defaulted",
		not bool(mismatch["ok"]) and String(mismatch["reason"]) != "unknown",
		str(mismatch))
	_check("an_unknown_name_reports_unknown_so_a_roster_can_still_be_raised",
		String(world.resolve_army_spawn("TestScenario", "TestGarrisonArmy", "PlayerAlpha")["reason"])
			== "unknown")

	var state := _state()
	var record := state.hero_record("TestHero")
	_check("placing_a_hero_army_seeds_the_hero_ledger",
		int(record.get("level", 0)) == 1
			and not bool(record.get("fallen", true))
			and int(record.get("owner", -9)) == 0
			and state.hero_level("TestHero") == 1
			and state.hero_level("Nobody") == 0,
		str(record))
	_check("a_living_hero_cannot_lead_two_armies",
		state.place_army(0, "Bramblewold", "HeroArmy1") < 0)
	var direct := state.place_army(0, "Ashfall", "TestGarrisonArmy")
	_check("a_bare_roster_name_still_raises_a_reinforcement",
		direct > 0 and String((state.armies[direct] as Dictionary)["roster"]) == "TestGarrisonArmy")
	_check("a_name_that_is_neither_spawn_row_nor_roster_is_refused",
		state.place_army(0, "Ashfall", "GhostArmy") < 0)

	# The ledger follows the units through attrition, and a wipe FELLS the hero
	# rather than erasing every trace of them.
	var hero_id := int(state.armies_in_region("Ashfall")[0])
	var before := state.state_hash()
	_check("attrition_writes_the_heros_level_into_the_ledger",
		state.apply_attrition(hero_id, [
			{"army_id": hero_id, "level": 4, "hitpoints_milli": 500}])
			and state.hero_level("TestHero") == 4)
	_check("the_heros_level_is_inside_the_strategic_hash",
		state.state_hash() != before)
	_check("a_wiped_hero_army_fells_the_hero_rather_than_erasing_it",
		state.apply_attrition(hero_id, [])
			and not state.armies.has(hero_id)
			and bool(state.hero_record("TestHero").get("fallen", false))
			and int(state.hero_record("TestHero").get("fell_turn", -9)) == state.turn_index
			and state.hero_level("TestHero") == 4,
		str(state.hero_record("TestHero")))
	# No strategic revival rule is authored anywhere in the document, so a
	# fallen hero stays fallen - refused BY NAME, never quietly respawned.
	_check("a_fallen_hero_cannot_be_re_raised_without_a_revival_rule",
		state.place_army(0, "Ashfall", "HeroArmy1") < 0)
	_check("the_fallen_hero_is_reported_to_its_own_seat",
		state.fallen_heroes(0).size() == 1
			and state.fallen_heroes(1).is_empty()
			and String((state.fallen_heroes(0)[0] as Dictionary)["template"]) == "TestHero")


# --- standing strategic buildings ----------------------------------------------

func _test_region_buildings() -> void:
	var state := _state()
	_check("authored_spawn_buildings_stand_in_authoritative_state",
		Array(state.buildings_in_region("Dunmarch")) == ["LW_FORT"]
			and state.buildings_in_region("Ashfall").is_empty(),
		str(state.region_structures))
	# WITH NO CATALOGUE BOUND the token stands VERBATIM and TYPELESS. That is not
	# a fallback: the record then says exactly what the scenario file says and
	# nothing more, and because it has no type it earns no `GAIN_PER_FORTRESS`.
	# Equating an unresolved token with a fortress would be a reading retail never
	# wrote down HERE - it wrote it down in `riskcampaign.ini`, and this state has
	# not been given that file.
	var unresolved := state.structures_in_region("Dunmarch")[0] as Dictionary
	_check("an_unresolved_scenario_token_stands_typeless_and_earns_nothing",
		String(unresolved["building"]) == "LW_FORT"
			and String(unresolved["type"]) == ""
			and int(state.turn_income(1).get("total", 0)) == 0,
		str(unresolved))
	# A REGION WITH NO PLOTS CANNOT CARRY ONE. Retail never authors a
	# `SpawnBuildings` row on a plotless region (verified across all 14 shipped
	# scenarios), and a layer that seated one anyway would put a structure on a
	# foundation the map does not have.
	_check("a_plotless_region_refuses_an_authored_structure",
		state.plot_count("Emberisle") == 0
			and not state.place_authored_structure(0, "Emberisle", "LW_FARM"))

	# Inside the hash: the same campaign minus its buildings must hash apart.
	var stripped := _document()
	var scenario_rows: Array = stripped["scenarios"]
	var scenario_row := (scenario_rows[0] as Dictionary).duplicate(true)
	var sets: Array = scenario_row["ownershipSets"]
	(sets[1] as Dictionary)["spawnBuildings"] = []
	scenario_rows[0] = scenario_row
	var bare_world := WorldScript.new()
	bare_world.load_from_dict(stripped, "TestCampaign")
	var bare := StateScript.new()
	bare.setup(bare_world, [
		{"template": "PlayerAlpha", "team": 1},
		{"template": "PlayerBeta", "team": 2},
	])
	bare.apply_ownership_sets("TestScenario")
	_check("standing_buildings_are_inside_the_strategic_hash",
		state.state_hash() != bare.state_hash())

	# The tokens reach the brief VERBATIM, and a standing LW_FORT does NOT set
	# the fort flag or the with-fort purse: the macro's expansion is a recorded
	# importer gap, and equating token with fortress would be a reading retail
	# never wrote down.
	var garrison := state.place_army(0, "Bramblewold", "GarrisonArmy1")
	state.transfer_region("Cinderfen", 0)
	state.move_army(garrison, "Cinderfen")
	var brief := HandoffScript.build_request(state.world, state, 0, "Dunmarch")
	_check("the_brief_carries_the_standing_tokens_verbatim_and_uninterpreted",
		Array((brief["region"] as Dictionary)["standing_buildings"]) == ["LW_FORT"]
			and not bool((brief["region"] as Dictionary)["has_fort"])
			and int((brief["settings"] as Dictionary)["starting_cash"]) == 6000,
		str(brief.get("region", {})))


# --- the version 3 brief: bonuses and heroes -------------------------------------

func _test_handoff_carries_bonuses_and_heroes() -> void:
	var state := _state()
	_check("a_regions_territory_membership_is_derivable",
		String(state.world.territory_of("Ashfall").get("effect_name", "")) == "TestTerritory"
			and state.world.territory_of("Emberisle").is_empty())
	var hero_id := int(state.armies_in_region("Ashfall")[0])
	state.move_army(hero_id, "Bramblewold")
	var brief := HandoffScript.build_request(state.world, state, 0, "Cinderfen")
	_check("the_brief_is_version_3", int(brief.get("schema_version", -1)) == 3)
	var attacker := brief["attacker"] as Dictionary
	# Player 0 holds every region of TestTerritory, so the unification and its
	# summed bonus must ride the attacking side.
	_check("the_attackers_unified_territory_bonuses_ride_the_brief",
		Array(attacker["unified_territories"]) == ["TestTerritory"]
			and (attacker["territory_bonuses"] as Dictionary) == {"experience": 20},
		str(attacker.get("territory_bonuses", {})))
	var defender := brief["defender"] as Dictionary
	_check("a_side_with_no_unification_carries_empty_totals_not_defaults",
		(defender["unified_territories"] as PackedStringArray).is_empty()
			and (defender["territory_bonuses"] as Dictionary).is_empty())
	_check("the_hero_army_carries_its_campaign_level",
		int((attacker["armies"][0] as Dictionary)["hero_level"]) == 1
			and String((attacker["armies"][0] as Dictionary)["hero_template"]) == "TestHero",
		str(attacker.get("armies", [])))
	_check("the_regions_own_bonuses_and_macros_still_ride",
		(brief["region"] as Dictionary)["bonuses"] == {"experience": 5}
			and ((brief["region"] as Dictionary)["bonus_macros"] as Dictionary).is_empty())


# --- victory conditions ----------------------------------------------------------

func _test_victory_conditions() -> void:
	var seats: Array = [
		{"template": "PlayerAlpha", "team": 1},
		{"template": "PlayerBeta", "team": 2},
	]
	var state := _state()
	var fresh := state.evaluate_victory()
	_check("a_fresh_board_ends_nothing",
		bool(fresh["ok"])
			and (fresh["defeated_players"] as PackedInt32Array).is_empty()
			and int(fresh["victorious_team"]) == StateScript.NEUTRAL,
		str(fresh))
	_check("the_capital_is_recorded_on_the_seat",
		String((state.players[0] as Dictionary).get("capital", "")) == "Ashfall"
			and String((state.players[1] as Dictionary).get("capital", "")) == "Dunmarch")

	# ELIMINATION (type 0): a seat with no regions is defeated, and with only
	# one team left standing the campaign has a winner.
	state.transfer_region("Cinderfen", 0)
	state.transfer_region("Dunmarch", 0)
	var ended := state.evaluate_victory()
	_check("elimination_defeats_the_seat_with_no_regions",
		Array(ended["defeated_players"]) == [1], str(ended))
	_check("the_last_team_standing_wins_elimination",
		int(ended["victorious_team"]) == 1, str(ended))
	var applied := state.apply_victory()
	_check("apply_victory_marks_the_defeated_seat",
		Array(applied.get("applied", PackedInt32Array())) == [1]
			and bool((state.players[1] as Dictionary)["defeated"]),
		str(applied))

	# CAPITAL ASSAULT (type 1): losing the StartRegion alone is defeat...
	var capital := StateScript.new()
	capital.setup(_world(), seats, {"victory_type": 1})
	capital.apply_ownership_sets("TestScenario")
	capital.transfer_region("Dunmarch", 0)
	var fallen := capital.evaluate_victory()
	_check("losing_the_capital_defeats_its_seat_under_capital_assault",
		Array(fallen["defeated_players"]) == [1] and int(fallen["victorious_team"]) == 1,
		str(fallen))
	# ...and the SAME board under elimination defeats nobody, because the seat
	# still holds Cinderfen. The rule, not the board, decides.
	var elimination := StateScript.new()
	elimination.setup(_world(), seats, {"victory_type": 0})
	elimination.apply_ownership_sets("TestScenario")
	elimination.transfer_region("Dunmarch", 0)
	var spared := elimination.evaluate_victory()
	_check("the_same_board_under_elimination_defeats_nobody",
		(spared["defeated_players"] as PackedInt32Array).is_empty()
			and int(spared["victorious_team"]) == StateScript.NEUTRAL,
		str(spared))

	# TERRITORY LADDER (type 2): the team wins at the authored count with the
	# loser still on the board - victory without anybody's defeat.
	var ladder := StateScript.new()
	ladder.setup(_world(), seats, {"victory_type": 2})
	ladder.apply_ownership_sets("TestScenario")
	ladder.transfer_region("Cinderfen", 0)
	ladder.transfer_region("Emberisle", 0)
	var won := ladder.evaluate_victory()
	_check("the_territory_ladder_wins_at_the_authored_count",
		int(won["victorious_team"]) == 1
			and (won["defeated_players"] as PackedInt32Array).is_empty(),
		str(won))

	# A victory type the scenario never authored is refused BEFORE any ownership
	# moves, leaving the board exactly as it was found.
	var unauthored := StateScript.new()
	unauthored.setup(_world(), seats, {"victory_type": 9})
	_check("an_unauthored_victory_type_is_refused_before_ownership_applies",
		not unauthored.apply_ownership_sets("TestScenario")
			and unauthored.regions_owned_by(0).is_empty())

	# The rule is hashed: two campaigns differing ONLY in victory type must
	# hash apart, because they end under different conditions.
	_check("the_victory_rule_is_inside_the_strategic_hash",
		capital.state_hash() != elimination.state_hash())


# --- the strategic layer's own gap register --------------------------------------

## A ROSTER ROW THAT NAMES ITSELF IS REFUSED, and the refusal is visible.
##
## Shipped RotWK data carries exactly one: `livingworldbuildableunits.inc:781`
## redeclares `LivingWorldPlayerArmy DainPlayerArmy` with
## `ArmyEntry ThingTemplate = DainPlayerArmy` - the roster block's own name, which
## is not a thing template - shadowing the correct declaration in
## `livingworldstartingunits.inc:146` (`ThingTemplate = DwarvenDain`). Under plain
## last-one-wins the Dwarven `HeroArmy2` was raised with no auto-resolve units and
## every battle it entered refused BY NAME, which read exactly like "the Dwarven
## roster is missing from the bindings bundle". It is not: the bundle binds
## `DwarvenDain` and 23 other Dwarven objects.
##
## The fixture below reproduces the shape rather than the payload - a correct row
## followed by a self-referential redeclaration of the same name.
func _test_a_self_referential_roster_row_is_refused() -> void:
	var document := _document()
	(document["playerArmies"] as Array).append({
		"name": "TestHeroArmy",
		"displayNameTag": "LWA:TestHero",
		"entries": [{"thingTemplate": "TestHeroArmy", "quantity": 1}],
	})
	var world := WorldScript.new()
	_check("a_document_with_a_self_referential_roster_still_loads",
		world.load_from_dict(document, "TestCampaign"), str(world.errors))
	var roster := world.player_armies.get("TestHeroArmy", {}) as Dictionary
	_check("the_well_formed_declaration_stands",
		(roster.get("entries", []) as Array).size() == 1
			and String(((roster["entries"] as Array)[0] as Dictionary)["thing_template"]) == "TestHero",
		str(roster))
	_check("the_refusal_is_recorded_as_a_retail_data_defect",
		world.data_defects.size() == 1
			and String((world.data_defects[0] as Dictionary)["kind"]) == "self_referential_army_entry"
			and String((world.data_defects[0] as Dictionary)["subject"]) == "TestHeroArmy",
		str(world.data_defects))
	_check("a_clean_document_records_no_defects", _world().data_defects.is_empty())


func _test_strategic_gap_register() -> void:
	var names := GapsScript.names()
	_check("the_strategic_gap_register_names_what_retail_data_cannot_support",
		Array(names) == [
			"army_merging_and_splitting", "army_recruitment_and_cp_costs",
			"phase_moves_apply_immediately",
			"phase_timer",
			"pre_battle_retreat_losses",
			"retreat_distance_rule",
			"single_battle_per_phase",
			"strategic_ai_recruitment",
			"strategic_building_nuggets",
			"strategic_powers",
		], str(names))
	# TWO MORE FALSE GAPS ARE ASSERTED GONE, and they went for the same reason
	# `neutral_region_capture` and `strategic_ai_turns` did: each reasoned from the
	# importer's JSON document alone as though that were all retail ships.
	# `strategic_building_construction` said the `LW_*` macros were unexpandable -
	# they are `#define`s in `riskcampaign.ini`, above retail's own comment saying
	# how to read them, and `livingworldbuildings.ini` has carried all 28 blocks
	# into `living-world-ui.json` all along. `strategic_treasury_income` said no
	# rule combined the income ingredients - `BuildingNugget IncreaseTreasury`
	# binds them per building and `data/lotr.str` states the cadence and the sum.
	_check("the_two_false_construction_and_treasury_gaps_are_gone",
		not Array(names).has("strategic_building_construction")
			and not Array(names).has("strategic_treasury_income")
			and GapsScript.reason("strategic_building_construction").is_empty()
			and GapsScript.reason("strategic_treasury_income").is_empty())
	# `neutral_region_capture` IS ASSERTED GONE, for the same reason
	# `strategic_ai_turns` is below: it concluded "the document records no
	# non-battle claim rule" from the importer's JSON alone, and the rule is in
	# `data/lotr.str`. The claim is implemented; a register that kept the entry
	# would be telling a reader the primary verb of the campaign does not work.
	_check("the_false_neutral_region_capture_gap_is_gone",
		not Array(names).has("neutral_region_capture")
			and GapsScript.reason("neutral_region_capture").is_empty())
	# WHAT THIS PROJECT AUTHORED ABOUT THE CLAIM IS NAMED, so no part of it can
	# pass for parity. Retail says the claim happens and who may make it; it does
	# not say which army marches, what it costs, or how a same-turn collision
	# resolves under a one-action turn.
	var authored := GapsScript.authored_rule_names()
	_check("the_project_authored_strategic_rules_are_named",
		Array(authored) == [
			"demolition_refunds_nothing",
			"lowest_free_build_plot",
			"neutral_claim_army_selection",
			"neutral_claim_costs_nothing",
			"one_structure_per_territory_per_turn",
			"phase_moves_apply_immediately",
			"phase_timer",
			"pre_battle_retreat_losses",
			"retreat_ai_order_tie_break",
			"retreat_cap_overflow_capital",
			"retreat_capital_unevidenced_destroys",
			"retreat_distance_rule",
			"single_battle_per_phase",
			"strategic_treasury_income_arithmetic",
		], str(authored))
	# THE INCOME ARITHMETIC IS THE ONE A PLAYER SEES A NUMBER FOR, so its stated
	# reason has to name all three addends rather than gesture at "the economy".
	var income_rule := GapsScript.authored_rule_reason("strategic_treasury_income_arithmetic")
	_check("the_authored_income_rule_names_all_three_retail_addends",
		income_rule.contains("GAIN_PER_FORTRESS")
			and income_rule.contains("GAIN_PER_FARM")
			and income_rule.contains("FERTILE_TERRITORY_BONUS"), income_rule)
	var authored_reasons_hold := true
	for name in authored:
		if GapsScript.authored_rule_reason(String(name)).is_empty():
			authored_reasons_hold = false
	_check("every_project_authored_rule_states_its_reason", authored_reasons_hold)
	# THE CLOSED GAP IS ASSERTED GONE, not merely replaced. `strategic_ai_turns`
	# justified shipping no opponent with "the document carries no AI decision
	# data", which was false of retail: `livingworldaitemplate.ini` carries
	# twenty authored weights. A register that only ever grows is a register
	# nobody believes, and a FALSE entry left in one is worse than none.
	_check("the_false_strategic_ai_turns_gap_is_gone",
		not Array(names).has("strategic_ai_turns")
			and GapsScript.reason("strategic_ai_turns").is_empty())
	# The surviving AI gap must name the weights it cannot spend, so a reader
	# learns WHICH part of retail's template is unmodelled rather than being told
	# "AI" and left to find out.
	#
	# RESTATED, NOT DELETED, and the restatement pins what closed. This assertion
	# used to require the reason to name `BuildingScore` and
	# `BonusPreferenceTreasury` as UNSPENDABLE. Both are spent now - the four
	# `BuildingScore*` weights choose what the opponent builds, and
	# `BonusPreferenceTreasury` names `fertileTerritory` (retail's own display
	# string for that field is "+%d Treasure") - so the old expectation no longer
	# holds, and the check now pins the narrower truth: what remains unspent is
	# recruitment, and the reason must say which weights those are and point at the
	# gap that explains why.
	var ai_reason := GapsScript.reason("strategic_ai_recruitment")
	_check("the_surviving_ai_gap_names_the_unspent_retail_weights",
		ai_reason.contains("Desired")
			and ai_reason.contains("BuildingScore")
			and ai_reason.contains("army_recruitment_and_cp_costs"), ai_reason)
	var reasons_hold := true
	for name in names:
		if GapsScript.reason(String(name)).is_empty():
			reasons_hold = false
	_check("every_registered_gap_states_its_reason", reasons_hold)


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

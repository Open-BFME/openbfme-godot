extends SceneTree

## The strategic->tactical handoff, END TO END and for real.
##
## `wotr_strategic_runner.gd` proves the strategic layer is self-consistent. It
## proves nothing about what happens when something calls it, which until now
## nothing did. This runner drives the whole chain with a REAL `RetailSliceSim`:
##
##   1. a strategic state produces a handoff brief
##   2. the brief configures a tactical match (team roster + rules overlay)
##   3. the commitment enters the strategic hash boundary
##   4. the tactical match RUNS and decides a winner
##   5. the winner returns and changes strategic state
##
## Each leg is asserted SEPARATELY, because any one of them can pass while the
## chain is broken - a translation that drops a field still returns a dictionary,
## and a result that is never applied still leaves a legal state behind.
##
## The world fixture is the same authored document `wotr_strategic_runner.gd`
## uses (invented region names, importer document SHAPE, no retail payload). The
## tactical harness rules mirror `retail_state_pin_runner.gd`'s.

const WorldScript = preload("res://src/wotr/wotr_world.gd")
const StateScript = preload("res://src/wotr/wotr_state.gd")
const HandoffScript = preload("res://src/wotr/wotr_handoff.gd")
const BattleScript = preload("res://src/wotr/wotr_battle.gd")
const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")

## The binding the strategic layer does not carry and this runner must supply.
## `FactionAlpha`/`FactionBeta` are LivingWorld player-template faction names;
## `men`/`mordor` are pack faction ids. See the gap note in `wotr_battle.gd`.
const FACTION_BINDINGS := {
	"FactionAlpha": "men",
	"FactionBeta": "mordor",
}

const ATTACKER := 0
const DEFENDER := 1
const TARGET_REGION := "Cinderfen"

## LIVENESS. A GDScript runtime error aborts the enclosing function without ever
## reaching a `_check`, so a runner that only counts failures reports GREEN when
## its fixture collapses - which is exactly what this file did on its first run
## (`passed=4 failed=0`, with eleven script errors above it). The expected count
## makes an inert run impossible to mistake for a passing one. Raise it
## deliberately when tests are added; never lower it to make a run go green.
const EXPECTED_CHECKS := 71

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_team_constants_match_the_simulation()
	_test_brief_translates()
	_test_translation_refuses_rather_than_guesses()
	_test_commitment_is_inside_the_hash_boundary()
	_test_snapshot_carries_the_battle()
	_test_end_to_end_attacker_wins()
	_test_end_to_end_defender_wins()
	_test_outcome_refuses_what_it_cannot_apply()
	_finish()


# --- leg 0: the two layers agree what a team id means ------------------------

func _test_team_constants_match_the_simulation() -> void:
	## `wotr_battle.gd` deliberately does NOT preload the tactical sim, so its
	## team constants are literals. That keeps the strategic layer loadable with
	## no tactical simulation in existence - but it also means nothing in the
	## source checks the correspondence. This test is that check. If either side
	## renumbers its teams, the bridge would otherwise map the winner to the
	## wrong seat, silently and in the right-looking direction half the time.
	_check("attacker_team_is_the_simulations_player_team",
		BattleScript.ATTACKER_TEAM == SimScript.PLAYER_TEAM,
		"%d vs %d" % [BattleScript.ATTACKER_TEAM, SimScript.PLAYER_TEAM])
	_check("defender_team_is_the_simulations_enemy_team",
		BattleScript.DEFENDER_TEAM == SimScript.ENEMY_TEAM,
		"%d vs %d" % [BattleScript.DEFENDER_TEAM, SimScript.ENEMY_TEAM])
	_check("undecided_matches_the_simulations_fresh_winner",
		BattleScript.UNDECIDED == SimScript.new().winner)


# --- leg 1+2: brief -> tactical configuration --------------------------------

func _test_brief_translates() -> void:
	var state := _staged_state()
	var brief := HandoffScript.build_request(state.world, state, ATTACKER, TARGET_REGION)
	_check("a_staged_attack_produces_a_brief", not brief.is_empty())

	var configured: Dictionary = BattleScript.configure(brief, FACTION_BINDINGS, ATTACKER)
	_check("translation_succeeds", bool(configured["ok"]), str(configured["refusals"]))

	var roster: Array = configured["team_roster"]
	_check("roster_seats_exactly_two_teams", roster.size() == 2, str(roster))
	var attacker_seat := roster[0] as Dictionary
	var defender_seat := roster[1] as Dictionary
	_check("attacker_takes_team_zero", int(attacker_seat["team"]) == BattleScript.ATTACKER_TEAM)
	_check("defender_takes_team_one", int(defender_seat["team"]) == BattleScript.DEFENDER_TEAM)
	# The faction that reaches the sim must be the PACK id, not the strategic
	# name. Passing "FactionAlpha" through would make team_retail_side() refuse
	# on every faction gate - c930b68's exact failure, one layer up.
	_check("attacker_faction_is_bound_to_a_pack_faction_id",
		String(attacker_seat["faction"]) == "men", str(attacker_seat))
	_check("defender_faction_is_bound_to_a_pack_faction_id",
		String(defender_seat["faction"]) == "mordor", str(defender_seat))
	_check("the_human_seat_is_not_flagged_ai", not bool(attacker_seat["is_ai"]))
	_check("the_unplayed_seat_is_flagged_ai", bool(defender_seat["is_ai"]))

	# Cinderfen carries createAutoFort, so the brief must have chosen the
	# WITH-FORT purse (1000), not the open-field one (6000).
	var rules: Dictionary = configured["gameplay_rules"]
	_check("starting_cash_reaches_the_rules_overlay",
		int(rules.get("starting_resources", -1)) == 1000, str(rules))
	# The overlay carries ONLY what the strategic layer authorises. A unit_rules
	# or faction_manifest key appearing here would mean this file had started
	# describing units, which it has no source for.
	_check("the_overlay_carries_nothing_it_cannot_source",
		rules.keys() == ["starting_resources"], str(rules.keys()))

	var commitment: Dictionary = configured["commitment"]
	_check("commitment_names_the_contested_region",
		String(commitment["region"]) == TARGET_REGION)
	_check("commitment_records_both_seats",
		int(commitment["attacker"]) == ATTACKER and int(commitment["defender"]) == DEFENDER)
	_check("commitment_carries_the_attacking_armies",
		Array(commitment["committed_armies"] as PackedInt32Array) == Array(state.armies_in_region("Bramblewold")),
		str(commitment["committed_armies"]))
	_check("commitment_digests_the_brief_it_came_from",
		BattleScript.commitment_matches_brief(commitment, brief))

	# The digest is the chain link between the two hashes. A brief that differs
	# anywhere must fail the match, or the link carries no information.
	var altered := brief.duplicate(true)
	(altered["settings"] as Dictionary)["starting_cash"] = 999
	_check("a_different_brief_fails_the_digest_match",
		not BattleScript.commitment_matches_brief(commitment, altered))


func _test_translation_refuses_rather_than_guesses() -> void:
	var state := _staged_state()
	var brief := HandoffScript.build_request(state.world, state, ATTACKER, TARGET_REGION)

	# An unbound faction must REFUSE BY NAME. It must never fall through to
	# passing the strategic name along as though it were a pack id.
	var unbound: Dictionary = BattleScript.configure(brief, {"FactionAlpha": "men"}, ATTACKER)
	_check("an_unbound_faction_refuses", not bool(unbound["ok"]))
	_check("the_refusal_names_the_unbound_faction",
		_refusal_mentions(unbound, "FactionBeta"), str(unbound["refusals"]))
	_check("a_refused_translation_yields_no_partial_configuration",
		(unbound["team_roster"] as Array).is_empty()
			and (unbound["gameplay_rules"] as Dictionary).is_empty()
			and (unbound["commitment"] as Dictionary).is_empty())

	# An unowned region has no defending side: a named boundary, not a hole.
	var neutral_state := _staged_state()
	neutral_state.transfer_region(TARGET_REGION, StateScript.NEUTRAL)
	var neutral_brief := HandoffScript.build_request(
		neutral_state.world, neutral_state, ATTACKER, TARGET_REGION)
	_check("attacking_an_unowned_region_still_produces_a_brief",
		not neutral_brief.is_empty())
	var neutral: Dictionary = BattleScript.configure(neutral_brief, FACTION_BINDINGS, ATTACKER)
	_check("an_unowned_region_refuses_a_tactical_match", not bool(neutral["ok"]))
	_check("the_refusal_explains_the_missing_defending_side",
		_refusal_mentions(neutral, "unowned"), str(neutral["refusals"]))

	_check("an_empty_brief_refuses",
		not bool((BattleScript.configure({}, FACTION_BINDINGS) as Dictionary)["ok"]))

	# UNAUTHORED IS NOT ZERO. `-1` is the world reader's "this document did not
	# say" sentinel. Forwarding it clamps to 0 in `_apply_gameplay_rules` and
	# hands both armies an empty purse nobody authored - a fabricated match
	# parameter that looks like a real one. Absent must stay absent so the sim's
	# own default stands. (This test exists because a mutation that removed the
	# guard initially survived the whole suite.)
	var silent := _document()
	var settings := silent["rtsSettings"] as Dictionary
	settings["startingCashRts"] = -1
	settings["startingCashRtsWithFort"] = -1
	var silent_world := WorldScript.new()
	silent_world.load_from_dict(silent, "TestCampaign")
	var silent_state := _staged_state_in(silent_world)
	var silent_brief := HandoffScript.build_request(
		silent_world, silent_state, ATTACKER, TARGET_REGION)
	_check("an_unauthored_purse_still_produces_a_brief", not silent_brief.is_empty())
	_check("the_brief_reports_the_purse_as_unauthored",
		int((silent_brief["settings"] as Dictionary)["starting_cash"]) == -1)
	var silent_config: Dictionary = BattleScript.configure(
		silent_brief, FACTION_BINDINGS, ATTACKER)
	_check("an_unauthored_purse_still_configures_a_match", bool(silent_config["ok"]),
		str(silent_config["refusals"]))
	_check("an_unauthored_purse_is_omitted_rather_than_sent_as_minus_one",
		not (silent_config["gameplay_rules"] as Dictionary).has("starting_resources"),
		str(silent_config["gameplay_rules"]))


# --- leg 3: the boundary -----------------------------------------------------

func _test_commitment_is_inside_the_hash_boundary() -> void:
	var state := _staged_state()
	var commitment := _commitment_for(state)

	var before := state.state_hash()
	_check("a_battle_begins", state.begin_battle(commitment))
	var during := state.state_hash()
	# If the commitment were outside the hash, two peers - one mid-battle, one
	# not - would agree on their state hash and then apply the same tactical
	# result to different strategic states. That is 87cf636's defect exactly.
	_check("a_battle_in_flight_changes_the_state_hash", during != before)

	_check("clearing_the_battle_reports_true", state.clear_battle())
	_check("a_resolved_battle_hashes_back_to_pristine",
		state.state_hash() == before,
		"%s vs %s" % [state.state_hash(), before])

	# Empty-is-absent, checked at the dictionary rather than only through the
	# hash: an empty record that still occupied a key would hash differently
	# from one that had never existed.
	_check("no_battle_contributes_no_key",
		not state.authoritative_state().has("pending_battle"))
	state.begin_battle(commitment)
	_check("a_battle_in_flight_contributes_its_key",
		state.authoritative_state().has("pending_battle"))

	# One battle at a time. Overwriting would strand the first battle's
	# committed armies with no symptom until the roster was counted.
	_check("a_second_battle_is_refused_not_overwritten",
		not state.begin_battle(commitment))
	_check("the_first_battle_survives_the_refusal",
		String(state.pending_battle["region"]) == TARGET_REGION)

	# setup() clears it, so the player-reachable restart path cannot carry a
	# dead battle into a fresh campaign.
	state.setup(state.world, [{"template": "PlayerAlpha", "team": 1}])
	_check("setup_clears_the_battle_in_flight", state.pending_battle.is_empty())


func _test_snapshot_carries_the_battle() -> void:
	var minter := _staged_state()
	minter.begin_battle(_commitment_for(minter))
	var bytes := minter.snapshot()

	var adopter := _staged_state()
	_check("an_adopter_restores_a_snapshot_taken_mid_battle", adopter.restore(bytes))
	_check("the_adopter_knows_a_battle_is_in_flight",
		String(adopter.pending_battle.get("region", "")) == TARGET_REGION,
		str(adopter.pending_battle))
	_check("the_adopter_hashes_equal_to_the_minter",
		adopter.state_hash() == minter.state_hash())
	# The adopter must be able to finish the minter's battle, not just describe
	# it: the committed army ids have to survive the round trip intact.
	_check("the_committed_armies_survive_the_round_trip",
		Array(adopter.pending_battle["committed_armies"] as PackedInt32Array)
			== Array(minter.pending_battle["committed_armies"] as PackedInt32Array))

	# The STALE-CARRY case, which is the one that fails silently: restoring a
	# between-battles snapshot into a state that has a battle in flight must
	# clear it. Absence in the snapshot means "no battle", not "leave yours".
	var peaceful := _staged_state().snapshot()
	var busy := _staged_state()
	busy.begin_battle(_commitment_for(busy))
	_check("restoring_a_peaceful_snapshot_succeeds", busy.restore(peaceful))
	_check("restoring_a_peaceful_snapshot_clears_a_stale_battle",
		busy.pending_battle.is_empty(), str(busy.pending_battle))


# --- legs 4+5: the tactical match runs, and its result returns ---------------

func _test_end_to_end_attacker_wins() -> void:
	var state := _staged_state()
	var brief := HandoffScript.build_request(state.world, state, ATTACKER, TARGET_REGION)
	var configured: Dictionary = BattleScript.configure(brief, FACTION_BINDINGS, ATTACKER)
	var committed := (configured["commitment"] as Dictionary)["committed_armies"] as PackedInt32Array
	state.begin_battle(configured["commitment"])

	_check("e2e_attacker_the_region_starts_in_defender_hands",
		state.owner_of(TARGET_REGION) == DEFENDER)

	var sim := _tactical_match(configured)
	# LEG 4, asserted on its own: the match the brief configured actually ran.
	_check("e2e_attacker_the_rules_overlay_reached_the_simulation",
		int(sim.team_resources[BattleScript.ATTACKER_TEAM]) == 1000,
		str(sim.team_resources))
	_check("e2e_attacker_the_faction_binding_reached_the_simulation",
		String((sim.team_descriptor(BattleScript.ATTACKER_TEAM) as Dictionary).get("faction", "")) == "men")
	_decide(sim, BattleScript.DEFENDER_TEAM)
	_check("e2e_attacker_the_tactical_match_decided",
		sim.winner == BattleScript.ATTACKER_TEAM, "winner=%d" % sim.winner)

	# LEG 5, asserted on its own: the result came back and MOVED something.
	var outcome: Dictionary = BattleScript.apply_outcome(state, sim.winner)
	_check("e2e_attacker_the_outcome_applies", bool(outcome["ok"]), str(outcome["refusals"]))
	_check("e2e_attacker_the_outcome_names_the_attacking_seat",
		int(outcome["winner_player"]) == ATTACKER)
	_check("e2e_attacker_the_region_changed_hands",
		state.owner_of(TARGET_REGION) == ATTACKER)
	_check("e2e_attacker_the_committed_army_advanced",
		Array(outcome["armies_advanced"] as PackedInt32Array) == Array(committed),
		str(outcome["armies_advanced"]))
	_check("e2e_attacker_the_army_now_stands_in_the_captured_region",
		Array(state.armies_in_region(TARGET_REGION)).has(int(committed[0])))
	_check("e2e_attacker_the_transaction_closed", state.pending_battle.is_empty())


func _test_end_to_end_defender_wins() -> void:
	var state := _staged_state()
	var brief := HandoffScript.build_request(state.world, state, ATTACKER, TARGET_REGION)
	var configured: Dictionary = BattleScript.configure(brief, FACTION_BINDINGS, ATTACKER)
	var committed := (configured["commitment"] as Dictionary)["committed_armies"] as PackedInt32Array
	state.begin_battle(configured["commitment"])

	var sim := _tactical_match(configured)
	_decide(sim, BattleScript.ATTACKER_TEAM)
	_check("e2e_defender_the_tactical_match_decided",
		sim.winner == BattleScript.DEFENDER_TEAM, "winner=%d" % sim.winner)

	var outcome: Dictionary = BattleScript.apply_outcome(state, sim.winner)
	_check("e2e_defender_the_outcome_applies", bool(outcome["ok"]), str(outcome["refusals"]))
	_check("e2e_defender_the_outcome_names_the_defending_seat",
		int(outcome["winner_player"]) == DEFENDER)
	_check("e2e_defender_the_region_did_not_change_hands",
		state.owner_of(TARGET_REGION) == DEFENDER)
	_check("e2e_defender_the_committed_army_was_destroyed",
		Array(outcome["armies_lost"] as PackedInt32Array) == Array(committed),
		str(outcome["armies_lost"]))
	_check("e2e_defender_the_destroyed_army_is_gone_from_the_world",
		not state.armies.has(int(committed[0])))
	_check("e2e_defender_the_transaction_closed", state.pending_battle.is_empty())


func _test_outcome_refuses_what_it_cannot_apply() -> void:
	var state := _staged_state()

	_check("applying_with_no_battle_in_flight_refuses",
		not bool((BattleScript.apply_outcome(state, 0) as Dictionary)["ok"]))

	state.begin_battle(_commitment_for(state))
	# An UNDECIDED match read one tick early. Without the guard this takes the
	# "defender won" branch and destroys the attacker's army for nothing.
	var undecided: Dictionary = BattleScript.apply_outcome(state, BattleScript.UNDECIDED)
	_check("an_undecided_match_refuses", not bool(undecided["ok"]))
	_check("the_undecided_refusal_says_so",
		_refusal_mentions(undecided, "undecided"), str(undecided["refusals"]))
	_check("a_refused_outcome_leaves_the_battle_in_flight",
		not state.pending_battle.is_empty())
	_check("a_refused_outcome_destroys_nothing",
		state.armies.has(int((state.pending_battle["committed_armies"] as PackedInt32Array)[0])))

	# A team that is not a side of this battle (a third seat, a creep team).
	var foreign: Dictionary = BattleScript.apply_outcome(state, 7)
	_check("a_team_outside_the_battle_refuses", not bool(foreign["ok"]))
	_check("the_foreign_team_refusal_names_the_team",
		_refusal_mentions(foreign, "7"), str(foreign["refusals"]))

	# Applying the same result twice must not capture twice.
	_check("the_first_application_succeeds",
		bool((BattleScript.apply_outcome(state, BattleScript.ATTACKER_TEAM) as Dictionary)["ok"]))
	_check("the_second_application_refuses",
		not bool((BattleScript.apply_outcome(state, BattleScript.ATTACKER_TEAM) as Dictionary)["ok"]))


# --- strategic fixture -------------------------------------------------------

## PlayerAlpha (seat 0) holds Ashfall+Bramblewold with a hero army; PlayerBeta
## (seat 1) holds Cinderfen+Dunmarch. The hero army is walked forward into
## Bramblewold so that seat 0 can legally stage an attack on Cinderfen, and a
## garrison is placed in Cinderfen so the defence is a real one.
func _staged_state() -> StateScript:
	return _staged_state_in(_world())


func _staged_state_in(world: WorldScript) -> StateScript:
	var state := StateScript.new()
	state.setup(world, [
		{"template": "PlayerAlpha", "team": 1},
		{"template": "PlayerBeta", "team": 2},
	])
	state.apply_ownership_sets("TestScenario")
	var hero := state.armies_in_region("Ashfall")
	if hero.is_empty() or not state.move_army(int(hero[0]), "Bramblewold"):
		printerr("WOTR_BATTLE FAIL fixture could not stage the attacking army")
	if state.place_army(DEFENDER, TARGET_REGION, "GarrisonArmy1") < 0:
		printerr("WOTR_BATTLE FAIL fixture could not place the defending garrison")
	return state


func _commitment_for(state: StateScript) -> Dictionary:
	var brief := HandoffScript.build_request(state.world, state, ATTACKER, TARGET_REGION)
	return (BattleScript.configure(brief, FACTION_BINDINGS, ATTACKER) as Dictionary)["commitment"]


func _world() -> WorldScript:
	var world := WorldScript.new()
	if not world.load_from_dict(_document(), "TestCampaign"):
		printerr("WOTR_BATTLE FAIL fixture failed to load: %s" % str(world.errors))
	return world


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
		"createAutoFort": id == TARGET_REGION,
		"customCenterPoint": true,
		"centerPoint": {"x": 0, "y": 0},
		"heroArmySpots": [],
		"garrisonArmySpots": [],
		"buildingSpots": [],
		"fortress": null,
		"connections": connections,
		"restrictBuildings": [],
	}


func _document() -> Dictionary:
	return {
		"format": 1,
		"schema": WorldScript.SCHEMA,
		"schemaVersion": WorldScript.SCHEMA_VERSION,
		"game": "bfme2",
		"sources": [],
		"regionCampaigns": [
			{
				"name": "TestCampaign",
				"kind": "LivingWorldRegionCampaign",
				"regionEffectsManagerName": "TestRegionEffects",
				"regions": [
					_region("Ashfall", ["Bramblewold"], 600),
					_region("Bramblewold", ["Ashfall", "Cinderfen"], 480),
					_region("Cinderfen", ["Bramblewold", "Dunmarch"], 360),
					_region("Dunmarch", ["Cinderfen"], -1),
				],
				"territoryBonuses": [],
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
				"maxPlayers": 2,
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
		"gaps": [],
	}


# --- tactical harness --------------------------------------------------------

## A real `RetailSliceSim` configured from the brief. Map geometry is the sim's
## own fallback, NOT anything the brief supplied - that is the boundary this
## packet stops at, and the test is honest about it rather than pretending the
## region's `map_name` loaded a map.
func _tactical_match(configured: Dictionary) -> SimScript:
	var sim := SimScript.new()
	var rules := _harness_rules()
	rules.merge(configured["gameplay_rules"] as Dictionary, true)
	sim._rules = rules
	sim.configure_team_roster(configured["team_roster"] as Array)
	sim.setup({}, {})
	sim.ai_enabled = false
	return sim


## Raze `losing_team`'s fortress and step one tick, which is how the base-loop
## victory rule resolves. Deterministic and immediate: nothing here depends on
## two AI armies happening to find each other.
func _decide(sim: SimScript, losing_team: int) -> void:
	var fortress: int = sim.fortress_id(losing_team)
	if fortress == 0:
		printerr("WOTR_BATTLE FAIL harness seeded no fortress for team %d" % losing_team)
		return
	sim._apply_structure_damage(1 - losing_team, fortress, 999999)
	sim.tick()


func _harness_rules() -> Dictionary:
	return {
		"enable_base_loop": true,
		"starting_resources": 10000,
		"ai_attack_delay_ticks": 100000,
		"unit_rules": {
			SimScript.SOLDIER_OBJECT_ID: _unit_rule(SimScript.SOLDIER_HORDE_ID, false),
			SimScript.ARCHER_OBJECT_ID: _unit_rule(SimScript.ARCHER_OBJECT_ID, false),
			SimScript.TOWER_GUARD_OBJECT_ID: _unit_rule(SimScript.TOWER_GUARD_OBJECT_ID, false),
			SimScript.KNIGHT_OBJECT_ID: _unit_rule(SimScript.KNIGHT_OBJECT_ID, false),
			SimScript.BUILDER_OBJECT_ID: _unit_rule(SimScript.BUILDER_OBJECT_ID, true),
		},
	}


func _unit_rule(horde_id: String, is_builder: bool) -> Dictionary:
	return {
		"horde_id": horde_id,
		"speed": 1.0,
		"speed_source": 10.0,
		"acceleration": 1.0,
		"acceleration_source": 10.0,
		"turn_rate_degrees_per_second": 180.0,
		"braking": 1.0,
		"braking_source": 10.0,
		"attack_range": 1.15,
		"attack_range_source": 11.5,
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
		"is_builder": is_builder,
	}


# --- harness -----------------------------------------------------------------

func _refusal_mentions(result: Dictionary, needle: String) -> bool:
	for reason in result["refusals"] as PackedStringArray:
		if String(reason).contains(needle):
			return true
	return false


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("WOTR_BATTLE PASS %s" % name)
	else:
		failed += 1
		printerr("WOTR_BATTLE FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("WOTR_BATTLE FAIL liveness: ran %d checks, expected %d - a function aborted before its assertions" % [ran, EXPECTED_CHECKS])
	print("WOTR_BATTLE_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)

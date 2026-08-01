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
const ReinforcementsScript = preload("res://src/wotr/wotr_battle_reinforcements.gd")
const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")

## The binding the strategic layer does not carry and this runner must supply.
## `FactionAlpha`/`FactionBeta` are LivingWorld player-template faction names;
## `men`/`mordor` are pack faction ids. See the gap note in `wotr_battle.gd`.
const FACTION_BINDINGS := {
	"FactionAlpha": "men",
	"FactionBeta": "mordor",
}

## The binding the strategic layer does not carry either: a region's authored
## `MAP WOR *` map name to a PACK map id. No retail WOTR region map is cooked in
## any pack, so every binding is a stated stand-in - see `_bind_battlefield` in
## `wotr_battle.gd`. The fixture's region maps are `MAP TEST <region>`.
const MAP_BINDINGS := {
	"MAP TEST Ashfall": "bfme2.map.rivendell",
	"MAP TEST Bramblewold": "bfme2.map.rivendell",
	"MAP TEST Cinderfen": "bfme2.map.dagorlad",
	"MAP TEST Dunmarch": "bfme2.map.mordor",
	"MAP TEST Emberisle": "bfme2.map.mount-doom",
	"MAP TEST Loneheath": "bfme2.map.mount-doom",
}

const ATTACKER := 0
const DEFENDER := 1
const TARGET_REGION := "Cinderfen"

## Auto-resolve-shaped unit records for the fixture rosters, the way the
## session's binding bundle would produce them. The armies the fixture places
## therefore carry REAL unit records, which the staged reinforcement feed and
## the fought-outcome survivor contract both require - an army with no unit
## records refuses by name, exactly as auto-resolve refuses one. Integer
## thousandths throughout, because these records enter the strategic hash.
const ROSTER_UNITS := {
	"TestHeroArmy": [
		{"template": "TestHero", "unit_type": "AutoResolveUnit_Hero",
			"body": "TestHeroBody", "combat_chain": "TestChain", "leadership": "",
			"armor": "TestHeroArmor", "weapon": "TestHeroWeapon", "level": 1,
			"hitpoints_milli": 800000, "max_hitpoints_milli": 800000},
	],
	"TestGarrisonArmy": [
		{"template": "TestArcherHorde", "unit_type": "AutoResolveUnit_Archer",
			"body": "TestArcherBody", "combat_chain": "TestChain", "leadership": "",
			"armor": "TestArcherArmor", "weapon": "TestArcherWeapon", "level": 1,
			"hitpoints_milli": 300000, "max_hitpoints_milli": 300000},
		{"template": "TestArcherHorde", "unit_type": "AutoResolveUnit_Archer",
			"body": "TestArcherBody", "combat_chain": "TestChain", "leadership": "",
			"armor": "TestArcherArmor", "weapon": "TestArcherWeapon", "level": 1,
			"hitpoints_milli": 300000, "max_hitpoints_milli": 300000},
		{"template": "TestFighterHorde", "unit_type": "AutoResolveUnit_Soldier",
			"body": "TestFighterBody", "combat_chain": "TestChain", "leadership": "",
			"armor": "TestFighterArmor", "weapon": "TestFighterWeapon", "level": 1,
			"hitpoints_milli": 480000, "max_hitpoints_milli": 480000},
	],
}

## LIVENESS. A GDScript runtime error aborts the enclosing function without ever
## reaching a `_check`, so a runner that only counts failures reports GREEN when
## its fixture collapses - which is exactly what this file did on its first run
## (`passed=4 failed=0`, with eleven script errors above it). The expected count
## makes an inert run impossible to mistake for a passing one. Raise it
## deliberately when tests are added; never lower it to make a run go green.
const EXPECTED_CHECKS := 165

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
	_test_nothing_outside_the_chain_reaches_the_tactical_configuration()
	_test_a_refused_restore_changes_nothing()
	_test_begin_battle_validates_the_commitment()
	_test_the_defeated_garrison_does_not_survive_the_capture()
	_test_a_partial_advance_is_reported_not_hidden()
	_test_the_reinforcement_feed_is_staged_from_retails_cadence()
	_test_a_fought_battle_reports_the_surviving_roster()
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

	var configured: Dictionary = BattleScript.configure(brief, FACTION_BINDINGS, MAP_BINDINGS)
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
	# The overlay carries ONLY what the strategic layer authorises - and now
	# EVERYTHING it authorises: the purse, the region's standing fort, and the
	# region's own authored bonuses. A unit_rules or faction_manifest key
	# appearing here would mean this file had started describing units, which
	# it has no source for; a fourth authorised key must be added to this exact
	# list deliberately, never discovered in it.
	_check("the_overlay_carries_exactly_what_the_strategic_layer_authorises",
		rules.keys() == ["starting_resources", "prebuilt_fortress", "region_bonuses"],
		str(rules.keys()))
	# Cinderfen carries createAutoFort, so the strategic layer authorises a
	# standing fortress - the same fact the WITH-FORT purse above answered to.
	_check("the_with_fort_region_reaches_the_overlay_as_a_prebuilt_fortress",
		bool(rules.get("prebuilt_fortress", false)))
	# The fixture region authors {"experience": 5}; the overlay carries it
	# VERBATIM. What a tactical rule does with an experience bonus is the
	# tactical half of the region_bonus_modifiers gap - nothing is turned into
	# a multiplier here.
	_check("the_regions_authored_bonuses_reach_the_overlay_verbatim",
		(rules.get("region_bonuses", {}) as Dictionary) == {"experience": 5},
		str(rules.get("region_bonuses", {})))

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
	var unbound: Dictionary = BattleScript.configure(brief, {"FactionAlpha": "men"}, MAP_BINDINGS)
	_check("an_unbound_faction_refuses", not bool(unbound["ok"]))
	_check("the_refusal_names_the_unbound_faction",
		_refusal_mentions(unbound, "FactionBeta"), str(unbound["refusals"]))
	_check("a_refused_translation_yields_no_partial_configuration",
		(unbound["team_roster"] as Array).is_empty()
			and (unbound["gameplay_rules"] as Dictionary).is_empty()
			and (unbound["commitment"] as Dictionary).is_empty())

	# UNOWNED GROUND IS NOT A BATTLE, and now it does not even reach here.
	#
	# `state.can_attack()` says no to an unowned region (it has no owning seat, so
	# no defending faction and no defending armies), so the handoff refuses to
	# build a brief for one at all. Retail does not fight for unowned ground
	# either - it is CLAIMED, by marching a hero army in; see
	# `CLAIM_RECORD_SCHEMA` in `wotr_state.gd` for the shipped strings and
	# `wotr_strategic_runner.gd` for the rule's own tests.
	var neutral_state := _staged_state()
	neutral_state.transfer_region(TARGET_REGION, StateScript.NEUTRAL)
	_check("no_brief_is_built_for_unowned_ground",
		HandoffScript.build_request(
			neutral_state.world, neutral_state, ATTACKER, TARGET_REGION).is_empty(),
		"unowned ground is claimed, not attacked")

	# THE BRIDGE STILL GUARDS THE BOUNDARY ITSELF, because a brief is a plain
	# dictionary and a caller could hand it one the handoff would never have
	# minted. A defenderless brief must refuse BY NAME rather than configure a
	# match with one side.
	var forged := HandoffScript.build_request(state.world, state, ATTACKER, TARGET_REGION)
	(forged["defender"] as Dictionary)["player"] = StateScript.NEUTRAL
	var neutral: Dictionary = BattleScript.configure(forged, FACTION_BINDINGS, MAP_BINDINGS)
	_check("an_unowned_region_refuses_a_tactical_match", not bool(neutral["ok"]))
	_check("the_refusal_explains_the_missing_defending_side",
		_refusal_mentions(neutral, "unowned"), str(neutral["refusals"]))

	_check("an_empty_brief_refuses",
		not bool((BattleScript.configure({}, FACTION_BINDINGS, MAP_BINDINGS) as Dictionary)["ok"]))

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
	var silent_config: Dictionary = BattleScript.configure(silent_brief, FACTION_BINDINGS, MAP_BINDINGS)
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
	var configured: Dictionary = BattleScript.configure(brief, FACTION_BINDINGS, MAP_BINDINGS)
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
	var configured: Dictionary = BattleScript.configure(brief, FACTION_BINDINGS, MAP_BINDINGS)
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


# --- the chain covers what configure() actually consumes ---------------------

## THE TWO-PEER TEST. `configure()` used to take three arguments and digest one,
## so two peers could hold identical strategic states, mint byte-identical
## commitments, agree on their strategic hashes - and still build tactical sims
## that hashed differently. That is e56a0d4's desync arriving through the
## mechanism built to prevent it.
##
## The invariant asserted here is the contrapositive, which is the testable
## direction: any input difference that can move the TACTICAL hash must already
## have moved the STRATEGIC hash. Both demonstrated axes get their own peer pair.
func _test_nothing_outside_the_chain_reaches_the_tactical_configuration() -> void:
	# STRUCTURAL: the roster is a pure projection of the commitment. Nothing can
	# reach the sim's team registry that a peer cannot read back out of the
	# hashed strategic record, because there is nowhere else for it to come from.
	var state := _staged_state()
	var brief := HandoffScript.build_request(state.world, state, ATTACKER, TARGET_REGION)
	var configured: Dictionary = BattleScript.configure(brief, FACTION_BINDINGS, MAP_BINDINGS)
	var commitment := configured["commitment"] as Dictionary
	_check("the_team_roster_is_a_pure_projection_of_the_commitment",
		(configured["team_roster"] as Array) == BattleScript.team_roster_for(commitment),
		str(configured["team_roster"]))

	# AXIS 1 - THE FACTION BINDING. Swapping the table sends opposite factions to
	# the sim. It used to mint a byte-identical commitment, so the strategic
	# hashes agreed while the tactical ones could not.
	var swapped_state := _staged_state()
	var swapped_brief := HandoffScript.build_request(
		swapped_state.world, swapped_state, ATTACKER, TARGET_REGION)
	var swapped: Dictionary = BattleScript.configure(
		swapped_brief, {"FactionAlpha": "mordor", "FactionBeta": "men"}, MAP_BINDINGS)
	_check("a_swapped_binding_still_configures_a_match", bool(swapped["ok"]),
		str(swapped["refusals"]))
	_check("a_swapped_binding_sends_a_different_faction_to_the_simulation",
		String(((swapped["team_roster"] as Array)[0] as Dictionary)["faction"])
			!= String(((configured["team_roster"] as Array)[0] as Dictionary)["faction"]))
	_check("a_swapped_binding_cannot_mint_the_same_commitment",
		StateScript.canonical_digest(swapped["commitment"])
			!= StateScript.canonical_digest(commitment),
		str(swapped["commitment"]))
	state.begin_battle(commitment)
	swapped_state.begin_battle(swapped["commitment"])
	_check("peers_whose_bindings_disagree_disagree_on_the_strategic_hash",
		state.state_hash() != swapped_state.state_hash(),
		"%s vs %s" % [state.state_hash(), swapped_state.state_hash()])

	# AXIS 1b - THE BATTLEFIELD. The ground a battle is fought on decides the
	# battle as surely as the factions on it, and no `MAP WOR *` map is cooked,
	# so the WOTR screen has to substitute one. A substitution chosen outside the
	# commitment and handed to the sim alongside the roster would be this same
	# defect a third time; the bound map is recorded, so a peer with a different
	# table disagrees on the STRATEGIC hash before a match exists.
	var reground_state := _staged_state()
	var reground_brief := HandoffScript.build_request(
		reground_state.world, reground_state, ATTACKER, TARGET_REGION)
	var reground: Dictionary = BattleScript.configure(
		reground_brief, FACTION_BINDINGS, {"MAP TEST Cinderfen": "bfme2.map.mordor"})
	_check("a_different_battlefield_binding_still_configures_a_match",
		bool(reground["ok"]), str(reground["refusals"]))
	_check("the_bound_battlefield_is_recorded_in_the_commitment",
		String((reground["commitment"] as Dictionary)["battlefield_map"]) == "bfme2.map.mordor",
		str(reground["commitment"]))
	_check("the_commitment_still_records_the_regions_own_map_name",
		String((reground["commitment"] as Dictionary)["map_name"]) == "MAP TEST Cinderfen",
		"the stand-in must never erase what it stood in for")
	_check("a_different_battlefield_cannot_mint_the_same_commitment",
		StateScript.canonical_digest(reground["commitment"])
			!= StateScript.canonical_digest(commitment))
	var reground_peer := _staged_state()
	reground_peer.begin_battle(reground["commitment"])
	_check("peers_whose_battlefields_disagree_disagree_on_the_strategic_hash",
		reground_peer.state_hash() != state.state_hash())
	# And an unbound region map REFUSES rather than picking a plausible map.
	var unground: Dictionary = BattleScript.configure(reground_brief, FACTION_BINDINGS, {})
	_check("an_unbound_region_map_refuses", not bool(unground["ok"]))
	_check("the_refusal_names_the_unbound_region_map",
		_refusal_mentions(unground, "MAP TEST Cinderfen"), str(unground["refusals"]))
	_check("an_unbound_region_map_yields_no_partial_configuration",
		(unground["commitment"] as Dictionary).is_empty()
			and (unground["team_roster"] as Array).is_empty())

	# AXIS 2 - WHO IS HUMAN. `is_ai` reaches `_seed_team_ai_state()`, which seeds
	# HASHED authoritative tactical state. It used to come from a `human_player`
	# argument - a per-session value the brief deliberately does not carry - so
	# two peers of the same match, each naming its own seat, diverged at tick 0.
	# It now comes from the seat's `controller`, which is strategic state.
	var human_attacker := _staged_state_seated(
		_world(), StateScript.CONTROLLER_HUMAN, StateScript.CONTROLLER_AI)
	var human_defender := _staged_state_seated(
		_world(), StateScript.CONTROLLER_AI, StateScript.CONTROLLER_HUMAN)
	_check("who_drives_a_seat_is_inside_the_strategic_hash",
		human_attacker.state_hash() != human_defender.state_hash(),
		"%s vs %s" % [human_attacker.state_hash(), human_defender.state_hash()])

	var attacker_config := _configured_for(human_attacker)
	var defender_config := _configured_for(human_defender)
	_check("the_human_seat_reaches_the_roster_as_not_ai",
		not bool(((attacker_config["team_roster"] as Array)[0] as Dictionary)["is_ai"]),
		str(attacker_config["team_roster"]))
	_check("the_ai_seat_reaches_the_roster_as_ai",
		bool(((attacker_config["team_roster"] as Array)[1] as Dictionary)["is_ai"]),
		str(attacker_config["team_roster"]))
	_check("moving_the_human_to_the_other_seat_moves_the_ai_flag_with_it",
		bool(((defender_config["team_roster"] as Array)[0] as Dictionary)["is_ai"])
			and not bool(((defender_config["team_roster"] as Array)[1] as Dictionary)["is_ai"]),
		str(defender_config["team_roster"]))
	_check("who_drives_a_seat_cannot_mint_the_same_commitment",
		StateScript.canonical_digest(attacker_config["commitment"])
			!= StateScript.canonical_digest(defender_config["commitment"]))

	# THE PROPERTY ITSELF, stated over real simulations: the tactical hashes of
	# these two configurations differ (25cf66b6... vs be06f4b1... in the original
	# reproduction), and the strategic hashes that authorised them differ too. A
	# peer can therefore detect the divergence BEFORE a match is built.
	var attacker_sim := _tactical_match(attacker_config)
	var defender_sim := _tactical_match(defender_config)
	_check("the_two_configurations_really_do_hash_differently_as_simulations",
		attacker_sim.state_hash() != defender_sim.state_hash())
	human_attacker.begin_battle(attacker_config["commitment"])
	human_defender.begin_battle(defender_config["commitment"])
	_check("diverging_simulations_were_authorised_by_diverging_strategic_hashes",
		human_attacker.state_hash() != human_defender.state_hash())

	# AND THE POSITIVE DIRECTION, which is what a live match relies on: two peers
	# that agree on strategic state build byte-identical tactical configurations
	# with no session argument to disagree about.
	var peer_a := _staged_state_seated(
		_world(), StateScript.CONTROLLER_HUMAN, StateScript.CONTROLLER_AI)
	var peer_b := _staged_state_seated(
		_world(), StateScript.CONTROLLER_HUMAN, StateScript.CONTROLLER_AI)
	_check("two_peers_of_the_same_match_agree_on_the_strategic_hash",
		peer_a.state_hash() == peer_b.state_hash())
	var config_a := _configured_for(peer_a)
	var config_b := _configured_for(peer_b)
	_check("agreeing_peers_mint_the_same_commitment",
		StateScript.canonical_digest(config_a["commitment"])
			== StateScript.canonical_digest(config_b["commitment"]))
	_check("agreeing_peers_configure_the_same_tactical_match",
		_tactical_match(config_a).state_hash() == _tactical_match(config_b).state_hash())


# --- a refused restore leaves nothing behind ---------------------------------

## `restore()` used to overwrite `turn_index`, `players`, `region_owner` and
## `armies` and THEN raise `Invalid cast` on a non-Dictionary `pending_battle`,
## returning false with the adopter already half-rebuilt: the donor's map under
## the adopter's own stale battle, still in flight, still inside the hash. A
## caller that checked the return value and refused to proceed was still holding
## a state that never existed on any peer.
func _test_a_refused_restore_changes_nothing() -> void:
	var donor := _staged_state()
	donor.advance_turn()
	donor.transfer_region("Dunmarch", ATTACKER)
	donor.begin_battle(_commitment_for(donor))
	var poisoned_state := donor.authoritative_state().duplicate(true)
	poisoned_state["pending_battle"] = "not a dictionary"

	var adopter := _staged_state()
	adopter.begin_battle(_commitment_for(adopter))
	var before_hash := adopter.state_hash()
	var before_turn := adopter.turn_index
	var before_battle: Dictionary = adopter.pending_battle.duplicate(true)
	var before_owner := adopter.owner_of("Dunmarch")

	_check("a_snapshot_with_a_malformed_battle_is_refused",
		not adopter.restore(var_to_bytes(poisoned_state)))
	_check("a_refused_restore_leaves_the_turn_index_alone",
		adopter.turn_index == before_turn, "%d vs %d" % [adopter.turn_index, before_turn])
	_check("a_refused_restore_leaves_the_region_map_alone",
		adopter.owner_of("Dunmarch") == before_owner)
	_check("a_refused_restore_leaves_the_adopters_own_battle_alone",
		adopter.pending_battle == before_battle, str(adopter.pending_battle))
	# The hash is the whole claim: a refused restore must be indistinguishable
	# from one that was never attempted.
	_check("a_refused_restore_leaves_the_state_hash_alone",
		adopter.state_hash() == before_hash,
		"%s vs %s" % [adopter.state_hash(), before_hash])

	# The same guard must not refuse legitimate snapshots.
	var honest := _staged_state()
	honest.begin_battle(_commitment_for(honest))
	_check("a_well_formed_mid_battle_snapshot_still_restores",
		adopter.restore(honest.snapshot()) and adopter.state_hash() == honest.state_hash())

	# A malformed row anywhere else refuses just as atomically.
	var wrong_players := _staged_state().authoritative_state().duplicate(true)
	wrong_players["players"] = "not an array"
	var guarded := _staged_state()
	var guarded_hash := guarded.state_hash()
	_check("a_snapshot_with_a_malformed_player_list_is_refused",
		not guarded.restore(var_to_bytes(wrong_players)))
	_check("that_refusal_is_atomic_too", guarded.state_hash() == guarded_hash)


# --- begin_battle validates what it puts inside the hash ---------------------

## The chain guarantee used to be worth exactly as much as caller discipline:
## `begin_battle({"region": "Cinderfen", "junk": true})` returned true and put an
## ad-hoc dictionary inside the strategic hash, with no schema, no sides and no
## `brief_digest` to check any tactical configuration against.
func _test_begin_battle_validates_the_commitment() -> void:
	var state := _staged_state()
	_check("an_ad_hoc_dictionary_naming_a_real_region_is_refused",
		not state.begin_battle({"region": TARGET_REGION, "junk": true}))
	_check("a_refused_commitment_starts_no_battle", state.pending_battle.is_empty())
	_check("a_refused_commitment_stays_out_of_the_hash",
		not state.authoritative_state().has("pending_battle"))

	var good := _commitment_for(state)

	var wrong_schema := good.duplicate(true)
	wrong_schema["schema"] = "openbfme.something-else"
	_check("a_foreign_schema_is_refused", not state.begin_battle(wrong_schema))

	var wrong_version := good.duplicate(true)
	wrong_version["schema_version"] = 99
	_check("an_unsupported_schema_version_is_refused", not state.begin_battle(wrong_version))

	var missing := good.duplicate(true)
	missing.erase("brief_digest")
	_check("a_commitment_missing_the_chain_link_is_refused", not state.begin_battle(missing))

	var mistyped := good.duplicate(true)
	mistyped["committed_armies"] = "one army, honest"
	_check("a_mistyped_field_is_refused", not state.begin_battle(mistyped))

	var extra := good.duplicate(true)
	extra["smuggled"] = 1
	# An unknown field would ride into the hash uninspected, which is the whole
	# class of defect the hash exists to catch.
	_check("an_unknown_field_is_refused", not state.begin_battle(extra))

	var bad_digest := good.duplicate(true)
	bad_digest["brief_digest"] = "not a sha256"
	_check("a_malformed_brief_digest_is_refused", not state.begin_battle(bad_digest))

	var empty_force := good.duplicate(true)
	empty_force["committed_armies"] = PackedInt32Array()
	_check("a_commitment_that_commits_nothing_is_refused",
		not state.begin_battle(empty_force))

	var no_ground := good.duplicate(true)
	no_ground["battlefield_map"] = ""
	_check("a_commitment_with_no_battlefield_is_refused",
		not state.begin_battle(no_ground))

	var same_side := good.duplicate(true)
	same_side["defender"] = int(same_side["attacker"])
	_check("a_commitment_seating_one_player_on_both_sides_is_refused",
		not state.begin_battle(same_side))

	var unseated := good.duplicate(true)
	unseated["defender"] = 9
	_check("a_commitment_naming_an_unseated_seat_is_refused",
		not state.begin_battle(unseated))

	_check("none_of_those_refusals_started_a_battle", state.pending_battle.is_empty())
	_check("a_real_commitment_is_still_admitted", state.begin_battle(good))


# --- the defeated side dies in both branches ---------------------------------

## `defending_armies` was written by `configure()` and read by nothing. On an
## attacker victory the region changed hands and the defeated garrison stayed
## exactly where it was: inside a region its owner no longer held, still
## answering to the defender, free to march away on the next command. The
## defender-win branch destroyed the losers; the attacker-win branch did not.
func _test_the_defeated_garrison_does_not_survive_the_capture() -> void:
	var state := _staged_state()
	var configured := _configured_for(state)
	var commitment := configured["commitment"] as Dictionary
	var defending: PackedInt32Array = commitment["defending_armies"]
	_check("the_commitment_records_a_defending_garrison", defending.size() > 0,
		str(defending))
	state.begin_battle(commitment)

	var outcome: Dictionary = BattleScript.apply_outcome(state, BattleScript.ATTACKER_TEAM)
	_check("the_capture_applies", bool(outcome["ok"]), str(outcome["refusals"]))
	_check("the_defeated_garrison_is_gone_from_the_world",
		not state.armies.has(int(defending[0])), str(state.armies.keys()))
	_check("the_outcome_reports_the_defeated_garrison_as_lost",
		Array(outcome["armies_lost"] as PackedInt32Array) == Array(defending),
		str(outcome["armies_lost"]))
	# The symptom the review named: a ghost that still answers to the defender
	# and can march out of a region the defender no longer owns.
	for army_id in state.armies_in_region(TARGET_REGION):
		_check("no_defender_owned_army_stands_in_the_captured_region",
			int((state.armies[int(army_id)] as Dictionary)["owner"]) == ATTACKER,
			"army %d" % int(army_id))
	_check("the_transaction_closed_after_the_capture", state.pending_battle.is_empty())

	# The defender-win branch is unchanged and still asymmetric in the other
	# direction - the DEFENDING garrison survives a successful defence, which is
	# correct: it was not defeated.
	var held := _staged_state()
	var held_configured := _configured_for(held)
	var held_defending: PackedInt32Array = (held_configured["commitment"] as Dictionary)["defending_armies"]
	held.begin_battle(held_configured["commitment"])
	BattleScript.apply_outcome(held, BattleScript.DEFENDER_TEAM)
	_check("a_successful_defence_leaves_the_defending_garrison_standing",
		held.armies.has(int(held_defending[0])))


# --- the documented partial-advance path, actually exercised -----------------

## `apply_outcome`'s header promises that a capture whose committed force trips
## the destination's command-point cap is REPORTED rather than rolled back or
## hidden: `captured` true, the army absent from `armies_advanced`, `refusals`
## naming it, `ok` false, transaction closed. Nothing exercised that branch.
##
## The fixture is a Cinderfen with a command-point cap of zero - an authored 0,
## not the unauthored -1 sentinel that falls back to the retail default - so the
## single committed army cannot legally arrive.
func _test_a_partial_advance_is_reported_not_hidden() -> void:
	var document := _document()
	for region in (document["regionCampaigns"] as Array)[0]["regions"] as Array:
		if String((region as Dictionary)["id"]) == TARGET_REGION:
			(region as Dictionary)["cpLimit"] = 0
	var capped_world := WorldScript.new()
	capped_world.load_from_dict(document, "TestCampaign")
	var state := _staged_state_in(capped_world)
	_check("the_capped_region_reports_a_zero_command_point_cap",
		capped_world.region_cp_limit(TARGET_REGION) == 0)

	var configured := _configured_for(state)
	var committed: PackedInt32Array = (configured["commitment"] as Dictionary)["committed_armies"]
	state.begin_battle(configured["commitment"])
	var outcome: Dictionary = BattleScript.apply_outcome(state, BattleScript.ATTACKER_TEAM)

	_check("a_partial_advance_reports_not_ok", not bool(outcome["ok"]))
	_check("a_partial_advance_still_captured_the_region", bool(outcome["captured"]))
	_check("the_region_really_did_change_hands", state.owner_of(TARGET_REGION) == ATTACKER)
	_check("the_refusal_names_the_army_that_could_not_advance",
		_refusal_mentions(outcome, "army %d" % int(committed[0])), str(outcome["refusals"]))
	_check("the_army_that_could_not_advance_is_absent_from_armies_advanced",
		(outcome["armies_advanced"] as PackedInt32Array).is_empty(),
		str(outcome["armies_advanced"]))
	_check("the_army_is_still_standing_where_it_started",
		String((state.armies[int(committed[0])] as Dictionary)["region"]) == "Bramblewold")
	# Reported, not rolled back, and never left open: a caller polls
	# pending_battle to know whether it may act.
	_check("a_partial_advance_still_closes_the_transaction",
		state.pending_battle.is_empty())


# --- the staged feed: retail's SecondsPerReinforcement, honoured --------------

## Retail feeds a WOTR army into a tactical battle ONE UNIT AT A TIME,
## `SecondsPerReinforcement` apart - the RotWK living-world document authors
## 300 in every campaign's `rtsSettings`, the BFME2-era fixture value here is
## 900. The slice used to spawn the whole roster at match start, which is what
## the `reinforcement_schedule` gap in `wotr_handoff.gd` names. The bridge now
## derives the exact feed from the brief, so a launcher has a schedule to obey
## instead of a roster to dump - and the sim-side consumption is the remaining
## half of that gap, not the whole of it.
func _test_the_reinforcement_feed_is_staged_from_retails_cadence() -> void:
	var state := _staged_state()
	var configured := _configured_for(state)
	var feed := configured["reinforcement_feed"] as Dictionary
	_check("a_configured_match_carries_a_staged_reinforcement_feed",
		bool(feed.get("ok", false)), str(feed.get("refusals", [])))
	_check("the_cadence_is_the_documents_own_SecondsPerReinforcement",
		int(feed.get("seconds_per_reinforcement", -1)) == 900,
		str(feed.get("seconds_per_reinforcement", -1)))
	var attacker_entries := feed["attacker"] as Array
	var defender_entries := feed["defender"] as Array
	_check("every_unit_of_both_sides_is_in_the_feed",
		attacker_entries.size() == 1 and defender_entries.size() == 3,
		"%d vs %d" % [attacker_entries.size(), defender_entries.size()])
	var arrivals: Array[int] = []
	for entry in defender_entries:
		arrivals.append(int((entry as Dictionary)["arrival_seconds"]))
	_check("units_arrive_one_at_a_time_a_full_cadence_apart",
		arrivals == [0, 900, 1800], str(arrivals))
	# Retail's own auto-resolve schedule pins army 1 to round 0 - "do not
	# change this, or battles will end immediately" - and the same reasoning
	# holds here: the battlefield is never empty while a feed is pending.
	_check("the_battle_starts_manned_rather_than_empty",
		int((attacker_entries[0] as Dictionary)["arrival_seconds"]) == 0
			and int((defender_entries[0] as Dictionary)["arrival_seconds"]) == 0)
	var commitment := configured["commitment"] as Dictionary
	var committed: PackedInt32Array = commitment["committed_armies"]
	var defending: PackedInt32Array = commitment["defending_armies"]
	var identities_hold := String((attacker_entries[0] as Dictionary)["template"]) == "TestHero" \
		and int((attacker_entries[0] as Dictionary)["army_id"]) == int(committed[0])
	for entry in defender_entries:
		if int((entry as Dictionary)["army_id"]) != int(defending[0]):
			identities_hold = false
	_check("every_entry_names_the_army_and_template_it_stages", identities_hold)

	# DETERMINISM: an independently rebuilt state configures the identical feed.
	var again := _configured_for(_staged_state())["reinforcement_feed"] as Dictionary
	_check("two_peers_of_the_same_state_stage_the_identical_feed",
		StateScript.canonical_digest(feed) == StateScript.canonical_digest(again))

	# THE VALUE FLOWS FROM THE DOCUMENT, NEVER FROM A CONSTANT. Authoring
	# retail's own RotWK cadence (300) must move every arrival with it.
	var rotwk := _document()
	(rotwk["rtsSettings"] as Dictionary)["secondsPerReinforcement"] = 300
	var rotwk_world := WorldScript.new()
	rotwk_world.load_from_dict(rotwk, "TestCampaign")
	var rotwk_feed := (_configured_for(
		_staged_state_in(rotwk_world))["reinforcement_feed"]) as Dictionary
	var rotwk_arrivals: Array[int] = []
	for entry in rotwk_feed["defender"] as Array:
		rotwk_arrivals.append(int((entry as Dictionary)["arrival_seconds"]))
	_check("a_document_authoring_retails_300_second_cadence_moves_every_arrival",
		int(rotwk_feed.get("seconds_per_reinforcement", -1)) == 300
			and rotwk_arrivals == [0, 300, 600],
		str(rotwk_arrivals))

	# UNAUTHORED IS NOT A DEFAULT. -1 is the world reader's sentinel; the feed
	# refuses BY NAME while the configuration stays usable, because an
	# auto-resolved battle needs no feed at all.
	var silent := _document()
	(silent["rtsSettings"] as Dictionary)["secondsPerReinforcement"] = -1
	var silent_world := WorldScript.new()
	silent_world.load_from_dict(silent, "TestCampaign")
	var silent_config := _configured_for(_staged_state_in(silent_world))
	var silent_feed := silent_config["reinforcement_feed"] as Dictionary
	_check("an_unauthored_cadence_still_configures_the_match",
		bool(silent_config["ok"]), str(silent_config["refusals"]))
	_check("an_unauthored_cadence_refuses_the_feed_rather_than_inventing_one",
		not bool(silent_feed.get("ok", false)))
	_check("the_feed_refusal_names_SecondsPerReinforcement",
		_refusal_mentions(silent_feed, "SecondsPerReinforcement"),
		str(silent_feed.get("refusals", [])))

	# A NO-FORT REGION authorises no prebuilt fortress: the overlay key is
	# ABSENT, not false - empty-is-absent, the discipline the whole hash uses.
	var fortless := _document()
	for region in (fortless["regionCampaigns"] as Array)[0]["regions"] as Array:
		(region as Dictionary)["createAutoFort"] = false
	var fortless_world := WorldScript.new()
	fortless_world.load_from_dict(fortless, "TestCampaign")
	var fortless_rules := (_configured_for(
		_staged_state_in(fortless_world))["gameplay_rules"]) as Dictionary
	_check("a_region_without_a_fort_authorises_no_prebuilt_fortress",
		not fortless_rules.has("prebuilt_fortress"), str(fortless_rules.keys()))

	# AN ARMY WITH NO UNIT RECORDS refuses by name - a missing binding must
	# never be staged around as though the army were empty on purpose.
	var unbound_feed := (_configured_for(
		_staged_state_without_units())["reinforcement_feed"]) as Dictionary
	_check("an_army_with_no_unit_records_refuses_the_feed_by_name",
		not bool(unbound_feed.get("ok", false))
			and _refusal_mentions(unbound_feed, "fields no unit records"),
		str(unbound_feed.get("refusals", [])))

	# THE CURSOR-FREE POLL: due() answers from the caller's own fed-count, so
	# two launchers polling at different rates cannot disagree about which unit
	# comes next.
	_check("due_returns_exactly_the_entries_whose_time_has_come",
		ReinforcementsScript.due(defender_entries, 900, 0).size() == 2
			and ReinforcementsScript.due(defender_entries, 900, 2).is_empty()
			and ReinforcementsScript.due(defender_entries, 1799, 1).size() == 1
			and int((ReinforcementsScript.due(defender_entries, 1799, 1)[0]
				as Dictionary)["feed_index"]) == 1)

	# THE WAY HOME: a feed entry plus the tactical layer's own strength
	# fraction becomes a strategic survivor row, integer end to end.
	var survivors := ReinforcementsScript.survivors_from_feed(defender_entries, {0: 500})
	_check("a_fed_unit_returns_as_a_strategic_survivor_row",
		survivors.size() == 1
			and int((survivors[0] as Dictionary)["hitpoints_milli"]) == 150000
			and int((survivors[0] as Dictionary)["army_id"]) == int(defending[0]),
		str(survivors))


# --- the fought battle comes home as a roster, not a boolean -------------------

## `tactical_battle_outcome_report`: a fought battle used to come home as
## `apply_outcome(winner_team)`, a boolean - so the winner's army returned
## untouched and the loser's vanished, two fidelities depending on which
## resolver ran. `apply_fought_outcome` takes the SURVIVING ROSTER, the exact
## contract auto-resolve already settles under, through the same settlement
## code, so the strategic consequences of a battle cannot depend on how it was
## decided.
func _test_a_fought_battle_reports_the_surviving_roster() -> void:
	var state := _staged_state()
	var configured := _configured_for(state)
	var commitment := configured["commitment"] as Dictionary
	var committed: PackedInt32Array = commitment["committed_armies"]
	var defending: PackedInt32Array = commitment["defending_armies"]
	state.begin_battle(commitment)

	# The attacker wins, wounded: the hero came through on half hitpoints and
	# the whole garrison died.
	var hero := ((state.armies[int(committed[0])] as Dictionary)["units"][0]
		as Dictionary).duplicate(true)
	hero["hitpoints_milli"] = 400000
	var outcome: Dictionary = BattleScript.apply_fought_outcome(
		state, BattleScript.ATTACKER_TEAM, [hero])
	_check("a_fought_victory_with_survivors_applies",
		bool(outcome["ok"]), str(outcome["refusals"]))
	_check("fought_the_region_changed_hands", state.owner_of(TARGET_REGION) == ATTACKER)
	_check("fought_the_wound_came_home_with_the_army",
		int(((state.armies[int(committed[0])] as Dictionary)["units"][0]
			as Dictionary)["hitpoints_milli"]) == 400000)
	_check("fought_the_survivors_advanced_into_the_region",
		Array(outcome["armies_advanced"] as PackedInt32Array) == [int(committed[0])],
		str(outcome["armies_advanced"]))
	_check("fought_the_reduced_and_lost_armies_are_reported_apart",
		Array(outcome["armies_reduced"] as PackedInt32Array) == [int(committed[0])]
			and Array(outcome["armies_lost"] as PackedInt32Array) == [int(defending[0])],
		"%s / %s" % [str(outcome["armies_reduced"]), str(outcome["armies_lost"])])
	_check("fought_the_defeated_garrison_is_gone", not state.armies.has(int(defending[0])))
	_check("fought_the_transaction_closed", state.pending_battle.is_empty())

	# A defence that HOLDS no longer wipes the attacker: the beaten army limps
	# home reduced, which the boolean path could never say.
	var held := _staged_state()
	var held_configured := _configured_for(held)
	var held_commitment := held_configured["commitment"] as Dictionary
	var held_committed: PackedInt32Array = held_commitment["committed_armies"]
	var held_defending: PackedInt32Array = held_commitment["defending_armies"]
	held.begin_battle(held_commitment)
	var beaten := ((held.armies[int(held_committed[0])] as Dictionary)["units"][0]
		as Dictionary).duplicate(true)
	beaten["hitpoints_milli"] = 250000
	var report: Array = [beaten]
	for unit in (held.armies[int(held_defending[0])] as Dictionary)["units"] as Array:
		report.append((unit as Dictionary).duplicate(true))
	var held_outcome: Dictionary = BattleScript.apply_fought_outcome(
		held, BattleScript.DEFENDER_TEAM, report)
	_check("a_fought_defence_applies", bool(held_outcome["ok"]), str(held_outcome["refusals"]))
	_check("fought_a_beaten_army_survives_reduced_rather_than_vanishing",
		held.armies.has(int(held_committed[0]))
			and int(((held.armies[int(held_committed[0])] as Dictionary)["units"][0]
				as Dictionary)["hitpoints_milli"]) == 250000)
	_check("fought_the_region_did_not_move", held.owner_of(TARGET_REGION) == DEFENDER)
	_check("fought_the_garrison_still_stands_at_full_strength",
		((held.armies[int(held_defending[0])] as Dictionary)["units"] as Array).size() == 3)

	# Refusals: an undecided match, a smuggled army, and a spent transaction.
	var guarded := _staged_state()
	guarded.begin_battle(_commitment_for(guarded))
	var before := guarded.state_hash()
	_check("a_fought_undecided_refuses",
		not bool((BattleScript.apply_fought_outcome(
			guarded, BattleScript.UNDECIDED, []) as Dictionary)["ok"]))
	var smuggled: Dictionary = BattleScript.apply_fought_outcome(
		guarded, BattleScript.ATTACKER_TEAM,
		[{"army_id": 99, "template": "Smuggler", "hitpoints_milli": 1000}])
	_check("a_survivor_naming_an_army_outside_the_battle_refuses_by_name",
		not bool(smuggled["ok"]) and _refusal_mentions(smuggled, "99"),
		str(smuggled["refusals"]))
	_check("a_refused_report_changes_nothing",
		guarded.state_hash() == before and not guarded.pending_battle.is_empty())
	# An honest walkover: no survivors at all still captures, exactly as an
	# auto-resolved walkover does - and nothing advances, because nothing lived.
	var walkover: Dictionary = BattleScript.apply_fought_outcome(
		guarded, BattleScript.ATTACKER_TEAM, [])
	_check("a_victory_with_no_survivors_still_captures_the_region",
		bool(walkover["ok"]) and bool(walkover["captured"])
			and guarded.owner_of(TARGET_REGION) == ATTACKER
			and (walkover["armies_advanced"] as PackedInt32Array).is_empty(),
		str(walkover))
	_check("a_spent_transaction_refuses_a_second_report",
		not bool((BattleScript.apply_fought_outcome(
			guarded, BattleScript.ATTACKER_TEAM, []) as Dictionary)["ok"]))


# --- strategic fixture -------------------------------------------------------

## PlayerAlpha (seat 0) holds Ashfall+Bramblewold with a hero army; PlayerBeta
## (seat 1) holds Cinderfen+Dunmarch. The hero army is walked forward into
## Bramblewold so that seat 0 can legally stage an attack on Cinderfen, and a
## garrison is placed in Cinderfen so the defence is a real one.
func _staged_state() -> StateScript:
	return _staged_state_in(_world())


func _staged_state_in(world: WorldScript) -> StateScript:
	return _staged_state_seated(world, StateScript.CONTROLLER_HUMAN, StateScript.CONTROLLER_AI)


## The same fixture with the seats' CONTROLLERS chosen. Who drives a seat is
## authoritative strategic state now, not a per-session argument, so the only way
## to vary it is here.
func _staged_state_seated(
	world: WorldScript,
	attacker_controller: String,
	defender_controller: String
) -> StateScript:
	var state := StateScript.new()
	state.setup(world, [
		{"template": "PlayerAlpha", "team": 1, "controller": attacker_controller},
		{"template": "PlayerBeta", "team": 2, "controller": defender_controller},
	])
	# BEFORE any army is placed, exactly as `wotr_session.begin()` orders it,
	# because `place_army()` copies the unit records onto the army at placement.
	state.roster_units = ROSTER_UNITS
	state.apply_ownership_sets("TestScenario")
	var hero := state.armies_in_region("Ashfall")
	if hero.is_empty() or not state.move_army(int(hero[0]), "Bramblewold"):
		printerr("WOTR_BATTLE FAIL fixture could not stage the attacking army")
	if state.place_army(DEFENDER, TARGET_REGION, "GarrisonArmy1") < 0:
		printerr("WOTR_BATTLE FAIL fixture could not place the defending garrison")
	return state


## The same fixture with NO roster_units table bound - the state a session
## reaches when the binding bundle is absent. Armies then carry no unit
## records, which the staged feed must refuse BY NAME rather than staging
## around.
func _staged_state_without_units() -> StateScript:
	var state := StateScript.new()
	state.setup(_world(), [
		{"template": "PlayerAlpha", "team": 1, "controller": StateScript.CONTROLLER_HUMAN},
		{"template": "PlayerBeta", "team": 2, "controller": StateScript.CONTROLLER_AI},
	])
	state.apply_ownership_sets("TestScenario")
	var hero := state.armies_in_region("Ashfall")
	if hero.is_empty() or not state.move_army(int(hero[0]), "Bramblewold"):
		printerr("WOTR_BATTLE FAIL fixture could not stage the unbound attacking army")
	if state.place_army(DEFENDER, TARGET_REGION, "GarrisonArmy1") < 0:
		printerr("WOTR_BATTLE FAIL fixture could not place the unbound garrison")
	return state


func _configured_for(state: StateScript) -> Dictionary:
	var brief := HandoffScript.build_request(state.world, state, ATTACKER, TARGET_REGION)
	return BattleScript.configure(brief, FACTION_BINDINGS, MAP_BINDINGS)


func _commitment_for(state: StateScript) -> Dictionary:
	return _configured_for(state)["commitment"] as Dictionary


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
